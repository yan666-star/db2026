/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 2

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"

#include "ast.h"
#include "yacc.tab.h"
#include <iostream>
#include <memory>

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc);

void yyerror(YYLTYPE *locp, const char* s) {
    std::cerr << "Parser Error at line " << locp->first_line << " column " << locp->first_column << ": " << s << std::endl;
}

using namespace ast;

#line 86 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "yacc.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_SHOW = 3,                       /* SHOW  */
  YYSYMBOL_TABLES = 4,                     /* TABLES  */
  YYSYMBOL_CREATE = 5,                     /* CREATE  */
  YYSYMBOL_TABLE = 6,                      /* TABLE  */
  YYSYMBOL_STATIC_CHECKPOINT = 7,          /* STATIC_CHECKPOINT  */
  YYSYMBOL_DROP = 8,                       /* DROP  */
  YYSYMBOL_DESC = 9,                       /* DESC  */
  YYSYMBOL_INSERT = 10,                    /* INSERT  */
  YYSYMBOL_INTO = 11,                      /* INTO  */
  YYSYMBOL_VALUES = 12,                    /* VALUES  */
  YYSYMBOL_DELETE = 13,                    /* DELETE  */
  YYSYMBOL_FROM = 14,                      /* FROM  */
  YYSYMBOL_ASC = 15,                       /* ASC  */
  YYSYMBOL_ORDER = 16,                     /* ORDER  */
  YYSYMBOL_BY = 17,                        /* BY  */
  YYSYMBOL_WHERE = 18,                     /* WHERE  */
  YYSYMBOL_UPDATE = 19,                    /* UPDATE  */
  YYSYMBOL_SET = 20,                       /* SET  */
  YYSYMBOL_SELECT = 21,                    /* SELECT  */
  YYSYMBOL_INT = 22,                       /* INT  */
  YYSYMBOL_CHAR = 23,                      /* CHAR  */
  YYSYMBOL_FLOAT = 24,                     /* FLOAT  */
  YYSYMBOL_INDEX = 25,                     /* INDEX  */
  YYSYMBOL_AND = 26,                       /* AND  */
  YYSYMBOL_JOIN = 27,                      /* JOIN  */
  YYSYMBOL_EXIT = 28,                      /* EXIT  */
  YYSYMBOL_HELP = 29,                      /* HELP  */
  YYSYMBOL_TXN_BEGIN = 30,                 /* TXN_BEGIN  */
  YYSYMBOL_TXN_COMMIT = 31,                /* TXN_COMMIT  */
  YYSYMBOL_TXN_ABORT = 32,                 /* TXN_ABORT  */
  YYSYMBOL_TXN_ROLLBACK = 33,              /* TXN_ROLLBACK  */
  YYSYMBOL_ORDER_BY = 34,                  /* ORDER_BY  */
  YYSYMBOL_ENABLE_NESTLOOP = 35,           /* ENABLE_NESTLOOP  */
  YYSYMBOL_ENABLE_SORTMERGE = 36,          /* ENABLE_SORTMERGE  */
  YYSYMBOL_EXPLAIN = 37,                   /* EXPLAIN  */
  YYSYMBOL_ANALYZE = 38,                   /* ANALYZE  */
  YYSYMBOL_ON = 39,                        /* ON  */
  YYSYMBOL_AS = 40,                        /* AS  */
  YYSYMBOL_GROUP = 41,                     /* GROUP  */
  YYSYMBOL_HAVING = 42,                    /* HAVING  */
  YYSYMBOL_LIMIT = 43,                     /* LIMIT  */
  YYSYMBOL_COUNT = 44,                     /* COUNT  */
  YYSYMBOL_DISTINCT = 45,                  /* DISTINCT  */
  YYSYMBOL_MAX = 46,                       /* MAX  */
  YYSYMBOL_MIN = 47,                       /* MIN  */
  YYSYMBOL_SUM = 48,                       /* SUM  */
  YYSYMBOL_AVG = 49,                       /* AVG  */
  YYSYMBOL_UNION = 50,                     /* UNION  */
  YYSYMBOL_SET_TXN_SNAPSHOT = 51,          /* SET_TXN_SNAPSHOT  */
  YYSYMBOL_SET_TXN_SERIALIZABLE = 52,      /* SET_TXN_SERIALIZABLE  */
  YYSYMBOL_LEQ = 53,                       /* LEQ  */
  YYSYMBOL_NEQ = 54,                       /* NEQ  */
  YYSYMBOL_GEQ = 55,                       /* GEQ  */
  YYSYMBOL_T_EOF = 56,                     /* T_EOF  */
  YYSYMBOL_IDENTIFIER = 57,                /* IDENTIFIER  */
  YYSYMBOL_VALUE_STRING = 58,              /* VALUE_STRING  */
  YYSYMBOL_VALUE_INT = 59,                 /* VALUE_INT  */
  YYSYMBOL_PARAMETER = 60,                 /* PARAMETER  */
  YYSYMBOL_VALUE_FLOAT = 61,               /* VALUE_FLOAT  */
  YYSYMBOL_VALUE_BOOL = 62,                /* VALUE_BOOL  */
  YYSYMBOL_63_ = 63,                       /* ';'  */
  YYSYMBOL_64_ = 64,                       /* '='  */
  YYSYMBOL_65_ = 65,                       /* '('  */
  YYSYMBOL_66_ = 66,                       /* ')'  */
  YYSYMBOL_67_ = 67,                       /* ','  */
  YYSYMBOL_68_ = 68,                       /* '.'  */
  YYSYMBOL_69_ = 69,                       /* '<'  */
  YYSYMBOL_70_ = 70,                       /* '>'  */
  YYSYMBOL_71_ = 71,                       /* '*'  */
  YYSYMBOL_72_ = 72,                       /* '/'  */
  YYSYMBOL_73_ = 73,                       /* '+'  */
  YYSYMBOL_74_ = 74,                       /* '-'  */
  YYSYMBOL_YYACCEPT = 75,                  /* $accept  */
  YYSYMBOL_start = 76,                     /* start  */
  YYSYMBOL_stmt = 77,                      /* stmt  */
  YYSYMBOL_txnStmt = 78,                   /* txnStmt  */
  YYSYMBOL_dbStmt = 79,                    /* dbStmt  */
  YYSYMBOL_setStmt = 80,                   /* setStmt  */
  YYSYMBOL_ddl = 81,                       /* ddl  */
  YYSYMBOL_dml = 82,                       /* dml  */
  YYSYMBOL_fieldList = 83,                 /* fieldList  */
  YYSYMBOL_colNameList = 84,               /* colNameList  */
  YYSYMBOL_field = 85,                     /* field  */
  YYSYMBOL_type = 86,                      /* type  */
  YYSYMBOL_valueList = 87,                 /* valueList  */
  YYSYMBOL_value = 88,                     /* value  */
  YYSYMBOL_condition = 89,                 /* condition  */
  YYSYMBOL_optWhereClause = 90,            /* optWhereClause  */
  YYSYMBOL_whereClause = 91,               /* whereClause  */
  YYSYMBOL_col = 92,                       /* col  */
  YYSYMBOL_op = 93,                        /* op  */
  YYSYMBOL_expr = 94,                      /* expr  */
  YYSYMBOL_setClauses = 95,                /* setClauses  */
  YYSYMBOL_setClause = 96,                 /* setClause  */
  YYSYMBOL_arithmeticTerms = 97,           /* arithmeticTerms  */
  YYSYMBOL_selector = 98,                  /* selector  */
  YYSYMBOL_select_list = 99,               /* select_list  */
  YYSYMBOL_select_item = 100,              /* select_item  */
  YYSYMBOL_agg_func = 101,                 /* agg_func  */
  YYSYMBOL_select_branch = 102,            /* select_branch  */
  YYSYMBOL_select_stmt = 103,              /* select_stmt  */
  YYSYMBOL_tableRef = 104,                 /* tableRef  */
  YYSYMBOL_tableList = 105,                /* tableList  */
  YYSYMBOL_opt_group_by_clause = 106,      /* opt_group_by_clause  */
  YYSYMBOL_group_by_clause = 107,          /* group_by_clause  */
  YYSYMBOL_opt_having_clause = 108,        /* opt_having_clause  */
  YYSYMBOL_having_clause = 109,            /* having_clause  */
  YYSYMBOL_having_condition = 110,         /* having_condition  */
  YYSYMBOL_opt_order_clause = 111,         /* opt_order_clause  */
  YYSYMBOL_order_clause = 112,             /* order_clause  */
  YYSYMBOL_opt_asc_desc = 113,             /* opt_asc_desc  */
  YYSYMBOL_opt_limit_clause = 114,         /* opt_limit_clause  */
  YYSYMBOL_set_knob_type = 115,            /* set_knob_type  */
  YYSYMBOL_tbName = 116,                   /* tbName  */
  YYSYMBOL_colName = 117                   /* colName  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_uint8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if 1

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* 1 */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
             && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE) \
             + YYSIZEOF (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  59
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   245

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  75
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  43
/* YYNRULES -- Number of rules.  */
#define YYNRULES  123
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  236

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   317


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      65,    66,    71,    73,    67,    74,    68,    72,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    63,
      69,    64,    70,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    73,    73,    78,    83,    88,    96,    97,    98,    99,
     100,   104,   108,   112,   116,   123,   127,   131,   138,   142,
     146,   153,   157,   161,   165,   169,   176,   180,   184,   188,
     198,   212,   216,   223,   227,   234,   241,   245,   249,   256,
     260,   267,   271,   275,   279,   283,   290,   297,   298,   305,
     309,   317,   321,   339,   343,   347,   351,   355,   359,   366,
     370,   374,   381,   385,   392,   396,   400,   404,   408,   412,
     419,   423,   427,   432,   440,   444,   448,   452,   459,   463,
     467,   471,   478,   479,   480,   482,   484,   485,   486,   487,
     491,   500,   504,   530,   534,   538,   542,   550,   561,   566,
     571,   576,   585,   586,   590,   591,   595,   596,   600,   601,
     605,   609,   613,   617,   621,   629,   630,   631,   635,   636,
     640,   641,   644,   646
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if 1
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "SHOW", "TABLES",
  "CREATE", "TABLE", "STATIC_CHECKPOINT", "DROP", "DESC", "INSERT", "INTO",
  "VALUES", "DELETE", "FROM", "ASC", "ORDER", "BY", "WHERE", "UPDATE",
  "SET", "SELECT", "INT", "CHAR", "FLOAT", "INDEX", "AND", "JOIN", "EXIT",
  "HELP", "TXN_BEGIN", "TXN_COMMIT", "TXN_ABORT", "TXN_ROLLBACK",
  "ORDER_BY", "ENABLE_NESTLOOP", "ENABLE_SORTMERGE", "EXPLAIN", "ANALYZE",
  "ON", "AS", "GROUP", "HAVING", "LIMIT", "COUNT", "DISTINCT", "MAX",
  "MIN", "SUM", "AVG", "UNION", "SET_TXN_SNAPSHOT", "SET_TXN_SERIALIZABLE",
  "LEQ", "NEQ", "GEQ", "T_EOF", "IDENTIFIER", "VALUE_STRING", "VALUE_INT",
  "PARAMETER", "VALUE_FLOAT", "VALUE_BOOL", "';'", "'='", "'('", "')'",
  "','", "'.'", "'<'", "'>'", "'*'", "'/'", "'+'", "'-'", "$accept",
  "start", "stmt", "txnStmt", "dbStmt", "setStmt", "ddl", "dml",
  "fieldList", "colNameList", "field", "type", "valueList", "value",
  "condition", "optWhereClause", "whereClause", "col", "op", "expr",
  "setClauses", "setClause", "arithmeticTerms", "selector", "select_list",
  "select_item", "agg_func", "select_branch", "select_stmt", "tableRef",
  "tableList", "opt_group_by_clause", "group_by_clause",
  "opt_having_clause", "having_clause", "having_condition",
  "opt_order_clause", "order_clause", "opt_asc_desc", "opt_limit_clause",
  "set_knob_type", "tbName", "colName", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-114)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-123)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     123,     5,    18,     9,   -37,    34,    51,   -37,   -25,   -30,
    -114,  -114,  -114,  -114,  -114,  -114,    44,  -114,  -114,  -114,
      69,    32,  -114,  -114,  -114,  -114,  -114,  -114,   -10,  -114,
      83,   -37,  -114,   -37,   -37,   -37,  -114,  -114,   -37,   -37,
      90,  -114,  -114,    60,    62,    70,    72,    73,    75,    57,
    -114,    94,   127,    78,  -114,   106,    81,  -114,   137,  -114,
    -114,   142,   137,   122,   -37,   105,   111,  -114,   115,   159,
     155,   131,   132,    -3,   136,   136,   136,   136,   131,    -8,
     143,   131,   131,   -10,   136,  -114,   144,  -114,  -114,   131,
     131,   131,   139,    54,  -114,  -114,   -15,  -114,   141,  -114,
      13,   135,   146,   147,   148,   150,   151,  -114,   137,  -114,
     -14,     4,  -114,  -114,  -114,   122,    49,   152,  -114,   -44,
    -114,    84,   -11,  -114,    -7,   149,  -114,  -114,  -114,  -114,
    -114,  -114,  -114,   189,  -114,   108,  -114,   131,  -114,    31,
     136,   154,  -114,  -114,  -114,  -114,  -114,  -114,     0,    -8,
      -8,   177,   -37,  -114,  -114,  -114,  -114,  -114,   136,  -114,
     131,  -114,   156,  -114,  -114,  -114,   131,  -114,    53,  -114,
      54,  -114,  -114,  -114,  -114,  -114,  -114,    54,  -114,  -114,
     124,   158,  -114,    23,   183,  -114,   208,   184,  -114,    49,
    -114,   168,  -114,  -114,   149,  -114,  -114,   149,   149,   149,
     149,  -114,    48,   162,   -37,  -114,    54,   136,   120,  -114,
    -114,   164,  -114,  -114,  -114,  -114,  -114,   149,   149,  -114,
    -114,   189,  -114,   165,   108,   205,  -114,  -114,  -114,  -114,
     136,   149,   120,  -114,  -114,  -114
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       4,     3,    11,    12,    13,    14,     0,    19,    20,     5,
       0,     0,     9,     6,    10,     7,     8,    91,   112,    15,
       0,     0,    16,     0,     0,     0,   122,    23,     0,     0,
       0,   120,   121,     0,     0,     0,     0,     0,     0,   123,
      74,    78,     0,    75,    76,    80,     0,    52,     0,     1,
       2,     0,     0,   118,     0,     0,     0,    22,     0,     0,
      47,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   112,     0,    92,     0,    29,    17,     0,
       0,     0,     0,     0,    27,   123,    47,    62,     0,    18,
       0,     0,     0,     0,     0,     0,     0,    79,     0,    98,
      47,    93,    77,    81,    51,   118,   117,   111,   119,     0,
      31,     0,     0,    33,     0,     0,    43,    41,    45,    42,
      44,    59,    49,    48,    60,     0,    61,     0,    28,     0,
       0,     0,    82,    83,    86,    87,    88,    89,     0,     0,
       0,   102,     0,    94,    30,   116,   115,   113,     0,    21,
       0,    36,     0,    38,    35,    24,     0,    25,     0,    39,
       0,    57,    56,    58,    53,    54,    55,     0,    63,    64,
      65,     0,    84,     0,   100,    99,     0,   106,    95,   117,
      32,     0,    34,    26,     0,    50,    46,     0,     0,     0,
       0,    66,    67,     0,     0,    97,     0,     0,     0,    90,
     114,     0,    40,    68,    69,    70,    71,     0,     0,    85,
      96,   101,   104,   103,     0,   107,   108,    37,    72,    73,
       0,     0,     0,   105,   110,   109
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -114,  -114,  -114,  -114,  -114,  -114,  -114,  -114,  -114,   145,
      74,  -114,  -114,  -113,    63,   -89,    29,    -1,    14,    64,
    -114,   100,  -114,  -114,  -114,   160,    -9,   180,   -57,   -20,
    -114,  -114,  -114,  -114,  -114,     7,   161,  -114,    56,   128,
    -114,    -2,   -43
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,    20,    21,    22,    23,    24,    25,    26,   119,   122,
     120,   164,   168,   131,   132,    94,   133,   134,   177,   135,
      96,    97,   202,    52,    53,    54,   136,    27,    28,   109,
     110,   187,   223,   209,   225,   226,    63,   117,   157,    87,
      43,    56,    57
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      55,    83,    37,    93,    93,    40,    61,   138,    51,    29,
      41,    42,   169,   149,    44,    34,    45,    46,    47,    48,
      36,   151,   159,   160,    31,    32,   179,    49,    98,    65,
      30,    66,    67,    68,    35,   107,    69,    70,   113,   114,
      62,    50,   100,    33,   152,    38,   121,   123,   123,    36,
      62,   148,   137,   150,    49,   165,   166,   108,   155,   167,
     166,    36,    88,   204,   156,    39,   183,   201,   101,    59,
      49,    55,   102,   103,   104,   105,   106,   111,   140,    51,
      36,   212,    58,   116,   213,   214,   215,   216,    95,   126,
     127,   128,   129,   130,    98,    60,   180,    64,    44,   141,
      45,    46,    47,    48,   228,   229,   161,   162,   163,   153,
      71,    49,   126,   127,   128,   129,   130,   121,   234,   193,
     194,   217,   218,   192,    72,  -122,     1,    73,     2,   184,
     185,     3,     4,     5,    78,    74,     6,    75,    76,   181,
      77,    79,     7,     8,     9,    80,    81,   111,   111,    82,
     188,    10,    11,    12,    13,    14,    15,   189,     9,    84,
      16,   171,   172,   173,    44,    86,    45,    46,    47,    48,
      89,    92,   174,    93,    17,    18,    90,   175,   176,    19,
      91,   205,   126,   127,   128,   129,   130,    44,    95,    45,
      46,    47,    48,    49,    99,   197,   198,   199,   200,   224,
      49,   142,   220,   118,   125,   139,   222,   126,   127,   128,
     129,   130,   143,   144,   145,   170,   146,   147,   186,   158,
     182,   191,   206,   224,   203,   207,   208,   211,   219,   233,
     227,   232,   230,   195,   190,   221,   124,   178,   231,   235,
     112,   196,    85,   154,   115,   210
};

