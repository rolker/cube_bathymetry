/*----------------------------------------------------------------------
	support.c		-	Guy Carpenter	-	Oct 18, 1989

	See support.h for function descriptions


	with various generla purpose utility functions added
	by JHC over the years.
----------------------------------------------------------------------*/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
/* #include <strings.h> */
#include <string.h>
#include <stdarg.h>
#include "support.h"

int ARGC = 0;
char **ARGV = 0;

typedef struct cleanup_rec {
	cleanup_fn		*fn;
	struct cleanup_rec	*next;	
} cleanup_rec;

cleanup_rec *cleanup_list = 0;

error_type error_val = error_val_no_error;

void warning (char *msg, ...)   
{
	va_list pvar;
	va_start(pvar, msg);
	fflush (stdout);
	if (msg) {
		fprintf (stderr, "Warning : ");
		vfprintf (stderr, msg, pvar);
		fprintf (stderr, "\n");
	}
	va_end(pvar);
	fflush (stderr);
}

void error (char *msg, ...) 
{
	va_list pvar;
	va_start(pvar, msg);
	fflush (stdout);
	if (msg) {
		fprintf (stderr, "Error : ");
		vfprintf (stderr, msg, pvar);
		fprintf (stderr, "\n");
	}
	va_end(pvar);
	fflush (stderr);
	cleanup ();
	exit (1);
}
/*----------------------------------------------------------------------
	cleanup_register
		Register a routine to be called on cleanup
----------------------------------------------------------------------*/
void cleanup_register (cleanup_fn *fn)
{
	cleanup_rec *p;
	p = CREATE(cleanup_rec);
	p->fn = fn;
	p->next = cleanup_list;
	cleanup_list = p;
}
/*----------------------------------------------------------------------
	cleanup
		Call any cleanup routines and return
----------------------------------------------------------------------*/
void cleanup ()
{
	cleanup_rec *p,*q;
	p = cleanup_list;
	while (p) {
		q = p->next;
		(*p->fn)();
		free (p);
		p = q;
	}
	cleanup_list = 0;
}

void usage (char *usage, char *msg, ...)
{
	va_list pvar;
	fflush (stdout);
	if (ARGV) fprintf (stderr, "Usage : %s %s\n",ARGV[0],usage);
	va_start(pvar, msg);
	if (msg) {
		fprintf (stderr, "(");
		vfprintf (stderr, msg, pvar);
		fprintf (stderr, ")\n");
	}
	va_end(pvar);
	fflush (stderr);
	cleanup ();
	exit (1);
}

error_type check_error ()	/* check & clear error flag */
{
	error_type err = error_val;
	error_val = error_val_no_error;
	return err;
}

double rounddown (double n, double mod)
{
	double ru;
	ru = ((int)(n/mod))*mod;
	if (ru > n) ru-=mod;
	return ru;
}

double roundup (double n, double mod)
{
	double ru;
	ru = ((int)(n/mod))*mod;
	if (ru < n) ru+=mod;
	return ru;
}

double roundupforce (double n, double mod)
{
	double ru = roundup (n,mod);
	if ((ru-n) < (mod/2)) ru+=mod;
	return ru;
}

boolean isinstr (char c, char *s)
{
	while (*s) if (*s++==c) return true;
	return false;
}

int fieldtoint (char *s, int len)
{
	int i=0;
	int sign = 1;

	for (;len && isspace(*s);s++,len--);
	if (!len) return 0;

	if (*s=='-') {sign= -1;s++;len--;}	/* process optional sign */
	else if (*s=='+') {s++;len--;}

        while (len && isdigit(*s)) {
		i = i * 10 + (*s++-'0');
		len--;
	}

	if (len) error_val = error_val_str_not_used;

	return sign*i;
}

double fieldtodbl (char *s, int len)
{
	int whole=0,dec=0;
	int sign = 1;
	double places = 1;

	for (;len && isspace(*s);s++,len--);
	if (!len) return 0.0;

	if (*s=='-') {sign= -1;s++;len--;}	/* process optional sign */
	else if (*s=='+') {s++;len--;}
						/* process whole part */
        while (len && isdigit(*s)) {
		whole = whole * 10 + (*s++-'0');
		len--;
	}

	if (len && *s=='.') {
		s++; len--;
		while (len && isdigit(*s) ) {
			dec = dec * 10 + (*s++-'0');
			len--;
			places*=10.0;
		}
	}
			
	if (len) error_val = error_val_str_not_used;

	return sign * ( whole + (double)dec / places );
}

int strtoint (char *s)
{
	int i=0;
	int sign = 1;

	for (;isspace(*s);s++);

	if (*s=='-') {sign= -1;s++;}	/* process optional sign */
	else if (*s=='+') s++;

        while (isdigit(*s))
		i = i * 10 + (*s++-'0');

	if (*s) error_val = error_val_str_not_used;

	return sign*i;
}

double strtodbl (char *s)
{
	double d=0.0, f=1;
	int sign = 1;

	for (;isspace(*s);s++);		/* skip whitespace */

	if (*s=='-') {sign= -1;s++;}	/* process optional sign */
	else if (*s=='+') s++;

        while (isdigit(*s))		/* process whole number */
		d = d * 10 + (*s++-'0');
	if (*s=='.') {
		s++;
		while (isdigit(*(s))) {	/* and fraction */
			f = f / 10;
			d = d + (*s++-'0')*f;
		}
	}

	if (*s) error_val = error_val_str_not_used;

	return sign*d;
}

