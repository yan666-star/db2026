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
  YYSYMBOL_DROP = 7,                       /* DROP  */
  YYSYMBOL_DESC = 8,                       /* DESC  */
  YYSYMBOL_INSERT = 9,                     /* INSERT  */
  YYSYMBOL_INTO = 10,                      /* INTO  */
  YYSYMBOL_VALUES = 11,                    /* VALUES  */
  YYSYMBOL_DELETE = 12,                    /* DELETE  */
  YYSYMBOL_FROM = 13,                      /* FROM  */
  YYSYMBOL_ASC = 14,                       /* ASC  */
  YYSYMBOL_ORDER = 15,                     /* ORDER  */
  YYSYMBOL_BY = 16,                        /* BY  */
  YYSYMBOL_WHERE = 17,                     /* WHERE  */
  YYSYMBOL_UPDATE = 18,                    /* UPDATE  */
  YYSYMBOL_SET = 19,                       /* SET  */
  YYSYMBOL_SELECT = 20,                    /* SELECT  */
  YYSYMBOL_INT = 21,                       /* INT  */
  YYSYMBOL_CHAR = 22,                      /* CHAR  */
  YYSYMBOL_FLOAT = 23,                     /* FLOAT  */
  YYSYMBOL_INDEX = 24,                     /* INDEX  */
  YYSYMBOL_AND = 25,                       /* AND  */
  YYSYMBOL_JOIN = 26,                      /* JOIN  */
  YYSYMBOL_EXIT = 27,                      /* EXIT  */
  YYSYMBOL_HELP = 28,                      /* HELP  */
  YYSYMBOL_TXN_BEGIN = 29,                 /* TXN_BEGIN  */
  YYSYMBOL_TXN_COMMIT = 30,                /* TXN_COMMIT  */
  YYSYMBOL_TXN_ABORT = 31,                 /* TXN_ABORT  */
  YYSYMBOL_TXN_ROLLBACK = 32,              /* TXN_ROLLBACK  */
  YYSYMBOL_ORDER_BY = 33,                  /* ORDER_BY  */
  YYSYMBOL_ENABLE_NESTLOOP = 34,           /* ENABLE_NESTLOOP  */
  YYSYMBOL_ENABLE_SORTMERGE = 35,          /* ENABLE_SORTMERGE  */
  YYSYMBOL_EXPLAIN = 36,                   /* EXPLAIN  */
  YYSYMBOL_ANALYZE = 37,                   /* ANALYZE  */
  YYSYMBOL_ON = 38,                        /* ON  */
  YYSYMBOL_AS = 39,                        /* AS  */
  YYSYMBOL_LEQ = 40,                       /* LEQ  */
  YYSYMBOL_NEQ = 41,                       /* NEQ  */
  YYSYMBOL_GEQ = 42,                       /* GEQ  */
  YYSYMBOL_T_EOF = 43,                     /* T_EOF  */
  YYSYMBOL_IDENTIFIER = 44,                /* IDENTIFIER  */
  YYSYMBOL_VALUE_STRING = 45,              /* VALUE_STRING  */
  YYSYMBOL_VALUE_INT = 46,                 /* VALUE_INT  */
  YYSYMBOL_VALUE_FLOAT = 47,               /* VALUE_FLOAT  */
  YYSYMBOL_VALUE_BOOL = 48,                /* VALUE_BOOL  */
  YYSYMBOL_49_ = 49,                       /* ';'  */
  YYSYMBOL_50_ = 50,                       /* '='  */
  YYSYMBOL_51_ = 51,                       /* '('  */
  YYSYMBOL_52_ = 52,                       /* ')'  */
  YYSYMBOL_53_ = 53,                       /* ','  */
  YYSYMBOL_54_ = 54,                       /* '.'  */
  YYSYMBOL_55_ = 55,                       /* '<'  */
  YYSYMBOL_56_ = 56,                       /* '>'  */
  YYSYMBOL_57_ = 57,                       /* '*'  */
  YYSYMBOL_YYACCEPT = 58,                  /* $accept  */
  YYSYMBOL_start = 59,                     /* start  */
  YYSYMBOL_stmt = 60,                      /* stmt  */
  YYSYMBOL_txnStmt = 61,                   /* txnStmt  */
  YYSYMBOL_dbStmt = 62,                    /* dbStmt  */
  YYSYMBOL_setStmt = 63,                   /* setStmt  */
  YYSYMBOL_ddl = 64,                       /* ddl  */
  YYSYMBOL_dml = 65,                       /* dml  */
  YYSYMBOL_fieldList = 66,                 /* fieldList  */
  YYSYMBOL_colNameList = 67,               /* colNameList  */
  YYSYMBOL_field = 68,                     /* field  */
  YYSYMBOL_type = 69,                      /* type  */
  YYSYMBOL_valueList = 70,                 /* valueList  */
  YYSYMBOL_value = 71,                     /* value  */
  YYSYMBOL_condition = 72,                 /* condition  */
  YYSYMBOL_optWhereClause = 73,            /* optWhereClause  */
  YYSYMBOL_whereClause = 74,               /* whereClause  */
  YYSYMBOL_col = 75,                       /* col  */
  YYSYMBOL_colList = 76,                   /* colList  */
  YYSYMBOL_op = 77,                        /* op  */
  YYSYMBOL_expr = 78,                      /* expr  */
  YYSYMBOL_setClauses = 79,                /* setClauses  */
  YYSYMBOL_setClause = 80,                 /* setClause  */
  YYSYMBOL_selector = 81,                  /* selector  */
  YYSYMBOL_tableRef = 82,                  /* tableRef  */
  YYSYMBOL_tableList = 83,                 /* tableList  */
  YYSYMBOL_opt_order_clause = 84,          /* opt_order_clause  */
  YYSYMBOL_order_clause = 85,              /* order_clause  */
  YYSYMBOL_opt_asc_desc = 86,              /* opt_asc_desc  */
  YYSYMBOL_set_knob_type = 87,             /* set_knob_type  */
  YYSYMBOL_tbName = 88,                    /* tbName  */
  YYSYMBOL_colName = 89                    /* colName  */
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
#define YYFINAL  47
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   137

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  58
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  32
/* YYNRULES -- Number of rules.  */
#define YYNRULES  80
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  152

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   303


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
      51,    52,    57,     2,    53,     2,    54,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    49,
      55,    50,    56,     2,     2,     2,     2,     2,     2,     2,
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
      45,    46,    47,    48
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    63,    63,    68,    73,    78,    86,    87,    88,    89,
      90,    94,    98,   102,   106,   113,   117,   124,   131,   135,
     139,   143,   147,   154,   158,   162,   167,   173,   184,   188,
     195,   199,   206,   213,   217,   221,   228,   232,   239,   243,
     247,   251,   258,   265,   266,   273,   277,   285,   289,   296,
     300,   307,   311,   315,   319,   323,   327,   334,   338,   345,
     349,   356,   363,   367,   371,   375,   379,   386,   391,   396,
     401,   410,   414,   418,   425,   426,   427,   431,   432,   435,
     437
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
  "CREATE", "TABLE", "DROP", "DESC", "INSERT", "INTO", "VALUES", "DELETE",
  "FROM", "ASC", "ORDER", "BY", "WHERE", "UPDATE", "SET", "SELECT", "INT",
  "CHAR", "FLOAT", "INDEX", "AND", "JOIN", "EXIT", "HELP", "TXN_BEGIN",
  "TXN_COMMIT", "TXN_ABORT", "TXN_ROLLBACK", "ORDER_BY", "ENABLE_NESTLOOP",
  "ENABLE_SORTMERGE", "EXPLAIN", "ANALYZE", "ON", "AS", "LEQ", "NEQ",
  "GEQ", "T_EOF", "IDENTIFIER", "VALUE_STRING", "VALUE_INT", "VALUE_FLOAT",
  "VALUE_BOOL", "';'", "'='", "'('", "')'", "','", "'.'", "'<'", "'>'",
  "'*'", "$accept", "start", "stmt", "txnStmt", "dbStmt", "setStmt", "ddl",
  "dml", "fieldList", "colNameList", "field", "type", "valueList", "value",
  "condition", "optWhereClause", "whereClause", "col", "colList", "op",
  "expr", "setClauses", "setClause", "selector", "tableRef", "tableList",
  "opt_order_clause", "order_clause", "opt_asc_desc", "set_knob_type",
  "tbName", "colName", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-102)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-80)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int8 yypact[] =
{
      89,    12,     9,    15,   -27,    10,    27,   -27,    16,   -25,
    -102,  -102,  -102,  -102,  -102,  -102,    38,  -102,    29,   -15,
    -102,  -102,  -102,  -102,  -102,  -102,    47,   -27,   -27,   -27,
     -27,  -102,  -102,   -27,   -27,    65,  -102,  -102,    35,    32,
    -102,  -102,    34,    75,    45,  -102,    69,  -102,  -102,   -27,
      49,    51,  -102,    52,    93,    88,    62,    63,    66,   -27,
      62,   -25,  -102,    62,    62,    62,    64,    66,  -102,  -102,
      -8,  -102,    72,  -102,  -102,  -102,   -12,   -21,  -102,   100,
       2,  -102,    21,    24,  -102,    26,    23,  -102,    98,    17,
      62,  -102,    23,   -27,   -27,    99,   -27,  -102,   -27,  -102,
      62,  -102,    73,  -102,  -102,  -102,    62,  -102,  -102,  -102,
    -102,  -102,    28,  -102,    66,  -102,  -102,  -102,  -102,  -102,
    -102,    18,  -102,  -102,    90,  -102,   110,  -102,  -102,   -12,
    -102,    81,  -102,  -102,    23,  -102,  -102,  -102,  -102,    66,
      66,    99,    77,  -102,  -102,    14,  -102,  -102,  -102,  -102,
    -102,  -102
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       4,     3,    11,    12,    13,    14,     0,     5,     0,     0,
       9,     6,    10,     7,     8,    15,     0,     0,     0,     0,
       0,    79,    20,     0,     0,     0,    77,    78,     0,    80,
      62,    49,    63,     0,     0,    48,     0,     1,     2,     0,
       0,     0,    19,     0,     0,    43,     0,     0,     0,     0,
       0,     0,    16,     0,     0,     0,     0,     0,    24,    80,
      43,    59,     0,    17,    50,    67,    43,    64,    47,     0,
       0,    28,     0,     0,    30,     0,     0,    45,    44,     0,
       0,    25,     0,     0,     0,    72,     0,    65,     0,    18,
       0,    33,     0,    35,    32,    21,     0,    22,    40,    38,
      39,    41,     0,    36,     0,    55,    54,    56,    51,    52,
      53,     0,    60,    61,    69,    68,     0,    26,    66,    43,
      29,     0,    31,    23,     0,    46,    57,    58,    42,     0,
       0,    72,     0,    37,    70,    76,    71,    27,    34,    75,
      74,    73
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
    -102,  -102,  -102,  -102,  -102,  -102,  -102,  -102,  -102,    68,
      30,  -102,  -102,   -86,  -101,   -68,  -102,    -9,  -102,  -102,
    -102,  -102,    44,    74,   -11,    39,    -5,  -102,  -102,  -102,
      -3,   -53
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,    18,    19,    20,    21,    22,    23,    24,    80,    83,
      81,   104,   112,   113,    87,    68,    88,    89,    42,   121,
     138,    70,    71,    43,    75,    76,   127,   146,   151,    38,
      44,    45
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      41,    32,    91,    72,    35,    67,   123,    78,    95,    67,
      82,    84,    84,   135,    93,    27,    25,    31,    96,    39,
      33,    29,   149,    31,    50,    51,    52,    53,   150,    47,
      54,    55,    40,    28,    48,   136,    26,    72,   144,    30,
      34,    94,   101,   102,   103,    90,    62,    82,   143,    74,
      36,    37,    41,   132,    99,   100,    77,   115,   116,   117,
      49,   141,    39,   108,   109,   110,   111,   118,   108,   109,
     110,   111,   119,   120,    97,    46,   105,   106,   107,   106,
     133,   134,   124,   125,    56,    57,   -79,    58,    59,    61,
      77,    77,     1,   128,     2,    77,     3,     4,     5,    60,
      63,     6,    64,    65,    66,    67,    69,     7,     8,     9,
      39,    73,   137,    98,   126,    86,    10,    11,    12,    13,
      14,    15,    92,   114,   131,    16,   140,   142,   139,   148,
     130,   145,    17,    85,   122,    79,   147,   129
};

