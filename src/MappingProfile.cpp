#include "MappingProfile.h"
#include "MidiClip.h"
#include <QJsonArray>

MappingProfile::MappingProfile()
{
    for (int i = 0; i < kPads; ++i)
        pads[i].source = kDefaultNotes[i];
}

std::shared_ptr<const MappingTable> MappingProfile::buildTable(QString *warning) const
{
    auto table = std::make_shared<MappingTable>();
    table->passthrough = passthrough;
    for (int i = 0; i < kPads; ++i) {
        const quint8 src = pads[i].source;
        if (src >= 128)
            continue;
        table->notes[src] = pads[i];
        if (pads[i].enabled && pads[i].mode == 1) {
            QString err;
            auto clip = std::make_shared<MidiClip>(
                parseMidiFile(pads[i].clipPath, pads[i].bpm, &err));
            if (clip->isValid()) {
                table->clipFor[src] = qint8(table->clips.size());
                table->clips.push_back(std::move(clip));
            } else if (warning && !pads[i].clipPath.isEmpty()) {
                *warning = QStringLiteral("垫%1的 MIDI 文件无法使用: %2").arg(i + 1).arg(err);
            }
        }
    }
    return table;
}

QJsonObject MappingProfile::toJson() const
{
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("app")] = QStringLiteral("SuperMidiMap");
    root[QStringLiteral("inputDevice")] = inputDevice;
    root[QStringLiteral("outputDevice")] = outputDevice;
    root[QStringLiteral("passthrough")] = passthrough;

    QJsonArray arr;
    for (int i = 0; i < kPads; ++i) {
        QJsonObject p;
        p[QStringLiteral("source")] = pads[i].source;
        p[QStringLiteral("enabled")] = pads[i].enabled;
        p[QStringLiteral("muted")] = pads[i].muted;
        p[QStringLiteral("target")] = pads[i].target;
        p[QStringLiteral("channel")] = pads[i].channel;
        p[QStringLiteral("mode")] = pads[i].mode;
        p[QStringLiteral("loop")] = pads[i].loop;
        p[QStringLiteral("bpm")] = pads[i].bpm;
        p[QStringLiteral("clipPath")] = pads[i].clipPath;
        arr.append(p);
    }
    root[QStringLiteral("pads")] = arr;
    return root;
}

MappingProfile MappingProfile::fromJson(const QJsonObject &obj, bool *ok)
{
    MappingProfile profile;
    const bool valid = obj.value(QStringLiteral("app")).toString() == QLatin1String("SuperMidiMap")
                       && obj.contains(QStringLiteral("pads"));
    if (valid) {
        profile.inputDevice = obj.value(QStringLiteral("inputDevice")).toString();
        profile.outputDevice = obj.value(QStringLiteral("outputDevice")).toString();
        profile.passthrough = obj.value(QStringLiteral("passthrough")).toBool(true);

        const QJsonArray arr = obj.value(QStringLiteral("pads")).toArray();
        for (int i = 0; i < kPads && i < arr.size(); ++i) {
            const QJsonObject p = arr.at(i).toObject();
            PadMapping &m = profile.pads[i];
            m.source = quint8(qBound(0, p.value(QStringLiteral("source")).toInt(kDefaultNotes[i]), 127));
            m.enabled = p.value(QStringLiteral("enabled")).toBool(true);
            m.muted = p.value(QStringLiteral("muted")).toBool(false);
            m.target = quint8(qBound(0, p.value(QStringLiteral("target")).toInt(m.source), 127));
            m.channel = quint8(qBound(0, p.value(QStringLiteral("channel")).toInt(0), 16));
            m.mode = p.value(QStringLiteral("mode")).toInt(0) == 1 ? quint8(1) : quint8(0);
            m.loop = p.value(QStringLiteral("loop")).toBool(true);
            m.bpm = p.value(QStringLiteral("bpm")).toDouble(120.0);
            if (m.bpm < 30.0 || m.bpm > 300.0)
                m.bpm = 120.0;
            m.clipPath = p.value(QStringLiteral("clipPath")).toString();
        }
    }
    if (ok)
        *ok = valid;
    return profile;
}
