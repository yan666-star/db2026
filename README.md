<div align="center">
<img src="RMDB.jpg" width="25%" />

**RMDB** · 关系型数据库管理系统
</div>

RMDB 是我们从零实现的一个关系型数据库内核：以 C++17 编写，采用页面式存储与 B+Tree 索引，实现了 MVCC（SNAPSHOT ISOLATION / SERIALIZABLE）、WAL 日志与崩溃恢复，并通过 Wire v3 协议对外提供服务，能够稳定运行 TPC-C 饱和负载。项目在 RMDB 教学框架基础上完成，内核实现与性能优化均由本团队完成。

## 快速开始

```bash
cmake -S . -B build
cmake --build build -j
./build/bin/rmdb <database_name>
```

## 实验环境

| 项目 | 要求 |
|------|------|
| 操作系统 | Ubuntu 18.04+（64 位） |
| 语言 / 编译器 | C++17 / GCC 7.1+ |
| 构建 | CMake 3.16+ |
| 其他 | flex、bison、readline |

## 系统能力

- **存储**：4KB 页面式 Heap 存储、B+Tree 索引、64 分片 Buffer Pool 与 RAII 页面守卫。
- **事务**：READ COMMITTED（锁式）、SNAPSHOT ISOLATION（MVCC 快照）、SERIALIZABLE（SSI 依赖检测）三档隔离级别；事务私有写批与 256 分片冲突检测。
- **持久化**：WAL 日志、1ms group commit、静态检查点与 `redo` / `undo` 崩溃恢复。
- **协议**：Wire v3，支持 `EXEC_STREAM`、`PREPARE_SET` 与 `EXEC_BATCH` 批执行。

## 仓库结构

```text
src/                 内核源码（parser / analyze / optimizer / execution / …）
SQL测试/              SQL 回归与性能测试脚本
docs/                 全部文档（见下方导航）
  design/            设计与实现、正确性保障
  explain/           分模块源码讲解
  performance/       性能优化历程与结果
  problems/          初赛题目
  solutions/         题解
  official/          官方章程与赛题资料
rmdb_client/         客户端
deps/                第三方依赖
```

## 文档导航

| 类别 | 链接 |
|------|------|
| 设计与实现 | [docs/design/设计与实现文档.md](docs/design/设计与实现文档.md) |
| 正确性保障 | [docs/design/正确性门槛关键要求.md](docs/design/正确性门槛关键要求.md) |
| 源码讲解 | [docs/explain/README.md](docs/explain/README.md) |
| 性能优化历程 | [docs/performance/xin优化历程.md](docs/performance/xin优化历程.md) |
| 性能前后对比 | [docs/performance/性能优化前后版本对比.md](docs/performance/性能优化前后版本对比.md) |

完整索引见 [docs/README.md](docs/README.md)。

## 推荐参考

- [Database System Concepts (Seventh Edition)](https://db-book.com/)
- [PostgreSQL 数据库内核分析](https://book.douban.com/subject/6971366/)
- [数据库系统实现](https://book.douban.com/subject/4838430/)
- [数据库系统概论(第5版)](http://chinadb.ruc.edu.cn/second/url/2)

## License

RMDB 采用 [木兰宽松许可证，第 2 版](https://license.coscl.org.cn/MulanPSL2)。拷贝、修改或分发时请遵守该许可证。
