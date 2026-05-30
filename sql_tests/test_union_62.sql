-- Test 6.2: type compatibility
CREATE TABLE orders1 (
    order_id INT,
    amount INT,
    region CHAR(10)
);

CREATE TABLE orders2 (
    order_id INT,
    amount FLOAT,
    region CHAR(20)
);

INSERT INTO orders1 VALUES (1, 150, 'Beijing');
INSERT INTO orders1 VALUES (2, 89, 'Shanghai');
INSERT INTO orders2 VALUES (3, 230.5, 'LongRegionNameHere');
INSERT INTO orders2 VALUES (4, 120.0, 'Shenzhen');

-- error 1: column count mismatch
SELECT * FROM
    (SELECT amount FROM orders1
     UNION
     SELECT * FROM orders2
    ) AS all_orders;

-- error 2: incompatible types
SELECT * FROM
    (SELECT amount FROM orders1
     UNION
     SELECT region FROM orders2
    ) AS all_orders;

-- error 3: unknown ORDER BY column
SELECT * FROM
    (SELECT amount FROM orders1
     UNION
     SELECT amount FROM orders2
    ) AS all_orders
ORDER BY order_id ASC;

-- type promotion test
SELECT * FROM
    (SELECT * FROM orders1
     UNION
     SELECT * FROM orders2
    ) AS all_orders
ORDER BY amount DESC, order_id ASC;