static const yytype_uint8 yycheck[] =
{
       9,    58,     4,    18,    18,     7,    16,    96,     9,     4,
      35,    36,   125,    27,    44,     6,    46,    47,    48,    49,
      57,   110,    66,    67,     6,     7,   139,    57,    71,    31,
      25,    33,    34,    35,    25,    78,    38,    39,    81,    82,
      50,    71,    45,    25,    40,    11,    89,    90,    91,    57,
      50,   108,    67,    67,    57,    66,    67,    65,     9,    66,
      67,    57,    64,    40,    15,    14,    66,   180,    71,     0,
      57,    80,    73,    74,    75,    76,    77,    79,    65,    80,
      57,   194,    38,    84,   197,   198,   199,   200,    57,    58,
      59,    60,    61,    62,   137,    63,   139,    14,    44,   100,
      46,    47,    48,    49,   217,   218,    22,    23,    24,   111,
      20,    57,    58,    59,    60,    61,    62,   160,   231,    66,
      67,    73,    74,   166,    64,    68,     3,    65,     5,   149,
     150,     8,     9,    10,    40,    65,    13,    65,    65,   140,
      65,    14,    19,    20,    21,    67,    40,   149,   150,    68,
     152,    28,    29,    30,    31,    32,    33,   158,    21,    17,
      37,    53,    54,    55,    44,    43,    46,    47,    48,    49,
      65,    12,    64,    18,    51,    52,    65,    69,    70,    56,
      65,   183,    58,    59,    60,    61,    62,    44,    57,    46,
      47,    48,    49,    57,    62,    71,    72,    73,    74,   208,
      57,    66,   204,    59,    65,    64,   207,    58,    59,    60,
      61,    62,    66,    66,    66,    26,    66,    66,    41,    67,
      66,    65,    39,   232,    66,    17,    42,    59,    66,   230,
      66,    26,    67,   170,   160,   206,    91,   137,   224,   232,
      80,   177,    62,   115,    83,   189
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     5,     8,     9,    10,    13,    19,    20,    21,
      28,    29,    30,    31,    32,    33,    37,    51,    52,    56,
      76,    77,    78,    79,    80,    81,    82,   102,   103,     4,
      25,     6,     7,    25,     6,    25,    57,   116,    11,    14,
     116,    35,    36,   115,    44,    46,    47,    48,    49,    57,
      71,    92,    98,    99,   100,   101,   116,   117,    38,     0,
      63,    16,    50,   111,    14,   116,   116,   116,   116,   116,
     116,    20,    64,    65,    65,    65,    65,    65,    40,    14,
      67,    40,    68,   103,    17,   102,    43,   114,   116,    65,
      65,    65,    12,    18,    90,    57,    95,    96,   117,    62,
      45,    71,    92,    92,    92,    92,    92,   117,    65,   104,
     105,   116,   100,   117,   117,   111,    92,   112,    59,    83,
      85,   117,    84,   117,    84,    65,    58,    59,    60,    61,
      62,    88,    89,    91,    92,    94,   101,    67,    90,    64,
      65,    92,    66,    66,    66,    66,    66,    66,   103,    27,
      67,    90,    40,   116,   114,     9,    15,   113,    67,    66,
      67,    22,    23,    24,    86,    66,    67,    66,    87,    88,
      26,    53,    54,    55,    64,    69,    70,    93,    96,    88,
     117,    92,    66,    66,   104,   104,    41,   106,   116,    92,
      85,    65,   117,    66,    67,    89,    94,    71,    72,    73,
      74,    88,    97,    66,    40,   116,    39,    17,    42,   108,
     113,    59,    88,    88,    88,    88,    88,    73,    74,    66,
     116,    91,    92,   107,   101,   109,   110,    66,    88,    88,
      67,    93,    26,    92,    88,   110
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    75,    76,    76,    76,    76,    77,    77,    77,    77,
      77,    78,    78,    78,    78,    79,    79,    79,    80,    80,
      80,    81,    81,    81,    81,    81,    82,    82,    82,    82,
      82,    83,    83,    84,    84,    85,    86,    86,    86,    87,
      87,    88,    88,    88,    88,    88,    89,    90,    90,    91,
      91,    92,    92,    93,    93,    93,    93,    93,    93,    94,
      94,    94,    95,    95,    96,    96,    96,    96,    96,    96,
      97,    97,    97,    97,    98,    98,    99,    99,   100,   100,
     100,   100,   101,   101,   101,   101,   101,   101,   101,   101,
     102,   103,   103,   104,   104,   104,   104,   104,   105,   105,
     105,   105,   106,   106,   107,   107,   108,   108,   109,   109,
     110,   111,   111,   112,   112,   113,   113,   113,   114,   114,
     115,   115,   116,   117
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     2,     4,     4,     1,
       1,     6,     3,     2,     6,     6,     7,     4,     5,     3,
       5,     1,     3,     1,     3,     2,     1,     4,     1,     1,
       3,     1,     1,     1,     1,     1,     3,     0,     2,     1,
       3,     3,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     3,     3,     4,     4,     5,     5,
       2,     2,     3,     3,     1,     1,     1,     3,     1,     3,
       1,     3,     4,     4,     5,     7,     4,     4,     4,     4,
       7,     1,     3,     1,     2,     3,     5,     4,     1,     3,
       3,     5,     0,     3,     1,     3,     0,     2,     1,     3,
       3,     3,     0,     2,     4,     1,     1,     0,     0,     2,
       1,     1,     1,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (&yylloc, YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF

/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (0)
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)


/* YYLOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

# ifndef YYLOCATION_PRINT

#  if defined YY_LOCATION_PRINT

   /* Temporary convenience wrapper in case some people defined the
      undocumented and private YY_LOCATION_PRINT macros.  */
#   define YYLOCATION_PRINT(File, Loc)  YY_LOCATION_PRINT(File, *(Loc))

#  elif defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

YY_ATTRIBUTE_UNUSED
static int
yy_location_print_ (FILE *yyo, YYLTYPE const * const yylocp)
{
  int res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += YYFPRINTF (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += YYFPRINTF (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += YYFPRINTF (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += YYFPRINTF (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += YYFPRINTF (yyo, "-%d", end_col);
    }
  return res;
}

#   define YYLOCATION_PRINT  yy_location_print_

    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT(File, Loc)  YYLOCATION_PRINT(File, &(Loc))

#  else

#   define YYLOCATION_PRINT(File, Loc) ((void) 0)
    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT  YYLOCATION_PRINT

#  endif
# endif /* !defined YYLOCATION_PRINT */


# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value, Location); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (yylocationp);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  YYLOCATION_PRINT (yyo, yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yykind, yyvaluep, yylocationp);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)],
                       &(yylsp[(yyi + 1) - (yynrhs)]));
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


/* Context of a parse error.  */
typedef struct
{
  yy_state_t *yyssp;
  yysymbol_kind_t yytoken;
  YYLTYPE *yylloc;
} yypcontext_t;

/* Put in YYARG at most YYARGN of the expected tokens given the
   current YYCTX, and return the number of tokens stored in YYARG.  If
   YYARG is null, return the number of expected tokens (guaranteed to
   be less than YYNTOKENS).  Return YYENOMEM on memory exhaustion.
   Return 0 if there are more than YYARGN expected tokens, yet fill
   YYARG up to YYARGN. */
static int
yypcontext_expected_tokens (const yypcontext_t *yyctx,
                            yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  int yyn = yypact[+*yyctx->yyssp];
  if (!yypact_value_is_default (yyn))
    {
      /* Start YYX at -YYN if negative to avoid negative indexes in
         YYCHECK.  In other words, skip the first -YYN actions for
         this state because they are default actions.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;
      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yyx;
      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
        if (yycheck[yyx + yyn] == yyx && yyx != YYSYMBOL_YYerror
            && !yytable_value_is_error (yytable[yyx + yyn]))
          {
            if (!yyarg)
              ++yycount;
            else if (yycount == yyargn)
              return 0;
            else
              yyarg[yycount++] = YY_CAST (yysymbol_kind_t, yyx);
          }
    }
  if (yyarg && yycount == 0 && 0 < yyargn)
    yyarg[0] = YYSYMBOL_YYEMPTY;
  return yycount;
}




#ifndef yystrlen
# if defined __GLIBC__ && defined _STRING_H
#  define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
# else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
# endif
#endif

#ifndef yystpcpy
# if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#  define yystpcpy stpcpy
# else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
# endif
#endif

#ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
      char const *yyp = yystr;
      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
#endif


static int
yy_syntax_error_arguments (const yypcontext_t *yyctx,
                           yysymbol_kind_t yyarg[], int yyargn)
{
  /* Actual size of YYARG. */
  int yycount = 0;
  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yyctx->yytoken != YYSYMBOL_YYEMPTY)
    {
      int yyn;
      if (yyarg)
        yyarg[yycount] = yyctx->yytoken;
      ++yycount;
      yyn = yypcontext_expected_tokens (yyctx,
                                        yyarg ? yyarg + 1 : yyarg, yyargn - 1);
      if (yyn == YYENOMEM)
        return YYENOMEM;
      else
        yycount += yyn;
    }
  return yycount;
}

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return -1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return YYENOMEM if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                const yypcontext_t *yyctx)
{
  enum { YYARGS_MAX = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  yysymbol_kind_t yyarg[YYARGS_MAX];
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* Actual size of YYARG. */
  int yycount = yy_syntax_error_arguments (yyctx, yyarg, YYARGS_MAX);
  if (yycount == YYENOMEM)
    return YYENOMEM;

  switch (yycount)
    {
#define YYCASE_(N, S)                       \
      case N:                               \
        yyformat = S;                       \
        break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
    }

  /* Compute error message size.  Don't count the "%s"s, but reserve
     room for the terminator.  */
  yysize = yystrlen (yyformat) - 2 * yycount + 1;
  {
    int yyi;
    for (yyi = 0; yyi < yycount; ++yyi)
      {
        YYPTRDIFF_T yysize1
          = yysize + yytnamerr (YY_NULLPTR, yytname[yyarg[yyi]]);
        if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
          yysize = yysize1;
        else
          return YYENOMEM;
      }
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return -1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yytname[yyarg[yyi++]]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
{
  YY_USE (yyvaluep);
  YY_USE (yylocationp);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}






/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
/* Lookahead token kind.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

/* Location data for the lookahead symbol.  */
static YYLTYPE yyloc_default
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
  = { 1, 1, 1, 1 }
# endif
;
YYLTYPE yylloc = yyloc_default;

    /* Number of syntax errors so far.  */
    int yynerrs = 0;

    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

    /* The location stack: array, bottom, top.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls = yylsa;
    YYLTYPE *yylsp = yyls;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[3];

  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  yylsp[0] = yylloc;
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;
        YYLTYPE *yyls1 = yyls;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yyls1, yysize * YYSIZEOF (*yylsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
        yyls = yyls1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex (&yylval, &yylloc);
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      yyerror_range[1] = yylloc;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END
  *++yylsp = yylloc;

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  yyerror_range[1] = yyloc;
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* start: stmt ';'  */
#line 74 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 1759 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 3: /* start: HELP  */
#line 79 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 1768 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 4: /* start: EXIT  */
#line 84 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1777 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 5: /* start: T_EOF  */
#line 89 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1786 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 11: /* txnStmt: TXN_BEGIN  */
#line 105 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 1794 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 12: /* txnStmt: TXN_COMMIT  */
#line 109 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 1802 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 13: /* txnStmt: TXN_ABORT  */
#line 113 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 1810 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 14: /* txnStmt: TXN_ROLLBACK  */
#line 117 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 1818 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 15: /* dbStmt: SHOW TABLES  */
#line 124 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 1826 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 16: /* dbStmt: CREATE STATIC_CHECKPOINT  */
#line 128 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<StaticCheckpoint>();
    }
#line 1834 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 17: /* dbStmt: SHOW INDEX FROM tbName  */
#line 132 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowIndex>((yyvsp[0].sv_str));
    }
#line 1842 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 18: /* setStmt: SET set_knob_type '=' VALUE_BOOL  */
#line 139 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 1850 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 19: /* setStmt: SET_TXN_SNAPSHOT  */
#line 143 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetTransactionIsolation>(SnapshotIsolation);
    }
#line 1858 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 20: /* setStmt: SET_TXN_SERIALIZABLE  */
#line 147 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetTransactionIsolation>(Serializable);
    }
#line 1866 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 21: /* ddl: CREATE TABLE tbName '(' fieldList ')'  */
#line 154 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 1874 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 22: /* ddl: DROP TABLE tbName  */
#line 158 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 1882 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 23: /* ddl: DESC tbName  */
#line 162 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 1890 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 24: /* ddl: CREATE INDEX tbName '(' colNameList ')'  */
#line 166 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1898 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 25: /* ddl: DROP INDEX tbName '(' colNameList ')'  */
#line 170 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1906 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 26: /* dml: INSERT INTO tbName VALUES '(' valueList ')'  */
#line 177 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-4].sv_str), (yyvsp[-1].sv_vals));
    }
