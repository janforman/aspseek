/* Copyright (C) 2000, 2001, 2002 by SWsoft
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*  $Id: datetime.cpp,v 1.14 2002/05/24 15:44:47 kir Exp $
 *  Author: Kir Kolyshkin
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "defines.h"
#include "datetime.h"

/* Function to convert date returned by web-server to time_t
 *
 * Earlier we used strptime, but it seems to be completely broken
 * on Solaris 2.6. Thanks to Maciek Uhlig <muhlig@us.edu.pl> for pointing
 * out this and bugfix proposal (to use Apache's ap_parseHTTPdate).
 *
 * 10 July 2000 kir.
 */


/*****
 *  BEGIN: Below is taken from Apache's util_date.c
 */

/* ====================================================================
 * Copyright (c) 1996-1999 The Apache Group.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the Apache Group
 *    for use in the Apache HTTP server project (http://www.apache.org/)."
 *
 * 4. The names "Apache Server" and "Apache Group" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache"
 *    nor may "Apache" appear in their names without prior written
 *    permission of the Apache Group.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the Apache Group
 *    for use in the Apache HTTP server project (http://www.apache.org/)."
 *
 * THIS SOFTWARE IS PROVIDED BY THE APACHE GROUP ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE APACHE GROUP OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Group and was originally based
 * on public domain software written at the National Center for
 * Supercomputing Applications, University of Illinois, Urbana-Champaign.
 * For more information on the Apache Group and the Apache HTTP server
 * project, please see <http://www.apache.org/>.
 *
 */

/********
 * BEGIN
 * This is taken from ap_ctype.h
 * --kir.
 */

#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

/* These macros allow correct support of 8-bit characters on systems which
 * support 8-bit characters.  Pretty dumb how the cast is required, but
 * that's legacy libc for ya.  These new macros do not support EOF like
 * the standard macros do.  Tough.
 */
#define ap_isalnum(c) (isalnum(((unsigned char)(c))))
#define ap_isalpha(c) (isalpha(((unsigned char)(c))))
#define ap_iscntrl(c) (iscntrl(((unsigned char)(c))))
#define ap_isdigit(c) (isdigit(((unsigned char)(c))))
#define ap_isgraph(c) (isgraph(((unsigned char)(c))))
#define ap_islower(c) (islower(((unsigned char)(c))))
#define ap_isprint(c) (isprint(((unsigned char)(c))))
#define ap_ispunct(c) (ispunct(((unsigned char)(c))))
#define ap_isspace(c) (isspace(((unsigned char)(c))))
#define ap_isupper(c) (isupper(((unsigned char)(c))))
#define ap_isxdigit(c) (isxdigit(((unsigned char)(c))))
#define ap_tolower(c) (tolower(((unsigned char)(c))))
#define ap_toupper(c) (toupper(((unsigned char)(c))))

#ifdef __cplusplus
}
#endif

/*******
 * END of taken from ap_ctype.h
 */

/*
 * Compare a string to a mask
 * Mask characters (arbitrary maximum is 256 characters, just in case):
 *   @ - uppercase letter
 *   $ - lowercase letter
 *   & - hex digit
 *   # - digit
 *   ~ - digit or space
 *   * - swallow remaining characters
 *  <x> - exact match for any other character
 */
int ap_checkmask(const char *data, const char *mask)
{
    int i;
    char d;

    for (i = 0; i < 256; i++) {
	d = data[i];
	switch (mask[i]) {
	case '\0':
	    return (d == '\0');

	case '*':
	    return 1;

	case '@':
	    if (!ap_isupper(d))
		return 0;
	    break;
	case '$':
	    if (!ap_islower(d))
		return 0;
	    break;
	case '#':
	    if (!ap_isdigit(d))
		return 0;
	    break;
	case '&':
	    if (!ap_isxdigit(d))
		return 0;
	    break;
	case '~':
	    if ((d != ' ') && !ap_isdigit(d))
		return 0;
	    break;
	default:
	    if (mask[i] != d)
		return 0;
	    break;
	}
    }
    return 0;			/* We only get here if mask is corrupted (exceeds 256) */
}

