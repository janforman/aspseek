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

/*  $Id: charsets.cpp,v 1.38 2002/06/18 10:57:23 kir Exp $
    Author : Alexander F. Avdonkin
	Uses parts of UdmSearch code
	Implemented classes: CharsetMap, CGetWord
*/

#include "aspseek-cfg.h"

#include <string.h>
#include <stdio.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "config.h"
#include "charsets.h"
#include "defines.h"
#include "logger.h"
#include "paths.h"
#ifdef UNICODE
#include "ucharset.h"
#endif
#include "misc.h"

static unsigned char usascii[]="";

CSgmlCharMapI SgmlCharMap;

TCHARSET Charsets[MAX_CHARSET] ={
	{CHARSET_USASCII, "en", "usascii", usascii, "", "", ""},
	{-1, "", NULL, NULL, "", "", ""}
};

static ALIAS Aliases[MAX_ALIAS] ={
	{CHARSET_USASCII, "us-ascii"},
	{CHARSET_USASCII, "ascii"},
	{CHARSET_USASCII, "7bit"},
	{CHARSET_USASCII, "7-bit"},
	{CHARSET_USASCII, "iso-ir-6"},
	{CHARSET_USASCII, "ansi_x3.4"},
	{-1, NULL}
};

CharsetMap charsetMap;

CharsetMap::CharsetMap()
{
	char lcharset[100];
	int i=0;
	memset(m_charsets, 0xFF, sizeof(m_charsets));

	while (Aliases[i].alias && i < MAX_ALIAS)
	{
		char *c, *d;
		int charset = Aliases[i].charset;
		if (m_charsets[charset] < 0) m_charsets[charset] = i;
		for (c = Aliases[i].alias, d = lcharset; *c; c++, d++) *d = tolower(*c);
		*d = 0;
		(*this)[lcharset] = charset;
		i++;
	}
}

void CharsetMap::AddAliasMap(int charsetid, const char *alias)
{
	char lcharset[100];
	char *c, *d;
	int aliasid=-1, i;

	for (i = 0; i< MAX_ALIAS; i++)
	{
		if (!Aliases[i].alias)
		{
			aliasid=i;
			break;
		}
		else if (!strcasecmp(Aliases[i].alias, alias))
		{
			return;
		}
	}

	if (aliasid == -1)
		return;

	Aliases[aliasid].charset = charsetid;
	Aliases[aliasid].alias = strdup(alias);

	int charset = Aliases[aliasid].charset;
	if (m_charsets[charset] < 0) m_charsets[charset] = aliasid;
	for (c = Aliases[aliasid].alias, d = lcharset; *c; c++, d++) *d = tolower(*c);
	*d = 0;
	(*this)[lcharset] = charset;
}

int CharsetMap::GetCharset(const char* alias)
{
	char lcharset[100], *d;
	const char *c;
	if (!alias) return CHARSET_USASCII;
	for (c = alias, d = lcharset; *c; c++, d++)
	{
		if ((unsigned)(d - lcharset) >= sizeof(lcharset) - 1) return CHARSET_USASCII;
		*d = tolower(*c);
	}
	*d = 0;
	iterator it = find(lcharset);
	return it == end() ? CHARSET_USASCII : it->second;
}

const char* CharsetMap::CharsetsStr(int charset)
{
	return (charset >= 0) && (charset < MAX_CHARSET) && (Charsets[charset].name) ? Aliases[m_charsets[charset]].alias : NULL;
}

int GetCharset(const char *alias)
{
	return charsetMap.GetCharset(alias);
}


static int LCindex = 0;

int SetDefaultCharset(int id)
{
	if (id != CHARSET_DEFAULT) LCindex=id;
	return LCindex;
}

int GetDefaultCharset()
{
	return LCindex;
}

char* Trim(char *p, char *delim)
{
	int len;
	len = strlen(p);
	while ((len > 0) && strchr(delim, p[len - 1]))
	{
		p[len - 1] = '\0';
		len--;
	}
	while ((*p) && strchr(delim,*p)) p++;
	return p;
}