#line 1914 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 27: /* dml: DELETE FROM tbName optWhereClause  */
#line 181 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_conds));
    }
#line 1922 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 28: /* dml: UPDATE tbName SET setClauses optWhereClause  */
#line 185 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_conds));
    }
#line 1930 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 29: /* dml: select_stmt opt_order_clause opt_limit_clause  */
#line 189 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        auto stmt = std::dynamic_pointer_cast<SelectStmt>((yyvsp[-2].sv_node));
        stmt->orders = std::move((yyvsp[-1].sv_orderbys));
        stmt->has_sort = !stmt->orders.empty();
        stmt->order = stmt->orders.empty() ? nullptr : stmt->orders[0];
        stmt->limit_num = (yyvsp[0].sv_int);
        stmt->has_limit = (yyvsp[0].sv_int) >= 0;
        (yyval.sv_node) = stmt;
    }
#line 1944 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 30: /* dml: EXPLAIN ANALYZE select_stmt opt_order_clause opt_limit_clause  */
#line 199 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        auto stmt = std::dynamic_pointer_cast<SelectStmt>((yyvsp[-2].sv_node));
        stmt->is_explain_analyze = true;
        stmt->orders = std::move((yyvsp[-1].sv_orderbys));
        stmt->has_sort = !stmt->orders.empty();
        stmt->order = stmt->orders.empty() ? nullptr : stmt->orders[0];
        stmt->limit_num = (yyvsp[0].sv_int);
        stmt->has_limit = (yyvsp[0].sv_int) >= 0;
        (yyval.sv_node) = stmt;
    }
