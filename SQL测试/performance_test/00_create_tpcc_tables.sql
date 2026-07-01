-- TPC-C shaped tables used by the performance-test smoke suite.
CREATE TABLE warehouse (
    w_id INT,
    w_name CHAR(10),
    w_street_1 CHAR(20),
    w_street_2 CHAR(20),
    w_city CHAR(20),
    w_state CHAR(2),
    w_zip CHAR(9),
    w_tax FLOAT,
    w_ytd FLOAT
);

CREATE TABLE district (
    d_id INT,
    d_w_id INT,
    d_name CHAR(10),
    d_street_1 CHAR(20),
    d_street_2 CHAR(20),
    d_city CHAR(20),
    d_state CHAR(2),
    d_zip CHAR(9),
    d_tax FLOAT,
    d_ytd FLOAT,
    d_next_o_id INT
);

CREATE TABLE customer (
    c_id INT,
    c_d_id INT,
    c_w_id INT,
    c_first CHAR(16),
    c_middle CHAR(2),
    c_last CHAR(16),
    c_street_1 CHAR(20),
    c_street_2 CHAR(20),
    c_city CHAR(20),
    c_state CHAR(2),
    c_zip CHAR(9),
    c_phone CHAR(16),
    c_since CHAR(30),
    c_credit CHAR(2),
    c_credit_lim INT,
    c_discount FLOAT,
    c_balance FLOAT,
    c_ytd_payment FLOAT,
    c_payment_cnt INT,
    c_delivery_cnt INT,
    c_data CHAR(50)
);

CREATE TABLE history (
    h_c_id INT,
    h_c_d_id INT,
    h_c_w_id INT,
    h_d_id INT,
    h_w_id INT,
    h_date CHAR(19),
    h_amount FLOAT,
    h_data CHAR(24)
);

CREATE TABLE orders (
    o_id INT,
    o_d_id INT,
    o_w_id INT,
    o_c_id INT,
    o_entry_d CHAR(19),
    o_carrier_id INT,
    o_ol_cnt INT,
    o_all_local INT
);

CREATE TABLE new_orders (
    no_o_id INT,
    no_d_id INT,
    no_w_id INT
);

CREATE TABLE order_line (
    ol_o_id INT,
    ol_d_id INT,
    ol_w_id INT,
    ol_number INT,
    ol_i_id INT,
    ol_supply_w_id INT,
    ol_delivery_d CHAR(30),
    ol_quantity INT,
    ol_amount FLOAT,
    ol_dist_info CHAR(24)
);

CREATE TABLE item (
    i_id INT,
    i_im_id INT,
    i_name CHAR(24),
    i_price FLOAT,
    i_data CHAR(50)
);

CREATE TABLE stock (
    s_i_id INT,
    s_w_id INT,
    s_quantity INT,
    s_dist_01 CHAR(24),
    s_dist_02 CHAR(24),
    s_dist_03 CHAR(24),
    s_dist_04 CHAR(24),
    s_dist_05 CHAR(24),
    s_dist_06 CHAR(24),
    s_dist_07 CHAR(24),
    s_dist_08 CHAR(24),
    s_dist_09 CHAR(24),
    s_dist_10 CHAR(24),
    s_ytd FLOAT,
    s_order_cnt INT,
    s_remote_cnt INT,
    s_data CHAR(50)
);
