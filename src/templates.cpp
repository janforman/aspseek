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

/*  $Id: templates.cpp,v 1.63 2002/07/19 11:52:01 kir Exp $
    Author : Alexander F. Avdonkin
	Uses parts of UdmSearch code
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "templates.h"
#include "charsets.h"
#include "datasource.h"
#include "logger.h"
#include "datetime.h"
#include "timer.h"
#include "crc32.h"

//unsigned long DaemonAddress = 0;
//CULONGSet spaces;

/* If we want to have more then one active template simultanously, we
	should avoid using static variables, so as for now move ALL of
	it into a structure.
	Second thought - convert to class, will be less work to correct functions
*/
char defaultw[] = "default";


CTemplate::CTemplate()
{
	Port=DEFAULT_PORT;
	LCharset=CHARSET_USASCII;
	ps = 0;
	pps = 10; use_clones = 1;
	found = 0;
	first = 0; last = 0;
	mode = NULL;
	size=0;
	num_pages = 0; navpage = 0; is_next = 0;
	crc=0;
	MaxExcerpts = 0;
	MaxExcerptLen = 200;
	icolor = 0;
	hcolors = NULL;
	self[0] = 0;
	ask_error[0] = 0;
	wordinfo[0] = 0;
	href[0] = 0;
	hrefwg[0] = 0;
	fullhref[0] = 0;
	urlw = NULL;
}

void CTemplate::Clear()
{
	cgiQuery.Clear();
	found = 0;
	docnum = 0;
	first = 0; last = 0;
	ttime = 0;
	mode = NULL;
	size = 0;
	title[0] = 0;
	texts.clear();
	descr[0] = 0;
	keywords[0] = 0;

#ifdef UNICODE
	titleH = NULL;
	textH = NULL;
	descrH = NULL;
	keywordsH = NULL;
#endif

	num_pages = 0; navpage = 0; is_next = 0;
	crc=0;
	icolor = 0;
	hcolors = NULL;
	self[0] = 0;
	ask_error[0] = 0;
	wordinfo[0] = 0;
	href[0] = 0;
	hrefwg[0] = 0;
	fullhref[0]= 0;
	urlw = NULL;
}

int CTemplate::InitRand()
{
	int i;
	time_t tclock;
	tclock = time(0);
	srand(tclock);
	for(i = 0; i < MAXRANDOM; i++)
		Randoms[i] = 0;
	return 1;
}

void CTemplate::ClearTemplates()
{
	Templates.clear();
}

void CTemplate::PrepareHighlite()
{
	CMapStringToStringVec::iterator it = Templates.find("hicolors");
	if (it != Templates.end())
	{
		hcolors = &it->second;
	}
}


void CutTailCR(string* ls)
{
	if (ls)
	{
		const char* str = ls->c_str();
		const char* estr = str + ls->size() - 1;
		while ((estr >= str) && ((*estr == '\n') || (*estr == '\r'))) estr--;
		char strn[BUFSIZ];
		memcpy(strn, str, estr - str + 1);
		strn[estr - str + 1] = 0;
		*ls = strn;
	}
}

void CTemplate::InitErrorMes()
{
	Errormes[ER_STOPWORDS] = "Only stopword(s) are used in query. You must specify at least one non-stop word";
	Errormes[ER_EXTRASYMBOL] = "Extra symbols at the end";
	Errormes[ER_EMPTYQUERY] = "Empty query";
	Errormes[ER_TOOSHORT] = "Too few letters or digits are used at the beginning of pattern";
	Errormes[ER_NOQUOTED] = "Unmatched string quote";
	Errormes[ER_NOPARENTHESIS] = "Unmatched parenthesis";
	Errormes[ER_NOSEARCHD] = "Can not connect to search daemon";
}