#line 1959 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 31: /* fieldList: field  */
#line 213 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 1967 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 32: /* fieldList: fieldList ',' field  */
#line 217 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 1975 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 33: /* colNameList: colName  */
#line 224 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 1983 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 34: /* colNameList: colNameList ',' colName  */
#line 228 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 1991 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 35: /* field: colName type  */
#line 235 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 1999 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 36: /* type: INT  */
#line 242 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 2007 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 37: /* type: CHAR '(' VALUE_INT ')'  */
#line 246 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 2015 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 38: /* type: FLOAT  */
#line 250 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 2023 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 39: /* valueList: value  */
#line 257 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2031 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 40: /* valueList: valueList ',' value  */
#line 261 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2039 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 41: /* value: VALUE_INT  */
#line 268 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 2047 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 42: /* value: VALUE_FLOAT  */
#line 272 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 2055 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 43: /* value: VALUE_STRING  */
#line 276 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 2063 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 44: /* value: VALUE_BOOL  */
#line 280 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 2071 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 45: /* value: PARAMETER  */
#line 284 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<ParamRef>(static_cast<uint16_t>((yyvsp[0].sv_int)));
    }
#line 2079 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 46: /* condition: expr op expr  */
#line 291 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_expr), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2087 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 47: /* optWhereClause: %empty  */
#line 297 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                      { /* ignore*/ }
#line 2093 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 48: /* optWhereClause: WHERE whereClause  */
#line 299 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 2101 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 49: /* whereClause: condition  */
#line 306 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2109 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 50: /* whereClause: whereClause AND condition  */
#line 310 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[-2].sv_conds);
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2118 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 51: /* col: tbName '.' colName  */
#line 318 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2126 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 52: /* col: colName  */
#line 322 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2134 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 53: /* op: '='  */
#line 340 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2142 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 54: /* op: '<'  */
#line 344 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2150 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 55: /* op: '>'  */
#line 348 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2158 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 56: /* op: NEQ  */
#line 352 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2166 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 57: /* op: LEQ  */
#line 356 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2174 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 58: /* op: GEQ  */
#line 360 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2182 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 59: /* expr: value  */
#line 367 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2190 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 60: /* expr: col  */
#line 371 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2198 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 61: /* expr: agg_func  */
#line 375 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_agg_func));
    }
