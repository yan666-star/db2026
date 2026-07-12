-- Mirrors crash_recovery_single_thread_test with all three write types.
CREATE TABLE oj_single (
    id INT,
    value INT,
    note CHAR(16)
);

BEGIN;
INSERT INTO oj_single VALUES (1, 10, 'keep_insert');
INSERT INTO oj_single VALUES (2, 20, 'keep_update');
INSERT INTO oj_single VALUES (3, 30, 'keep_delete');
COMMIT;

BEGIN;
UPDATE oj_single SET value = 21 WHERE id = 2;
DELETE FROM oj_single WHERE id = 3;
INSERT INTO oj_single VALUES (4, 40, 'keep_insert2');
COMMIT;

BEGIN;
UPDATE oj_single SET value = 999 WHERE id = 1;
DELETE FROM oj_single WHERE id = 2;
INSERT INTO oj_single VALUES (5, 50, 'undo_insert');

CRASH;
