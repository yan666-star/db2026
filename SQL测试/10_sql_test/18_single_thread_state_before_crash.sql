CREATE TABLE single_state (
    id INT,
    value FLOAT,
    note CHAR(12)
);

INSERT INTO single_state VALUES (1, 1.0, 'one');
INSERT INTO single_state VALUES (2, 2.0, 'two');
INSERT INTO single_state VALUES (3, 3.0, 'three');
INSERT INTO single_state VALUES (4, 4.0, 'four');

BEGIN;
UPDATE single_state SET value = 11.0 WHERE id = 1;
UPDATE single_state SET value = 12.5, note = 'one-ok' WHERE id = 1;
DELETE FROM single_state WHERE id = 2;
INSERT INTO single_state VALUES (5, 5.0, 'temp');
DELETE FROM single_state WHERE id = 5;
COMMIT;

BEGIN;
UPDATE single_state SET value = 99.0 WHERE id = 1;
UPDATE single_state SET value = 98.0 WHERE id = 1;
DELETE FROM single_state WHERE id = 3;
INSERT INTO single_state VALUES (6, 6.0, 'rollback');
ROLLBACK;

BEGIN;
DELETE FROM single_state WHERE id = 4;
INSERT INTO single_state VALUES (7, 7.25, 'seven');
UPDATE single_state SET value = 33.5 WHERE id = 3;
COMMIT;

BEGIN;
UPDATE single_state SET value = 100.0 WHERE id = 1;
DELETE FROM single_state WHERE id = 3;
UPDATE single_state SET value = 70.0 WHERE id = 7;
INSERT INTO single_state VALUES (8, 8.0, 'loser');

CRASH;
