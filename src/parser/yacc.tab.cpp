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
#line 1 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"

#include "ast.h"
#include "yacc.tab.h"
#include <iostream>
#include <memory>

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc);

void yyerror(YYLTYPE *locp, const char* s) {
    std::cerr << "Parser Error at line " << locp->first_line << " column " << locp->first_column << ": " << s << std::endl;
}

using namespace ast;

#line 86 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"

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
  YYSYMBOL_MAX = 45,                       /* MAX  */
  YYSYMBOL_MIN = 46,                       /* MIN  */
  YYSYMBOL_SUM = 47,                       /* SUM  */
  YYSYMBOL_AVG = 48,                       /* AVG  */
  YYSYMBOL_UNION = 49,                     /* UNION  */
  YYSYMBOL_SET_TXN_SNAPSHOT = 50,          /* SET_TXN_SNAPSHOT  */
  YYSYMBOL_SET_TXN_SERIALIZABLE = 51,      /* SET_TXN_SERIALIZABLE  */
  YYSYMBOL_LEQ = 52,                       /* LEQ  */
  YYSYMBOL_NEQ = 53,                       /* NEQ  */
  YYSYMBOL_GEQ = 54,                       /* GEQ  */
  YYSYMBOL_T_EOF = 55,                     /* T_EOF  */
  YYSYMBOL_IDENTIFIER = 56,                /* IDENTIFIER  */
  YYSYMBOL_VALUE_STRING = 57,              /* VALUE_STRING  */
  YYSYMBOL_VALUE_INT = 58,                 /* VALUE_INT  */
  YYSYMBOL_VALUE_FLOAT = 59,               /* VALUE_FLOAT  */
  YYSYMBOL_VALUE_BOOL = 60,                /* VALUE_BOOL  */
  YYSYMBOL_61_ = 61,                       /* ';'  */
  YYSYMBOL_62_ = 62,                       /* '='  */
  YYSYMBOL_63_ = 63,                       /* '('  */
  YYSYMBOL_64_ = 64,                       /* ')'  */
  YYSYMBOL_65_ = 65,                       /* ','  */
  YYSYMBOL_66_ = 66,                       /* '.'  */
  YYSYMBOL_67_ = 67,                       /* '<'  */
  YYSYMBOL_68_ = 68,                       /* '>'  */
  YYSYMBOL_69_ = 69,                       /* '*'  */
  YYSYMBOL_YYACCEPT = 70,                  /* $accept  */
  YYSYMBOL_start = 71,                     /* start  */
  YYSYMBOL_stmt = 72,                      /* stmt  */
  YYSYMBOL_txnStmt = 73,                   /* txnStmt  */
  YYSYMBOL_dbStmt = 74,                    /* dbStmt  */
  YYSYMBOL_setStmt = 75,                   /* setStmt  */
  YYSYMBOL_ddl = 76,                       /* ddl  */
  YYSYMBOL_dml = 77,                       /* dml  */
  YYSYMBOL_fieldList = 78,                 /* fieldList  */
  YYSYMBOL_colNameList = 79,               /* colNameList  */
  YYSYMBOL_field = 80,                     /* field  */
  YYSYMBOL_type = 81,                      /* type  */
  YYSYMBOL_valueList = 82,                 /* valueList  */
  YYSYMBOL_value = 83,                     /* value  */
  YYSYMBOL_condition = 84,                 /* condition  */
  YYSYMBOL_optWhereClause = 85,            /* optWhereClause  */
  YYSYMBOL_whereClause = 86,               /* whereClause  */
  YYSYMBOL_col = 87,                       /* col  */
  YYSYMBOL_op = 88,                        /* op  */
  YYSYMBOL_expr = 89,                      /* expr  */
  YYSYMBOL_setClauses = 90,                /* setClauses  */
  YYSYMBOL_setClause = 91,                 /* setClause  */
  YYSYMBOL_selector = 92,                  /* selector  */
  YYSYMBOL_select_list = 93,               /* select_list  */
  YYSYMBOL_select_item = 94,               /* select_item  */
  YYSYMBOL_agg_func = 95,                  /* agg_func  */
  YYSYMBOL_select_branch = 96,             /* select_branch  */
  YYSYMBOL_select_stmt = 97,               /* select_stmt  */
  YYSYMBOL_tableRef = 98,                  /* tableRef  */
  YYSYMBOL_tableList = 99,                 /* tableList  */
  YYSYMBOL_opt_group_by_clause = 100,      /* opt_group_by_clause  */
  YYSYMBOL_group_by_clause = 101,          /* group_by_clause  */
  YYSYMBOL_opt_having_clause = 102,        /* opt_having_clause  */
  YYSYMBOL_having_clause = 103,            /* having_clause  */
  YYSYMBOL_having_condition = 104,         /* having_condition  */
  YYSYMBOL_opt_order_clause = 105,         /* opt_order_clause  */
  YYSYMBOL_order_clause = 106,             /* order_clause  */
  YYSYMBOL_opt_asc_desc = 107,             /* opt_asc_desc  */
  YYSYMBOL_opt_limit_clause = 108,         /* opt_limit_clause  */
  YYSYMBOL_set_knob_type = 109,            /* set_knob_type  */
  YYSYMBOL_tbName = 110,                   /* tbName  */
  YYSYMBOL_colName = 111                   /* colName  */
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
#define YYLAST   212

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  70
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  42
/* YYNRULES -- Number of rules.  */
#define YYNRULES  111
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  213

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   315


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
      63,    64,    69,     2,    65,     2,    66,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    61,
      67,    62,    68,     2,     2,     2,     2,     2,     2,     2,
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
      55,    56,    57,    58,    59,    60
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    72,    72,    77,    82,    87,    95,    96,    97,    98,
      99,   103,   107,   111,   115,   122,   126,   130,   137,   141,
     145,   152,   156,   160,   164,   168,   175,   179,   183,   187,
     197,   211,   215,   222,   226,   233,   240,   244,   248,   255,
     259,   266,   270,   274,   278,   285,   292,   293,   300,   304,
     312,   316,   334,   338,   342,   346,   350,   354,   361,   365,
     369,   376,   380,   387,   394,   398,   402,   406,   413,   417,
     421,   425,   432,   433,   434,   435,   436,   437,   441,   450,
     454,   480,   484,   488,   492,   500,   511,   516,   521,   526,
     535,   536,   540,   541,   545,   546,   550,   551,   555,   559,
     563,   567,   571,   579,   580,   581,   585,   586,   590,   591,
     594,   596
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
  "ON", "AS", "GROUP", "HAVING", "LIMIT", "COUNT", "MAX", "MIN", "SUM",
  "AVG", "UNION", "SET_TXN_SNAPSHOT", "SET_TXN_SERIALIZABLE", "LEQ", "NEQ",
  "GEQ", "T_EOF", "IDENTIFIER", "VALUE_STRING", "VALUE_INT", "VALUE_FLOAT",
  "VALUE_BOOL", "';'", "'='", "'('", "')'", "','", "'.'", "'<'", "'>'",
  "'*'", "$accept", "start", "stmt", "txnStmt", "dbStmt", "setStmt", "ddl",
  "dml", "fieldList", "colNameList", "field", "type", "valueList", "value",
  "condition", "optWhereClause", "whereClause", "col", "op", "expr",
  "setClauses", "setClause", "selector", "select_list", "select_item",
  "agg_func", "select_branch", "select_stmt", "tableRef", "tableList",
  "opt_group_by_clause", "group_by_clause", "opt_having_clause",
  "having_clause", "having_condition", "opt_order_clause", "order_clause",
  "opt_asc_desc", "opt_limit_clause", "set_knob_type", "tbName", "colName", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-119)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-111)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      90,    13,     5,     9,   -38,    47,     6,   -38,    56,     1,
    -119,  -119,  -119,  -119,  -119,  -119,    22,  -119,  -119,  -119,
      64,     7,  -119,  -119,  -119,  -119,  -119,  -119,    -7,  -119,
      75,   -38,  -119,   -38,   -38,   -38,  -119,  -119,   -38,   -38,
      97,  -119,  -119,    16,    70,    71,    79,    89,    92,    88,
    -119,   116,   143,    93,  -119,   119,    94,  -119,   140,  -119,
    -119,   145,   140,   120,   -38,   101,   102,  -119,   103,   155,
     150,   113,   110,    -4,   115,   115,   115,   115,   113,     0,
      68,   113,   113,    -7,   115,  -119,   114,  -119,  -119,   113,
     113,   113,   112,    91,  -119,  -119,   -14,  -119,   111,  -119,
     117,   118,   122,   123,   125,   126,  -119,   140,  -119,   -11,
     -12,  -119,  -119,  -119,   120,    46,   128,  -119,    32,  -119,
      58,    37,  -119,    40,    27,  -119,  -119,  -119,  -119,  -119,
    -119,   151,  -119,   -27,  -119,   113,  -119,    27,  -119,  -119,
    -119,  -119,  -119,  -119,   -25,     0,     0,   135,   -38,  -119,
    -119,  -119,  -119,  -119,   115,  -119,   113,  -119,   121,  -119,
    -119,  -119,   113,  -119,    42,  -119,    91,  -119,  -119,  -119,
    -119,  -119,  -119,    91,  -119,  -119,     3,   139,  -119,   162,
     138,  -119,    46,  -119,   133,  -119,  -119,    27,  -119,  -119,
     -38,  -119,    91,   115,    84,  -119,  -119,   130,  -119,  -119,
     151,  -119,   131,   -27,   157,  -119,  -119,   115,    27,    84,
    -119,  -119,  -119
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       4,     3,    11,    12,    13,    14,     0,    19,    20,     5,
       0,     0,     9,     6,    10,     7,     8,    79,   100,    15,
       0,     0,    16,     0,     0,     0,   110,    23,     0,     0,
       0,   108,   109,     0,     0,     0,     0,     0,     0,   111,
      64,    68,     0,    65,    66,    70,     0,    51,     0,     1,
       2,     0,     0,   106,     0,     0,     0,    22,     0,     0,
      46,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   100,     0,    80,     0,    29,    17,     0,
       0,     0,     0,     0,    27,   111,    46,    61,     0,    18,
       0,     0,     0,     0,     0,     0,    69,     0,    86,    46,
      81,    67,    71,    50,   106,   105,    99,   107,     0,    31,
       0,     0,    33,     0,     0,    43,    41,    42,    44,    58,
      48,    47,    59,     0,    60,     0,    28,     0,    72,    73,
      74,    75,    76,    77,     0,     0,     0,    90,     0,    82,
      30,   104,   103,   101,     0,    21,     0,    36,     0,    38,
      35,    24,     0,    25,     0,    39,     0,    56,    55,    57,
      52,    53,    54,     0,    62,    63,     0,    88,    87,     0,
      94,    83,   105,    32,     0,    34,    26,     0,    49,    45,
       0,    85,     0,     0,     0,    78,   102,     0,    40,    84,
      89,    92,    91,     0,    95,    96,    37,     0,     0,     0,
      93,    98,    97
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -119,  -119,  -119,  -119,  -119,  -119,  -119,  -119,  -119,   104,
      41,  -119,  -119,  -118,    33,   -43,    10,    -1,    -5,    28,
    -119,    69,  -119,  -119,   127,    -9,   141,   -57,   -20,  -119,
    -119,  -119,  -119,  -119,     2,   129,  -119,    23,    95,  -119,
      -2,   -68
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,    20,    21,    22,    23,    24,    25,    26,   118,   121,
     119,   160,   164,   129,   130,    94,   131,   132,   173,   133,
      96,    97,    52,    53,    54,   134,    27,    28,   108,   109,
     180,   202,   195,   204,   205,    63,   116,   153,    87,    43,
      56,    57
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      55,    83,    37,    98,    93,    40,   165,    93,    51,    61,
     106,    31,    32,   112,   113,    34,   145,    29,    36,   175,
      39,   120,   122,   122,    62,   167,   168,   169,   148,    65,
      33,    66,    67,    68,    35,   170,    69,    70,    30,   176,
     171,   172,    62,   190,    36,    44,    45,    46,    47,    48,
     144,   135,    49,   136,   146,   151,    36,    49,    38,    36,
      58,   152,    88,   107,    59,   100,   147,    98,    60,   198,
      50,    55,   101,   102,   103,   104,   105,   110,    72,    51,
     157,   158,   159,   115,   125,   126,   127,   128,   120,    64,
     211,    41,    42,     1,   185,     2,   155,   156,     3,     4,
       5,   161,   162,     6,   163,   162,   186,   187,   149,     7,
       8,     9,    44,    45,    46,    47,    48,    71,    10,    11,
      12,    13,    14,    15,    49,   177,   178,    16,    44,    45,
      46,    47,    48,    73,    74,    44,    45,    46,    47,    48,
      17,    18,    75,   110,   110,    19,   181,    49,   125,   126,
     127,   128,    76,   182,  -110,    77,    78,    79,    80,    81,
      82,     9,    84,    86,    89,    90,    91,    92,    93,    95,
      99,    49,   117,   137,   191,   124,   179,   166,   192,   193,
     194,   138,   139,   209,   184,   203,   140,   141,   199,   142,
     143,   197,   201,   154,   206,   123,   207,   183,   208,   188,
     203,   189,   200,    85,   174,   196,   210,   111,     0,   150,
       0,   212,   114
};

