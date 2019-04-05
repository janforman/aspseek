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

/*  $Id: ucharset.cpp,v 1.12 2002/08/01 13:47:10 kir Exp $
    Author : Alexander F. Avdonkin
*/

#include "aspseek-cfg.h"
#include <stdio.h>
#include <errno.h>
#include "ucharset.h"
#include "paths.h"
#include "logger.h"
#include "misc.h"

CCharsetMap charsetMapU;
CCharsetBase baseCharset;
CCharsetUTF8 utf8Charset;

hash_map<WORD, CCharU1*> ULetters;
hash_map<string, CWordSet*> Dictionaries2;
CWordSet idDictionary;

#define LowerString(dest, src) char* dest = (char*)alloca(strlen(src) + 1);	{char *d = dest;	for (const char* c = src; *c; c++) *d++ = tolower(*c); *d = 0;}

void CWordSet::AddWord(WORD* pword, const CWordLetter* parent)
{
	CWordLetter word(*pword, parent);
	const CWordLetter* nword;
	nword = &(*(insert(word).first));
	if (pword[1])
	{
		AddWord(pword + 1, nword);
	}
	else
	{
		((CWordLetter*)nword)->m_end = 1;
	}
}

WORD* CWordSet::NextWord(WORD* word)
{
	WORD* nword = NextWord1(word);
	return nword == word ? word + 1 : nword;
}

WORD* CWordSet::NextWord1(WORD* word)
{
	const CWordLetter* parent = NULL;
	WORD* pw = word;
	WORD* rw = NULL;
	while (*pw && (parent = Find(*pw, parent)))
	{
		pw++;
		if (parent->m_end) rw = pw;
	}
	return rw ? rw : pw;
}

CCharsetMap::CCharsetMap()
{
	(*this)["utf-8"] = 1;
	m_charsets.push_back(&utf8Charset);
	m_names.push_back("utf-8");
	m_langs.push_back("");
}

int CCharsetMap::AddCharset(const char* name, CCharsetB* charset, const char* lang)
{
	int& ci = (*this)[name];
	if (ci == 0)
	{
		m_charsets.push_back(charset);
		ci = m_charsets.size();
		iterator it = find(name);
		m_names.push_back(it->first.c_str());
		m_langs.push_back(lang);
	}
	else
	{
		delete m_charsets[ci];
		m_charsets[ci] = charset;
		m_langs[ci] = lang;
	}
	return ci;
}

void CCharsetMap::FixLangs()
{
	for (unsigned int i = 0; i < m_langs.size(); i++)
	{
		hash_map<string, CWordSet*>::iterator it = Dictionaries2.find(m_langs[i]);
		if (it != Dictionaries2.end())
		{
			m_charsets[i]->SetDictionary(it->second);
		}
	}
}

void FixLangs()
{
	charsetMapU.FixLangs();
}

void CCharsetU1::AddCodes(ULONG* codes, ULONG* ucodes)
{
	for (int i = 0; i < 2; i++)
	{
		if (codes[i] >= 0x80)
		{
			CCharU1* cu1 = m_chars + codes[i] - 0x80;
			cu1->m_code[0] = codes[0] == 0 ? codes[1] : codes[0];
			cu1->m_code[1] = codes[1] == 0 ? codes[0] : codes[1];
			cu1->m_ucode[0] = ucodes[0] == 0 ? ucodes[1] : ucodes[0];
			cu1->m_ucode[1] = ucodes[1] == 0 ? ucodes[0] : ucodes[1];
			m_reverseMap[ucodes[i]] = cu1;
			if (ucodes[i] > 0) ULetters[ucodes[i]] = cu1;
		}
	}
}

void GetFullPath(const char* dir, string& path)
{
	if (isAbsolutePath(dir))
		path = dir;
	else
	{
		path = CONF_DIR;
		path.append("/");
		path.append(dir);
	}
}