int CTemplate::LoadTemplate(char *name, const char *query, char* default_where)
{
	FILE *file;
	int variables = 0;
	char *s, *lasttok = NULL;
	char str[BUFSIZ];
	int hi = 0, hic = 0;
//	char first_letters[128]="";

	file = fopen(name,"r");
	if (!file) return 1;

	/* Init for the case when there is no  */
	/* LocalCharset definition in template */
	LCharset = GetDefaultCharset();

	CStringStringVector& defVec = Templates[default_where];
	CStringVector* deftempl;
	CStringVector* templ;
	if (defVec.size() > 0)
	{
		deftempl = templ = defVec[0];
	}
	else
	{
		defVec.push_back(deftempl = new CStringVector);
		templ = deftempl;
	}

	while ((fgets(str, sizeof(str), file)))
	{
		// skip the comments
		if (str[0] == '#')
			continue;

		if (!STRNCMP(str, "<!--variables"))
		{
			templ = deftempl;
			variables = 1;
		}
		else if (!STRNCMP(str, "<!--include"))
		{
			char fname[BUFSIZ];
			if (sscanf(str + 11, "%s", fname))
			{
				LoadTemplate(fname, query, default_where);
			}
		}
		else if (!STRNCMP(str, "<!--/"))
		{
			if (hi)
			{
				string* ls = &templ->back();
				CutTailCR(ls);
			}
			templ = deftempl;
		}
		else if (!STRNCMP(str, "-->"))
		{
			if (variables)
			{
				templ = deftempl;
				variables = 0;
			}
		}
		else if (!STRNCMP(str, "<!--") && isalpha(str[4]))
		{
			char tname[20];
			char* pstr = str + 4;
			char* p = strchr(pstr, '-');
			int len = p ? p - pstr : strlen(pstr);
			if (len > 19) len = 19;
			memcpy(tname, pstr, len);
			tname[len] = 0;
			CStringStringVector& vec = Templates[tname];
			vec.push_back(templ = new CStringVector);
			hi = ((strcmp(tname, "hiopen") == 0) || 
				(strcmp(tname, "hiexopen") == 0) ||
				(strcmp(tname, "exopen") == 0));
			hic = ((strcmp(tname, "hicolors") == 0) ||
				(strcmp(tname, "hiclose") == 0) ||
				(strcmp(tname, "hiexclose") == 0) ||
				(strcmp(tname, "exclose") == 0));
		}
		else
		{
			if (hi || hic)
			{
				char* estr = str + strlen(str) - 1;
				while ((estr >= str) && ((*estr == '\n') ||
					(*estr == '\r')))
				{
					*estr-- = 0;
				}
			}
			templ->push_back(str);
			if (variables && (s = GetToken(str, "\r\n", &lasttok)))
			{
				if (!STRNCASECMP(str, ER_STOPWORDS))
				{
					Errormes[ER_STOPWORDS] = Trim(str + strlen(ER_STOPWORDS) + 1, "= \t\r\n");
				}
				else if (!STRNCASECMP(str, ER_EXTRASYMBOL))
				{
					Errormes[ER_EXTRASYMBOL] = Trim(str + strlen(ER_EXTRASYMBOL) + 1, "= \t\r\n");
				}
				else if (!STRNCASECMP(str, ER_EMPTYQUERY))
				{
					Errormes[ER_EMPTYQUERY] = Trim(str + strlen(ER_EMPTYQUERY) + 1, "= \t\r\n");
				}
				else if (!STRNCASECMP(str, ER_TOOSHORT))
				{
					Errormes[ER_TOOSHORT] = Trim(str + strlen(ER_TOOSHORT) + 1, "= \t\r\n");
				}
				else if (!STRNCASECMP(str, ER_NOQUOTED))
				{
					Errormes[ER_NOQUOTED] = Trim(str + strlen(ER_NOQUOTED) + 1, "= \t\r\n");
				}
				else if (!STRNCASECMP(str, ER_NOPARENTHESIS))
				{
					Errormes[ER_NOPARENTHESIS] = Trim(str + strlen(ER_NOPARENTHESIS) + 1, "= \t\r\n");
				}
				else if (!STRNCASECMP(str, ER_NOSEARCHD))
				{
					Errormes[ER_NOSEARCHD] = Trim(str + strlen(ER_NOSEARCHD) + 1, "= \t\r\n");
				}
				else if (!STRNCASECMP(str, "ResultsPerPage"))
				{
					ps = atoi(Trim(str + 14, "= \t\r\n"));
				}
				else if (!STRNCASECMP(str, "PagesPerScreen"))
				{
					pps = atoi(Trim(str + 14, "= \t\r\n"));
				}
				else if(!STRNCASECMP(str, "Clones"))
				{
					use_clones = STRNCASECMP(Trim(str + 6, "= \r\t\n"), "no");
				}
				else if(!STRNCASECMP(str, "DaemonAddress"))
				{
					char addr[100];
					int port;
					int s;
					if ((s = sscanf(Trim(str + 13, " "), "%[^:]:%i", addr, &port)))
					{
						daemons.insert(CDaemonAddress(inet_addr(addr), s == 2 ? port : Port));
//						DaemonAddress = inet_addr(addr);
					}
				}
				else if (!STRNCASECMP(str, "MaxExcerpts"))
				{
					MaxExcerpts = atoi(str + 11);
				}
				else if (!STRNCASECMP(str, "MaxExcerptLen"))
				{
					MaxExcerptLen = atoi(Trim(str + 13, "= \t\r\n"));
				}
// We do not need any charsets in s.cgi in UNICODE mode
#ifndef UNICODE
				else if (!STRNCASECMP(str, "CharsetTable"))
				{
					int len = strlen(str);
					char *name = new char[len];
					char *dir = new char[len];
					char *lang = new char[len];

					if (sscanf(str+12, "%s%s%s", name, lang, dir) == 3)
					{
						if (LoadCharset(lang, name, dir) == -1)
						{
							logger.log(CAT_ALL, L_ERR, "Charset %s has not been loaded\n", name);
						}
					}
					else
					{
						logger.log(CAT_ALL, L_ERR, "Bad CharsetTable format: %s\n", str );
					}
					delete name;
					delete dir;
					delete lang;
				}
				else if (!STRNCASECMP(str, "LocalCharset"))
				{
					LocalCharset = Trim(str + 12, "= \r\t\n");
					SetDefaultCharset(GetCharset(LocalCharset.c_str()));
				}
#endif /* ! UNICODE */
				else if (!STRNCASECMP(str, "Random"))
				{
					int n, max;
					n = atoi(str + 6);
					if ((n >= 0) && (n < MAXRANDOM))
					{
						char * s = str + 6;
						while ((*s) && (isdigit(*s))) s++;
						max = atoi(Trim(s, "= \t\r\n")) + 1;
						Randoms[n] = (int) ((float)max * rand() / (RAND_MAX + 1.0));
					}
				}
			}
		}
	}
	fclose(file);
	SetDefaultCharset(GetCharset(LocalCharset.c_str()));
	return 0;
}
/*
char* hilightcpy(char *dst, char *src, CStringSet& w_list, char *start,char *stop)
{
	char *s, *t, *word1;
	char real_word[64] = "";

	word1 = s = src;
	t = dst; *t = 0;
	while (*s)
	{
		if (!WordChar(*s, GetDefaultCharset()) || !(s[1]))
		{
			if (word1 < s)
			{
				char save = 0;
				if (s[1])
				{
					save = *s;
					*s = 0;
				}
				strncpy(real_word, word1, sizeof(real_word) - 3);
				Tolower((unsigned char*)real_word, GetDefaultCharset());
				if (w_list.IsHighlight(real_word))
				{
					sprintf(t, "%s%s%s", start, word1, stop);
					t = t + strlen(t);
				}
				else
				{
					strcpy(t, word1);
					t += strlen(word1);
				}
				if (s[1])
				{
					*s = save;
				}
			}
			if (s[1])
			{
				*t = *s;
				t++;
				*t = 0;
			}
			word1 = s + 1;
		}
		s++;
	}
	return dst;
}
*/

#ifdef UNICODE
char* CTemplate::hilightcpy(CDataSource& database, char *dst, const char *src, CULONGVector& positions)
{
	ULONG prev = 0;
	char* pdst = dst;
	for (CULONGVector::iterator pos = positions.begin(); pos != positions.end(); pos += 2)
	{
		if (!pos[0] && !pos[1]) break;
		memcpy(pdst, src + prev, pos[0] - prev);
		pdst += pos[0] - prev;
		if (!PrintTemplate(database, pdst, "hiexopen", "", "", ""))
		{
			strcpy(pdst, "<B>");
		}
		pdst += strlen(pdst);
//		pdst += sprintf(pdst, "%s", start);
		memcpy(pdst, src + pos[0], pos[1] - pos[0]);
		pdst += pos[1] - pos[0];
		if (!PrintTemplate(database, pdst, "hiexclose", "", "", ""))
		{
			strcpy(pdst, "</B>");
		}
		pdst += strlen(pdst);
//		pdst += sprintf(pdst, "%s", stop);
		prev = pos[1];
	}
	strcpy(pdst, src + prev);
	return dst;
}

#endif