static const yytype_uint8 yycheck[] =
{
       9,     4,    70,    56,     7,    17,    92,    60,    76,    17,
      63,    64,    65,   114,    26,     6,     4,    44,    39,    44,
      10,     6,     8,    44,    27,    28,    29,    30,    14,     0,
      33,    34,    57,    24,    49,   121,    24,    90,   139,    24,
      13,    53,    21,    22,    23,    53,    49,   100,   134,    58,
      34,    35,    61,   106,    52,    53,    59,    40,    41,    42,
      13,   129,    44,    45,    46,    47,    48,    50,    45,    46,
      47,    48,    55,    56,    77,    37,    52,    53,    52,    53,
      52,    53,    93,    94,    19,    50,    54,    53,    13,    20,
      93,    94,     3,    96,     5,    98,     7,     8,     9,    54,
      51,    12,    51,    51,    11,    17,    44,    18,    19,    20,
      44,    48,   121,    13,    15,    51,    27,    28,    29,    30,
      31,    32,    50,    25,    51,    36,    16,    46,    38,    52,
     100,   140,    43,    65,    90,    61,   141,    98
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     5,     7,     8,     9,    12,    18,    19,    20,
      27,    28,    29,    30,    31,    32,    36,    43,    59,    60,
      61,    62,    63,    64,    65,     4,    24,     6,    24,     6,
      24,    44,    88,    10,    13,    88,    34,    35,    87,    44,
      57,    75,    76,    81,    88,    89,    37,     0,    49,    13,
      88,    88,    88,    88,    88,    88,    19,    50,    53,    13,
      54,    20,    88,    51,    51,    51,    11,    17,    73,    44,
      79,    80,    89,    48,    75,    82,    83,    88,    89,    81,
      66,    68,    89,    67,    89,    67,    51,    72,    74,    75,
      53,    73,    50,    26,    53,    73,    39,    88,    13,    52,
      53,    21,    22,    23,    69,    52,    53,    52,    45,    46,
      47,    48,    70,    71,    25,    40,    41,    42,    50,    55,
      56,    77,    80,    71,    82,    82,    15,    84,    88,    83,
      68,    51,    89,    52,    53,    72,    71,    75,    78,    38,
      16,    73,    46,    71,    72,    75,    85,    84,    52,     8,
      14,    86
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    58,    59,    59,    59,    59,    60,    60,    60,    60,
      60,    61,    61,    61,    61,    62,    62,    63,    64,    64,
      64,    64,    64,    65,    65,    65,    65,    65,    66,    66,
      67,    67,    68,    69,    69,    69,    70,    70,    71,    71,
      71,    71,    72,    73,    73,    74,    74,    75,    75,    76,
      76,    77,    77,    77,    77,    77,    77,    78,    78,    79,
      79,    80,    81,    81,    82,    82,    82,    83,    83,    83,
      83,    84,    84,    85,    86,    86,    86,    87,    87,    88,
      89
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     4,     4,     6,     3,
       2,     6,     6,     7,     4,     5,     6,     8,     1,     3,
       1,     3,     2,     1,     4,     1,     1,     3,     1,     1,
       1,     1,     3,     0,     2,     1,     3,     3,     1,     1,
       3,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       3,     3,     1,     1,     1,     2,     3,     1,     3,     3,
       5,     3,     0,     2,     1,     1,     0,     1,     1,     1,
       1
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
#line 64 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        parse_tree = (yyvsp[-1].sv_node);
        YYACCEPT;
    }
