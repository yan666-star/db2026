# 资格赛模拟题：yan2 通用编译、测试与 Bash

> 适用基线：`yan2@d42abe6af3eaca869d11b52270ace4487fbcca77`。本文中的目录、端口、构建方式和测试入口均按该版本核对。

## 1. 先确认工作区

```bash
git branch --show-current
git rev-parse HEAD
git status --short
```

预期分支为 `yan2`。线下赛不要执行 `git pull`、安装 AI 扩展或依赖网络服务；赛前应提前准备好编译依赖。

## 2. yan2 的构建事实

- 根目录使用 CMake。
- 服务端目标为 `build/bin/rmdb`，端口在 `src/rmdb.cpp` 中固定为 `8765`。
- `src/parser/CMakeLists.txt` 使用 Flex/Bison，并在配置阶段删除旧的 `lex.yy.cpp`、`yacc.tab.cpp`、`yacc.tab.h`，再从 `lex.l`、`yacc.y` 生成。
- 因此修改词法或语法后不能只编译旧目标；至少要重新运行 CMake 配置。

首次构建或修改 `lex.l`/`yacc.y` 后：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)" --target rmdb test_parser
```

只修改普通 `.h/.cpp`，且构建目录没有失效时：

```bash
cmake --build build -j"$(nproc)" --target rmdb test_parser
```

若生成文件或依赖关系异常，删除 `build` 后重新配置；不要手工同时维护生成的三个 parser 文件。

## 3. 先跑解析器测试

```bash
ctest --test-dir build --output-on-failure -R test_parser
```

新关键字至少覆盖：合法语句、缺少必要关键字、非法组合和大小写行为。`lex.l` 当前显式列出大写关键字，不能凭感觉假定大小写自动兼容。

## 4. 复用 yan2 现有 SQL 客户端

仓库已有 `SQL测试/10_sql_test/run_sql.py`，它按服务端协议连接 `127.0.0.1:8765`，无需再复制一份易漂移的 `send_sql.py`：

```bash
python3 SQL测试/10_sql_test/run_sql.py path/to/test.sql \
  --host 127.0.0.1 --port 8765 --timeout 10
```

也可以编译 `rmdb_client` 做交互测试，但批量回归优先复用现有 Python 驱动。

## 5. 通用 Bash 驱动

保存为 `work/feature_tests/run_feature.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <db-dir> <sql-file>" >&2
  exit 2
fi

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
db_dir="$1"
sql_file="$2"
log_file="$repo_dir/work/feature_tests/server.log"

cd "$repo_dir"
mkdir -p work/feature_tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)" --target rmdb test_parser
ctest --test-dir build --output-on-failure -R test_parser

rm -rf -- "$db_dir"
./build/bin/rmdb "$db_dir" >"$log_file" 2>&1 &
server_pid=$!
cleanup() {
  kill "$server_pid" 2>/dev/null || true
  wait "$server_pid" 2>/dev/null || true
}
trap cleanup EXIT

python3 - <<'PY'
import socket
import time

for _ in range(50):
    try:
        socket.create_connection(("127.0.0.1", 8765), 0.2).close()
        break
    except OSError:
        time.sleep(0.1)
else:
    raise SystemExit("rmdb did not listen on port 8765")
PY

python3 SQL测试/10_sql_test/run_sql.py "$sql_file" \
  --host 127.0.0.1 --port 8765 --timeout 10
```

这里的 `rm -rf` 只允许用于明确传入的测试数据库目录；不要传仓库根目录、`$HOME` 或空字符串。

## 6. 每道题的验证顺序

```bash
git diff --check
git diff -- src/parser src/analyze src/optimizer src/execution
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)" --target rmdb test_parser
ctest --test-dir build --output-on-failure -R test_parser
bash work/feature_tests/run_feature.sh /tmp/rmdb_feature_db work/feature_tests/test.sql
```

检查点：

- 修改 `lex.l` 时，`yacc.y` 必须声明同名 token。
- 修改 AST 字段后，沿 `SelectStmt → Query → Plan → Executor` 检查传递。
- JOIN 条件在 yan2 中先合并到 `FromClause::conds`，Planner 再按表集合拆分本层条件。
- `ProjectionExecutor` 自己维护 LIMIT 和 `plan_->rows_`；DISTINCT 必须保证“先去重，再计数，再 LIMIT”。
- 编译通过不等于语义正确；空表、多匹配、重复值和非法语法都要单测。
