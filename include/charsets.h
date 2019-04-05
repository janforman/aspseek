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

/*  $Id: charsets.h,v 1.20 2002/12/18 18:38:53 kir Exp $
    Author : Alexander F. Avdonkin
	Uses parts of UdmSearch code
*/

#ifndef _CHARSETS_H_
#define _CHARSETS_H_
#include <sys/time.h>
#include <string>
#include "aspseek-cfg.h"
#include "maps.h"
#include "documents.h"

#define CHARSET_DEFAULT		-1
#define CHARSET_USASCII		0

typedef unsigned char TABLE[256];

typedef struct
{
	int		charset;
	char		lang[3];
	char		*name;
	unsigned	char *chars;
	TABLE		lower;
	TABLE		upper;
	unsigned char wordch[32];
} TCHARSET;

typedef struct alias_struct
{
	int		charset;
	char *alias;
} ALIAS;

class CCharsetB;

#define WORDCHAR "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

int WordChar(unsigned char s, int charset);
int GetCharset(const char *alias);
int SetDefaultCharset(int id);
int GetDefaultCharset();
int FillLowerString(unsigned char *str,TABLE table);
int FillUpperString(unsigned char *str,TABLE table);
void MakeWordRange(TCHARSET* charset);
char* Tolower(unsigned char *str, int charset);
char* Toupper(unsigned char *str, int charset);
WORD SgmlToChar(const char*& sgml,int charset);
char* Recode(char *str,int from,int to);
int InitCharset();
char* GetToken(char *s, const char *delim, char **last);
char* Trim(char *p, char *delim);
int LoadCharset(const char *lang, const char *name, const char* dir);
void AddAlias(const char *charset, char *aliases);
int AddCharset(const char *lang, const char *name, unsigned char *charset);
int CanRecode(int from, int to);
void InitCharset0();

#define MAX_ALIAS	128
#define MAX_CHARSET	32
extern TCHARSET Charsets[MAX_CHARSET];

/// Represents single word, with length limited to 32
class CWord
{
public:
	char m_word[MAX_WORD_LEN + 1];
public:
	CWord()
	{
		m_word[0] = 0;
	}
	CWord(const char* str)
	{
		strncpy(m_word, str, sizeof(m_word));
	}
	const char* Word() const {return m_word;};
	CWord& operator=(char* str)
	{
		strncpy(m_word, str, sizeof(m_word));
		return *this;
	}
	bool operator==(const CWord& Word) const
	{
		return strcmp(m_word, Word.m_word) == 0;
	}
};

namespace STL_EXT_NAMESPACE {
	template <> struct hash<CWord> {
		size_t operator()(const CWord& __s) const
		{
			return __stl_hash_string(__s.Word());
		}
	};
}

class CGetWord
{
public:
	unsigned char* m_pchar;
	unsigned char m_char;

public:
	CGetWord()
	{
		m_pchar = NULL;
	}
	~CGetWord()
	{
		if (m_pchar)
		{
			*m_pchar = m_char;
		}
	}
	char* GetWord(unsigned char **last, int charset);
};

class CTag
{
public:
	char *m_name;
	char *m_href;
	char *m_src;
	char *m_content;
	char *m_equiv;
	char *m_value;
	char *m_selected;
	char *m_checked;
	char *m_size;
public:
	void Zero()
	{
		m_name = m_href = m_src = m_content = m_equiv = m_value = m_selected = m_checked = m_size = NULL;
	}
	CTag()
	{
		Zero();
	}
	~CTag()
	{
		if (m_name) delete m_name;
		if (m_href) delete m_href;
		if (m_src) delete m_src;
		if (m_content) delete m_content;
		if (m_equiv) delete m_equiv;
		if (m_value) delete m_value;
		if (m_selected) delete m_selected;
		if (m_checked) delete m_checked;
		if (m_size) delete m_size;
		Zero();
	}

	char* ParseTag(char *stag);
#ifdef UNICODE
	BYTE* ParseTag(CCharsetB* charset, BYTE *stag, int len);
#endif
};

class CharsetMap : public hash_map<std::string, int>
{
public:
	int m_charsets[MAX_CHARSET];

public:
	CharsetMap();
	int GetCharset(const char* charset);
	void AddAliasMap(int charsetid, const char *alias);
	const char* CharsetsStr(int charset);
};

double timedif(struct timeval& tm1, struct timeval& tm);

#define Alloca(n) n > 10000 ? new char[n] : (char*)alloca(n)
#define Freea(x, n) if (n > 10000) delete x;

int ParseTag(CTag& tag, int& len, char* s, char*& e);

extern CharsetMap charsetMap;

class CSgmlCharMap : public hash_map<char, CSgmlCharMap*>
{
public:
	WORD m_code;

public:
	CSgmlCharMap()
	{
		m_code = 0;
	}
	~CSgmlCharMap()
	{
		for (iterator it = begin(); it != end(); it++)
			delete it->second;
	}
	void AddSgml(const char* sgml, WORD code);
	WORD GetCode(const char*& sgml);
};

class CSgmlCharMapI : public CSgmlCharMap
{
public:
	CSgmlCharMapI();
};

#ifdef USE_CHARSET_GUESSER
#define MAX_NGRAM	400
#define MAX_NGRAMLEN	4
#define LANG_FACTOR	0.01

#define MAX_BLOCKS	4
#define MIN_TEXTLEN	5
#define MAX_TEXTLEN	256

typedef CFixedString<MAX_NGRAMLEN + 1> CLmString;

namespace STL_EXT_NAMESPACE {
	template <> struct hash<CLmString>
	{
		size_t operator()(const CLmString& __s) const
		{
			return __stl_hash_string(__s.c_str());
		}
	};
}

class CLmText: public hash_map<CLmString, int>
{
public:
	CLmText(const char *text)
	{
		MakeLm(text);
	}
	void MakeLm(const char *text);
	char *GetBlock(const char *src, char *dst, int len);
};

class CLang : public hash_map <CLmString, int>
{
public:
	int LoadLm( char *dir);
};

class CLangs : public hash_map<int, CLang*>
{
public:
	void AddLang(int charset, char *table);
	int GetLang(char *text);
	~CLangs();
};
extern CLangs langs;

#endif /* USE_CHARSET_GUESSER */
#endif /* _CHARSETS_H_ */
