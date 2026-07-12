-- test1: two-table full match, NLJ then INLJ
CREATE TABLE join_full_l (id int, name char(8));
CREATE TABLE join_full_r (id int, value char(8));
INSERT INTO join_full_l VALUES (1, 'L1');
INSERT INTO join_full_l VALUES (2, 'L2');
INSERT INTO join_full_l VALUES (3, 'L3');
INSERT INTO join_full_r VALUES (1, 'R1');
INSERT INTO join_full_r VALUES (2, 'R2');
INSERT INTO join_full_r VALUES (3, 'R3');

SELECT join_full_l.name, join_full_r.value
FROM join_full_l
JOIN join_full_r ON join_full_l.id = join_full_r.id;
EXPLAIN ANALYZE SELECT join_full_l.name, join_full_r.value
FROM join_full_l
JOIN join_full_r ON join_full_l.id = join_full_r.id;

CREATE INDEX join_full_r(id);

SELECT join_full_l.name, join_full_r.value
FROM join_full_l
JOIN join_full_r ON join_full_l.id = join_full_r.id;
EXPLAIN ANALYZE SELECT join_full_l.name, join_full_r.value
FROM join_full_l
JOIN join_full_r ON join_full_l.id = join_full_r.id;

-- test2: two-table partial match, NLJ then INLJ
CREATE TABLE join_part_l (id int, name char(8));
CREATE TABLE join_part_r (id int, value char(8));
INSERT INTO join_part_l VALUES (1, 'L1');
INSERT INTO join_part_l VALUES (2, 'L2');
INSERT INTO join_part_l VALUES (4, 'L4');
INSERT INTO join_part_r VALUES (1, 'R1');
INSERT INTO join_part_r VALUES (2, 'R2');
INSERT INTO join_part_r VALUES (3, 'R3');

SELECT join_part_l.name, join_part_r.value
FROM join_part_l
JOIN join_part_r ON join_part_l.id = join_part_r.id;
EXPLAIN ANALYZE SELECT join_part_l.name, join_part_r.value
FROM join_part_l
JOIN join_part_r ON join_part_l.id = join_part_r.id;

CREATE INDEX join_part_r(id);

SELECT join_part_l.name, join_part_r.value
FROM join_part_l
JOIN join_part_r ON join_part_l.id = join_part_r.id;
EXPLAIN ANALYZE SELECT join_part_l.name, join_part_r.value
FROM join_part_l
JOIN join_part_r ON join_part_l.id = join_part_r.id;

-- test3: two-table no match, NLJ then INLJ
CREATE TABLE join_none_l (id int);
CREATE TABLE join_none_r (id int);
INSERT INTO join_none_l VALUES (1);
INSERT INTO join_none_l VALUES (2);
INSERT INTO join_none_r VALUES (3);
INSERT INTO join_none_r VALUES (4);

SELECT join_none_l.id, join_none_r.id
FROM join_none_l
JOIN join_none_r ON join_none_l.id = join_none_r.id;
EXPLAIN ANALYZE SELECT join_none_l.id, join_none_r.id
FROM join_none_l
JOIN join_none_r ON join_none_l.id = join_none_r.id;

CREATE INDEX join_none_r(id);

SELECT join_none_l.id, join_none_r.id
FROM join_none_l
JOIN join_none_r ON join_none_l.id = join_none_r.id;
EXPLAIN ANALYZE SELECT join_none_l.id, join_none_r.id
FROM join_none_l
JOIN join_none_r ON join_none_l.id = join_none_r.id;

-- test5: five-table left-deep join, including multiple ON predicates
CREATE TABLE join_t1 (id int, code int);
CREATE TABLE join_t2 (id int, code int);
CREATE TABLE join_t3 (id int);
CREATE TABLE join_t4 (id int);
CREATE TABLE join_t5 (id int);
INSERT INTO join_t1 VALUES (1, 10);
INSERT INTO join_t1 VALUES (2, 20);
INSERT INTO join_t2 VALUES (1, 10);
INSERT INTO join_t2 VALUES (2, 20);
INSERT INTO join_t3 VALUES (1);
INSERT INTO join_t3 VALUES (2);
INSERT INTO join_t4 VALUES (1);
INSERT INTO join_t4 VALUES (2);
INSERT INTO join_t5 VALUES (1);
INSERT INTO join_t5 VALUES (2);

EXPLAIN ANALYZE SELECT join_t1.id, join_t5.id
FROM join_t1
JOIN join_t2 ON join_t1.id = join_t2.id AND join_t1.code = join_t2.code
JOIN join_t3 ON join_t1.id = join_t3.id
JOIN join_t4 ON join_t1.id = join_t4.id
JOIN join_t5 ON join_t1.id = join_t5.id;

CREATE INDEX join_t2(id);
CREATE INDEX join_t3(id);
CREATE INDEX join_t4(id);
CREATE INDEX join_t5(id);

SELECT join_t1.id, join_t5.id
FROM join_t1
JOIN join_t2 ON join_t1.id = join_t2.id AND join_t1.code = join_t2.code
JOIN join_t3 ON join_t1.id = join_t3.id
JOIN join_t4 ON join_t1.id = join_t4.id
JOIN join_t5 ON join_t1.id = join_t5.id;
EXPLAIN ANALYZE SELECT join_t1.id, join_t5.id
FROM join_t1
JOIN join_t2 ON join_t1.id = join_t2.id AND join_t1.code = join_t2.code
JOIN join_t3 ON join_t1.id = join_t3.id
JOIN join_t4 ON join_t1.id = join_t4.id
JOIN join_t5 ON join_t1.id = join_t5.id;
