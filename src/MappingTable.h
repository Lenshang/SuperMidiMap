#pragma once
#include <array>
#include <memory>
#include <QtGlobal>

// 单个垫的映射规则。channel 为 0 表示跟随原始通道。
// source 是该垫监听的源音符号（设备实际发送的音符），构建翻译表时作为下标。
struct PadMapping
{
    bool enabled = false;  // 启用翻译；否则该音符按全局透传规则处理
    bool muted = false;    // 静音：直接丢弃（仅在 enabled 时生效）
    quint8 source = 0;     // 源音符号（设备发来的音符）
    quint8 target = 0;     // 目标音符号 0-127
    quint8 channel = 0;    // 目标通道 1-16；0 = 跟随原通道
};

// 不可变的 128 音符翻译表。
// GUI 线程构建新表后原子发布，MIDI 回调线程无锁读取，避免任何音频路径阻塞。
class MappingTable
{
public:
    std::array<PadMapping, 128> notes{};
    bool passthrough = true;   // 未启用翻译的音符与其他通道消息是否原样透传
};
