# ARPS 项目设计

本文档是 ARPS 的统一设计说明，面向后续维护者和最终接入开发者。内容覆盖项目目标、
架构边界、运行形态、设备端与主机端职责，以及当前二进制协议 v1。

## 项目定位

ARPS 是一个 Android 屏幕原始帧采集组件。设备端通过 `app_process` 在 Android
shell 环境运行，不安装为普通应用；它采集主显示屏的 `ARGB_8888` 原始像素，经 LZ4
压缩后，通过 adb reverse/TCP 主动连接主机并推送连续帧。

项目首要目标不是提供完整投屏工具，而是为 C++ 自动化框架提供稳定、低依赖、可嵌入
的屏幕帧输入。

## 目标

- 采集 Android 12/API 31 及以上设备的主屏原始帧。
- 输出无损 `ARGB_8888` 像素数据和必要帧元数据。
- 使用 native LZ4 降低传输体积，避免纯 Java 压缩回退路径。
- 设备端通过 `app_process` 启动，不安装 apk，不保留 Activity/UI。
- 主机端以 C++ `native-client` 作为最终集成边界。
- 通过明确协议承载帧数据、控制消息、错误消息和未来扩展字段。
- 支持启动时点亮屏幕、可选关闭物理屏幕输出、退出时恢复或设置屏幕状态。

## 非目标

- 不做 H.264/H.265/AV1 视频编码。
- 不做音频、触控、剪贴板或完整投屏 UI。
- 不在设备端实现业务识图或自动化框架逻辑。
- 不依赖安装后的 Android package 路径。
- 不把调试构建产物、临时截图或采集样例作为项目基线的一部分。
- 不实现纯 Java LZ4 回退；native 加载失败时应暴露真实失败并修复 native 链路。

## 目录边界

```text
device/                 Android 设备端 app_process 客户端
native-client/          C++ 协议接收、校验和解压库
host-debug/             开发期调试工具，复用 native-client 验证主机链路
third_party/lz4/         vendored LZ4 C 源码
docs/design.md          本统一设计文档
gradle/wrapper/          Gradle Wrapper，随源码提交
```

`host-debug/` 的源码属于项目基线，因为它是当前验证 `native-client` 和设备端链路的
主要入口。它的构建产物、临时截图、抓帧 payload、参考仓库和其他本地状态不提交，
应通过 `.gitignore` 排除。`host-debug/` 的开发期用途和使用方式见
`docs/host-debug.md`。

## 构建形态

设备端产物是一个不安装的 apk：

```text
arps-device.apk
```

选择 apk 的原因是它可以同时承载 dex 和 `lib/<abi>/liblz4arps.so`，并可作为
`CLASSPATH` 交给 `app_process` 运行。设备端启动时通过 `CLASSPATH` 定位自身 apk，
从 zip 中解出当前 ABI 的 native 库到 `/data/local/tmp`，再执行 `System.load`。

启动示例：

```bash
adb push arps-device.apk /data/local/tmp/arps-device.apk
adb shell CLASSPATH=/data/local/tmp/arps-device.apk app_process / com.visotc.ARPS.Main --connect-port=27183
```

Gradle Wrapper 需要随仓库提交：

- `gradlew`
- `gradle/wrapper/gradle-wrapper.jar`
- `gradle/wrapper/gradle-wrapper.properties`

Wrapper 不是构建产物。它固定构建入口和 Gradle 版本，使 Orb Debian VM 或其他干净环境
可以直接运行 `./gradlew`，再由 wrapper 下载 `distributionUrl` 指定的 Gradle。

## 设备端设计

设备端入口为 `com.visotc.ARPS.Main`。核心流程如下：

1. 解析命令行参数。
2. 加载自身 apk 内的 `liblz4arps.so`。
3. 连接主机 TCP 端口。
4. 发送 `HELLO`。
5. 等待主机 `START`。
6. 按启动参数处理屏幕电源状态。
7. 采集、压缩并发送 `FRAME`。
8. 连接关闭、写失败或收到停止请求后清理退出。

设备端只负责采集、压缩、封包和屏幕电源处理。它不提供 UI，不持有业务状态，也不把
自动化框架概念带入设备进程。

## 主机端设计

`native-client` 是最终集成边界。它负责：

- 监听或接管已连接 socket。
- 发送 `START` 配置。
- 读取 ARPS packet。
- 校验协议字段。
- 解压 `raw` 或 `lz4_block` payload。
- 以 `ArpsReadStatus` 区分帧、控制包、超时、关闭和协议错误。
- 复用内部 frame buffer，调用方若需跨帧保存应自行 copy。

adb 管理不强制属于 `native-client`。如果上层自动化框架已有 adb 管理能力，应只复用
协议接收和解压层；如果没有，可以在更高层增加 push、adb reverse、启动和清理逻辑。

## 传输模型

首版使用 TCP。典型链路是：

