#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

mode="${1:-quick}"
clients="${CLIENTS:-16}"
build_dir="${BUILD_DIR:-$repo_root/build}"

if [[ "$mode" != "quick" && "$mode" != "full" ]]; then
    echo "usage: $0 [quick|full]" >&2
    exit 2
fi

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    cmake -S . -B "$build_dir"
    cmake --build "$build_dir" -j "${BUILD_JOBS:-$(nproc)}"
fi

"$build_dir/bin/unit_test"

python3 'SQL测试/performance_test/probe_output_file_isolation_independence.py' \
    --start-server --build-dir "$build_dir" \
    --db-name output_file_isolation_test_db

python3 'SQL测试/performance_test/run_generic_acid_suite.py' \
    --start-server --isolation default --build-dir "$build_dir" \
    --db-name generic_rc_test_db

python3 'SQL测试/performance_test/run_generic_acid_suite.py' \
    --start-server --crash-check --isolation snapshot --build-dir "$build_dir" \
    --db-name generic_snapshot_test_db

python3 'SQL测试/performance_test/run_generic_acid_suite.py' \
    --start-server --isolation serializable --build-dir "$build_dir" \
    --db-name generic_serializable_test_db

python3 'SQL测试/performance_test/run_performance_smoke.py' \
    --start-server --build-dir "$build_dir" --db-name performance_smoke_test_db

benchmark_args=(
    --start-server
    --crash-check
    --clients "$clients"
    --isolation snapshot
    --build-dir "$build_dir"
    --db-name official_like_test_db
)

if [[ "$mode" == "quick" ]]; then
    benchmark_args+=(--quick)
fi

python3 'SQL测试/performance_test/run_official_like_benchmark.py' "${benchmark_args[@]}"

echo "Local evaluation gates passed ($mode mode, $clients clients)."
