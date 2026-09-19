# SuperMidiMap

<div align="center">

**低延迟 MIDI 打击垫音符号重映射器**

给无法自定义键值的 MIDI 打击垫实时改映射，通过系统虚拟 MIDI 端口送进 DAW

![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20macOS-blue)
![CI](../../actions/workflows/build.yml/badge.svg)
![Qt](https://img.shields.io/badge/Qt-6.10-green)

</div>

## 这是什么

很多入门级 MIDI 打击垫（如 `impactx_4x5_pad` 这类 4×4 垫面设备）出厂键位固定、无法自定义。
SuperMidiMap 在系统和 DAW 之间插入一层**实时翻译**：打击垫 → SuperMidiMap（查表改音符号/通道）→ 虚拟 MIDI 端口 → DAW。

```
Windows:
impactx_4x5_pad（出厂固定键位）
    ↓ MIDI 输入
SuperMidiMap（回调线程内查表直转，实测附加延迟 ~0.3ms）
    ↓ Default App Loopback (A) 的 OUT
    ↓ Windows MIDI 服务自动交叉环回 → (B) 的 IN
DAW 输入设备选择 "Default App Loopback (B)"

macOS:
打击垫 → SuperMidiMap → IAC 总线（Audio MIDI Setup 启用）→ DAW
```

> ⚠️ **Windows 交叉环回**（实测确认）：微软 MIDI 服务的 "Default App Loopback (A)/(B)"
> 是一对交叉环回——从 (A) 的 OUT 发出的消息从 **(B)** 的 IN 出现，反之亦然。
> 所以 DAW 要选 **(B)**。程序界面会自动提示当前接线，并拒绝会造成自激回环的组合。

## 功能

- **4×4 实时打击垫网格**：按下发光（力度映射亮度），每个垫显示源音符与映射目标
- **源音符与目标音符均可自定义**：出厂键位仅作默认，任何输入音符都能映射到任何输出
- **输入过滤**：音符选择框支持直接键入音名或号码（如 `c4`、`60`）实时过滤，回车提交
- 点击垫修改：源音符、目标音符、输出通道（可跟随原通道）、静音；源音符冲突时状态栏警告
- 未映射的音符及 CC、弯音等消息原样透传（可全局关闭）
- Note On/Off 严格配对：映射中途修改不"卡音"，程序自动补发 Note Off
- 设备热插拔检测与自动重连；Panic 一键关闭全部音符；防自激保护
- **配置档**：界面下拉框一键切换已保存的配置，JSON 格式，退出自动保存
- **系统托盘**：最小化/关闭窗口都驻留托盘，点击图标还原
- 防自激保护：输入/输出配成会回环的组合时拒绝启动并红字提示

## 实测延迟（WinMM + Windows MIDI 服务）

| 链路 | 平均延迟 |
|---|---|
| 环回直连（不含程序） | 0.15 ms |
| 全链路（注入 → 程序翻译 → 收回，含两次环回穿越） | 0.28 ms |

## 使用方法

1. 从 [Releases](../../releases/latest) 下载并解压（或按下一节自行构建）
2. **Windows**：启动 `SuperMidiMap.exe`，输入选你的打击垫（自动预选），输出选 `Default App Loopback (A)`；DAW 的 MIDI 输入选 `Default App Loopback (B)`
3. **macOS**：先在「音频 MIDI 设置 → 窗口 → 显示 MIDI 工作室 → 双击 IAC 驱动」启用 IAC 总线并新建一个总线；然后 SuperMidiMap 输出选该总线，DAW 的 MIDI 输入也选同一个 IAC 总线
4. 点击网格中的垫修改映射，立即生效

关闭/最小化窗口后程序驻留托盘继续工作；托盘图标右键 → 退出 彻底关闭。

## 从源码构建

依赖：Qt 6.10+（qtbase 组件即可）、CMake 3.21+、Ninja。

**Windows**（MSVC 2022）：

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt安装目录>
cmake --build build
windeployqt build\SuperMidiMap.exe
```

**macOS**（Xcode Command Line Tools）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt安装目录>
cmake --build build
macdeployqt build/SuperMidiMap.app
```

仓库自带 GitHub Actions（`.github/workflows/build.yml`）：push 到 main 自动构建 Windows + macOS，
推送 `v*` 标签（如 `git tag v0.3.0 && git push --tags`）会自动把两个平台的压缩包发布到 Releases。

## 项目结构

```
src/backends/   平台 MIDI 后端（WinMM / CoreMIDI）
src/MidiEngine  平台无关的翻译内核（回调线程内查表直转，无锁读翻译表）
src/PadGrid     4×4 打击垫网格控件
src/MainWindow  主窗口（设备/映射/配置档/托盘）
tools/          midi-probe：WinMM MIDI 测试工具（list / mon / send / latency）
```

## 已知边界

- 系统消息（SysEx、MTC、时钟）不转发（打击垫类设备通常不使用）
- macOS 支持为实验性：CoreMIDI 后端与 IAC 接线未经长期实测，欢迎 issue/PR
- WinMM 看到的设备名最长 32 字符，同名设备请用序号区分

## 许可证

尚未指定开源许可证；Releases 中的二进制可自由使用。如需源码许可可提 issue 讨论。