static const yytype_int16 yycheck[] =
{
       9,    58,     4,    71,    18,     7,   124,    18,     9,    16,
      78,     6,     7,    81,    82,     6,    27,     4,    56,   137,
      14,    89,    90,    91,    49,    52,    53,    54,    40,    31,
      25,    33,    34,    35,    25,    62,    38,    39,    25,    64,
      67,    68,    49,    40,    56,    44,    45,    46,    47,    48,
     107,    65,    56,    96,    65,     9,    56,    56,    11,    56,
      38,    15,    64,    63,     0,    69,   109,   135,    61,   187,
      69,    80,    73,    74,    75,    76,    77,    79,    62,    80,
      22,    23,    24,    84,    57,    58,    59,    60,   156,    14,
     208,    35,    36,     3,   162,     5,    64,    65,     8,     9,
      10,    64,    65,    13,    64,    65,    64,    65,   110,    19,
      20,    21,    44,    45,    46,    47,    48,    20,    28,    29,
      30,    31,    32,    33,    56,   145,   146,    37,    44,    45,
      46,    47,    48,    63,    63,    44,    45,    46,    47,    48,
      50,    51,    63,   145,   146,    55,   148,    56,    57,    58,
      59,    60,    63,   154,    66,    63,    40,    14,    65,    40,
      66,    21,    17,    43,    63,    63,    63,    12,    18,    56,
      60,    56,    58,    62,   176,    63,    41,    26,    39,    17,
      42,    64,    64,    26,    63,   194,    64,    64,   190,    64,
      64,    58,   193,    65,    64,    91,    65,   156,   203,   166,
     209,   173,   192,    62,   135,   182,   207,    80,    -1,   114,
      -1,   209,    83
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     5,     8,     9,    10,    13,    19,    20,    21,
      28,    29,    30,    31,    32,    33,    37,    50,    51,    55,
      71,    72,    73,    74,    75,    76,    77,    96,    97,     4,
      25,     6,     7,    25,     6,    25,    56,   110,    11,    14,
     110,    35,    36,   109,    44,    45,    46,    47,    48,    56,
      69,    87,    92,    93,    94,    95,   110,   111,    38,     0,
      61,    16,    49,   105,    14,   110,   110,   110,   110,   110,
     110,    20,    62,    63,    63,    63,    63,    63,    40,    14,
      65,    40,    66,    97,    17,    96,    43,   108,   110,    63,
      63,    63,    12,    18,    85,    56,    90,    91,   111,    60,
      69,    87,    87,    87,    87,    87,   111,    63,    98,    99,
     110,    94,   111,   111,   105,    87,   106,    58,    78,    80,
     111,    79,   111,    79,    63,    57,    58,    59,    60,    83,
      84,    86,    87,    89,    95,    65,    85,    62,    64,    64,
      64,    64,    64,    64,    97,    27,    65,    85,    40,   110,
     108,     9,    15,   107,    65,    64,    65,    22,    23,    24,
      81,    64,    65,    64,    82,    83,    26,    52,    53,    54,
      62,    67,    68,    88,    91,    83,    64,    98,    98,    41,
     100,   110,    87,    80,    63,   111,    64,    65,    84,    89,
      40,   110,    39,    17,    42,   102,   107,    58,    83,   110,
      86,    87,   101,    95,   103,   104,    64,    65,    88,    26,
      87,    83,   104
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    70,    71,    71,    71,    71,    72,    72,    72,    72,
      72,    73,    73,    73,    73,    74,    74,    74,    75,    75,
      75,    76,    76,    76,    76,    76,    77,    77,    77,    77,
      77,    78,    78,    79,    79,    80,    81,    81,    81,    82,
      82,    83,    83,    83,    83,    84,    85,    85,    86,    86,
      87,    87,    88,    88,    88,    88,    88,    88,    89,    89,
      89,    90,    90,    91,    92,    92,    93,    93,    94,    94,
      94,    94,    95,    95,    95,    95,    95,    95,    96,    97,
      97,    98,    98,    98,    98,    98,    99,    99,    99,    99,
     100,   100,   101,   101,   102,   102,   103,   103,   104,   105,
     105,   106,   106,   107,   107,   107,   108,   108,   109,   109,
     110,   111
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     2,     4,     4,     1,
       1,     6,     3,     2,     6,     6,     7,     4,     5,     3,
       5,     1,     3,     1,     3,     2,     1,     4,     1,     1,
       3,     1,     1,     1,     1,     3,     0,     2,     1,     3,
       3,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     3,     1,     1,     1,     3,     1,     3,
       1,     3,     4,     4,     4,     4,     4,     4,     7,     1,
       3,     1,     2,     3,     5,     4,     1,     3,     3,     5,
       0,     3,     1,     3,     0,     2,     1,     3,     3,     3,
       0,     2,     4,     1,     1,     0,     0,     2,     1,     1,
       1,     1
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
#line 73 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 1736 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 3: /* start: HELP  */
#line 78 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 1745 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 4: /* start: EXIT  */
#line 83 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1754 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 5: /* start: T_EOF  */
#line 88 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1763 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 11: /* txnStmt: TXN_BEGIN  */
#line 104 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 1771 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 12: /* txnStmt: TXN_COMMIT  */
#line 108 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 1779 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 13: /* txnStmt: TXN_ABORT  */
#line 112 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 1787 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 14: /* txnStmt: TXN_ROLLBACK  */
#line 116 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 1795 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 15: /* dbStmt: SHOW TABLES  */
#line 123 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 1803 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 16: /* dbStmt: CREATE STATIC_CHECKPOINT  */
#line 127 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<StaticCheckpoint>();
    }
