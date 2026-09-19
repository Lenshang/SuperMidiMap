#include "MappingProfile.h"
#include <QJsonArray>

MappingProfile::MappingProfile()
{
    for (int i = 0; i < kPads; ++i)
        pads[i].source = kDefaultNotes[i];
}

std::shared_ptr<const MappingTable> MappingProfile::buildTable() const
{
    auto table = std::make_shared<MappingTable>();
    table->passthrough = passthrough;
    for (int i = 0; i < kPads; ++i) {
        const quint8 src = pads[i].source;
        if (src < 128)
            table->notes[src] = pads[i];
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
        }
    }
    if (ok)
        *ok = valid;
    return profile;
}
