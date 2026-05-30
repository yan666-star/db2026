-- single subquery derived table (non-union)
CREATE TABLE dt1 (a INT, b INT);
INSERT INTO dt1 VALUES (2, 20);
INSERT INTO dt1 VALUES (1, 10);
SELECT * FROM (SELECT * FROM dt1) AS t ORDER BY b DESC;
