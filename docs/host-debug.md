# Host Debug Tools

本文档说明 `host-debug/` 的设计定位和使用边界。`host-debug/` 是开发期调试工具集，
用于验证设备端、协议接收层和帧显示链路；它不是最终 C++ 自动化框架的集成边界，也
不是正式交付 API。

## 定位

ARPS 的正式集成边界是 `native-client/`。`host-debug/` 只服务于开发和排障：

- 快速确认设备端是否能连接、握手并推送帧。
- 观察 LZ4 解压后的画面是否正确。
- 查看采集、拷贝、压缩、读取、解码和渲染耗时。
- 在没有 Android 设备时用 mock source 验证主机接收和显示链路。
- 用最小 Python receiver 复核协议包结构和原始 payload。

`host-debug/` 的源码应随项目提交，因为它是当前验证 `native-client` 和设备端链路的
主要入口。构建输出、临时样例、截图和抓帧 payload 继续通过 `.gitignore` 排除。

## 目录结构

```text
host-debug/
  cli/                 SDL 可视化调试主机
  mock-source/         合成帧源，模拟设备端连接主机
  minirecv/            Python 最小协议接收器
```

## `host-debug/cli`

`host-debug/cli` 是主要可视化调试工具。它链接同一套 `native-client` 接收实现，
因此可以验证最终 C++ 接入路径会使用的协议解析、校验和 LZ4 解压逻辑。

职责：

- 监听主机 TCP 地址和端口。
- 接收设备端或 mock source 的 `HELLO`。
- 发送 `START` 配置。
- 持续读取 `FRAME`、`ERROR`、`STOP` 等状态。
- 使用 SDL 展示最新完整帧。
- 在画面上叠加 fps、frame no、payload 大小、读包/解码/渲染耗时和设备端阶段耗时。
- 可选通过 adb 自动启动真实设备端。

构建：

```bash
make -C host-debug/cli
```

该工具依赖 `clang++`、vendored `third_party/lz4`、`native-client` 和本机 SDL2
开发库。构建输出位于 `host-debug/cli/build/`，不应提交。

Windows 构建使用顶层 CMake 入口，首次配置会通过 `FetchContent` 下载 SDL2
`release-2.32.0` 源码，并在本地构建出 `SDL2.dll`：

```powershell
cmake -S host-debug -B host-debug/build-win
cmake --build host-debug/build-win --config Release
```

Release 输出目录会包含 `arps-host-debug.exe`、`arps-mock-source.exe` 和 `SDL2.dll`。

只监听连接：

```bash
host-debug/cli/build/arps-host-debug --port=27183 --compression=lz4_block
```

连接真实 Android 设备：

```bash
host-debug/cli/build/arps-host-debug \
  --serial=<adb-serial> \
  --apk=device/build/outputs/apk/debug/arps-device.apk \
  --port=27183 \
  --compression=lz4_block
```

Windows 上也可以使用 TCP adb serial，例如：

```powershell
host-debug\build-win\Release\arps-host-debug.exe `
  --serial=192.168.65.31:5555 `
  --adb=C:\tools\scrcpy-win64-v3.3.4\adb.exe `
  --apk=device\build\outputs\apk\debug\arps-device.apk `
  --port=27183 `
  --compression=lz4_block