#line 1811 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 17: /* dbStmt: SHOW INDEX FROM tbName  */
#line 131 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowIndex>((yyvsp[0].sv_str));
    }
#line 1819 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 18: /* setStmt: SET set_knob_type '=' VALUE_BOOL  */
#line 138 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 1827 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 19: /* setStmt: SET_TXN_SNAPSHOT  */
#line 142 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetTransactionIsolation>(SnapshotIsolation);
    }
#line 1835 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 20: /* setStmt: SET_TXN_SERIALIZABLE  */
#line 146 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetTransactionIsolation>(Serializable);
    }
#line 1843 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 21: /* ddl: CREATE TABLE tbName '(' fieldList ')'  */
#line 153 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 1851 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 22: /* ddl: DROP TABLE tbName  */
#line 157 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 1859 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 23: /* ddl: DESC tbName  */
#line 161 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 1867 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 24: /* ddl: CREATE INDEX tbName '(' colNameList ')'  */
#line 165 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1875 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 25: /* ddl: DROP INDEX tbName '(' colNameList ')'  */
#line 169 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1883 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 26: /* dml: INSERT INTO tbName VALUES '(' valueList ')'  */
#line 176 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-4].sv_str), (yyvsp[-1].sv_vals));
    }
#line 1891 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 27: /* dml: DELETE FROM tbName optWhereClause  */
#line 180 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_conds));
    }
