# Performance-Test Smoke Suite

This directory contains a small local smoke suite for the 2026 preliminary
performance-test statement. It is not the official 30s warmup plus 360s timed
TPC-C benchmark. It is meant to quickly check the new required entrances:

- `load file_name into table_name;`
- `set output_file off` without a semicolon
- string `MIN` and `MAX`
- primary-key index creation on loaded TPC-C-shaped tables
- one compact `NewOrder`-shaped transaction under snapshot isolation

Run it after building RMDB:

```bash
python3 SQL测试/performance_test/run_performance_smoke.py --start-server
```

The runner starts the server from `build` so the official relative load paths
work from the database directory:

```text
../../src/test/performance_test/table_data/*.csv
```

To run against an already-started server:

```bash
python3 SQL测试/performance_test/run_performance_smoke.py \
  --db-dir build/performance_smoke_db
```

The runner deliberately preserves the exact no-semicolon form of
`set output_file off` and checks that `output.txt` does not grow after that
command.

## Generic correctness and official-shaped local load

Run table-name-independent ACID, concurrency, phantom and crash-recovery checks:

```bash
python3 'SQL测试/performance_test/run_generic_acid_suite.py' \
  --start-server --crash-check --isolation snapshot
```

Run a short 16-client mixed-load regression:

```bash
python3 'SQL测试/performance_test/run_official_like_benchmark.py' \
  --start-server --quick --clients 16 --crash-check
```

Without `--quick`, the mixed-load runner defaults to three rounds of 30 seconds
warmup plus 360 seconds measurement and reports the median committed NewOrder
tpmC. Its transaction selection uses the published 10/23 NewOrder, 10/23
Payment and 1/23 each remaining transaction mix. This is a local engineering
benchmark, not the private official evaluator.

See `docs/official-performance-test-notes.md` for the requirement summary,
external-tool analysis, limitations and recommended Ubuntu test matrix.

On Ubuntu, the combined build/correctness/recovery/performance entry point is:

```bash
bash 'SQL测试/performance_test/run_local_evaluation.sh' quick
bash 'SQL测试/performance_test/run_local_evaluation.sh' full
```

`full` uses the published 30s/360s/three-round windows and normally takes more
than 19 minutes just for the mixed-load phases. Set `CLIENTS=32` to exercise the
higher local concurrency case, or `SKIP_BUILD=1` to reuse an existing build.
