#pragma once
#include "MidiBackend.h"
#include <CoreMIDI/CoreMIDI.h>

// macOS 后端：CoreMIDI。虚拟环回使用系统自带的 IAC 总线
// （Audio MIDI Setup -> MIDI Studio -> IAC Driver，新建总线即可，无交叉配对）。
class CoreMidiBackend final : public MidiBackend
{
public:
    CoreMidiBackend();
    ~CoreMidiBackend() override;

    QStringList inputDevices() const override;   // MIDI sources（可读）
    QStringList outputDevices() const override;  // MIDI destinations（可写）
    bool openInput(const QString &name, QString *error) override;
    bool openOutput(const QString &name, QString *error) override;
    void closeInput() override;
    void closeOutput() override;
    bool inputOpen() const override { return m_source != 0; }
    bool outputOpen() const override { return m_dest != 0; }
    void sendShort(quint8 status, quint8 d1, quint8 d2) override;

private:
    static void readProc(const MIDIPacketList *pktlist, void *refCon, void *connRefCon);
    QString endpointName(MIDIEndpointRef endpoint) const;
    MIDIEndpointRef findEndpoint(bool source, const QString &name) const;

    MIDIClientRef m_client = 0;
    MIDIPortRef m_inPort = 0;
    MIDIPortRef m_outPort = 0;
    MIDIEndpointRef m_source = 0;
    MIDIEndpointRef m_dest = 0;
};