char* CGetWord::GetWord(unsigned char **last, int charset)
{
	unsigned char *s;
    unsigned char *tok = NULL;
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

		if ((Charsets[charset].wordch[*s >> 3] & (1 << (*s & 7))) != 0)
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
			return (char*)tok;
		}
		fl = (Charsets[charset].wordch[*s >> 3] & (1 << (*s & 7))) == 0 ? 0 : 1;
		if (fl == 0)
		{
			m_pchar = s;
			m_char = *s;
			*s = 0;
			*last = s + 1;
		}
		s++;
    }
    return (char*)tok;
}

char* GetToken(char *s, const char *delim, char **last)
{
    char *spanp;
    int c, sc;
    char *tok;

    if (s == NULL && (s = *last) == NULL)
	return NULL;

cont:
    c = *s++;
    for (spanp = (char *)delim; (sc = *spanp++) != 0; )
    {
		if (c == sc)
		{
			goto cont;
		}
    }

    if (c == 0)		// no non-delimiter characters
    {
		*last = NULL;
		return NULL;
    }
    tok = s - 1;

    for (;;)
    {
		c = *s++;
		spanp = (char *)delim;
		do
		{
			if ((sc = *spanp++) == c)
			{
				if (c == 0)
				{
					s = NULL;
				}
				else
				{
					char *w = s - 1;
					*w = '\0';
				}
				*last = s;
				return tok;
			}
		}
		while (sc != 0);
    }
}

int FillLowerString(unsigned char *str,TABLE table)
{
	unsigned int i,len;

	len = strlen((char*)str) / 2;
	for (i = 0;i < 256; i++) table[i] = tolower((unsigned char)i);
	for (i = 0;i < len; i++)
	{
		table[str[i]]=str[i+len];
	}
	return 0;
}

int FillUpperString(unsigned char *str,TABLE table)
{
	unsigned int i,len;

	len = strlen((char*)str) / 2;
	for (i = 0;i < 256; i++) table[i] =  toupper((unsigned char)i);
	for (i = 0;i < len; i++)
	{
		table[str[i+len]]=str[i];
	}
	return 0;
}

void MakeWordRange(TCHARSET* charset)
{
	memset(charset->wordch, 0, sizeof(charset->wordch));
	unsigned char* pc = charset->chars;
	while (*pc)
	{
		charset->wordch[*pc >> 3] |= (1 << (*pc & 7));
		pc++;
	}
	pc = (unsigned char*)WORDCHAR;
	while (*pc)
	{
		charset->wordch[*pc >> 3] |= (1 << (*pc & 7));
		pc++;
	}
}

void InitCharset0()
{
	MakeWordRange(Charsets);
}

int InitCharset()
{
	int i=0;
	while (Charsets[i].name && i<MAX_CHARSET)
	{
		FillUpperString(Charsets[i].chars,Charsets[i].upper);
		FillLowerString(Charsets[i].chars,Charsets[i].lower);
		MakeWordRange(&Charsets[i]);
		i++;
	}
	return 0;
}


int FillRecodeString(unsigned char *from, unsigned char *to,TABLE table)
{
	unsigned int len,i;

	for (i = 0; i < 256; i++) table[i] = (unsigned char)i;
	len = strlen((char*)from);
	if (len != strlen((char*)to)) return 1;
	for (i = 0; i < len; i++)
	{
		if (from[i] != to[i])
			table[(unsigned int)from[i]] = to[i];
	}
	return 0;
}


char* Recode(char *str,int from,int to)
{
	char *s;
	TABLE t;
	if (!(s = str)) return NULL;
	if ((from >= MAX_CHARSET) || !Charsets[from].name ) return NULL;
	if ((to >= MAX_CHARSET) || !Charsets[to].name) return NULL;
	if (from == to) return str;
	if (FillRecodeString(Charsets[from].chars, Charsets[to].chars, t)) return NULL;
	while (*s)
	{
		*s = t[(unsigned char)*s];
		s++;
	}
	return str;
}