#line 1664 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 3: /* start: HELP  */
#line 69 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
#line 1673 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 4: /* start: EXIT  */
#line 74 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1682 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 5: /* start: T_EOF  */
#line 79 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
#line 1691 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 11: /* txnStmt: TXN_BEGIN  */
#line 95 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnBegin>();
    }
#line 1699 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 12: /* txnStmt: TXN_COMMIT  */
#line 99 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnCommit>();
    }
#line 1707 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 13: /* txnStmt: TXN_ABORT  */
#line 103 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnAbort>();
    }
#line 1715 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 14: /* txnStmt: TXN_ROLLBACK  */
#line 107 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<TxnRollback>();
    }
#line 1723 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 15: /* dbStmt: SHOW TABLES  */
#line 114 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowTables>();
    }
#line 1731 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 16: /* dbStmt: SHOW INDEX FROM tbName  */
#line 118 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<ShowIndex>((yyvsp[0].sv_str));
    }
#line 1739 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 17: /* setStmt: SET set_knob_type '=' VALUE_BOOL  */
#line 125 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<SetStmt>((yyvsp[-2].sv_setKnobType), (yyvsp[0].sv_bool));
    }
#line 1747 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 18: /* ddl: CREATE TABLE tbName '(' fieldList ')'  */
#line 132 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateTable>((yyvsp[-3].sv_str), (yyvsp[-1].sv_fields));
    }
