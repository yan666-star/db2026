# 文档导航

本目录按用途分层，避免赛题 PDF、题解笔记、性能实验记录和源码讲解混在一起。

## 目录结构

| 目录 | 内容 |
|------|------|
| [official/](official/) | 官方章程、技术方案、使用/环境/结构 PDF、决赛赛题与测试说明 |
| [problems/](problems/) | 初赛题目（第 1–10 题 + 性能测试） |
| [solutions/](solutions/) | 题解与实现笔记 |
| [performance/](performance/) | 性能优化计划、日记、前后对比与 **xin 优化历程** |
| [design/](design/) | 架构设计、正确性门槛、no-global-wait 方案 |
| [explain/](explain/) | 按模块整理的源码讲解（原 explianDocs） |
| [superpowers/](superpowers/) | 分阶段实施计划与设计规格 |

根目录保留兼容软链接：`题目` → `docs/problems`，`题解` → `docs/solutions`。

## 建议阅读顺序（答辩 / 接手）

1. [performance/xin优化历程.md](performance/xin优化历程.md) — `xin` 分支优化目标、阶段、手段与结果  
2. [performance/性能优化前后版本对比.md](performance/性能优化前后版本对比.md) — 正式评测前后数据  
3. [design/设计与实现文档.md](design/设计与实现文档.md) — 实现思路提纲  
4. [explain/README.md](explain/README.md) — 模块讲解入口  
5. [official/](official/) — 需要核对规则时再翻官方材料  

## 快速入口

- 构建与运行：仓库根目录 [README.md](../README.md)
- Agent 约定：根目录 [AGENTS.md](../AGENTS.md)
- SQL 回归与性能脚本：`SQL测试/`
