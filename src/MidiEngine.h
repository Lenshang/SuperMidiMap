#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "backends/MidiBackend.h"
#include "MappingTable.h"

class MidiBackend;

// 低延迟 MIDI 翻译引擎（平台无关）。
// 平台相关的收发由 MidiBackend 后端实现（Windows: WinMM / macOS: CoreMIDI）。
//
// 数据路径：驱动回调线程收到消息 -> 查表翻译 -> 同一线程立即发出。
// 全程无队列、无缓冲、不进 GUI 线程，附加延迟约 1ms 以内。
// GUI 只通过队列信号收到"哪个垫亮了"的通知。
class MidiEngine : public QObject
{
    Q_OBJECT
public:
    explicit MidiEngine(QObject *parent = nullptr);
    ~MidiEngine() override;

    static QStringList inputDevices();
    static QStringList outputDevices();

    bool start(const QString &inputName, const QString &outputName);
    void stop();
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

    // 原子替换翻译表；替换前会为所有已按下的音符补发 Note Off，防止卡音
    void applyTable(std::shared_ptr<const MappingTable> table);

    void panic();           // 关闭所有音符（含翻译表外发出的）
    void sendTestNote();    // 向输出设备直接发送一个测试音符

    quint64 messagesForwarded() const { return m_forwarded.load(std::memory_order_relaxed); }
    quint64 messagesDropped() const { return m_dropped.load(std::memory_order_relaxed); }

signals:
    void padActivity(int sourceNote, int velocity, bool isOn);
    void stateChanged();
    void errorOccurred(const QString &message);

private:
    struct ActiveNote
    {
        quint8 channel;     // 源通道
        quint8 srcNote;     // 源音符号
        quint8 outChannel;  // 按下时实际发出的通道
        quint8 outNote;     // 按下时实际发出的音符号
    };

    void onShortMessage(quint32 packed);
    void sendShort(quint8 status, quint8 d1, quint8 d2);
    void countDropped() { m_dropped.fetch_add(1, std::memory_order_relaxed); }
    void eraseActiveLocked(quint8 channel, quint8 srcNote);
    void flushActiveNotes();

    std::unique_ptr<MidiBackend> m_backend;
    std::atomic<bool> m_running{false};

    // 翻译表：GUI 线程独占写，MIDI 回调线程共享读。
    // 不用 std::atomic<shared_ptr>——MSVC 支持但 Apple libc++ 未实现。
    mutable std::shared_mutex m_tableMutex;
    std::shared_ptr<const MappingTable> m_table;

    std::mutex m_activeMutex;
    std::vector<ActiveNote> m_active;

    std::atomic<quint64> m_forwarded{0};
    std::atomic<quint64> m_dropped{0};
};
