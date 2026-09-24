// midi-probe — WinMM MIDI 测试工具（不依赖 Qt，用于验证虚拟 Loopback 链路）
//
// 用法:
//   midi-probe list                                        列出所有 MIDI 输入/输出设备
//   midi-probe mon <输入设备名|序号>                        监视某输入设备的所有消息
//   midi-probe send <输出设备名|序号> <note> [vel] [ch] [count] [intervalMs]
//                                                          发送 Note On/Off
//   midi-probe hold <输出设备名|序号> <note> [ms] [vel] [ch]   按住发送（长 Note On）
//   midi-probe latency <输出设备|序号> <输入设备|序号> [note] [count]
//                                                          测量 发送->接收 单程延迟
//
// 示例（验证 SuperMidiMap 翻译链路，程序输入/输出均设为 "Loopback (A)"）:
//   midi-probe latency "Loopback (B)" "Loopback (B)" 60    一步测延迟+翻译结果
//   midi-probe mon "Loopback (B)"                          终端 1：看翻译后的音符
//   midi-probe send "Loopback (B)" 60 100 1 4 400          终端 2：模拟打击垫

#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static std::string toUtf8(const wchar_t *w)
{
    char buf[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf), nullptr, nullptr);
    return buf;
}

static std::string toLower(std::string s)
{
    for (char &c : s)
        c = char(tolower(unsigned char(c)));
    return s;
}

static const char *kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static void printMessage(unsigned status, unsigned d1, unsigned d2, unsigned long ts)
{
    const unsigned type = status & 0xF0;
    const unsigned ch = (status & 0x0F) + 1;
    if (type == 0x90 && d2 > 0)
        printf("  [%7lu ms] Note On   ch=%2u  %s%-2u  vel=%u\n", ts, ch, kNoteNames[d1 % 12], d1 / 12 - 1, d2);
    else if (type == 0x80 || (type == 0x90 && d2 == 0))
        printf("  [%7lu ms] Note Off  ch=%2u  %s%-2u\n", ts, ch, kNoteNames[d1 % 12], d1 / 12 - 1);
    else if (type == 0xB0)
        printf("  [%7lu ms] CC       ch=%2u  cc=%-3u val=%u\n", ts, ch, d1, d2);
    else
        printf("  [%7lu ms] status=0x%02X d1=%u d2=%u\n", ts, status, d1, d2);
    fflush(stdout);
}

static void CALLBACK inProc(HMIDIIN, UINT wMsg, DWORD_PTR, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    if (wMsg == MIM_DATA)
        printMessage(unsigned(dwParam1 & 0xFF), unsigned((dwParam1 >> 8) & 0xFF),
                     unsigned((dwParam1 >> 16) & 0xFF), (unsigned long)dwParam2);
}

// ---- latency 命令：单进程内测量 发送 -> (被测链路) -> 接收 的单程延迟 ----

static int resolveDevice(const char *arg, bool input);

static LARGE_INTEGER g_qpcFreq = {};
static volatile LONGLONG g_t0 = 0;
static volatile LONGLONG g_delta = -1;
static volatile unsigned g_gotNote = 0;
static volatile unsigned g_gotVel = 0;

static void CALLBACK latencyInProc(HMIDIIN, UINT wMsg, DWORD_PTR, DWORD_PTR dwParam1, DWORD_PTR)
{
    if (wMsg != MIM_DATA)
        return;
    const unsigned st = unsigned(dwParam1 & 0xFF);
    const unsigned d1 = unsigned((dwParam1 >> 8) & 0xFF);
    const unsigned d2 = unsigned((dwParam1 >> 16) & 0xFF);
    if ((st & 0xF0) == 0x90 && d2 > 0) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        g_gotNote = d1;
        g_gotVel = d2;
        g_delta = now.QuadPart - g_t0;
    }
}

