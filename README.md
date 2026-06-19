# ARPS

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**A**ndroid **R**aw **P**ixel **S**tream —— Android 屏幕原始帧采集组件。

ARPS 在 Android shell 环境通过 `app_process` 运行（不安装为普通应用），采集主显示屏的
`ARGB_8888` 原始像素，经 native LZ4 压缩后，通过 adb reverse / TCP 主动连接主机并推送连续帧。

它的首要目标不是完整投屏工具，而是为 C++ 自动化框架提供稳定、低依赖、可嵌入的屏幕帧输入。

## 特性

- 采集 Android 12 / API 31 及以上设备的主屏原始帧。
- 输出无损 `ARGB_8888` 像素和必要的帧元数据（宽高、`row_bytes`、rotation、color space 等）。
- native LZ4 压缩（`liblz4arps.so`），无纯 Java 回退路径。
- 设备端以 `app_process` 启动，不安装 apk，不保留 Activity / UI。
- 启动时可点亮屏幕、可选关闭物理屏幕输出、退出时恢复或设置屏幕状态。
- 明确的二进制协议 v1.1，承载帧数据、控制消息、错误消息和扩展字段。

### 非目标

- 不做 H.264 / H.265 / AV1 视频编码。
- 不做音频、触控、剪贴板或完整投屏 UI。
- 不在设备端实现业务识图或自动化框架逻辑。
- 不实现纯 Java LZ4 回退；native 加载失败时暴露真实错误并修复 native 链路。

## 架构

```text
device/                 Android 设备端 app_process 客户端（采集 / 压缩 / 封包 / 屏幕电源）
native-client/          C++ 协议接收、校验和解压库 —— 最终集成边界
host-debug/             开发期调试工具，复用 native-client 验证主机链路
  cli/                  SDL 可视化调试主机
  mock-source/          合成帧源，无设备时模拟设备端连接
  minirecv/             Python 最小协议接收器
third_party/lz4/        vendored LZ4 C 源码
docs/                   设计文档
gradle/wrapper/         Gradle Wrapper（随源码提交）
```

典型链路：

```text
host listen 127.0.0.1:<port>
host adb reverse tcp:<port> tcp:<port>
device connect 127.0.0.1:<port>
device -> HELLO
host   -> START
device -> READY
push: device -> FRAME...
pull: host -> FRAME_REQUEST, device -> FRAME
host   -> POWER_CONTROL
host closes socket or device sends STOP/ERROR
```

`native-client` 是上层框架应依赖的正式边界；`host-debug` 仅用于开发和排障，不是交付 API。

## 构建

### 设备端 apk

> Java / Android 构建默认在 OrbStack 的 `debian` VM 内执行，避免污染本机环境。

设备端产物是一个**不安装**的 apk（`arps-device.apk`），它同时承载 dex 和
`lib/<abi>/liblz4arps.so`，并作为 `CLASSPATH` 交给 `app_process` 运行。

```bash
./gradlew :device:assembleDebug
# 产物：device/build/outputs/apk/debug/arps-device.apk
```

设备端启动时通过 `CLASSPATH` 定位自身 apk，从 zip 中解出当前 ABI 的 native 库到
`/data/local/tmp` 下的唯一临时文件，`System.load` 后尽量删除临时文件。

### 主机调试工具（host-debug）

可视化调试主机 `arps-host-debug` 链接同一套 `native-client` 接收实现，依赖 `clang++`、
vendored `third_party/lz4` 和本机 SDL2 开发库。

```bash
# Linux / macOS
make -C host-debug/cli
make -C host-debug/mock-source
```

Windows 使用顶层 CMake，首次配置会通过 `FetchContent` 下载 SDL2 并在本地构建 `SDL2.dll`：

```powershell
cmake -S host-debug -B host-debug/build-win
cmake --build host-debug/build-win --config Release
# 输出：arps-host-debug.exe、arps-mock-source.exe、SDL2.dll
```

## 使用

### 手动启动设备端

```bash
adb push device/build/outputs/apk/debug/arps-device.apk /data/local/tmp/arps-device.apk
adb shell CLASSPATH=/data/local/tmp/arps-device.apk \
  app_process / com.visotc.ARPS.Main --connect-port=27183
```

设备端的最终流模式由主机在 `START.stream_mode` 中下发；命令行 `--stream-mode`
只是在收到 `START` 前的默认值。

### 用调试 CLI 接收并显示

只监听连接：

```bash
host-debug/cli/build/arps-host-debug --port=27183 --compression=lz4_block
```

自动通过 adb 启动真实设备端（开发期便利，会执行 `adb reverse` / `push` / `app_process`）：

```bash
host-debug/cli/build/arps-host-debug \
  --serial=<adb-serial> \
  --apk=device/build/outputs/apk/debug/arps-device.apk \
  --port=27183 \
  --compression=lz4_block
```

常用参数：`--host` `--port` `--compression=raw|lz4_block` `--max-fps` `--display-id`
`--turn-screen-off` `--keep-screen-on` `--capture-mode=auto|hardware|bitmap`
`--stream-mode=push|pull` `--exit-power-mode=restore_previous|keep_on|turn_off`
`--serial` `--apk` `--adb`。

