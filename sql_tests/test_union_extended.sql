-- ============================================================
-- Union 扩展测试（更多数据量 / 边界 / 组合）
-- ============================================================

-- ---------- A. 宽表 5 列 + 三分支 ----------
CREATE TABLE wide1 (c1 INT, c2 INT, c3 FLOAT, c4 CHAR(8), c5 CHAR(6));
CREATE TABLE wide2 (c1 INT, c2 INT, c3 FLOAT, c4 CHAR(8), c5 CHAR(6));
CREATE TABLE wide3 (c1 INT, c2 INT, c3 FLOAT, c4 CHAR(8), c5 CHAR(6));
INSERT INTO wide1 VALUES (1, 10, 1.1, 'alpha', 'x');
INSERT INTO wide1 VALUES (2, 20, 2.2, 'beta', 'y');
INSERT INTO wide2 VALUES (1, 10, 1.1, 'alpha', 'x');
INSERT INTO wide2 VALUES (3, 30, 3.3, 'gamma', 'z');
INSERT INTO wide3 VALUES (4, 40, 4.4, 'delta', 'w');
INSERT INTO wide3 VALUES (2, 20, 2.2, 'beta', 'y');
SELECT * FROM
    (SELECT * FROM wide1 UNION SELECT * FROM wide2 UNION SELECT * FROM wide3) AS w
ORDER BY c3 DESC, c1 ASC;

-- ---------- B. 分支带 WHERE 过滤 ----------
CREATE TABLE wf1 (id INT, status INT, val FLOAT);
CREATE TABLE wf2 (id INT, status INT, val FLOAT);
INSERT INTO wf1 VALUES (1, 1, 10.0);
INSERT INTO wf1 VALUES (2, 0, 20.0);
INSERT INTO wf1 VALUES (3, 1, 30.0);
INSERT INTO wf2 VALUES (4, 1, 40.0);
INSERT INTO wf2 VALUES (5, 0, 50.0);
INSERT INTO wf2 VALUES (1, 1, 10.0);
SELECT * FROM
    (SELECT * FROM wf1 WHERE status = 1 UNION SELECT * FROM wf2 WHERE status = 1) AS w
ORDER BY val ASC;

-- ---------- C. 全相同 amount，按 region 字典序 ASC ----------
CREATE TABLE tie1 (id INT, amount FLOAT, region CHAR(10));
CREATE TABLE tie2 (id INT, amount FLOAT, region CHAR(10));
INSERT INTO tie1 VALUES (1, 100.0, 'Beijing');
INSERT INTO tie1 VALUES (2, 100.0, 'Shanghai');
INSERT INTO tie2 VALUES (3, 100.0, 'Guangzhou');
INSERT INTO tie2 VALUES (4, 100.0, 'Beijing');
SELECT * FROM (SELECT * FROM tie1 UNION SELECT * FROM tie2) AS t
ORDER BY amount DESC, region ASC;

-- ---------- D. CHAR(20) 长字符串 + 短字符串混合 ----------
CREATE TABLE lng1 (id INT, txt CHAR(20));
CREATE TABLE lng2 (id INT, txt CHAR(20));
INSERT INTO lng1 VALUES (1, 'Short');
INSERT INTO lng1 VALUES (2, 'MediumLen');
INSERT INTO lng2 VALUES (3, 'VeryLongRegionName');
INSERT INTO lng2 VALUES (1, 'Short');
SELECT * FROM (SELECT * FROM lng1 UNION SELECT * FROM lng2) AS l
ORDER BY id ASC;

-- ---------- E. 仅选部分列（2 列），两分支列数一致 ----------
CREATE TABLE pc1 (a INT, b INT, c INT);
CREATE TABLE pc2 (a INT, b INT, c INT);
INSERT INTO pc1 VALUES (1, 10, 100);
INSERT INTO pc1 VALUES (2, 20, 200);
INSERT INTO pc2 VALUES (1, 10, 999);
INSERT INTO pc2 VALUES (3, 30, 300);
SELECT * FROM
    (SELECT a, b FROM pc1 UNION SELECT a, b FROM pc2) AS p
ORDER BY b DESC;

-- ---------- F. INT/FLOAT 全列提升 + 三行去重为一行 ----------
CREATE TABLE fd1 (k INT, f INT, s CHAR(4));
CREATE TABLE fd2 (k INT, f FLOAT, s CHAR(8));
INSERT INTO fd1 VALUES (7, 42, 'test');
INSERT INTO fd2 VALUES (7, 42.0, 'test');
INSERT INTO fd2 VALUES (8, 99.9, 'data');
SELECT * FROM (SELECT * FROM fd1 UNION SELECT * FROM fd2) AS f ORDER BY k;

