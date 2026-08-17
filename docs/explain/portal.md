# `portal.h` 详细讲解

源码入口：[portal.h](../../src/portal.h:1)

这份文件位于 Plan 和 Executor 之间。Planner 已经决定计划树的形状，`portal.h` 负责把每一个 Plan 节点换成对应的 Executor 对象，并把左右子计划递归接成执行器树。

## 一、文件中的三类对象

### 1. `portalTag`

位置：[portal.h](../../src/portal.h:38)

```cpp
typedef enum portalTag {
    PORTAL_Invalid_Query = 0,
    PORTAL_ONE_SELECT,
    PORTAL_EXPLAIN_ANALYZE,
    PORTAL_DML_WITHOUT_SELECT,
    PORTAL_MULTI_QUERY,
    PORTAL_CMD_UTILITY
} portalTag;
```

这个枚举不表示 SQL 的具体语法。它表示 Portal 后面应当选用哪一种运行方式。

例如：

```text
SELECT ...                 -> PORTAL_ONE_SELECT
EXPLAIN ANALYZE SELECT ... -> PORTAL_EXPLAIN_ANALYZE
INSERT/UPDATE/DELETE       -> PORTAL_DML_WITHOUT_SELECT
CREATE TABLE               -> PORTAL_MULTI_QUERY
BEGIN/COMMIT/SET           -> PORTAL_CMD_UTILITY
```

增加 `ANTI JOIN` 或 `SEMI JOIN` 时，SQL 仍然是一条 SELECT，所以不需要新增 portalTag。新 JOIN 的区别保存在 JoinPlan 和 JoinExecutor 中。

### 2. `PortalStmt`

位置：[portal.h](../../src/portal.h:48)

```cpp
struct PortalStmt {
    portalTag tag;
    std::vector<TabCol> sel_cols;
    std::unique_ptr<AbstractExecutor> root;
    std::shared_ptr<Plan> plan;
};
```

`tag`

保存上一节的运行类别。`Portal::run()` 只读它，不再重新分析 plan 的具体类型。

`sel_cols`

保存客户端最终看到的列标题。例如：

```sql
SELECT departments.dept_id, departments.dept_name ...
```

对应两个 `TabCol`。这里没有记录数据，也没有 offset。真实记录由 `root->Next()` 返回。

`root`

保存执行器树的根。类型是 `unique_ptr<AbstractExecutor>`，原因是根对象独占整棵执行器树。根内部再通过自己的 unique_ptr 拥有子执行器。

`plan`

保存原始计划树。DDL 和 Utility 可能没有执行器根，而是由 QlManager 直接读取 plan；EXPLAIN ANALYZE 也需要计划对象中的行数统计。

构造函数中的：

```cpp
root(std::move(root_))
```

表示把传入执行器的所有权交给成员 `root`。传入参数 `root_` 随后为空，这是 unique_ptr 的正常语义。

### 3. `Portal`

位置：[portal.h](../../src/portal.h:57)

```cpp
SmManager *sm_manager_;
```

这是借用指针。`Portal` 不创建也不销毁 SmManager。构造扫描和写执行器时要把这个指针继续传下去，因为这些执行器需要表元数据、记录文件句柄和索引句柄。

## 二、`collect_output_cols()` 的递归过程

位置：[portal.h](../../src/portal.h:60)

函数签名：

```cpp
static std::vector<TabCol> collect_output_cols(
    const std::shared_ptr<Plan> &plan)
```

`plan` 使用 const 引用传递，避免增加/减少 shared_ptr 引用计数，也保证函数不会把调用者的 plan 指针改掉。

这个函数只收集最终输出列名。它不创建执行器，不读取数据。

### ProjectionPlan 分支

```cpp
if (auto p = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
    return p->sel_cols_;
}
```

`dynamic_pointer_cast` 成功时，`p` 指向同一个计划对象，只是静态类型变成 ProjectionPlan。投影计划已经明确列清单，所以直接返回。

### AggregatePlan 分支

聚合输出列可能没有普通表列，例如 `COUNT(*) AS cnt`。代码逐个读取 `select_items_`：

```text
有 alias      -> 输出 alias
无 alias 聚合 -> 临时使用 agg_下标
普通列        -> 使用 item.col
```

这里的下标 `i` 是 SELECT 项位置，不是输入表列下标。

### SortPlan 和 FilterPlan 分支

Sort 与 Filter 不改变记录的列布局，所以继续递归读取 `subplan_`：

```cpp
return collect_output_cols(s->subplan_);
```

新增一个不改变 schema 的一元算子，例如 LimitPlan，也应继续向子计划递归。

### UnionPlan 分支

Union 的各分支已经被统一成 `out_cols_`，函数把每个 `ColMeta` 的表名和列名转换成 `TabCol`。类型、长度和 offset 不属于标题信息，因此不复制。

## 三、`start(plan, context)` 的语句分流

位置：[portal.h](../../src/portal.h:96)

参数：

```text
plan    Optimizer 生成的根计划
context 当前 SQL 的事务、锁、日志和结果缓冲环境
```

返回值是 `shared_ptr<PortalStmt>`。上层需要把它传给 `run()`，所以这里不是执行 SQL，只是准备运行对象。

### SELECT 分支

```cpp
std::unique_ptr<AbstractExecutor> root =
    convert_plan_executor(x->subplan_, context);

std::vector<TabCol> out_cols =
    collect_output_cols(x->subplan_);
```