/*
 * tm2sec converts a GMT tm structure into the number of seconds since
 * 1st January 1970 UT.  Note that we ignore tm_wday, tm_yday, and tm_dst.
 *
 * The return value is always a valid time_t value -- (time_t)0 is returned
 * if the input date is outside that capable of being represented by time(),
 * i.e., before Thu, 01 Jan 1970 00:00:00 for all systems and
 * beyond 2038 for 32bit systems.
 *
 * This routine is intended to be very fast, much faster than mktime().
 */
time_t ap_tm2sec(const struct tm * t)
{
    int year;
    time_t days;
    static const int dayoffset[12] =
    {306, 337, 0, 31, 61, 92, 122, 153, 184, 214, 245, 275};

    year = t->tm_year;

    if (year < 70 || ((sizeof(time_t) <= 4) && (year >= 138)))
	return BAD_DATE;

    /* shift new year to 1st March in order to make leap year calc easy */

    if (t->tm_mon < 2)
	year--;

    /* Find number of days since 1st March 1900 (in the Gregorian calendar). */

    days = year * 365 + year / 4 - year / 100 + (year / 100 + 3) / 4;
    days += dayoffset[t->tm_mon] + t->tm_mday - 1;
    days -= 25508;		/* 1 jan 1970 is 25508 days since 1 mar 1900 */

    days = ((days * 24 + t->tm_hour) * 60 + t->tm_min) * 60 + t->tm_sec;

    if (days < 0)
	return BAD_DATE;	/* must have overflowed */
    else
	return days;		/* must be a valid time */
}

/*
 * Parses an HTTP date in one of three standard forms:
 *
 *     Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
 *     Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
 *     Sun Nov  6 08:49:37 1994       ; ANSI C's asctime() format
 *
 * and returns the time_t number of seconds since 1 Jan 1970 GMT, or
 * 0 if this would be out of range or if the date is invalid.
 *
 * The restricted HTTP syntax is
 *
 *     HTTP-date    = rfc1123-date | rfc850-date | asctime-date
 *
 *     rfc1123-date = wkday "," SP date1 SP time SP "GMT"
 *     rfc850-date  = weekday "," SP date2 SP time SP "GMT"
 *     asctime-date = wkday SP date3 SP time SP 4DIGIT
 *
 *     date1        = 2DIGIT SP month SP 4DIGIT
 *                    ; day month year (e.g., 02 Jun 1982)
 *     date2        = 2DIGIT "-" month "-" 2DIGIT
 *                    ; day-month-year (e.g., 02-Jun-82)
 *     date3        = month SP ( 2DIGIT | ( SP 1DIGIT ))
 *                    ; month day (e.g., Jun  2)
 *
 *     time         = 2DIGIT ":" 2DIGIT ":" 2DIGIT
 *                    ; 00:00:00 - 23:59:59
 *
 *     wkday        = "Mon" | "Tue" | "Wed"
 *                  | "Thu" | "Fri" | "Sat" | "Sun"
 *
 *     weekday      = "Monday" | "Tuesday" | "Wednesday"
 *                  | "Thursday" | "Friday" | "Saturday" | "Sunday"
 *
 *     month        = "Jan" | "Feb" | "Mar" | "Apr"
 *                  | "May" | "Jun" | "Jul" | "Aug"
 *                  | "Sep" | "Oct" | "Nov" | "Dec"
 *
 * However, for the sake of robustness (and Netscapeness), we ignore the
 * weekday and anything after the time field (including the timezone).
 *
 * This routine is intended to be very fast; 10x faster than using sscanf.
 *
 * Originally from Andrew Daviel <andrew@vancouver-webpages.com>, 29 Jul 96
 * but many changes since then.
 *
 */
