# 项目协作规则

## 文档同步

- `AGENTS.md` 和 `CLAUDE.md` 内容必须完全相同；修改任意一个时必须同步更新另一个。

## 本机覆盖配置

- 如果项目根目录存在 `AGENTS.override.md` 或 `CLAUDE.override.md`，请先读取并遵守其中的本机配置。
- 本机覆盖配置不属于项目通用规则，不应提交到当前项目仓库。

## Git

- 执行 `git commit` 时必须使用提权。
- Commit 使用 Conventional Commits，如 `feat: 新增功能`、`fix(api): 修复接口错误`。
- Commit 类型前缀和可选 scope 使用英文，冒号后的说明必须使用中文。
- Commit message 不附加 `Co-Authored-By` 行。

## 目录约定

- `references/` 存放参考仓库，默认只读；除非明确要求，不在其中做项目实现改动。
- `docs/` 存放当前项目设计文档；协议和项目决策变更应同步更新相关文档。