char* CTemplate::hilightcpy(CDataSource& database, char *dst, char *src, CStringSet& w_list)
{
#ifdef UNICODE
	CCharsetB* charset = GetUCharset(cgiQuery.m_charset.c_str());
	char* lt = src;
	char* token;
	char* pdst = dst;
	WORD uword[MAX_WORD_LEN + 1];
	while (true)
	{
		char* t = lt;
		int r = GetUWord((unsigned char**)&lt, charset, uword, &token);
		if (r == 1) break;
		memcpy(pdst, t, token - t);
		pdst += (token - t);
		if (r == 0)
		{
			int hil = w_list.IsHighlight(uword);
			if (hil)
			{
				if (!PrintTemplate(database, pdst, "hiexopen", "", "", ""))
				{
					strcpy(pdst, "<B>");
				}
				pdst += strlen(pdst);
//				pdst += sprintf(pdst, "%s", start);
			}
			memcpy(pdst, token, lt - token);
			pdst += (lt - token);
			if (hil)
			{
				if (!PrintTemplate(database, pdst, "hiexclose", "", "", ""))
				{
					strcpy(pdst, "</B>");
				}
				pdst += strlen(pdst);
//				pdst += sprintf(pdst, "%s", stop);
			}
		}
		*pdst = 0;
	}
#else
	char *s, *t, *word1;
	char real_word[64] = "";

	word1 = s = src;
	t = dst; *t = 0;
	do
	{
		if ((*s == 0) || !WordChar(*s, GetDefaultCharset()))
		{
			if (word1 < s)
			{
				int wl = s - word1;
				int rs = min((int)sizeof(real_word) - 3, wl);
				memcpy(real_word, word1, rs);
				real_word[rs] = 0;
				Tolower((unsigned char*)real_word, GetDefaultCharset());
				int hil = w_list.IsHighlight(real_word);
				if (hil)
				{
					if (!PrintTemplate(database, t, "hiexopen", "", "", ""))
					{
						strcpy(t, "<B>");
					}
					t += strlen(t);
//					t += sprintf(t, "%s", start);
				}
				memcpy(t, word1, wl);
				t += wl;
				if (hil)
				{
					if (!PrintTemplate(database, t, "hiexclose", "", "", ""))
					{
						strcpy(t, "</B>");
					}
					t += strlen(t);
//					t += sprintf(t, "%s", stop);
				}
			}
			*t++ = *s;
			word1 = s + 1;
		}
	}
	while (*s++);
#endif
	return dst;
}

int CTemplate::PrintOption(CDataSource& database, char* Target, const char* where, const char* option)
{
	CTag tag;
	char *s;
	int len;
	char tmp[STRSIZ]="";
	char* pTarget = Target + strlen(Target);
	if (!(s = strchr(option,'>')))
	{
		strcpy(pTarget, option);
		return 0;
	}
	len = s - option;
	s = new char[len + 1];
	memcpy(s, option, len);
	s[len] = 0;
	tag.ParseTag(s);
	if (tag.m_selected && tag.m_value)
	{
		PrintOneTemplate(database, tmp, option, where, "", "", "");
		CTag tag1;
		tag1.ParseTag(tmp);
		const char *value = tag1.m_selected ? tag1.m_selected : "";
		sprintf(pTarget, "<OPTION VALUE=\"%s\"%s>", tag1.m_value,
			strcmp(tag.m_value, value) ? "" : " SELECTED");
		pTarget += strlen(pTarget);
		PrintOneTemplate(database, pTarget, option + len + 1, where, "", "", "");
	}
	else
	{
		strcpy(pTarget, option);
	}
	delete s;
	return 0;
}

int CTemplate::PrintRadio(CDataSource& database, char* Target, const char* where, const char* option, const char* name)
{
	CTag tag;
	char *s;
	int len;
	char tmp[STRSIZ]="";
	char* pTarget = Target + strlen(Target);
	if (!(s = strchr(option,'>')))
	{
		strcpy(pTarget, option);
		return 0;
	}
	len = s - option;
	s = (char*)alloca(len + 1);
	memcpy(s, option, len);
	s[len] = 0;
	tag.ParseTag(s);
	if (tag.m_checked && tag.m_value)
	{
//		int i;
		PrintOneTemplate(database, tmp, option, where, "", "", "");
		CTag tag1;
		tag1.ParseTag(tmp);
		const char* value = tag1.m_checked ? tag1.m_checked : "";
		sprintf(pTarget, "<input type=radio name=%s value =\"%s\"%s>", name, tag1.m_value,
			strcmp(tag.m_value, value) ? "" : " CHECKED");
		pTarget += strlen(pTarget);
		PrintOneTemplate(database, pTarget, option + len + 1, where, "", "", "");
	}
	else
	{
		strcpy(pTarget, option);
	}
	return 0;
}

