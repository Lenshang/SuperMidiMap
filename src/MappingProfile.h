#pragma once
#include <QString>
#include <QJsonObject>
#include <array>
#include <memory>
#include "MappingTable.h"

// impactx_4x5_pad 的映射配置档。
// kDefaultNotes 是设备出厂固定键位（按界面布局行优先：左上 -> 右下），
// 仅作为每个垫的默认源音符；源音符可以在界面里自由修改。
class MappingProfile
{
public:
    MappingProfile();

    static constexpr int kPads = 16;
    static constexpr std::array<quint8, kPads> kDefaultNotes = {
        72, 73, 74, 75,   // C5  C#5 D5  D#5
        68, 69, 70, 71,   // G#4 A4  A#4 B4
        64, 65, 66, 67,   // E4  F4  F#4 G4
        60, 61, 62, 63,   // C4  C#4 D4  D#4
    };

    QString inputDevice;   // 输入设备名（打击垫）
    QString outputDevice;  // 输出设备名（虚拟 Loopback 的 OUT 端）
    bool passthrough = true;

    std::array<PadMapping, kPads> pads{};  // 与 kDefaultNotes 一一对应

    // 由 16 个垫的配置构建完整的 128 音符翻译表（含 MIDI 片段）。
    // 片段文件解析失败时跳过该片段并设置 *warning。
    std::shared_ptr<const MappingTable> buildTable(QString *warning = nullptr) const;

    QJsonObject toJson() const;
    static MappingProfile fromJson(const QJsonObject &obj, bool *ok = nullptr);
};
