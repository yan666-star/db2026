-- A broader transaction probe.
-- Rows 1 and 2 are committed. Rows 3 and 4 must be rolled back after recovery.

CREATE TABLE recovery_probe (
    id INT,
    amount FLOAT,
    tag CHAR(16)
);

BEGIN;
INSERT INTO recovery_probe VALUES (1, 10.5, 'before_cp');
INSERT INTO recovery_probe VALUES (2, 20.25, 'before_cp');
COMMIT;

CREATE STATIC_CHECKPOINT;

BEGIN;
INSERT INTO recovery_probe VALUES (3, 30.75, 'after_cp');
UPDATE recovery_probe SET amount = 99.0 WHERE id = 1;
INSERT INTO recovery_probe VALUES (4, 40.5, 'after_cp');

CRASH;
