/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_MAPSHEET_PAR_H_INCLUDED
# define YY_YY_MAPSHEET_PAR_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    METERS = 258,                  /* METERS  */
    KILOMETERS = 259,              /* KILOMETERS  */
    DEGREES = 260,                 /* DEGREES  */
    MINUTES = 261,                 /* MINUTES  */
    RADIANS = 262,                 /* RADIANS  */
    PROJECTION = 263,              /* PROJECTION  */
    SHEET = 264,                   /* SHEET  */
    OBRACE = 265,                  /* OBRACE  */
    CBRACE = 266,                  /* CBRACE  */
    OBRACKET = 267,                /* OBRACKET  */
    CBRACKET = 268,                /* CBRACKET  */
    LISTSEP = 269,                 /* LISTSEP  */
    EOS = 270,                     /* EOS  */
    SPACING = 271,                 /* SPACING  */
    LOCATION = 272,                /* LOCATION  */
    BOUNDS = 273,                  /* BOUNDS  */
    TYPE = 274,                    /* TYPE  */
    ORIGIN = 275,                  /* ORIGIN  */
    FALSE_ORIGIN = 276,            /* FALSE_ORIGIN  */
    BACKSTORE = 277,               /* BACKSTORE  */
    FLOAT = 278,                   /* FLOAT  */
    IDENTIFIER = 279,              /* IDENTIFIER  */
    STRING = 280                   /* STRING  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif
/* Token kinds.  */
#define YYEMPTY -2
#define YYEOF 0
#define YYerror 256
#define YYUNDEF 257
#define METERS 258
#define KILOMETERS 259
#define DEGREES 260
#define MINUTES 261
#define RADIANS 262
#define PROJECTION 263
#define SHEET 264
#define OBRACE 265
#define CBRACE 266
#define OBRACKET 267
#define CBRACKET 268
#define LISTSEP 269
#define EOS 270
#define SPACING 271
#define LOCATION 272
#define BOUNDS 273
#define TYPE 274
#define ORIGIN 275
#define FALSE_ORIGIN 276
#define BACKSTORE 277
#define FLOAT 278
#define IDENTIFIER 279
#define STRING 280

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 91 "mapsheet_par.y"

	s32			ival;
	f64			fval;
	char		*sval;
	SheetList	*shval;
	SheetDef	sdval;
	SheetVec	vecval;
	Projection	prval;
	Number		numval;
	NumVec		nvecval;

#line 129 "mapsheet_par.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_MAPSHEET_PAR_H_INCLUDED  */