int CTemplate::PrintOneTemplate(CDataSource& database, char* Target,
	const char *s, const char* where, const char *url,
	const char *content_type, const char *last_modified)
{
	//DOCUMENT *r=NULL;
	int rnum,tmp;

	*Target = 0;
	char* pTarget = Target;
	while (*s)
	{
		pTarget += strlen(pTarget);
		if ((*s == '\\') && (s[1] == '$'))
		{
			sprintf(pTarget, "$");
			s += 2;
			continue;
		}
		if (*s!='$')
		{
			sprintf(pTarget, "%c", *s);
			s++;
			continue;
		}

		s++;
		if (strncmp(s, "sp", 2) == 0)
		{
			int space;
			if (sscanf(s + 2, "%i", &space))
			{
				strcpy(pTarget, cgiQuery.m_spaces.find(space) == cgiQuery.m_spaces.end() ? "" : " checked");
				s += 2;
				while (isdigit(*s)) s++;
				continue;
			}
		}
		else if (!strncmp(s, "st", 2))
		{
			sprintf(pTarget, "%d", cgiQuery.m_site);
			s += 2;
			continue;
		}

		switch(*s)
		{
		// If you want $, write $$ in template
		case '$':
			sprintf(pTarget, "%c", *s);
			s++;
			continue;
#ifdef UNICODE
		// $c
		case 'c': sprintf(pTarget, "%s", cgiQuery.m_charset.c_str()); break;
#endif
		case 'A':
			if (s[1] == 'V') // $AV - ASPseek version
			{
				s++;
				sprintf(pTarget, VERSION);
			}
			else // just $A
			{
				sprintf(pTarget, "%s", self);
			}
			break;
		case 'Q':
			if (isalpha(s[1]))
			{
				s++;
				if (*s == 'F')
				{
					sprintf(pTarget, "%s", cgiQuery.m_qr_url_escaped.c_str());
				}
			}
			else
			{
				sprintf(pTarget, "%s", cgiQuery.m_query_form_escaped.c_str());
			}
			break;
		case 'q': sprintf(pTarget, "%s", strncmp(where, "res", 3) == 0 ? cgiQuery.m_qr_url_escaped.c_str() : cgiQuery.m_qr_form_escaped.c_str()); break;
		case 'P': sprintf(pTarget, "%s", cgiQuery.m_tmpl_url_escaped.c_str()); break;
		case 'E': sprintf(pTarget, "%s", Errormes[ask_error].c_str()); break;
		case 'W': sprintf(pTarget, "%s", wordinfo); break;
		case 'f':
			if (s[1] == 'm') // $fm
			{
				s++;
				if (cgiQuery.m_search_mode == SEARCH_MODE_FORMS)
				{
					if (cgiQuery.m_wordform_langs.empty())
						sprintf(pTarget, "on");
					else
						sprintf(pTarget, "%s", cgiQuery.m_wordform_langs.c_str());
				}
				else
				{
					sprintf(pTarget, "off");
				}
			}
			else // $f
			{
				sprintf(pTarget, "%ld", first);
			}
			break;
		case 'l': sprintf(pTarget, "%ld", last); break;
		case 't':
			if (s[1] == 'l') // $tl
			{
				s++;
				sprintf(pTarget, (cgiQuery.m_posmask & 8) ? "on" : "off");
			}
			else
				sprintf(pTarget, "%d", found);
			break;
		case 'm': sprintf(pTarget, "%s", mode ? mode : "all"); break;
		case 's': sprintf(pTarget, "%s", (cgiQuery.m_sort_by == SORT_BY_DATE) ? "date" : "rate"); break;
		case 'o': sprintf(pTarget, "%d", cgiQuery.m_format); break;
		case 'w':
			if (found > 1)
			{
				PrintTemplate(database, pTarget, "inres", "", "", "");
			}
			break;
		case 'H':
			{
				if (hcolors && hcolors->size())
				{
					CStringVector* hc = (unsigned)cgiQuery.m_format < hcolors->size() ? (*hcolors)[cgiQuery.m_format] : hcolors->back();
					sprintf(pTarget, "%s", (*hc)[icolor % hc->size()].c_str());
				}
				else
				{
					sprintf(pTarget, "#FFFF66");
				}
			}
			break;
		case 'T':
			{
				char templ2[100];
				const char* ps = s + 1;
				char* pt = templ2;
				while ((pt - templ2 < 99) && (isalnum(*ps)))
				{
					*pt++ = *ps++;
				}
				*pt = 0;
				s = ps - 1;
				PrintTemplate(database, pTarget, templ2, url, content_type, last_modified);
				break;
			}
		case 'R':
			PrintTemplate(database, pTarget, cgiQuery.m_gr ? "ressites" : "resurls", url, "", "");
			break;
		case 'M':
			if ((urlw->m_count & 0xFFFFFF) > (unsigned long)(isdigit(s[1]) ? (s++, s[0] - '0') : 1))
			{
				PrintTemplate(database, pTarget, "moreurls", url, "", "");
			}
			break;
		case 'S':
			s++;
			switch (*s)
			{
				case 'H':
					sprintf(fullhref, "%s&amp;st=%lu", hrefwg, urlw->m_siteID);
					strcpy(pTarget, fullhref);
					break;
				case 'C':
					sprintf(pTarget, "%lu", urlw->m_count & 0xFFFFFF);
					break;
				case 'S':
					{
						strcpy(fullhref, url);
						char* ds = strstr(fullhref, "//");
						if (ds)
						{
							ds = strchr(ds + 2, '/');
							if (ds) *ds = 0;
						}
						strcpy(pTarget, fullhref);
						break;
					}
				case 'T':
					sprintf(pTarget, "%lu", urlw->m_siteID);
					break;
			}
			break;
		case 'r': {
			s++;
			rnum = atoi(s);
			if ((rnum >= 0) && (rnum < MAXRANDOM))
				sprintf(pTarget, "%d", Randoms[rnum]);
			while (*s && isdigit(s[1]))	s++;
			break;
		}
		case 'Y': {
			sprintf(pTarget,"%.3f",ttime);
			break;
		}
		case 'V': {
			if (num_pages > 1)
				PrintTemplate(database, pTarget, "navigator", "", "", "");
			break;
		}
		case 'D':
			s++;
			switch(*s)
			{
			case 'U': sprintf(pTarget, "%s", url ? url : ""); break;
			case 'E':
				{
					char eurl[2000];
					EscapeURL(eurl, url ? url : "");
					sprintf(pTarget, "%s", eurl);
					break;
				}
			case 'C': sprintf(pTarget, "%s", content_type ? content_type : ""); break;
			case 'M': sprintf(pTarget, "%s", last_modified ? last_modified : ""); break;
			case 'R': sprintf(pTarget, "%7.5f", (double)urlw->m_weight / 4294967296.0); break;
			case 'S': sprintf(pTarget, "%d", size); break;
			case 'B': sprintf(pTarget, "%d", size >> 10); break;
			case 'Z': PrintTemplate(database, pTarget, size < 1024 ? "sizeb" : "sizek", "", "", ""); break;
			case 'N': sprintf(pTarget, "%ld", docnum); break;

#ifdef UNICODE
			case 'D': hilightcpy(database, pTarget, descr, *descrH); break;
			case 'T': hilightcpy(database, pTarget, title, *titleH); break;
			case 'K': hilightcpy(database, pTarget, keywords, *keywordsH); break;
			case 'X':
				{
					CULONGVector::iterator pos = textH->begin();
					for (ULONG i = 0; i < texts.size(); i++)
					{
						CULONGVector tmpH;
						if (!PrintTemplate(database, pTarget, "exopen", "", "", ""))
						{
							strcpy(pTarget, "<br/>...");
						}
						pTarget += strlen(pTarget);
						for (; pos != textH->end(); pos += 2)
						{
							if (!pos[0] && !pos[1])
							{
								pos += 2;
								break;
							}
							tmpH.push_back(pos[0]);
							tmpH.push_back(pos[1]);
						}
						if (texts[i].size())
						{
							hilightcpy(database, pTarget, texts[i].c_str(), tmpH);
							pTarget += strlen(pTarget);
							if (!PrintTemplate(database, pTarget, "exclose", "", "", ""))
							{
								strcpy(pTarget, "...");
							}
							pTarget += strlen(pTarget);
						}
					}
					break;
				}
#else
			case 'D': hilightcpy(database, pTarget, descr, LightWords); break;
			case 'T': hilightcpy(database, pTarget, title, LightWords); break;
			case 'K': hilightcpy(database, pTarget, keywords, LightWords); break;
			case 'X':
				{
					for (ULONG i = 0; i < texts.size(); i++)
					{
						if (!PrintTemplate(database, pTarget, "exopen", "", "", ""))
						{
							strcpy(pTarget, "<br/>...");
						}
						pTarget += strlen(pTarget);
						if (texts[i].size())
						{
							hilightcpy(database, pTarget, (char*)texts[i].c_str(), LightWords);
							pTarget += strlen(pTarget);
							if (!PrintTemplate(database, pTarget, "exclose", "", "", ""))
							{
								strcpy(pTarget, "...");
							}
							pTarget += strlen(pTarget);
						}
					}
					break;
				}
#endif
			default : sprintf(pTarget, "%c", *s);
			}
			break;

		case 'C':
			s++;
			switch(*s)
			{
			case 'L':
				if (use_clones && crc && *crc && (strncmp(where, "res", 3) == 0))
				{
					CSrcDocVector docs;
					database.GetCloneList(crc, urlw, docs);
					for (CSrcDocVector::iterator doc = docs.begin(); doc != docs.end(); doc++)
					{
						PrintTemplate(database, pTarget, "clone", (*doc)->m_url.c_str(), content_type, last_modified);//(*doc)->m_content_type.c_str(), (*doc)->m_last_modified.c_str());
					}
				}
				break;
			case 'C':
				if (!strncmp(content_type, "text/html", 9))
					PrintTemplate(database, pTarget, "cached", url, "", "");
				else
					PrintTemplate(database, pTarget, "textversion", url, "", "");
				break;
			default : sprintf(pTarget, "%c", *s);
			}
			break;
		case 'N':
			s++;
			switch (*s)
			{
			case 'L':
				if (cgiQuery.m_np > 0)
				{
					if (cgiQuery.m_np > 1)
						sprintf(fullhref, "%s&amp;np=%d", href, cgiQuery.m_np - 1);
					else
						strcpy(fullhref, href);
					PrintTemplate(database, pTarget, "navleft", "", "", "");
				}
				else
				{
					PrintTemplate(database, pTarget, "navleft0", "", "", "");
				}
				break;
			case 'B':
				for (navpage = 1; navpage <= num_pages; navpage++)
				{
					tmp = cgiQuery.m_np + 1 - pps / 2;
					if (tmp < 1)
						tmp = 1;
					if (tmp > num_pages - pps + 1)
						tmp = num_pages - pps + 1;
					if (navpage >= tmp && navpage < (tmp + pps))
					{
						if (navpage > 1)
							sprintf(fullhref, "%s&amp;np=%d", href, navpage - 1);
						else
							strcpy(fullhref, href);
						if (cgiQuery.m_np == navpage - 1)
							PrintTemplate(database, pTarget, "navbar0", "", "", "");
						else
							PrintTemplate(database, pTarget, "navbar1", "", "", "");
					}
				}
				break;
			case 'R':
				if (is_next)
				{
					sprintf(fullhref, "%s&amp;np=%d", href, cgiQuery.m_np + 1);
					PrintTemplate(database, pTarget, "navright", "", "", "");
				}
				else
				{
					PrintTemplate(database, pTarget, "navright0", "", "", "");
				}
				break;
			case 'H': sprintf(pTarget, "%s", fullhref); break;
			case 'P': sprintf(pTarget, "%d", navpage); break;
			}
			break;
		case 'i':
		case 'I':
			if (!strncasecmp(s, "if(",3))
			{
				const char* fname = s + 3;
				if ((s = strchr(s, ')')))
				{
					char templ[STRSIZ];
					sprintf(templ, ".%c", SLASH);
					int lt = strlen(templ);
					memcpy(templ + lt, fname, s - fname);
					templ[s + lt - fname] = 0;
					if (LoadTemplate(templ, "", "include"))
					{
						sprintf(pTarget, "Unable to include '%s'", templ);
					}
					else
					{
						PrintTemplate(database, pTarget, "include", "", "", "");
					}
				}
			}
			else
				sprintf (pTarget, "%c", *s);
			break;
		case 'g':
			if (s[1] == 'r') // $gr
			{
				s++;
				sprintf(pTarget, cgiQuery.m_gr ? "on" : "off");
			}
			break;
		case 'n':
			if (s[1] == 'p') // $np
			{
				s++;
				sprintf(pTarget, "%d", cgiQuery.m_np);
			}
			break;
		case 'a':
			if (s[1] == 'd') // $ad
			{
				s++;
				sprintf(pTarget, (cgiQuery.m_accDist ==
					ACCOUNT_DISTANCE_YES) ?	"on" : "off");
			}
			break;
		case 'b':
			if (s[1] == 'd') // $bd
			{
				s++;
				sprintf(pTarget, (cgiQuery.m_posmask & 1) ? "on" : "off");
			}
			break;
		case 'd':
			if (s[1] == 's') // $ds
			{
				s++;
				sprintf(pTarget, (cgiQuery.m_posmask & 2) ? "on" : "off");
			}
			break;
		case 'k':
			if (s[1] == 'w') // $kw
			{
				s++;
				sprintf(pTarget, (cgiQuery.m_posmask & 4) ? "on" : "off");
			}
			break;
			
		default:
			if (!strncmp(s, "ps", 2))
			{
				sprintf(pTarget, "%d", cgiQuery.m_ps ? cgiQuery.m_ps : DEFAULT_PS);
				s++;
			}
			else if (!strncmp(s, "ul", 2))
			{
				sprintf(pTarget, "%s", cgiQuery.m_ul_unescaped.c_str());
				s++;
			}
			else
				sprintf(pTarget, "%c", *s);
		}
		s++;
	}
	return 0;
}

