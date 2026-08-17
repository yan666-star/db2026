# `lex.l` 详细讲解

源码入口：[lex.l](../../src/parser/lex.l:1)

这个文件是 **Flex 词法分析器源文件**。它不做任何语义判断，只做一件事：把 SQL 字符流切成一个个 token，交给 yacc.y。SQL 是一串字符：

```sql
select * from a semi join b on a.id = b.id;
```

lex.l 把它切成：

```text
SELECT  *  FROM  IDENTIFIER(a)  SEMI  JOIN  IDENTIFIER(b)  ON  IDENTIFIER(a)  .  IDENTIFIER(id)  =  IDENTIFIER(b)  .  IDENTIFIER(id)  ;
```

yacc.y 拿到这串 token 后再判断排列是否合法。

## 一、文件顶部的 Flex 配置项（`%option`）

位置：[lex.l](../../src/parser/lex.l:11)

```lex
%option caseless
%option noyywrap
%option nounput
%option noinput
%option bison-bridge
%option bison-locations
```

逐个解释：

| 配置 | 作用 | 不写会怎样 |
|---|---|---|
| `caseless` | 关键字大小写不敏感，`SELECT`/`select`/`Select` 都能匹配 `"SELECT"` | 只能匹配全大写，`select` 会被当标识符 |
| `noyywrap` | 告诉 flex 不需要 `yywrap()`（多文件续读函数） | 链接时报未定义 `yywrap` |
| `nounput` / `noinput` | 禁止生成 `unput`/`input` 函数（减少编译告警） | 多出无用函数，无实际影响 |
| `bison-bridge` | 让词法动作通过 `yylval`（指针参数）而不是全局变量传递 token 值 | 拿不到 token 的语义值，`sv_int` 等全失效 |
| `bison-locations` | 让 flex 维护 `yylloc`（行列位置） | 报错时没有行号列号 |

关键结论：`caseless` 是"关键字大小写不敏感"的根源，但**标识符 `[a-zA-Z]` 本身大小写敏感**，因此表名 `A` 和 `a` 是两个不同标识符。这与 SQL 关键字不同，表名不自动忽略大小写。

## 二、正则片段定义区（第一个 `%%` 之前）

位置：[lex.l](../../src/parser/lex.l:69)

```lex
alpha        [a-zA-Z]
digit        [0-9]
white_space  [ \t]+
sql_space    [ \t\r\n]+
new_line     "\r"|"\n"|"\r\n"
sign         "+"|"-"
identifier   {alpha}(_|{alpha}|{digit})*
value_int    {sign}?{digit}+
value_float  {sign}?{digit}+\.({digit}+)?
value_string '[^']*'
single_op    ";"|"("|")"|","|"+"|"-"|"*"|"/"|"="|">"|"<"|"."
```

逐个解释：

| 片段 | 匹配内容 | 关键点 |
|---|---|---|
| `alpha` | 单个英文字母 | 大小写都含 |
| `digit` | 单个数字 | 无符号 |
| `identifier` | `字母 (下划线/字母/数字)*` | 标识符**必须以字母开头**，不能以下划线或数字开头 |
| `value_int` | 可带正负号的整数 | `sign?` 表示符号可有可无 |
| `value_float` | 可带正负号、必须有小数点 | `123.` 可匹配（小数部分可有可无） |
| `value_string` | 单引号包裹的非空字符 | `'abc'`；不处理转义，`'a''b'` 会匹配成 `'a'` |
| `single_op` | 单个字符运算符/标点 | `.` 也在其中，用于 `a.id` |
| `sql_space` | 空白（含换行） | 专门给多单词命令用 |

`sql_space` 的用途见下文"多单词命令"一节。

## 三、状态定义

```lex
%x STATE_COMMENT
```

`%x` 定义一个**独占状态** `STATE_COMMENT`。当匹配到 `/*` 时进入注释状态，之后所有规则只在注释状态下生效，直到 `*/` 回到 `INITIAL`（初始状态）。这是块注释的实现方式。