#line 1755 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 19: /* ddl: DROP TABLE tbName  */
#line 136 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropTable>((yyvsp[0].sv_str));
    }
#line 1763 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 20: /* ddl: DESC tbName  */
#line 140 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DescTable>((yyvsp[0].sv_str));
    }
#line 1771 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 21: /* ddl: CREATE INDEX tbName '(' colNameList ')'  */
#line 144 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<CreateIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1779 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 22: /* ddl: DROP INDEX tbName '(' colNameList ')'  */
#line 148 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DropIndex>((yyvsp[-3].sv_str), (yyvsp[-1].sv_strs));
    }
#line 1787 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 23: /* dml: INSERT INTO tbName VALUES '(' valueList ')'  */
#line 155 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<InsertStmt>((yyvsp[-4].sv_str), (yyvsp[-1].sv_vals));
    }
#line 1795 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 24: /* dml: DELETE FROM tbName optWhereClause  */
#line 159 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<DeleteStmt>((yyvsp[-1].sv_str), (yyvsp[0].sv_conds));
    }
#line 1803 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 25: /* dml: UPDATE tbName SET setClauses optWhereClause  */
#line 163 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_node) = std::make_shared<UpdateStmt>((yyvsp[-3].sv_str), (yyvsp[-1].sv_set_clauses), (yyvsp[0].sv_conds));
    }