int CTemplate::PrintTemplate(CDataSource& database, char* Target, const char* where, const char *url, const char *content_type, const char *last_modified)
{
//	int i;
	CMapStringToStringVec::iterator it = Templates.find(where);
	if (it != Templates.end())
	{
		CStringStringVector& vec = it->second;
		CStringVector* templ = (unsigned)cgiQuery.m_format < vec.size() ? vec[cgiQuery.m_format] : vec.back();
		char* pTarget = Target;
		for (CStringVector::iterator str = templ->begin(); str != templ->end(); str++)
		{
//			printf("str->c_str()=%s\n", str->c_str());
			pTarget += strlen(pTarget);

			if (!STRNCASECMP(str->c_str(), "<OPTION"))
			{
				PrintOption(database, pTarget, where, str->c_str());
			}
			else if (!STRNCASECMP(str->c_str(), "<input type=radio name="))
			{
				char name[100] = "";
				sscanf(str->c_str() + 23, "%s", name);
				PrintRadio(database, pTarget, where, str->c_str(), name);
			}
			else
			{
				PrintOneTemplate(database, pTarget, str->c_str(), where, url, content_type, last_modified);
			}

		}
		return 1;
	}
	return 0;
}

int CTemplate::PrintTemplate(CDataSource& database, const char* where, const char *url, const char *content_type, const char *last_modified)
{
	char Target[STRSIZ*16] = "";
	PrintTemplate(database, Target, where, url, content_type, last_modified);
	request->site->client->write(request, Target, strlen(Target));
//	fwrite(Target, strlen(Target), 1, stdout);
	return 1;
}