第一句建立执行器树。第二句建立输出标题。两者处理的是同一棵子计划，但结果不同：一个产生记录，一个描述显示列。

### UPDATE 分支

代码先把 `x->subplan_` 转成扫描执行器：

```cpp
convert_plan_executor(x->subplan_, context, nullptr, true, false)
```

后面三个参数含义：

```text
filter_plan = nullptr          当前没有额外统计借用指针
enable_equality_cache = true   允许等值缓存缩小候选 Rid
track_serializable_reads=false 这是写操作前的目标定位，不按普通 SELECT 路径登记
```

随后循环收集：

```cpp
std::vector<Rid> rids;
for (scan->beginTuple(); !scan->is_end(); scan->nextTuple()) {
    rids.push_back(scan->rid());
}
```

这里不使用 `scan->Next()` 的记录内容，只取当前 `rid()`。UpdateExecutor 得到 rids 后再逐个读取旧记录、计算新值并写回。

DELETE 的流程相同。INSERT 不需要扫描目标行，直接把 values 交给 InsertExecutor。

## 四、`run()` 的执行入口

位置：[portal.h](../../src/portal.h:184)

```cpp
void run(std::shared_ptr<PortalStmt> portal,
         QlManager *ql,
         txn_id_t *txn_id,
         Context *context)
```

`portal`

由 `start()` 返回，内部已经装好 tag、root、plan 和列标题。

`ql`

查询语言执行管理器。Portal 不自己打印行，而是把根执行器交给 QlManager。

`txn_id`

Utility 命令可能开始或结束事务，因此传地址，让函数能够修改会话保存的事务编号。

`context`

当前语句环境，继续传给 QlManager。

SELECT 分支使用：

```cpp
ql->select_from(
    std::move(portal->root),
    std::move(portal->sel_cols),
    context);
```

move 以后 `portal->root` 不再拥有执行器。QlManager 接管根，并按迭代器协议消费结果。

## 五、`convert_plan_executor()` 的递归接线

位置：[portal.h](../../src/portal.h:227)

### 五个参数

```cpp
std::shared_ptr<Plan> plan
Context *context
FilterPlan *filter_plan = nullptr
bool enable_equality_cache = false
bool track_serializable_reads = true
```

`plan`

当前递归节点。每次递归处理一个计划对象。

`context`

最终传到需要访问事务、锁、日志的叶子执行器。

`filter_plan`

借用指针，用于把扫描阶段通过的行数写回某个 FilterPlan。它不拥有计划。

`enable_equality_cache`

控制 SeqScan 是否可以使用记录层整数等值缓存。普通 SELECT 默认 false，Update/Delete 的目标定位可设 true。

`track_serializable_reads`

控制扫描是否登记 SERIALIZABLE 读集合。新增扫描执行器时也要接住这个语义，不能因为换算法而漏登记。

### ProjectionPlan 分支

先递归创建子执行器，再把子执行器、选择列和 `x.get()` 交给 ProjectionExecutor。

`x.get()` 是指向 shared_ptr 所管理对象的裸指针。执行器只借用它统计 `rows_`；计划树在 PortalStmt 中继续存活，所以运行期间不会悬空。

### ScanPlan 分支

```cpp
bool force_seq_scan =
    context->txn_mgr_ != nullptr &&
    context->txn_mgr_->uses_mvcc(context->txn_);
```

即使 Planner 选择 IndexScan，MVCC 模式也可能强制顺序扫描。这一判断说明“计划选择”和“当前事务可安全执行的物理路径”不是完全相同的层次。

### JoinPlan 分支

```cpp
auto left = convert_plan_executor(x->left_, ...);
auto right = convert_plan_executor(x->right_, ...);
```

左右变量此时各自拥有一棵完整子执行器树。构造 Join 时：

```cpp
std::make_unique<NestedLoopJoinExecutor>(
    std::move(left), std::move(right), x->conds_, x.get());
```

两个 move 把子树所有权移入 Join。

增加 ANTI/SEMI 时，接线应位于这个分支：

```text
x->type == INNER_JOIN -> NestedLoopJoinExecutor
x->type == ANTI_JOIN  -> ExtendedJoinExecutor 或 AntiJoinExecutor
x->type == SEMI_JOIN  -> ExtendedJoinExecutor 或 SemiJoinExecutor
```

构造函数参数顺序必须和执行器声明一致。`conds_` 决定匹配，`type` 决定命中后输出哪一侧，二者不能互相替代。

## 六、增加新一元执行器的最小接线模板

假设已有 `LimitPlan` 和 `LimitExecutor`：

```cpp
else if (auto x = std::dynamic_pointer_cast<LimitPlan>(plan)) {
    auto child = convert_plan_executor(
        x->subplan_, context, filter_plan,
        false, track_serializable_reads);

    return std::make_unique<LimitExecutor>(
        std::move(child), x->limit_, x.get());
}
```

逐句状态：

```text
x       借用当前 LimitPlan
child   暂时拥有子执行器树
move    把 child 所有权交给 LimitExecutor
x->limit_ 传递逻辑参数
x.get() 借给执行器统计行数
```

如果 Limit 不改变列布局，`collect_output_cols()` 还要加入递归到 `subplan_` 的分支。

## 七、修改本文件时保持的四个对应关系

```text
Plan 子指针       <-> Executor 子 unique_ptr
Plan 参数字段     <-> Executor 构造参数
Executor cols()   <-> collect_output_cols() 标题
PortalStmt tag    <-> QlManager 运行入口
```

任何一组只改左边不改右边，功能都会在接线处断开。
