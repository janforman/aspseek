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

/*  $Id: filters.h,v 1.15 2004/03/22 13:16:24 kir Exp $
    Author : Alexander F. Avdonkin
	Uses parts of UdmSearch code
*/

#ifndef _FILTERS_H_
#define _FILTERS_H_

#include <stdio.h>
#include <vector>
#include <string>
#include <sys/types.h>
#include <regex.h>
#include "defines.h"
#include "config.h"
#include "maps.h"
#include "misc.h"

using std::vector;
using std::string;

class CWordCache;

class CFilterBase
{
public:
	int	m_filter_type;
	int	m_reverse;

public:
	virtual int Match(const char* param, CWordCache* wcache) {return 0;};
	virtual void AddReason(char* dest) {};
	virtual ~CFilterBase() {};
};

class CFilter : public CFilterBase
{
public:
	CFilter()
	{
		m_regstr = NULL;
	}
	virtual ~CFilter()
	{
		if (m_regstr)
		{
			delete m_regstr;
			regfree(&m_filter);
		}
	}
	void SetFilter(char* regstr)
	{
		#define ERRSTRSIZE 100
		char regerrstr[STRSIZ]="";
		int err = regcomp(&m_filter, regstr, REG_EXTENDED | REG_ICASE);
		if (err)
		{
			regerror(err, &m_filter, regerrstr, ERRSTRSIZE);
			sprintf(conf_err_str, "Wrong regex in config file: %s: %s", regstr, regerrstr);
			regfree(&m_filter);
		}
		else
		{
			m_regstr = strdup(regstr);
		}
	}
	virtual int Match(const char* param, CWordCache* wcache);
	virtual void AddReason(char* dest);

public:
	regex_t m_filter;
	char*	m_regstr;
};

class CCountryFilter : public CFilterBase
{
public:
	hash_set<string> m_countries;

public:
	virtual ~CCountryFilter(void){}
	virtual int Match(const char* param, CWordCache* wcache);
};

class CFilters : public vector<CFilterBase*>
{
public:
	double m_time;

public:
	CFilters()
	{
		m_time = 0;
	}
	~CFilters()
	{
		for (iterator filter = begin(); filter != end(); filter++)
		{
			delete *filter;
		}
	}
	int FilterType(const char * param, char * reason, CWordCache* wcache);
};

class CExtFilters : public hash_map<string, CFilterBase>
{
public:
	int FilterType(const char * param, char * reason, int* method);
};

class CMime
{
public:
	~CMime()
	{
		if (m_mime_type.size() > 0)
		{
			regfree(&m_reg);
		}
	}

	int SetType(char* reg, char* mime_type)
	{
		#define ERRSTRSIZE 100
		char regerrstr[STRSIZ]="";
		int err = regcomp(&m_reg, reg, REG_EXTENDED | REG_ICASE);
		if (err)
		{
			regerror(err, &m_reg, regerrstr, ERRSTRSIZE);
			sprintf(conf_err_str, "Wrong regex in config file: %s: %s", reg, regerrstr);
			regfree(&m_reg);
			return 0;
		}
		else
		{
			m_mime_type = mime_type;
			return 1;
		}
	}

public:
	regex_t	m_reg;
	string m_mime_type;
};

class CReplacement
{
public:
	regex_t m_rfind;
	string m_find;
	string m_replace;

public:
	int SetFindReplace(char* find, char* replace)
	{
		int err;
		if ((*find == 0) || (err = regcomp(&m_rfind, find, REG_EXTENDED | REG_ICASE)))
		{
			regfree(&m_rfind);
			return 1;
		}
		else
		{
			m_find = find;
			m_replace = replace;
			return 0;
		}
	}
	~CReplacement()
	{
		if (m_find.size() > 0)
		{
			regfree(&m_rfind);
		}
	}
	int Replace(const char* string, char* dest, int maxc);
};

class CReplaceVec : public vector<CReplacement*>
{
public:
	int Replace(const char* string, char* dest, int maxc);
};

class CMimes : public vector<CMime*>
{
public:
	~CMimes()
	{
		for (iterator mime = begin(); mime != end(); mime++)
		{
			delete *mime;
		}
	}
};

int FilterType(const char * param, char * reason, CWordCache* wcache);
int AddExtFilter(char *ext, int filter_type, int reverse);
int AddFilter(char * filter, int filter_type, int reverse);
void AddCountries(char* s);

extern CFilters Filters;

#endif
