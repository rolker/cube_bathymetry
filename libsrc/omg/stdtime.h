/*----------------------------------------------------------------------
	stdtime.h	-	Guy Carpenter	-	Jan  3, 1990

	Time format conversion routines.


Note    that years are specified in full (1989, not 89)
	that months are numbered 1-12
	that days are numbered 1-stdtime_days_in_month[m]
----------------------------------------------------------------------*/

/*#ifndef stdtime_
#define stdtime_ */

extern int stdtime_days_in_month[12];

int stdtime_from_dmy (short day, short month, short year);
short stdtime_dmy_to_jul_day (short day, short month, short year);
int stdtime_from_jul_day (short year, short day, short hour, short min, short sec);
int stdtime_nicetime (char *result, char *format, int time);

int stdtime_now_double (double *dtime);

/*#endif */
