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

/*  $Id: affix.cpp,v 1.7 2002/01/09 11:50:15 kir Exp $
    Author : Alexander F. Avdonkin
*/

#include "aspseek-cfg.h"

#include <stdio.h>
#include "affix.h"
#include "charsets.h"
#include "logger.h"

CMapEndingToAffix affixRules;
CMapFlagToRules flagRules;
CDictionary dictionary;

CURegFilter* BuildFilter(CHAR* expression);

CMapEndingToAffix& CMapEndingToAffix::GetMap(const CHAR* ending)
{
	if (ending[0] == 0)
	{
		return *this;
	}
	else
	{
		if (ending[1] == 0)
		{
			CMapEndingToAffix*& map1 = (*this)[ending[0]];
			if (map1 == NULL) map1 = new CMapEndingToAffix;
			return *map1;
		}
		else
		{
			CMapEndingToAffix*& map1 = (GetMap(ending + 1))[ending[0]];
			if (map1 == NULL) map1 = new CMapEndingToAffix;
			return *map1;
		}
	}
}

void CMapEndingToAffix::AddRule(CAffixRule* rule)
{
	GetMap(rule->m_ending.c_str()).m_rules.push_back(rule);
}

void CMapFlagToRules::AddRule(CAffixRule* rule)
{
	(*this)[rule->m_flag].push_back(rule);
}

/*
void AddRule(char flag, char* replacement, char* ending, char* regexp, char* lang)
{
	regex_t filter;
	if (regcomp(&filter, regexp, REG_EXTENDED | REG_ICASE) == 0)
	{
		CAffixRule* rule = new CAffixRule;
		rule->m_flag = flag;
		rule->m_filter = filter;
		rule->m_ending = ending;
		rule->m_replacement = replacement;
		rule->m_lang = lang;
		affixRules.AddRule(rule);
		flagRules.AddRule(rule);
	}
}
*/

void AddRule(char flag, CHAR* replacement, CHAR* ending, CHAR* regexp, char* lang)
{
	CURegFilter* rf;
	if ((rf = BuildFilter(regexp)))
	{
		CAffixRule* rule = new CAffixRule;
		rule->m_flag = flag;
		rule->m_filter = rf;
		rule->m_ending = ending;
		rule->m_replacement = replacement;
		rule->m_lang = lang;
		affixRules.AddRule(rule);
		flagRules.AddRule(rule);
	}
}

char* remove_spaces(char *dist, char *src)
{
	char *d, *s;
	d = dist;
	s = src;
	while (*s)
	{
		if (*s != ' ' && *s != '-' && *s != '\t')
		{
			*d = *s;
			d++;
		}
		s++;
	}
	*d = 0;
	return dist;
}

enum
{
	NO_STATE,
	SUFFIXES,
	PREFIXES
};

