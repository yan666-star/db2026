#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

mode="${1:-quick}"
clients="${CLIENTS:-16}"
build_dir="${BUILD_DIR:-$repo_root/build}"
data_scale="${DATA_SCALE:-mini}"  # mini|small|medium|full

if [[ "$mode" != "quick" && "$mode" != "full" ]]; then
    echo "usage: $0 [quick|full]" >&2
    exit 2
fi

# ── Generate / verify test data ──────────────────────────────────────────
data_dir="$repo_root/src/test/performance_test/table_data"
if [[ "$data_scale" != "mini" ]]; then
    generated_dir="$data_dir/tpcc_$data_scale"
    if [[ ! -d "$generated_dir" ]]; then
        echo "=== Generating $data_scale-scale TPC-C data ==="
        python3 SQL测试/performance_test/generate_tpcc_data.py \
            --scale "$data_scale"
    fi
fi

# ── Build ────────────────────────────────────────────────────────────────
if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    echo "=== Configuring CMake ==="
    cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
    echo "=== Building ==="
    cmake --build "$build_dir" -j "${BUILD_JOBS:-$(nproc)}"
fi

# ── Unit tests ───────────────────────────────────────────────────────────
echo "=== Running C++ unit tests ==="
cd "$build_dir"
ctest --output-on-failure
cd "$repo_root"

# ── Smoke tests (SQL files) ──────────────────────────────────────────────
echo "=== Running SQL smoke tests ==="
python3 SQL测试/performance_test/run_performance_smoke.py \
    --start-server --build-dir "$build_dir" \
    --db-dir "$build_dir/smoke_test_db" \
    --server-log "$build_dir/performance_smoke_server.log"

# ── ACID / isolation tests ───────────────────────────────────────────────
echo "=== ACID tests (SNAPSHOT ISOLATION) ==="
python3 SQL测试/performance_test/run_acid_tests.py \
    --start-server --isolation snapshot --build-dir "$build_dir" \
    --db-dir "$build_dir/acid_si_test_db" \
    --server-log "$build_dir/acid_si_server.log"

echo "=== ACID tests (SERIALIZABLE) ==="
python3 SQL测试/performance_test/run_acid_tests.py \
    --start-server --isolation serializable --build-dir "$build_dir" \
    --db-dir "$build_dir/acid_ser_test_db" \
    --server-log "$build_dir/acid_ser_server.log"

# ── Benchmark ────────────────────────────────────────────────────────────
benchmark_args=(
    --start-server
    --setup
    --isolation snapshot
    --clients "$clients"
    --build-dir "$build_dir"
    --db-dir "$build_dir/benchmark_test_db"
    --server-log "$build_dir/benchmark_server.log"
)

if [[ "$mode" == "quick" ]]; then
    benchmark_args+=(--quick)
else
    benchmark_args+=(--crash-check)
fi

echo "=== TPC-C benchmark ($mode mode, $clients clients) ==="
python3 SQL测试/performance_test/run_benchmark.py "${benchmark_args[@]}"

# ── Perf diag check (if JSON output exists) ──────────────────────────────
json_path="$build_dir/benchmark_report.json"
if [[ -f "$json_path" ]]; then
    echo "=== Performance diagnostic check ==="
    python3 SQL测试/performance_test/check_perf_diag.py "$json_path" || \
        echo "WARN: perf diag check failed (non-fatal)"
fi

echo ""
echo "Local evaluation gates passed ($mode mode, $clients clients)."