int LoadCharsetU1(const char * lang, const char *name, const char* dir)
{
	FILE *tbl_f;
	char str[256], *chp;
	int line = 0;
	string path;
	GetFullPath(dir, path);

	if (!(tbl_f = fopen(path.c_str(), "r")))
	{
		logger.log(CAT_FILE, L_ERR, "Can't open charset table file %s: %s\n", dir, strerror(errno));
		return -1;
	}
	logger.log(CAT_ALL, L_DEBUG, "Loading unicode charset [%s] from table %s\n", name, dir);
	CCharsetU1* charset = new CCharsetU1;
	while (fgets(str, sizeof(str), tbl_f))
	{
		ULONG code[2], ucode[2];
		line++;
		chp = Trim(str, " \t\n\r");
		if (!*chp || *chp == '#') continue;
		if (sscanf(chp, "%lx %lx %lx %lx", code, ucode, code + 1, ucode + 1) == 4)
		{
			if ((code[0] < 0x100) && (code[1] < 0x100))
			{
				charset->AddCodes(code, ucode);
			}
		}
		else
		{
			logger.log(CAT_ALL, L_WARNING, "Bad line <%s> in table %s\n", chp, dir);
		}
	}
	fclose(tbl_f);
	LowerString(namel, name)
	return charsetMapU.AddCharset(namel, charset, lang);
}

int LoadCharsetU2V(const char * lang, const char *name, const char* dir)
{
	FILE *tbl_f;
	char str[256], *chp;
	int line = 0;
	string path;
	GetFullPath(dir, path);

	if (!(tbl_f = fopen(path.c_str(), "r")))
	{
		logger.log(CAT_FILE, L_ERR, "Can't open charset table file %s: %s\n", dir, strerror(errno));
		return -1;
	}
	logger.log(CAT_ALL, L_DEBUG, "Loading unicode charset [%s] from table %s\n", name, dir);
	CCharsetU2V* charset = new CCharsetU2V;
	while (fgets(str, sizeof(str), tbl_f))
	{
		ULONG code, ucode;
		line++;
		chp = Trim(str, " \t\n\r");
		if (!*chp || *chp == '#') continue;
		if (sscanf(chp, "%lx %lx", &code, &ucode) == 2)
		{
			if (code >= 0x8000)
			{
				charset->AddCodes(code, ucode);
			}
		}
		else
		{
			logger.log(CAT_ALL, L_WARNING, "Bad line <%s> in table %s\n", chp, dir);
		}
	}
	fclose(tbl_f);
	LowerString(namel, name)
	return charsetMapU.AddCharset(namel, charset, lang);
}

int GetU1Word(unsigned char **last, CCharsetBase* charset, WORD* result, char** token)
{
	unsigned char *s;
	WORD* pr = result;
	WORD wu = 0;

	if ((s = *last) == NULL)
		return 1;

	// We find beginning of the word
	while (wu == 0)
	{
		if (*s == 0)
		{
			*last = s;
			return 1;
		}

		if ((wu = charset->UCodeLower(*s)))
		{
			*pr++ = wu;
		}
		if (token) *token = (char*)s;
		s++;
	}
	// We find end of the word
	while (wu)
	{
		if (*s == 0)
		{
			*pr = 0;
			*last = s;
			break;
		}
		WORD wu = charset->UCodeLower(*s);
		if (wu == 0)
		{
			*last = s + 1;
			*pr = 0;
			break;
		}
		else
		{
			*pr++ = wu;
			if (pr - result >= MAX_WORD_LEN)
			{
				while (*++s && wu) wu = charset->UCodeLower(*s);
				*pr = 0;
				*last = (*s) ? s + 1 : s;
				return 2;
			}
		}
		s++;
	}
	return 0;
}

inline int IsLetter(WORD w)
{
	return (w < 0x80 ? isalnum(w) : ULetters.find(w) != ULetters.end());
}

