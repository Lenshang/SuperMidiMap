#pragma once
#include "MidiBackend.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

// Windows 后端：WinMM API，回调线程内直发，延迟最低
class WinMidiBackend final : public MidiBackend
{
public:
    ~WinMidiBackend() override;

    QStringList inputDevices() const override;
    QStringList outputDevices() const override;
    bool openInput(const QString &name, QString *error) override;
    bool openOutput(const QString &name, QString *error) override;
    void closeInput() override;
    void closeOutput() override;
    bool inputOpen() const override { return m_in != nullptr; }
    bool outputOpen() const override { return m_out != nullptr; }
    void sendShort(quint8 status, quint8 d1, quint8 d2) override;

private:
    static void CALLBACK inProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance,
                                DWORD_PTR dwParam1, DWORD_PTR dwParam2);

    HMIDIIN m_in = nullptr;
    HMIDIOUT m_out = nullptr;
};
