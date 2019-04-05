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

/*  $Id: affix.h,v 1.11 2002/12/18 18:38:53 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _AFFIX_H_
#define _AFFIX_H_

#include <sys/types.h>
#include <regex.h>
#include "aspseek-cfg.h"
#include "my_hash_map"
#include "my_hash_set"
#include "documents.h"
#ifdef UNICODE
#include "ucharset.h"
#endif


typedef CFixedString<4> CLangStr;

class CUSymbol
{
public:
	int m_negate;

public:
	CUSymbol()
	{
		m_negate = 0;
	}
	virtual int MatchN(CHAR symbol) = 0;
	int Match(CHAR symbol)
	{
		return MatchN(symbol) != m_negate;
	}
};

class CAnySymbol : public CUSymbol
{
public:
	virtual int MatchN(CHAR symbol)
	{
		return 1;
	}
};

class CULetter : public CUSymbol
{
public:
	CHAR m_symbol;

public:
	CULetter(CHAR symbol)
	{
		m_symbol = symbol;
	}
	virtual int MatchN(CHAR symbol)
	{
		return m_symbol == symbol;
	}
};

class CUSymSet : public CUSymbol
{
public:
	CHAR* m_symbols;
	int m_count;

public:
	virtual int MatchN(CHAR symbol);
};

class CURegFilter
{
public:
	vector<CUSymbol*> m_symbols;

public:
	int Match(const CHAR* string);
};

class CAffixRule
{
public:
#ifdef UNICODE
	CUFixedString<12> m_ending;
	CUFixedString<12> m_replacement;
#else
	CFixedString<12> m_ending;
	CFixedString<12> m_replacement;
#endif
	CLangStr m_lang;
//	regex_t m_filter;
	CURegFilter* m_filter;
	char m_flag;

public:
	~CAffixRule()
	{
//		regfree(&m_filter);
		delete m_filter;
	}
	int Match(const CHAR* param, const char* lang)
	{
//		regmatch_t subs;
//		return regexec(&m_filter, param, 1, &subs, 0) == 0;
		return (strcmp(lang, m_lang.c_str()) == 0) && m_filter->Match(param);
	}
};

typedef vector<CAffixRule*> CAffixRules;

class CMapEndingToAffix : public hash_map<CHAR, CMapEndingToAffix*>
{
public:
	CAffixRules m_rules;

public:
	void AddRule(CAffixRule* rule);
	CMapEndingToAffix& GetMap(const CHAR* ending);
};

class CMapFlagToRules : public hash_map<char, CAffixRules>
{
public:
	void AddRule(CAffixRule* rule);
};

class CFlags
{
public:
	CFixedString<8> m_flags;
	CLangStr m_lang;
};

#ifdef UNICODE
typedef CUFixedString<MAX_WORD_LEN + 1> CSWord;

namespace STL_EXT_NAMESPACE {
	template <> struct hash<CSWord> {
		size_t operator()(const CSWord& __s) const
		{
			return stl_hash_ustring(__s.c_str());
		}
	};
}

#else
typedef CFixedString<MAX_WORD_LEN + 1> CSWord;

namespace STL_EXT_NAMESPACE {
	struct hash<CSWord> {
		size_t operator()(const CSWord& __s) const
		{
			return __stl_hash_string(__s.c_str());
		}
	};
}
#endif /* UNICODE */

typedef hash_map<CSWord, CFlags> CDictionary;

#ifdef UNICODE
int ImportDictionary(char *lang, char *filename, const char* charset);
int ImportAffixes(char *lang, char *filename, const char* charset);
void TestSpell(const char* fname, const char* charset);
#else
int ImportAffixes(char *lang, char *filename);
int ImportDictionary(char *lang, char *filename);
void TestSpell(const char* fname);
#endif

void FindForms(CHAR* word, hash_set<CSWord>& forms, const char *langs);

#endif
