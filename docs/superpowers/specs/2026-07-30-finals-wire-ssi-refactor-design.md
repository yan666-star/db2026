# 决赛 Wire Protocol、类型化执行与 SSI 重构设计

日期：2026-07-30  
状态：已确认，可进入实施计划  
适用仓库：`D:\DMS-DESIGN\db2026`  
交叉测试资源：`D:\DMS-DESIGN\TPCC-Tester-main`

## 1. 设计目标

本设计以《2026年全国大学生计算机系统能力大赛数据库系统设计赛-决赛赛题》为最高规范，
完成以下三个目标：

1. 彻底删除历史 NUL 结尾文本 SQL 网络协议，服务器默认且仅实现 RMDB Wire Protocol v3。
2. 将 SQL 执行结果从字符表格和 `output.txt` 中解耦，支持 `EXEC_STREAM`、
   `PREPARE_SET` 和 `EXEC_BATCH` 的类型化执行路径。
3. 重构 SERIALIZABLE 下的 SSI 冲突检测，使 victim 和中止时机严格满足赛题额外契约：
   当前 SELECT、INSERT、UPDATE 或 DELETE 新增 rw 反依赖并形成危险结构时，
   必须立即中止当前语句所属事务，并在返回 `TRANSACTION_ABORT` 前完成整笔事务回滚。

设计还规定如何利用 TPCC-Tester 构造本地决赛同构测试，但任何 Tester 行为都不能反向改变
数据库语义。发生冲突时的优先级为：

```text
决赛正式赛题
> db2026 通用数据库语义
> TPCC-Tester 本地测试实现
```

## 2. 当前实现与问题

### 2.1 网络入口

当前 `src/rmdb.cpp` 直接承担：

- TCP 监听和连接线程创建；
- 单次 `read(fd, ..., BUFFER_LENGTH)`；
- SQL 文本识别、Parser 调用和事务生命周期；
- 把执行结果写入固定字符缓冲区；
- 单次 `write(fd, ...)` 返回；
- `set output_file off` 和 `output.txt` 行为。

该结构不满足决赛要求：

- 没有 8 字节 `RMDB/3.0` 握手；
- 没有统一 8 字节 frame header；
- 没有大端序、1 MiB 上限和严格 payload 消费；
- 没有 `read_exact` / `write_all`；
- 没有 `EXEC_STREAM`、`PREPARE_SET`、`EXEC_BATCH`；
- 没有类型化 schema、ROW、参数和值；
- 没有 AUTO_ABORT 的唯一失败响应；
- 排名路径仍重复传输和解析完整 SQL；
- 查询结果仍与字符表格、`output.txt` 耦合。

### 2.2 执行层

当前 `Context` 通过 `char *data_send_` 和 `int *offset_` 携带网络输出状态，
`QlManager::select_from` 使用 `RecordPrinter` 把 tuple 格式化为文本。
这会丢失 FLOAT32 原始位模式，也无法构造 PREPARE schema 和 typed batch result。

当前 AST 只有字面量节点，不支持 `$1...$n` 参数引用。prepared 路径若继续把参数拼回 SQL，
会重复解析并破坏 CHAR、FLOAT32 和 NULL 保留编码的类型语义。

### 2.3 SSI

当前实现已经维护：

- SERIALIZABLE 的记录读集合；
- 表级谓词读集合；
- incoming/outgoing rw 边；
- 两事务环和三事务提交顺序判断；
- 事务完成后的依赖清理。

但仍存在必须修正的边界：

- UPDATE/DELETE 构造扫描器时关闭 SERIALIZABLE 读跟踪；
- 写路径先安装 pending version，再检查 SSI 危险结构；
- `mark_mvcc_txn_aborted` 只设置标志，不代表已完整回滚；
- SSI 逻辑与 MVCC、版本写入和异常抛出混杂；
- 没有决赛固定 victim、立即中止和协议终结的确定性历史测试；
- 没有证明空范围读在顺序扫描和索引扫描中得到同一语义。

### 2.4 TPCC-Tester

当前 Tester 是旧文本客户端：

