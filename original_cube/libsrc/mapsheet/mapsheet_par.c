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
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "mapsheet_par.y"

/*
 * $Id: mapsheet_par.y 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.3.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.3  2001/09/23 20:37:32  brc
 * Added optional backing store clause and recognition token, and upgraded the
 * mapsheet construction code to use the calls with backing store specification.
 *
 * Revision 1.2  2001/08/20 22:38:56  brc
 * Made allocator static so that it doesn't pollute the namespace
 * without due cause.  This shouldn't be a problem now that the mapsheet
 * ASCII reader has moved into the library where it should have been all
 * along...
 *
 * Revision 1.1  2001/08/11 00:02:20  brc
 * Added mapsheet parser to core code, rather than having it hidden in the
 * utilities section.  This also means that the interface is nicely hidden, and
 * that the user just sees mapsheet_new_from_ascii().
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	mapsheet_par.y
 * Purpose:	YACC source for mapsheet descriptions.
 * Date:	18 July 2000
 *
 * Copyright 2022, Center for Coastal and Ocean Mapping and NOAA-UNH Joint Hydrographic
 * Center, University of New Hampshire.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies
 * or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "stdtypes.h"
#include "mapsheet.h"
#include "projection.h"
#include "error.h"

#include "mapsheet_private_parser.h"

#define DEG2MET(x) ((x)*60.0*1.0*1852.0)
				/* Converts degrees of arc to meters at equator, approx. */

static const char *modname = "mapsheet_par";

extern int maplex(void);

static SheetList *alloc_sheetlist(void);
extern SheetList *sheets;
void maperror(char *msg);


#line 161 "mapsheet_par.c"

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

#include "mapsheet_par.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_METERS = 3,                     /* METERS  */
  YYSYMBOL_KILOMETERS = 4,                 /* KILOMETERS  */
  YYSYMBOL_DEGREES = 5,                    /* DEGREES  */
  YYSYMBOL_MINUTES = 6,                    /* MINUTES  */
  YYSYMBOL_RADIANS = 7,                    /* RADIANS  */
  YYSYMBOL_PROJECTION = 8,                 /* PROJECTION  */
  YYSYMBOL_SHEET = 9,                      /* SHEET  */
  YYSYMBOL_OBRACE = 10,                    /* OBRACE  */
  YYSYMBOL_CBRACE = 11,                    /* CBRACE  */
  YYSYMBOL_OBRACKET = 12,                  /* OBRACKET  */
  YYSYMBOL_CBRACKET = 13,                  /* CBRACKET  */
  YYSYMBOL_LISTSEP = 14,                   /* LISTSEP  */
  YYSYMBOL_EOS = 15,                       /* EOS  */
  YYSYMBOL_SPACING = 16,                   /* SPACING  */
  YYSYMBOL_LOCATION = 17,                  /* LOCATION  */
  YYSYMBOL_BOUNDS = 18,                    /* BOUNDS  */
  YYSYMBOL_TYPE = 19,                      /* TYPE  */
  YYSYMBOL_ORIGIN = 20,                    /* ORIGIN  */
  YYSYMBOL_FALSE_ORIGIN = 21,              /* FALSE_ORIGIN  */
  YYSYMBOL_BACKSTORE = 22,                 /* BACKSTORE  */
  YYSYMBOL_FLOAT = 23,                     /* FLOAT  */
  YYSYMBOL_IDENTIFIER = 24,                /* IDENTIFIER  */
  YYSYMBOL_STRING = 25,                    /* STRING  */
  YYSYMBOL_YYACCEPT = 26,                  /* $accept  */
  YYSYMBOL_file = 27,                      /* file  */
  YYSYMBOL_sheet = 28,                     /* sheet  */
  YYSYMBOL_projection = 29,                /* projection  */
  YYSYMBOL_typespec = 30,                  /* typespec  */
  YYSYMBOL_origspec = 31,                  /* origspec  */
  YYSYMBOL_optfalseo = 32,                 /* optfalseo  */
  YYSYMBOL_sheetdef = 33,                  /* sheetdef  */
  YYSYMBOL_dimspec = 34,                   /* dimspec  */
  YYSYMBOL_numvec = 35,                    /* numvec  */
  YYSYMBOL_num = 36,                       /* num  */
  YYSYMBOL_spacespec = 37,                 /* spacespec  */
  YYSYMBOL_unit = 38,                      /* unit  */
  YYSYMBOL_obackstore = 39                 /* obackstore  */
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
typedef yytype_int8 yy_state_t;

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

