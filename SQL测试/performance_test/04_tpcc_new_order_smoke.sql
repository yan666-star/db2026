-- A compact NewOrder-shaped transaction. It is intentionally tiny: the goal is
-- to validate query/update paths used by the real performance workload, not to
-- reproduce the official timed benchmark.
set transaction isolation level snapshot isolation;

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
SET d_next_o_id = d_next_o_id + 1
WHERE d_id = 1
  AND d_w_id = 1;

INSERT INTO orders
VALUES (11, 1, 1, 2, '2026-07-01 10:00:00', 0, 2, 1);

INSERT INTO new_orders
VALUES (11, 1, 1);

SELECT i_price, i_name, i_data
FROM item
WHERE i_id = 1;

SELECT s_quantity, s_data,
       s_dist_01, s_dist_02, s_dist_03, s_dist_04, s_dist_05,
       s_dist_06, s_dist_07, s_dist_08, s_dist_09, s_dist_10
FROM stock
WHERE s_i_id = 1
  AND s_w_id = 1;

UPDATE stock
SET s_quantity = s_quantity - 5
WHERE s_i_id = 1
  AND s_w_id = 1;

INSERT INTO order_line
VALUES (
    11, 1, 1, 1, 1, 1,
    '2026-07-01 10:00:00',
    5,
    1937.5,
    'perf-smoke-line-1'
);

SELECT i_price, i_name, i_data
FROM item
WHERE i_id = 2;

SELECT s_quantity, s_data,
       s_dist_01, s_dist_02, s_dist_03, s_dist_04, s_dist_05,
       s_dist_06, s_dist_07, s_dist_08, s_dist_09, s_dist_10
FROM stock
WHERE s_i_id = 2
  AND s_w_id = 1;

UPDATE stock
SET s_quantity = s_quantity - 3
WHERE s_i_id = 2
  AND s_w_id = 1;

INSERT INTO order_line
VALUES (
    11, 1, 1, 2, 2, 1,
    '2026-07-01 10:00:00',
    3,
    100.0,
    'perf-smoke-line-2'
);

COMMIT;

SELECT d_next_o_id
FROM district
WHERE d_id = 1
  AND d_w_id = 1;

SELECT o_id
FROM orders
WHERE o_id = 11
  AND o_d_id = 1
  AND o_w_id = 1;

SELECT ol_o_id, ol_number, ol_quantity
FROM order_line
WHERE ol_o_id = 11
  AND ol_d_id = 1
  AND ol_w_id = 1;
