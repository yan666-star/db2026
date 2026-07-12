-- Covers committed REDO and loser UNDO after a static checkpoint.
-- The unique index also checks index recovery when an indexed key changes.

CREATE TABLE recovery_full (
    id INT,
    amount FLOAT,
    tag CHAR(16)
);

CREATE INDEX recovery_full (id);

INSERT INTO recovery_full VALUES (1, 10.0, 'one');
INSERT INTO recovery_full VALUES (2, 20.0, 'two');
INSERT INTO recovery_full VALUES (3, 30.0, 'three');

CREATE STATIC_CHECKPOINT;

BEGIN;
UPDATE recovery_full SET amount = 66.0 WHERE id = 3;
INSERT INTO recovery_full VALUES (6, 60.0, 'six');
ROLLBACK;

BEGIN;
UPDATE recovery_full SET id = 10, amount = 15.5 WHERE id = 1;
DELETE FROM recovery_full WHERE id = 2;
UPDATE recovery_full SET amount = 35.0 WHERE id = 3;
INSERT INTO recovery_full VALUES (4, 40.0, 'four');
COMMIT;

BEGIN;
UPDATE recovery_full SET id = 11, amount = 99.0 WHERE id = 10;
DELETE FROM recovery_full WHERE id = 3;
INSERT INTO recovery_full VALUES (5, 50.0, 'five');

-- SECOND_CLIENT: INSERT INTO recovery_full VALUES (7, 70.0, 'seven');
CRASH;
