# 项目协作规则

## 文档同步

- `AGENTS.md` 和 `CLAUDE.md` 内容必须完全相同；修改任意一个时必须同步更新另一个。

## 工具限制

### node 与 npm

- 除非必要不使用 `npm`，优先使用 `pnpm`。

## Java / Android 构建环境

- 为避免污染本机环境，Java/Android 构建默认在 OrbStack 的 `debian` VM 内执行，入口使用 `orb -m debian ...` 或 `ssh debian@orb`。
- macOS workspace 在 VM 内可通过 `/mnt/mac/Users/visotc/src/i'need'power` 访问；不要为了构建在本机安装 Java、Android SDK 或 NDK。
- VM 内 Android 环境集中放在 `/opt/ineedpower`：`ANDROID_HOME=/opt/ineedpower/android-sdk`，`GRADLE_USER_HOME=/opt/ineedpower/gradle`。
- native LZ4 是既定方向：设备端用 NDK/CMake 构建 `liblz4arps.so`，打进同一个不安装 apk 的 `lib/<abi>/`，运行时由 Java 端从自身 apk 解压后 `System.load`。
- 不要实现或回退到纯 Java LZ4；如果 native 加载失败，应记录真实失败并修 native 链路。
- Orb Debian 是 arm64，但 Android NDK 只提供 `linux-x86_64` host 工具；`ndk-build` 会报 `Unknown host CPU architecture: aarch64`，不要使用。后续 native 构建走 Gradle `externalNativeBuild.cmake` / CMake toolchain。

## Git

- 执行 `git commit` 时必须使用提权。
- Commit 使用 Conventional Commits，如 `feat: 新增功能`、`fix(api): 修复接口错误`。
- Commit 类型前缀和可选 scope 使用英文，冒号后的说明必须使用中文。
- Commit message 不附加 `Co-Authored-By` 行。

## 目录约定

- `references/` 存放参考仓库，默认只读；除非明确要求，不在其中做项目实现改动。
- `docs/` 存放当前项目设计文档；协议和项目决策变更应同步更新相关文档。