WORD GetCode(CCharsetB* charset, unsigned char*& s)
{
	if (*s == '&')
	{
		s++;
		WORD code = SgmlToChar((const char*&)s, 0);
		return IsLetter(code) ? TolowerU(code) : 0;
	}
	return charset->UCodeLower((const BYTE*&) s);
};

int GetUWord(unsigned char **last, CCharsetB* charset, WORD* result, char** token)
{
	unsigned char *s;
	WORD* pr = result;
	WORD wu = 0;

	if ((s = *last) == NULL)
		return 1;

	// We find beginning of the word
	while (wu == 0)
	{
		unsigned char* s1 = s;
//		if ((wu = charset->UCodeLower(s)))
		if ((wu = GetCode(charset, s)))
		{
			*pr++ = wu;
		}
		else
		{
			if (s == s1)
			{
				*last = s;
				return 1;
			}
		}
		if (token) *token = (char*)s1;
	}
#ifdef UNICODE
	CWordSet* dic = charset->GetDictionary();
	const CWordLetter* letter = NULL;
	BYTE* ls = s;
	int useDic = (dic != NULL) && (IsIdeograph(wu));
	if (useDic) letter = dic->Find(wu, letter);
	if (!useDic || letter)
#endif
	// We find end of the word
	{
		while (wu)
		{
			BYTE *s1 = s;
			WORD wu = GetCode(charset, s);
#ifdef UNICODE
			if ((wu == 0) || (!useDic && IsIdeograph(wu)))
#else
			if (wu == 0)
#endif
			{
				*last = s1;
				break;
			}
			else
			{
#ifdef UNICODE
				if (useDic)
				{
					if (letter->m_end) ls = s1;
					if ((!IsIdeograph(wu)) || ((letter = dic->Find(wu, letter)) == NULL))
					{
						*last = ls;
						break;
					}
				}
#endif
				*pr++ = wu;
				if (pr - result >= MAX_WORD_LEN)
				{
					while (wu) wu = charset->UCodeLower((const BYTE*&)s);
					*pr = 0;
					*last = s;
					return 2;
				}
			}
		}
	}
#ifdef UNICODE
	else
	{
		*last = s;
	}
#endif
	*pr = 0;
	return 0;
}

string CCharsetB::RecodeFrom(string src, CCharsetB* srcCharset, char notfound)
{
	if (srcCharset == this)
	{
		return src;
	}
	BYTE* dst = (BYTE*)alloca((src.size() * 3) + 1);
	BYTE* pdst = dst;
	const BYTE* psrc = (BYTE*)src.c_str();
	while (*psrc)
	{
		WORD ucode = srcCharset->UCode(psrc);
		if (ucode == 0)
		{
			*pdst++ = notfound;
		}
		else
		{
			Code(pdst, ucode, notfound);
		}
	}
	*pdst = 0;
	return (char*)dst;
}

string CCharsetB::RecodeFrom(const WORD* src, char notfound)
{
	char* dst = (char*)alloca((strwlen(src) * 3) + 1);
	BYTE* pdst = (BYTE*)dst;
	const WORD* psrc = src;
	while (*psrc)
	{
		Code(pdst, *psrc++, notfound);
	}
	*pdst = 0;
	return dst;
}

void CCharsetBase::CodeLower(WORD* dst, const BYTE* src)
{
	while (*src)
	{
		*dst++ = UCodeLower(*src++);
	}
	*dst = 0;
}

CCharsetB* GetUCharset(const char* name)
{
	LowerString(nameu, name)
	return charsetMapU.GetCharset(nameu);
}

CCharsetB* GetUCharset(int index)
{
	return charsetMapU.m_charsets[index - 1];
}

const char* GetCharsetName(int index)
{
	return charsetMapU.m_names[index - 1];
}