#line 1899 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 28: /* dml: UPDATE tbName SET setClauses optWhereClause  */
#line 184 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_conds));
    }
#line 1907 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 29: /* dml: select_stmt opt_order_clause opt_limit_clause  */
#line 188 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        auto stmt = std::dynamic_pointer_cast<SelectStmt>((yyvsp[-2].sv_node));
        stmt->orders = std::move((yyvsp[-1].sv_orderbys));
        stmt->has_sort = !stmt->orders.empty();
        stmt->order = stmt->orders.empty() ? nullptr : stmt->orders[0];
        stmt->limit_num = (yyvsp[0].sv_int);
        stmt->has_limit = (yyvsp[0].sv_int) >= 0;
        (yyval.sv_node) = stmt;
    }
#line 1921 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 30: /* dml: EXPLAIN ANALYZE select_stmt opt_order_clause opt_limit_clause  */
#line 198 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
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
#line 1936 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 31: /* fieldList: field  */
#line 212 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 1944 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 32: /* fieldList: fieldList ',' field  */
#line 216 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 1952 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 33: /* colNameList: colName  */
#line 223 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 1960 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 34: /* colNameList: colNameList ',' colName  */
#line 227 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 1968 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 35: /* field: colName type  */
#line 234 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 1976 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 36: /* type: INT  */
#line 241 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 1984 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 37: /* type: CHAR '(' VALUE_INT ')'  */
#line 245 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 1992 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 38: /* type: FLOAT  */
#line 249 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 2000 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 39: /* valueList: value  */
#line 256 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 2008 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 40: /* valueList: valueList ',' value  */
#line 260 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 2016 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 41: /* value: VALUE_INT  */
#line 267 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 2024 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 42: /* value: VALUE_FLOAT  */
#line 271 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 2032 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 43: /* value: VALUE_STRING  */
#line 275 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 2040 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 44: /* value: VALUE_BOOL  */
#line 279 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 2048 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 45: /* condition: expr op expr  */
#line 286 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_expr), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 2056 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 46: /* optWhereClause: %empty  */
#line 292 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                      { /* ignore*/ }
#line 2062 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 47: /* optWhereClause: WHERE whereClause  */
#line 294 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 2070 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 48: /* whereClause: condition  */
#line 301 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 2078 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 49: /* whereClause: whereClause AND condition  */
#line 305 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[-2].sv_conds);
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 2087 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 50: /* col: tbName '.' colName  */
#line 313 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2095 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 51: /* col: colName  */
#line 317 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2103 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 52: /* op: '='  */
#line 335 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2111 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 53: /* op: '<'  */
#line 339 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2119 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 54: /* op: '>'  */
#line 343 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2127 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 55: /* op: NEQ  */
#line 347 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2135 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 56: /* op: LEQ  */
#line 351 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2143 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 57: /* op: GEQ  */
#line 355 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2151 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 58: /* expr: value  */
#line 362 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2159 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 59: /* expr: col  */
#line 366 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2167 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 60: /* expr: agg_func  */
#line 370 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_agg_func));
    }
