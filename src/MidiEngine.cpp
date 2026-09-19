#include "MidiEngine.h"
#include <QTimer>
#include <algorithm>

#if defined(Q_OS_WIN)
#include "backends/WinMidiBackend.h"
#elif defined(Q_OS_MAC)
#include "backends/CoreMidiBackend.h"
#endif

MidiEngine::MidiEngine(QObject *parent)
    : QObject(parent)
{
#if defined(Q_OS_WIN)
    m_backend = std::make_unique<WinMidiBackend>();
#elif defined(Q_OS_MAC)
    m_backend = std::make_unique<CoreMidiBackend>();
#endif
    if (m_backend)
        m_backend->setMessageCallback([this](quint32 packed) { onShortMessage(packed); });
    {
        std::unique_lock<std::shared_mutex> lock(m_tableMutex);
        m_table = std::make_shared<const MappingTable>();
    }
}

MidiEngine::~MidiEngine()
{
    stop();
}

QStringList MidiEngine::inputDevices()
{
#if defined(Q_OS_WIN)
    const WinMidiBackend backend;
    return backend.inputDevices();
#elif defined(Q_OS_MAC)
    const CoreMidiBackend backend;
    return backend.inputDevices();
#else
    return {};
#endif
}

QStringList MidiEngine::outputDevices()
{
#if defined(Q_OS_WIN)
    const WinMidiBackend backend;
    return backend.outputDevices();
#elif defined(Q_OS_MAC)
    const CoreMidiBackend backend;
    return backend.outputDevices();
#else
    return {};
#endif
}

bool MidiEngine::start(const QString &inputName, const QString &outputName)
{
    stop();

    QString error;
    // 先开输出，保证第一条回调消息就能被立即转发
    if (!m_backend->openOutput(outputName, &error)) {
        emit errorOccurred(error);
        return false;
    }
    if (!m_backend->openInput(inputName, &error)) {
        emit errorOccurred(error);
        return false;
    }

    m_running.store(true, std::memory_order_release);
    emit stateChanged();
    return true;
}

void MidiEngine::stop()
{
    if (!m_backend)
        return;
    m_backend->closeInput();
    flushActiveNotes();
    m_backend->closeOutput();
    if (m_running.exchange(false, std::memory_order_acq_rel))
        emit stateChanged();
}

void MidiEngine::applyTable(std::shared_ptr<const MappingTable> table)
{
    if (!table)
        return;
    flushActiveNotes();  // 旧映射下已按下的音符先补发 Note Off
    std::unique_lock<std::shared_mutex> lock(m_tableMutex);
    m_table = std::move(table);
}

void MidiEngine::panic()
{
    flushActiveNotes();
    for (int ch = 0; ch < 16; ++ch) {
        sendShort(quint8(0xB0 | ch), 123, 0);  // All Notes Off
        sendShort(quint8(0xB0 | ch), 120, 0);  // All Sound Off
    }
}

void MidiEngine::sendTestNote()
{
    if (!m_backend->outputOpen())
        return;
    sendShort(0x90, 36, 100);
    QTimer::singleShot(300, this, [this] { sendShort(0x80, 36, 0); });
}

void MidiEngine::onShortMessage(quint32 packed)
{
    const quint8 status = quint8(packed & 0xFF);
    const quint8 d1 = quint8((packed >> 8) & 0xFF);
    const quint8 d2 = quint8((packed >> 16) & 0xFF);
    if (status < 0x80 || status >= 0xF0)  // 非法或系统实时消息：不处理
        return;

    const quint8 type = status & 0xF0;
    const quint8 ch = status & 0x0F;
    std::shared_ptr<const MappingTable> table;
    {
        std::shared_lock<std::shared_mutex> lock(m_tableMutex);
        table = m_table;   // 共享读，锁内仅复制指针
    }

    if (type == 0x90 || type == 0x80) {
        if (d1 >= 128)
            return;
        const bool isOn = (type == 0x90 && d2 > 0);
        emit padActivity(d1, d2, isOn);

        if (isOn) {
            const PadMapping &m = table->notes[d1];
            if (m.enabled && m.muted) { countDropped(); return; }
            if (!m.enabled && !table->passthrough) { countDropped(); return; }
            const quint8 outNote = m.enabled ? m.target : d1;
            const quint8 outCh = (m.enabled && m.channel > 0) ? quint8(m.channel - 1) : ch;
            sendShort(quint8(type | outCh), outNote, d2);
            std::lock_guard<std::mutex> lock(m_activeMutex);
            eraseActiveLocked(ch, d1);
            m_active.push_back(ActiveNote{ch, d1, outCh, outNote});
        } else {
            quint8 outCh = ch;
            quint8 outNote = d1;
            bool known = false;
            {
                std::lock_guard<std::mutex> lock(m_activeMutex);
                // 用按下时记录的目标发 Note Off，避免映射中途被改导致卡音
                for (auto it = m_active.begin(); it != m_active.end(); ++it) {
                    if (it->channel == ch && it->srcNote == d1) {
                        outCh = it->outChannel;
                        outNote = it->outNote;
                        m_active.erase(it);
                        known = true;
                        break;
                    }
                }
            }
            if (!known) {
                const PadMapping &m = table->notes[d1];
                if (m.enabled && m.muted) { countDropped(); return; }
                if (!m.enabled && !table->passthrough) { countDropped(); return; }
                if (m.enabled) {
                    outNote = m.target;
                    if (m.channel > 0)
                        outCh = quint8(m.channel - 1);
                }
            }
            sendShort(quint8(0x80 | outCh), outNote, 0);
        }
        return;
    }

    // 其他通道消息（CC / 触后 / 弯音 / 程序更换）：按全局透传规则处理
    if (table->passthrough)
        sendShort(status, d1, d2);
    else
        countDropped();
}

void MidiEngine::sendShort(quint8 status, quint8 d1, quint8 d2)
{
    if (!m_backend || !m_backend->outputOpen()) {
        countDropped();
        return;
    }
    m_backend->sendShort(status, d1, d2);
    m_forwarded.fetch_add(1, std::memory_order_relaxed);
}

void MidiEngine::eraseActiveLocked(quint8 channel, quint8 srcNote)
{
    m_active.erase(std::remove_if(m_active.begin(), m_active.end(),
                                  [channel, srcNote](const ActiveNote &n) {
                                      return n.channel == channel && n.srcNote == srcNote;
                                  }),
                   m_active.end());
}

void MidiEngine::flushActiveNotes()
{
    std::vector<ActiveNote> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_activeMutex);
        snapshot.swap(m_active);
    }
    for (const ActiveNote &n : snapshot)
        sendShort(quint8(0x80 | n.outChannel), n.outNote, 0);
}
