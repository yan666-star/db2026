# Repository Guidelines

## Project Structure & Module Organization

This repository contains the RMDB C++17 database system. Core source code lives in `src/`, split by subsystem: `parser/`, `analyze/`, `optimizer/`, `execution/`, `record/`, `index/`, `storage/`, `transaction/`, `recovery/`, `replacer/`, `system/`, and `common/`. The main server entry point is `src/rmdb.cpp`; unit-test entry points include `src/unit_test.cpp` and parser tests under `src/parser/`.

Third-party dependencies are under `deps/`. Build outputs go in `build/` and should not be treated as source. SQL regression and performance assets are in the SQL test directory, while problem statements and solution notes are in the Chinese-named topic and solution directories. Root PDFs document environment setup, usage, structure, and contest requirements.

## Build, Test, and Development Commands

Configure and build from the repository root:

```bash
cmake -S . -B build
cmake --build build -j
```

Run the main database binary:

```bash
./build/bin/rmdb <database_name>
```

Run compiled tests:

```bash
./build/bin/unit_test
./build/bin/test_parser
ctest --test-dir build
```

Run SQL scenario tests from their folders, for example:

```bash
cd "<SQL test directory>/10_sql_test" && ./run_public_single.sh
```

## Coding Style & Naming Conventions

Use C++17 and keep code compatible with GCC on Linux. The project builds with `-Wall -O2`; fix warnings introduced by your changes. Follow the existing style: 4-space indentation, braces on control blocks, `snake_case` for files and many functions, and subsystem prefixes such as `Rm`, `Ix`, `Log`, and `Transaction` where already established. Keep generated parser artifacts in sync with `lex.l` and `yacc.y` when grammar changes are made.

## Testing Guidelines

Prefer focused tests close to the changed subsystem. Use GoogleTest-backed binaries for unit behavior and the SQL scripts for end-to-end recovery, transaction, parser, and performance checks. For recovery or MVCC changes, run the relevant `10_sql_test` scripts plus `unit_test` before submitting.

## Commit & Pull Request Guidelines

Recent history uses short, imperative messages such as `Fix deleted-row resurrection after checkpoint crash recovery.` and brief task summaries. Keep commits specific to one change and mention the affected subsystem or task. Pull requests should include a brief problem statement, implementation summary, commands/tests run, and any known performance or recovery risks.

## Agent-Specific Instructions

Do not overwrite unrelated local changes. Avoid committing build outputs, temporary database directories, or generated logs. When changing behavior, update or add the smallest relevant test script or unit test.