-- ---------- G. 顶层 UNION + 派生表混合（先测顶层） ----------
CREATE TABLE g1 (n INT);
CREATE TABLE g2 (n INT);
INSERT INTO g1 VALUES (5);
INSERT INTO g1 VALUES (1);
INSERT INTO g2 VALUES (3);
INSERT INTO g2 VALUES (1);
SELECT * FROM g1 UNION SELECT * FROM g2 ORDER BY n DESC;

-- ---------- H. 大数值 INT + FLOAT ----------
CREATE TABLE big1 (v INT);
CREATE TABLE big2 (v FLOAT);
INSERT INTO big1 VALUES (1000000);
INSERT INTO big1 VALUES (-999);
INSERT INTO big2 VALUES (1000000.0);
INSERT INTO big2 VALUES (0.5);
SELECT * FROM (SELECT * FROM big1 UNION SELECT * FROM big2) AS b ORDER BY v DESC;

-- ---------- I. 四分支不同数据量（1/2/3/4 行） ----------
CREATE TABLE quad_a (x INT);
CREATE TABLE quad_b (x INT);
CREATE TABLE quad_c (x INT);
CREATE TABLE quad_d (x INT);
INSERT INTO quad_a VALUES (1);
INSERT INTO quad_b VALUES (2);
INSERT INTO quad_b VALUES (3);
INSERT INTO quad_c VALUES (3);
INSERT INTO quad_c VALUES (4);
INSERT INTO quad_c VALUES (5);
INSERT INTO quad_d VALUES (1);
INSERT INTO quad_d VALUES (5);
INSERT INTO quad_d VALUES (6);
INSERT INTO quad_d VALUES (7);
SELECT * FROM
    (SELECT * FROM quad_a UNION SELECT * FROM quad_b
     UNION SELECT * FROM quad_c UNION SELECT * FROM quad_d) AS q
ORDER BY x ASC;

-- ---------- J. ORDER BY 三列 ----------
CREATE TABLE tri1 (a INT, b INT, c INT);
CREATE TABLE tri2 (a INT, b INT, c INT);
INSERT INTO tri1 VALUES (1, 2, 3);
INSERT INTO tri1 VALUES (1, 1, 9);
INSERT INTO tri2 VALUES (1, 2, 1);
INSERT INTO tri2 VALUES (2, 0, 0);
SELECT * FROM (SELECT * FROM tri1 UNION SELECT * FROM tri2) AS t
ORDER BY a ASC, b DESC, c ASC;

-- ---------- K. 小数精度边界 ----------
CREATE TABLE prec1 (id INT, v FLOAT);
CREATE TABLE prec2 (id INT, v FLOAT);
INSERT INTO prec1 VALUES (1, 0.1);
INSERT INTO prec1 VALUES (2, 0.2);
INSERT INTO prec2 VALUES (3, 0.3);
INSERT INTO prec2 VALUES (1, 0.1);
SELECT * FROM (SELECT * FROM prec1 UNION SELECT * FROM prec2) AS p ORDER BY v;

-- ---------- L. 重复 region 不同 order_id（6.1 类似但更多行） ----------
CREATE TABLE ord1 (order_id INT, amount FLOAT, region CHAR(10));
CREATE TABLE ord2 (order_id INT, amount FLOAT, region CHAR(10));
CREATE TABLE ord3 (order_id INT, amount FLOAT, region CHAR(10));
INSERT INTO ord1 VALUES (1, 150.0, 'Beijing');
INSERT INTO ord1 VALUES (2, 230.5, 'Shanghai');
INSERT INTO ord1 VALUES (3, 89.99, 'Guangzhou');
INSERT INTO ord2 VALUES (1, 150.0, 'Beijing');
INSERT INTO ord2 VALUES (4, 120.0, 'Shenzhen');
INSERT INTO ord2 VALUES (5, 560.0, 'Chengdu');
INSERT INTO ord3 VALUES (1, 150.0, 'Beijing');
INSERT INTO ord3 VALUES (6, 199.99, 'Wuhan');
INSERT INTO ord3 VALUES (7, 560.0, 'Chengdu');
SELECT * FROM
    (SELECT * FROM ord1 UNION SELECT * FROM ord2 UNION SELECT * FROM ord3) AS o
ORDER BY amount DESC, order_id ASC;
