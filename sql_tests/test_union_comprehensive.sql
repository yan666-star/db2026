-- ============================================================
-- Union 综合测试集 (comprehensive)
-- ============================================================

-- ---------- 1. 基础去重：跨 4 个分支完全重复 ----------
CREATE TABLE u4a (id INT, val INT);
CREATE TABLE u4b (id INT, val INT);
CREATE TABLE u4c (id INT, val INT);
CREATE TABLE u4d (id INT, val INT);
INSERT INTO u4a VALUES (1, 100);
INSERT INTO u4b VALUES (1, 100);
INSERT INTO u4c VALUES (1, 100);
INSERT INTO u4d VALUES (1, 100);
INSERT INTO u4a VALUES (2, 200);
INSERT INTO u4b VALUES (3, 300);
-- expect: 3 rows after dedup
SELECT * FROM
    (SELECT * FROM u4a UNION SELECT * FROM u4b UNION SELECT * FROM u4c UNION SELECT * FROM u4d) AS t
ORDER BY val ASC;

-- ---------- 2. INT/FLOAT 跨类型去重（多组重复） ----------
CREATE TABLE mix1 (id INT, amt INT, city CHAR(8));
CREATE TABLE mix2 (id INT, amt FLOAT, city CHAR(12));
INSERT INTO mix1 VALUES (1, 50, 'Hangzhou');
INSERT INTO mix1 VALUES (2, 99, 'Ningbo');
INSERT INTO mix1 VALUES (3, 200, 'Wenzhou');
INSERT INTO mix2 VALUES (1, 50.0, 'Hangzhou');
INSERT INTO mix2 VALUES (4, 150.5, 'Jiaxing');
INSERT INTO mix2 VALUES (2, 99.0, 'Ningbo');
-- (1,50,Hangzhou) and (2,99,Ningbo) dedup; 4 rows total
SELECT * FROM
    (SELECT * FROM mix1 UNION SELECT * FROM mix2) AS m
ORDER BY amt DESC, id ASC;

-- ---------- 3. CHAR 长度提升 CHAR(5)+CHAR(15) ----------
CREATE TABLE cs1 (k INT, tag CHAR(5));
CREATE TABLE cs2 (k INT, tag CHAR(15));
INSERT INTO cs1 VALUES (1, 'apple');
INSERT INTO cs1 VALUES (2, 'pear');
INSERT INTO cs2 VALUES (3, 'watermelon');
INSERT INTO cs2 VALUES (1, 'apple');
SELECT * FROM
    (SELECT * FROM cs1 UNION SELECT * FROM cs2) AS c
ORDER BY k ASC;

-- ---------- 4. 多列排序：amount DESC, region ASC, order_id ASC ----------
CREATE TABLE mo1 (order_id INT, amount FLOAT, region CHAR(10));
CREATE TABLE mo2 (order_id INT, amount FLOAT, region CHAR(10));
INSERT INTO mo1 VALUES (1, 100.0, 'Beijing');
INSERT INTO mo1 VALUES (2, 100.0, 'Shanghai');
INSERT INTO mo1 VALUES (3, 200.0, 'Guangzhou');
INSERT INTO mo2 VALUES (4, 100.0, 'Beijing');
INSERT INTO mo2 VALUES (5, 50.0, 'Chengdu');
INSERT INTO mo2 VALUES (6, 200.0, 'Wuhan');
SELECT * FROM
    (SELECT * FROM mo1 UNION SELECT * FROM mo2) AS mo
ORDER BY amount DESC, region ASC, order_id ASC;

-- ---------- 5. 顶层 UNION（3 分支，无派生表包装） ----------
CREATE TABLE tl1 (x INT, y FLOAT);
CREATE TABLE tl2 (x INT, y FLOAT);
CREATE TABLE tl3 (x INT, y FLOAT);
INSERT INTO tl1 VALUES (1, 1.5);
INSERT INTO tl2 VALUES (2, 2.5);
INSERT INTO tl3 VALUES (1, 1.5);
INSERT INTO tl3 VALUES (3, 3.5);
SELECT * FROM tl1 UNION SELECT * FROM tl2 UNION SELECT * FROM tl3 ORDER BY y DESC;

-- ---------- 6. 派生表无 AS 关键字 ----------
CREATE TABLE na1 (a INT, b CHAR(6));
CREATE TABLE na2 (a INT, b CHAR(6));
INSERT INTO na1 VALUES (10, 'alpha');
INSERT INTO na2 VALUES (20, 'beta');
INSERT INTO na2 VALUES (10, 'alpha');
SELECT * FROM (SELECT * FROM na1 UNION SELECT * FROM na2) t ORDER BY a DESC;

-- ---------- 7. 限定表名 ORDER BY ----------
CREATE TABLE qo1 (id INT, score FLOAT);
CREATE TABLE qo2 (id INT, score FLOAT);
INSERT INTO qo1 VALUES (1, 88.0);
INSERT INTO qo1 VALUES (2, 92.5);
INSERT INTO qo2 VALUES (3, 75.0);
INSERT INTO qo2 VALUES (1, 88.0);
SELECT * FROM (SELECT * FROM qo1 UNION SELECT * FROM qo2) AS merged
ORDER BY merged.score DESC;

