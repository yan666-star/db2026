-- ============================================================
-- Union 错误路径 + 边界测试
-- ============================================================

-- E1: 列数不一致
CREATE TABLE e1a (a INT, b INT);
CREATE TABLE e1b (a INT, b INT, c INT);
INSERT INTO e1a VALUES (1, 2);
INSERT INTO e1b VALUES (1, 2, 3);
SELECT * FROM (SELECT a FROM e1a UNION SELECT * FROM e1b) AS t;

-- E2: 类型不兼容 INT vs CHAR
CREATE TABLE e2a (x INT);
CREATE TABLE e2b (x CHAR(4));
INSERT INTO e2a VALUES (1);
INSERT INTO e2b VALUES ('ab');
SELECT * FROM (SELECT x FROM e2a UNION SELECT x FROM e2b) AS t;

-- E3: ORDER BY 未知列（派生表只暴露 a）
CREATE TABLE e3a (a INT, b INT);
CREATE TABLE e3b (a INT, b INT);
INSERT INTO e3a VALUES (1, 10);
INSERT INTO e3b VALUES (2, 20);
SELECT * FROM (SELECT a FROM e3a UNION SELECT a FROM e3b) AS t ORDER BY b ASC;

-- E4: ORDER BY 引用内部分支表名（应 failure，不能越权）
CREATE TABLE e4a (a INT, b INT);
CREATE TABLE e4b (a INT, b INT);
INSERT INTO e4a VALUES (1, 10);
INSERT INTO e4b VALUES (2, 20);
SELECT * FROM (SELECT * FROM e4a UNION SELECT * FROM e4b) AS t ORDER BY e4a.b DESC;

-- E5: 分支含 GROUP BY（不允许）
CREATE TABLE e5a (a INT);
CREATE TABLE e5b (a INT);
INSERT INTO e5a VALUES (1);
INSERT INTO e5b VALUES (2);
SELECT * FROM (SELECT a FROM e5a GROUP BY a UNION SELECT a FROM e5b) AS t;

-- E6: 单分支 UNION（语法上需要 n>=2，仅 1 个分支应 failure）
-- 注：parser 层单 select_branch 不是 union，派生表单 select 合法；此处测 union_branches<2
CREATE TABLE e6a (a INT);
INSERT INTO e6a VALUES (1);
-- 合法：单表派生表（非 union）
SELECT * FROM (SELECT * FROM e6a) AS t;

-- E7: 空结果 UNION
CREATE TABLE e7a (a INT);
CREATE TABLE e7b (a INT);
SELECT * FROM (SELECT * FROM e7a UNION SELECT * FROM e7b) AS t ORDER BY a;

-- E8: 三向类型提升 + 成功路径
CREATE TABLE e8a (id INT, amt INT, name CHAR(5));
CREATE TABLE e8b (id INT, amt FLOAT, name CHAR(10));
INSERT INTO e8a VALUES (1, 100, 'short');
INSERT INTO e8b VALUES (2, 200.5, 'longername');
SELECT * FROM (SELECT * FROM e8a UNION SELECT * FROM e8b) AS t ORDER BY amt DESC;
