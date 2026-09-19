#include "CoreMidiBackend.h"
#include <QCoreApplication>

namespace {

int channelMessageLength(Byte status)
{
    switch (status & 0xF0) {
    case 0xC0:
    case 0xD0:
        return 2;  // 状态 + 1 数据字节
    case 0x80:
    case 0x90:
    case 0xA0:
    case 0xB0:
    case 0xE0:
        return 3;  // 状态 + 2 数据字节
    default:
        return 0;  // 系统消息：由解析循环按单字节跳过
    }
}

} // namespace

CoreMidiBackend::CoreMidiBackend()
{
    if (MIDIClientCreate(CFSTR("SuperMidiMap"), nullptr, nullptr, &m_client) != noErr)
        m_client = 0;
    if (m_client) {
        MIDIInputPortCreate(m_client, CFSTR("In"), &CoreMidiBackend::readProc, this, &m_inPort);
        MIDIOutputPortCreate(m_client, CFSTR("Out"), &m_outPort);
    }
}

CoreMidiBackend::~CoreMidiBackend()
{
    closeAll();
    if (m_inPort)
        MIDIPortDispose(m_inPort);
    if (m_outPort)
        MIDIPortDispose(m_outPort);
    if (m_client)
        MIDIClientDispose(m_client);
}

QString CoreMidiBackend::endpointName(MIDIEndpointRef endpoint) const
{
    CFStringRef name = nullptr;
    if (endpoint && MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &name) == noErr
        && name) {
        const QString s = QString::fromCFString(name);
        CFRelease(name);
        return s;
    }
    return {};
}

QStringList CoreMidiBackend::inputDevices() const
{
    QStringList list;
    const ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; ++i)
        list << endpointName(MIDIGetSource(i));
    return list;
}

QStringList CoreMidiBackend::outputDevices() const
{
    QStringList list;
    const ItemCount n = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; ++i)
        list << endpointName(MIDIGetDestination(i));
    return list;
}

MIDIEndpointRef CoreMidiBackend::findEndpoint(bool source, const QString &name) const
{
    const ItemCount n = source ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; ++i) {
        const MIDIEndpointRef ep = source ? MIDIGetSource(i) : MIDIGetDestination(i);
        if (ep && endpointName(ep) == name)
            return ep;
    }
    return 0;
}

bool CoreMidiBackend::openInput(const QString &name, QString *error)
{
    if (!m_client || !m_inPort) {
        if (error)
            *error = QCoreApplication::translate("CoreMidiBackend", "CoreMIDI 客户端初始化失败");
        return false;
    }
    const MIDIEndpointRef src = findEndpoint(true, name);
    if (!src) {
        if (error)
            *error = QCoreApplication::translate("CoreMidiBackend", "找不到输入设备: %1").arg(name);
        return false;
    }
    if (MIDIPortConnectSource(m_inPort, src, nullptr) != noErr) {
        if (error)
            *error = QCoreApplication::translate("CoreMidiBackend", "连接输入设备失败");
        return false;
    }
    m_source = src;
    return true;
}

bool CoreMidiBackend::openOutput(const QString &name, QString *error)
{
    if (!m_client || !m_outPort) {
        if (error)
            *error = QCoreApplication::translate("CoreMidiBackend", "CoreMIDI 客户端初始化失败");
        return false;
    }
    const MIDIEndpointRef dest = findEndpoint(false, name);
    if (!dest) {
        if (error)
            *error = QCoreApplication::translate("CoreMidiBackend", "找不到输出设备: %1").arg(name);
        return false;
    }
    m_dest = dest;
    return true;
}

void CoreMidiBackend::closeInput()
{
    if (m_source && m_inPort)
        MIDIPortDisconnectSource(m_inPort, m_source);
    m_source = 0;
}

void CoreMidiBackend::closeOutput()
{
    m_dest = 0;  // destination 无连接状态，直接丢弃引用
}

void CoreMidiBackend::sendShort(quint8 status, quint8 d1, quint8 d2)
{
    if (!m_dest || !m_outPort)
        return;
    Byte buffer[64];
    MIDIPacketList *pktList = reinterpret_cast<MIDIPacketList *>(buffer);
    MIDIPacket *pkt = MIDIPacketListInit(pktList);
    const Byte msg[3] = { status, d1, d2 };
    if (MIDIPacketListAdd(pktList, sizeof(buffer), pkt, 0 /*立即发送*/, 3, msg))
        MIDISend(m_outPort, m_dest, pktList);
}

void CoreMidiBackend::readProc(const MIDIPacketList *pktlist, void *refCon, void * /*connRefCon*/)
{
    auto *self = static_cast<CoreMidiBackend *>(refCon);
    if (!self || !self->m_callback)
        return;

    const MIDIPacket *pkt = &pktlist->packet[0];
    for (UInt32 p = 0; p < pktlist->numPackets; ++p, pkt = MIDIPacketNext(pkt)) {
        const Byte *data = pkt->data;
        UInt16 i = 0;
        const UInt16 n = pkt->length;
        while (i < n) {
            const Byte st = data[i];
            if (st < 0x80) {           // 无运行状态支持：忽略散落数据字节
                ++i;
                continue;
            }
            if (st == 0xF0) {          // SysEx：跳到 F7 结束
                while (i < n && data[i] != 0xF7)
                    ++i;
                if (i < n)
                    ++i;
                continue;
            }
            const int len = channelMessageLength(st);
            if (len == 0) {            // 其他系统消息按单字节跳过
                ++i;
                continue;
            }
            if (i + len > n)
                break;
            const quint32 packed =
                quint32(st) | (quint32(data[i + 1]) << 8)
                | (quint32(len == 3 ? data[i + 2] : 0) << 16);
            self->m_callback(packed);
            i = UInt16(i + len);
        }
    }
}