int CanRecode(int from, int to)
{
	if ((from >= MAX_CHARSET) || !Charsets[from].name ) return 0;
	if ((to >= MAX_CHARSET) || !Charsets[to].name) return 0;
	if (from == to) return 1;
	return strlen((char*)Charsets[from].chars) == strlen((char*)Charsets[to].chars);
}

static struct
{
    char		*sgml;
    unsigned char	equiv;
} SGMLChars[] =
  {
    { "lt",	      '<' } ,
    { "gt",	      '>' } ,
    { "amp",	      '&' } ,
    { "quot",	      '"' } ,
    { "nbsp",         ' ' } , /* non breaking space */
    { "Nbsp",         ' ' } , /* non breaking space */

    { "trade",	      153 } , /* trade mark */
    { "iexcl",        161 } , /* inverted exclamation mark */
    { "cent",         162 } , /* cent sign */
    { "pound",        163 } , /* pound sign */
    { "curren",       164 } , /* currency sign */
    { "yen",          165 } , /* yen sign */
    { "brvbar",       166 } , /* broken vertical bar, (brkbar) */
    { "sect",         167 } , /* section sign */
    { "uml",          168 } , /* spacing diaresis */
    { "copy",         169 } , /* copyright sign */
    { "ordf",         170 } , /* feminine ordinal indicator */
    { "laquo",        171 } , /* angle quotation mark, left */
    { "not",          172 } , /* negation sign */
    { "shy",          173 } , /* soft hyphen */
    { "reg",          174 } , /* circled R registered sign */
    { "hibar",        175 } , /* spacing macron */
    { "deg",          176 } , /* degree sign */
    { "plusmn",       177 } , /* plus-or-minus sign */
    { "sup2",         178 } , /* superscript 2 */
    { "sup3",         179 } , /* superscript 3 */
    { "acute",        180 } , /* spacing acute (96) */
    { "micro",        181 } , /* micro sign */
    { "para",         182 } , /* paragraph sign */
    { "middot",       183 } , /* middle dot */
    { "cedil",        184 } , /* spacing cedilla */
    { "sup1",         185 } , /* superscript 1 */
    { "ordm",         186 } , /* masculine ordinal indicator */
    { "raquo",        187 } , /* angle quotation mark, right */
    { "frac14",       188 } , /* fraction 1/4 */
    { "frac12",       189 } , /* fraction 1/2 */
    { "frac34",       190 } , /* fraction 3/4 */
    { "iquest",       191 } , /* inverted question mark */
    { "Agrave",       192 } , /* capital A, grave accent */
    { "Aacute",       193 } , /* capital A, acute accent */
    { "Acirc",        194 } , /* capital A, circumflex accent */
    { "Atilde",       195 } , /* capital A, tilde */
    { "Auml",         196 } , /* capital A, dieresis or umlaut mark */
    { "Aring",        197 } , /* capital A, ring */
    { "AElig",        198 } , /* capital AE diphthong (ligature) */
    { "Ccedil",       199 } , /* capital C, cedilla */
    { "Egrave",       200 } , /* capital E, grave accent */
    { "Eacute",       201 } , /* capital E, acute accent */
    { "Ecirc",        202 } , /* capital E, circumflex accent */
    { "Euml",         203 } , /* capital E, dieresis or umlaut mark */
    { "Igrave",       205 } , /* capital I, grave accent */
    { "Iacute",       204 } , /* capital I, acute accent */
    { "Icirc",        206 } , /* capital I, circumflex accent */
    { "Iuml",         207 } , /* capital I, dieresis or umlaut mark */
    { "ETH",          208 } , /* capital Eth, Icelandic (Dstrok) */
    { "Ntilde",       209 } , /* capital N, tilde */
    { "Ograve",       210 } , /* capital O, grave accent */
    { "Oacute",       211 } , /* capital O, acute accent */
    { "Ocirc",        212 } , /* capital O, circumflex accent */
    { "Otilde",       213 } , /* capital O, tilde */
    { "Ouml",         214 } , /* capital O, dieresis or umlaut mark */
    { "times",        215 } , /* multiplication sign */
    { "Oslash",       216 } , /* capital O, slash */
    { "Ugrave",       217 } , /* capital U, grave accent */
    { "Uacute",       218 } , /* capital U, acute accent */
    { "Ucirc",        219 } , /* capital U, circumflex accent */
    { "Uuml",         220 } , /* capital U, dieresis or umlaut mark */
    { "Yacute",       221 } , /* capital Y, acute accent */
    { "THORN",        222 } , /* capital THORN, Icelandic */
    { "szlig",        223 } , /* small sharp s, German (sz ligature) */
    { "agrave",       224 } , /* small a, grave accent */
    { "aacute",       225 } , /* small a, acute accent */
    { "acirc",        226 } , /* small a, circumflex accent */
    { "atilde",       227 } , /* small a, tilde */
    { "auml",         228 } , /* small a, dieresis or umlaut mark */
    { "aring",        229 } , /* small a, ring */
    { "aelig",        230 } , /* small ae diphthong (ligature) */
    { "ccedil",       231 } , /* small c, cedilla */
    { "egrave",       232 } , /* small e, grave accent */
    { "eacute",       233 } , /* small e, acute accent */
    { "ecirc",        234 } , /* small e, circumflex accent */
    { "euml",         235 } , /* small e, dieresis or umlaut mark */
    { "igrave",       236 } , /* small i, grave accent */
    { "iacute",       237 } , /* small i, acute accent */
    { "icirc",        238 } , /* small i, circumflex accent */
    { "iuml",         239 } , /* small i, dieresis or umlaut mark */
    { "eth",          240 } , /* small eth, Icelandic */
    { "ntilde",       241 } , /* small n, tilde */
    { "ograve",       242 } , /* small o, grave accent */
    { "oacute",       243 } , /* small o, acute accent */
    { "ocirc",        244 } , /* small o, circumflex accent */
    { "otilde",       245 } , /* small o, tilde */
    { "ouml",         246 } , /* small o, dieresis or umlaut mark */
    { "divide",       247 } , /* division sign */
    { "oslash",       248 } , /* small o, slash */
    { "ugrave",       249 } , /* small u, grave accent */
    { "uacute",       250 } , /* small u, acute accent */
    { "ucirc",        251 } , /* small u, circumflex accent */
    { "uuml",         252 } , /* small u, dieresis or umlaut mark */
    { "yacute",       253 } , /* small y, acute accent */
    { "thorn",        254 } , /* small thorn, Icelandic */
    { "yuml",         255 }  /* small y, dieresis or umlaut mark */
};

