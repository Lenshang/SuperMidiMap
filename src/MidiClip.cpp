#include "MidiClip.h"
#include <QFile>
#include <algorithm>

namespace {

struct RawEvent
{
    qint64 tick = 0;
    quint8 status = 0;
    quint8 d1 = 0;
    quint8 d2 = 0;
};

struct TempoChange
{
    qint64 tick = 0;
    double usPerQuarter = 500000.0;
};

quint32 readBE32(const uchar *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint16 readBE16(const uchar *p)
{
    return (quint16(p[0]) << 8) | quint16(p[1]);
}

quint32 readVLQ(const uchar *data, int &pos, int end)
{
    quint32 v = 0;
    for (int i = 0; i < 4 && pos < end; ++i) {
        const uchar b = data[pos++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80))
            break;
    }
    return v;
}

} // namespace

MidiClip parseMidiFile(const QString &path, double bpmOverride, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return MidiClip{};
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("无法打开文件: %1").arg(path));
    const QByteArray bytes = file.readAll();
    file.close();

    const uchar *d = reinterpret_cast<const uchar *>(bytes.constData());
    const int n = bytes.size();
    if (n < 14 || qstrncmp(bytes.constData(), "MThd", 4) != 0)
        return fail(QStringLiteral("不是标准 MIDI 文件（缺少 MThd 头）"));

    const quint32 headerLen = readBE32(d + 4);
    const quint16 division = readBE16(d + 12);
    int pos = int(8 + headerLen);

    // 时基：bit15=0 为每四分音符 tick 数；否则为 SMPTE（少见，按每秒 tick 数均匀换算）
    double msPerTickUniform = -1.0;
    qint64 ticksPerQuarter = 480;
    if (division & 0x8000) {
        const int fps = qint8(uchar(division >> 8));       // 负数 SMPTE 帧率
        const int tpf = division & 0xFF;
        if (fps <= 0 || tpf == 0)
            return fail(QStringLiteral("不支持的 SMPTE 时基"));
        msPerTickUniform = 1000.0 / (double(fps) * double(tpf));
    } else if (division > 0) {
        ticksPerQuarter = division;
    } else {
        return fail(QStringLiteral("无效的时基设置"));
    }
    if (bpmOverride > 0 && msPerTickUniform < 0)
        msPerTickUniform = 60000.0 / (bpmOverride * double(ticksPerQuarter));

    std::vector<RawEvent> raw;
    std::vector<TempoChange> tempos;

    // 逐块解析
    while (pos + 8 <= n) {
        const char *id = bytes.constData() + pos;
        const quint32 len = readBE32(d + pos + 4);
        pos += 8;
        if (pos + int(len) > n)
            break;
        if (qstrncmp(id, "MTrk", 4) != 0) {  // 未知块直接跳过
            pos += int(len);
            continue;
        }
        const int end = pos + int(len);
        qint64 absTick = 0;
        quint8 running = 0;
        while (pos < end) {
            absTick += qint64(readVLQ(d, pos, end));
            if (pos >= end)
                break;
            quint8 b = d[pos++];
            if (b == 0xFF) {                              // Meta 事件
                if (pos >= end)
                    break;
                const quint8 type = d[pos++];
                const quint32 mlen = readVLQ(d, pos, end);
                if (type == 0x51 && mlen == 3 && pos + 3 <= end) {
                    const double uspq = (quint32(d[pos]) << 16) | (quint32(d[pos + 1]) << 8)
                                        | quint32(d[pos + 2]);
                    if (uspq > 0)
                        tempos.push_back({absTick, uspq});
                }
                pos += int(mlen);
                running = 0;
                if (type == 0x2F)                          // End of Track
                    break;
            } else if (b == 0xF0 || b == 0xF7) {          // SysEx
                const quint32 slen = readVLQ(d, pos, end);
                pos += int(slen);
                running = 0;
            } else if (b & 0x80) {                        // 通道消息，带状态
                const quint8 type = b & 0xF0;
                if (type == 0xC0 || type == 0xD0) {
                    if (pos >= end) break;
                    running = b;
                    raw.push_back({absTick, b, d[pos++], 0});
                } else if (type == 0x80 || type == 0x90 || type == 0xA0
                           || type == 0xB0 || type == 0xE0) {
                    if (pos + 2 > end) break;
                    running = b;
                    raw.push_back({absTick, b, d[pos], d[pos + 1]});
                    pos += 2;
                } else {
                    pos += (type == 0xF0 || type == 0xF7) ? 1 : 0;  // 不支持的其他消息
                }
            } else {                                      // 运行状态（省略状态字节）
                const quint8 type = running & 0xF0;
                if (type == 0xC0 || type == 0xD0) {
                    raw.push_back({absTick, running, b, 0});
                } else if (type == 0x80 || type == 0x90 || type == 0xA0
                           || type == 0xB0 || type == 0xE0) {
                    if (pos >= end) break;
                    raw.push_back({absTick, running, b, d[pos++]});
                } else {
                    // 无有效运行状态，丢弃该数据字节
                }
            }
        }
        pos = end;
    }

    // 只保留音符开/关事件
    std::vector<RawEvent> notes;
    for (const RawEvent &e : raw) {
        const quint8 t = e.status & 0xF0;
        if (t == 0x90 && e.d2 > 0)
            notes.push_back(e);
        else if (t == 0x80 || (t == 0x90 && e.d2 == 0))
            notes.push_back({e.tick, quint8(0x80 | (e.status & 0x0F)), e.d1, 0});
    }
    if (notes.empty())
        return fail(QStringLiteral("文件中没有音符事件"));

    // 同一 tick 先关后开，避免循环时同音符重叠
    std::sort(notes.begin(), notes.end(), [](const RawEvent &a, const RawEvent &b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        const bool aOn = (a.status & 0xF0) == 0x90;
        const bool bOn = (b.status & 0xF0) == 0x90;
        if (aOn != bOn)
            return !aOn;   // 关在前
        return false;
    });

    // tick -> 毫秒
    MidiClip clip;
    if (msPerTickUniform >= 0) {
        for (const RawEvent &e : notes) {
            ClipEvent ce;
            ce.timeMs = double(e.tick) * msPerTickUniform;
            ce.status = e.status;
            ce.note = e.d1;
            ce.vel = e.d2;
            clip.events.append(ce);
        }
    } else {
        std::sort(tempos.begin(), tempos.end(), [](const TempoChange &a, const TempoChange &b) {
            return a.tick < b.tick;
        });
        double curMs = 0;
        qint64 curTick = 0;
        double uspq = tempos.empty() ? 500000.0 : tempos.front().usPerQuarter;
        size_t ti = 0;
        for (const RawEvent &e : notes) {
            while (ti < tempos.size() && tempos[ti].tick <= e.tick) {
                curMs += double(tempos[ti].tick - curTick) * (uspq / 1000.0) / double(ticksPerQuarter);
                curTick = tempos[ti].tick;
                uspq = tempos[ti].usPerQuarter;
                ++ti;
            }
            curMs += double(e.tick - curTick) * (uspq / 1000.0) / double(ticksPerQuarter);
            curTick = e.tick;
            ClipEvent ce;
            ce.timeMs = curMs;
            ce.status = e.status;
            ce.note = e.d1;
            ce.vel = e.d2;
            clip.events.append(ce);
        }
    }
    for (const ClipEvent &e : clip.events)
        clip.lengthMs = qMax(clip.lengthMs, e.timeMs);
    clip.lengthMs += 0.001;
    return clip;
}
