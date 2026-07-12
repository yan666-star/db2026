# TPCC 正确性优先吞吐优化计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `subagent-driven-development` or `executing-plans` task by task. Do not merge
> multiple phases into one performance submission.

**目标：** 在保留当前 ACID、SI、表/索引一致性和 kill-9 恢复行为的前提下，
降低写事务 abort 成本和事务重叠窗口，提高成功 NewOrder tpmC。

**基线：** 当前分支 `dc8c03c`，核心正确性来自 `0e0af15`，并包含
`34c45ec` 的通用索引点查与读并发优化。

**技术栈：** C++17、MVCC/SI、B+Tree、BufferPool、WAL、Python 本地评测器。

## 全局约束

- 不识别 TPC-C 表名、字段名、事务名或固定 SQL 文本。
- 不删除 `latest_commit > start_ts` 的 SI 写写冲突判断。
- load 初始行继续按 `ts=0` baseline 可见。
- MVCC UPDATE 找不到目标 RID 继续 abort。
- 物理数据和索引应用完成后才能写 COMMIT；客户端成功响应不能早于 COMMIT 日志写出。
- 每个阶段独立提交、独立跑正确性门槛；失败时只回退该阶段。

## 结果判断

`official-performance-test-notes.md` 中最重要的数据如下：

- 1 客户端：NewOrder tpmC 4496.66，全部事务 100% 提交。
- 16 客户端：tpmC 433.64，NewOrder 提交率 1.51%。
- 单条 order-line、16 客户端：tpmC 3581.17，提升 8.26 倍。
- 旧诊断日志中 42,202 次 abort 有 38,605 次来自
  `write_conflict_pending`，占 91% 以上。
- 大部分 abort 没有物理回滚，但每次仍执行 ABORT 日志强制刷盘。

因此当前不是单条 SQL 必然慢，而是事务越长，热点写重叠越多；失败事务又
占用日志和回滚资源，最终形成并发负扩展。

本地数字也不能直接与官方排行榜比较。当前脚本默认只有 3 个 district、
10 个 item；每个 NewOrder 从 10 个 item 中抽 5–10 次且允许重复，并把一次
stock 修改拆成 3 条 UPDATE。标准规模和标准 SQL 的冲突概率、网络往返和
事务长度都不同。

---

### Task 1：校准本地负载并输出可比较诊断

**文件：**

- 修改：`SQL测试/performance_test/run_official_like_benchmark.py`
- 修改：`docs/official-performance-test-notes.md`

**实施：**

- [x] 保留现有模式并命名为 `mini-contention`，用于放大并发问题。
- [x] 增加 `official-shape`：同一订单 item 不重复；一次 UPDATE 同时修改
  `s_quantity/s_ytd/s_order_cnt`，不再拆成 3 条 SQL。
- [x] JSON 记录数据规模、SQL 形状和实际隔离来源。当前 `default` 在
  `set output_file off` 后实际由新连接继承 SI，报告中不能再只写“默认隔离”。
- [x] 在服务端启用 `RMDB_PERF_DIAG=1`，保存每轮前后计数差值，而不是只看
  进程累计值。
- [ ] 分别记录 1/4/8/16/32 客户端结果；官方形状与冲突放大模式分开比较。

**验收：** 两种模式都通过压测后一致性和 kill-9 恢复。后续优化以
`official-shape` 为主，以 `mini-contention` 检查最坏冲突行为。

---

### Task 2：纯内存 MVCC abort 不再逐次强制刷盘

**文件：**

- 修改：`src/transaction/transaction_manager.cpp`
- 修改：`src/transaction/transaction_manager.h`
- 测试：`SQL测试/performance_test/run_generic_acid_suite.py`

**实施：**

- [x] 明确定义 `volatile_only_mvcc_abort`：事务使用 MVCC、没有物理 rollback、
  没有进入 `commit_mvcc` 物理应用阶段。
- [x] 这类 abort 仍追加 ABORT log，但不调用
  `flush_log_to_disk(true)`；日志由后续正常 flush 按顺序写出。
- [x] 非 MVCC abort、发生 INSERT 等物理回滚的 abort、进入过 MVCC commit
  的 abort 保持现有 checkpoint flush 和强制日志刷盘。
