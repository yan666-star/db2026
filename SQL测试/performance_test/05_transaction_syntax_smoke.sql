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