#define SGML_CHAR_NUM (sizeof(SGMLChars) / sizeof(SGMLChars[0]))

CSgmlCharMapI::CSgmlCharMapI()
{
	for (unsigned int i = 0; i < SGML_CHAR_NUM; i++)
	{
		AddSgml(SGMLChars[i].sgml, SGMLChars[i].equiv);
	}
}

void CSgmlCharMap::AddSgml(const char* sgml, WORD code)
{
	if (*sgml == 0)
	{
		m_code = code;
	}
	else
	{
		CSgmlCharMap*& map = (*this)[*sgml];
		if (map == NULL) map = new CSgmlCharMap;
		map->AddSgml(sgml + 1, code);
	}
}

WORD CSgmlCharMap::GetCode(const char*& sgml)
{
	if (m_code)
	{
		return m_code;
	}
	else
	{
		if (isalnum(*sgml))
		{
			iterator it = find(*sgml);
			if (it == end())
			{
				return 0;
			}
			else
			{
				++sgml;
				return it->second->GetCode(sgml);
			}
		}
		else
		{
			return 0;
		}
	}
}

char *Tolower(unsigned char *str, int charset)
{
	unsigned char *s;
	TABLE *t;

	if (!(s = str)) return NULL;
	if (charset < MAX_CHARSET)
	{
		t=&(Charsets[charset].lower);
		s = str;
		while (*s)
		{
			*s = (*t)[*s];
			s++;
		}
	}
	else
	{
		while(*s)
		{
			*s = tolower(*s);
			s++;
		}
	}
	return (char*)str;
}