#if !defined yyoverflow

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
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

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
#define YYFINAL  5
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   60

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  26
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  14
/* YYNRULES -- Number of rules.  */
#define YYNRULES  22
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  71

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   280


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
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   125,   125,   126,   129,   171,   196,   202,   213,   214,
     225,   234,   254,   278,   288,   296,   309,   310,   311,   312,
     313,   316,   317
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "METERS", "KILOMETERS",
  "DEGREES", "MINUTES", "RADIANS", "PROJECTION", "SHEET", "OBRACE",
  "CBRACE", "OBRACKET", "CBRACKET", "LISTSEP", "EOS", "SPACING",
  "LOCATION", "BOUNDS", "TYPE", "ORIGIN", "FALSE_ORIGIN", "BACKSTORE",
  "FLOAT", "IDENTIFIER", "STRING", "$accept", "file", "sheet",
  "projection", "typespec", "origspec", "optfalseo", "sheetdef", "dimspec",
  "numvec", "num", "spacespec", "unit", "obackstore", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-57)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int8 yypact[] =
{
      -3,    -1,     1,   -57,    10,   -57,   -57,     7,   -13,   -12,
       8,     8,    11,     5,     0,     3,     2,    12,    13,   -57,
      14,    15,    16,    17,    18,     9,    19,   -57,   -57,    20,
      21,   -57,   -57,    22,    23,    25,   -57,   -57,   -57,   -57,
     -57,   -57,     2,    24,    26,    28,    27,   -57,    30,    29,
     -57,    31,    33,     2,    35,    36,    32,    37,     9,     9,
      40,     2,    41,    42,     9,    45,   -57,   -57,    44,   -57,
     -57
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,     0,     2,     0,     1,     3,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     4,
       0,    21,     0,     0,     8,     0,     0,    11,    12,     0,
       0,    10,     6,     0,     0,     0,    16,    17,    18,    19,
      20,    14,     0,     0,     0,     0,     0,     5,     0,     0,
      22,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    15,     7,     0,    13,
       9
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -57,   -57,    38,   -57,   -57,   -57,   -57,   -57,   -57,    49,
     -42,   -57,   -56,   -57
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,     2,     3,     8,    15,    24,    35,    12,    13,    17,
      26,    21,    41,    31
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
      48,     5,    62,    63,    10,    11,     1,    14,    68,     4,
       1,    57,    36,    37,    38,    39,    40,     9,     7,    65,
      16,    20,    19,    23,    22,    25,    29,    27,    28,    33,
       0,    32,     0,    42,     0,    46,    47,    30,    49,    34,
       6,    50,    51,    43,    53,    45,    44,    56,    58,    59,
      52,    61,    54,    64,    55,    60,    66,    67,    69,    70,
      18
};