#line 2206 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 62: /* setClauses: setClause  */
#line 382 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2214 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 63: /* setClauses: setClauses ',' setClause  */
#line 386 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2222 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 64: /* setClause: colName '=' value  */
#line 393 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2230 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 65: /* setClause: colName '=' colName  */
#line 397 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2238 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 66: /* setClause: colName '=' colName value  */
#line 401 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-3].sv_str), (yyvsp[-1].sv_str), '+', (yyvsp[0].sv_val));
    }
#line 2246 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 67: /* setClause: colName '=' colName arithmeticTerms  */
#line 405 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-3].sv_str), (yyvsp[-1].sv_str), std::move((yyvsp[0].sv_arithmetic_terms)));
    }
#line 2254 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 68: /* setClause: colName '=' colName '*' value  */
#line 409 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-4].sv_str), (yyvsp[-2].sv_str), '*', (yyvsp[0].sv_val));
    }
#line 2262 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 69: /* setClause: colName '=' colName '/' value  */
#line 413 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-4].sv_str), (yyvsp[-2].sv_str), '/', (yyvsp[0].sv_val));
    }
#line 2270 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 70: /* arithmeticTerms: '+' value  */
#line 420 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_arithmetic_terms) = std::vector<ArithmeticTerm>{{'+', (yyvsp[0].sv_val)}};
    }
