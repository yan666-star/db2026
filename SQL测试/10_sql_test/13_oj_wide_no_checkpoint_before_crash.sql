BEGIN;
UPDATE district SET d_next_o_id = 99 WHERE d_id = 1 AND d_w_id = 1;
UPDATE stock SET s_quantity = 99 WHERE s_i_id = 10 AND s_w_id = 1;
INSERT INTO new_orders VALUES (99, 1, 1);

CRASH;