#line 2175 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 61: /* setClauses: setClause  */
#line 377 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2183 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 62: /* setClauses: setClauses ',' setClause  */
#line 381 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2191 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 63: /* setClause: colName '=' value  */
#line 388 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2199 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 64: /* selector: '*'  */
#line 395 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_select_items) = {};
    }
#line 2207 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 66: /* select_list: select_item  */
#line 403 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_select_items) = std::vector<std::shared_ptr<SelectItem>>{(yyvsp[0].sv_select_item)};
    }
#line 2215 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 67: /* select_list: select_list ',' select_item  */
#line 407 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_select_items).push_back((yyvsp[0].sv_select_item));
    }
#line 2223 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 68: /* select_item: col  */
#line 414 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_select_item) = std::make_shared<SelectItem>((yyvsp[0].sv_col), "");
    }
#line 2231 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 69: /* select_item: col AS colName  */
#line 418 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_select_item) = std::make_shared<SelectItem>((yyvsp[-2].sv_col), (yyvsp[0].sv_str));
    }
#line 2239 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 70: /* select_item: agg_func  */
#line 422 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_select_item) = std::make_shared<SelectItem>((yyvsp[0].sv_agg_func), "");
    }
#line 2247 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 71: /* select_item: agg_func AS colName  */
#line 426 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_select_item) = std::make_shared<SelectItem>((yyvsp[-2].sv_agg_func), (yyvsp[0].sv_str));
    }