char * UnescapeCGIQuery(char *d, char *s)
{
	int hi, lo = 0;
	char *dd;
	if ((d == NULL) || (s == NULL)) return 0;
	dd = d;
	while (*s)
	{
		if (*s == '%')
		{
			++s;
			if ((*s >= '0') && (*s <= '9')) hi = *s - '0'; else hi = *s - 'A' + 10;
			++s;
			if ((*s >= '0') && (*s <= '9')) lo = *s - '0'; else lo = *s - 'A' + 10;
			*d = (hi << 4) + lo;
		}
		else if (*s == '+')
		{
			*d = ' ';
		}
		else
		{
			*d = *s;
		}
		s++; d++;
	}
	*d = 0;
	return dd;
}

char* EscapeHtml(char *d, const char *s, char what)
{
	char *dd;
	if ((d == NULL) || (s == NULL)) return 0;
	dd = d;
	while(*s)
	{
		if (*s == what)
		{
			if (what == '"')
			{
				strcpy(d, "&quot;");
				d += 5;
			}
			else if (what == ' ')
			{
				*d='+';
			}
		}
		else
		{
			*d = *s;
		}
		s++; d++;
	}
	*d = 0;
	return dd;
}

char* EscapeURL(char *d, const char *s)
{
	char *dd;
	if ((d == NULL) || (s == NULL)) return 0;
	dd = d;
	while(*s)
	{
		if (strchr("%&<>+[](){}/?#'\"\\;,",*s) || ((unsigned char)*s > 127))
		{
			unsigned char c = *s;
			sprintf(d, "%%%X%X", (c >> 4) & 0xf, c & 0xf);
			d += 2;
		}
		else if (*s == ' ')
		{
			*d = '+';
		}
		else
		{
			*d = *s;
		}
		s++; d++;
	}
	*d = 0;
	return dd;
}

void HtmlSpecialChars(const char *str, char* p)
{
	char *pos;
	if (!str)
	{
		*p = 0;
		return;
	}
	*p = 0;
	for (pos = p; *str; str++)
	{
		switch (*str)
		{
			case '&': strcpy(pos, "&amp;"); pos += 5; break;
			case '"': strcpy(pos, "&quot;"); pos += 6; break;
			case '<': strcpy(pos, "&lt;"); pos += 4; break;
			case '>': strcpy(pos, "&gt;"); pos += 4; break;
			default:
				*pos = *str;
				pos++; *pos = 0;
		}
	}
}

void CSrcDocumentR::Recv(CBufferedSocket& socket, int MaxExcerpts)
{
	socket.RecvULONG(m_urlID);
	socket.RecvULONG(m_siteID);
	socket.RecvULONG(m_last_index_time);
	socket.RecvULONG(m_next_index_time);
	socket.RecvULONG(m_hops);
	socket.RecvULONG(m_referrer);
	socket.RecvULONG(m_status);
	socket.RecvULONG(m_slow);
	socket.RecvString(m_crc);
	socket.RecvString(m_last_modified);
	socket.RecvString(m_url);
	socket.RecvString(m_descr);
	socket.RecvString(m_keywords);
	socket.RecvString(m_title);
	socket.RecvString(m_content_type);
	ULONG exsz;
	socket.RecvULONG(exsz);
	if (exsz > ((ULONG)MaxExcerpts ? (ULONG)MaxExcerpts : 1)) // searchd goes weird?
	{
		return;
	}
	for (ULONG i = 0; i < exsz; i++)
	{
		string exc;
		socket.RecvString(exc);
		m_excerpts.push_back(exc);
	}
	socket.RecvULONG(m_size);
#ifdef UNICODE
//	socket.RecvULONGVector(m_textH);
	if (socket.RecvULONGVector(m_descrH) < 0)
		return;
	if (socket.RecvULONGVector(m_titleH) < 0)
		return;
	if (socket.RecvULONGVector(m_keywordsH) < 0)
		return;
	if (socket.RecvULONGVector(m_excerptsH) < 0)
		return;
#endif
}

char* AddSite(char* pdest, string& sites, int minus = 0)
{
	const char* psite = sites.c_str();
	char* pp = pdest;
	int sc = 0;
	pdest += sprintf(pdest, " ");
	while (*psite)
	{
		while (*psite && (*psite == ' ')) psite++;
		if (*psite == 0) break;
		char* sp = strchr(psite, ' ');
		int len = sp ? sp - psite : strlen(psite);
		pdest += sprintf(pdest, "%ssite: ", sc > 0 ? " | " : "");
		memcpy(pdest, psite, len);
		pdest += len;
		*pdest = 0;
		psite += len;
		sc++;
	}
	if (sc > 1)
	{
		*pp = '(';
		pdest += sprintf(pdest, ")");
	}
	else
	{
		if (minus)
		{
			*pp = '-';
			pp[-1] = ' ';
		}
	}
	return pdest;
}

void AddIE(string& qwords, string& is, string& xs)
{
	if (is.size() || xs.size())
	{
		char* dest = (char*)alloca(qwords.size() + (is.size() + xs.size()) * 10);
		char* pdest = dest + sprintf(dest, "%s ", qwords.c_str());
		if (is.size())
		{
			pdest = AddSite(pdest, is);
		}
		if (xs.size())
		{
			pdest += sprintf(pdest, " -");
			pdest = AddSite(pdest, xs, 1);
		}
		qwords = dest;
	}
}

void AddExcl(string& qwords, string& xwords)
{
	if (xwords.size())
	{
		char* dest = (char*)alloca(qwords.size() + xwords.size() + 10);
		sprintf(dest, "(%s) -(%s)", qwords.c_str(), xwords.c_str());
		qwords = dest;
	}
}

string CombineIE(string& include, string& exclude, int iphrase, int xphrase)
{
	char* dest = (char*)alloca(include.size() + exclude.size() + 20);
	char* pdest = dest;
	pdest += sprintf(pdest, iphrase ? "\"%s\"" : "%s", include.c_str());
	if (include.size() && exclude.size())
	{
		if (xphrase)
		{
			pdest += sprintf(pdest, " -\"%s\"", exclude.c_str());
		}
		else
		{
			const char* px = exclude.c_str();
			while (*px)
			{
				while (*px && (*px == ' ')) px++;
				if (*px == 0) break;
				char* sp = strchr(px, ' ');
				int len = sp ? sp - px : strlen(px);
				*pdest++ = ' ';
				*pdest++ = '-';
				memcpy(pdest, px, len);
				pdest += len;
				*pdest = 0;
				px += len;
			}
		}
	}
	return dest;
}

