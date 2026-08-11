# 一些建议：

分散磁盘I/O

在使用磁盘时，我们应该选用多块磁盘代替一块超大容量的磁盘。这样可以有效的提升磁盘并行读写性能，因此可以提升系统的整体吞吐量。

使用比较大的数据库Block Size

这是一条需要权衡的经验。如果选择了很大的数据库块大小，会降低对单个块上数据的读写效率，存在较大的争用。与此同时，对某些数据迁移方法上也存在一些限制。除此之外，应该说增加数据库块大小都有利于提高数据库性能。例如，较大的数据块可以保证内存中存放更多常用的数据；防止索引的level过高等。

合理利用重复存储
空间换时间，减少多表查询

 为经常需要排序、分组和联合操作的字段建立索引
经常需要 ORDER BY、GROUP BY、DISTINCT 和 UNION  等操作的字段，排序操作会浪费很多时间。如果为其建立索引，可以有效地避免排序操作。

 为常作为查询条件的字段建立索引
如果某个字段经常用来做查询条件，那么该字段的查询速度会影响整个表的查询速度。因此，为这样的字段建立索引，可以提高整个表的查询速度。

注意：常查询条件的字段不一定是所要选择的列，换句话说，最适合索引的列是出现在 WHERE 子句中的列，或连接子句中指定的列，而不是出现在 SELECT 关键字后的选择列表中的列


 尽量使用数据量少的索引
如果索引的值很长，那么查询的速度会受到影响。例如，对一个 CHAR(100) 类型的字段进行全文检索需要的时间肯定要比对 CHAR(10) 类型的字段需要的时间要多

尽量使用前缀来索引
如果索引字段的值很长，最好使用值的前缀来索引。例如，TEXT 和 BLOG 类型的字段，进行全文检索会很浪费时间。如果只检索字段的前面的若干个字符，这样可以提高检索速度。

 删除不再使用或者很少使用的索引
表中的数据被大量更新，或者数据的使用方式被改变后，原有的一些索引可能不再需要。应该定期找出这些索引，将它们删除，从而减少索引对更新操作的影响。

不使用ORDER BY RAND()
select id from `dynamic` order by rand() limit 1000;
运行项目并下载源码
上面的SQL语句，可优化为：

select id from `dynamic` t1 join (select rand() * (select max(id) from `dynamic`) as nid) t2 on t1.id > t2.nidl


对于联合索引来说，要遵守最左前缀法则
举列来说索引含有字段id、name、school，可以直接用id字段，也可以id、name这样的顺序，但是name;school都无法使用这个索引。所以在创建联合索引的时候一定要注意索引字段顺序，常用的查询字段放在最前面。

合理使用事务
尽量缩小事务的范围，避免长时间占用数据库资源。使用合适的隔离级别，READ COMMITTED 隔离级别相对于 REPEATABLE READ 会有更高的并发性能，但需要权衡数据一致性需求

优化查询语句
尽量减少大范围扫描和复杂查询，对必要的数据表和字段添加合适的索引，以提高查询效率。

调整系统参数
根据实际情况调整 InnoDB 参数，如 innodb_max_dirty_pages_pct、innodb_log_buffer_size 和 innodb_flush_log_at_trx_commit，以优化数据库性能。

 Midpoint Insertion LRU：链表被分为 New Sublist（热端，5/8）和 Old Sublist（冷端，3/8）。新读入的页先插入冷端，只有在冷端存活超过 innodb_old_blocks_time（默认1秒）后再次被访问，才晋升到热端。

这样设计是为了防止全表扫描这类"一次性"操作把真正的热数据从缓存中刷掉。

View 的可见性判断逻辑
-- Read View 包含以下信息：
-- m_ids：生成快照时，当前系统中活跃（未提交）的事务ID列表
-- min_trx_id：m_ids 中最小值
-- max_trx_id：生成快照时，下一个将分配的事务ID
-- creator_trx_id：创建此 Read View 的事务ID

-- 对于某行数据的版本（trx_id），可见性规则：
-- 1. trx_id == creator_trx_id → 自己修改的，可见
-- 2. trx_id < min_trx_id     → 已提交的老事务，可见
-- 3. trx_id >= max_trx_id    → 快照之后开启的事务，不可见
-- 4. min_trx_id <= trx_id < max_trx_id：
--    若 trx_id 在 m_ids 中 → 未提交，不可见
--    若 trx_id 不在 m_ids 中 → 已提交，可见
-- 不可见时，沿 ROLL_PTR 找上一个版本，直到找到可见版本
Lobster AI
sql
1
2
3
4
5
6
7
8
9
10
11
12
13
14
3.3 RC 与 RR  隔离级别的本质差异
-- READ COMMITTED（RC）：每次 SELECT 都生成新的 Read View
--   → 能读到其他事务提交后的数据（不可重复读）

