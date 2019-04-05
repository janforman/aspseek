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

/*  $Id: ucharset.h,v 1.15 2002/12/18 18:38:53 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _UCHARSETS_H_
#define _UCHARSETS_H_

#include <ctype.h> /* for tolower */
#include "aspseek-cfg.h"
#include "my_hash_map"
#include "defines.h"
#include "charsets.h"
#include <string>

using std::string;

class CCharU1
{
public:
	BYTE m_code[2];	// Index 0 for small letters, 1 for capital
	WORD m_ucode[2];

public:
	CCharU1()
	{
		m_code[0] = m_code[1] = 0;
		m_ucode[0] = m_ucode[1] = 0;
	}
};

class CCharsetBase;
class CWordSet;
//class CSet2;

//extern CSet2 idDictionary;
extern CWordSet idDictionary;

extern CCharsetBase baseCharset;

class CCharsetB
{
public:
	virtual int IsLetter(BYTE* symbol, int len) {return 1;};
	virtual int SymbolType(BYTE* symbol, int len) {return 1;};
	virtual CWordSet* GetDictionary() {return NULL;};
	virtual int CharLen(BYTE symbol) {return 1;};
	virtual int RecodeStringLower(BYTE* src, WORD* dst, int maxlen) = 0;
	virtual WORD UCode(const BYTE*& src) = 0;
	virtual WORD UCodeLower(const BYTE*& src, int letter = 1) = 0;
	virtual void Code(BYTE*& dst, WORD code, char notfound) = 0;
	virtual CCharsetBase* GetU1() {return &baseCharset;};
	virtual WORD NextChar(const BYTE*& src) = 0;
	virtual void PutChar(BYTE*& dst, WORD character) = 0;
	virtual WORD UCodeL(WORD code) = 0;
	virtual void SetDictionary(CWordSet* dic) {};
	virtual void GetWords(WORD* sentence, vector<WORD*>& words);
	string RecodeFrom(string src, CCharsetB* srcCharset, char notfound = '?');
	string RecodeFrom(const WORD* src, char notfound = '?');
};

class CCharsetBase : public CCharsetB
{
public:
	void CodeLower(WORD* dst, const BYTE* src);
	virtual WORD UCodeLower(BYTE symbol, int letter = 1)
	{
		return symbol < 0x80 ? (letter == 0) || (Charsets[0].wordch[symbol >> 3] & (1 << (symbol & 7))) ? tolower(symbol) : 0 : 0;
	}
	virtual BYTE CodeLower(BYTE symbol)
	{
		return symbol < 0x80 ? tolower(symbol) : 0;
	}
	virtual WORD UCode(BYTE symbol)
	{
		return symbol < 0x80 ? symbol : 0;
	}
	virtual BYTE Code(WORD uchar, BYTE notfound = '?')
	{
		return uchar < 0x80 ? uchar : notfound;
	}
	virtual ~CCharsetBase() {};
	virtual int RecodeStringLower(BYTE* src, WORD* dst, int maxlen);
	virtual WORD UCodeLower(const BYTE*& src, int letter = 1)
	{
		return *src ? UCodeLower(*src++, letter) : 0;
	}
	virtual WORD UCode(const BYTE*& src)
	{
		return *src ? UCode(*src++) : 0;
	}
	virtual void Code(BYTE*& dst, WORD code, char notfound)
	{
		*dst++ = Code(code, notfound);
	}
	virtual WORD NextChar(const BYTE*& src)
	{
		return *src ? *src++ : 0;
	}
	virtual void PutChar(BYTE*& dst, WORD character)
	{
		*dst++ = character;
	}
	virtual WORD UCodeL(WORD code)
	{
		return UCodeLower(code);
	}
	virtual CCharsetBase* GetU1() {return this;};
};

