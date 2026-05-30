-- int/float dedup after type promotion
CREATE TABLE u1 (order_id INT, amount INT, region CHAR(10));
CREATE TABLE u2 (order_id INT, amount FLOAT, region CHAR(20));
INSERT INTO u1 VALUES (1, 150, 'Beijing');
INSERT INTO u1 VALUES (2, 89, 'Shanghai');
INSERT INTO u2 VALUES (1, 150.0, 'Beijing');
INSERT INTO u2 VALUES (3, 230.5, 'Shenzhen');

SELECT * FROM
    (SELECT * FROM u1 UNION SELECT * FROM u2) AS t
ORDER BY amount DESC, order_id ASC;

-- top-level UNION (no derived-table wrapper)
SELECT * FROM u1 UNION SELECT * FROM u2 ORDER BY amount DESC;