-- REPEATABLE READ（RR，MySQL默认）：事务开始时生成一次 Read View，全程复用
--   → 整个事务内读到的数据一致（可重复读）

SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ;
START TRANSACTION;
SELECT * FROM orders WHERE id = 1;    -- 读到版本 A
-- 此时另一个事务修改了 id=1 并提交
SELECT * FROM orders WHERE id = 1;    -- RR下仍读到版本 A，RC下读到新版本


WAL（Write-Ahead Logging ）：数据修改时先写 Redo Log，再异步将脏页刷回磁盘。即使系统崩溃，重启后通过 Redo Log 可以重放未落盘的修改，保证数据不丢失。

[mysqld]
# 单个 redo log 文件大小（MySQL 8.0.30 之前）
innodb_log_file_size = 2G

# redo log 文件数量
innodb_log_files_in_group = 2

# 刷盘策略（最重要的参数）
# 0 = 每秒刷一次（性能最好，宕机最多丢1秒数据）
# 1 = 每次提交都刷（最安全，性能最低）
# 2 = 每次提交写OS缓冲，每秒刷盘（折中方案）
innodb_flush_log_at_trx_commit = 1
Lobster AI
ini
1
2
3
4
5
6
7
8
9
10
11
12
4.2 Checkpoint 机制与脏页刷新
[mysqld]
# 脏页比例上限，超过则加速刷新（默认75）
innodb_max_dirty_pages_pct = 75

# IO 能力上限，影响刷脏速度（根据磁盘 IOPS 设置）
# SSD 可设 2000-10000，HDD 建议 200-800
innodb_io_capacity = 4000
innodb_io_capacity_max = 8000

# 自适应刷新（根据 redo log 消耗速率动态调整刷脏）
innodb_adaptive_flushing = ON

连接池参数黄金法则
# HikariCP 推荐配置
spring:
  datasource:
    hikari:
      # 核心公式：pool_size = (核心数 * 2) + 磁盘数
      maximum-pool-size: 20
      minimum-idle: 5
      connection-timeout: 30000
      max-lifetime: 1800000
      idle-timeout: 600000
      connection-test-query: SELECT 1


Performance Schema：生产级别的监控手段
-- 找出执行耗时最多的 TOP 10 SQL
SELECT
  digest_text,
  count_star AS exec_count,
  ROUND(avg_timer_wait / 1e12, 3) AS avg_sec,
  ROUND(sum_timer_wait / 1e12, 3) AS total_sec,
  sum_rows_examined AS rows_examined
FROM performance_schema.events_statements_summary_by_digest
ORDER BY sum_timer_wait DESC
LIMIT 10;

-- 查看各表的 IO 热度
SELECT object_schema, object_name,
  count_read, count_write
FROM performance_schema.table_io_waits_summary_by_table
ORDER BY count_read + count_write DESC
LIMIT 20;
Lobster AI
sql
1
2
3
4
5
6
7
8
9
10
11
12
13
14
15
16
17
八、分区表：大表的另一种拆分思路
-- 按月范围分区
CREATE TABLE order_log (
    id BIGINT NOT NULL AUTO_INCREMENT,
    user_id INT NOT NULL,
    created_at DATETIME NOT NULL,
    PRIMARY KEY (id, created_at)     -- 分区键必须包含在主键中
) ENGINE=InnoDB
PARTITION BY RANGE (YEAR(created_at) * 100 + MONTH(created_at)) (
    PARTITION p202401 VALUES LESS THAN (202402),
    PARTITION p202402 VALUES LESS THAN (202403),
    PARTITION p202403 VALUES LESS THAN (202404),
    PARTITION p_future VALUES LESS THAN MAXVALUE
);

-- 按月归档：直接 DROP 分区，比 DELETE 快百倍
ALTER TABLE order_log DROP PARTITION p202401;

✅ 删除分区数据比 DELETE 快几个数量级，因为它直接删除数据文件，无需逐行操作和写 Undo Log。这是日志归档场景的最佳实践。

性能利器
特性	作用	使用场景
隐式索引（Invisible Index）	让索引暂时不被优化器使用	索引上线/下线前灰度验证
降序索引	真正的降序 B+Tree	排行榜、时间线查询
函数索引	对表达式建索引	替代计算列 + 索引的写法
Clone Plugin	在线物理备份	快速扩容读从库