char* Toupper(unsigned char *str, int charset)
{
	unsigned char *s;
	TABLE *t;

	if (!(s = str)) return NULL;
	if (charset < MAX_CHARSET)
	{
		t=&(Charsets[charset].upper);
		s = str;
		while (*s)
		{
			*s=(*t)[*s];
			s++;
		}
	}
	else
	{
		while (*s)
		{
			*s = toupper(*s);
			s++;
		}
	}
	return (char*)str;
}

#define Alloca(n) n > 10000 ? new char[n] : (char*)alloca(n)
#define Freea(x, n) if (n > 10000) delete x;

WORD SgmlToChar(const char*& sgml, int charset)
{
	WORD code;
	if (*sgml == '#')
	{
		sgml++;
		code = 0;
		while (isdigit(*sgml))
		{
			code = code * 10 + (*sgml - '0');
			sgml++;
		}
	}
	else
	{
		code = SgmlCharMap.GetCode(sgml);
	}
	if (*sgml == ';') sgml++;
	return code;
/*	int i;
	unsigned char res[2];

	for(i = 0; i < SGML_CHAR_NUM; i++)
	{
		if (!strncasecmp(sgml, SGMLChars[i].sgml, strlen(SGMLChars[i].sgml)))
		{
			res[0] = SGMLChars[i].equiv; res[1] = 0;
			if (islower(*(unsigned char*)(sgml)))
			{
				Tolower(res,charset);
			}
			else
			{
				Toupper(res,charset);
			}
			return (char)res[0];
		}
	}
	return 0;*/
}

/// Allocates memory and copies src to dest. If dest is not NULL deletes it first.
inline void strnewcpy(char *& dest, char * src)
{
	if (dest != NULL)
	{
//		printf("Bug: dest!=NULL %s\n", dest);
		delete dest;
	}
	dest = new char[strlen(src) + 1];
	strcpy(dest, src);
}

char* CTag::ParseTag(char *stag)
{
	char *s, *e, *p = stag, *ec = stag;
	int len;

	len = strlen(stag);
	s = stag + 1;
	e = s;
	while (!strchr(" \t\r\n>", *e)) e++;
	*e = 0;
	strnewcpy(m_name, s);
	e++;
	SKIP(e, " \t\r\n>");
	while (e - stag < len)
	{
		s = e;
		SKIPN(e, " \t\r\n=>");
		if(*e != '=')
		{
			*e++ = 0;
			SKIP(e, " \t\r\n>");
		}
		if(*e == '=')
		{
			*e = 0; e++;
			SKIP(e, " \t\r\n>");
			p = e;
			if(*p == '"')
			{
				p++; e++;
				SKIPN(e, "\">");
			}
			else if (*p == '\'')
			{
				p++; e++;
				SKIPN(e, "'>");
			}
			else
			{
				SKIPN(e, " \t\r\n>");
			}
			*e = 0; e++;

			if (!strcasecmp(s,"href"))
			{
				strnewcpy(m_href, p);
			}
			else if (!strcasecmp(s,"value"))
			{
				strnewcpy(m_value, p);
			}
			else if (!strcasecmp(s,"selected"))
			{
				strnewcpy(m_selected, p);
			}
			else if (!strcasecmp(s,"checked"))
			{
				strnewcpy(m_checked, p);
			}
			else if (!strcasecmp(s,"src"))
			{
				strnewcpy(m_src, p);
			}
			else if (!strcasecmp(s,"content"))
			{
				strnewcpy(m_content, p);
				ec = p;
			}
			else if (!strcasecmp(s,"name") || !strcasecmp(s,"http-equiv"))
			{
				strnewcpy(m_equiv, p);
			}
			else if (!strcasecmp(s,"size"))
			{
				strnewcpy(m_size, p);
			}
		}
	}
	return ec;
}

