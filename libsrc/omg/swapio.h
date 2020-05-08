/*----------------------------------------------------------------------

	swapio.h	-	John Hughes Clarke	-	March , 1996

	include file for  byte swapping applications

----------------------------------------------------------------------*/

#ifndef swapio_
#define swapio_

/* #include <sys/stdtypes.h>*/ /* to get size_t */
#include <stdio.h>
#include <ctype.h>
#ifndef MAXDOUBLE
#include <math.h>
#endif


/*
#if defined(sun)
#include <sys/stdtypes.h> 
#endif
*/

#if defined(__alpha) || defined(ultrix) || defined(__ultrix) || defined(i386)
#define INTEL 1
#else
#define INTEL 0
#endif

#if defined(mc68000) || defined(sony) || defined(sgi) || defined(sun)
#define MOTOROLA 1
#else
#define MOTOROLA 0
#endif

/*
#define MOTOROLA 1
#define INTEL 0
*/

void get_swapped_int(char *data, int *anint);
void put_swapped_int(char *data, int *anint);

void put_swapped_char(char *data, char *achar, int count);
void get_swapped_char(char *data, char *achar, int count);

void get_swapped_short(char *data, short *sht);
void put_swapped_short(char *data, short *sht);

void get_swapped_float(char *data, float *afloat);
void put_swapped_float(char *data, float *afloat);

void get_swapped_double(char *data, double *adouble);
void put_swapped_double(char *data, double *adouble);

float swap_float(float value);
int swap_int(int value);
short swap_short(short value);
double swap_double(double value);

size_t jswapfloat_fwrite(float *pointer, size_t size, size_t num_items,
                        FILE *stream);
size_t jswapfloat_fread(float *pointer, size_t size, size_t num_items,
                        FILE *stream);

size_t jswapint_fwrite(int *pointer, size_t size, size_t num_items,
                        FILE *stream);
size_t jswapint_fread(int *pointer, size_t size, size_t num_items,
                        FILE *stream);

size_t jswapshort_fwrite(short *pointer, size_t size, size_t num_items,
                        FILE *stream);
size_t jswapshort_fread(short *pointer, size_t size, size_t num_items,
                        FILE *stream);

#endif
