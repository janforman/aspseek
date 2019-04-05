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

/*  $Id: filters.cpp,v 1.18 2003/01/16 16:45:42 kir Exp $
    Author : Alexander F. Avdonkin
	Uses parts of UdmSearch code
	Implemented classes: CExtFilters, CFilter, CReplacement, CReplaceVec
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "filters.h"
#include "charsets.h"
#include "sqldb.h"
#include "resolve.h"
#include "geo.h"
#include "timer.h"

CExtFilters ExtFilters;
CFilters Filters;

int AddExtFilter(char *ext, int filter_type, int reverse)
{
	CFilterBase& fb = ExtFilters[ext];
	fb.m_filter_type = filter_type;
	fb.m_reverse = reverse;
	return 0;
}

int CExtFilters::FilterType(const char * param, char * reason, int* method)
{
	char* pd = strrchr(param, '.');
	if (pd == NULL) return 1;
	pd++;
	int len = strlen(pd);
	char* ext = Alloca(len + 1);
	for (int i = 0; i < len; i++)
	{
		ext[i] = tolower(pd[i]);
	}
	ext[len] = 0;
	iterator it = find(ext);
	Freea(ext, len + 1);
	if (it != end())
	{
		CFilterBase& fb = it->second;
		switch (fb.m_filter_type)
		{
			case ALLOW:	strcpy(reason, "Allow"); break;
			case DISALLOW:	strcpy(reason, "Disallow"); break;
			case HEAD:	strcpy(reason, "CheckOnly"); break;
			default:	strcpy(reason,"Unknown"); break;
		}
		strcat(reason, fb.m_reverse ? "NoMatch" : "");
		strcat(reason, it->first.c_str());
		*method = fb.m_filter_type;
		return 0;
	}
	else
	{
		return 1;
	}
}

int CFilter::Match(const char* param, CWordCache* wcache)
{
	regmatch_t subs;
	int err = regexec(&m_filter, param, 1, &subs, 0);
	return (m_reverse == 0) == (err == 0);
}

void CFilter::AddReason(char* dest)
{
	if (m_regstr)
	{
		strcpy(dest, m_regstr);
	}
}

int CFilters::FilterType(const char * param, char * reason, CWordCache* wcache)
{
	CTimerAdd timer(m_time);
	for (iterator filter = begin(); filter != end(); filter++)
	{
		if ((*filter)->Match(param, wcache))
		{
			switch ((*filter)->m_filter_type)
			{
				case ALLOW:	strcpy(reason, "Allow"); break;
				case DISALLOW:	strcpy(reason, "Disallow"); break;
				case HEAD:	strcpy(reason, "CheckOnly"); break;
				default:	strcpy(reason,"Unknown"); break;
			}
			strcat(reason, (*filter)->m_reverse ? "NoMatch" : "");
//			strcat(reason, (*filter)->m_regstr ? (*filter)->m_regstr : "");
			(*filter)->AddReason(reason + strlen(reason));
			return (*filter)->m_filter_type;
		}
	}
	strcpy(reason,"Allow by default");
	return ALLOW;
}

// Modified by Jeff Watts on 2/22/02 to allow the use of variables in the
// replacement string.  Specify variables as \n where n is the offset of
// the regular expression you want to insert.
//
// Example:
//   Replace ^http://([^\.])\.mydomain.com http://\1.myotherdomain.com

int CReplacement::Replace(const char* string1, char* dest, int maxc)
{
	// Set a maximum of 10 possible byte offsets (registers)
	int max_offsets = 10;
	regmatch_t offsets[10];	// Allocate space for 10 byte offsets

	// Which offset (variable) have we found in the replacement string
	int o = 0;
	// The current position in the replacement string
	unsigned int pos = 0;
	string rep_string(m_replace);
	int err;
	const char* str = string1;
	int f = 0;
	char* pdest = dest;


	while ((err = regexec(&m_rfind, str, max_offsets, offsets, 0)) == 0)
	{

		// Determine if any variables are in the replacement string
		// Reset position
		pos = 0;
		// Reinitialize rep_string, so that we can replace
		// the variables for the newest match
		rep_string = m_replace;
		// Find all the backslash characters in the string
		while ((pos = rep_string.find("\\", pos)) != string::npos) {

			// If the character after the backslash is an integer,
			// replace the backslash and the integer with
			// the corresponding value in the offsets array.
			if (isdigit(rep_string[pos+1])) {
				o = rep_string[pos+1] - '0';
				rep_string.replace(pos, 2, str,
					offsets[o].rm_so,
					offsets[o].rm_eo - offsets[o].rm_so);
			// If the character after the backslash
			// is anything else, simply remove the
			// backslash and increment the position counter.
			} else {
				rep_string.replace(pos, 1, "");
				pos++;
			}
		}


		// Determine the maximum number of bytes we can copy for the
		// prefix to the matched string.  This max is limited by maxc.
		int c = min(maxc, offsets[0].rm_so);

		// Copy bytes before the match to the destination string.
		memcpy(pdest, str, c);

		// Adjust destination string pointer to end of current string
		// and adjust maxc to indicate the max number of bytes
		// we can still copy.
		pdest += c;
		maxc -= c;

		// Determine the maximum number of bytes we can copy for the
		// matched string.
		c = min(maxc, (int)rep_string.size());

		// Copy the match to the destination string
		memcpy(pdest, rep_string.c_str(), c);

		// Adjust destination pointer to the end of the string
		// and null terminate.
		// the string.
		pdest += c;
		*pdest = 0;

		// Adjust maxc to indicate the max number of bytes
		// we can still copy.
		maxc -= c;


		// Move the pointer to the original string to the end
		// of the match. This prepares us to do another replacement
		// the next time through the loop.
		str += offsets[0].rm_eo;

		// Set f to indicate that a match has been found and replaced.
		f = 1;

	}

	// If at least one change was made, we need to add
	// the suffix to the newly created string.
	if (f)
	{
		// Determine the maximum length of the suffix,
		// as governed by the size of maxc
		// (the number of characters we can still add on).
		int c = min(maxc, (int)strlen(str));

		// Copy this many characters to the destination string.
		memcpy(pdest, str, c);

		// Null terminate the destination string.
		pdest[c] = 0;
	}

	// Return 1 if a change was made, 0 otherwise
	return f;
}

int CReplaceVec::Replace(const char* string1, char* dest, int maxc)
{
	const char* str = string1;
	char* dst = (char*)alloca(maxc + 1);
	int f = 0;
	for (iterator r = begin(); r != end(); r++)
	{
		if ((*r)->Replace(str, dst, maxc))
		{
			f = 1;
			strcpy(dest, dst);
			str = dest;
		}
	}
	return f;
}

int FilterType(const char * param, char * reason, CWordCache* wcache)
{
	int method;
	if (ExtFilters.FilterType(param, reason, &method))
	{
		return Filters.FilterType(param, reason, wcache);
	}
	else
	{
		return method;
	}
}

int alnum(char* from, char* to)
{
	for (char* p = from; p < to; p++)
	{
		if (!isalnum(*p)) return 0;
	}
	return 1;
}

int AddFilter(char * filter, int filter_type, int reverse)
{
	int len = strlen(filter);
	if ((len >= 2) && (strncmp(filter, "\\.", 2) == 0) &&
		(filter[len - 1] == '$') && (alnum(filter + 2, filter + len - 1)))
	{
		char* p;
		for (p = filter + 2; p < filter + len - 1; p++)
		{
			*p = tolower(*p);
		}
		*p = 0;
		return AddExtFilter(filter + 2, filter_type, reverse);
	}
	CFilter* f = new CFilter;
	f->SetFilter(filter);
	if (f->m_regstr)
	{
		f->m_filter_type = filter_type;
		f->m_reverse = reverse;
		Filters.push_back(f);
		return 0;
	}
	else
	{
		delete f;
		return 1;
	}
}

int CCountryFilter::Match(const char* param, CWordCache* wcache)
{
	int match = 0;
	int len = strlen(param) + 1;
	char* schema = (char*)alloca(len);
	char* specific = (char*)alloca(len);
	char* hostinfo = (char*)alloca(len);
	char* path = (char*)alloca(len);
	if (sscanf(param,"%[^:]:%s", schema, specific) == 2)
	{
		char *p = schema;
		while (*p)
		{
			*p = tolower(*p); p++;
		}
		if (!strcmp(schema, "ftp") ||
		(!strcmp(schema, "http")) ||
		(!strcmp(schema, "https")) ||
		(!strcmp(schema, "news")) ||
		(!strcmp(schema, "nntp")))
		{
			switch (sscanf(specific, "//%[^/]%s", hostinfo, path))
			{
			case 1: case 2:
				{
					int len = strlen(hostinfo) + 1;
					char* auth = (char*)alloca(len);
					char* hostname = (char*)alloca(len);
					switch (sscanf(hostinfo,
						"%[^@]@%s", auth, hostname))
					{
						case 1:
							hostname = auth;
							break;
						default:
							hostname = NULL;
							break;
					}
					if (hostname)
					{
						int port;
						char* host = (char*)alloca(len);
						sscanf(hostname, "%[^:]:%d", host, &port);
						in_addr addr;
						sa_family_t family;
						int r = GetHostByName(wcache,
							host, addr, &family, 10);
						if (r == 0)
						{
							unsigned char* ipaddr = (unsigned char*)&addr.s_addr;
							char country[20];
//							const char* country = GetCountry((((((ipaddr[0] << 8) | ipaddr[1]) << 8) | ipaddr[2]) << 8) | ipaddr[3]);
							if ((GetCountry(*wcache, (((((ipaddr[0] << 8) | ipaddr[1]) << 8) | ipaddr[2]) << 8) | ipaddr[3], country) == 0)
								&& (m_countries.find(country) != m_countries.end()))
							{
								match = 1;
							}
						}
					}
				}

			}
		}

	}
	return (m_reverse == 0) == (match != 0);
}

void AddCountries(char* str)
{
	char *s, *lt;
	GetToken(str, " \t\r\n", &lt);
	CCountryFilter* f = new CCountryFilter;
	f->m_filter_type = ALLOW;
	f->m_reverse = 0;
	while ((s = GetToken(NULL, " \t\r\n",&lt)))
	{
		f->m_countries.insert(s);
	}
	if (f->m_countries.size())
	{
		Filters.push_back(f);
	}
	else
	{
		delete f;
	}
}
