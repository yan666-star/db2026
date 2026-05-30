-- Test 6.1: Union with ORDER BY
CREATE TABLE orders1 (
    order_id INT,
    amount FLOAT,
    region CHAR(10)
);

CREATE TABLE orders2 (
    order_id INT,
    amount FLOAT,
    region CHAR(10)
);

CREATE TABLE orders3 (
    order_id INT,
    amount FLOAT,
    region CHAR(10)
);

INSERT INTO orders1 VALUES (1, 150.0, 'Beijing');
INSERT INTO orders1 VALUES (2, 230.5, 'Shanghai');
INSERT INTO orders1 VALUES (3, 89.99, 'Guangzhou');

INSERT INTO orders2 VALUES (1, 150.0, 'Beijing');
INSERT INTO orders2 VALUES (4, 120.0, 'Shenzhen');
INSERT INTO orders2 VALUES (5, 560.0, 'Chengdu');

INSERT INTO orders3 VALUES (1, 150.0, 'Beijing');
INSERT INTO orders3 VALUES (6, 199.99, 'Wuhan');

SELECT * FROM
    (SELECT * FROM orders1
     UNION
     SELECT * FROM orders2
     UNION
     SELECT * FROM orders3) AS all_orders
ORDER BY amount DESC;

-- Test 6.2 error cases (run separately)
-- DROP TABLE orders1;
-- DROP TABLE orders2;
-- DROP TABLE orders3;