int CCgiQueryC::ParseCgiQuery(char* query, char* templ)
{
	char *lasttok, *token;
	int empty = 1;
	token = GetToken(query, "&", &lasttok);
	char str[BUFSIZ];
	char* pspaces = m_cspaces;
	char* linkTo = NULL;
	// time limiting variables
	int tl_dt = 0; // type of time settings (1: back, 2: er, 3: range)
	// if tl_dt == 1
	time_t tl_dp = BAD_DATE; // how much earlier from now()
	// if tl_dt == 2
	int tl_dx = 0; // 1: newer, -1: older than date below
	int tl_dd = 0; // day
	int tl_dm = 0; // month
	int tl_dy = 0; // year
	// if tl_dt == 3
	time_t tl_db = 0; // begin date
	time_t tl_de = 0; // end date

	m_t_from = m_t_to = 0; // just to be sure

	while (token)
	{
		if (!STRNCMP(token, "q="))
		{
			empty = 0;
			m_query_words = UnescapeCGIQuery(str, token + 2);
		}
		else if (!STRNCMP(token, "r="))
		{
			char str[STRSIZ]="";
			m_results = UnescapeCGIQuery(str, token + 2);
		}
		else if (!STRNCMP(token, "in="))
		{
			if (strcmp(token + 3, "on") == 0)
			{
				m_inres = 1;
			}
		}
		else if (!STRNCMP(token, "bd="))
		{
			if (strcmp(token + 3, "on") == 0)
			{
				m_posmask |= 1;
			}
		}
		else if (!STRNCMP(token, "ds="))
		{
			if (strcmp(token + 3, "on") == 0)
			{
				m_posmask |= 2;
			}
		}
		else if (!STRNCMP(token, "kw="))
		{
			if (strcmp(token + 3, "on") == 0)
			{
				m_posmask |= 4;
			}
		}
		else if (!STRNCMP(token, "tl="))
		{
			if (strcmp(token + 3, "on") == 0)
			{
				m_posmask |= 8;
			}
		}
		else if (!STRNCMP(token, "t="))
		{
			m_take = 1;
		}
		else if (!STRNCMP(token, "ch="))
		{
			m_cached = UnescapeCGIQuery(str, token + 3);
		}
		else if (!STRNCMP(token, "iq="))
		{
			char str[STRSIZ]="";
			empty = 0;
			m_include_words = UnescapeCGIQuery(str, token + 3);
		}
		else if (!STRNCMP(token, "xq="))
		{
			char str[STRSIZ]="";
			empty = 0;
			m_exclude_words = UnescapeCGIQuery(str, token + 3);
		}
		else if (!STRNCMP(token, "im="))
		{
			if (strcmp(token + 3, "p") == 0) m_iph = 1;
		}
		else if (!STRNCMP(token, "xm="))
		{
			if (strcmp(token + 3, "p") == 0) m_xph = 1;
		}
		else if (!STRNCMP(token, "is="))
		{
			char str[STRSIZ]="";
			m_includeSites = UnescapeCGIQuery(str, token + 3);
		}
		else if (!STRNCMP(token, "xs="))
		{
			char str[STRSIZ]="";
			m_excludeSites = UnescapeCGIQuery(str, token + 3);
		}
		else if (!STRNCMP(token, "np="))
		{
			m_np = atoi(token + 3);
		}
		else if (!STRNCMP(token, "o="))
		{
			m_format = atoi(token + 2);
			if ((m_format > 99) || (m_format < 0))
				m_format = 0;
		}
		else if(!STRNCMP(token, "ps="))
		{
			m_ps = atoi(token + 3);
			// Extra safety: Do not allow to pass very big page sizes
			m_ps =(m_ps > MAX_PS) ? m_ps = MAX_PS : m_ps;
		}
		else if (!STRNCMP(token, "ul=") && strlen(token) > 3)
		{
			char str[STRSIZ] = "";
			UnescapeCGIQuery(str, token + 3);
			if (!m_ul_unescaped.empty())
				m_ul_unescaped.append(" ");
			m_ul_unescaped.append(str);
			sprintf(m_ul_str + strlen(m_ul_str),"&amp;ul=%s", token + 3);
		}
		else if (!STRNCMP(token, "sp") && strlen(token) > 2)
		{
			int space;
			if (sscanf(token + 2, "%i=on", &space) > 0)
			{
				if (strcmp(token + strlen(token) - 3, "=on") == 0)
				{
					if (pspaces - m_cspaces <= 256)
					{
						sprintf(pspaces, "%s%i", m_cspaces != pspaces ? "," : "", space);
						pspaces += strlen(pspaces);
					}
					m_spaces.insert(space);
					sprintf(m_ul_str + strlen(m_ul_str),"&amp;sp%i=on", space);
				}
			}
		}
		else if (!STRNCMP(token, "gr") && strlen(token) > 2)
		{
			if (strcmp(token + 2, "=off") == 0)
			{
				m_gr = 0;
			}
		}
		else if (!STRNCMP(token, "st=") && strlen(token) > 3)
		{
			m_site = atoi(token + 3);
		}
		else if (!STRNCMP(token, "tmpl="))
		{
			char* tmpl = token + 5;
			char tmplu[2000];
			snprintf(tmplu, sizeof(tmplu), "&amp;tmpl=%s", tmpl);
			m_tmpl_url_escaped = tmplu;
			UnescapeCGIQuery(tmplu, tmpl);
			if (tmplu[0] == '/')
			{
				strcpy(templ, tmplu);
			}
			else
			{
				char* sl = strrchr(templ, '/');
				strcpy(sl ? sl + 1 : templ, tmplu);
			}
			m_tmplp = tmpl;
		}
		else if (!STRNCMP(token, "cs="))
		{
			m_charset = token + 3;
		}
		else if (!STRNCMP(token, "fm="))
		{
			if (strcmp(token + 3, "on") == 0)
			{
				m_search_mode = SEARCH_MODE_FORMS;
			}
			else if (strcmp(token + 3, "off") == 0)
			{
				m_search_mode = SEARCH_MODE_EXACT;
			}
			else if (token[3] != '\0')
			{
				m_search_mode = SEARCH_MODE_FORMS;
				char str[STRSIZ] = "";
				char* fm = UnescapeCGIQuery(str, token + 3);
				if ((m_wordform_langs.empty()) && (str[0]!=','))
					m_wordform_langs = ",";
				m_wordform_langs.append(fm);
				// Add , to the end of the string
				if (m_wordform_langs[m_wordform_langs.size() - 1] != ',')
					m_wordform_langs.append(",");
			}
			else // empty fm=
			{
				m_search_mode = SEARCH_MODE_EXACT;
			}
		}
		else if (!STRNCMP(token, "ln="))
		{
			linkTo = token + 3;
			empty = 0;
		}
		else if (!STRNCMP(token, "s="))
		{
			if (strcmp(token + 2, "rate") == 0)
			{
				m_sort_by = SORT_BY_RATE;
			}
			else if (strcmp(token + 2, "date") == 0)
			{
				m_sort_by = SORT_BY_DATE;
			}
		}
		// --- time limiting options begin ---
		else if (!STRNCMP(token, "dt="))
		{
			char* arg = token + 3;
			if (!STRNCMP(arg, "back"))
				tl_dt = 1;
			else if (!STRNCMP(arg, "er"))
				tl_dt = 2;
			else if (!STRNCMP(arg, "range"))
				tl_dt = 3;
		}
		else if (!STRNCMP(token, "dp="))
		{
			tl_dp = tstr2time_t(token + 3);
			if (tl_dp == 0) // means "Anytime", so no filter
				tl_dp = BAD_DATE;
		}
		else if (!STRNCMP(token, "dx="))
		{
			tl_dx = atoi(token + 3);
		}
		else if (!STRNCMP(token, "dd="))
		{
			tl_dd = atoi(token + 3);
		}
		else if (!STRNCMP(token, "dm="))
		{
			tl_dm = atoi(token + 3);
		}
		else if (!STRNCMP(token, "dy="))
		{
			tl_dy = atoi(token + 3);
		}
		else if (!STRNCMP(token, "db="))
		{
			char str[STRSIZ] = "";
			UnescapeCGIQuery(str, token + 3);
			tl_db = dmyStr2time_t(str);
		}
		else if (!STRNCMP(token, "de="))
		{
			char str[STRSIZ] = "";
			UnescapeCGIQuery(str, token + 3);
			tl_de = dmyStr2time_t(str);
		}
		else if (!STRNCMP(token, "fr="))
		{
			m_t_from = atoi(token + 3);
		}
		else if (!STRNCMP(token, "to="))
		{
			m_t_to = atoi(token + 3);
		}
		else if (!STRNCMP(token, "ad="))
		{
			m_accDist = strcmp(token + 3, "on") == 0 ? ACCOUNT_DISTANCE_YES : ACCOUNT_DISTANCE_NO;
		}
		// --- time limiting options end ---
		token = GetToken(NULL, "&", &lasttok);
	}
	// process time limiting options
	time_t tt;
	if ( (m_t_from==0) && (m_t_to==0) ) // we're doing the first page
	{
		switch (tl_dt)
		{
			case 1: // back in time
				tt = now() - tl_dp;
				if ((tl_dp != BAD_DATE) && (tt > 0))
				{
					m_t_from = tt;
					m_t_to = TIME_T_MAX;
				}
				break;
			case 2: // newer or older
				tt = dmy2time_t(tl_dd, tl_dm, tl_dy);
				if (tt != BAD_DATE)
				{
					if (tl_dx == 1)
					{ // forward
						m_t_from = tt;
						m_t_to = TIME_T_MAX;
					}
					else if (tl_dx == -1) // backward
					{
						m_t_to = tt;
						m_t_from = 1;
					}
				}
				break;
			case 3: // range is given
				if ( (tl_db) && (tl_de) && (tl_db != BAD_DATE) && (tl_de != BAD_DATE) )
				{
					m_t_from = tl_db;
					m_t_to = tl_de;
				}
		}
	}
	if (linkTo)
	{
		char link[3000];
		char* plink = link + sprintf(link, "link: ");
		UnescapeCGIQuery(plink, linkTo);
		m_query_words = link;
	}
	else
	{
		if (m_include_words.size() > 0)
		{
			m_query_words = CombineIE(m_include_words, m_exclude_words, m_iph, m_xph);
		}
	}
	return empty;
}

