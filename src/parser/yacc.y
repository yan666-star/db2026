%{
#include "ast.h"
#include "yacc.tab.h"
#include <iostream>
#include <memory>

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc);

void yyerror(YYLTYPE *locp, const char* s) {
    std::cerr << "Parser Error at line " << locp->first_line << " column " << locp->first_column << ": " << s << std::endl;
}

using namespace ast;
%}

// request a pure (reentrant) parser
%define api.pure full
// enable location in error handler
%locations
// enable verbose syntax error message
%define parse.error verbose

// keywords
%token SHOW TABLES CREATE TABLE DROP DESC INSERT INTO VALUES DELETE FROM ASC ORDER BY
WHERE UPDATE SET SELECT INT CHAR FLOAT INDEX AND JOIN EXIT HELP TXN_BEGIN TXN_COMMIT TXN_ABORT TXN_ROLLBACK ORDER_BY ENABLE_NESTLOOP ENABLE_SORTMERGE
EXPLAIN ANALYZE ON AS
GROUP HAVING LIMIT COUNT MAX MIN SUM AVG UNION
// non-keywords
%token LEQ NEQ GEQ T_EOF

// type-specific tokens
%token <sv_str> IDENTIFIER VALUE_STRING
%token <sv_int> VALUE_INT
%token <sv_float> VALUE_FLOAT
%token <sv_bool> VALUE_BOOL

// specify types for non-terminal symbol
%type <sv_node> stmt dbStmt ddl dml txnStmt setStmt
%type <sv_field> field
%type <sv_fields> fieldList
%type <sv_type_len> type
%type <sv_comp_op> op
%type <sv_expr> expr
%type <sv_val> value
%type <sv_vals> valueList
%type <sv_str> tbName colName
//new ex analyse andd 原来%type <sv_strs> tableList colNameList
%type <sv_strs> colNameList
%type <sv_table_ref> tableRef
%type <sv_from_clause> tableList

%type <sv_col> col
%type <sv_cols> colList group_by_clause opt_group_by_clause
%type <sv_select_item> select_item
%type <sv_select_items> selector select_list
%type <sv_agg_func> agg_func
%type <sv_having_expr> having_condition
%type <sv_having_exprs> having_clause opt_having_clause
%type <sv_set_clause> setClause
%type <sv_set_clauses> setClauses
%type <sv_cond> condition
%type <sv_conds> whereClause optWhereClause
%type <sv_orderbys> order_clause opt_order_clause
%type <sv_node> union_query union_branch
%type <sv_orderby_dir> opt_asc_desc
%type <sv_setKnobType> set_knob_type
%type <sv_int> opt_limit_clause

%%
start:
        stmt ';'
    {
        parse_tree = $1;
        YYACCEPT;
    }
    |   HELP
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
    |   EXIT
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
    |   T_EOF
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
    ;

stmt:
        dbStmt
    |   ddl
    |   dml
    |   txnStmt
    |   setStmt
    ;

txnStmt:
        TXN_BEGIN
    {
        $$ = std::make_shared<TxnBegin>();
    }
    |   TXN_COMMIT
    {
        $$ = std::make_shared<TxnCommit>();
    }
    |   TXN_ABORT
    {
        $$ = std::make_shared<TxnAbort>();
    }
    | TXN_ROLLBACK
    {
        $$ = std::make_shared<TxnRollback>();
    }
    ;

dbStmt:
        SHOW TABLES
    {
        $$ = std::make_shared<ShowTables>();
    }
    |   SHOW INDEX FROM tbName
    {
        $$ = std::make_shared<ShowIndex>($4);
    }
    ;

setStmt:
        SET set_knob_type '=' VALUE_BOOL
    {
        $$ = std::make_shared<SetStmt>($2, $4);
    }
    ;

ddl:
        CREATE TABLE tbName '(' fieldList ')'
    {
        $$ = std::make_shared<CreateTable>($3, $5);
    }
    |   DROP TABLE tbName
    {
        $$ = std::make_shared<DropTable>($3);
    }
    |   DESC tbName
    {
        $$ = std::make_shared<DescTable>($2);
    }
    |   CREATE INDEX tbName '(' colNameList ')'
    {
        $$ = std::make_shared<CreateIndex>($3, $5);
    }
    |   DROP INDEX tbName '(' colNameList ')'
    {
        $$ = std::make_shared<DropIndex>($3, $5);
    }
    ;

