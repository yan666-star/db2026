-- Variant tests for platform scenarios

-- V1: 6.1 without AS keyword
CREATE TABLE v1a (order_id INT, amount FLOAT, region CHAR(10));
CREATE TABLE v1b (order_id INT, amount FLOAT, region CHAR(10));
INSERT INTO v1a VALUES (1, 150.0, 'Beijing');
INSERT INTO v1a VALUES (2, 230.5, 'Shanghai');
INSERT INTO v1b VALUES (1, 150.0, 'Beijing');
INSERT INTO v1b VALUES (4, 120.0, 'Shenzhen');
SELECT * FROM (SELECT * FROM v1a UNION SELECT * FROM v1b) t ORDER BY amount DESC;

-- V2: multi-column ORDER BY on 6.1-like data
CREATE TABLE v2a (order_id INT, amount FLOAT, region CHAR(10));
CREATE TABLE v2b (order_id INT, amount FLOAT, region CHAR(10));
CREATE TABLE v2c (order_id INT, amount FLOAT, region CHAR(10));
INSERT INTO v2a VALUES (1, 150.0, 'Beijing');
INSERT INTO v2a VALUES (2, 230.5, 'Shanghai');
INSERT INTO v2a VALUES (3, 89.99, 'Guangzhou');
INSERT INTO v2b VALUES (1, 150.0, 'Beijing');
INSERT INTO v2b VALUES (4, 120.0, 'Shenzhen');
INSERT INTO v2b VALUES (5, 560.0, 'Chengdu');
INSERT INTO v2c VALUES (1, 150.0, 'Beijing');
INSERT INTO v2c VALUES (6, 199.99, 'Wuhan');
SELECT * FROM (SELECT * FROM v2a UNION SELECT * FROM v2b UNION SELECT * FROM v2c) AS all_orders ORDER BY amount DESC, order_id ASC;

-- V3: qualified ORDER BY
SELECT * FROM (SELECT * FROM v2a UNION SELECT * FROM v2b UNION SELECT * FROM v2c) AS all_orders ORDER BY all_orders.amount DESC;

-- V4: top-level UNION only
CREATE TABLE v4a (order_id INT, amount INT, region CHAR(10));
CREATE TABLE v4b (order_id INT, amount FLOAT, region CHAR(20));
INSERT INTO v4a VALUES (1, 150, 'Beijing');
INSERT INTO v4a VALUES (2, 89, 'Shanghai');
INSERT INTO v4b VALUES (1, 150.0, 'Beijing');
INSERT INTO v4b VALUES (3, 230.5, 'Shenzhen');
SELECT * FROM v4a UNION SELECT * FROM v4b ORDER BY amount DESC, order_id ASC;

-- V6: nested derived table wrapping union
SELECT * FROM
    (SELECT * FROM
        (SELECT * FROM v2a UNION SELECT * FROM v2b) AS inner_u
    ) AS outer_u
ORDER BY amount DESC;