/*----------------------------------------------------------------------
strtoangle
	like strtodbl/strtoint except accepts the following formats
		nnn		Whole degrees
		nnn.ddd		Decimal degrees
		nnn'mm		Degrees and minutes
		[-|+][nnn][.nnn|'nn][n|N|s|S]
----------------------------------------------------------------------*/
/* OLD STRING TO ANGLE CONVERSION WORKED ....
	double strtoangle (char *s)
	{
		char *p;
		int i;
		double a;
	
		for (p=s,i=0;*p;p++,i++) if (*p=='\'' || *p=='^') {
			a = (double)fieldtoint (s,i);
			a += (double)strtoint(++p)/60.0 * (a<0 ? -1.0 : 1.0);
			return a;
		}
		return strtodbl (s);
	}
   END OF COMMENTED OUT CODE */
double strtoangle (char *s)
{
	int i=0,j=0;
	float f=0.0,n;
	int sign = 1;

/* gobble white space */

	for (;isspace(*s);s++);

/* optional leading sign */

	if (*s=='-') {sign= -1;s++;}
	else if (*s=='+') s++;

/* optional leading whole angle */

        while (isdigit(*s))
		i = i * 10 + (*s++-'0');

/* optional trailing decimal degrees or minutes */

	if (*s=='\'' || *s=='^') {	/* minutes */
		s++;
		while (isdigit(*s))
		    j = j * 10 + (*s++-'0');
		f = (float)j / 60.0;
		if(*s=='.') {
		  s++; n=1.0; j=0;	
		  while (isdigit(*s)) {
		    j = j * 10 + (*s++-'0');
		    n = n * 10.0;
		  }
		  f = f + (float)j / (n*60.0);
		}
	} else if (*s=='.') {		/* decimal degrees */
		s++; n=1.0;
		while (isdigit(*s)) {
		    j = j * 10 + (*s++-'0');
		    n = n * 10.0;
		}
		f = (float)j / n;
	}

/* optional n/s/N/S sign difference on the end */

	if (isinstr (*s,"nNeE")) s++;
	else if (isinstr (*s,"sSwW")) {sign *= -1;s++;}

	if (*s) error_val = error_val_str_not_used;

	return ((float)i + f) * (float)sign;
}

char *mem_strcpy (char *str)
{
	char *str2;
	str2 = (char *)malloc (strlen(str)+1);
	strcpy (str2,str);
	return str2;
}

char *mem_strappend (char *str1, char *str2)
{
	char *str3;
	str3 = (char *)malloc (strlen(str1)+strlen(str2)+1);
	strcpy (str3,str1);
	strcat (str3,str2);
	return str3;
}


/* new JEHC 1998 */
char *mem_justpath (char *filename)
{
	char *p, *copy, *mark, hold;

	p = filename;
	mark = 0l;
	while (*p) {
		if (*p=='/') mark = p;	/* mark last period */
		p++;
	}

	/* keep the slash ... */
	if (mark) {
		hold = *(mark+1);
		*(mark+1)='\0';
	}
	copy = mem_strcpy (filename);
	*(mark+1) = hold;

	return copy;
}
char *mem_strippath (char *filename)
{
	char *base, *p;

	p = base = filename;
	while (*p) {
		if (*p=='/') base=p+1;
		p++;
	}

	return mem_strcpy (base);
}
/* new JEHC 1997 */
char *mem_stripprefix (char *filename)
{
	char *base, *p;

	p = base = filename;
	while (*p) {
		if (*p=='.') base=p+1;
		p++;
	}

	return mem_strcpy (base);
}
char *mem_stripext (char *filename)
{
	char *p, *copy, *mark;

	p = filename;
	mark = 0l;
	while (*p) {
		if (*p=='.') mark = p;	/* mark last period */
		p++;
	}

	if (mark) *mark='\0';
	copy = mem_strcpy (filename);
	*mark = '.';

	return copy;
}

char *word_tolower (char *str)
{
	char *word = str;
	if (word) {
		while (*word) {
			*word=TOLOWER(*word);
			word++;
		}
	}
	return str;
}
int getc_line(FILE *file, char *string, int printit)
{
int c, nochars;

           nochars=0;
           while((c=getc(file)) != '\n' ) {
                if( c == EOF) return(-1);
                *(string+nochars) = c;
                nochars++;
           }
          *(string+nochars)= '\0';

        if(printit) printf("%s\n", string);

        return(nochars);
}


/*-------------------------------------------------------------*/
/* 
	general utility,
		given a file name that contains a lost of files,
		add all the uncommented (#) filenames to an
		array of names, iterating a count for each valid filename.

	used by swathed, weigh_grid and mos2
*/
/*-------------------------------------------------------------*/
int load_inputfiles_from_list(char *name, char *allnames[], int *totin)
{
FILE *file;
char *path;
char linestring[256], restofname[80];
int nochars, novalid=0;

        file = fopen(name,"r");
        if(!file) return(-1);

        path = mem_justpath(name);
        printf(" using path %s \n", path);

        while(nochars = getc_line(file, linestring, 0) != -1 ) {
         if(nochars) {
         if(linestring[0] != '#') {
                sscanf(linestring,"%s",restofname);

                allnames[*totin] = mem_strappend(path,restofname);
                printf(" adding %d %s\n", *totin, allnames[*totin]);
                *totin +=1;
                novalid++;
         } else {
                printf(" skipping comment in %s -- (%d) %s\n",
                        name, nochars,linestring);
         }
         } else {
           printf("skipping blank line in list file\n");
         }
        }


        fclose(file);

        return(novalid);
}

/*-------------------------------------------------------------*/