void CCgiQueryC::FormQuery()
{
	char str[BUFSIZ];
#ifndef UNICODE
	if (m_charset.size())
	{
		int cs = GetCharset(m_charset.c_str());
		if (cs)
		{
			char* buf = (char*)alloca(m_query_words.size() + 1);
			strcpy(buf, m_query_words.c_str());
			Recode(buf, cs, GetDefaultCharset());
			m_query_words = buf;
		}
	}
#endif
	AddIE(m_query_words, m_includeSites, m_excludeSites);
	if ((m_posmask != 0) && (m_posmask != 0xF))
	{
		char *buf = (char*)alloca(m_query_words.size() + 100);
		sprintf(buf, "%s%s%s%s(%s)", m_posmask & 1 ? "body:" : "", m_posmask & 2 ? "desc:" : "", m_posmask & 4 ? "keywords:" : "", m_posmask & 8 ? "title:" : "", m_query_words.c_str());
		m_query_words = buf;
	}
	m_query_url_escaped = EscapeURL(str, m_query_words.c_str());
	m_query_form_escaped = EscapeHtml(str, m_query_words.c_str(), '"');
	if (m_inres && m_query_words.size() && m_results.size())
	{
		char str[STRSIZ]="";
		sprintf(str, "(%s) (%s)", m_results.c_str(), m_query_words.c_str());
		m_query_words = str;
		EscapeURL(str, m_results.c_str());
		sprintf(m_r_str, "&amp;in=on&amp;r=%s", str);
	}
	m_qr_form_escaped = EscapeHtml(str, m_query_words.c_str(), '"');
	m_qr_url_escaped = EscapeURL(str, m_query_words.c_str());
}

void CCgiQueryC::FormHrefs(CTemplate *templ, char* href, char* hrefwg)
{
	char* phref = href;
	phref += sprintf(phref, "%s?q=%s%s%s", templ->self, m_query_url_escaped.c_str(), m_r_str, m_ul_str);
#ifdef UNICODE
	if (m_charset.size() > 0)
	{
		phref += sprintf(phref, "&amp;cs=%s", m_charset.c_str());
	}
#endif
	if (m_sort_by == SORT_BY_DATE)
	{
		phref += sprintf(phref, "&amp;s=date");
	}
	if (m_ps)
	{
		phref += sprintf(phref, "&amp;ps=%d", m_ps);
	}
	if (m_format)
	{
		phref += sprintf(phref, "&amp;o=%d", m_format);
	}
	// time limiting options
	if (m_t_from)
	{
		phref += sprintf(phref, "&amp;fr=%ld", m_t_from);
	}
	if (m_t_to)
	{
		phref += sprintf(phref, "&amp;to=%ld", m_t_to);
	}
	// end of time limiting options
	if (m_tmplp.size())
	{
		phref += sprintf(phref, "&amp;tmpl=%s", m_tmplp.c_str());
	}
	if (m_search_mode == SEARCH_MODE_FORMS)
	{
		if (m_wordform_langs.empty())
			phref += sprintf(phref, "&amp;fm=on");
		else
			phref += sprintf(phref, "&amp;fm=%s", m_wordform_langs.c_str());
	}
	else
	{
		phref += sprintf(phref, "&amp;fm=off");
	}
	strcpy(hrefwg, href);	// Href for "more results from"
	if (m_gr == 0)
	{
		phref += sprintf(phref, "&amp;gr=off");
	}
	if (m_site != 0)
	{
		phref += sprintf(phref, "&amp;st=%u", m_site);
	}
}
