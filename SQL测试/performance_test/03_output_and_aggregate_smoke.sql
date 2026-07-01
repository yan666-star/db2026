-- Independent aggregate probe for string MIN/MAX.
CREATE TABLE agg_probe (
    id INT,
    name CHAR(8),
    amount FLOAT
);

INSERT INTO agg_probe VALUES (1, 'pear', 7.5);
INSERT INTO agg_probe VALUES (2, 'apple', 3.25);
INSERT INTO agg_probe VALUES (3, 'orange', 5.0);

SELECT MIN(name) AS min_name, MAX(name) AS max_name
FROM agg_probe;

SELECT COUNT(*) AS warehouse_count
FROM warehouse;

-- Required by the statement: no semicolon.
set output_file off

-- These SELECT results should still be returned to the client, but output.txt
-- must not grow after the switch above.
SELECT MIN(i_name) AS min_item_name, MAX(i_name) AS max_item_name
FROM item;

SELECT COUNT(*) AS customer_count
FROM customer;
