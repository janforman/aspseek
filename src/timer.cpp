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

/*  $Id: timer.cpp,v 1.4 2002/05/14 11:32:02 kir Exp $
    Author : Alexander F. Avdonkin
*/

#include <sys/time.h>
#include "timer.h"

time_t now(void)
{
	time_t tloc;
	return time(&tloc);
}

double timedif(struct timeval& tm1, struct timeval& tm)
{
	return (tm1.tv_sec - tm.tv_sec) + (tm1.tv_usec - tm.tv_usec) / 1000000.0;
}
