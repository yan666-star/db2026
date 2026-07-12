CREATE TABLE single_format (
    id INT,
    amount FLOAT,
    note CHAR(16)
);

INSERT INTO single_format VALUES (1, 1.25, 'auto');

BEGIN;
INSERT INTO single_format VALUES (2, 2.5, 'commit');
INSERT INTO single_format VALUES (3, 3.0, 'delete_me');
COMMIT;

BEGIN;
UPDATE single_format
SET amount = 22.125, note = 'tail  '
WHERE id = 2;
DELETE FROM single_format WHERE id = 3;
INSERT INTO single_format VALUES (4, 4.000001, 'keep');
COMMIT;

BEGIN;
UPDATE single_format SET amount = 77.0 WHERE id = 1;
INSERT INTO single_format VALUES (6, 6.0, 'rolled_back');
ROLLBACK;

BEGIN;
UPDATE single_format SET amount = 99.0 WHERE id = 1;
DELETE FROM single_format WHERE id = 2;
INSERT INTO single_format VALUES (5, 5.0, 'undo_insert');

CRASH;
