# 生成测试用 MIDI 鼓片段：120 BPM、两小节（约 3.6 秒）
# Kick C1(36) 打 1/3 拍，Snare D1(38) 打 2/4 拍，Hi-hat F#1(42) 打八分音符
import struct
import sys

TPQ = 480  # 每四分音符 480 tick
events = []  # (abs_tick, msg_bytes)

def on(t, n, v):
    events.append((t, bytes([0x90, n, v])))

def off(t, n):
    events.append((t, bytes([0x80, n, 0])))

for bar in range(2):
    b = bar * 1920
    for beat in range(4):
        t = b + beat * 480
        if beat in (0, 2):
            on(t, 36, 110); off(t + 120, 36)
        else:
            on(t, 38, 100); off(t + 120, 38)
        on(t, 42, 70); off(t + 60, 42)

# 同一 tick 先关后开
events.sort(key=lambda e: (e[0], (e[1][0] & 0xF0) == 0x90))

data = b""
prev = 0
for t, msg in events:
    delta = t - prev
    vlq = bytes([delta & 0x7F])
    delta >>= 7
    while delta:
        vlq = bytes([(delta & 0x7F) | 0x80]) + vlq
        delta >>= 7
    data += vlq + msg
    prev = t
data += bytes([0, 0xFF, 0x2F, 0x00])  # End of Track

# 120 BPM = 500000 us/quarter
tempo = bytes([0, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20])
track = tempo + data
out = b"MThd" + struct.pack(">IHHH", 6, 0, 1, TPQ)
out += b"MTrk" + struct.pack(">I", len(track)) + track

path = sys.argv[1] if len(sys.argv) > 1 else "test_drum.mid"
with open(path, "wb") as f:
    f.write(out)
print(f"written: {path} ({len(out)} bytes, {len(events)} events)")
