# 源码讲解（docs/explain）

按 `src/` 子系统拆分的源码讲解。每个子目录：

- `README.md`：模块总览
- `*.详细讲解.md`：单文件精读

## 入口

| 文档 | 说明 |
|------|------|
| [00-源码总览.md](00-源码总览.md) | SQL 主链路与根目录文件归属 |
| [portal.md](portal.md) | Plan → Executor 桥接 |

## 模块一览

| 模块 | 入口 |
|------|------|
| parser | [parser/README.md](parser/README.md) |
| analyze | [analyze/README.md](analyze/README.md) |
| optimizer | [optimizer/README.md](optimizer/README.md) |
| execution | [execution/README.md](execution/README.md) |
| storage | [storage/README.md](storage/README.md) |
| record | [record/README.md](record/README.md) |
| index | [index/README.md](index/README.md) |
| replacer | [replacer/README.md](replacer/README.md) |
| system | [system/README.md](system/README.md) |
| transaction | [transaction/README.md](transaction/README.md) |
| common | [common/README.md](common/README.md) |

## 说明

- 文内源码链接均为相对路径 `../../src/...`，可在仓库内直接跳转。
- 讲解以当前 `xin` 分支代码为准。
- 决赛性能相关演进见 [../performance/xin优化历程.md](../performance/xin优化历程.md)。