```

带 `--serial` 时，CLI 会执行以下开发期动作：

1. `adb reverse tcp:<port> tcp:<port>`
2. `adb push <apk> /data/local/tmp/arps-device.apk`
3. `adb shell CLASSPATH=/data/local/tmp/arps-device.apk app_process / com.visotc.ARPS.Main ...`

这些动作是调试便利，不应上升为 `native-client` 的职责。最终业务框架如果已有 adb
管理层，应继续由框架负责设备选择、artifact 分发和进程生命周期。

常用参数：

- `--host=127.0.0.1`
- `--port=27183`
- `--compression=raw|lz4_block`
- `--max-fps=30`
- `--display-id=0`
- `--turn-screen-off=true|false`
- `--require-non-black-start=true|false`
- `--capture-mode=auto|surface|bitmap`
- `--exit-power-mode=restore_previous|keep_on|turn_off`
- `--serial=<adb-serial>`
- `--apk=<path>`
- `--adb=<adb-binary>`

## `host-debug/mock-source`

`host-debug/mock-source` 是 C++ 合成帧源，用来模拟设备端。它主动连接主机调试 CLI，
发送 `HELLO`，读取 `START`，然后按 ARPS 协议推送渐变背景和移动色块组成的 synthetic
frames。

用途：

- 不依赖 Android 设备验证 `host-debug/cli` 的显示链路。
- 不依赖设备采集链路验证 `native-client` 的解包和解压。
- 复现主机端渲染、fps、payload 大小、状态包处理等问题。

构建：

```bash
make -C host-debug/mock-source
```

Windows 下 `mock-source` 由 `host-debug` 顶层 CMake 与 `arps-host-debug` 一起构建。

运行示例：

```bash
# Terminal 1
host-debug/cli/build/arps-host-debug --port=27183 --compression=lz4_block

# Terminal 2
host-debug/mock-source/build/arps-mock-source \
  --host=127.0.0.1 \
  --port=27183 \
  --width=640 \
  --height=360 \
  --fps=30
```

`mock-source` 支持 `raw` 和 `lz4_block`，实际压缩方式由主机 `START` 中的
`compression` 决定。

## `host-debug/minirecv`

`host-debug/minirecv/arps_minirecv.py` 是最小 Python receiver，只使用标准库。它适合
排查协议层问题，不负责解压 LZ4 或显示画面。

职责：

- 监听 TCP 端口。
- 读取并打印 `HELLO`。
- 发送 `START`。
- 读取指定数量的 `FRAME`。
- 打印 `FRAME BaseData` 元数据和 `ExtData`。
- 可选保存最后一个压缩 payload。

运行示例：

```bash
python3 host-debug/minirecv/arps_minirecv.py \
  --port 27183 \
  --frames 1 \
  --compression lz4_block
```

配合真实设备：

```bash
adb reverse tcp:27183 tcp:27183
adb push device/build/outputs/apk/debug/arps-device.apk /data/local/tmp/arps-device.apk
adb shell CLASSPATH=/data/local/tmp/arps-device.apk app_process / com.visotc.ARPS.Main --connect-port=27183
```

保存 raw payload：

```bash
python3 host-debug/minirecv/arps_minirecv.py \
  --port 27183 \
  --frames 1 \
  --compression raw \
  --save-payload frame.rgba
```

`minirecv/` 下的 `.lz4`、`.rgba`、`.png` 文件是本地排查样例，不是协议规范的一部分。

## 提交策略

`host-debug/` 的源码、Makefile 和轻量说明文档应进入仓库。以下内容不提交：

- `host-debug/**/build/`
- `host-debug/minirecv/*.lz4`
- `host-debug/minirecv/*.rgba`
- `host-debug/minirecv/*.png`
- `.DS_Store` 和其他本机状态文件

提交源码的原因：

- `arps-host-debug` 是当前最直接的端到端验证入口。
- 它链接并复用 `native-client`，能及时暴露协议接收、校验和解压问题。
- `mock-source` 能在没有 Android 设备时验证主机链路。
- `minirecv` 能用最小依赖排查协议包结构。

边界仍然不变：正式主机集成应依赖 `native-client/`，而不是依赖调试 CLI。

如果未来要把某个调试工具提升为正式项目产物，应先做一次边界收敛：

- 移除样例 payload 和本地截图。
- 将构建产物继续保持 ignored。
- 明确依赖安装方式，例如 SDL2 是否作为可选依赖。
- 让工具复用公开的 `native-client` API，避免复制协议实现。
- 在 CI 或本地验证中加入对应构建检查。