```text
host listen 127.0.0.1:<port>
host adb reverse tcp:<port> tcp:<port>
device connect 127.0.0.1:<port>
device -> HELLO
host   -> START
device -> FRAME...
host closes socket or device sends STOP/ERROR
```

TCP 提供有序可靠字节流，但它不表达“只要最新帧”的语义。ARPS 当前采取串行
`capture -> compress -> write` 模型：写阻塞时设备端不会继续无限制捕获新帧，从而
避免内存队列堆积。上层如果只关心最新画面，可以在收到完整帧后自行丢弃过时帧。

## 屏幕电源策略

设备端记录连接前屏幕状态，并支持以下启动配置：

- `power_on_if_screen_off`：启动时如果主屏熄灭，注入 POWER 键点亮。
- `turn_screen_off`：开始推流后尝试关闭物理屏幕输出。
- `require_non_black_start`：启动阶段检查黑帧，连续黑帧时拒绝推流。
- `exit_power_mode`：退出时 `restore_previous`、`keep_on` 或 `turn_off`。

关闭物理屏幕输出和按 POWER 键关屏不是同一层能力。前者面向“物理屏幕熄灭但采集仍
继续”的场景；后者会改变设备实际屏幕状态。不同厂商和 Android 版本可能存在差异，
失败时应记录真实错误和设备信息，而不是静默回退。

## 像素与压缩约定

设备端当前采集 `Bitmap.Config.ARGB_8888` 并通过 `copyPixelsToBuffer()` 输出原始
字节。ARPS v1 的消费约定为每像素 4 字节，内存顺序按 `R, G, B, A` 解释。协议层不
做通道转换；显示层或业务层如需 BGRA 等格式，应在消费侧转换。

每帧都携带 `width`、`height`、`row_bytes`、`rotation`、`pixel_format` 和压缩方式。
消费方必须以 `row_bytes` 做行寻址，不得假设它永远等于 `width * 4`。

当前压缩方式：

- `raw`：直接发送原始像素。
- `lz4_block`：发送单帧独立 LZ4 block。

`delta_lz4` 和扩展压缩类型仅保留枚举，当前实现拒绝。

## 协议 v1

ARPS v1 运行在单条有序 TCP 连接上。所有多字节整数均使用 big-endian。协议不依赖
TCP EOF 划分包边界，每个包都必须通过长度字段完整读取。

### 常量

```text
magic            = "ARPSBYVISOTC"  # 12 bytes ASCII
protocol_major   = 1
protocol_minor   = 0
fixed_header_len = 32
default_max_len  = 64 MiB
```

接收端必须拒绝不匹配的 magic、未知的 `protocol_major`、不等于 32 的
`header_len`，以及超过本端上限的 `packet_len`。

### Packet Layout

每个包由 32 字节固定头和一个长度受控的 body 组成：

```text
FixedHeader
base_len:u32
BaseData:base_len bytes
bitmap_len:u32
BitmapPayload:bitmap_len bytes
ext_len:u32
ExtData:ext_len bytes
UnknownTail:packet_len consumed 剩余字节
```

`packet_len` 表示 FixedHeader 后整个 body 的长度，不包含 FixedHeader 自身。当前
已知 body 至少包含三个 section 长度字段，因此最小有效值为 12。

实现要求：

- 解析 section 时不得读取超过 `packet_len`。
- 解析完已知 section 后，必须跳过 `packet_len - consumed` 的未知尾部。
- 不能通过搜索下一段 magic 来恢复包边界。
- 不能把 TCP EOF 当成单个 packet 的结束标记。

### FixedHeader

```text
offset  size  field
0       12    magic
12      2     protocol_major
14      2     protocol_minor
16      2     packet_type
18      2     header_len
20      4     packet_flags
24      4     packet_sequence
28      4     packet_len
```

`packet_flags` 当前固定为 0，保留给后续版本。

`packet_sequence` 是包序号，控制包和图像包都会递增。它不是图像帧号；图像帧号在
`FRAME BaseData.frame_no` 中。

### Packet Types

```text
1 = HELLO
2 = START
3 = FRAME
4 = ERROR
5 = STOP
```

未知 `packet_type` 应作为协议错误处理。

### Control Packets

`HELLO`、`START`、`ERROR`、`STOP` 都使用同一包结构：

```text
base_len   = 0
bitmap_len = 0
ext_len    = UTF-8 JSON length
ExtData    = UTF-8 JSON
```

`HELLO` 由设备端发送，描述设备信息和当前能力。

```json
{
  "device": {
    "manufacturer": "Google",
    "brand": "google",
    "model": "Pixel",
    "android_sdk": 35,
    "android_release": "15"
  },
  "capabilities": {
    "pixel_formats": ["argb8888"],
    "compressions": ["raw", "lz4_block"],
    "screen_power": true,
    "max_packet_len": 67108864,
    "native_lz4": true
  }
}
```

`START` 由主机端发送，设备端收到后开始采集。