- [x] 增加 `abort_log_force_flush_skipped` 计数，确认跳过范围只覆盖
  volatile-only 分支。

**恢复依据：** 如果 ABORT 已写出，之前日志也按顺序写出，而物理表从未应用
pending update/delete；如果崩溃前 ABORT 未写出，恢复把事务当作 loser，
执行 redo+undo 后仍回到旧值。

**正确性用例：** pending UPDATE/DELETE 后 abort，在 ABORT 尚未单独 force、
被后续事务日志带出、以及立即 kill-9 三种时机下，重启后都只能看到旧值；
INSERT 后 abort 仍走原物理回滚路径。

**性能验收：** `abort_log_force_flush` 明显下降，16 客户端中位数 tpmC 不低于
基线；所有 ACID、一致性和恢复检查通过。

---

### Task 3：用数据调整 pending writer 等待，而不是固定 sleep

**文件：**

- 修改：`src/transaction/transaction_manager.cpp`
- 修改：`docs/official-performance-test-notes.md`

**实施：**

- [x] 把 `pending_wait_resolved` 拆为 writer committed、writer aborted、timeout。
- [x] 对 `RMDB_PENDING_WAIT_US=0/50/200/500/2000` 做同负载 A/B。
- [x] writer commit 后，等待者仍因 `latest_commit > start_ts` abort；这部分等待
  属于无效延迟，应缩短。writer abort 占多数时才保留短等待。
- [x] 不实现线程启动 sleep、全局事务串行或隐藏重试。

**验收：** 选择三轮中位数最高且尾延迟稳定的预算；SI 冲突规则不变。

---

### Task 4：缩短成功事务热路径

**候选文件：**

- `src/storage/buffer_pool_manager.cpp`
- `src/storage/buffer_pool_manager.h`
- `src/record/rm_file_handle.cpp`
- `src/transaction/transaction_manager.cpp`

**实施顺序：**

- [x] 先增加 BufferPool hit/miss、全局 latch 等待时间、commit-apply 等待时间、
  `mvcc_latch_` 等待时间计数，默认关闭。
- [x] 若 BufferPool latch 占主导，设计 page-table 共享锁和 per-frame 状态锁；
  pin 从 0→1 与 LRU 移除、1→0 与 LRU 加入必须在同一 frame 临界区完成。
- [ ] 若 commit-apply 读锁占主导，使用带 writer intent 的公平读写门控。
  物理 commit/GC/checkpoint 仍独占，避免读到部分 NewOrder。
- [ ] `record_versions_` 分片只在 `mvcc_latch_` 数据证明为热点后实施；多 RID
  commit 冲突检查仍需一致的锁顺序。

**排除：** 不直接重放“决战2.0”的整批 batch/共享锁改动。每个锁改动必须
单独通过并发 delete、旧快照可见、post-run consistency 和 kill-9 检查。

---

### Task 5：高风险项最后处理

以下项目不进入前两轮优化：

- MVCC INSERT 延迟到 commit：可能减少晚期 stock 冲突后的物理回滚，但需要
  新的 RID 预留、唯一键和索引发布协议。
- CLR 或完整 WAL group commit：可以消除物理 abort 的全库 checkpoint flush，
  但要重做恢复证明。
- SQL 计划缓存：只有 parser/analyze profiling 显示为主要热点时才实施；缓存
  必须按通用结构和 schema version 失效，不能匹配评测 SQL。
- 切换到 RC/2PL：题面写明基于 SI，且当前正确版本由 `set output_file off`
  传播 SI。没有新的官方书面确认前，不把隔离级别切换当作性能方案。

## 推荐执行顺序

1. 校准负载和诊断输出。
2. 实施 volatile-only MVCC abort 免强制刷盘。
3. 根据 writer 最终状态调整 pending wait。
4. 用等待时间数据选择 BufferPool、commit-apply 或 MVCC map 中的下一个热点。
5. 最后评估 deferred insert、CLR 和 WAL group commit。

这个顺序先处理已有数字证明的浪费，再处理结构锁。它不承诺凭单个改动达到
上万 tpmC，但能避免继续优化与官方规模不一致的微型冲突负载，也能保证每次
性能变化都可以单独归因和回退。