int CCharsetBase::RecodeStringLower(BYTE* src, WORD* dst, int maxlen)
{
	WORD* puw = dst;
	for (BYTE *c = src; *c; c++)
	{
		if (maxlen == 0)
		{
			*puw = 0;
			return puw - dst + 1;
		}
		*puw++ = UCodeLower(*c, 0);
		maxlen--;
//		*c = CodeLower(*c);
	}
	*puw = 0;
	return puw - dst;
}

int CCharsetUTF8::IsLetter(BYTE* symbol, int len)
{
	WORD ucode;
	if (symbol[0] < 0x80)
	{
		return 1;
	}
	if (symbol[0] < 0xE0)
	{
		ucode = ((symbol[0] & 0x1F) << 6) | (symbol[1] & 0x3F);
	}
	else
	{
		ucode = ((symbol[0] & 0xF) << 12) | ((symbol[1] & 0x3F) << 6) | (symbol[2] & 0x3F);
	}
	return ULetters.find(ucode) == ULetters.end() ? 0 : 1;
}

int CCharsetUTF8::SymbolType(BYTE* symbol, int len)
{
	WORD ucode;
	if (symbol[0] < 0x80)
	{
		return 1;
	}
	if (symbol[0] < 0xE0)
	{
		ucode = ((symbol[0] & 0x1F) << 6) | (symbol[1] & 0x3F);
	}
	else
	{
		ucode = ((symbol[0] & 0xF) << 12) | ((symbol[1] & 0x3F) << 6) | (symbol[2] & 0x3F);
	}
	return ULetters.find(ucode) == ULetters.end() ? 0 : IsIdeograph(ucode) ? 2 : 1;
}

int CCharsetUTF8::RecodeStringLower(BYTE* src, WORD* dst, int maxlen)
{
	ULONG ucode;
	WORD* pdst = dst;
	while ((ucode = UCodeLower((const BYTE*&)src, 0)))
	{
		if (maxlen == 0)
		{
			*pdst = 0;
			return pdst - dst + 1;
		}
		*pdst++ = ucode;
		maxlen--;
	}
	*pdst = 0;
	return pdst - dst;
}

WORD CCharsetUTF8::UCode(const BYTE*& src)
{
	return ::UCode(src);
}

WORD CCharsetUTF8::UCodeLower(WORD code, int letter)
{
	if (code < 0x80) return (letter == 0) || (Charsets[0].wordch[code >> 3] & (1 << (code & 7))) ? tolower(code) : 0;
	hash_map<WORD, CCharU1*>::iterator it = ULetters.find(code);
	return it == ULetters.end() ? 0 : it->second->m_ucode[0];
}

WORD CCharsetUTF8::UCodeLower(const BYTE*& src, int letter)
{
	WORD code = UCode(src);
	if (code)
	{
		return UCodeLower(code, letter);
	}
	else
	{
		return 0;
	}
}

WORD CCharsetU2V::UCodeLower(const BYTE*& src, int letter)
{
	WORD code = UCode(src);
	if (code)
	{
		if (code < 0x80) return (letter == 0) || (Charsets[0].wordch[code >> 3] & (1 << (code & 7))) ? tolower(code) : 0;
		return code;
	}
	else
	{
		return 0;
	}
}

void CCharsetUTF8::Code(BYTE*& dst, WORD code, char notfound)
{
	Utf8Code(dst, code);
}

void AddAliasU1(const char* charset, const char* alias)
{
	LowerString(charsetu, charset)
	LowerString(aliasu, alias)
	return charsetMapU.AddAlias(charsetu, aliasu);
}

WORD* CGetUWord::GetWord(WORD **last)
{
	WORD *s;
	WORD *tok = NULL;
	int fl;

	if ((s = *last) == NULL)
		return NULL;

	// We find beginning of the word
	fl = 1;
	while (fl)
	{
		if(*s == 0)
		{
			*last = s;
			return NULL ;
		}

		if (IsLetter(*s))
		{
			tok = s;
			fl = 0;
		}
		s++;
	}
	// We find end of the word
	fl = 1;
	while (fl)
	{
		if (*s == 0)
		{
			*last = s;
			return tok;
		}
		fl = IsLetter(*s);
		if (fl == 0)
		{
			m_pchar = s;
			m_char = *s;
			*s = 0;
			*last = s + 1;
		}
		s++;
	}
	return tok;
}