#line 1811 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 26: /* dml: SELECT selector FROM tableList optWhereClause opt_order_clause  */
#line 168 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-2].sv_from_clause).conds;
        conds.insert(conds.end(), (yyvsp[-1].sv_conds).begin(), (yyvsp[-1].sv_conds).end());
        (yyval.sv_node) = std::make_shared<SelectStmt>((yyvsp[-4].sv_cols), (yyvsp[-2].sv_from_clause).tables, conds, (yyvsp[0].sv_orderby));
    }
#line 1821 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 27: /* dml: EXPLAIN ANALYZE SELECT selector FROM tableList optWhereClause opt_order_clause  */
#line 174 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        auto conds = (yyvsp[-2].sv_from_clause).conds;
        conds.insert(conds.end(), (yyvsp[-1].sv_conds).begin(), (yyvsp[-1].sv_conds).end());
        auto stmt = std::make_shared<SelectStmt>((yyvsp[-4].sv_cols), (yyvsp[-2].sv_from_clause).tables, conds, (yyvsp[0].sv_orderby));
        stmt->is_explain_analyze = true;
        (yyval.sv_node) = stmt;
    }
#line 1833 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 28: /* fieldList: field  */
#line 185 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_fields) = std::vector<std::shared_ptr<Field>>{(yyvsp[0].sv_field)};
    }
