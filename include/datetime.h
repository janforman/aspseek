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

/*  $Id: datetime.h,v 1.8 2002/05/14 11:47:15 kir Exp $
 *  Author: Kir Kolyshkin
 */

#ifndef _DATETIME_H_
#define _DATETIME_H_

#include <time.h>
#include "defines.h"

#define BAD_DATE -1

/** Converts string in the form dd/mm/yyyy to time_t
 */
time_t dmy2time_t(int d, int m, int y);

/** Converts day, month, year to time_t
 */
time_t dmyStr2time_t(char * time_str);

/** This should convert time returned by web server to time_t
 */
time_t httpDate2time_t(const char* date);


/** This should convert time returned by ftp server to time_t
 */
time_t ftpDate2time_t(const char* date);

/** Converts time_str to time_t (seconds)
 * time_str can be exactly number of seconds
 * or in the form 'xxxA[yyyB[zzzC]]'
 * (Spaces are allowed between xxx and A and yyy and so on)
 *   there xxx, yyy, zzz are numbers (can be negative!)
 *         A, B, C can be one of the following:
 *		s - second
 *		M - minute
 *		h - hour
 *		d - day
 *		m - month
 *		y - year
 *	(these letters are as in strptime/strftime functions)
 *
 * Examples:
 * 1234 - 1234 seconds
 * 4h30M - 4 hours and 30 minutes (will return 9000 seconds)
 * 1y6m-15d - 1 year and six month minus 15 days (will return 45792000 s)
 * 1h-60M+1s - 1 hour minus 60 minutes plus 1 second (will return 1 s)
 */
time_t tstr2time_t(char* time_str);

#endif /* _DATETIME_H_ */