-- ---------- 8. 嵌套派生表：外层 SELECT * FROM (内层 UNION) ----------
CREATE TABLE nest1 (p INT, q INT);
CREATE TABLE nest2 (p INT, q INT);
INSERT INTO nest1 VALUES (1, 10);
INSERT INTO nest1 VALUES (2, 20);
INSERT INTO nest2 VALUES (2, 20);
INSERT INTO nest2 VALUES (3, 30);
SELECT * FROM
    (SELECT * FROM (SELECT * FROM nest1 UNION SELECT * FROM nest2) AS inner_t) AS outer_t
ORDER BY q DESC;

-- ---------- 9. 分支部分列一致 + 类型提升（2 分支 SELECT *） ----------
CREATE TABLE pt1 (a INT, b INT, c CHAR(6));
CREATE TABLE pt2 (a INT, b FLOAT, c CHAR(10));
INSERT INTO pt1 VALUES (1, 10, 'hello');
INSERT INTO pt1 VALUES (2, 20, 'world');
INSERT INTO pt2 VALUES (1, 10.0, 'hello');
INSERT INTO pt2 VALUES (3, 30.5, 'longname');
SELECT * FROM (SELECT * FROM pt1 UNION SELECT * FROM pt2) AS pt
ORDER BY b DESC, a ASC;

-- ---------- 10. 大量重复：10 行仅 2 个唯一值 ----------
CREATE TABLE bulk_a (k INT);
CREATE TABLE bulk_b (k INT);
INSERT INTO bulk_a VALUES (1);
INSERT INTO bulk_a VALUES (1);
INSERT INTO bulk_a VALUES (2);
INSERT INTO bulk_a VALUES (2);
INSERT INTO bulk_a VALUES (1);
INSERT INTO bulk_b VALUES (1);
INSERT INTO bulk_b VALUES (2);
INSERT INTO bulk_b VALUES (2);
INSERT INTO bulk_b VALUES (2);
INSERT INTO bulk_b VALUES (1);
SELECT * FROM (SELECT * FROM bulk_a UNION SELECT * FROM bulk_b) AS b ORDER BY k ASC;

-- ---------- 11. FLOAT 格式边界：整数值 / 小数 ----------
CREATE TABLE fl1 (id INT, price FLOAT);
CREATE TABLE fl2 (id INT, price FLOAT);
INSERT INTO fl1 VALUES (1, 560.0);
INSERT INTO fl1 VALUES (2, 230.5);
INSERT INTO fl1 VALUES (3, 89.99);
INSERT INTO fl2 VALUES (4, 120.0);
INSERT INTO fl2 VALUES (5, 199.99);
INSERT INTO fl2 VALUES (1, 560.0);
SELECT * FROM (SELECT * FROM fl1 UNION SELECT * FROM fl2) AS f
ORDER BY price DESC;

-- ---------- 12. 顶层 UNION + INT/FLOAT 提升 + 去重 ----------
CREATE TABLE top1 (id INT, v INT);
CREATE TABLE top2 (id INT, v FLOAT);
INSERT INTO top1 VALUES (1, 100);
INSERT INTO top1 VALUES (2, 200);
INSERT INTO top2 VALUES (1, 100.0);
INSERT INTO top2 VALUES (3, 300.5);
SELECT * FROM top1 UNION SELECT * FROM top2 ORDER BY v ASC, id ASC;

-- ---------- 13. 三表 CHAR(10) 完全相同 schema + 6.1 变体 ----------
CREATE TABLE o1 (order_id INT, amount FLOAT, region CHAR(10));
CREATE TABLE o2 (order_id INT, amount FLOAT, region CHAR(10));
CREATE TABLE o3 (order_id INT, amount FLOAT, region CHAR(10));
INSERT INTO o1 VALUES (1, 150.0, 'Beijing');
INSERT INTO o1 VALUES (2, 230.5, 'Shanghai');
INSERT INTO o2 VALUES (1, 150.0, 'Beijing');
INSERT INTO o2 VALUES (4, 120.0, 'Shenzhen');
INSERT INTO o3 VALUES (1, 150.0, 'Beijing');
INSERT INTO o3 VALUES (6, 199.99, 'Wuhan');
SELECT * FROM (SELECT * FROM o1 UNION SELECT * FROM o2 UNION SELECT * FROM o3) AS o
ORDER BY amount DESC, order_id ASC;

-- ---------- 14. ORDER BY 第一列 ASC 默认（无 ASC/DESC 关键字） ----------
CREATE TABLE def1 (n INT, s CHAR(4));
CREATE TABLE def2 (n INT, s CHAR(4));
INSERT INTO def1 VALUES (3, 'ccc');
INSERT INTO def1 VALUES (1, 'aaa');
INSERT INTO def2 VALUES (2, 'bbb');
INSERT INTO def2 VALUES (1, 'aaa');
SELECT * FROM (SELECT * FROM def1 UNION SELECT * FROM def2) AS d
ORDER BY n, s;

-- ---------- 15. 单分支重复 + 跨分支：验证 UNION ALL 语义不是 ALL ----------
CREATE TABLE dup1 (v INT);
CREATE TABLE dup2 (v INT);
INSERT INTO dup1 VALUES (5);
INSERT INTO dup1 VALUES (5);
INSERT INTO dup2 VALUES (5);
INSERT INTO dup2 VALUES (6);
SELECT * FROM (SELECT * FROM dup1 UNION SELECT * FROM dup2) AS d ORDER BY v;