- SQL 使用 `?` 字符串替换；
- 一条 SQL 一次文本往返；
- 返回值通过竖线字符表格解析；
- benchmark 使用固定每线程事务数；
- 冲突路径无限重试；
- checker 只有 4 项简化检查；
- 初始 `order_line` 被近似为每订单 10 行；
- 当前负载不是 30 秒预热加连续 3×150 秒窗口。

`docs/architecture.md` 描述了 Wire v3 目标结构，但尚未在源码中实现，而且保留了
`legacy | v3` 与 `cursor_compat` 过渡层。根据本次已确认选择，最终不保留这些过渡路径。

## 3. 实施范围与分解

本任务分为三个连续子项目：

### 子项目 A：Wire v3 与类型化执行

- 拆分 `rmdb.cpp`；
- 实现握手、frame、typed cell 和三类请求；
- 新增类型化执行结果接口；
- 新增 AST 参数节点和执行绑定；
- 删除旧文本协议、`output.txt` 和输出开关；
- 迁移 C++ 客户端与本地 Python/Rust 测试客户端。

### 子项目 B：SSI 固定 victim 与立即中止

- 统一记录读、谓词读、空范围读；
- 修复 UPDATE/DELETE 的读跟踪；
- 把 rw 边插入与危险结构检查封装为明确决策；
- 危险结构由当前语句闭合时固定中止当前事务；
- 在协议响应前同步完成回滚和依赖清理；
- 建立确定性 SI/SER 历史测试。

### 子项目 C：决赛要求、测试与性能文档

- 写入完整赛题解析；
- 给出本地测试方法和配置方法；
- 给出 Tester 使用和升级边界；
- 建立最终要求到源码、测试和验证结果的追踪矩阵；
- 区分已验证、结构检查通过、仅设计、因环境未执行四种状态。

## 4. 总体架构

```text
TCP Listener
  -> ConnectionSession
     -> Handshake
     -> FrameReader
     -> RequestDispatcher
        -> ExecStreamHandler
        -> PrepareSetHandler
        -> ExecBatchHandler
           -> SqlExecutionService
              -> Parser / Analyze / Planner / Portal / Executor
              -> TransactionManager
                 -> SSI conflict decision
              -> TypedResultSink
     -> FrameWriter
```

### 4.1 `rmdb.cpp`

重构后只负责：

- 初始化系统组件；
- 打开监听端口；
- 接受连接；
- 创建 `ConnectionSession`；
- 服务关闭和资源回收。

初版保留一连接一工作线程。正式并发为 32 连接，协议规定同一连接只有一个 outstanding
request；吞吐重点是 prepared plan、typed bind、batch 和聚合响应，不需要先引入 epoll。

### 4.2 网络模块

建议新增：

```text
src/network/
├── wire_protocol.h
├── wire_codec.h/.cpp
├── socket_io.h/.cpp
├── connection_session.h/.cpp
├── request_dispatcher.h/.cpp
├── prepared_dictionary.h/.cpp
└── response_builder.h/.cpp
```

网络层不依赖 TPC-C 表名、事务类型或固定 statement id。

### 4.3 SQL 执行服务

新增 `SqlExecutionService`，作为协议层与既有 SQL 流水线的唯一桥梁。
执行结果统一为：

```text
CommandOk
Query(schema, row stream)
TransactionAbort(diagnostic)
Error(diagnostic)
```

执行层不再知道 socket，不再写 `output.txt`，也不再把 tuple 格式化成字符表格。

### 4.4 会话状态

每个 `ConnectionSession` 独立持有：

- 协议状态；
- 会话隔离级别；
- 当前事务；
- prepared dictionary；
- 请求/响应有界缓冲区；
- 本连接错误和终结状态。

连接之间只能共享不可变解析或计划模板。参数、Executor、事务、结果和 statement id
作用域必须隔离。

## 5. Wire Protocol v3

### 5.1 握手

TCP 建立后客户端发送：

```text
offset 0: ASCII "RMDB"
offset 4: u16 major = 3
offset 6: u16 minor = 0
```

多字节整数使用大端序。服务器完整读取 8 字节并原样回送。魔数或版本不支持时关闭连接，
不得把握手字节传给 SQL Parser。

