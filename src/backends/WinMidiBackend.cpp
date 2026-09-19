#include "WinMidiBackend.h"
#include <QObject>

namespace {

int findInputId(const QString &name)
{
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR
            && QString::fromWCharArray(caps.szPname) == name)
            return int(i);
    }
    return -1;
}

int findOutputId(const QString &name)
{
    const UINT n = midiOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIOUTCAPSW caps{};
        if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR
            && QString::fromWCharArray(caps.szPname) == name)
            return int(i);
    }
    return -1;
}

} // namespace

WinMidiBackend::~WinMidiBackend()
{
    closeAll();
}

QStringList WinMidiBackend::inputDevices() const
{
    QStringList list;
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            list << QString::fromWCharArray(caps.szPname);
    }
    return list;
}

QStringList WinMidiBackend::outputDevices() const
{
    QStringList list;
    const UINT n = midiOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIOUTCAPSW caps{};
        if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            list << QString::fromWCharArray(caps.szPname);
    }
    return list;
}

bool WinMidiBackend::openInput(const QString &name, QString *error)
{
    const int id = findInputId(name);
    if (id < 0) {
        if (error)
            *error = QObject::tr("找不到输入设备: %1").arg(name);
        return false;
    }
    const MMRESULT mr = midiInOpen(&m_in, UINT(id),
                                   reinterpret_cast<DWORD_PTR>(&WinMidiBackend::inProc),
                                   reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    if (mr != MMSYSERR_NOERROR) {
        m_in = nullptr;
        if (error)
            *error = QObject::tr("打开输入设备失败 (MIDI 错误码 %1)").arg(int(mr));
        return false;
    }
    if (midiInStart(m_in) != MMSYSERR_NOERROR) {  // WinMM: 不调用 Start 就不会投递任何消息
        midiInClose(m_in);
        m_in = nullptr;
        if (error)
            *error = QObject::tr("启动输入设备失败 (MIDI 错误码 %1)").arg(int(mr));
        return false;
    }
    return true;
}

bool WinMidiBackend::openOutput(const QString &name, QString *error)
{
    const int id = findOutputId(name);
    if (id < 0) {
        if (error)
            *error = QObject::tr("找不到输出设备: %1").arg(name);
        return false;
    }
    const MMRESULT mr = midiOutOpen(&m_out, UINT(id), 0, 0, CALLBACK_NULL);
    if (mr != MMSYSERR_NOERROR) {
        m_out = nullptr;
        if (error)
            *error = QObject::tr("打开输出设备失败 (MIDI 错误码 %1)").arg(int(mr));
        return false;
    }
    return true;
}

void WinMidiBackend::closeInput()
{
    if (m_in) {
        midiInStop(m_in);
        midiInClose(m_in);
        m_in = nullptr;
    }
}

void WinMidiBackend::closeOutput()
{
    if (m_out) {
        midiOutReset(m_out);
        midiOutClose(m_out);
        m_out = nullptr;
    }
}

void WinMidiBackend::sendShort(quint8 status, quint8 d1, quint8 d2)
{
    if (!m_out)
        return;
    const DWORD packed = DWORD(status) | (DWORD(d1) << 8) | (DWORD(d2) << 16);
    midiOutShortMsg(m_out, packed);
}

void CALLBACK WinMidiBackend::inProc(HMIDIIN /*hMidiIn*/, UINT wMsg, DWORD_PTR dwInstance,
                                     DWORD_PTR dwParam1, DWORD_PTR /*dwParam2*/)
{
    auto *self = reinterpret_cast<WinMidiBackend *>(dwInstance);
    if (!self || !self->m_callback)
        return;
    switch (wMsg) {
    case MIM_DATA:
    case MIM_MOREDATA:
        self->m_callback(quint32(dwParam1 & 0xFFFFFF));
        break;
    default:
        break;
    }
}