#ifdef UNICODE
int ImportAffixes(char *lang, char *filename, const char* charset)
{
	WORD umask[BUFSIZ];
	WORD ufind[BUFSIZ];
	WORD urepl[BUFSIZ];
	CCharsetBase* ucharset = GetU1Charset(charset);
#else
int ImportAffixes(char *lang, char *filename)
{
#endif
	char str[BUFSIZ];
	char flag = 0;
	char mask[BUFSIZ] = "";
	char find[BUFSIZ] = "";
	char repl[BUFSIZ] = "";
	char *s;
	int i;
	int state = NO_STATE;

	FILE *affix;

#ifndef UNICODE
	int LCharset;
	LCharset = GetDefaultCharset();
#endif
	if (!(affix = fopen(filename,"r")))
	{
		logger.log(CAT_ALL, L_ERR, "Can't open %s\n", filename);
		return 1;

	}
	logger.log(CAT_ALL, L_DEBUG, "Loaded affixes from: %s\n", filename);

	while (fgets(str, sizeof(str), affix))
	{
		if (!STRNCASECMP(str, "suffixes"))
		{
			state = SUFFIXES;
			continue;
		}
		if (!STRNCASECMP(str, "prefixes"))
		{
			state = PREFIXES;
			continue;
		}
		if (!STRNCASECMP(str, "flag "))
		{
			s = str + 5;
			while (strchr("* ", *s)) s++;
			flag = *s;
			continue;
		}
		if (state == NO_STATE) continue;

		if ((s = strchr(str,'#'))) *s = 0;
		if (!*str) continue;

#ifndef UNICODE
		Tolower((unsigned char*)str, LCharset);
#endif
		strcpy(mask,"");
		strcpy(find,"");
		strcpy(repl,"");

		i = sscanf(str,"%[^>\n]>%[^,\n],%[^\n]", mask, find, repl);
		if (strchr(find, '\'') || strchr(repl, '\''))
			continue; // skip it - we don't want words with apostrophes

		remove_spaces(str, repl); strcpy(repl, str);
		remove_spaces(str, find); strcpy(find, str);
		remove_spaces(str, mask); strcpy(mask, str);
//		sprintf(mask, state == SUFFIXES ? "%s$" : "^%s", str);

		switch(i)
		{
		case 3:
			if (*repl == '-')
			{
				repl[0] = 0;
			}
			break;
		case 2:
			if (*find != '-')
			{
				strcpy(repl, find);
				strcpy(find, "");
			}
			break;
		default:
			continue;
		}
#ifdef UNICODE
		ucharset->CodeLower(ufind, (BYTE*)find);
		ucharset->CodeLower(umask, (BYTE*)mask);
		ucharset->CodeLower(urepl, (BYTE*)repl);
		if (state == SUFFIXES)
			AddRule(flag, ufind, urepl, umask, lang);
#else
		if (state == SUFFIXES)
			AddRule(flag, find, repl, mask, lang);
#endif
	}
	fclose(affix);
	return 0;
}

#ifdef UNICODE
int ImportDictionary(char *lang, char *filename, const char* charset)
{
	CCharsetBase* ucharset = GetU1Charset(charset);
#else
int ImportDictionary(char *lang, char *filename)
{
#endif
	char str[BUFSIZ];
	char *s, *flag;
	FILE *dict;
	int LCharset;

	LCharset = GetDefaultCharset();
	if (!(dict = fopen(filename, "r")))
	{
		logger.log(CAT_ALL, L_ERR, "Can't open %s\n", filename);
		return 1;
	}
	logger.log(CAT_ALL, L_ERR, "Loaded dictionary %s from %s\n", lang, filename);

	while (fgets(str, sizeof(str), dict))
	{
		if ((flag = strchr(str, '/')))
		{
			*flag = 0;
			flag++; s = flag;
			while (*s)
			{
				if (*s >= 'A' && *s <= 'Z') s++;
				else
				{
					*s = 0;
					break;
				}
			}
		}
		else
		{
			flag = "";
		}
#ifndef UNICODE
		Tolower((unsigned char*)str, LCharset);
#endif
		s = str;
		while (*s)
		{
			if (*s == '\r') *s = 0;
			if (*s == '\n') *s = 0;
			s++;
		}
#ifdef UNICODE
		WORD ustr[BUFSIZ];
		ucharset->CodeLower(ustr, (BYTE*)str);
		CFlags& flags = dictionary[ustr];
#else
		CFlags& flags = dictionary[str];
#endif
		flags.m_flags = flag;
		flags.m_lang = lang;
	}
	fclose(dict);
	return 0;
}

void AddForms(CHAR* word, CHAR* pe, hash_map<CSWord, CFlags>& forms, CAffixRule* rule, const char* langs)
{
	if (pe - word + STRLEN(rule->m_replacement.c_str()) <= MAX_WORD_LEN)
	{
		CSWord sword;
		memcpy(sword.m_content, word, (char*)pe - (char*)word);
		STRCPY(sword.m_content + (pe - word), rule->m_replacement.c_str());
		CDictionary::iterator it = dictionary.find(sword);
		if (it == dictionary.end())
			return;
		if ((langs) && (langs[0]!='\0'))
		{
			// Check list of allowed langs
			string lang(",");
			lang.append(it->second.m_lang.c_str());
			lang.append(",");
			if (!strstr(langs, lang.c_str()))
				return;
		}
		if (rule->Match(sword.c_str(), it->second.m_lang.c_str()))
		{
			if (strchr(it->second.m_flags.c_str(), rule->m_flag))
			{
				forms[sword] = it->second;
			}
		}
	}
}

void FindForms(CHAR* word, hash_set<CSWord>& forms, const char *langs)
{
	hash_map<CSWord, CFlags> normal;
	if (*word == 0) return;
	CHAR* pend = word + STRLEN(word);
	CMapEndingToAffix* map = &affixRules;
	CDictionary::iterator it = dictionary.find(word);

	if (it == dictionary.end())
	{
		normal[word];
	}
	else
	{
		if ((langs) && (langs[0]!='\0'))
		{
			// Check list of allowed langs
			string lang(",");
			lang.append(it->second.m_lang.c_str());
			lang.append(",");
			if (strstr(langs, lang.c_str()))
			{
				normal[word] = it->second;
			}
			else
			{
				normal[word];
			}
		}
		else
		{
			normal[word] = it->second;
		}
	}
	while (pend >= word)
	{
		for (vector<CAffixRule*>::iterator rule = map->m_rules.begin(); rule != map->m_rules.end(); rule++)
		{
			AddForms(word, pend, normal, *rule, langs);
		}
		if (--pend < word) break;
		CMapEndingToAffix::iterator it = map->find(*pend);
		if (it != map->end())
		{
			map = it->second;
		}
		else
		{
			break;
		}
	}

	for (hash_map<CSWord, CFlags>::iterator itw = normal.begin(); itw != normal.end(); itw++)
	{
		forms.insert(itw->first);
		for (const char* pflag = itw->second.m_flags.c_str(); *pflag; pflag++)
		{
			CMapFlagToRules::iterator itf = flagRules.find(*pflag);
			if (itf != flagRules.end())
			{
				CAffixRules& rules = itf->second;
				for (vector<CAffixRule*>::iterator rule = rules.begin(); rule != rules.end(); rule++)
				{
					if ((*rule)->Match(itw->first.c_str(), itw->second.m_lang.c_str()))
					{
						int rlen = STRLEN((*rule)->m_replacement.c_str());
						int wlen = STRLEN(itw->first.c_str());
						int flen = STRLEN((*rule)->m_ending.c_str());
						if (wlen - rlen + flen <= MAX_WORD_LEN)
						{
							if ((rlen == 0) || ((wlen >= rlen) && (STRCMP(itw->first.c_str() + wlen - rlen, (*rule)->m_replacement.c_str()) == 0)))
							{
								CSWord sword;
								memcpy(sword.m_content, itw->first.c_str(), (wlen - rlen) * sizeof(CHAR));
								STRCPY(sword.m_content + wlen - rlen, (*rule)->m_ending.c_str());
								forms.insert(sword);
							}
						}
					}
				}
			}
		}
	}
}

#ifdef UNICODE
void PrintUTF8(WORD code)
{
	if ((code & 0xFF80) == 0)
	{
		printf("%c", code);
	}
	else if ((code & 0xF800) == 0)
	{
		printf("%c%c", 0xC0 | (code >> 6), 0x80 | (code & 0x3F));
	}
	else
	{
		printf("%c%c%c", 0xE0 | (code >> 12), 0x80 | ((code >> 6) & 0x3F), 0x80 | (code & 0x3F));
	}
}

void PrintUTF8(const WORD* word)
{
	while (*word)
	{
		PrintUTF8(*word++);
	}
};

void TestSpell(const char* fname, const char* charset)
{
	CCharsetBase* ucharset = GetU1Charset(charset);
	printf("Content-type: text/html\r\n\r\n");
	printf("<html><head>\r\n");
	printf("<meta http-equiv=\"Content-type\" content=\"text/html; charset=utf-8\" />\r\n");
	printf("</head>\r\n<body>\r\n");
#else
void TestSpell(const char* fname)
{
#endif
	FILE* f = fopen(fname, "r");
	if (f)
	{
		char str[BUFSIZ];
		while (fgets(str, sizeof(str), f))
		{
			char word[BUFSIZ];
			hash_set<CSWord> forms;
			if (sscanf(str, "%[^/\n\r]", word) >= 1)
			{
#ifdef UNICODE
				WORD uword[BUFSIZ];
				ucharset->CodeLower(uword, (BYTE*)word);
				FindForms(uword, forms, "");
				PrintUTF8(uword);
				printf(" %i", forms.size());
				for (hash_set<CSWord>::iterator it = forms.begin(); it != forms.end(); it++)
				{
					printf(" ");
					PrintUTF8(it->c_str());
				}
				printf("<br />");
#else
				Tolower((unsigned char*)word, GetDefaultCharset());
				FindForms(word, forms, "");
				printf("%32s %i", word, forms.size());
				for (hash_set<CSWord>::iterator it = forms.begin(); it != forms.end(); it++)
				{
					printf(" %s", it->c_str());
				}
				printf("\n");
#endif
			}
		}
		fclose(f);
#ifdef UNICODE
		printf("</body>");
#endif
	}
	else
	{
		printf("Can't open file: %s\n", fname);
	}
}

int CUSymSet::MatchN(CHAR symbol)
{
	for (int i = 0; i < m_count; i++)
	{
		if (m_symbols[i] == symbol)
		{
			return 1;
		}
	}
	return 0;
}

int CURegFilter::Match(const CHAR* str)
{
	unsigned long len = STRLEN(str);
	if (len < m_symbols.size()) return 0;
	const CHAR* s = str + len - m_symbols.size();
	vector<CUSymbol*>::iterator sym = m_symbols.begin();
	while (*s)
	{
		if (!(*sym)->Match(*s)) return 0;
		sym++;
		s++;
	}
	return 1;
};

CUSymSet* GetSet(CHAR*& expression)
{
	CHAR syms[0x100];
	CHAR* psym = syms;
	int neg = 0;
	if (*expression == '^')
	{
		neg = 1;
		expression++;
	}
	while (*expression != ']')
	{
		if (*expression == 0) return NULL;
		*psym++ = *expression;
		expression++;
	}
	expression++;
	CUSymSet* ss = new CUSymSet;
	ss->m_negate = neg;
	ss->m_count = psym - syms;
	ss->m_symbols = new CHAR[ss->m_count];
	memcpy(ss->m_symbols, syms, (char*)psym - (char*)syms);
	return ss;
}

CUSymbol* GetUSymbol(CHAR*& expression)
{
	switch (*expression)
	{
	case '.':
		expression++;
		return new CAnySymbol;
	case '[':
		return GetSet(++expression);
	case '\0':
		return NULL;
	default:
		return new CULetter(*expression++);
	}
}

CURegFilter* BuildFilter(CHAR* expression)
{
	CUSymbol* sym;
	CURegFilter* rf = new CURegFilter;
	while ((sym = GetUSymbol(expression)))
	{
		rf->m_symbols.push_back(sym);
	}
	return rf;
}