dml:
        INSERT INTO tbName VALUES '(' valueList ')'
    {
        $$ = std::make_shared<InsertStmt>($3, $6);
    }
    |   DELETE FROM tbName optWhereClause
    {
        $$ = std::make_shared<DeleteStmt>($3, $4);
    }
    |   UPDATE tbName SET setClauses optWhereClause
    {
        $$ = std::make_shared<UpdateStmt>($2, $4, $5);
    }
    /* 此处为 explain analyze 的辅助扩展 */
    |   SELECT selector FROM tableList optWhereClause opt_group_by_clause opt_having_clause opt_order_clause opt_limit_clause
    {
        auto conds = $4.conds;
        conds.insert(conds.end(), $5.begin(), $5.end());
        std::shared_ptr<OrderBy> first_order = $8.empty() ? nullptr : $8[0];
        auto stmt = std::make_shared<SelectStmt>($2, $4.tables, conds, $6, $7, first_order, $9);
        stmt->orders = std::move($8);
        stmt->has_sort = !stmt->orders.empty();
        $$ = stmt;
    }   
    |   EXPLAIN ANALYZE SELECT selector FROM tableList optWhereClause opt_group_by_clause opt_having_clause opt_order_clause opt_limit_clause
    {
        auto conds = $6.conds;
        conds.insert(conds.end(), $7.begin(), $7.end());
        std::shared_ptr<OrderBy> first_order = $10.empty() ? nullptr : $10[0];
        auto stmt = std::make_shared<SelectStmt>($4, $6.tables, conds, $8, $9, first_order, $11);
        stmt->orders = std::move($10);
        stmt->has_sort = !stmt->orders.empty();
        stmt->is_explain_analyze = true;
        $$ = stmt;
    }
    ;

fieldList:
        field
    {
        $$ = std::vector<std::shared_ptr<Field>>{$1};
    }
    |   fieldList ',' field
    {
        $$.push_back($3);
    }
    ;

colNameList:
        colName
    {
        $$ = std::vector<std::string>{$1};
    }
    | colNameList ',' colName
    {
        $$.push_back($3);
    }
    ;

field:
        colName type
    {
        $$ = std::make_shared<ColDef>($1, $2);
    }
    ;

type:
        INT
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
    |   CHAR '(' VALUE_INT ')'
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_STRING, $3);
    }
    |   FLOAT
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
    ;

valueList:
        value
    {
        $$ = std::vector<std::shared_ptr<Value>>{$1};
    }
    |   valueList ',' value
    {
        $$.push_back($3);
    }
    ;

value:
        VALUE_INT
    {
        $$ = std::make_shared<IntLit>($1);
    }
    |   VALUE_FLOAT
    {
        $$ = std::make_shared<FloatLit>($1);
    }
    |   VALUE_STRING
    {
        $$ = std::make_shared<StringLit>($1);
    }
    |   VALUE_BOOL
    {
        $$ = std::make_shared<BoolLit>($1);
    }
    ;

condition:
        expr op expr
    {
        $$ = std::make_shared<BinaryExpr>($1, $2, $3);
    }
    ;

optWhereClause:
        /* epsilon */ { /* ignore*/ }
    |   WHERE whereClause
    {
        $$ = $2;
    }
    ;

whereClause:
        condition 
    {
        $$ = std::vector<std::shared_ptr<BinaryExpr>>{$1};
    }
    |   whereClause AND condition
    {
        $$ = $1;
        $$.push_back($3);
    }
    ;

col:
        tbName '.' colName
    {
        $$ = std::make_shared<Col>($1, $3);
    }
    |   colName
    {
        $$ = std::make_shared<Col>("", $1);
    }
    ;

colList:
        col
    {
        $$ = std::vector<std::shared_ptr<Col>>{$1};
    }
    |   colList ',' col
    {
        $$.push_back($3);
    }
    ;

op:
        '='
    {
        $$ = SV_OP_EQ;
    }
    |   '<'
    {
        $$ = SV_OP_LT;
    }
    |   '>'
    {
        $$ = SV_OP_GT;
    }
    |   NEQ
    {
        $$ = SV_OP_NE;
    }
    |   LEQ
    {
        $$ = SV_OP_LE;
    }
    |   GEQ
    {
        $$ = SV_OP_GE;
    }
    ;

expr:
        value
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    |   col
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    |   agg_func
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    ;

setClauses:
        setClause
    {
        $$ = std::vector<std::shared_ptr<SetClause>>{$1};
    }
    |   setClauses ',' setClause
    {
        $$.push_back($3);
    }
    ;

setClause:
        colName '=' value
    {
        $$ = std::make_shared<SetClause>($1, $3);
    }
    ;

selector:
        '*'
    {
        $$ = {};
    }
    |   select_list
    ;

select_list:
        select_item
    {
        $$ = std::vector<std::shared_ptr<SelectItem>>{$1};
    }
    |   select_list ',' select_item
    {
        $$.push_back($3);
    }
    ;

select_item:
        col
    {
        $$ = std::make_shared<SelectItem>($1, "");
    }
    |   col AS colName
    {
        $$ = std::make_shared<SelectItem>($1, $3);
    }
    |   agg_func
    {
        $$ = std::make_shared<SelectItem>($1, "");
    }
    |   agg_func AS colName
    {
        $$ = std::make_shared<SelectItem>($1, $3);
    }
    ;