#line 1841 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 29: /* fieldList: fieldList ',' field  */
#line 189 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_fields).push_back((yyvsp[0].sv_field));
    }
#line 1849 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 30: /* colNameList: colName  */
#line 196 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_strs) = std::vector<std::string>{(yyvsp[0].sv_str)};
    }
#line 1857 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 31: /* colNameList: colNameList ',' colName  */
#line 200 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_strs).push_back((yyvsp[0].sv_str));
    }
#line 1865 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 32: /* field: colName type  */
#line 207 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_field) = std::make_shared<ColDef>((yyvsp[-1].sv_str), (yyvsp[0].sv_type_len));
    }
#line 1873 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 33: /* type: INT  */
#line 214 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
#line 1881 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 34: /* type: CHAR '(' VALUE_INT ')'  */
#line 218 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_STRING, (yyvsp[-1].sv_int));
    }
#line 1889 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 35: /* type: FLOAT  */
#line 222 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_type_len) = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
#line 1897 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 36: /* valueList: value  */
#line 229 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals) = std::vector<std::shared_ptr<Value>>{(yyvsp[0].sv_val)};
    }
#line 1905 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 37: /* valueList: valueList ',' value  */
#line 233 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_vals).push_back((yyvsp[0].sv_val));
    }
#line 1913 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 38: /* value: VALUE_INT  */
#line 240 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<IntLit>((yyvsp[0].sv_int));
    }
#line 1921 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 39: /* value: VALUE_FLOAT  */
#line 244 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<FloatLit>((yyvsp[0].sv_float));
    }
#line 1929 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 40: /* value: VALUE_STRING  */
#line 248 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<StringLit>((yyvsp[0].sv_str));
    }
#line 1937 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 41: /* value: VALUE_BOOL  */
#line 252 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_val) = std::make_shared<BoolLit>((yyvsp[0].sv_bool));
    }
#line 1945 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 42: /* condition: col op expr  */
#line 259 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cond) = std::make_shared<BinaryExpr>((yyvsp[-2].sv_col), (yyvsp[-1].sv_comp_op), (yyvsp[0].sv_expr));
    }
#line 1953 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 43: /* optWhereClause: %empty  */
#line 265 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                      { /* ignore*/ }
#line 1959 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 44: /* optWhereClause: WHERE whereClause  */
#line 267 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[0].sv_conds);
    }
#line 1967 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 45: /* whereClause: condition  */
#line 274 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = std::vector<std::shared_ptr<BinaryExpr>>{(yyvsp[0].sv_cond)};
    }
#line 1975 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 46: /* whereClause: whereClause AND condition  */
#line 278 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_conds) = (yyvsp[-2].sv_conds);
        (yyval.sv_conds).push_back((yyvsp[0].sv_cond));
    }
#line 1984 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 47: /* col: tbName '.' colName  */
#line 286 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 1992 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 48: /* col: colName  */
#line 290 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_col) = std::make_shared<Col>("", (yyvsp[0].sv_str));
    }
#line 2000 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 49: /* colList: col  */
#line 297 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols) = std::vector<std::shared_ptr<Col>>{(yyvsp[0].sv_col)};
    }
#line 2008 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 50: /* colList: colList ',' col  */
#line 301 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols).push_back((yyvsp[0].sv_col));
    }
#line 2016 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 51: /* op: '='  */
#line 308 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_EQ;
    }
#line 2024 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 52: /* op: '<'  */
#line 312 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LT;
    }
#line 2032 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 53: /* op: '>'  */
#line 316 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GT;
    }
#line 2040 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 54: /* op: NEQ  */
#line 320 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_NE;
    }
#line 2048 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 55: /* op: LEQ  */
#line 324 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_LE;
    }
#line 2056 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 56: /* op: GEQ  */
#line 328 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_comp_op) = SV_OP_GE;
    }
#line 2064 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 57: /* expr: value  */
#line 335 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_val));
    }
