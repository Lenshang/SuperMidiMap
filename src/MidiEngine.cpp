#include "MidiEngine.h"
#include "MidiClip.h"
#include <QTimer>
#include <algorithm>
#include <cmath>

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
    // 片段播放线程：常驻，空闲时 1ms 轮询，开销可忽略
    m_playbackRun.store(true, std::memory_order_release);
    m_playbackThread = std::thread([this] { playbackLoop(); });
}

MidiEngine::~MidiEngine()
{
    stop();
    m_playbackRun.store(false, std::memory_order_release);
    if (m_playbackThread.joinable())
        m_playbackThread.join();
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
    stopAllClips();
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
    stopAllClips();
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

        // MIDI 片段垫：按住开始播放，松开停止；触发音符本身不转发
        if (isOn && table->clipFor[d1] >= 0) {
            startClip(d1);
            return;
        }
        if (!isOn) {
            if (stopClip(d1))
                return;                       // 有播放实例被停止（已补发 Note Off）
            if (table->notes[d1].mode == 1)
                return;                       // 片段垫但无实例（如刚重连）：吞掉
        }

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

// ---- MIDI 片段播放 ----

void MidiEngine::startClip(quint8 sourceNote)
{
    std::shared_ptr<const MappingTable> table;
    {
        std::shared_lock<std::shared_mutex> lock(m_tableMutex);
        table = m_table;
    }
    if (!table)
        return;
    const qint8 idx = table->clipFor[sourceNote];
    if (idx < 0 || idx >= int(table->clips.size()))
        return;
    const auto &clip = table->clips[idx];
    if (!clip || !clip->isValid())
        return;

    ClipInstance inst;
    inst.table = table;
    inst.clipIndex = idx;
    inst.baseChannel = table->notes[sourceNote].channel;
    inst.loop = table->notes[sourceNote].loop;
    inst.startTime = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(m_clipsMutex);
    m_clips[sourceNote] = std::move(inst);  // 若已在播放则从头开始
}

bool MidiEngine::stopClip(quint8 sourceNote)
{
    std::vector<std::pair<quint8, quint8>> offs;
    {
        std::lock_guard<std::mutex> lock(m_clipsMutex);
        const auto it = m_clips.find(sourceNote);
        if (it == m_clips.end())
            return false;
        offs.swap(it->second.sounding);
        m_clips.erase(it);
    }
    for (const auto &n : offs)  // 补发 Note Off，防止卡音
        sendShort(quint8(0x80 | n.first), n.second, 0);
    return true;
}

void MidiEngine::stopAllClips()
{
    std::vector<std::pair<quint8, quint8>> offs;
    {
        std::lock_guard<std::mutex> lock(m_clipsMutex);
        for (auto &kv : m_clips) {
            for (const auto &n : kv.second.sounding)
                offs.push_back(n);
            kv.second.sounding.clear();
        }
        m_clips.clear();
    }
    for (const auto &n : offs)
        sendShort(quint8(0x80 | n.first), n.second, 0);
}

void MidiEngine::playbackLoop()
{
#ifdef Q_OS_WIN
    timeBeginPeriod(1);   // 保证 1ms 休眠粒度
#endif
    while (m_playbackRun.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        std::vector<std::pair<quint8, quint8>> autoStopOffs;
        std::vector<quint8> autoStopKeys;
        {
            std::lock_guard<std::mutex> lock(m_clipsMutex);
            if (m_clips.empty())
                continue;
            const auto now = std::chrono::steady_clock::now();

            for (auto &kv : m_clips) {
                ClipInstance &inst = kv.second;
                const auto &clip = inst.table->clips[inst.clipIndex];
                if (!clip || clip->lengthMs <= 0 || clip->events.isEmpty())
                    continue;

                const double elapsedMs = std::chrono::duration<double, std::milli>(
                                             now - inst.startTime).count();
                double pos = elapsedMs;
                if (inst.loop)
                    pos = std::fmod(elapsedMs, clip->lengthMs);

                auto emitRange = [&](double from, double to) {
                    while (inst.eventPos < size_t(clip->events.size())) {
                        const ClipEvent &ev = clip->events.at(int(inst.eventPos));
                        if (ev.timeMs >= to)
                            break;
                        if (ev.timeMs >= from) {
                            const quint8 evCh = ev.status & 0x0F;
                            const quint8 ch = inst.baseChannel ? quint8(inst.baseChannel - 1) : evCh;
                            const quint8 st = quint8((ev.status & 0xF0) | ch);
                            sendShort(st, ev.note, ev.vel);
                            if ((st & 0xF0) == 0x90 && ev.vel > 0)
                                inst.sounding.push_back({ch, ev.note});
                            else if ((st & 0xF0) == 0x80)
                                inst.sounding.erase(
                                    std::remove(inst.sounding.begin(), inst.sounding.end(),
                                                std::make_pair(ch, ev.note)),
                                    inst.sounding.end());
                        }
                        ++inst.eventPos;
                    }
                };

                if (pos < inst.lastPosMs) {   // 循环回绕：先播完尾部再从头
                    emitRange(inst.lastPosMs, clip->lengthMs + 1.0);
                    inst.eventPos = 0;
                    emitRange(0.0, pos + 1.0);
                } else {
                    emitRange(inst.lastPosMs, pos + 1.0);
                }
                inst.lastPosMs = pos;

                if (!inst.loop && elapsedMs >= clip->lengthMs) {  // 非循环播放完毕
                    autoStopOffs.insert(autoStopOffs.end(),
                                        inst.sounding.begin(), inst.sounding.end());
                    inst.sounding.clear();
                    autoStopKeys.push_back(kv.first);
                }
            }
            for (const quint8 key : autoStopKeys)
                m_clips.erase(key);
        }
        for (const auto &n : autoStopOffs)
            sendShort(quint8(0x80 | n.first), n.second, 0);
    }
#ifdef Q_OS_WIN
    timeEndPeriod(1);
#endif
}
