# Static Checkpoint SQL Tests

`run_sql.py` keeps one TCP connection open for the entire SQL file. This is
required because RMDB associates transaction state with the client connection.

## Checkpoint Smoke Test

```bash
cd ~/Desktop/VMware/db2026
rm -rf checkpoint_smoke_db
./build/bin/rmdb checkpoint_smoke_db > /tmp/rmdb_checkpoint_smoke.log 2>&1 &
sleep 2
python3 sql_test/run_sql.py sql_test/00_checkpoint_smoke.sql
python3 sql_test/verify_checkpoint.py checkpoint_smoke_db
```

The verifier should print `checkpoint valid` and show the byte offset and LSN
of the checkpoint record.

## Test Without a Checkpoint

Build RMDB, then run:

```bash
cd ~/Desktop/VMware/db2026
rm -rf recovery_no_checkpoint_db
./build/bin/rmdb recovery_no_checkpoint_db > /tmp/rmdb_no_checkpoint.log 2>&1 &
sleep 2
python3 sql_test/run_sql.py sql_test/01_without_checkpoint_before_crash.sql
```

The final `CRASH` command terminates the server. Restart it with the same
database directory and verify the recovered data:

```bash
./build/bin/rmdb recovery_no_checkpoint_db > /tmp/rmdb_no_checkpoint.log 2>&1 &
sleep 2
python3 sql_test/run_sql.py sql_test/02_without_checkpoint_verify.sql
```

The result must contain only `(1, 1)`.

## Test With a Static Checkpoint

```bash
cd ~/Desktop/VMware/db2026
rm -rf recovery_checkpoint_db
./build/bin/rmdb recovery_checkpoint_db > /tmp/rmdb_checkpoint.log 2>&1 &
sleep 2
python3 sql_test/run_sql.py sql_test/03_with_checkpoint_before_crash.sql
```

Restart and verify:

```bash
./build/bin/rmdb recovery_checkpoint_db > /tmp/rmdb_checkpoint.log 2>&1 &
sleep 2
python3 sql_test/run_sql.py sql_test/04_with_checkpoint_verify.sql
```

The result must contain only `(1, 1)`.

## Broader Transaction Probe

```bash
cd ~/Desktop/VMware/db2026
rm -rf recovery_probe_db
./build/bin/rmdb recovery_probe_db > /tmp/rmdb_recovery_probe.log 2>&1 &
sleep 2
python3 sql_test/run_sql.py sql_test/05_checkpoint_transaction_probe.sql
```

Restart and verify:

```bash
./build/bin/rmdb recovery_probe_db > /tmp/rmdb_recovery_probe.log 2>&1 &
sleep 2
python3 sql_test/run_sql.py sql_test/06_checkpoint_transaction_verify.sql
```

Only rows `1` and `2` should remain. Row `1` must retain the value
`10.5`, proving that the uncommitted update was undone.

## Full REDO/UNDO And Index Probe

```bash
cd ~/Desktop/VMware/db2026
rm -rf recovery_full_db
./build/bin/rmdb recovery_full_db > /tmp/rmdb_recovery_full.log 2>&1 &
sleep 2
python3 sql_test/run_full_recovery_crash.py \
    sql_test/07_full_recovery_before_crash.sql
./build/bin/rmdb recovery_full_db > /tmp/rmdb_recovery_full.log 2>&1 &
sleep 2
python3 sql_test/run_sql.py sql_test/08_full_recovery_verify.sql
```

It keeps an uncommitted transaction open on one connection and commits an
independent insert on a second connection. The second commit forces the shared
WAL buffer to disk before the crash, so startup must genuinely REDO and then
UNDO the loser transaction. The final table must contain IDs `3`, `4`, `7`,
and `10`. Point lookups verify that the unique index matches the recovered
table records. The one-command probe also restarts recovery twice and appends
an incomplete WAL tail, checking recovery idempotence and torn-log truncation.
After the first recovery it commits another update to the same row previously
touched by the loser transaction, proving that later restarts do not repeat an
old UNDO over newly committed data. The final restart removes `db.restart`,
forcing a full WAL scan and index rebuild fallback.

The same probe can be run with:

```bash
bash sql_test/run_full_recovery.sh
```
