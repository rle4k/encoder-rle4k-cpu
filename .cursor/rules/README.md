# encoder-rle4k-cpu Cursor 规则

本目录存放受 git 管理的 Cursor Agent 规则（`.mdc`），供 IDE 在会话中自动加载。

## 规则索引

| 文件 | 范围 | 说明 |
|------|------|------|
| `0001-general.mdc` | 始终 | 通用 AI 编程行为红线 |
| `0002-project-overview.mdc` | 始终 | 项目总览与技术栈 |
| `0003-build-policy.mdc` | `*.ps1`/`*.bat`/`CMakeLists.txt` | 构建策略与脚本约定 |
| `0004-code-intelligence.mdc` | 始终 | GitNexus 代码智能 |
| `0005-no-fabrication.mdc` | 始终 | 禁止杜撰不存在的文件/链接/符号 |
| `0006-project-structure.mdc` | 始终 | 目录结构与写入权限 |
| `0007-git-remotes.mdc` | 始终 | 推送目标：Gitea + Gitee；禁止默认推 GitHub |

## 维护

- 规范变更时同步更新对应 `.mdc` 与 `AGENTS.md`
- 新增规则：单文件单主题，使用 YAML frontmatter（`description`、`globs`、`alwaysApply`）
- 勿将本目录加入 `.gitignore`
