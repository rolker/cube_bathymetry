/*----------------------------------------------------------------------

	swapio.c	-	John Hughes Clarke	-	March , 1996

	subroutines for byte swapping code.

----------------------------------------------------------------------*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "swapio.h"





/* ----------------------- */
 void get_swapped_short(char *data, short *anint)
{
short theint;

	if(INTEL && !MOTOROLA) {
	memcpy(&theint, data, 2);
	*anint = swap_short(theint);
	} else {
	memcpy(anint, data, 2);
	}
}
/* ----------------------- */
/* ----------------------- */
 void put_swapped_short(char *data, short *anint)
{
short theint;

	if(INTEL && !MOTOROLA) {
	theint = swap_short(*anint);
	memcpy(data, &theint, 2);
	} else {
	memcpy(data, anint, 2);
	}
}
/* ----------------------- */
/* the next pair purely for consistency (no swapping really required) */
/* ----------------------- */
 void put_swapped_char(char *data, char *achar, int count)
{
	memcpy(data, achar, count);
}

/* ----------------------- */
 void get_swapped_char(char *data, char *achar, int count)
{
	memcpy(achar, data, count);
}


/* ----------------------- */
 void get_swapped_int(char *data, int *anint)
{
int theint;

	if(INTEL && !MOTOROLA) {
	memcpy(&theint, data, 4);
	*anint = swap_int(theint);
	} else {
	memcpy(anint, data, 4);
	}
}
/* ----------------------- */
/* ----------------------- */
 void put_swapped_int(char *data, int *anint)
{
int theint;

	if(INTEL && !MOTOROLA) {
	theint = swap_int(*anint);
	memcpy(data, &theint, 4);
	} else {
	memcpy(data, anint, 4);
	}
}
/* ----------------------- */

/* ----------------------- */
 void get_swapped_float(char *data, float *afloat)
{
float thefloat;

	if(INTEL && !MOTOROLA) {
	memcpy(&thefloat, data, 4);
	*afloat = swap_float(thefloat);
	} else {
	memcpy(afloat, data, 4);
	}
}
/* ----------------------- */
/* ----------------------- */
 void put_swapped_float(char *data, float *afloat)
{
float thefloat;

	if(INTEL && !MOTOROLA) {
	thefloat = swap_float(*afloat);
	memcpy(data, &thefloat, 4);
	} else {
	memcpy(data, afloat, 4);
	}
}
/* ----------------------- */


/* ----------------------- */
 void get_swapped_double(char *data, double *adouble)
{
double thedouble;

	if(INTEL && !MOTOROLA) {
	memcpy(&thedouble, data, 8);
	*adouble = swap_double(thedouble);
	} else {
	memcpy(adouble, data, 8);
	}
}
/* ----------------------- */
/* ----------------------- */
 void put_swapped_double(char *data, double *adouble)
{
double thedouble;

	if(INTEL && !MOTOROLA) {
	thedouble = swap_double(*adouble);
	memcpy(data, &thedouble, 8);
	} else {
	memcpy(data, adouble, 8);
	}
}
/* ----------------------- */


/* ----------------------- */
/* ----------------------- */
short swap_short(short value)
{
union{
        short two;
        unsigned char  one[2];
                } equiv;
unsigned char    temp;

        equiv.two = value;
        temp = equiv.one[0];
        equiv.one[0] = equiv.one[1];
        equiv.one[1] = temp;

        return(equiv.two);
}

/* -------------------------- */
/* ----------------------- */
int swap_int(int value)
{
union{
        int four;
        unsigned char  one[4];
                } equiv;
unsigned char    temp;

        equiv.four = value;

        temp = equiv.one[0];
        equiv.one[0] = equiv.one[3];
        equiv.one[3] = temp;

        temp = equiv.one[1];
        equiv.one[1] = equiv.one[2];
        equiv.one[2] = temp;

        return(equiv.four);
}

/* -------------------------- */
/* ----------------------- */
float swap_float(float value)
{
union{
        float four;
        unsigned char  one[4];
                } equiv;
unsigned char    temp;

        equiv.four = value;

        temp = equiv.one[0];
        equiv.one[0] = equiv.one[3];
        equiv.one[3] = temp;

        temp = equiv.one[1];
        equiv.one[1] = equiv.one[2];
        equiv.one[2] = temp;

        return(equiv.four);
}