class CCharsetUTF8 : public CCharsetB
{
public:
	virtual int CharLen(BYTE symbol)
	{
		if (symbol < 0x80) return 1;
		if (symbol < 0xE0) return 2;
		return 3;
	};
	virtual CWordSet* GetDictionary() {return &idDictionary;};
	virtual int IsLetter(BYTE* symbol, int len);
	virtual int SymbolType(BYTE* symbol, int len);
	virtual int RecodeStringLower(BYTE* src, WORD* dst, int maxlen);
	WORD UCodeLower(const BYTE*& src, int letter = 1);
	WORD UCodeLower(WORD src, int letter = 1);
	virtual WORD UCode(const BYTE*& src);
	virtual void Code(BYTE*& dst, WORD code, char notfound);
	virtual WORD NextChar(const BYTE*& src)
	{
		return UCode(src);
	}
	virtual void PutChar(BYTE*& dst, WORD character)
	{
		Code(dst, character, 0);
	}
	virtual WORD UCodeL(WORD code)
	{
		return UCodeLower(code);
	}
};
/*
class CSet2 : public hash_map<WORD, CSet2*>
{
public:
	int m_end;
public:
	CSet2() : hash_map<WORD, CSet2*>(7)
	{
		m_end = 0;
	}
	void AddWord(WORD* word);
	WORD* NextWord(WORD* word);
	WORD* NextWord1(WORD* word);
	CSet2* Find(WORD code)
	{
		iterator it = find(code);
		return it == end() ? NULL : it->second;
	}
};
*/
class CCharsetU2V : public CCharsetB
{
public:
	WORD m_map[0x8000];
	WORD m_revMap[0x10000];
	CWordSet* m_dictionary;

public:
	CCharsetU2V()
	{
		memset(m_map, 0, sizeof(m_map));
		memset(m_revMap, 0, sizeof(m_revMap));
		m_dictionary = NULL;
	}
	virtual void SetDictionary(CWordSet* dic)
	{
		m_dictionary = dic;
	};
	virtual int CharLen(BYTE symbol)
	{
		if (symbol < 0x80) return 1;
		return 2;
	};
	void AddCodes(WORD code, WORD ucode);
	virtual int IsLetter(BYTE* symbol, int len);
	virtual int SymbolType(BYTE* symbol, int len);
	virtual CWordSet* GetDictionary() {return m_dictionary;};
	virtual WORD UCode(const BYTE*& src);
	virtual void Code(BYTE*& dst, WORD code, char notfound);
	virtual WORD NextChar(const BYTE*& src);
	virtual void PutChar(BYTE*& dst, WORD character);
	virtual int RecodeStringLower(BYTE* src, WORD* dst, int maxlen);
	virtual WORD UCodeL(WORD code)
	{
		return code < 0x80 ? tolower(code) : m_map[code & 0x7FFF];
	}
	virtual WORD UCodeLower(const BYTE*& symbol, int letter = 1);
	virtual void GetWords(WORD* sentence, vector<WORD*>& words);
};

class CCharsetU1 : public CCharsetBase
{
public:
	CCharU1 m_chars[0x80];	//Index 0 is for char 0x80
	hash_map<WORD, CCharU1*> m_reverseMap;

public:
	void AddCodes(ULONG* codes, ULONG* ucodes);
	virtual WORD UCodeLower(BYTE symbol, int letter = 1)
	{
		return symbol < 0x80 ? CCharsetBase::UCodeLower(symbol, letter) : m_chars[symbol - 0x80].m_ucode[0];
	}
	virtual BYTE CodeLower(BYTE symbol)
	{
		return symbol < 0x80 ? CCharsetBase::CodeLower(symbol) : m_chars[symbol - 0x80].m_code[0];
	}
	virtual WORD UCode(BYTE symbol)
	{
		if (symbol < 0x80)
		{
			return symbol;
		}
		CCharU1* cu = m_chars + (symbol - 0x80);
		return cu->m_code[0] == symbol ? cu->m_ucode[0] : cu->m_ucode[1];
	}
	virtual BYTE Code(WORD uchar, BYTE notfound = '?')
	{
		if (uchar < 0x80)
		{
			return uchar;
		}
		hash_map<WORD, CCharU1*>::iterator it = m_reverseMap.find(uchar);
		if (it == m_reverseMap.end())
		{
			return notfound;
		}
		CCharU1* cu = it->second;
		return cu->m_ucode[0] == uchar ? cu->m_code[0] : cu->m_code[1];
	}
	virtual int IsLetter(BYTE* c, int len)
	{
		return UCodeLower(*c) ? 1 : 0;
	}
	virtual int SymbolType(BYTE* symbol, int len)
	{
		return UCodeLower(*symbol) ? 1 : 0;
	}
	virtual ~CCharsetU1() {};
};

