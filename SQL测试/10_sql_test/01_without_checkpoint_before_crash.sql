-- Expected recovery result:
--   The committed row (1, 1) remains.
--   The uncommitted row (2, 2) is absent.

CREATE TABLE t1 (
    id INT,
    num INT
);

BEGIN;
INSERT INTO t1 VALUES (1, 1);
COMMIT;

BEGIN;
INSERT INTO t1 VALUES (2, 2);

CRASH;
