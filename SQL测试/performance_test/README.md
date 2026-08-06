# RMDB 本地测试方法

本文档适用于当前 worktree：

```text
D:\DMS-DESIGN\db2026\.worktrees\finals-wire-ssi
```



## 一、统一的服务生命周期

以下测试入口共用 `server_manager.py` 管理 RMDB：

- `run_performance_smoke.py`
- `run_acid_tests.py`
- `run_benchmark.py`
- `check_consistency.py`

指定 `--start-server` 后，测试脚本会依次完成：

1. 在 `--build-dir/bin/` 中定位 `rmdb`；
2. 将完整的 `--db-dir` 作为数据库启动参数；
3. 启动 `rmdb <database-directory>`；
4. 轮询端口并完成 Wire v3 握手，不使用固定 `sleep`；
5. 执行原有测试；
6. 无论测试成功还是抛出异常，都在 `finally` 中关闭 RMDB。

启动失败时会显示：

```text
RMDB server startup failed
```

随后输出 `--server-log` 对应的服务日志。

所有入口统一支持：

```text
--start-server
--build-dir <build-directory>
--db-dir <database-directory>
--host <host>                       # 默认 127.0.0.1
--port <port>                       # 默认 8765
--server-log <log-file>
--startup-timeout <seconds>         # 默认 10 秒
--reset-db                          # 启动前清理 build 内测试库
--keep-db                           # 复用已有数据库
```

兼容参数 `--db-name` 仍然保留，但新命令建议明确使用 `--db-dir`。

如果不指定 `--start-server`，脚本保持原行为：只连接已经运行的 RMDB，不启动或关闭外部服务。

## 二、编译

必须在包含 CMake、C++ 编译器及项目依赖的 Linux 环境中执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

确认以下程序存在：

```bash
ls -l build/bin/rmdb build/bin/unit_test
```

直接运行 `build/bin/rmdb` 会因为缺少数据库参数而显示 Usage。正确形式为：

```bash
./build/bin/rmdb build/example_db
```

通常不需要手工执行这一命令；测试脚本的 `--start-server` 会自动处理。

## 三、推荐测试顺序

### 1. C++ 单元测试

```bash
ctest --test-dir build --output-on-failure
```

底层单元测试失败时，应先处理失败，不继续进行并发和性能测试。

### 2. Smoke

```bash
python3 SQL测试/performance_test/run_performance_smoke.py \
  --start-server \
  --build-dir build \
  --db-dir build/performance_smoke_db \
  --server-log build/performance_smoke_server.log
```

该入口自动创建或重建测试库、启动 Wire v3服务、执行SQL和并发探针，最后关闭服务。

在进入并发探针前，Smoke 会先执行决赛功能契约预检：

- `UPDATE ... SET col = col WHERE ...` 必须返回 `COMMAND_OK`；
- `SELECT col AS alias ...` 返回的 Wire META 必须保留精确 alias 和类型。

这两项失败时会立即停止，不再继续运行耗时的并发探针。

### 3. Snapshot Isolation ACID

```bash
python3 SQL测试/performance_test/run_acid_tests.py \
  --start-server \
  --build-dir build \
  --db-dir build/acid_snapshot_db \
  --server-log build/acid_snapshot_server.log \
  --isolation snapshot
```

SI 测试同时覆盖：UPDATE 自赋值事务语义、SELECT AS 的精确 Wire META、
stale-snapshot DELETE 的语句级 `TRANSACTION_ABORT`，以及真正无匹配
DELETE 仍返回成功，避免把所有 0 行写操作错误地判成冲突。

### 4. Serializable ACID

```bash
python3 SQL测试/performance_test/run_acid_tests.py \
  --start-server \
  --build-dir build \
  --db-dir build/acid_serializable_db \
  --server-log build/acid_serializable_server.log \
  --isolation serializable
```

需要验证 SIGKILL 恢复时增加：

```bash
--crash-check
```

ACID 脚本会通过同一个 `RMDBServerManager` 执行 `kill()` 和 `restart(reset_db=False)`，恢复阶段不会删除原数据库。

### 5. Quick Benchmark

```bash
python3 SQL测试/performance_test/run_benchmark.py \
  --start-server \
  --build-dir build \
  --db-dir build/benchmark_db \
  --server-log build/benchmark_server.log \
  --setup \
  --quick \
  --clients 16
```

`--setup` 的数据流程保持不变：创建表、装载 TPC-C 数据并创建索引。全部索引创建成功后，
脚本会额外执行一次 `CREATE STATIC_CHECKPOINT;`，再运行 benchmark。该检查点只为大 WAL
提供恢复起点和索引基线，不能代替每个成功 COMMIT 在响应前完成 WAL 稳定化。
服务生命周期和检查点调整没有修改 TPC-C workload、事务混合比例、Wire 请求或装载 SQL。

正式排名连接首次发送 `PREPARE_SET` 时，服务端也会在不存在可用检查点的情况下建立同样的
通用静态检查点。检查点不识别表名、SQL 模板或预期结果，并且仍须等待活动事务全部结束，
按“写入并同步 checkpoint WAL → 刷新数据页和元数据 → 保存索引快照 → 持久化 restart offset”
的顺序完成。崩溃重启仍在监听端口之前同步执行 analyze、redo 和 loser undo；不会以提前监听
规避决赛要求的 `SHOW TABLES` 恢复就绪检查。

### 6. 一致性检查

一致性检查需要一个已经完成TPC-C装载的数据库。检查现有 benchmark 数据库时使用：

```bash
python3 SQL测试/performance_test/check_consistency.py \
  --start-server \
  --build-dir build \
  --db-dir build/benchmark_db \
  --server-log build/consistency_server.log \
  --keep-db
```

仅执行下面的命令也能验证自动启动和关闭逻辑：

```bash
python3 SQL测试/performance_test/check_consistency.py --start-server
```

但默认的新建 `build/consistency_db` 不含TPC-C表，因此一致性内容检查会报告缺表。`check_consistency.py` 不会隐式装载数据，以免改变原有检查逻辑。

### 7. 本地整套评测

Quick：

```bash
CLIENTS=16 bash SQL测试/performance_test/run_local_evaluation.sh quick
```

提高到32客户端：

```bash
CLIENTS=32 bash SQL测试/performance_test/run_local_evaluation.sh quick
```

完整长测：

```bash
CLIENTS=32 bash SQL测试/performance_test/run_local_evaluation.sh full
```

`run_local_evaluation.sh` 会为 Smoke、两种隔离级别的 ACID 和 Benchmark 分别传入独立的 `--db-dir` 与 `--server-log`，避免测试库互相覆盖。

## 四、测试结束后的进程检查

正常结束和Python异常都会触发 `RMDBServerManager.stop()`。如果测试进程本身被外部强制终止，可手工检查8765端口：

```bash
ss -ltnp | grep ':8765 '
```

再核对进程命令行及数据库路径：

```bash
ps -ef | grep '[b]uild/bin/rmdb'
```

只终止确认属于当前 worktree 和当前测试数据库的进程，不要使用无范围的批量结束命令。

## 五、结果判断

不要只看脚本最后一行。至少检查：

- 进程退出码是否为0；
- 输出中是否存在 `FAIL`、`ERROR`、`AssertionError`；
- server log是否包含崩溃或协议错误；
- ACID report是否生成；
- Benchmark JSON是否生成；
- 测试结束后8765端口是否仍由本轮RMDB占用。