### 5.2 frame

统一 header：

```text
u32 payload_bytes
u8  tag
u8  flags
u16 reserved
```

要求：

- payload 最大 1 MiB；
- reserved 必须为 0；
- 除 `EXEC_BATCH` 外请求 flags 必须为 0；
- `EXEC_BATCH` flags 必须为 `0x01`；
- 响应 flags/reserved 必须为 0；
- 未知 tag/type/flag、截断字段和尾随字节均拒绝；
- 长度在分配内存前验证；
- socket 读写使用完整循环处理分片、短读、短写、EINTR 和 EOF。

### 5.3 tag

客户端请求：

```text
0x20 EXEC_STREAM
0x21 PREPARE_SET
0x22 EXEC_BATCH
```

服务器响应：

```text
0x01 META
0x02 ROW
0x10 COMMAND_OK
0x11 RESULT_END
0x12 TRANSACTION_ABORT
0x13 ERROR
0x14 PREPARE_OK
0x15 BATCH_RESULT
```

### 5.4 类型

```text
0x01 INT32
0x02 FLOAT32
0x03 CHAR
```

cell：

```text
u8 present
if present == 1:
  INT32: i32 big-endian
  FLOAT32: u32 raw IEEE-754 binary32 bits, big-endian
  CHAR: u32 length + raw bytes
```

CHAR 不带页内 padding 和 NUL。FLOAT 不经过十进制文本格式化。

### 5.5 EXEC_STREAM

请求 payload 是不含 NUL 的非空 UTF-8 SQL。

查询严格返回：

```text
META -> ROW* -> RESULT_END
```

`RESULT_END.row_count` 必须等于 ROW frame 数。非查询成功只返回空 `COMMAND_OK`。
并发控制主动回滚返回 `TRANSACTION_ABORT`；普通语法、语义和不可重试执行错误返回 `ERROR`。

### 5.6 PREPARE_SET

每连接字典支持 1..256 个 statement：

- statement id 非零且唯一；
- `$1...$n` 必须形成稠密集合；
- 参数按声明类型绑定；
- command schema 列数为 0；
- query schema 的列数、顺序和类型必须与真实投影一致；
- 所有语句在临时字典中验证成功后，才原子替换旧字典；
- 失败不留下部分安装状态。

字典项持有不可变 parsed/analyzed/plan template、参数类型、结果 schema 和 schema generation。
Executor 和运行时表达式不能跨 operation 复用。

### 5.7 EXEC_BATCH

服务器先完整解码并验证 batch，再执行任何 operation。

要求：

- 1..256 个 operation；
- 参数数和类型来自 prepared dictionary；
- operation 严格顺序执行；
- command 不产生逐条 ACK；
- query 结果暂存在 1 MiB 有界 response builder；
- 成功只返回一个 `BATCH_RESULT(OK)`；
- 失败丢弃全部部分查询结果。

AUTO_ABORT 失败响应前：

```text
识别失败 operation
-> 完整回滚活动事务
-> 清理会话事务状态
-> 丢弃部分结果
-> 返回唯一 BATCH_RESULT
```

状态：

- `0 OK`；
- `1 TRANSACTION_ABORT`，可重试；
- `2 ERROR`，不可恢复。

失败时：

```text
failed_operation == executed_operations
result_count == 0
```

### 5.8 COMMIT 持久化边界

COMMIT operation 只有在对应 commit WAL record 的新正字节写入已被同一 ACK 窗口内
可核验的 fsync/fdatasync 等稳定化操作覆盖后，才能形成成功响应。

允许 group commit，但每个 ACK 都必须等待覆盖自己的 durable watermark。

## 6. 参数绑定与类型化结果

### 6.1 AST

新增 `ParamRef`：

```text
ParamRef {
  ordinal: 1-based
  declared_type
}
```

Parser 在 PREPARE 阶段识别 `$n`，但不创建具体 literal。Analyze 阶段验证参数所在表达式
允许的类型，Planner 保留参数槽位。

### 6.2 执行绑定

每次 operation 创建：