#ifdef UNICODE
WORD USkip(CCharsetB* charset, BYTE*& s, const char* set)
{
	WORD code;
	BYTE* s1;
	while ((s1 = s, code = charset->NextChar((const BYTE*&)s)) && (code < 0x80) && (strchr(set, (char)code)));
	s = s1;
	return code;
}

WORD USkipN(CCharsetB* charset, BYTE*& s, const char* set)
{
	WORD code;
	BYTE* s1;
	while ((s1 = s, code = charset->NextChar((const BYTE*&)s)) && ((code >= 0x80) || (!strchr(set, (char)code))));
	s = s1;
	return code;
}

BYTE* CTag::ParseTag(CCharsetB* charset, BYTE *stag, int len)
{
	BYTE *s, *p = stag, *ec = stag;
	BYTE* e;
	s = stag + 1;
	e = s;
	WORD code;
	BYTE* e1;
	while ((e1 = e, code = charset->NextChar((const BYTE*&)e)) && ((code >= 0x80) || !strchr(" \t\r\n>", (char)code)));
	*e1 = 0;
	strnewcpy(m_name, (char*)s);
	USkip(charset, e, " \t\r\n>");
	while (e - stag < len)
	{
		s = e;
		WORD code = USkipN(charset, e, " \t\r\n=>");
		if (code != '=')
		{
			*e++ = 0;
			code = USkip(charset, e, " \t\r\n>");
		}
		if (code == '=')
		{
			*e = 0; e++;
			code = USkip(charset, e, " \t\r\n>");
			p = e;
			if (code == '"')
			{
				p++; e++;
				USkipN(charset, e, "\">");
			}
			else if (code == '\'')
			{
				p++; e++;
				USkipN(charset, e, "'>");
			}
			else
			{
				USkipN(charset, e, " \t\r\n>");
			}
			*e = 0; e++;

			if (!strcasecmp((char*)s, "href"))
			{
				strnewcpy(m_href, (char*)p);
			}
			else if (!strcasecmp((char*)s, "value"))
			{
				strnewcpy(m_value, (char*)p);
			}
			else if (!strcasecmp((char*)s, "selected"))
			{
				strnewcpy(m_selected, (char*)p);
			}
			else if (!strcasecmp((char*)s, "checked"))
			{
				strnewcpy(m_checked, (char*)p);
			}
			else if (!strcasecmp((char*)s, "src"))
			{
				strnewcpy(m_src, (char*)p);
			}
			else if (!strcasecmp((char*)s, "content"))
			{
				strnewcpy(m_content, (char*)p);
				ec = p;
			}
			else if (!strcasecmp((char*)s, "name") || !strcasecmp((char*)s, "http-equiv"))
			{
				strnewcpy(m_equiv, (char*)p);
			}
			else if (!strcasecmp((char*)s, "size"))
			{
				strnewcpy(m_size, (char*)p);
			}
		}
	}
	return ec;
}
#endif

int WordChar(unsigned char s, int charset)
{
	return Charsets[charset].wordch[s >> 3] & (1 << (s & 7)) ? 1 : 0;
}
/*
double timedif(struct timeval& tm1, struct timeval& tm)
{
	return (tm1.tv_sec - tm.tv_sec) + (tm1.tv_usec - tm.tv_usec) / 1000000.0;
}
*/
int ParseTag(CTag& tag, int& len, char* s, char*& e)
{
	// First, try to find corresponding ending > tag, skipping quoted text...
	// Try to be as forgiving as possible.  We balance quotes and handle
	// misplaced/broken quotes by requiring initial quote be directly
	// preceeded with an '=' char i.e. we only toggle on an opening quote
	// if we find a preceeding '=' char.  The one known exception here is
	// <tag name="value>.  However it seems (at time of writing) that
	// neither Netscape, IE or Opera handle this particular case
	// correctly. -matt
	char q = 0;
	int invert;
	int inquote = 0;
	int indquote = 0;
	int finish = 0;
	e = s;
	do {
		e++;
		switch (*e)
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
			if (!strchr(" \t\r\n", *e))
			{
				q = *e;
			}
			break;
		}
	} while (!finish);
	len = e - s + (*e ? 1 : 0);
	char* tmp = Alloca(len + 1);
	strncpy(tmp, s, len);
	tmp[len] = 0;
	char* t = tag.ParseTag(tmp);
	Freea(tmp, len + 1);
	return t - tmp;
}

