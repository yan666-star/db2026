-- The official performance workload creates primary-key indexes before running
-- transactions. RMDB indexes are unique, so only primary-key shaped columns are
-- covered here.
CREATE INDEX warehouse(w_id);
CREATE INDEX item(i_id);
CREATE INDEX stock(s_w_id, s_i_id);
CREATE INDEX district(d_w_id, d_id);
CREATE INDEX customer(c_w_id, c_d_id, c_id);
CREATE INDEX orders(o_w_id, o_d_id, o_id);
CREATE INDEX new_orders(no_w_id, no_d_id, no_o_id);
CREATE INDEX order_line(ol_w_id, ol_d_id, ol_o_id, ol_number);