```text
ExecutionBindings {
  values: typed Value[]
}
```

表达式求值从 bindings 读取参数。禁止未经转义地拼回 SQL，禁止在 batch 中重新解析完整 SQL。

### 6.3 输出 schema

新增：

```text
OutputColumn {
  name
  ColType
}
```

普通列从 `ColMeta` 推导；聚合列按函数规则推导：

- COUNT -> INT32；
- MIN/MAX -> 输入类型；
- SUM(INT) -> INT32；
- SUM(FLOAT) -> FLOAT32。

别名优先作为 Wire 列名。

### 6.4 TypedResultSink

`Executor` 返回的 `RmRecord` 按输出 `ColMeta` 直接编码：

- INT 读取 4 字节；
- FLOAT 读取 4 字节原始位；
- CHAR 去除页内尾部 NUL padding，只发送逻辑字节。

`EXEC_STREAM` 使用流式 sink；`EXEC_BATCH` 使用有界内存 sink。

## 7. SSI 设计

### 7.1 数据结构

```text
SsiTxnEntry {
  txn_id
  start_ts
  commit_ts
  state
  record_reads
  predicate_reads
  incoming_rw
  outgoing_rw
}
```

rw 方向固定为：

```text
reader --rw--> writer
```

仅两个重叠的 SERIALIZABLE 事务之间建立。SI 排名事务不进入 SSI 图。

### 7.2 读跟踪

- SELECT、UPDATE、DELETE 在扫描前注册谓词；
- 即使返回 0 行也保留空范围谓词；
- 实际可见且命中的行注册 record read；
- 顺序扫描和索引扫描使用同一逻辑谓词；
- 无法精确规范化的谓词保守退化为表级读，不能漏报。

### 7.3 写影响

- INSERT：after 版本命中其他事务谓词；
- UPDATE：before 或 after 任一版本命中；
- DELETE：before 版本命中；
- 精确写入命中其他事务 record read。

### 7.4 危险结构

候选：

```text
T_in --rw--> T_pivot --rw--> T_out
```

新增边必须属于该结构，且相邻事务重叠。

危险条件：

```text
T_in == T_out
```

或：

```text
T_out 已提交
并且
T_in 未提交或 T_out.commit_ts < T_in.commit_ts
```

### 7.5 victim 与中止时机

检查接口显式携带 `current_statement_txn`。新增边闭合危险结构时，
固定返回 `ABORT_CURRENT_TRANSACTION`，不允许根据年龄、代价或 pivot 另选 victim。

写路径先计算 prospective before/after，先检查冲突和 SSI，再安装 pending version。

完整中止顺序：

```text
判定危险结构
-> 标记 ABORTING
-> 释放 MVCC 叶子锁
-> 统一 abort
-> 撤销表/索引/版本/写集/锁
-> 删除 predicates/record reads/rw edges
-> 会话活动事务置空
-> 返回 TRANSACTION_ABORT
```

COMMIT 不作为本类 SSI 冲突的延迟裁决点。

### 7.6 生命周期

committed SSI 元数据保留到其 `commit_ts` 不再可能与最老活动 SERIALIZABLE 快照重叠。
aborted 元数据在完整回滚和依赖双向清理后回收。

## 8. TPCC-Tester 交叉验证

最终 Tester 只保留 Wire v3。

目标模块：

```text
protocol/
sql/
workload/
txn/
engine/
check/
report/
diagnose/
```

迁移：

- `--diagnose`：Wire 测活、CRUD、事务、聚合、隔离、AUTO_ABORT；
- `--init`：Wire 创建 9 表、10 索引、9 次 LOAD；
- `--stats`：类型化 COUNT；
- `--check`：精确生成账本和事务账本；
- `--benchmark`：SI + PREPARE_SET + EXEC_BATCH。

配置：

- quick：缩小规模和时间，但协议、类型和事务语义不变；
- finals：50 仓、32 客户端、30 秒预热、连续 3×150 秒、
  45/43/4/4/4、160 槽轮盘和仓库覆盖门禁。