int LoadCharset(const char *lang, const char *name, const char* file)
{
	FILE *tbl_f;
	char str[256], *chp;
	unsigned char charset[256];
	unsigned int ch;
	int line = 0, id=0;
	string path;

	if (isAbsolutePath(file))
		path = file;
	else
	{
		path = CONF_DIR;
		path.append("/");
		path.append(file);
	}

	if (!(tbl_f = fopen(path.c_str(), "r")))
	{
		logger.log(CAT_FILE, L_ERR, "Can't open charset table file %s: %s\n", file, strerror(errno));
		return -1;
	}
	logger.log(CAT_ALL, L_DEBUG, "Loading charset [%s] from table %s\n", name, file);
	memset(charset, 0, 256);
	while (fgets(str, sizeof(str), tbl_f))
	{
		line++;
		chp = Trim(str, " \t\n\r");
		if (!*chp || *chp == '#')
			continue;
		if (sscanf(chp, "0x%x", &ch) !=1)
		{
			ch=(unsigned char)chp[0];
		}
		if (ch && ch < 256)
		{
			charset[id]=ch;
			id++;
		}
		else
		{
			logger.log(CAT_ALL, L_WARNING, "Bad character <%s> in table  %s\n", chp, file);
		}
	}
	fclose(tbl_f);
	return AddCharset(lang, name, charset);
}

int AddCharset(const char *lang, const char *name, unsigned char *charset)
{
	int i;

	if ((i = GetCharset(name)) != CHARSET_USASCII)
	{
		logger.log(CAT_ALL, L_WARN, "Charset %s is already loaded - skipping\n", name);
		return 1;
	}

	for(i=0; i<MAX_CHARSET; i++)
	{
		if (!Charsets[i].name)
		{
			Charsets[i].charset = i;
			strncpy(Charsets[i].lang, lang, 2);
			Charsets[i].name = strdup(name);
			Charsets[i].chars =(unsigned char*) strdup((char *)charset);
			charsetMap.AddAliasMap(i, name);
			return i;
		}
	}
	return -1;
}

void AddAlias(const char *charset, char *aliases)
{
	char *s,*lt;
#ifndef UNICODE
	int charsetid;
	if ((charsetid = GetCharset(charset)) == CHARSET_USASCII)
	{
		logger.log(CAT_ALL, L_ERR, "Add aliases for charset %s skipped no such charset\n", charset);
		return;
	}
#endif
	s = GetToken(aliases, " \t", &lt);
	while (s)
	{
#ifdef UNICODE
		AddAliasU1(charset, s);
#else
		charsetMap.AddAliasMap(charsetid, s);
#endif
		s = GetToken(NULL, " \t",&lt);
	}
}

#ifdef USE_CHARSET_GUESSER
CLangs langs;

char *CLmText::GetBlock(const char *src, char *dst, int len)
{
	const unsigned char *sp = (const unsigned char*) src;
	int tlen = len;
	int i = 0;
	int fl = 0;

	while (*sp && !(*sp & 128) && tlen)
	{
		sp++;
		tlen--;
	}

	while (tlen && *sp && i < MAX_TEXTLEN)
	{

		while (i < MAX_TEXTLEN && tlen && *sp)
		{
			if (*sp <= '9')
			{
				if (!fl)
				{
					dst[i++] = ' ';
				}
				fl = 1;
			}
			else if (*sp == '<')
			{
				if (!fl)
				{
					dst[i++] = ' ';
				}
				fl = 1;
				break;
			}
			else
			{
				dst[i++] = *sp;
				fl = 0;
			}
			sp++;
			tlen--;
		}
		while (*sp && (*sp++ != '>') && (tlen) && (i < MAX_TEXTLEN))
		{
			tlen--;
		}
	}
	dst[i] = 0;
	return dst;
}