#line 2278 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 71: /* arithmeticTerms: '-' value  */
#line 424 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_arithmetic_terms) = std::vector<ArithmeticTerm>{{'-', (yyvsp[0].sv_val)}};
    }
#line 2286 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 72: /* arithmeticTerms: arithmeticTerms '+' value  */
#line 428 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_arithmetic_terms) = std::move((yyvsp[-2].sv_arithmetic_terms));
        (yyval.sv_arithmetic_terms).push_back({'+', (yyvsp[0].sv_val)});
    }
#line 2295 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 73: /* arithmeticTerms: arithmeticTerms '-' value  */
#line 433 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_arithmetic_terms) = std::move((yyvsp[-2].sv_arithmetic_terms));
        (yyval.sv_arithmetic_terms).push_back({'-', (yyvsp[0].sv_val)});
    }
#line 2304 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 74: /* selector: '*'  */
#line 441 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_select_items) = {};
    }
#line 2312 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 76: /* select_list: select_item  */
#line 449 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_select_items) = std::vector<std::shared_ptr<SelectItem>>{(yyvsp[0].sv_select_item)};
    }
#line 2320 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 77: /* select_list: select_list ',' select_item  */
#line 453 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_select_items).push_back((yyvsp[0].sv_select_item));
    }
#line 2328 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 78: /* select_item: col  */
#line 460 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_select_item) = std::make_shared<SelectItem>((yyvsp[0].sv_col), "");
    }