正式 27 个 SER 隐藏历史的完整 SQL 和调度不公开，本地测试不得宣称与隐藏实现等价。
本地按公开场景族构造确定性覆盖矩阵，并验证固定 victim 和立即中止。

## 9. 测试设计

### 9.1 协议单元测试

- 握手和版本拒绝；
- 大端 header；
- fragmented read/write；
- payload 1 MiB 边界；
- 非零 reserved、未知 tag/type/flag；
- 截断和尾随字节；
- META/ROW/RESULT_END 顺序；
- row_count；
- PREPARE 原子替换；
- typed INT32/FLOAT32/CHAR；
- AUTO_ABORT 无部分结果。

### 9.2 SQL 与 prepared

- `$n` 稠密性；
- 重复参数；
- 字符串内 `$n` 不是 marker；
- schema 类型；
- schema 失效；
- batch 不重复 Parser；
- query/command 响应类型不可互换。

### 9.3 SI/SER

- 3 个配置类；
- SI 快照、自写可见、提交/回滚、活动和陈旧写冲突；
- record/predicate/empty-range；
- INSERT/UPDATE/DELETE 进入或离开谓词；
- 两事务写偏序；
- 三事务提交顺序；
- 当前 SELECT/INSERT/UPDATE/DELETE 分别闭合危险结构；
- abort 清理；
- watermark 生命周期；
- 顺序扫描和索引扫描等价。

### 9.4 事务与恢复

- NewOrder 无效商品整笔回滚；
- AUTO_ABORT 回滚记录和索引；
- FLOAT32 位级更新和 SUM；
- COUNT(DISTINCT)；
- COMMIT ACK 前稳定化；
- SIGKILL 后成功提交仍可见；
- 未成功提交不残留；
- 恢复幂等本地回归。

### 9.5 性能

- 1/2/4/8/16/32 连接阶梯；
- quick profile；
- finals profile；
- 三个窗口分别统计；
- NewOrder/min 中位数；
- commit/abort/expected rollback/abandoned 分类；
- p50/p99；
- 在线和恢复后检查。

## 10. 文档交付

在 `docs/` 新增或重写：

1. `2026决赛性能测试要求完整解析.md`
   - 逐条赛题要求；
   - 初赛与决赛差异；
   - 协议、隔离、FLOAT、DISTINCT、WAL、恢复、评分。
2. `2026决赛本地测试与配置方法.md`
   - 构建、启动、diagnose、装载、quick、finals、SSI、崩溃测试；
   - 参数含义、预期输出和故障定位。
3. `2026决赛要求实现追踪矩阵.md`
   - 要求；
   - 代码位置；
   - 测试位置；
   - 当前状态；
   - 验证证据和剩余风险。

原有 `docs/性能题目与要求.md` 增加醒目标记：其前半部分属于初赛旧文本路径，
决赛实施以新文档和正式赛题为准，不能继续围绕 `output.txt` 优化。

## 11. 非目标

- 不实现同连接 pipeline、乱序响应或多路复用；
- 不加入 TPC-C 表名、SQL 文本、statement id 专用服务端分支；
- 不以单线程、固定客户端映射或无交错作为正确性前提；
- 不以关闭 WAL、冲突检测、索引维护或延迟回滚换吞吐；
- 不宣称本地 Tester 等同于官方密封评测；
- 不在未执行 Linux/SIGKILL/strace/全规模测试时声称通过。

## 12. 验收标准

完成必须同时满足：

- 旧文本网络入口和 `output.txt` 排名路径已删除；
- Wire v3 字节契约和错误状态通过测试；
- C++、Python/Rust 客户端均能执行 Wire 测活；
- PREPARE/BATCH 排名路径不重复解析完整 SQL；
- AUTO_ABORT 在响应前完成回滚；
- SSI 当前语句固定 victim 且立即中止；
- COUNT(DISTINCT) 和 FLOAT32 类型化路径满足公开规则；
- COMMIT 成功响应满足可审计稳定化；
- quick 交叉测试通过；
- 能运行 finals 配置并保存三个窗口结果；
- 文档追踪矩阵对所有决赛公开要求标注验证状态；
- 最终复核明确列出未通过、未实现或未在当前环境验证的项目。