`capture-mode=hardware` 仅在 `lz4_block` 下使用
`ScreenCapture.captureDisplay + AHardwareBuffer lock + JNI LZ4` 直接压缩
`HardwareBuffer`；`bitmap` 保留旧的 `Bitmap.copy + copyPixelsToBuffer` 路径；
`auto` 优先尝试 `hardware`，失败后回退到 `bitmap`。该路径只额外链接 Android 系统
`libandroid.so` 以访问 NDK `AHardwareBuffer` API，未引入新的第三方依赖。

### 无设备验证主机链路（mock-source）

```bash
# Terminal 1
host-debug/cli/build/arps-host-debug --port=27183 --compression=lz4_block

# Terminal 2
host-debug/mock-source/build/arps-mock-source \
  --host=127.0.0.1 --port=27183 --width=640 --height=360 --fps=30
```

### Python 最小接收器（minirecv）

只用标准库排查协议层，不解压 LZ4、不显示画面：

```bash
python3 host-debug/minirecv/arps_minirecv.py \
  --port 27183 --frames 1 --compression lz4_block --stream-mode pull
```

`minirecv --stream-mode pull` 会在 `START.stream_mode` 中要求设备进入 pull 模式，
因此配套设备命令无需重复传 `--stream-mode=pull`。

## 协议 v1.1（概要）

ARPS v1.1 运行在单条有序 TCP 连接上，多字节整数一律 big-endian，包边界由长度字段划分
（不依赖 TCP EOF）。

```text
magic            = "ARPSBYVISOTC"   # 12 bytes ASCII
protocol_major   = 1
protocol_minor   = 1
fixed_header_len = 32
default_max_len  = 64 MiB
```

每个包由 32 字节 `FixedHeader` 和长度受控的 body 组成，body 依次为
`base_len + BaseData + bitmap_len + BitmapPayload + ext_len + ExtData + UnknownTail`。

包类型：`1=HELLO` `2=START` `3=READY` `4=FRAME_REQUEST` `5=FRAME`
`6=ERROR` `7=STOP` `8=POWER_CONTROL`。控制包以 UTF-8 JSON 承载在 `ExtData`；
`FRAME` 在 64 字节定长 `BaseData` 中携带帧元数据，`BitmapPayload` 为 `raw` 或
`lz4_block` 压缩像素。

`START.power_on_if_screen_off` 负责新连接或重新 init ARPS 时按需点亮。复用已有连接
执行下一轮任务时，主机端应发送 `POWER_CONTROL`：
`{"keep_screen_on":true,"power_on_if_screen_off":true,"reason":"task_start"}`。
任务结束只发送 `{"keep_screen_on":false,"reason":"task_end"}` 释放 wake lock，不
发送 `STOP`、不重发 `START`、不触发 `exit_power_mode`。

像素约定：每像素 4 字节，内存顺序按 `R, G, B, A` 解释；行寻址必须使用 `row_bytes`，
不得假设等于 `width * 4`，但接收端必须拒绝小于 `width * 4` 的 `row_bytes`。
协议层不做通道转换。

完整规范见 [docs/design.md](docs/design.md)，调试工具说明见 [docs/host-debug.md](docs/host-debug.md)。

## 致谢

ARPS 的实现离不开以下两个开源项目，在此致谢：

- [**LZ4**](https://github.com/lz4/lz4) —— 极速无损压缩算法。设备端用它压缩原始帧、主机端解压，
  源码以 vendored 形式放在 [third_party/lz4](third_party/lz4)（遵循其 BSD 2-Clause 许可）。
- [**SDL2**](https://github.com/libsdl-org/SDL) —— 跨平台多媒体库。`host-debug` 的可视化调试主机
  用它创建窗口、渲染帧并叠加性能信息（遵循其 zlib 许可）。

感谢这两个项目长期的高质量维护，让 ARPS 能把精力集中在采集与协议本身。

设计思路上，ARPS 还参考并借鉴了以下项目，特别致谢：

- [**DroidCast_raw**](https://github.com/VisoTC/DroidCast_raw) —— 提供了通过 `app_process`
  运行、不安装应用直接采集原始屏幕像素的整体思路。
- [**scrcpy**](https://github.com/genymobile/scrcpy) —— 在 adb reverse 链路、`app_process`
  启动方式和屏幕电源处理等设备端工程实践上提供了重要参考。

## 已知限制

- `FLAG_SECURE` / DRM 保护内容可能被系统截成黑帧。
- 熄屏采集能力受设备厂商、Android 版本和系统策略影响，以真机结果为准。
- push 模式下主机读慢时 TCP 链路会产生队头阻塞，当前通过串行
  `capture -> compress -> write` 避免设备端无界排队；pull 模式下由
  `FRAME_REQUEST` 显式驱动一请求一帧。
- `monotonic_time_ns` 属于设备时钟域，不能直接计算主机端到端延迟。

## 许可证

本项目采用 [**MIT**](LICENSE) 许可证，`Copyright (c) 2026 VisoTC`。
你可以自由使用、复制、修改和分发本项目代码，只需在副本中保留版权声明和许可证文本。

打包的第三方组件保留各自的许可证：vendored LZ4 为 BSD 2-Clause（见
[third_party/lz4/LICENSE](third_party/lz4/LICENSE)），SDL2 为 zlib 许可。
