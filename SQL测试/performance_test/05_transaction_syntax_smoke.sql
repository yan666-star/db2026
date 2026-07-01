-- Covers transaction command spellings used by some benchmark drivers.
START TRANSACTION;
INSERT INTO history
VALUES (2, 1, 1, 1, 1, '2026-07-01 10:00:01', 1.0, 'syntax-commit');
COMMIT TRANSACTION;

BEGIN WORK;
INSERT INTO history
VALUES (2, 1, 1, 1, 1, '2026-07-01 10:00:02', 2.0, 'syntax-rollback');
ROLLBACK WORK;

SELECT h_data
FROM history
WHERE h_date = '2026-07-01 10:00:01';

SELECT h_data
FROM history
WHERE h_date = '2026-07-01 10:00:02';

BEGIN WORK;
INSERT INTO history
VALUES (2, 1, 1, 1, 1, '2026-07-01 10:00:03', 3.0, 'failed-before-conflict');
INSERT INTO orders
VALUES (1, 1, 1, 2, '2026-07-01 10:00:03', 0, 1, 1);
INSERT INTO history
VALUES (2, 1, 1, 1, 1, '2026-07-01 10:00:04', 4.0, 'failed-after-conflict');
COMMIT TRANSACTION;

SELECT h_data
FROM history
WHERE h_date = '2026-07-01 10:00:03';

SELECT h_data
FROM history
WHERE h_date = '2026-07-01 10:00:04';
