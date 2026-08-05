-- Load generated TPC-C data into tables.
-- The DATA_DIR placeholder is replaced at runtime by the benchmark script.
-- Default paths point to the mini dataset; use --data-dir to override.
load ../../src/test/performance_test/table_data/warehouse.csv into warehouse;
load ../../src/test/performance_test/table_data/item.csv into item;
load ../../src/test/performance_test/table_data/stock.csv into stock;
load ../../src/test/performance_test/table_data/district.csv into district;
load ../../src/test/performance_test/table_data/customer.csv into customer;
load ../../src/test/performance_test/table_data/history.csv into history;
load ../../src/test/performance_test/table_data/orders.csv into orders;
load ../../src/test/performance_test/table_data/new_orders.csv into new_orders;
load ../../src/test/performance_test/table_data/order_line.csv into order_line;
