<div align="center">
<img src="RMDB.jpg" width="25%" />

**RMDB** · 全国大学生计算机系统能力大赛 · 数据库管理系统赛道
</div>

参赛队伍在 RMDB 框架上实现完整关系型数据库内核，并具备运行 TPC-C 常用负载的能力。RMDB 由中国人民大学数据库教学团队开发，并得到教育部-华为“智能基座”项目及大赛技术委员会支持。

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

## 仓库结构

```text
src/                 内核源码（parser / analyze / optimizer / execution / …）
SQL测试/              SQL 回归与性能测试脚本
docs/                 全部文档（见下方导航）
  official/          官方 PDF 与决赛赛题
  problems/          初赛题目
  solutions/         题解
  performance/       性能优化记录（含 xin 优化历程）
  design/            架构与正确性设计
  explain/           分模块源码讲解
rmdb_client/         客户端
deps/                第三方依赖
```

根目录 `题目`、`题解` 为指向 `docs/problems`、`docs/solutions` 的兼容软链接。

## 文档导航

完整索引见 **[docs/README.md](docs/README.md)**。

| 类别 | 链接 |
|------|------|
| 使用 / 环境 / 结构 | [docs/official/RMDB使用文档.pdf](docs/official/RMDB使用文档.pdf) · [环境配置](docs/official/RMDB环境配置文档.pdf) · [项目结构](docs/official/RMDB项目结构.pdf) |
| **xin 优化历程** | [docs/performance/xin优化历程.md](docs/performance/xin优化历程.md) |
| 性能前后对比 | [docs/performance/性能优化前后版本对比.md](docs/performance/性能优化前后版本对比.md) |
| 源码讲解 | [docs/explain/README.md](docs/explain/README.md) |
| 初赛题目 / 题解 | [docs/problems/](docs/problems/) · [docs/solutions/](docs/solutions/) |

## 推荐参考

- [Database System Concepts (Seventh Edition)](https://db-book.com/)
- [PostgreSQL 数据库内核分析](https://book.douban.com/subject/6971366/)
- [数据库系统实现](https://book.douban.com/subject/4838430/)
- [数据库系统概论(第5版)](http://chinadb.ruc.edu.cn/second/url/2)

## License

RMDB 采用 [木兰宽松许可证，第 2 版](https://license.coscl.org.cn/MulanPSL2)。拷贝、修改或分发时请遵守该许可证。
