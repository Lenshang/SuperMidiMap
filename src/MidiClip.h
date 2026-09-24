#pragma once
#include <QString>
#include <QVector>
#include <QtGlobal>

// 一个 MIDI 片段（由 .mid 文件解析而来）：按时间排序的通道音符事件
// 与片段总时长。按住垫时循环播放，松开立即停止。
struct ClipEvent
{
    double timeMs = 0;   // 相对片段起点的毫秒数
    quint8 status = 0;   // 0x90|ch（开）或 0x80|ch（关）
    quint8 note = 0;
    quint8 vel = 0;
};

struct MidiClip
{
    double lengthMs = 0;
    QVector<ClipEvent> events;  // 按 timeMs 升序；同一时刻关在前开在后

    bool isValid() const { return lengthMs > 0 && !events.isEmpty(); }
};

// 解析标准 MIDI 文件（SMF 格式 0/1）。
// bpmOverride > 0 时忽略文件内的速度事件并按该 BPM 均匀换算；
// 否则按文件内的速度事件（tempo map）换算。
// 失败时返回无效片段并设置 *error。
MidiClip parseMidiFile(const QString &path, double bpmOverride, QString *error = nullptr);