agg_func:
      COUNT '(' '*' ')'        { $$ = std::make_shared<AggFunc>(AGG_COUNT, true, nullptr); }
    | COUNT '(' col ')'        { $$ = std::make_shared<AggFunc>(AGG_COUNT, false, $3); }
    | MAX '(' col ')'          { $$ = std::make_shared<AggFunc>(AGG_MAX, false, $3); }
    | MIN '(' col ')'          { $$ = std::make_shared<AggFunc>(AGG_MIN, false, $3); }
    | SUM '(' col ')'          { $$ = std::make_shared<AggFunc>(AGG_SUM, false, $3); }
    | AVG '(' col ')'          { $$ = std::make_shared<AggFunc>(AGG_AVG, false, $3); }
    ;
// ex an改动原tablelist
union_branch:
        SELECT selector FROM tableList optWhereClause opt_group_by_clause opt_having_clause
    {
        auto conds = $4.conds;
        conds.insert(conds.end(), $5.begin(), $5.end());
        $$ = std::make_shared<SelectStmt>($2, $4.tables, conds, $6, $7, nullptr, -1);
    }
    ;

union_query:
        union_branch
    {
        auto u = std::make_shared<UnionStmt>();
        u->branches.push_back(std::dynamic_pointer_cast<SelectStmt>($1));
        $$ = u;
    }
    |   union_query UNION union_branch
    {
        auto u = std::dynamic_pointer_cast<UnionStmt>($1);
        u->branches.push_back(std::dynamic_pointer_cast<SelectStmt>($3));
        $$ = u;
    }
    ;

tableRef:
        tbName
    {
        $$ = TableRef($1, "");
    }
    |   tbName tbName
    {
        $$ = TableRef($1, $2);
    }
    |   tbName AS tbName
    {
        $$ = TableRef($1, $3);
    }
    |   '(' union_query ')' AS tbName
    {
        TableRef ref;
        ref.is_subquery = true;
        ref.alias = $5;
        ref.union_subquery = std::dynamic_pointer_cast<UnionStmt>($2);
        $$ = ref;
    }
    |   '(' union_query ')' tbName
    {
        TableRef ref;
        ref.is_subquery = true;
        ref.alias = $4;
        ref.union_subquery = std::dynamic_pointer_cast<UnionStmt>($2);
        $$ = ref;
    }
    ;

tableList:
        tableRef
    {
        $$.tables = {$1};
        $$.conds = {};
    }
    |   tableList ',' tableRef
    {
        $1.tables.push_back($3);
        $$ = $1;
    }
     |   tableList JOIN tableRef
    {
        $1.tables.push_back($3);
        $$ = $1;
    }
    |   tableList JOIN tableRef ON condition
    {
        $1.tables.push_back($3);
        $1.conds.push_back($5);
        $$ = $1;
    }
    ;

opt_group_by_clause:
      /* epsilon */ { $$ = {}; }
    | GROUP BY group_by_clause { $$ = $3; }
    ;

group_by_clause:
      col { $$ = std::vector<std::shared_ptr<Col>>{$1}; }
    | group_by_clause ',' col { $$.push_back($3); }
    ;

opt_having_clause:
      /* epsilon */ { $$ = {}; }
    | HAVING having_clause { $$ = $2; }
    ;

having_clause:
      having_condition { $$ = std::vector<std::shared_ptr<HavingExpr>>{$1}; }
    | having_clause AND having_condition { $$.push_back($3); }
    ;

having_condition:
      agg_func op value { $$ = std::make_shared<HavingExpr>($1, $2, $3); }
    ;

opt_order_clause:
    ORDER BY order_clause
    {
        $$ = $3;
    }
    |   /* epsilon */ { $$ = {}; }
    ;

order_clause:
      col opt_asc_desc
    {
        $$ = {std::make_shared<OrderBy>($1, $2)};
    }
    | order_clause ',' col opt_asc_desc
    {
        $1.push_back(std::make_shared<OrderBy>($3, $4));
        $$ = $1;
    }
    ;   

opt_asc_desc:
    ASC          { $$ = OrderBy_ASC;     }
    |  DESC      { $$ = OrderBy_DESC;    }
    |       { $$ = OrderBy_DEFAULT; }
    ;    

opt_limit_clause:
      /* epsilon */ { $$ = -1; }
    | LIMIT VALUE_INT { $$ = $2; }
    ;

set_knob_type:
    ENABLE_NESTLOOP { $$ = EnableNestLoop; }
    |   ENABLE_SORTMERGE { $$ = EnableSortMerge; }
    ;

tbName: IDENTIFIER;

colName: IDENTIFIER;
%%