class CCharsetMap : public hash_map<string, int>
{
public:
	hash_map<string, string> m_aliasMap;
	vector<CCharsetB*> m_charsets;
	vector<const char*> m_names;
	vector<string> m_langs;

public:
	CCharsetMap();
	int AddCharset(const char* name, CCharsetB* charset, const char* lang);
	void FixLangs();
	CCharsetB* GetCharset(const char* charset)
	{
		iterator it = find(charset);
		if (it == end())
		{
			hash_map<string, string>::iterator ita = m_aliasMap.find(charset);
			if (ita == m_aliasMap.end())
			{
				return &baseCharset;
			}
			else
			{
				it = find(ita->second);
				return it == end() ? &baseCharset : m_charsets[it->second - 1];
			}
		}
		else
		{
			return m_charsets[it->second - 1];
		}
	}
	void AddAlias(const char* charset, const char* alias)
	{
		m_aliasMap[alias] = charset;
	}
};

inline size_t stl_hash_ustring(const WORD* s)
{
	unsigned long h = 0;
	for ( ; *s; ++s)
		h = 5 * h + *s;
	return size_t(h);
}

class CUWord
{
public:
	WORD m_word[MAX_WORD_LEN + 1];

public:
	CUWord()
	{
		m_word[0] = 0;
	}
	CUWord(const WORD* str)
	{
		Assign(str);
	}
	const WORD* Word() const {return m_word;};
	CUWord& Assign(const WORD* str)
	{
		WORD* pw = m_word;
		while (*str)
		{
			*pw++ = *str++;
			if (pw - m_word >= MAX_WORD_LEN) break;
		}
		*pw = 0;
		return *this;
	}
	CUWord& operator=(WORD* str)
	{
		return Assign(str);
	}
	bool operator==(const CUWord& Word) const
	{
		const WORD* w = m_word;
		const WORD* w1 = Word.m_word;
		while (*w && *w1)
		{
			if (*w != *w1) return false;
			w++; w1++;
		}
		return (*w == 0) && (*w1 == 0);
	}
};

inline int strwlen(const WORD* w)
{
	const WORD* pw;
	for (pw = w; *pw; pw++);
	return pw - w;
}

inline int strwcmp(const WORD* w1, const WORD* w2)
{
	const WORD* pw1 = w1;
	const WORD* pw2 = w2;
	while (*pw1 && *pw2)
	{
		if (*pw1 != *pw2) break;
		pw1++; pw2++;
	}
	return *pw1 == *pw2 ? 0 : *pw1 < *pw2 ? -1 : 1;
}

inline WORD* strwcpy(WORD* dst, const WORD* src)
{
	WORD* d = dst;
	while (*src)
	{
		*d++ = *src++;
	}
	*d = 0;
	return dst;
}

inline WORD* stpwcpy(WORD* dst, const WORD* src)
{
	WORD* d = dst;
	while (*src)
	{
		*d++ = *src++;
	}
	*d = 0;
	return d;
}

inline WORD* stpbwcpy(WORD* dst, const char* src)
{
	WORD* d = dst;
	while (*src)
	{
		*d++ = *src++;
	}
	return d;
}