WORD TolowerU(WORD ucode)
{
	hash_map<WORD, CCharU1*>::iterator it = ULetters.find(ucode);
	return it == ULetters.end() ? ucode : it->second->m_ucode[0];
}

#ifdef UNICODE
int ParseTag(CCharsetB* charset, CTag& tag, int& len, char* s, char*& e)
{
	// First, try to find corresponding ending > tag, skipping quoted text...
	// Try to be as forgiving as possible.  We balance quotes and handle
	// missplaced/broken quotes by requiring initial quote be directly
	// preceeded with an '=' char i.e. we only toggle on an opening quote
	// if we find a preceeding '=' char.  The one known exception here is
	// <tag name="value>.  However it seems (at time of writing) that
	// neither Netscape, IE or Opera handle this particular case
	// correctly. -matt
	WORD q = 0;
	int invert;
	int inquote = 0;
	int indquote = 0;
	int finish = 0;
	e = s;
	WORD code = 0;
	while (!finish && (code = charset->NextChar((const BYTE*&)e)))
	{
		switch (code)
		{
		case '>':
			if ((!inquote) && (!indquote))
				finish = 1;
			break;
		case '\'':
			if (!indquote)
			{
				invert = 1;
				if (!inquote && (q != '='))
				{
					invert = 0;
				}
				if (invert)
				{
					inquote = ~inquote;
				}
			}
			break;
		case '"':
			if (!inquote)
			{
				invert = 1;
				if (!indquote && (q != '='))
				{
					invert = 0;
				}
				if (invert)
				{
					indquote = ~indquote;
				}
			}
			break;
		case '\0':
			finish = 1;
			break;
		default:
			if ((code < 0x80) && !strchr(" \t\r\n", (char)code))
			{
				q = code;
			}
			break;
		}
	}
	len = e - s;
	char* tmp = Alloca(len + 1);
	memcpy(tmp, s, len);
	tmp[len] = 0;
	int t = tag.ParseTag(charset, (BYTE*&)tmp, len) - (BYTE*&)tmp;
	Freea(tmp, len + 1);
	return t;

}
#endif
/*
void CSet2::AddWord(WORD* word)
{
	if (*word)
	{
		CSet2*& set = (*this)[*word];
		if (set == NULL) set = new CSet2;
		set->AddWord(word + 1);
	}
	else
	{
		m_end = 1;
	}
}

WORD* CSet2::NextWord1(WORD* word)
{
	if (*word == 0) return word;
	iterator it = find(*word);
	if (it == end())
	{
		return m_end ? word : NULL;
	}
	else
	{
		WORD* w = it->second->NextWord1(word + 1);
		return w ? w : m_end ? word : NULL;
	}
}

WORD* CSet2::NextWord(WORD* word)
{
	WORD* nword = NextWord1(word);
	return nword == word ? word + 1 : nword;
}
*/
int CCharsetU2V::IsLetter(BYTE* symbol, int len)
{
	WORD ucode;
	if (symbol[0] < 0x80)
	{
		return 1;
	}
	ucode = (symbol[0] << 8) | symbol[1];
	return m_map[ucode & 0x7FFF] ? 1 : 0;
}

int CCharsetU2V::SymbolType(BYTE* symbol, int len)
{
	WORD code;
	if (symbol[0] < 0x80)
	{
		return 1;
	}
	code = (symbol[0] << 8) | symbol[1];
	WORD ucode = m_map[code & 0x7FFF];
	return ucode ? IsIdeograph(ucode) ? 2 : 1 : 0;
}

