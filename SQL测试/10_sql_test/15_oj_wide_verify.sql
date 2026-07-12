SELECT d_id, d_w_id, d_next_o_id
FROM district
ORDER BY d_id ASC;

SELECT s_i_id, s_w_id, s_quantity
FROM stock
ORDER BY s_i_id ASC;

SELECT * FROM orders ORDER BY o_id ASC;
SELECT * FROM new_orders ORDER BY no_o_id ASC;
SELECT * FROM order_line ORDER BY ol_o_id ASC;