template<int n>
class CUFixedString
{
public:
	WORD m_content[n];

public:
	CUFixedString()
	{
		m_content[0] = 0;
	}
	CUFixedString(const WORD* str)
	{
		Assign(str);
	}
	void Assign(const WORD* str)
	{
		WORD* pw = m_content;
		while (*str)
		{
			*pw++ = *str++;
			if (pw - m_content >= n) break;
		}
		*pw = 0;
	}
	const WORD* c_str() const
	{
		return m_content;
	}
	void operator=(const char* str)
	{
		Assign(str);
	}
	bool operator==(const CUFixedString& Word) const
	{
		const WORD* w = m_content;
		const WORD* w1 = Word.m_content;
		while (*w && *w1)
		{
			if (*w != *w1) return false;
			w++; w1++;
		}
		return *w == *w1;
	}
};

class CGetUWord
{
public:
	WORD* m_pchar;
	WORD m_char;

public:
	CGetUWord()
	{
		m_pchar = NULL;
	}
	~CGetUWord()
	{
		if (m_pchar)
		{
			*m_pchar = m_char;
		}
	}
	WORD* GetWord(WORD **last);
};

namespace STL_EXT_NAMESPACE {
	template <> struct hash<CUWord> {
		size_t operator()(const CUWord& __s) const
		{
			return stl_hash_ustring(__s.Word());
		}
	};
}

void FixLangs();
int LoadDictionary2(const char* lang, const char* file, const char* encoding);

int LoadCharsetU2V(const char * lang, const char *name, const char* dir);
int LoadCharsetU1(const char * lang, const char *name, const char* dir);
void AddAliasU1(const char* charset, const char* alias);

int GetU1Word(unsigned char **last, CCharsetBase* charset, WORD* result, char** token = NULL);
int GetUWord(unsigned char **last, CCharsetB* charset, WORD* result, char** token = NULL);

CCharsetB* GetUCharset(const char* name);
CCharsetB* GetUCharset(int index);
const char* GetCharsetName(int index);

int ParseTag(CCharsetB* charset, CTag& tag, int& len, char* s, char*& e);

inline CCharsetBase* GetU1Charset(const char* name)
{
	return GetUCharset(name)->GetU1();
}

inline CCharsetBase* GetU1Charset(int index)
{
	return GetUCharset(index)->GetU1();
}

WORD TolowerU(WORD ucode);

inline int IsIdeograph(WORD ucode)
{
	return (ucode >= 0x3400) && (ucode <= 0x9FAF);
}

inline int StrNCmp(CCharsetB* charset, const BYTE* src, const BYTE* src1, int len)
{
	for (int i = 0; i < len; i++, src1++)
	{
		WORD code = charset->NextChar(src);
		if (code != *src1) return code > *src1 ? 1 : -1;
	}
	return 0;
}

inline BYTE* UStrStrE(CCharsetB* charset, const BYTE* src, const BYTE* string)
{
	WORD code;
	while (true)
	{
		while ((code = charset->NextChar(src)) && (code != *string));
		if (code == 0) return (BYTE*)src;
		const BYTE* s = src;
		const BYTE* pstr = string + 1;
		while (*pstr)
		{
			code = charset->NextChar(src);
			if (code == 0) return (BYTE*)src;
			if (code != *pstr) break;
			pstr++;
		}
		if (*pstr == 0) return (BYTE*)src;
		src = s;
	}
}

class CWordLetter
{
public:
	const CWordLetter* m_parent;
	WORD m_ucode;
	short m_end;

public:
	CWordLetter(WORD code, const CWordLetter* parent)
	{
		m_parent = parent;
		m_ucode = code;
		m_end = 0;
	}
	int operator==(const CWordLetter& w) const
	{
		return (m_parent == w.m_parent) && (m_ucode == w.m_ucode);
	}
};

