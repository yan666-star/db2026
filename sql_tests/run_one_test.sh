#!/bin/bash
set -e
cd /root/db2026
pkill -9 -f bin/rmdb 2>/dev/null || true
sleep 1
DB=$1
SQL=$2
rm -rf "$DB"
./build/bin/rmdb "$DB" > /tmp/rmdb_${DB}.log 2>&1 &
sleep 2
if ! pgrep -f "bin/rmdb $DB" > /dev/null; then
  echo "Server failed to start for $DB"
  cat /tmp/rmdb_${DB}.log
  exit 1
fi
python3 sql_tests/run_sql.py "$SQL" > /tmp/run_${DB}.log 2>&1 || true
echo "===== $DB / $SQL ====="
cat "$DB/output.txt" 2>/dev/null || echo "(no output.txt)"
echo
