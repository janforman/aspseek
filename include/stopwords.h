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

/* $Id: stopwords.h,v 1.12 2002/12/18 18:38:53 kir Exp $
   Author : Kir Kolyshkin
*/

#ifndef _STOPWORDS_H_
#define _STOPWORDS_H_

#include <ctype.h>	// for tolower
#include "aspseek-cfg.h"
#include "my_hash_set"
#include "documents.h"	// for CFixedString
#ifdef UNICODE
#include "ucharset.h"
#endif

/// Stopword is limited to 15 characters (should be more than enough)
#ifdef UNICODE
typedef CUFixedString<16> CWLString;
#else
typedef CFixedString<16> CWLString;
#endif

/// Class that holds stopword and it's language
class CWordLang: public CWLString
{
public:
	char m_lang[3];	///< two character ISO language code
public:
	CWordLang(void): CWLString()
	{
		m_lang[0]='\0';
	}
#ifdef UNICODE
	CWordLang(const WORD* wrd): CWLString(wrd)
#else
	CWordLang(const char* wrd): CWLString(wrd)
#endif
	{
		m_lang[0]='\0';
	}
#ifdef UNICODE
	CWordLang(const WORD* wrd, const char *lang): CWLString(wrd)
#else
	CWordLang(const char* wrd, const char *lang): CWLString(wrd)
#endif
	{
		m_lang[0]=tolower(lang[0]);
		m_lang[1]=tolower(lang[1]);
		m_lang[2]='\0';
	}
	const char* GetLang(void) const
	{
		return m_lang;
	};
};

namespace STL_EXT_NAMESPACE {
	template <> struct hash<CWordLang>
	{
		size_t operator()(const CWordLang& __s) const
		{
#ifdef UNICODE
			return stl_hash_ustring(__s.c_str());
#else
			return __stl_hash_string(__s.c_str());
#endif
		}
	};
}

class CStopWords: public hash_set<CWordLang>
{
public:
	/// Load stopwords for given language from file
	/// Returns NULL if this is not stopword, or language code
#ifdef UNICODE
	int Load(const char* file, const char* lang, const char* encoding);
	const char* IsStopWord(const WORD* wrd) const
#else
	int Load(const char* file, const char* lang);
	const char* IsStopWord(const char* wrd) const
#endif
	{
		iterator it = find(wrd);
		return (it == end()) ? NULL : it->GetLang();
	}
};

extern CStopWords Stopwords;
#endif /* _STOPWORDS_H_ */