#line 2336 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 79: /* select_item: col AS colName  */
#line 464 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_select_item) = std::make_shared<SelectItem>((yyvsp[-2].sv_col), (yyvsp[0].sv_str));
    }
#line 2344 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 80: /* select_item: agg_func  */
#line 468 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_select_item) = std::make_shared<SelectItem>((yyvsp[0].sv_agg_func), "");
    }
#line 2352 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 81: /* select_item: agg_func AS colName  */
#line 472 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_select_item) = std::make_shared<SelectItem>((yyvsp[-2].sv_agg_func), (yyvsp[0].sv_str));
    }
#line 2360 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 82: /* agg_func: COUNT '(' '*' ')'  */
#line 478 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_COUNT, true, nullptr); }
#line 2366 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 83: /* agg_func: COUNT '(' col ')'  */
#line 479 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_COUNT, false, (yyvsp[-1].sv_col)); }
#line 2372 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 84: /* agg_func: COUNT '(' DISTINCT col ')'  */
#line 481 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                                { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_COUNT, false, (yyvsp[-1].sv_col), true); }
#line 2378 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 85: /* agg_func: COUNT '(' DISTINCT '(' col ')' ')'  */
#line 483 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                                { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_COUNT, false, (yyvsp[-2].sv_col), true); }
#line 2384 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 86: /* agg_func: MAX '(' col ')'  */
#line 484 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_MAX, false, (yyvsp[-1].sv_col)); }
#line 2390 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 87: /* agg_func: MIN '(' col ')'  */
#line 485 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_MIN, false, (yyvsp[-1].sv_col)); }
#line 2396 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 88: /* agg_func: SUM '(' col ')'  */
#line 486 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_SUM, false, (yyvsp[-1].sv_col)); }
#line 2402 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 89: /* agg_func: AVG '(' col ')'  */
#line 487 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_AVG, false, (yyvsp[-1].sv_col)); }
#line 2408 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 90: /* select_branch: SELECT selector FROM tableList optWhereClause opt_group_by_clause opt_having_clause  */
#line 492 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-3].sv_from_clause).conds;
        conds.insert(conds.end(), (yyvsp[-2].sv_conds).begin(), (yyvsp[-2].sv_conds).end());
        (yyval.sv_node) = std::make_shared<SelectStmt>((yyvsp[-5].sv_select_items), (yyvsp[-3].sv_from_clause).tables, conds, (yyvsp[-1].sv_cols), (yyvsp[0].sv_having_exprs), nullptr, -1);
    }
#line 2418 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 91: /* select_stmt: select_branch  */
#line 501 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_node) = (yyvsp[0].sv_node);
    }
#line 2426 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 92: /* select_stmt: select_stmt UNION select_branch  */
#line 505 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        std::vector<std::shared_ptr<SelectStmt>> branches;
        auto left = std::dynamic_pointer_cast<SelectStmt>((yyvsp[-2].sv_node));
        auto right = std::dynamic_pointer_cast<SelectStmt>((yyvsp[0].sv_node));
        if (left->is_union) {
            branches = left->union_branches;
        } else {
            branches.push_back(left);
        }
        branches.push_back(right);
        auto u = std::make_shared<SelectStmt>(
            std::vector<std::shared_ptr<SelectItem>>{},
            std::vector<TableRef>{},
            std::vector<std::shared_ptr<BinaryExpr>>{},
            std::vector<std::shared_ptr<Col>>{},
            std::vector<std::shared_ptr<HavingExpr>>{},
            nullptr,
            -1);
        u->is_union = true;
        u->union_branches = std::move(branches);
        (yyval.sv_node) = u;
    }
#line 2453 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 93: /* tableRef: tbName  */
#line 531 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_table_ref) = TableRef((yyvsp[0].sv_str), "");
    }
#line 2461 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 94: /* tableRef: tbName tbName  */
#line 535 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_table_ref) = TableRef((yyvsp[-1].sv_str), (yyvsp[0].sv_str));
    }
