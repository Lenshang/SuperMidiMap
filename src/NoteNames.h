#pragma once
#include <QString>

namespace NoteNames {

inline const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

inline QString name(int note)
{
    if (note < 0 || note > 127)
        return QStringLiteral("--");
    return QString::fromLatin1(kNames[note % 12]) + QString::number(note / 12 - 1);
}

inline QString withNumber(int note)
{
    if (note < 0 || note > 127)
        return QStringLiteral("--");
    return name(note) + QStringLiteral(" (%1)").arg(note);
}

} // namespace NoteNames
