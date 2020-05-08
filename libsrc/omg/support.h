/*----------------------------------------------------------------------
	support.h	-	Guy Carpenter	-	Oct 18, 1989

	Library of commonly used support routines.

	Code is included for
		error message handling
		boolean types
		useful definitions

----------------------------------------------------------------------*/

#ifndef support_
#define support_

#include <stdio.h>

#ifndef M_PI
#define M_PI 3.141592654
#endif
#ifndef M_E
#define M_E	 2.718281828
#endif

extern int ARGC;
extern char **ARGV;

#define MIN(a,b)	(((a)<(b)) ? (a) : (b))
#define MAX(a,b)	(((a)>(b)) ? (a) : (b))
#define SQUARE(a)	((a)*(a))
/* wouldn't you think PI should be defined in math.h */
/* (brc) It is on most systems, as M_PI, and at better resolution than this! */
#ifndef PI
#define PI		3.1415926535
#endif
#define CREATE(T)	((T *)malloc(sizeof(T)))
#define SWAP(A,B,C)	{C=(A);A=(B);B=(C);}
#define TOLOWER(C)	(isupper(C) ? tolower(C) : (C))

#ifndef true
#define false           ((unsigned char) 00)
#define true            ((unsigned char) 0xff)
typedef unsigned char boolean;
#endif

boolean isinstr (char c, char *s);
				/* convert an ascii fixed length field */
int fieldtoint (char *s, int len);
double fieldtodbl (char *s, int len);
int strtoint (char *s);		/* convert an ascii string integer to int  */
double strtodbl (char *s);	/* convert an ascii string float/int to dbl*/
double strtoangle (char *s);	/* accepts aaa.ddd or aaa'mm format        */

typedef void *cleanup_fn(void);
void cleanup_register (cleanup_fn *fn);
void cleanup ();





/* void usage (char *msg,...); */ 	/* print out usage message and exit */
extern void error (char *msg,...);	/* print out error message and exit */
extern void warning (char *msg,...);	/* print out warning and continue   */

/*
double rounddown (double n, double mod);
double roundup (double n, double mod);
*/
double roundupforce (double n, double mod);	/* never returns n */

char *mem_strcpy (char *str);	/* malloc and copy */
char *mem_strappend (char *str1, char *str2);	/* malloc and append */
/* new JEHC 1998 */
char *mem_justpath (char *filename);    /* retain path only */
char *mem_strippath (char *filename);	/* remove path */
/* new JEHC 1997 */
char *mem_stripprefix (char *filename); /* retain suffix only */
char *mem_stripext (char *filename);	/* remove extension */
char *word_tolower (char *str);		/* upper to lower case */
typedef enum {error_val_no_error, error_val_str_not_used} error_type;
error_type check_error ();

int getc_line(FILE *file, char *string, int printit);

int load_inputfiles_from_list(char *name, char *allnames[], int *totin);


#endif
