BEGIN;

SELECT c_discount, c_last, c_credit, w_tax
FROM customer, warehouse
WHERE w_id = 1
  AND c_w_id = w_id
  AND c_d_id = 1
  AND c_id = 2;

SELECT d_next_o_id, d_tax
FROM district
WHERE d_id = 1
  AND d_w_id = 1;

UPDATE district
SET d_next_o_id = 6
WHERE d_id = 1
  AND d_w_id = 1;

INSERT INTO orders
VALUES (5, 1, 1, 2, '2023-06-03 19:25:47', 26, 5, 1);

INSERT INTO new_orders
VALUES (5, 1, 1);

SELECT i_price, i_name, i_data
FROM item
WHERE i_id = 10;

SELECT s_quantity, s_data,
       s_dist_01, s_dist_02, s_dist_03, s_dist_04, s_dist_05,
       s_dist_06, s_dist_07, s_dist_08, s_dist_09, s_dist_10
FROM stock
WHERE s_i_id = 10
  AND s_w_id = 1;

UPDATE stock
SET s_quantity = 8
WHERE s_i_id = 10
  AND s_w_id = 1;

INSERT INTO order_line
VALUES (
    5, 1, 1, 1, 10, 1,
    '2023-06-03 19:25:47',
    8,
    327.600000,
    'statement-complete-test'
);

SELECT i_price, i_name, i_data
FROM item
WHERE i_id = 10;

COMMIT;

BEGIN;

SELECT d_next_o_id, d_tax
FROM district
WHERE d_id = 1
  AND d_w_id = 1;

UPDATE district
SET d_next_o_id = 99
WHERE d_id = 1
  AND d_w_id = 1;

UPDATE stock
SET s_quantity = 99
WHERE s_i_id = 10
  AND s_w_id = 1;

INSERT INTO orders
VALUES (6, 1, 1, 2, '2023-06-03 19:25:47', 26, 5, 1);

INSERT INTO new_orders
VALUES (6, 1, 1);

CRASH;
