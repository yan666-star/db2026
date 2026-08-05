/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */
#undef NDEBUG

#include <cassert>

#include "parser.h"

int main() {
    std::vector<std::string> sqls = {
        "show tables;",
        "create static_checkpoint;",
        "set transaction isolation level snapshot isolation;",
        "set transaction isolation level serializable;",
        "desc tb;",
        "create table tb (a int, b float, c char(4));",
        "drop table tb;",
        "create index tb(a);",
        "create index tb(a, b, c);",
        "drop index tb(a, b, c);",
        "drop index tb(b);",
        "insert into tb values (1, 3.14, 'pi');",
        "insert into tb values ($1, $2, '$3');",
        "delete from tb where a = 1;",
        "delete from tb where a = $1;",
        "update tb set a = 1, b = 2.2, c = 'xyz' where x = 2 and y < 1.1 and z > 'abc';",
        "update tb set a = a where a = 1;",
        "update tb set a = a + 1, b = b - 2.2 where a = 1;",
        "update tb set a = a - 1 + 91 where a = 1;",
        "update tb set a=a*2, b=b/2.0 where a=1;",
        "select * from tb;",
        "select * from tb where x = $1 and y = $2;",
        "select * from tb where x <> 2 and y >= 3. and z <= '123' and b < tb.a;",
        "select count(distinct a) from tb;",
        "select count(distinct(a)) from tb;",
        "select x.a, y.b from x, y where x.a = y.b and c = d;",
        "select x.a, y.b from x join y where x.a = y.b and c = d;",
        "select x.a, y.b from x join y on x.a = y.a and x.b = y.b;",
        "select x.a, z.c from x join y on x.a = y.a join z on x.a = z.a;",
        "exit;",
        "help;",
        "",
    };
    for (auto &sql : sqls) {
        std::cout << sql << std::endl;
        YY_BUFFER_STATE buf = yy_scan_string(sql.c_str());
        assert(yyparse() == 0);
        if (ast::parse_tree != nullptr) {
            ast::TreePrinter::print(ast::parse_tree);
            yy_delete_buffer(buf);
            std::cout << std::endl;
        } else {
            std::cout << "exit/EOF" << std::endl;
        }
    }

    YY_BUFFER_STATE self_assignment_buf =
        yy_scan_string("update tb set a = a where a = 1;");
    assert(yyparse() == 0);
    auto update = std::dynamic_pointer_cast<ast::UpdateStmt>(ast::parse_tree);
    assert(update != nullptr);
    assert(update->set_clauses.size() == 1);
    assert(update->set_clauses[0]->col_name == "a");
    assert(update->set_clauses[0]->rhs_col_name == "a");
    assert(update->set_clauses[0]->arithmetic_op == '\0');
    assert(update->set_clauses[0]->val == nullptr);
    yy_delete_buffer(self_assignment_buf);

    YY_BUFFER_STATE chained_arithmetic_buf =
        yy_scan_string("update tb set a = a - 1 + 91 where a = 1;");
    assert(yyparse() == 0);
    update = std::dynamic_pointer_cast<ast::UpdateStmt>(ast::parse_tree);
    assert(update != nullptr);
    assert(update->set_clauses.size() == 1);
    assert(update->set_clauses[0]->arithmetic_terms.size() == 2);
    assert(update->set_clauses[0]->arithmetic_terms[0].op == '-');
    assert(update->set_clauses[0]->arithmetic_terms[1].op == '+');
    auto first_operand = std::dynamic_pointer_cast<ast::IntLit>(
        update->set_clauses[0]->arithmetic_terms[0].val);
    auto second_operand = std::dynamic_pointer_cast<ast::IntLit>(
        update->set_clauses[0]->arithmetic_terms[1].val);
    assert(first_operand != nullptr && first_operand->val == 1);
    assert(second_operand != nullptr && second_operand->val == 91);
    yy_delete_buffer(chained_arithmetic_buf);

    ast::parse_tree.reset();
    return 0;
}