#line 2255 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 72: /* agg_func: COUNT '(' '*' ')'  */
#line 432 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_COUNT, true, nullptr); }
#line 2261 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 73: /* agg_func: COUNT '(' col ')'  */
#line 433 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_COUNT, false, (yyvsp[-1].sv_col)); }
#line 2267 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 74: /* agg_func: MAX '(' col ')'  */
#line 434 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_MAX, false, (yyvsp[-1].sv_col)); }
#line 2273 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 75: /* agg_func: MIN '(' col ')'  */
#line 435 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_MIN, false, (yyvsp[-1].sv_col)); }
#line 2279 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 76: /* agg_func: SUM '(' col ')'  */
#line 436 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_SUM, false, (yyvsp[-1].sv_col)); }
#line 2285 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 77: /* agg_func: AVG '(' col ')'  */
#line 437 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                               { (yyval.sv_agg_func) = std::make_shared<AggFunc>(AGG_AVG, false, (yyvsp[-1].sv_col)); }
#line 2291 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 78: /* select_branch: SELECT selector FROM tableList optWhereClause opt_group_by_clause opt_having_clause  */
#line 442 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-3].sv_from_clause).conds;
        conds.insert(conds.end(), (yyvsp[-2].sv_conds).begin(), (yyvsp[-2].sv_conds).end());
        (yyval.sv_node) = std::make_shared<SelectStmt>((yyvsp[-5].sv_select_items), (yyvsp[-3].sv_from_clause).tables, conds, (yyvsp[-1].sv_cols), (yyvsp[0].sv_having_exprs), nullptr, -1);
    }