## 四、规则区各段逐条解释

### 4.1 注释与空白

```lex
"/*"                       { BEGIN(STATE_COMMENT); }
<STATE_COMMENT>"*/"        { BEGIN(INITIAL); }
<STATE_COMMENT>[^*]        { /* ignore */ }
<STATE_COMMENT>\*          { /* ignore */ }
"--".*                     { /* ignore single line comment */ }
{white_space}              { /* ignore */ }
{new_line}                 { /* ignore */ }
```

- `"/*"`：进入注释状态，后面到 `*/` 之前的字符全部忽略。
- `<STATE_COMMENT>"*/"`：前缀 `<STATE_COMMENT>` 表示这条规则**只在注释状态下生效**，匹配到 `*/` 后回到初始状态。
- `"--".*`：`--` 开头直到行尾是单行注释。注意 `.*` 不含换行，所以注释到行尾自然结束。
- 空白、换行匹配后**不 return**，直接忽略。flex 里"没有动作返回"意味着这串字符被吞掉，继续扫描下一段。

### 4.2 关键字（DDL/DML/事务/查询子句）

```lex
"SELECT" { return SELECT; }
"JOIN"   {return JOIN;}
```

每条规则形式是：

```text
"字符串"   { 动作 }
```

匹配到引号里的字符串时，执行 `return SELECT;`，把 token 编号 `SELECT` 返回给 yacc.y。token 编号定义在 `yacc.tab.h`（由 yacc.y 生成），lex.l 顶部 `#include "yacc.tab.h"` 就是为拿到这些编号。

### 4.3 多单词命令（含 `sql_space`）

```lex
"SET"{sql_space}"TRANSACTION"{sql_space}"ISOLATION"{sql_space}"LEVEL"{sql_space}"SNAPSHOT"{sql_space}"ISOLATION" { return SET_TXN_SNAPSHOT; }
"SET" { return SET; }
```

`SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION` 是一整串多单词命令。普通 `{white_space}` 用 `[ \t]+`（不含换行），但多单词命令里单词之间可能换行，所以这里用 `{sql_space}`（含 `\r\n`）。

**顺序陷阱**：`"SET"...` 长规则必须写在 `"SET"` 之前。flex 最长匹配会让 `SET TRANSACTION...` 优先于单独 `SET`。但如果 `SET` 写在前面，因为"同起始位置最长优先"的规则，flex 仍会选更长的 `SET TRANSACTION...` 规则——不过为了可读性，仍建议长规则在前。

### 4.4 UNION/ALL/INTERSECT/EXCEPT 的写法

```lex
[Uu][Nn][Ii][Oo][Nn]   { return UNION; }
[Aa][Ll][Ll]            { return ALL; }
[Ii][Nn][Tt][Ee][Rr][Ss][Ee][Cc][Tt] { return INTERSECT; }
[Ee][Xx][Cc][Ee][Pp][Tt] { return EXCEPT; }
```

这里没用 `"UNION"` 而是逐个字符写 `[Uu][Nn]...`。为什么？因为 `%option caseless` 会让 `"UNION"` 也大小写不敏感，理论上 `"UNION"` 就行。逐个字符写是历史遗留 + 兜底写法，效果等价于大小写不敏感匹配。

同时注意后面标识符规则里还有一段 UNION 兜底识别（见 4.6），是"双重保险"，防止 `union` 被当表名。

### 4.5 字面量 token 携带值

```lex
"TRUE"  { yylval->sv_bool = true;  return VALUE_BOOL; }
"FALSE" { yylval->sv_bool = false; return VALUE_BOOL; }
```

关键点：`yylval` 是 `ast::SemValue*`（由 `bison-bridge` 提供），`yylval->sv_bool = true;` 把值塞进语义值，再 `return VALUE_BOOL;`。yacc.y 侧通过 `$1.sv_bool` 读取这个值。