/* -------------------------- */
/* ----------------------- */
double swap_double(double value)
{
union{
        double dbl;
        unsigned char  one[8];
                } equiv;
unsigned char    temp;

        equiv.dbl = value;

        temp = equiv.one[0];
        equiv.one[0] = equiv.one[7];
        equiv.one[7] = temp;

        temp = equiv.one[1];
        equiv.one[1] = equiv.one[6];
        equiv.one[6] = temp;

        temp = equiv.one[2];
        equiv.one[2] = equiv.one[5];
        equiv.one[5] = temp;

        temp = equiv.one[3];
        equiv.one[3] = equiv.one[4];
        equiv.one[4] = temp;

        return(equiv.dbl);
}

/* -------------------------- */

size_t jswapfloat_fwrite(float *pointer, size_t size, size_t num_items,
			FILE *stream)
{
int i;
size_t rc;
	if(INTEL && !MOTOROLA) 
	for(i=0;i<(size*num_items)/sizeof(float);i++) 
		*(pointer+i) = swap_float(*(pointer+i));


	rc = fwrite(pointer, size, num_items, stream);

/* swapping back in case we want to use it some more */

	if(INTEL && !MOTOROLA) 
	for(i=0;i<(size*num_items)/sizeof(float);i++) 
		*(pointer+i) = swap_float(*(pointer+i));

	return(rc);

}
/* -------------------------- */

size_t jswapfloat_fread(float *pointer, size_t size, size_t num_items,
			FILE *stream)
{
int i;
size_t rc;

	rc = fread(pointer, size, num_items, stream);


	if(INTEL && !MOTOROLA) 
	for(i=0;i<(size*num_items)/sizeof(float);i++) 
		*(pointer+i) = swap_float(*(pointer+i));

	return(rc);

}
/* -------------------------- */
/* -------------------------- */

size_t jswapint_fwrite(int *pointer, size_t size, size_t num_items,
			FILE *stream)
{
int i;
size_t rc;
	if(INTEL && !MOTOROLA) 
	for(i=0;i<(size*num_items)/sizeof(int);i++) 
		*(pointer+i) = swap_int(*(pointer+i));


	rc = fwrite(pointer, size, num_items, stream);

/* swapping back in case we want to use it some more */

	if(INTEL && !MOTOROLA) 
	for(i=0;i<(size*num_items)/sizeof(int);i++) 
		*(pointer+i) = swap_int(*(pointer+i));

	return(rc);

}
/* -------------------------- */

size_t jswapint_fread(int *pointer, size_t size, size_t num_items,
			FILE *stream)
{
int i;
size_t rc;

	rc = fread(pointer, size, num_items, stream);


	if(INTEL && !MOTOROLA) 
	for(i=0;i<(size*num_items)/sizeof(int);i++) 
		*(pointer+i) = swap_int(*(pointer+i));

	return(rc);

}
/* -------------------------- */

/* -------------------------- */

size_t jswapshort_fwrite(short *pointer, size_t size, size_t num_items,
			FILE *stream)
{
int i;
size_t rc;
	if(INTEL && !MOTOROLA) 
	for(i=0;i<(size*num_items)/sizeof(short);i++) 
		*(pointer+i) = swap_short(*(pointer+i));


	rc = fwrite(pointer, size, num_items, stream);

/* swapping back in case we want to use it some more */

	if(INTEL && !MOTOROLA) 
	for(i=0;i<(size*num_items)/sizeof(short);i++) 
		*(pointer+i) = swap_short(*(pointer+i));

	return(rc);

}
/* -------------------------- */

size_t jswapshort_fread(short *pointer, size_t size, size_t num_items,
			FILE *stream)
{
int i;
size_t rc;

	rc = fread(pointer, size, num_items, stream);


	if(INTEL && !MOTOROLA) 
	for(i=0;i<(size*num_items)/sizeof(short);i++) 
		*(pointer+i) = swap_short(*(pointer+i));

	return(rc);

}
/* -------------------------- */