#line 2072 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 58: /* expr: col  */
#line 339 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_expr) = std::static_pointer_cast<Expr>((yyvsp[0].sv_col));
    }
#line 2080 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 59: /* setClauses: setClause  */
#line 346 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses) = std::vector<std::shared_ptr<SetClause>>{(yyvsp[0].sv_set_clause)};
    }
#line 2088 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 60: /* setClauses: setClauses ',' setClause  */
#line 350 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clauses).push_back((yyvsp[0].sv_set_clause));
    }
#line 2096 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 61: /* setClause: colName '=' value  */
#line 357 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_set_clause) = std::make_shared<SetClause>((yyvsp[-2].sv_str), (yyvsp[0].sv_val));
    }
#line 2104 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 62: /* selector: '*'  */
#line 364 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_cols) = {};
    }
#line 2112 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 64: /* tableRef: tbName  */
#line 372 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_ref) = TableRef((yyvsp[0].sv_str), "");
    }
#line 2120 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 65: /* tableRef: tbName tbName  */
#line 376 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_ref) = TableRef((yyvsp[-1].sv_str), (yyvsp[0].sv_str));
    }
#line 2128 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 66: /* tableRef: tbName AS tbName  */
#line 380 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_table_ref) = TableRef((yyvsp[-2].sv_str), (yyvsp[0].sv_str));
    }
#line 2136 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 67: /* tableList: tableRef  */
#line 387 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyval.sv_from_clause).tables = {(yyvsp[0].sv_table_ref)};
        (yyval.sv_from_clause).conds = {};
    }
#line 2145 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 68: /* tableList: tableList ',' tableRef  */
#line 392 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_from_clause).tables.push_back((yyvsp[0].sv_table_ref));
        (yyval.sv_from_clause) = (yyvsp[-2].sv_from_clause);
    }
#line 2154 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 69: /* tableList: tableList JOIN tableRef  */
#line 397 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyvsp[-2].sv_from_clause).tables.push_back((yyvsp[0].sv_table_ref));
        (yyval.sv_from_clause) = (yyvsp[-2].sv_from_clause);
    }
#line 2163 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 70: /* tableList: tableList JOIN tableRef ON condition  */
#line 402 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    {
        (yyvsp[-4].sv_from_clause).tables.push_back((yyvsp[-2].sv_table_ref));
        (yyvsp[-4].sv_from_clause).conds.push_back((yyvsp[0].sv_cond));
        (yyval.sv_from_clause) = (yyvsp[-4].sv_from_clause);
    }
#line 2173 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 71: /* opt_order_clause: ORDER BY order_clause  */
#line 411 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    { 
        (yyval.sv_orderby) = (yyvsp[0].sv_orderby); 
    }
#line 2181 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 72: /* opt_order_clause: %empty  */
#line 414 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                      { /* ignore*/ }
#line 2187 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 73: /* order_clause: col opt_asc_desc  */
#line 419 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
    { 
        (yyval.sv_orderby) = std::make_shared<OrderBy>((yyvsp[-1].sv_col), (yyvsp[0].sv_orderby_dir));
    }
#line 2195 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 74: /* opt_asc_desc: ASC  */
#line 425 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_ASC;     }
#line 2201 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 75: /* opt_asc_desc: DESC  */
#line 426 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                 { (yyval.sv_orderby_dir) = OrderBy_DESC;    }
#line 2207 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 76: /* opt_asc_desc: %empty  */
#line 427 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
            { (yyval.sv_orderby_dir) = OrderBy_DEFAULT; }
#line 2213 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 77: /* set_knob_type: ENABLE_NESTLOOP  */
#line 431 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                    { (yyval.sv_setKnobType) = EnableNestLoop; }
#line 2219 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;

  case 78: /* set_knob_type: ENABLE_SORTMERGE  */
#line 432 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"
                         { (yyval.sv_setKnobType) = EnableSortMerge; }
#line 2225 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"
    break;


#line 2229 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.tab.cpp"

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

#line 438 "/mnt/hgfs/DMS-DESIGN/db2026/src/parser/yacc.y"

