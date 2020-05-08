/*----------------------------------------------------------------------
	stdtime.c		-	Guy Carpenter	-	Jan  3, 1990

	See stdtime.h for function descriptions
----------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#ifndef __hpux
#endif
#include <time.h>
#include <ctype.h>
#include "stdtime.h"

	/* the old pre Feb 24th 2000 def  JHC */
/* #define IS_LEAP_YEAR(Y)	((!(Y%4)) && (Y%100) )*/	/* centuries? */

int IS_LEAP_YEAR(int year)
{
	if(year%4 != 0) return(0);
	else
	if(year%400 == 0) return(1);
	else
	if(year%100 == 0) return(0);
	else 
	return(1);
}

int stdtime_days_in_month[12] = {	31,28,31,30,		/* j f m a */
				31,30,31,31,		/* m j j a */
				30,31,30,31};		/* s o n d */

#define S_WEEKDAY	0
#define S_MONTH		4
#define S_DATE		8
#define S_TIME   	11
#define S_YEAR    	20

short stdtime_dmy_to_jul_day (short day, short month, short year)
{
	short jday;

	jday = day;
	if (IS_LEAP_YEAR(year) && month>2) jday++;
	while (--month) jday+=stdtime_days_in_month[month-1];
	return jday;
}

int stdtime_from_dmy (short day, short month, short year) 
{
	short dy, leap, jday;

	dy = year - 1970;
	leap = (dy+1)/4;	/* must not count year */
	/* WHAT luck! as I didn't account for the century, it
		came up with the special case of 4 centuries! */

	jday = stdtime_dmy_to_jul_day (day, month, year);

	return (dy * 365 + jday + leap - 1) * 24 * 60 * 60;
}
int stdtime_from_jul_day (short year, short day, short hour, short min, short sec)
{
	int secs;
	int leap;
	short dy;

	dy = year - 1970;
	leap = (dy+1)/4;
	/* WHAT luck! as I didn't account for the century, it
		came up with the special case of 4 centuries! */

	secs = ((((dy * 365 + day + leap - 1) * 24) + 
		hour) * 60 +
		min) * 60 + sec;
		
	return secs;
}



int stdtime_nicetime (char *result, char *format, int time)
{
	int count = 0;

	char	*timestr;		/* text string */
        struct	tm *timefld; 		/* time and date fields */
	time_t tt;
	char *p,*q,fmt[10];
	
	tt= time;
	timefld = gmtime(&tt);
	timestr = asctime(timefld);
 	timestr[3]=timestr[7]=timestr[10]=timestr[19]=timestr[24]='\0';

	p = format;
	*result = '\0';
	while (*p) {
		if (*p=='%') {
			q = fmt;
			*q++ = '%';
			while (isdigit(*++p)) *q++ = *p;	/* copy % and digits */

			switch (*p) {
				case 'W':
					*q++='s';*q++='\0';
					sprintf (result,fmt,&timestr[S_WEEKDAY]);
					p++;
					break;
				case 'M':
					*q++='s';*q++='\0';
					sprintf (result,fmt,&timestr[S_MONTH]);
					p++;
					break;
				case 'D':
					*q++='d';*q++='\0';
					sprintf (result,fmt,timefld->tm_mday);
					p++;
					break;
				case 'T':
					*q++='s';*q++='\0';
					sprintf (result,fmt,&timestr[S_TIME]);
					p++;
					break;
				case 'Y':
					*q++='s';*q++='\0';
					sprintf (result,fmt,&timestr[S_YEAR]);
					p++;
					break;
				case 'n':
					*q++='d';*q++='\0';
					sprintf (result,fmt,timefld->tm_mon+1);
					p++;
					break;
				case 'h':
					*q++='d';*q++='\0';
					sprintf (result,fmt,timefld->tm_hour);
					p++;
					break;
				case 'm':
					*q++='d';*q++='\0';
					sprintf (result,fmt,timefld->tm_min);
					p++;
					break; 
				case 'j':
					*q++='d';*q++='\0';
					sprintf (result,fmt,timefld->tm_yday+1);
					p++;
					break;
				case 's':
					*q++='d';*q++='\0';
					sprintf (result,fmt,timefld->tm_sec);
					p++;
					break;
				default:
					*q++='\0';
					sprintf (result,result,"%s",fmt);
					break;
			}
			while (*result) result++;
		} else {
			*result++ = *p++;
			*result='\0';
		}
	} /* end while */

	return(0);
}

#ifndef WIN32
int stdtime_now ()
{
	struct timezone tz;
	struct timeval tv;

	gettimeofday (&tv,&tz);

	return tv.tv_sec - tz.tz_minuteswest * 60;
}
int stdtime_now_double (double *dtime)
{
	struct timezone tz;
	struct timeval tv;

	gettimeofday (&tv,&tz);

	if(tz.tz_dsttime != 0)  tz.tz_minuteswest -= 60;
	*dtime =  (double)(tv.tv_sec - (tz.tz_minuteswest * 60))
			+((double)tv.tv_usec/1000000.0);
	return tv.tv_sec - tz.tz_minuteswest * 60;
}
#endif