```json
{
  "display_id": 0,
  "pixel_format": "argb8888",
  "compression": "lz4_block",
  "max_fps": 30,
  "max_packet_len": 67108864,
  "power_on_if_screen_off": true,
  "turn_screen_off": false,
  "require_non_black_start": true,
  "capture_mode": "auto",
  "exit_power_mode": "restore_previous"
}
```

`ERROR` 由设备端发送，表示设备端异常。主机端应记录并结束当前会话。

```json
{
  "message": "Startup refused: captured frames are still mostly black",
  "type": "java.io.IOException"
}
```

`STOP` 表示设备端正常停止。

```json
{
  "reason": "normal_stop"
}
```

### FRAME Packets

`FRAME` 包承载一帧图像。

```text
base_len   >= 64
BaseData   = FRAME BaseData v1 前 64 字节，后续字节保留扩展
bitmap_len = compressed payload length
ExtData    = UTF-8 JSON，可为空
```

FRAME BaseData v1 前 64 字节固定布局如下：

```text
offset  size  field
0       8     frame_no
8       8     monotonic_time_ns
16      4     width
20      4     height
24      4     row_bytes
28      4     rotation
32      4     pixel_format
36      4     compression_type
40      4     uncompressed_len
44      4     compressed_len
48      4     display_id
52      4     color_space
56      4     payload_checksum
60      4     base_flags
```

字段说明：

- `frame_no`：图像帧号，从 1 开始递增。
- `monotonic_time_ns`：设备端采集时间，使用设备本地 monotonic clock。该值不能和
  主机时钟直接相减，只适合做设备侧阶段耗时或帧间隔分析。
- `width` / `height`：当前帧像素宽高。
- `row_bytes`：解压后每行字节数。
- `rotation`：Android display rotation，`0/1/2/3` 对应 `0/90/180/270` 度。
- `pixel_format`：当前只支持 `1 = ANDROID_ARGB_8888_RAW`。
- `compression_type`：`0 = raw`，`1 = lz4_block`，`2 = delta_lz4`，`3 = extended`。
- `uncompressed_len`：解压后的图像字节数，必须等于 `row_bytes * height`。
- `compressed_len`：压缩 payload 字节数，必须等于外层 `bitmap_len`。
- `display_id`：当前帧来源 display id。
- `color_space`：`0 = unknown`，`1 = sRGB`，`2 = Display-P3`。
- `payload_checksum`：`0` 表示未启用；非 0 时为 BitmapPayload 的 CRC32。
- `base_flags`：当前固定为 0，保留给后续版本。

如果 `base_len > 64`，v1 接收端读取前 64 字节后应跳过剩余 base bytes。

### FRAME ExtData

设备端在 `ExtData` 中发送 UTF-8 JSON，用于诊断和性能统计。字段不是解码所必需，
接收端应允许缺失。

```json
{
  "capture_api": "android.window.ScreenCapture.captureDisplay",
  "android_sdk": 35,
  "compression": "lz4_block",
  "capture_ms": 5.31,
  "copy_ms": 1.42,
  "compress_ms": 2.08
}
```

## 接收端校验

接收端至少应执行以下校验：

- magic、major version、header length、packet length 合法。
- 每个 section 的长度都在 `packet_len` 边界内。
- `FRAME base_len >= 64`。
- `pixel_format == 1`。
- `compressed_len == bitmap_len`。
- `uncompressed_len == row_bytes * height`。
- `payload_checksum != 0` 时，CRC32 匹配 BitmapPayload。
- `raw` payload 长度等于 `uncompressed_len`。
- `lz4_block` 解压成功，且输出长度等于 `uncompressed_len`。

控制包应作为状态返回给调用方，而不是静默丢弃。当前 C++ 接口使用
`ArpsReadStatus` 区分 `Frame`、`Hello`、`Error`、`Stop`、`Timeout`、`Closed` 和
`ProtocolError`。

## 兼容性规则

同一个 `protocol_major` 下允许以下向后兼容扩展：

- 增大 `base_len` 并在 `FRAME BaseData` 后追加字段。
- 在 `ExtData` JSON 中增加字段。
- 在已知 section 后追加新 section；旧接收端通过 `packet_len` 跳过未知尾部。
- 增加新的 `protocol_minor`，但不得改变已定义字段含义。

以下变化必须提升 `protocol_major`：

- 修改 FixedHeader 布局。
- 改变当前已定义字段的字节序、大小或语义。
- 改变 body 中 `base_len + base + bitmap_len + bitmap + ext_len + ext` 的已知前缀顺序。

## 已知限制

- `FLAG_SECURE` 或 DRM 保护内容可能被系统截成黑帧。
- 熄屏采集能力受设备厂商、Android 版本和系统策略影响，需要以真机结果为准。
- TCP 链路在主机读慢时会产生队头阻塞，当前通过串行采集写入避免设备端无界排队。
- `monotonic_time_ns` 属于设备时钟域，不能直接计算主机端到端延迟。