#line 2301 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 79: /* select_stmt: select_branch  */
#line 451 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = (yyvsp[0].sv_node);
    }
#line 2309 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 80: /* select_stmt: select_stmt UNION select_branch  */
#line 455 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
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
#line 2336 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 81: /* tableRef: tbName  */
#line 481 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_ref) = TableRef((yyvsp[0].sv_str), "");
    }
#line 2344 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 82: /* tableRef: tbName tbName  */
#line 485 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_ref) = TableRef((yyvsp[-1].sv_str), (yyvsp[0].sv_str));
    }
#line 2352 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 83: /* tableRef: tbName AS tbName  */
#line 489 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_ref) = TableRef((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2360 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 84: /* tableRef: '(' select_stmt ')' AS tbName  */
#line 493 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        TableRef ref;
        ref.is_subquery = true;
        ref.alias = (yyvsp[0].sv_str);
        ref.subquery = std::dynamic_pointer_cast<SelectStmt>((yyvsp[-3].sv_node));
        (yyval.sv_table_ref) = ref;
    }
#line 2372 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 85: /* tableRef: '(' select_stmt ')' tbName  */
#line 501 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        TableRef ref;
        ref.is_subquery = true;
        ref.alias = (yyvsp[0].sv_str);
        ref.subquery = std::dynamic_pointer_cast<SelectStmt>((yyvsp[-2].sv_node));
        (yyval.sv_table_ref) = ref;
    }
#line 2384 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 86: /* tableList: tableRef  */
#line 512 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_from_clause).tables = {(yyvsp[0].sv_table_ref)};
        (yyval.sv_from_clause).conds = {};
    }
