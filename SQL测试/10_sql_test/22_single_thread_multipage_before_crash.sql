CREATE TABLE recovery_pages (
    id INT,
    value FLOAT,
    payload CHAR(480)
);

BEGIN;
INSERT INTO recovery_pages VALUES (1, 1.25, 'row-01');
INSERT INTO recovery_pages VALUES (2, 2.25, 'row-02');
INSERT INTO recovery_pages VALUES (3, 3.25, 'row-03');
INSERT INTO recovery_pages VALUES (4, 4.25, 'row-04');
INSERT INTO recovery_pages VALUES (5, 5.25, 'row-05');
INSERT INTO recovery_pages VALUES (6, 6.25, 'row-06');
INSERT INTO recovery_pages VALUES (7, 7.25, 'row-07');
INSERT INTO recovery_pages VALUES (8, 8.25, 'row-08');
INSERT INTO recovery_pages VALUES (9, 9.25, 'row-09');
INSERT INTO recovery_pages VALUES (10, 10.25, 'row-10');
INSERT INTO recovery_pages VALUES (11, 11.25, 'row-11');
INSERT INTO recovery_pages VALUES (12, 12.25, 'row-12');
INSERT INTO recovery_pages VALUES (13, 13.25, 'row-13');
INSERT INTO recovery_pages VALUES (14, 14.25, 'row-14');
INSERT INTO recovery_pages VALUES (15, 15.25, 'row-15');
INSERT INTO recovery_pages VALUES (16, 16.25, 'row-16');
INSERT INTO recovery_pages VALUES (17, 17.25, 'row-17');
INSERT INTO recovery_pages VALUES (18, 18.25, 'row-18');
INSERT INTO recovery_pages VALUES (19, 19.25, 'row-19');
INSERT INTO recovery_pages VALUES (20, 20.25, 'row-20');
INSERT INTO recovery_pages VALUES (21, 21.25, 'row-21');
INSERT INTO recovery_pages VALUES (22, 22.25, 'row-22');
INSERT INTO recovery_pages VALUES (23, 23.25, 'row-23');
INSERT INTO recovery_pages VALUES (24, 24.25, 'row-24');
INSERT INTO recovery_pages VALUES (25, 25.25, 'row-25');
INSERT INTO recovery_pages VALUES (26, 26.25, 'row-26');
INSERT INTO recovery_pages VALUES (27, 27.25, 'row-27');
INSERT INTO recovery_pages VALUES (28, 28.25, 'row-28');
INSERT INTO recovery_pages VALUES (29, 29.25, 'row-29');
INSERT INTO recovery_pages VALUES (30, 30.25, 'row-30');
COMMIT;

BEGIN;
UPDATE recovery_pages SET value = 55.5 WHERE id = 5;
DELETE FROM recovery_pages WHERE id = 7;
DELETE FROM recovery_pages WHERE id = 8;
INSERT INTO recovery_pages VALUES (31, 31.25, 'row-31');
INSERT INTO recovery_pages VALUES (32, 32.25, 'row-32');
INSERT INTO recovery_pages VALUES (33, 33.25, 'row-33');
INSERT INTO recovery_pages VALUES (34, 34.25, 'row-34');
INSERT INTO recovery_pages VALUES (35, 35.25, 'row-35');
COMMIT;

BEGIN;
UPDATE recovery_pages SET value = 999.0 WHERE id = 5;
DELETE FROM recovery_pages WHERE id = 1;
DELETE FROM recovery_pages WHERE id = 31;
INSERT INTO recovery_pages VALUES (36, 36.25, 'loser-36');
INSERT INTO recovery_pages VALUES (37, 37.25, 'loser-37');
INSERT INTO recovery_pages VALUES (38, 38.25, 'loser-38');
INSERT INTO recovery_pages VALUES (39, 39.25, 'loser-39');
INSERT INTO recovery_pages VALUES (40, 40.25, 'loser-40');

CRASH;