### 4.6 运算符与标识符

```lex
">=" { return GEQ; }
"<=" { return LEQ; }
"<>" { return NEQ; }
{single_op} { return yytext[0]; }
{identifier} {
    std::string ident = yytext;
    if (ident.size() == 5 && /* 是 union */) {
        return UNION;
    }
    yylval->sv_str = yytext;
    return IDENTIFIER;
}
```

- `>=`、`<=`、`<>` 是**双字符**运算符，必须写在 `single_op` 之前，否则 `>`、`<` 会抢走匹配。
- `{single_op}` 匹配单字符运算符，`return yytext[0];` 直接把**字符本身**当 token 编号返回。所以 yacc.y 里 `'+' '-' '(' ')'` 等直接用字符做 token。这就是为什么 `.` 也是 single_op——`a.id` 里的点会返回 `'.'` token。
- `{identifier}` 动作里先判断是不是 `union`（兜底），否则 `yylval->sv_str = yytext;` 把标识符字符串存进语义值，返回 `IDENTIFIER`。

### 4.7 字面量

```lex
{value_int}   { yylval->sv_int = atoi(yytext);    return VALUE_INT; }
{value_float} { yylval->sv_float = atof(yytext);  return VALUE_FLOAT; }
{value_string} { yylval->sv_str = std::string(yytext + 1, strlen(yytext) - 2); return VALUE_STRING; }
```

- 整数：`atoi` 转 int。
- 浮点：`atof` 转 float。
- 字符串：`yytext + 1` 跳过左引号，`strlen(yytext) - 2` 去掉两边的引号，得到纯内容。例如 `'abc'` → `abc`。

### 4.8 EOF 与错误

```lex
<<EOF>> { return T_EOF; }
. { std::cerr << "Lexer Error: unexpected character " << yytext[0] << std::endl; }
```

- `<<EOF>>`：输入结束返回 `T_EOF`，yacc.y 靠它知道解析结束。
- `.`：匹配任何**未被前面规则匹配的单个字符**，打印错误。这是兜底规则，必须放最后。

## 五、`YY_USER_ACTION` 宏（行列号维护）

位置：[lex.l](../../src/parser/lex.l:29)

```cpp
#define YY_USER_ACTION \
    yylloc->first_line = yylloc->last_line; \
    yylloc->first_column = yylloc->last_column; \
    for (int i = 0; yytext[i] != '\0'; i++) { \
        if(yytext[i] == '\n') { yylloc->last_line++; yylloc->last_column = 1; } \
        else { yylloc->last_column++; } \
    }
```

Flex 每匹配一个 token、执行动作**之前**，会先执行这段宏。它遍历 `yytext` 更新 `yylloc`（当前 token 的行列）。这样报错时能报出行号。实现中通常不需要修改它。

## 六、核心变量逐个解释

| 变量 | 归属 | 类型 | 含义 |
|---|---|---|---|
| `yytext` | flex 内置 | `char*` | 当前匹配到的文本（如 `SELECT`、`abc`） |
| `yyleng` | flex 内置 | `int` | `yytext` 的长度 |
| `yylval` | bison-bridge 传入 | `ast::SemValue*` | token 携带的语义值，动作里通过 `yylval->sv_xxx` 写入 |
| `yylloc` | bison-locations 传入 | `YYLTYPE*` | 当前 token 的行列位置 |

## 七、易错点总结

1. **关键字必须在标识符规则前**，否则被吞成 `IDENTIFIER`。
2. **双字符运算符必须在 `single_op` 前**，否则 `>=` 变成 `>` + `=`。
3. **规则必须行首写**，缩进会被当 C 代码。
4. **`caseless` 只管关键字**，表名/列名（标识符）大小写敏感。
5. **lex 声明 token 但 yacc 没声明** → 编译失败；**yacc 声明但 lex 不返回** → 语法永远匹配不到。两边必须闭环。