time_t httpDate2time_t(const char *date){
    struct tm ds;
    int mint, mon;
    const char *monstr, *timstr;
    static const int months[12] =
    {
	('J' << 16) | ('a' << 8) | 'n', ('F' << 16) | ('e' << 8) | 'b',
	('M' << 16) | ('a' << 8) | 'r', ('A' << 16) | ('p' << 8) | 'r',
	('M' << 16) | ('a' << 8) | 'y', ('J' << 16) | ('u' << 8) | 'n',
	('J' << 16) | ('u' << 8) | 'l', ('A' << 16) | ('u' << 8) | 'g',
	('S' << 16) | ('e' << 8) | 'p', ('O' << 16) | ('c' << 8) | 't',
	('N' << 16) | ('o' << 8) | 'v', ('D' << 16) | ('e' << 8) | 'c'};

    if (!date)
	return BAD_DATE;

    while (*date && ap_isspace(*date))	/* Find first non-whitespace char */
	++date;

    if (*date == '\0')
	return BAD_DATE;

    if ((date = strchr(date, ' ')) == NULL)	/* Find space after weekday */
	return BAD_DATE;

    ++date;			/* Now pointing to first char after space, which should be */
    /* start of the actual date information for all 3 formats. */

    if (ap_checkmask(date, "## @$$ #### ##:##:## *")) {	/* RFC 1123 format */
	ds.tm_year = ((date[7] - '0') * 10 + (date[8] - '0') - 19) * 100;
	if (ds.tm_year < 0)
	    return BAD_DATE;

	ds.tm_year += ((date[9] - '0') * 10) + (date[10] - '0');

	ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

	monstr = date + 3;
	timstr = date + 12;
    }
    else if (ap_checkmask(date, "##-@$$-## ##:##:## *")) {		/* RFC 850 format  */
	ds.tm_year = ((date[7] - '0') * 10) + (date[8] - '0');
	if (ds.tm_year < 70)
	    ds.tm_year += 100;

	ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

	monstr = date + 3;
	timstr = date + 10;
    }
    else if (ap_checkmask(date, "@$$ ~# ##:##:## ####*")) {	/* asctime format  */
	ds.tm_year = ((date[16] - '0') * 10 + (date[17] - '0') - 19) * 100;
	if (ds.tm_year < 0)
	    return BAD_DATE;

	ds.tm_year += ((date[18] - '0') * 10) + (date[19] - '0');

	if (date[4] == ' ')
	    ds.tm_mday = 0;
	else
	    ds.tm_mday = (date[4] - '0') * 10;

	ds.tm_mday += (date[5] - '0');

	monstr = date;
	timstr = date + 7;
    }
    else
	return BAD_DATE;

    if (ds.tm_mday <= 0 || ds.tm_mday > 31)
	return BAD_DATE;

    ds.tm_hour = ((timstr[0] - '0') * 10) + (timstr[1] - '0');
    ds.tm_min = ((timstr[3] - '0') * 10) + (timstr[4] - '0');
    ds.tm_sec = ((timstr[6] - '0') * 10) + (timstr[7] - '0');

    if ((ds.tm_hour > 23) || (ds.tm_min > 59) || (ds.tm_sec > 61))
	return BAD_DATE;

    mint = (monstr[0] << 16) | (monstr[1] << 8) | monstr[2];
    for (mon = 0; mon < 12; mon++)
	if (mint == months[mon])
	    break;
    if (mon == 12)
	return BAD_DATE;

    if ((ds.tm_mday == 31) && (mon == 3 || mon == 5 || mon == 8 || mon == 10))
	return BAD_DATE;

    /* February gets special check for leapyear */

    if ((mon == 1) &&
	((ds.tm_mday > 29)
	 || ((ds.tm_mday == 29)
	     && ((ds.tm_year & 3)
		 || (((ds.tm_year % 100) == 0)
		     && (((ds.tm_year % 400) != 100)))))))
	return BAD_DATE;

    ds.tm_mon = mon;

    /* printf("parseHTTPdate: %s is %lu\n", date, ap_tm2sec(&ds)); */

    return ap_tm2sec(&ds);
}

