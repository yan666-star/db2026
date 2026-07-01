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
