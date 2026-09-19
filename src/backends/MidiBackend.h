#pragma once
#include <QString>
#include <QStringList>
#include <functional>
#include <QtGlobal>

// 平台 MIDI 后端抽象：收发 MIDI 1.0 通道短消息。
// packed 消息格式: status | (d1 << 8) | (d2 << 16)，与 WinMM/内核打包方式一致。
class MidiBackend
{
public:
    using MessageCallback = std::function<void(quint32 packed)>;

    virtual ~MidiBackend() = default;

    // 枚举可读（输入）/可写（输出）设备
    virtual QStringList inputDevices() const = 0;
    virtual QStringList outputDevices() const = 0;

    virtual bool openInput(const QString &name, QString *error) = 0;
    virtual bool openOutput(const QString &name, QString *error) = 0;
    virtual void closeInput() = 0;
    virtual void closeOutput() = 0;
    void closeAll()
    {
        closeInput();
        closeOutput();
    }

    virtual bool inputOpen() const = 0;
    virtual bool outputOpen() const = 0;

    // 在驱动回调线程内直接发送，无队列
    virtual void sendShort(quint8 status, quint8 d1, quint8 d2) = 0;

    void setMessageCallback(MessageCallback cb) { m_callback = std::move(cb); }

protected:
    MessageCallback m_callback;
};