#line 2469 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 95: /* tableRef: tbName AS tbName  */
#line 539 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_table_ref) = TableRef((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2477 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 96: /* tableRef: '(' select_stmt ')' AS tbName  */
#line 543 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        TableRef ref;
        ref.is_subquery = true;
        ref.alias = (yyvsp[0].sv_str);
        ref.subquery = std::dynamic_pointer_cast<SelectStmt>((yyvsp[-3].sv_node));
        (yyval.sv_table_ref) = ref;
    }
#line 2489 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 97: /* tableRef: '(' select_stmt ')' tbName  */
#line 551 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        TableRef ref;
        ref.is_subquery = true;
        ref.alias = (yyvsp[0].sv_str);
        ref.subquery = std::dynamic_pointer_cast<SelectStmt>((yyvsp[-2].sv_node));
        (yyval.sv_table_ref) = ref;
    }
#line 2501 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 98: /* tableList: tableRef  */
#line 562 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_from_clause).tables = {(yyvsp[0].sv_table_ref)};
        (yyval.sv_from_clause).conds = {};
    }
#line 2510 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 99: /* tableList: tableList ',' tableRef  */
#line 567 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_from_clause).tables.push_back((yyvsp[0].sv_table_ref));
        (yyval.sv_from_clause) = (yyvsp[-2].sv_from_clause);
    }
#line 2519 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 100: /* tableList: tableList JOIN tableRef  */
#line 572 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_from_clause).tables.push_back((yyvsp[0].sv_table_ref));
        (yyval.sv_from_clause) = (yyvsp[-2].sv_from_clause);
    }
#line 2528 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 101: /* tableList: tableList JOIN tableRef ON whereClause  */
#line 577 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyvsp[-4].sv_from_clause).tables.push_back((yyvsp[-2].sv_table_ref));
        (yyvsp[-4].sv_from_clause).conds.insert((yyvsp[-4].sv_from_clause).conds.end(), (yyvsp[0].sv_conds).begin(), (yyvsp[0].sv_conds).end());
        (yyval.sv_from_clause) = (yyvsp[-4].sv_from_clause);
    }
#line 2538 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 102: /* opt_group_by_clause: %empty  */
#line 585 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                    { (yyval.sv_cols) = {}; }
#line 2544 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 103: /* opt_group_by_clause: GROUP BY group_by_clause  */
#line 586 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                               { (yyval.sv_cols) = (yyvsp[0].sv_cols); }
#line 2550 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 104: /* group_by_clause: col  */
#line 590 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
          { (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)}; }
#line 2556 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 105: /* group_by_clause: group_by_clause ',' col  */
#line 591 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                              { (yyval.sv_cols).push_back((yyvsp[0].sv_col)); }
#line 2562 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 106: /* opt_having_clause: %empty  */
#line 595 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                    { (yyval.sv_having_exprs) = {}; }
#line 2568 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 107: /* opt_having_clause: HAVING having_clause  */
#line 596 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                           { (yyval.sv_having_exprs) = (yyvsp[0].sv_having_exprs); }
#line 2574 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 108: /* having_clause: having_condition  */
#line 600 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                       { (yyval.sv_having_exprs) = std::vector<std::shared_ptr<HavingExpr>>{(yyvsp[0].sv_having_expr)}; }
#line 2580 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 109: /* having_clause: having_clause AND having_condition  */
#line 601 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                                         { (yyval.sv_having_exprs).push_back((yyvsp[0].sv_having_expr)); }
#line 2586 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 110: /* having_condition: agg_func op value  */
#line 605 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                        { (yyval.sv_having_expr) = std::make_shared<HavingExpr>((yyvsp[-2].sv_agg_func), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_val)); }
#line 2592 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 111: /* opt_order_clause: ORDER BY order_clause  */
#line 610 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = (yyvsp[0].sv_orderbys);
    }
#line 2600 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 112: /* opt_order_clause: %empty  */
#line 613 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                      { (yyval.sv_orderbys) = {}; }
#line 2606 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 113: /* order_clause: col opt_asc_desc  */
#line 618 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = {std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir))};
    }
#line 2614 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 114: /* order_clause: order_clause ',' col opt_asc_desc  */
#line 622 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
    {
        (yyvsp[-3].sv_orderbys).push_back(std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir)));
        (yyval.sv_orderbys) = (yyvsp[-3].sv_orderbys);
    }
#line 2623 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 115: /* opt_asc_desc: ASC  */
#line 629 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 2629 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 116: /* opt_asc_desc: DESC  */
#line 630 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 2635 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 117: /* opt_asc_desc: %empty  */
#line 631 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 2641 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 118: /* opt_limit_clause: %empty  */
#line 635 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                    { (yyval.sv_int) = -1; }
#line 2647 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 119: /* opt_limit_clause: LIMIT VALUE_INT  */
#line 636 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                      { (yyval.sv_int) = (yyvsp[0].sv_int); }
#line 2653 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 120: /* set_knob_type: ENABLE_NESTLOOP  */
#line 640 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 2659 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;

  case 121: /* set_knob_type: ENABLE_SORTMERGE  */
#line 641 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 2665 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"
    break;


#line 2669 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.tab.cpp"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      {
        yypcontext_t yyctx
          = {yyssp, yytoken, &yylloc};
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == -1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *,
                             YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (yymsg)
              {
                yysyntax_error_status
                  = yysyntax_error (&yymsg_alloc, &yymsg, &yyctx);
                yymsgp = yymsg;
              }
            else
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = YYENOMEM;
              }
          }
        yyerror (&yylloc, yymsgp);
        if (yysyntax_error_status == YYENOMEM)
          YYNOMEM;
      }
    }

  yyerror_range[1] = yylloc;
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, &yylloc);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  ++yylsp;
  YYLLOC_DEFAULT (*yylsp, yyerror_range, 2);

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (&yylloc, YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, yylsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
  return yyresult;
}

#line 647 "/mnt/hgfs/db2026/.worktrees/finals-wire-ssi/src/parser/yacc.y"

