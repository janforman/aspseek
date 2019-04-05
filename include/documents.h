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

/*  $Id: documents.h,v 1.13 2002/01/09 11:50:14 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _DOCUMENTS_H_
#define _DOCUMENTS_H_

#include <string>
#include <vector>
#include <errno.h>
#include "defines.h"
#include "event.h"

using std::string;
using std::vector;

template<int n>
class CFixedString
{
public:
	char m_content[n];

public:
	CFixedString()
	{
		m_content[0] = 0;
	}
	CFixedString(const char* str)
	{
		strncpy(m_content, str, n - 1);
		m_content[n - 1] = 0;
	}
	const char* c_str() const
	{
		return m_content;
	}
	void operator=(const char* src)
	{
		strncpy(m_content, src, n - 1);
		m_content[n - 1] = 0;
	}
	bool operator==(const CFixedString& Word) const
	{
		return strcmp(m_content, Word.m_content) == 0;
	}
};


class CDocument
{
public:
	CDocument()
	{
		Init();
	}
	CDocument* Init()
	{
		m_hops = 0;
		m_referrer = 0;
		m_slow = 0;
		m_redir = 0;
		return this;
	}
	int Slow()
	{
		return m_slow;
	}

public:
	ULONG m_urlID;
	ULONG m_siteID;
	int m_last_index_time;
	int m_next_index_time;
	int m_hops;
	int m_referrer;
	int m_status;
	int m_slow;
	int m_redir;
	CFixedString<33> m_crc;
	CFixedString<33> m_last_modified;
	CFixedString<MAX_ETAG_LEN> m_etag;
	CFixedString<MAX_URL_LEN + 1> m_url;
#ifdef OPTIMIZE_CLONE
	int m_origin;
#endif
};

typedef vector<ULONG> CULONGVector;
typedef vector<string> CStringVector;

class CSrcDocument : public CDocument
{
public:
	string m_descr;
	string m_keywords;
	string m_title;
	string m_content_type;
	string m_text;
	CStringVector m_excerpts;
#ifdef UNICODE
	string m_charset;
	CULONGVector m_descrH;
	CULONGVector m_keywordsH;
	CULONGVector m_titleH;
	CULONGVector m_textH;
	CULONGVector m_excerptsH;
#endif
	char* m_content;
	ULONG m_size;

public:
	CSrcDocument()
	{
		m_content = NULL;
	}
	~CSrcDocument()
	{
		if (m_content) delete m_content;
	}
};

class CSrcDocVector : public vector<CSrcDocument*>
{
public:
	~CSrcDocVector()
	{
		for (iterator it = begin(); it != end(); it++)
		{
			if (*it) delete *it;
		}
	}
};

#endif
