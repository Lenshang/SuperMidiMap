#pragma once
#include <array>
#include <memory>
#include <vector>
#include <QString>
#include <QtGlobal>

class MidiClip;

// 单个垫的映射规则。
// mode=0 音符模式：source 音符翻译为 target/channel（或静音）。
// mode=1 片段模式：按住 source 垫循环播放 clipPath 指向的 MIDI 文件，
//        松开停止；触发音符本身不转发。
struct PadMapping
{
    bool enabled = false;  // 启用翻译；否则该音符按全局透传规则处理
    bool muted = false;    // 静音：直接丢弃（仅在 enabled 时生效）
    quint8 source = 0;     // 源音符号（设备发来的音符）
    quint8 target = 0;     // 目标音符号 0-127（音符模式）
    quint8 channel = 0;    // 目标通道 1-16；0 = 跟随原通道
    quint8 mode = 0;       // 0 = 音符, 1 = MIDI 片段
    bool loop = true;      // 片段模式：按住期间循环
    double bpm = 120.0;    // 片段模式：播放速度（覆盖文件内速度）
    QString clipPath;      // 片段模式：MIDI 文件路径
};

// 不可变的 128 音符翻译表。
// GUI 线程构建新表后原子发布（shared_mutex 保护），MIDI 回调线程无锁读取。
class MappingTable
{
public:
    std::array<PadMapping, 128> notes{};
    bool passthrough = true;   // 未启用翻译的音符与其他通道消息是否原样透传

    // 片段支持：clips 为本表引用的片段集合，clipFor[音符] 为下标（-1 = 非片段垫）
    std::vector<std::shared_ptr<const MidiClip>> clips;
    std::array<qint8, 128> clipFor{};
};