WORD CCharsetU2V::UCode(const BYTE*& src)
{
	ULONG code = NextChar(src);
	return code < 0x80 ? code : m_map[code & 0x7FFF];
}

void CCharsetU2V::Code(BYTE*& dst, WORD code, char notfound)
{
	if (code < 0x80)
	{
		*dst++ = (BYTE)code;
	}
	else
	{
		WORD ncode = m_revMap[code];
		if (ncode == 0)
		{
			*dst++ = notfound;
		}
		else
		{
			*dst++ = ncode >> 8;
			*dst++ = ncode & 0xFF;
		}
	}
}

WORD CCharsetU2V::NextChar(const BYTE*& src)
{
	if (src[0] < 0x80)
	{
		return *src ? *src++ : 0;
	}
	if (src[1])
	{
		WORD code = (src[0] << 8) | src[1];
		src += 2;
		return code;
	}
	else
	{
		src++;
		return 0;
	}
}

void CCharsetU2V::PutChar(BYTE*& dst, WORD character)
{
	if (character < 0x80)
	{
		*dst++ = (BYTE)character;
	}
	else
	{
		*dst++ = character >> 8;
		*dst++ = character & 0xFF;
	}
}

int CCharsetU2V::RecodeStringLower(BYTE* src, WORD* dst, int maxlen)
{
	ULONG ucode;
	WORD* pdst = dst;
	while ((ucode = UCodeLower((const BYTE*&)src)))
	{
		if (maxlen == 0)
		{
			*pdst = 0;
			return pdst - dst + 1;
		}
		*pdst++ = ucode;
		maxlen--;
	}
	*pdst = 0;
	return pdst - dst;
}

void CCharsetU2V::AddCodes(WORD code, WORD ucode)
{
	m_map[code & 0x7FFF] = ucode;
	m_revMap[ucode] = code;
	CCharU1*& codesU1 = ULetters[ucode];
	if (codesU1 == NULL) codesU1 = new CCharU1;
	codesU1->m_code[0] = codesU1->m_code[1] = code;
	codesU1->m_ucode[0] = codesU1->m_ucode[1] = ucode;
}

int LoadDictionary2(const char* lang, const char* file, const char* encoding)
{
	string path;
	GetFullPath(file, path);
	CCharsetB* charset = encoding ? GetUCharset(encoding) : NULL;
	FILE* f = fopen(path.c_str(), "r");
	if (f)
	{
		char str[1000];
		CWordSet*& set = Dictionaries2[lang];
		if (set == NULL) set = new CWordSet;
		while (fgets(str, sizeof(str), f))
		{
			WORD word[1000];
			WORD* pword = word;
			const BYTE* s = (const BYTE*)str;
			const BYTE* s1;
			while (true)
			{
				WORD code = charset ? charset->UCode(s) : (s1 = s, s += 2, *(WORD*)s1);
				if (code < 0x80)
				{
					*pword = 0;
					break;
				}
				*pword++ = code;
			}
			set->AddWord(word);
			idDictionary.AddWord(word);
		}
		fclose(f);
	}
	else
	{
		return -1;
	}
	return 0;
}

void CCharsetB::GetWords(WORD* sentence, vector<WORD*>& words)
{
	words.push_back(sentence + strwlen(sentence));
}

void CCharsetU2V::GetWords(WORD* sentence, vector<WORD*>& words)
{
	if (m_dictionary)
	{
		WORD* pword = sentence;
		while (*pword)
		{
			if (!IsIdeograph(*pword))
			{
				WORD* pw;
				for (pw = pword; (!IsIdeograph(*pw)) && (*pw != 0); pw++);
				pword = pw;
				words.push_back(pword);
				continue;
			}
			pword = m_dictionary->NextWord(pword);
			words.push_back(pword);
		}
	}
	else
	{
		words.push_back(sentence + strwlen(sentence));
	}
}