namespace STL_EXT_NAMESPACE {
	template <> struct hash<CWordLetter> {
		size_t operator()(const CWordLetter& __s) const
		{
			return __s.m_ucode + (int)__s.m_parent;
		}
	};
}

class CWordSet : public hash_set<CWordLetter>
{
public:
	void AddWord(WORD* pword, const CWordLetter* parent);
	void AddWord(WORD* pword)
	{
		AddWord(pword, NULL);
	}
	const CWordLetter* Find(WORD code, const CWordLetter* parent)
	{
		CWordLetter letter(code, parent);
		iterator it = find(letter);
		if (it == end())
		{
			return NULL;
		}
		else
		{
			return &(*it);
		}
	}
	WORD* NextWord(WORD* word);
	WORD* NextWord1(WORD* word);
};

inline void Utf8Code(BYTE*& dst, WORD code)
{
	if ((code & 0xFF80) == 0)
	{
		*dst++ = code;
	}
	else if ((code & 0xF800) == 0)
	{
		*dst++ = 0xC0 | (code >> 6);
		*dst++ = 0x80 | (code & 0x3F);
	}
	else
	{
		*dst++ = 0xE0 | (code >> 12);
		*dst++ = 0x80 | ((code >> 6) & 0x3F);
		*dst++ = 0x80 | (code & 0x3F);
	}
}

inline void Utf8CodeL(BYTE*& dst, WORD code)
{
	if ((code & 0xFF80) == 0)
	{
		*dst++ = code;
	}
	else if ((code & 0xF800) == 0)
	{
		*dst++ = 0x80 | (code & 0x3F);
		*dst++ = 0xC0 | (code >> 6);
	}
	else
	{
		*dst++ = 0x80 | (code & 0x3F);
		*dst++ = 0x80 | ((code >> 6) & 0x3F);
		*dst++ = 0xE0 | (code >> 12);
	}
}

inline WORD UCode(const BYTE*& src)
{
	WORD ucode;
	if (src[0] < 0x80)
	{
		return *src ? *src++ : 0;
	}
	if (src[0] < 0xE0)
	{
		if (src[1] && (src[0] & 0x1F))
		{
			ucode = ((src[0] & 0x1F) << 6) | (src[1] & 0x3F);
			src += 2;
		}
		else
		{
			src++;
			return 0;
		}
	}
	else
	{
		if (src[1] && (src[0] & 0xF))
		{
			if (src[2] && (src[1] & 0x3F))
			{
				ucode = ((src[0] & 0xF) << 12) | ((src[1] & 0x3F) << 6) | (src[2] & 0x3F);
				src += 3;
			}
			else
			{
				src += 2;
				return 0;
			}
		}
		else
		{
			src++;
			return 0;
		}
	}
	return ucode;
}

#define USTRNCMP(charset, src, dst) StrNCmp(charset, (BYTE*)src, (BYTE*)dst, sizeof(dst) - 1)

// This function returns 1 if string matches pattern
template<class T1>
int Match(const T1* string, const T1* pattern, T1 anySeq, T1 anySym)
{
	const T1* stringStartScan, *patternStartScan;
	const T1* stringIndex, *patternIndex;
	stringIndex = string;
	patternIndex = pattern;
	stringStartScan = NULL;
	patternStartScan = NULL;

	while (*patternIndex)
	{
		T1 p = *patternIndex++;
		if (p == anySeq)
		{
			if (*patternIndex == 0) return 1;
			stringStartScan = stringIndex;
			patternStartScan = patternIndex;
		}
		else
		{
			if (*stringIndex == 0) return 0;
			T1 t = *stringIndex++;
			if (((t != p) && (p != anySym)) || ((*patternIndex == 0) && (*stringIndex != 0)))
			{
				if (stringStartScan == NULL) return 0;
				stringIndex += patternStartScan - patternIndex + 1;
				patternIndex = patternStartScan;
			}
		}
	}
	if (*stringIndex != 0) return 0;
	return 1;
}

#endif