/********
 * END: Above is taken from Apache's util_date.c
 */

time_t ftpDate2time_t(char *date){
	struct tm ds;

	if (!(ap_checkmask(date+4, "##############*")))
		return BAD_DATE;

/*        strptime(date+6, "%y%m%d%H%M%S", &tm_s); */

	ds.tm_year = (((date[4] - '0') * 10) + (date[5] - '0') - 19)*100;
	ds.tm_year += ((date[6] - '0') * 10) + (date[7] - '0');

	ds.tm_mon = ((date[8] - '0') * 10) + (date[9] - '0') - 1;
	ds.tm_mday = ((date[10] - '0') * 10) + (date[11] - '0');

	ds.tm_hour = ((date[12] - '0') * 10) + (date[13] - '0');
	ds.tm_min = ((date[14] - '0') * 10) + (date[15] - '0');
	ds.tm_sec = ((date[16] - '0') * 10) + (date[17] - '0');

	/* printf("parseFTPdate: %s is %lu\n", date, ap_tm2sec(&ds)); */

	return ap_tm2sec(&ds);
}

time_t tstr2time_t(char * time_str){
	time_t t=0;
	long i;
	char *s, *ts;

	/* flag telling us that time_str is exactly <num> without any char
	 * so we think it's seconds
	 * flag==0 means not defined yet
	 * flag==1 means ordinary format (XXXmYYYs)
	 * flag==2 means seconds only
	 */
	int flag=0;

	ts=time_str;
	do{
		i=strtol(ts, &s, 10);
		if (s==ts){ /* failed to find a number */
			return BAD_DATE;
		}

		/* ignore spaces */
		while (isspace(*s))
			s++;

		switch(*s){
			case 's': /* seconds */
				t+=i; flag=1; break;
			case 'M': /* minutes */
				t+=i*60; flag=1; break;
			case 'h': /* hours */
				t+=i*60*60; flag=1; break;
			case 'd': /* days */
				t+=i*60*60*24; flag=1; break;
			case 'm': /* months */
				/* I assume month is 30 days */
				t+=i*60*60*24*30; flag=1; break;
			case 'y': /* years */
				t+=i*60*60*24*365; flag=1; break;
			case 0: /* end of string */
				if (flag==1)
					return BAD_DATE;
				else{
					t=i;
					flag=2;
					return t;
				}
			default:
				return BAD_DATE;
		}
		/* go to next char */
		ts=++s;
	/* is it end? */
	} while (*s);

	return t;
}

time_t dmy2time_t(int d, int m, int y){
	struct tm t;

	memset((void *)&t, 0, sizeof(t));
	t.tm_mday=d;
	t.tm_mon=m;
	t.tm_year=y-1900;
	return ap_tm2sec(&t);
}

time_t dmyStr2time_t(char *str){
	struct tm t;
	char *cp, *cp2;

	memset((void *)&t, 0, sizeof(t));
	// str is dd/mm/yyyy
	t.tm_mday=(int)strtol(str, &cp, 10);
	if ((cp == str) || (*cp != '/') || (t.tm_mday > 31) )	// some weirdness
		return BAD_DATE;
	t.tm_mon=(int)strtol(++cp, &cp2, 10) - 1; // months are from 0
	if ((cp2 == cp) || (*cp2 != '/') || (t.tm_mon > 11) )	// some weirdness
		return BAD_DATE;
	t.tm_year=(int)strtol(++cp2, &cp, 10) - 1900; // years are from 1900
	if (cp == cp2)
		return BAD_DATE;

	return ap_tm2sec(&t);
}