static const yytype_int8 yycheck[] =
{
      42,     0,    58,    59,    17,    18,     9,    19,    64,    10,
       9,    53,     3,     4,     5,     6,     7,    10,     8,    61,
      12,    16,    11,    20,    24,    23,    12,    15,    15,    12,
      -1,    15,    -1,    14,    -1,    12,    11,    22,    14,    21,
       2,    15,    14,    23,    14,    23,    25,    14,    13,    13,
      23,    14,    23,    13,    23,    23,    15,    15,    13,    15,
      11
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     9,    27,    28,    10,     0,    28,     8,    29,    10,
      17,    18,    33,    34,    19,    30,    12,    35,    35,    11,
      16,    37,    24,    20,    31,    23,    36,    15,    15,    12,
      22,    39,    15,    12,    21,    32,     3,     4,     5,     6,
       7,    38,    14,    23,    25,    23,    12,    11,    36,    14,
      15,    14,    23,    14,    23,    23,    14,    36,    13,    13,
      23,    14,    38,    38,    13,    36,    15,    15,    38,    13,
      15
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    26,    27,    27,    28,    29,    30,    31,    32,    32,
      33,    34,    34,    35,    36,    37,    38,    38,    38,    38,
      38,    39,    39
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     2,     5,     6,     3,     8,     0,     8,
       3,     3,     3,     9,     2,     8,     1,     1,     1,     1,
       1,     0,     3
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
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


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




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
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
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
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
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
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
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
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






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
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

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

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

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
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
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

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
      yychar = yylex ();
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


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* file: sheet  */
#line 125 "mapsheet_par.y"
                { (yyvsp[0].shval)->next = sheets; sheets = (yyvsp[0].shval); }
#line 1222 "mapsheet_par.c"
    break;

  case 3: /* file: file sheet  */
#line 126 "mapsheet_par.y"
                           { (yyvsp[0].shval)->next = sheets; sheets = (yyvsp[0].shval); }
#line 1228 "mapsheet_par.c"
    break;

  case 4: /* sheet: SHEET OBRACE projection sheetdef CBRACE  */
#line 130 "mapsheet_par.y"
        {
		if (((yyval.shval) = alloc_sheetlist()) == NULL) {
			maperror("cannot build sheet description structure");
			return(-1);
		}
		if ((yyvsp[-1].sdval).center.units == Sheet_Meters) {
			/* Attempt to build mapsheet with projected center */
#ifdef __DEBUG__
			error_msgv(modname, "making mapsheet by projected center, "
				"(w,h)=(%lf, %lf) m, deltas = (%lf, %lf) m, center = (%lf, %lf) m\n",
				(yyvsp[-1].sdval).sizes.x, (yyvsp[-1].sdval).sizes.y, (yyvsp[-1].sdval).spacing.x, (yyvsp[-1].sdval).spacing.y,
				(yyvsp[-1].sdval).center.x, (yyvsp[-1].sdval).center.y);
#endif
			(yyval.shval)->sheet = mapsheet_new_by_proj_center_backed(
							(yyvsp[-2].prval),
							(yyvsp[-1].sdval).sizes.x, (yyvsp[-1].sdval).sizes.y,
							(yyvsp[-1].sdval).spacing.x, (yyvsp[-1].sdval).spacing.y,
							(yyvsp[-1].sdval).center.x, (yyvsp[-1].sdval).center.y,
							(yyvsp[-1].sdval).backing_store);
		} else {
			/* Attempt to build mapsheet with lon/lat center */
#ifdef __DEBUG__
			error_msgv(modname, "making mapsheet by geographic center, "
				"(w,h)=(%lf, %lf) m, deltas = (%lf, %lf) m, center = (%lf, %lf) deg\n",
				(yyvsp[-1].sdval).sizes.x, (yyvsp[-1].sdval).sizes.y, (yyvsp[-1].sdval).spacing.x, (yyvsp[-1].sdval).spacing.y,
				(yyvsp[-1].sdval).center.x, (yyvsp[-1].sdval).center.y);
#endif
			(yyval.shval)->sheet = mapsheet_new_by_center_backed(
							(yyvsp[-2].prval),
							(yyvsp[-1].sdval).sizes.x, (yyvsp[-1].sdval).sizes.y,
							(yyvsp[-1].sdval).spacing.x, (yyvsp[-1].sdval).spacing.y,
							(yyvsp[-1].sdval).center.x, (yyvsp[-1].sdval).center.y,
							(yyvsp[-1].sdval).backing_store);
		}
		if ((yyval.shval)->sheet == NULL) {
			maperror("cannot build mapsheet");
			return(-1);
		}
	}
#line 1272 "mapsheet_par.c"
    break;

  case 5: /* projection: PROJECTION OBRACE typespec origspec optfalseo CBRACE  */
#line 172 "mapsheet_par.y"
        {
#ifdef __DEBUG__
		error_msgv(modname, "projection = %s, o=(%lf,%lf) fo=(%lf,%lf)\n", (yyvsp[-3].sval),
			(yyvsp[-2].vecval).x, (yyvsp[-2].vecval).y, (yyvsp[-1].vecval).x, (yyvsp[-1].vecval).y);
#endif
		if (strcmp((yyvsp[-3].sval), "utm") == 0) {
			if ((yyvsp[-1].vecval).x != 0.0 || (yyvsp[-1].vecval).y != 0.0)
				maperror("UTM has no user-defined false origin (ignoring)");
			(yyval.prval) = projection_new_utm((yyvsp[-2].vecval).x, (yyvsp[-2].vecval).y);
		} else if (strcmp((yyvsp[-3].sval), "mercator") == 0) {
			(yyval.prval) = projection_new_mercator((yyvsp[-2].vecval).x, (yyvsp[-2].vecval).y, (yyvsp[-1].vecval).x, (yyvsp[-1].vecval).y);
		} else if (strcmp((yyvsp[-3].sval), "polarstereo") == 0) {
			(yyval.prval) = projection_new_polar_stereo((yyvsp[-2].vecval).x, (yyvsp[-2].vecval).y, (yyvsp[-1].vecval).x, (yyvsp[-1].vecval).y);
		} else {
			maperror("projection type not recognised");
			return(-1);
		}
		if ((yyval.prval) == NULL) {
			maperror("cannot create projection method");
			return(-1);
		}
	}
#line 1299 "mapsheet_par.c"
    break;

  case 6: /* typespec: TYPE IDENTIFIER EOS  */
#line 197 "mapsheet_par.y"
        {
		(yyval.sval) = (yyvsp[-1].sval);
	}
#line 1307 "mapsheet_par.c"
    break;

  case 7: /* origspec: ORIGIN OBRACKET FLOAT LISTSEP FLOAT CBRACKET unit EOS  */
#line 203 "mapsheet_par.y"
        {
		if ((yyvsp[-1].vecval).units != Sheet_Degrees) {
			maperror("origin can only be specified in angular units");
			return(-1);
		}
		(yyval.vecval).units = (yyvsp[-1].vecval).units;
		(yyval.vecval).x = (yyvsp[-5].fval) * (yyvsp[-1].vecval).x; (yyval.vecval).y = (yyvsp[-3].fval) * (yyvsp[-1].vecval).y;
	}
#line 1320 "mapsheet_par.c"
    break;

  case 8: /* optfalseo: %empty  */
#line 213 "mapsheet_par.y"
                { memset(&((yyval.vecval)), 0, sizeof(SheetVec)); }
#line 1326 "mapsheet_par.c"
    break;

  case 9: /* optfalseo: FALSE_ORIGIN OBRACKET FLOAT LISTSEP FLOAT CBRACKET unit EOS  */
#line 215 "mapsheet_par.y"
                {
			if ((yyvsp[-1].vecval).units != Sheet_Meters) {
				maperror("false origin can only be specified in linear units");
				return(-1);
			}
			(yyval.vecval).units = (yyvsp[-1].vecval).units;
			(yyval.vecval).x = (yyvsp[-5].fval) * (yyvsp[-1].vecval).x; (yyval.vecval).y = (yyvsp[-3].fval) * (yyvsp[-1].vecval).y;
		}
#line 1339 "mapsheet_par.c"
    break;

  case 10: /* sheetdef: dimspec spacespec obackstore  */
#line 226 "mapsheet_par.y"
        {
		(yyval.sdval) = (yyvsp[-2].sdval);
		(yyval.sdval).spacing = (yyvsp[-1].vecval);
		(yyval.sdval).backing_store = (yyvsp[0].sval);
	}
#line 1349 "mapsheet_par.c"
    break;

  case 11: /* dimspec: LOCATION numvec EOS  */
#line 235 "mapsheet_par.y"
                {
			if ((yyvsp[-1].nvecval).vec[0].units != (yyvsp[-1].nvecval).vec[1].units) {
				maperror("center locations must have the same units");
				return(-1);
			}
			(yyval.sdval).center.units = (yyvsp[-1].nvecval).vec[0].units;
			(yyval.sdval).center.x = (yyvsp[-1].nvecval).vec[0].value; (yyval.sdval).center.y = (yyvsp[-1].nvecval).vec[1].value;
			(yyval.sdval).sizes.units = Sheet_Meters;
			if ((yyvsp[-1].nvecval).vec[2].units == Sheet_Degrees) {
				maperror("warning: converting degrees to meters at equator");
				(yyval.sdval).sizes.x = DEG2MET((yyvsp[-1].nvecval).vec[2].value);
			} else
				(yyval.sdval).sizes.x = (yyvsp[-1].nvecval).vec[2].value;
			if ((yyvsp[-1].nvecval).vec[3].units == Sheet_Degrees) {
				maperror("warning: converting degrees to meters at equator");
				(yyval.sdval).sizes.y = DEG2MET((yyvsp[-1].nvecval).vec[3].value);
			} else
				(yyval.sdval).sizes.y = (yyvsp[-1].nvecval).vec[3].value;
		}
#line 1373 "mapsheet_par.c"
    break;

  case 12: /* dimspec: BOUNDS numvec EOS  */
#line 255 "mapsheet_par.y"
                {
			if ((yyvsp[-1].nvecval).vec[0].units != (yyvsp[-1].nvecval).vec[1].units ||
				(yyvsp[-1].nvecval).vec[2].units != (yyvsp[-1].nvecval).vec[3].units ||
				(yyvsp[-1].nvecval).vec[1].units != (yyvsp[-1].nvecval).vec[2].units) {
				maperror("bounds must all be in the same units");
				return(-1);
			}
			(yyval.sdval).center.units = (yyvsp[-1].nvecval).vec[0].units;
			(yyval.sdval).center.x = ((yyvsp[-1].nvecval).vec[0].value+(yyvsp[-1].nvecval).vec[2].value)/2.0;
			(yyval.sdval).center.y = ((yyvsp[-1].nvecval).vec[1].value+(yyvsp[-1].nvecval).vec[3].value)/2.0;
			(yyval.sdval).sizes.units = Sheet_Meters;
			if ((yyvsp[-1].nvecval).vec[0].units == Sheet_Degrees) {
				maperror("warning: converting degrees to meters at equator");
				(yyval.sdval).sizes.x = DEG2MET((yyvsp[-1].nvecval).vec[2].value-(yyvsp[-1].nvecval).vec[0].value);
				(yyval.sdval).sizes.y = DEG2MET((yyvsp[-1].nvecval).vec[3].value-(yyvsp[-1].nvecval).vec[1].value);
			} else {
				(yyval.sdval).sizes.x = (yyvsp[-1].nvecval).vec[2].value - (yyvsp[-1].nvecval).vec[0].value;
				(yyval.sdval).sizes.y = (yyvsp[-1].nvecval).vec[3].value - (yyvsp[-1].nvecval).vec[1].value;
			}
		}
#line 1398 "mapsheet_par.c"
    break;

  case 13: /* numvec: OBRACKET num LISTSEP num LISTSEP num LISTSEP num CBRACKET  */
#line 279 "mapsheet_par.y"
        {
		(yyval.nvecval).vec[0] = (yyvsp[-7].numval);
		(yyval.nvecval).vec[1] = (yyvsp[-5].numval);
		(yyval.nvecval).vec[2] = (yyvsp[-3].numval);
		(yyval.nvecval).vec[3] = (yyvsp[-1].numval);
	}
#line 1409 "mapsheet_par.c"
    break;

  case 14: /* num: FLOAT unit  */
#line 289 "mapsheet_par.y"
        {
		(yyval.numval).units = (yyvsp[0].vecval).units;
		(yyval.numval).value = (yyvsp[-1].fval) * (yyvsp[0].vecval).x;
	}
#line 1418 "mapsheet_par.c"
    break;

  case 15: /* spacespec: SPACING OBRACKET FLOAT LISTSEP FLOAT CBRACKET unit EOS  */
#line 297 "mapsheet_par.y"
        {
		(yyval.vecval).units = Sheet_Meters;
		(yyval.vecval).x = (yyvsp[-5].fval) * (yyvsp[-1].vecval).x; (yyval.vecval).y = (yyvsp[-3].fval) * (yyvsp[-1].vecval).y;
		
		if ((yyvsp[-1].vecval).units != Sheet_Meters) {
			maperror("warning: converting degrees to meters at equator");
			(yyval.vecval).x *= DEG2MET(1.0);
			(yyval.vecval).y *= DEG2MET(1.0);
		}
	}
#line 1433 "mapsheet_par.c"
    break;

  case 16: /* unit: METERS  */
#line 309 "mapsheet_par.y"
               { (yyval.vecval).units = Sheet_Meters; (yyval.vecval).x = (yyval.vecval).y = 1.0; }
#line 1439 "mapsheet_par.c"
    break;

  case 17: /* unit: KILOMETERS  */
#line 310 "mapsheet_par.y"
                     { (yyval.vecval).units = Sheet_Meters; (yyval.vecval).x = (yyval.vecval).y = 1000.0; }
#line 1445 "mapsheet_par.c"
    break;

  case 18: /* unit: DEGREES  */
#line 311 "mapsheet_par.y"
                  { (yyval.vecval).units = Sheet_Degrees; (yyval.vecval).x = (yyval.vecval).y = 1.0; }
#line 1451 "mapsheet_par.c"
    break;

  case 19: /* unit: MINUTES  */
#line 312 "mapsheet_par.y"
                  { (yyval.vecval).units = Sheet_Degrees; (yyval.vecval).x = (yyval.vecval).y = 1/60.0; }
#line 1457 "mapsheet_par.c"
    break;

  case 20: /* unit: RADIANS  */
#line 313 "mapsheet_par.y"
                  { (yyval.vecval).units = Sheet_Degrees; (yyval.vecval).x = (yyval.vecval).y = M_PI/180.0; }
#line 1463 "mapsheet_par.c"
    break;

  case 21: /* obackstore: %empty  */
#line 316 "mapsheet_par.y"
                { (yyval.sval) = NULL; }
#line 1469 "mapsheet_par.c"
    break;

  case 22: /* obackstore: BACKSTORE STRING EOS  */
#line 317 "mapsheet_par.y"
                               { (yyval.sval) = (yyvsp[-1].sval); }
#line 1475 "mapsheet_par.c"
    break;


#line 1479 "mapsheet_par.c"

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
      yyerror (YY_("syntax error"));
    }

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
                      yytoken, &yylval);
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


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


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
  yyerror (YY_("memory exhausted"));
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
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 319 "mapsheet_par.y"


int 		maplinenum = 1;
SheetList	*sheets = NULL;

void maperror(char *msg)
{
	error_msgv(modname, "%s at line %d.\n", msg, maplinenum);
}

static SheetList *alloc_sheetlist(void)
{
	SheetList	*rtn;
	
	if ((rtn = (SheetList*)calloc(1, sizeof(SheetList))) == NULL) {
		maperror("out of memory for sheet structure");
		return(NULL);
	}
	return(rtn);
}