void CLmText::MakeLm(const char *text)
{
	const char *sp, *pend;
	int len, len_blk = MAX_TEXTLEN;

	sp = text; // Fix me find <body>
	len = strlen(sp);
	if (len > MAX_TEXTLEN) len_blk = len / MAX_BLOCKS;
	pend = sp + len;
	for (int i = 0; i < MAX_BLOCKS && sp < pend; i++)
	{
		char buf[MAX_NGRAMLEN + 1];
		char blk[MAX_TEXTLEN + 1], *pblk;

		pblk = GetBlock(sp, blk, (pend - sp) > len_blk ? len_blk : pend - sp);
		while(*pblk)
		{
			for (int j = 0; j < MAX_NGRAMLEN; j++)
			{
				if (!*(pblk+j))	break;
				strncpy(buf, pblk, j + 1);
				buf[j + 1] = '\0';
				CLmString csp(buf);
				(*this)[csp]++;
			}
			pblk++;
		}
		sp = (pend - sp) > len_blk ?  sp + len_blk : pend;
	}
}

int CLang::LoadLm(char *file)
{
	FILE *tbl_f;
	char str[MAX_NGRAMLEN + 1];
	char ngram[MAX_NGRAMLEN + 1];
	char *chp;
	int i = 0;
	string path;

	if (isAbsolutePath(file))
		path = file;
	else
	{
		path = CONF_DIR;
		path.append("/");
		path.append(file);
	}

	if (!(tbl_f = fopen(path.c_str(), "r")))
	{
		logger.log(CAT_FILE, L_ERR, "Can't open ngram table %s: %s\n", path.c_str(), strerror(errno));
		return 1;
	}

	while (fgets(str, MAX_NGRAMLEN, tbl_f) && (i <= MAX_NGRAM))
	{
		chp = Trim(str, " \t\n\r");
		if (!*chp || *chp == '#')
			continue;
		char *p = ngram;
		while(*chp)
		{
			if (*chp == '_')
			{
				*p = ' ';
			}
			else
			{
				*p = *chp;
			}
			p++;
			chp++;
		}
		*p = 0;
		CLmString lms(ngram);
		(*this)[lms] = i++;
	}


/*	for (iterator it = begin (); it != end(); it++)
		cout << (*it).first.c_str() << "\n";
*/
	logger.log(CAT_ALL, L_INFO, "Loaded %d ngrams from %s\n", size(), path.c_str());
	fclose(tbl_f);
	return 0;
}

int CLangs::GetLang(char *text)
{
	hash_map<CLmString, int>::iterator it_ngram;
	hash_map<CLmString, int>::iterator it_lm;
	int g_lang = 0, hit, miss;
	float l_factor = 0, result = 0;

	CLmText lmtext(text);

	for (iterator it_lang = begin(); it_lang != end(); it_lang++)
	{
		CLang *langmap = it_lang->second;
		hit = miss = 0;
		for (it_lm = lmtext.begin(); it_lm != lmtext.end(); it_lm++)
		{
			if ((it_ngram = langmap->find(it_lm->first)) != langmap->end())
			{
				hit++;
			}
			else
			{
				miss++;
			}
		}
		l_factor = (l_factor = (float) (hit) / miss);

		if (l_factor > result )
		{
			result = l_factor;
			g_lang = it_lang->first;
		}
	}
//	logger.log(CAT_ALL, L_DEBUG, "Language: %d factor: %f lmtxtsz %d\n", g_lang, result, lmtext.size());
	return result >= LANG_FACTOR ? g_lang : CHARSET_USASCII;
}

void CLangs::AddLang(int charset, char *table)
{
	CLang *lang;
	lang = new CLang;
	if (lang->LoadLm(table))
		return;
	(*this)[charset] = lang;
}

CLangs::~CLangs()
{
	for (iterator it=begin(); it!=end(); it++)
		delete it->second;
}
#endif
