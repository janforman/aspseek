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

/*  $Id: misc.cpp,v 1.14 2002/01/28 11:17:34 kir Exp $
    Author : Kir Kolyshkin, Matt Sullivan
*/

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include "logger.h"
#include "misc.h"

int safe_mkdir(const char *dir, mode_t mode, const char *errstr_prefix,
	int exit_on_fail)
{
	int ret;

	ret=mkdir(dir, mode);
	if (ret)
	{
		if (errno==EEXIST) // dir already exists - it's ok
			return 0;
		logger.log(CAT_FILE, L_ERR, "%s Can't create directory '%s': %s\n", errstr_prefix, dir, strerror(errno));
		if (exit_on_fail)
			exit(1);
	}
	return ret;
}


size_t fsize(int fd)
{
	struct stat st;
	if (fstat(fd, &st) < 0)
	{
		logger.log(CAT_FILE, L_ERR, "Can't stat file: %s\n", strerror(errno));
		return 0; // error
	}
	return st.st_size;
}

char *strcasestr(const char *haystack, const char *needle)
{
	char *res = NULL;
	if (haystack && needle)
	{
		int match = 0;
		char *orig_needle = (char *)needle;
		while (*haystack && *needle)
		{
			if (tolower(*needle) == tolower(*haystack))
			{
				if (!match)
				{
					res = (char *)haystack;
					match = 1;
				}
				needle++;
			}
			else if (match)
			{
				res = NULL;
				match = 0;
				needle = orig_needle;
			}
			haystack++;
		}
	}
	return (res);
}

char *str_rtrim(register char *src)
{
	if (src && src[0])
	{
		register char *p, *q;
		register int len = strlen(src);
		p = q = src;
		q += len;
		if (q > p)
		{
			q--;
			while (isspace(*q) && (q > p))
			{
				q--;
			}
			*(q + 1) = 0;
		}
	}
	return (src);
}

char *str_ltrim(register char *src)
{
	while (src && isspace(*src))
	{
		src++;
	}
	return (src);
}

char *str_trim(register char *src)
{
	src = str_ltrim(src);
	src = str_rtrim(src);
	return (src);
}
