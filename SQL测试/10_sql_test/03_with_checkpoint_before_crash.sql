-- Expected recovery result:
--   The committed row before the checkpoint remains.
--   The uncommitted row after the checkpoint is absent.

CREATE TABLE t1 (
    id INT,
    num INT
);

BEGIN;
INSERT INTO t1 VALUES (1, 1);
COMMIT;

CREATE STATIC_CHECKPOINT;

BEGIN;
INSERT INTO t1 VALUES (2, 2);

CRASH;