static int latencyCmd(int argc, char **argv)
{
    if (argc < 4) {
        printf("用法: midi-probe latency <输出设备|序号> <输入设备|序号> [note] [count]\n");
        return 1;
    }
    const int outId = resolveDevice(argv[2], false);
    const int inId = resolveDevice(argv[3], true);
    const unsigned note = argc > 4 ? unsigned(atoi(argv[4])) : 60;
    const int count = argc > 5 ? atoi(argv[5]) : 30;
    if (outId < 0) {
        printf("未找到输出设备: %s\n", argv[2]);
        return 1;
    }
    if (inId < 0) {
        printf("未找到输入设备: %s\n", argv[3]);
        return 1;
    }
    QueryPerformanceFrequency(&g_qpcFreq);

    HMIDIOUT out = nullptr;
    HMIDIIN in = nullptr;
    if (midiOutOpen(&out, UINT(outId), 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        printf("打开输出设备失败\n");
        return 1;
    }
    if (midiInOpen(&in, UINT(inId), reinterpret_cast<DWORD_PTR>(&latencyInProc), 0, CALLBACK_FUNCTION)
        != MMSYSERR_NOERROR) {
        printf("打开输入设备失败\n");
        return 1;
    }
    midiInStart(in);

    const DWORD on = DWORD(0x90) | (note << 8) | (100 << 16);
    const DWORD off = DWORD(0x80) | (note << 8);
    printf("链路: OUT [%d] -> IN [%d], note=%u, 共 %d 次\n", outId, inId, note, count);

    double sum = 0, mn = 1e9, mx = 0;
    int ok = 0;
    for (int i = 0; i < count; ++i) {
        g_delta = -1;
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        g_t0 = t.QuadPart;
        const MMRESULT sr = midiOutShortMsg(out, on);
        int waited = 0;
        while (g_delta < 0 && waited < 500) {
            Sleep(2);
            waited += 2;
        }
        if (g_delta >= 0) {
            const double ms = double(g_delta) * 1000.0 / double(g_qpcFreq.QuadPart);
            sum += ms;
            mn = ms < mn ? ms : mn;
            mx = ms > mx ? ms : mx;
            ++ok;
            printf("  #%02d  %6.2f ms  (收到 note=%u vel=%u)\n", i + 1, ms, g_gotNote, g_gotVel);
        } else {
            printf("  #%02d  超时未收到 (midiOutShortMsg=%u)\n", i + 1, unsigned(sr));
        }
        midiOutShortMsg(out, off);
        Sleep(40);
    }
    midiOutReset(out);
    midiOutClose(out);
    midiInStop(in);
    midiInClose(in);
    if (ok)
        printf("\n%d/%d 成功  平均 %.2f ms  最小 %.2f ms  最大 %.2f ms\n",
               ok, count, sum / ok, mn, mx);
    return 0;
}

static void listDevices()
{
    printf("MIDI 输入设备 (in):\n");
    for (UINT i = 0; i < midiInGetNumDevs(); ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            printf("  [%u] %s\n", unsigned(i), toUtf8(caps.szPname).c_str());
    }
    printf("MIDI 输出设备 (out):\n");
    for (UINT i = 0; i < midiOutGetNumDevs(); ++i) {
        MIDIOUTCAPSW caps{};
        if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            printf("  [%u] %s\n", unsigned(i), toUtf8(caps.szPname).c_str());
    }
}

static int resolveDevice(const char *arg, bool input)
{
    if (arg && *arg && strspn(arg, "0123456789") == strlen(arg))
        return atoi(arg);
    const std::string want = toLower(arg ? arg : "");
    const UINT n = input ? midiInGetNumDevs() : midiOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        wchar_t name[64] = {};
        if (input) {
            MIDIINCAPSW caps{};
            if (midiInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
                continue;
            wcscpy(name, caps.szPname);
        } else {
            MIDIOUTCAPSW caps{};
            if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
                continue;
            wcscpy(name, caps.szPname);
        }
        if (toLower(toUtf8(name)).find(want) != std::string::npos)
            return int(i);
    }
    return -1;
}

static void usage()
{
    printf("midi-probe — WinMM MIDI 测试工具\n"
           "用法:\n"
           "  midi-probe list\n"
           "  midi-probe mon <输入设备名|序号>\n"
           "  midi-probe send <输出设备名|序号> <note> [vel] [ch] [count] [intervalMs]\n"
           "  midi-probe hold <输出设备名|序号> <note> [ms] [vel] [ch]\n"
           "  midi-probe latency <输出设备|序号> <输入设备|序号> [note] [count]\n");
}

int main(int argc, char **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        usage();
        return 1;
    }
    const std::string cmd = argv[1];

    if (cmd == "list") {
        listDevices();
        return 0;
    }

    if (cmd == "mon") {
        if (argc < 3) {
            printf("用法: midi-probe mon <输入设备名|序号>\n");
            return 1;
        }
        const int id = resolveDevice(argv[2], true);
        if (id < 0) {
            printf("未找到输入设备: %s\n", argv[2]);
            listDevices();
            return 1;
        }
        MIDIINCAPSW caps{};
        midiInGetDevCapsW(UINT(id), &caps, sizeof(caps));
        printf("正在监视输入 [%d] %s ... (Ctrl+C 退出)\n", id, toUtf8(caps.szPname).c_str());
        fflush(stdout);
        HMIDIIN in = nullptr;
        const MMRESULT mr = midiInOpen(&in, UINT(id), reinterpret_cast<DWORD_PTR>(&inProc), 0, CALLBACK_FUNCTION);
        if (mr != MMSYSERR_NOERROR) {
            printf("打开输入设备失败: %u\n", unsigned(mr));
            return 1;
        }
        midiInStart(in);
        for (;;)
            Sleep(1000);  // Ctrl+C 结束
    }

    if (cmd == "send") {
        if (argc < 4) {
            printf("用法: midi-probe send <输出设备名|序号> <note> [vel] [ch] [count] [intervalMs]\n");
            return 1;
        }
        const int id = resolveDevice(argv[2], false);
        if (id < 0) {
            printf("未找到输出设备: %s\n", argv[2]);
            listDevices();
            return 1;
        }
        const int note = atoi(argv[3]);
        const int vel = argc > 4 ? atoi(argv[4]) : 100;
        const int ch = argc > 5 ? atoi(argv[5]) : 1;
        const int count = argc > 6 ? atoi(argv[6]) : 1;
        const int gapMs = argc > 7 ? atoi(argv[7]) : 300;

        HMIDIOUT out = nullptr;
        const MMRESULT mr = midiOutOpen(&out, UINT(id), 0, 0, CALLBACK_NULL);
        if (mr != MMSYSERR_NOERROR) {
            printf("打开输出设备失败: %u\n", unsigned(mr));
            return 1;
        }
        const DWORD on = DWORD(0x90 | (ch - 1)) | (DWORD(note) << 8) | (DWORD(vel) << 16);
        const DWORD off = DWORD(0x80 | (ch - 1)) | (DWORD(note) << 8);
        printf("向 [%d] 发送 note=%d (%s%d) vel=%d ch=%d, 共 %d 次\n",
               id, note, kNoteNames[note % 12], note / 12 - 1, vel, ch, count);
        for (int i = 0; i < count; ++i) {
            const MMRESULT r1 = midiOutShortMsg(out, on);
            printf("  -> Note On (mmr=%u)\n", unsigned(r1));
            fflush(stdout);
            Sleep(120);
            midiOutShortMsg(out, off);
            printf("  -> Note Off\n");
            fflush(stdout);
            if (i + 1 < count)
                Sleep(gapMs);
        }
        midiOutClose(out);
        return 0;
    }

    if (cmd == "hold") {
        if (argc < 4) {
            printf("用法: midi-probe hold <输出设备|序号> <note> [ms] [vel] [ch]\n");
            return 1;
        }
        const int id = resolveDevice(argv[2], false);
        if (id < 0) {
            printf("未找到输出设备: %s\n", argv[2]);
            return 1;
        }
        const int note = atoi(argv[3]);
        const int holdMs = argc > 4 ? atoi(argv[4]) : 1000;
        const int vel = argc > 5 ? atoi(argv[5]) : 100;
        const int ch = argc > 6 ? atoi(argv[6]) : 1;

        HMIDIOUT out = nullptr;
        if (midiOutOpen(&out, UINT(id), 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            printf("打开输出设备失败\n");
            return 1;
        }
        printf("向 [%d] 发送 Note On note=%d vel=%d ch=%d，按住 %d ms...\n",
               id, note, vel, ch, holdMs);
        fflush(stdout);
        midiOutShortMsg(out, DWORD(0x90 | (ch - 1)) | (DWORD(note) << 8) | (DWORD(vel) << 16));
        Sleep(holdMs);
        midiOutShortMsg(out, DWORD(0x80 | (ch - 1)) | (DWORD(note) << 8));
        printf("已释放 (Note Off)\n");
        midiOutClose(out);
        return 0;
    }

    if (cmd == "latency")
        return latencyCmd(argc, argv);

    usage();
    return 1;
}