#line 2393 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 87: /* tableList: tableList ',' tableRef  */
#line 517 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_from_clause).tables.push_back((yyvsp[0].sv_table_ref));
        (yyval.sv_from_clause) = (yyvsp[-2].sv_from_clause);
    }
#line 2402 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 88: /* tableList: tableList JOIN tableRef  */
#line 522 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_from_clause).tables.push_back((yyvsp[0].sv_table_ref));
        (yyval.sv_from_clause) = (yyvsp[-2].sv_from_clause);
    }
#line 2411 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 89: /* tableList: tableList JOIN tableRef ON whereClause  */
#line 527 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyvsp[-4].sv_from_clause).tables.push_back((yyvsp[-2].sv_table_ref));
        (yyvsp[-4].sv_from_clause).conds.insert((yyvsp[-4].sv_from_clause).conds.end(), (yyvsp[0].sv_conds).begin(), (yyvsp[0].sv_conds).end());
        (yyval.sv_from_clause) = (yyvsp[-4].sv_from_clause);
    }
#line 2421 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 90: /* opt_group_by_clause: %empty  */
#line 535 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                    { (yyval.sv_cols) = {}; }
#line 2427 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 91: /* opt_group_by_clause: GROUP BY group_by_clause  */
#line 536 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                               { (yyval.sv_cols) = (yyvsp[0].sv_cols); }
#line 2433 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 92: /* group_by_clause: col  */
#line 540 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
          { (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)}; }
#line 2439 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 93: /* group_by_clause: group_by_clause ',' col  */
#line 541 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                              { (yyval.sv_cols).push_back((yyvsp[0].sv_col)); }
#line 2445 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 94: /* opt_having_clause: %empty  */
#line 545 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                    { (yyval.sv_having_exprs) = {}; }
#line 2451 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 95: /* opt_having_clause: HAVING having_clause  */
#line 546 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                           { (yyval.sv_having_exprs) = (yyvsp[0].sv_having_exprs); }
#line 2457 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 96: /* having_clause: having_condition  */
#line 550 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                       { (yyval.sv_having_exprs) = std::vector<std::shared_ptr<HavingExpr>>{(yyvsp[0].sv_having_expr)}; }
#line 2463 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 97: /* having_clause: having_clause AND having_condition  */
#line 551 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                                         { (yyval.sv_having_exprs).push_back((yyvsp[0].sv_having_expr)); }
#line 2469 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 98: /* having_condition: agg_func op value  */
#line 555 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                        { (yyval.sv_having_expr) = std::make_shared<HavingExpr>((yyvsp[-2].sv_agg_func), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_val)); }
#line 2475 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 99: /* opt_order_clause: ORDER BY order_clause  */
#line 560 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = (yyvsp[0].sv_orderbys);
    }
#line 2483 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 100: /* opt_order_clause: %empty  */
#line 563 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                      { (yyval.sv_orderbys) = {}; }
#line 2489 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 101: /* order_clause: col opt_asc_desc  */
#line 568 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_orderbys) = {std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir))};
    }
#line 2497 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 102: /* order_clause: order_clause ',' col opt_asc_desc  */
#line 572 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyvsp[-3].sv_orderbys).push_back(std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir)));
        (yyval.sv_orderbys) = (yyvsp[-3].sv_orderbys);
    }
#line 2506 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 103: /* opt_asc_desc: ASC  */
#line 579 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 2512 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 104: /* opt_asc_desc: DESC  */
#line 580 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 2518 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 105: /* opt_asc_desc: %empty  */
#line 581 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 2524 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 106: /* opt_limit_clause: %empty  */
#line 585 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                    { (yyval.sv_int) = -1; }
#line 2530 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 107: /* opt_limit_clause: LIMIT VALUE_INT  */
#line 586 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                      { (yyval.sv_int) = (yyvsp[0].sv_int); }
#line 2536 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 108: /* set_knob_type: ENABLE_NESTLOOP  */
#line 590 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 2542 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 109: /* set_knob_type: ENABLE_SORTMERGE  */
#line 591 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 2548 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;


#line 2552 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"

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

#line 597 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"

