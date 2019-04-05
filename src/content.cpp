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

/*  $Id: content.cpp,v 1.32 2004/03/22 11:47:59 kir Exp $
    Author : Alexander F. Avdonkin
	Uses parts of UdmSearch code
	Implemented classes: CParsedContent, CUrlContent
*/

#include <algorithm>
#include "aspseek-cfg.h"
#include "content.h"
#include "wcache.h"
#include "parse.h"
#include "charsets.h"
#include "squeue.h"
#include "deltas.h"
#include "stopwords.h"
#include "timer.h"
#include "misc.h"
#include "sqldbi.h"

using std::sort;

/* HTML parser states */
#define HTML_UNK	0
#define HTML_TAG	1
#define HTML_TXT	2
#define HTML_COM	3

/** Removes paired quotes (" or ') from the string.
 * If opening and closing quotes are not matching - do nothing.
 * If there is a text after the closing quote - trim it as well.
 */
void trim_quotes(std::string & str)
{
	char qc;
	int pos;
	if ((str[0] != '"') && (str[0] != '\''))
		return;
	qc = str[0];
	pos = str.rfind(qc);
	if (pos == 0) // only one quote at the first position found
		return;
	str = str.substr(1, pos - 1);
	return;
}


#ifdef UNICODE
void CParsedContent::AddWord(const WORD* word, WORD pos, int fontsize)
#else
void CParsedContent::AddWord(const char* word, WORD pos, int fontsize)
#endif
{
	const char *lang;
	if (!(lang = Stopwords.IsStopWord(word)))
	{
		AddWord1(word, pos, fontsize);
	}
	else
	{
		m_langmap->addlang(lang);
	}
}

int CParsedContent::ParseText(char* content, WORD& pos, CServer* CurSrv,
	int fontsize)
{
	char *lt;

	SetParsed();
	if (content)
	{
		lt = content;
		while (true)
		{
#ifdef UNICODE
			WORD word[MAX_WORD_LEN + 1];
			int r = GetUWord((unsigned char**)&lt, m_ucharset, word);
			if (r == 1) break;
			if (r == 0)
			{
				AddWord(word, pos, fontsize);
			}
#else
			CGetWord gw;
			char* s;
			s = gw.GetWord((unsigned char**)&lt, m_charset);
			if (s == NULL) break;
			int len = strlen(s);
			if ((len >= (CurSrv ? CurSrv->m_min_word_length : 1)) && (len <= (CurSrv ? CurSrv->m_max_word_length : MAX_WORD_LEN)))
			{
				char word[MAX_WORD_LEN + 1];
				safe_strcpy(word, s);
				Tolower((unsigned char*)word, m_charset);
				AddWord(word, pos, fontsize);
			}
#endif
			pos = (pos & 0xC000) + ((pos + 1) & 0x3FFF);
		}
	}
	return 0;
}

#ifdef UNICODE
int CParsedContent::ParseText(WORD* content, WORD& pos, CServer* CurSrv,
	int fontsize)
{
	WORD *lt;

	SetParsed();
	if (content)
	{
		lt = content;
		while (true)
		{
			CGetUWord gw;
			WORD* s;
			s = gw.GetWord(&lt);
			if (s == NULL) break;
/*			int len = strwlen(s);
			if ((len >= (CurSrv ? CurSrv->m_min_word_length : 1)) && (len <= (CurSrv ? CurSrv->m_max_word_length : MAX_WORD_LEN)))
			{
				AddWord(s, pos, fontsize);
			}
			pos = (pos & 0xC000) + ((pos + 1) & 0x3FFF);*/
			vector<WORD*> words;
			m_ucharset->GetWords(s, words);
			WORD* pw = s;
			for (vector<WORD*>::iterator ew = words.begin(); ew != words.end(); ew++)
			{
				int len = *ew - pw;
				if ((len >= (CurSrv ? CurSrv->m_min_word_length : 1)) && (len <= (CurSrv ? CurSrv->m_max_word_length : MAX_WORD_LEN)))
				{
					WORD word[MAX_WORD_LEN + 1];
					memcpy(word, pw, len * sizeof(WORD));
					word[len] = 0;
					AddWord(word, pos, fontsize);
				}
				pos = (pos & 0xC000) + ((pos + 1) & 0x3FFF);
				pw = *ew;
			}
		}
	}
	return 0;
}
#endif

int CParsedContent::ParseHead(char* content)
{
#ifndef UNICODE
	if (m_cache->local_charset == CHARSET_USASCII) return 0;
#endif
	int len;
	char* e;
	int state = HTML_UNK;
	int inscript = 0;
	char* s = content;
//	int dcharset = 0;
	string from_charset;
	WORD code;
#ifdef UNICODE
	const BYTE* s1 = (const BYTE*)content;
	const BYTE* e1;
	while ((s = (char*)s1, code = m_ucharset->NextChar(s1)))
	{
#else
	while (*s)
	{
		code = *(BYTE*)s;
#endif
		CTag tag;
		int t;
		switch(state)
		{
		case HTML_UNK: /* yet unknown */
#ifdef UNICODE
			if (!USTRNCMP(m_ucharset, s, "<!--"))
#else
			if (!STRNCMP(s, "<!--"))
#endif
			{
				state = HTML_COM;
			}
			else
			{
				state = *s == '<' ? HTML_TAG : HTML_TXT;
			}
#ifdef UNICODE
			s1 = (BYTE*)s;
#endif
			break;

		case HTML_TAG: /* tag */
#ifdef UNICODE
			t = ParseTag(m_ucharset, tag, len, s, e);
#else
			t = ParseTag(tag, len, s, e);
#endif
			if (!strcasecmp(tag.m_name, "body") || !strcasecmp(tag.m_name, "/head")) goto Exit;
			else if (!strcasecmp(tag.m_name, "script")) inscript = 1;
			else if (!strcasecmp(tag.m_name, "/script")) inscript = 0;
			else if ((!strcasecmp(tag.m_name, "meta")) && tag.m_equiv && tag.m_content)
			{
				if (!strcasecmp(tag.m_equiv, "Content-Type"))
				{
					char *p;
					for (p = tag.m_content; *p; p++) *p = tolower(*p);
					if ((p = strstr(tag.m_content,"charset=")))
					{
						from_charset = p + 8;
						trim_quotes(from_charset);
//						dcharset = GetCharset(p + 8);
					}
				}
				if (!strcasecmp(tag.m_equiv, "Last-Modified"))
				{
					m_lastmod = tag.m_content;
				}
			}
#ifdef UNICODE
			s1 = (BYTE*)s + len;
#else
			s += len;
#endif
			state = HTML_UNK;
			break;

		case HTML_COM: /* comment */
#ifdef UNICODE
			s1 = UStrStrE(m_ucharset, s1, (const BYTE*)"-->");
#else
			e = strstr(s, "-->");
			len = e ? e - s + 3 : strlen(s);
			s += len;
#endif
			state = HTML_UNK;
			break;

		case HTML_TXT: /* text */
#ifdef UNICODE
			e1 = (BYTE*)s;
			while ((e = (char*)e1, code = m_ucharset->NextChar(e1)) && code != '<');
			while (inscript && code && (STRNCASECMP(e, "</script")))
			{
				while ((e = (char*)e1, code = m_ucharset->NextChar(e1)) && (code != '<'));
			}
			s1 = (BYTE*)e;
#else
			e = strchr(s, '<');
			while (inscript && e && (STRNCASECMP(e, "</script")))
				e = strchr(e + 1, '<');
			int len = e ? e - s : strlen(s);
			s += len;
#endif
			state = HTML_UNK;
			break;
		}
	}
Exit:
	if (from_charset.size())
	{
		RecodeContent(content, GetCharset(from_charset.c_str()), m_cache->local_charset );
		SetOriginalCharset(from_charset.c_str());
	}

	return 0;
}
/*
void CUrlContent::SetCharset(char* tag_content, char* content)
{
	if (!charset)
	{
		char *p;
		if ((p = strstr(tag_content,"charset=")))
		{
			charset = p + 8;
			Recode(content, GetCharset(charset), m_cache->local_charset);
		}
	}
}
*/
void CWordContent::Parse(BYTE* content, const char* content_type, CWordCache* cache, CServer* curSrv, const char* cs)
{
	if (content)
	{
		m_cache = cache;
//		char* p;
#ifdef UNICODE
/*		if ((p = strstr(content_type, "charset=")))
		{
			m_ucharset = GetUCharset(p + 8);
		}*/
		m_ucharset = GetUCharset(cs);
#else
/*		int charset;
		if ((p = strstr(content_type, "charset=")))
		{
			charset = CHARSET_DEFAULT;
			charset = GetCharset(p + 8);
		}*/
		int charset = GetCharset(cs);
		if ((charset != CHARSET_DEFAULT) && CanRecode(charset, cache->local_charset)) m_charset = cache->local_charset;
#endif
		if (!STRNCASECMP(content_type, "text/html"))
		{
			char title[MAXTITLESIZE] = "";
			char keywords[MAXKEYWORDSIZE] = "";
			char descript[MAXDESCSIZE] = "";
			char text[2 * MAXTEXTSIZE] = "";
			ParseHtml(this, NULL, curSrv, NULL, (char*)content, 1, 0, text, keywords, descript, title);
		}
		else if (!STRNCASECMP(content_type, "text/plain") ||
			!STRNCASECMP(content_type, "text/tab-separated-values") ||
			!STRNCASECMP(content_type, "text/css"))
		{
			WORD pos = 0;
			ParseText((char*)content, pos, curSrv);
		}
	}
}

void CUrlContent::Save()
{
	double sqlRead = 0, sqlWrite = 0, sqlUpdate = 0;
	if (m_cache->m_hfiles) m_cache->m_hfiles->LockFiles();
	if (m_url.m_parsed)
	{
		int changed = 0;
		BYTE* oldc = NULL;
		ULONG oldwCount;
		ULONG oldLen;
		ULONG* oldHrefs = NULL;
		ULONG oldHsz;
		unsigned long int uLen;
		char content_type[50];
		string charset;
		CWordContent wold;
		{
			CTimer1 timer(sqlRead);
			oldLen = m_cache->m_database->GetContents(m_url.m_urlID, (BYTE**)&oldc, oldwCount, &oldHrefs, &oldHsz, content_type, charset);
			oldHsz >>= 2;
		}
		BYTE* uncompr = Uncompress(oldc, oldLen, uLen);
		wold.Parse(uncompr, content_type, m_cache, m_curSrv, charset.c_str());
		oldwCount = wold.size();
		int sz = ((size() + oldwCount) * 3 + m_url.m_totalCount);
		WORD* buf;
		if (sz >= 10000)
		{
			buf = new WORD[sz];
		}
		else
		{
			buf = (WORD*)alloca(sz << 1);
		}
		WORD* pbuf = buf;
		for (iterator it = begin(); it != end(); it++)
		{
			CWordBuddyVector& vec = it->second;
			*(ULONG*)pbuf = m_cache->GetWordID(it->first.Word());
			pbuf += 2;
			int sz = vec.size();
			if (sz > 0x3FFF)
			{
				sz = 0x3FFF;
			}
			*pbuf = sz;
#ifdef USE_FONTSIZE
			*pbuf |= (it->second.m_fontsize << 14);
#endif
			pbuf++;
			memcpy(pbuf, vec.begin(), sz << 1);
			sort<WORD*>(pbuf, pbuf + sz);
			pbuf += sz;
		}
		WORD* ebuf = pbuf;
		int wsize = size() + oldwCount;
		CULONG* words = wsize >= 10000 ? new CULONG[wsize] : (CULONG*)alloca(wsize * sizeof(CULONG));
		CULONG* pword = words;
		pbuf = buf;
		for (pbuf = buf; pbuf < ebuf; pbuf += (pbuf[2] & 0x3FFF) + 3)
		{
			pword->m_id = (ULONG*)pbuf;
			pword++;
		}
		CULONG* ewords = pword;
		sort<CULONG*>(words, pword);
		ULONG* deleted = oldwCount >= 10000 ? new ULONG[oldwCount] : (ULONG*)alloca(oldwCount * sizeof(ULONG));
		ULONG* pdeleted = deleted;
		for (CWordContent::iterator it = wold.begin(); it != wold.end(); it++)
		{
			if (find(*it) == end())
			{
				*pdeleted++ = m_cache->GetWordID(it->Word());
				changed = 1;
			}
		}
		if (changed == 0)
		{
			changed = (m_url.m_size != uLen) || (memcmp(uncompr, m_url.m_content, m_url.m_size) != 0);
		}
		if (uncompr - oldc != 4) delete uncompr;
		ULONG hsz = m_hrefs.size();
		ULONG* hrefs = hsz > 1000 ? new ULONG[hsz] : (ULONG*)alloca(hsz * sizeof(ULONG));
		ULONG* phref = hrefs;
		for (CULONGSet::iterator it = m_hrefs.begin(); it != m_hrefs.end(); it++)
		{
			*phref++ = *it;
		}
		sort<ULONG*>(hrefs, phref);
		if (m_follow && ((hsz != oldHsz) || ((hsz != 0) && memcmp(hrefs, oldHrefs, hsz << 2))))
		{
			changed = 1;
		}
		{
			CTimer1 timer(sqlWrite);
			m_cache->m_database->SaveContents(&m_url, ewords - words, changed, m_follow ? hrefs : oldHrefs, m_follow ? hsz : oldHsz, sqlUpdate);
		}
		if (changed || (m_cache->m_flags & FLAG_REINDEX))
		{
			pword = ewords;
			for (ULONG* del = deleted; del < pdeleted; del++, pword++)
			{
				*(ULONG*)pbuf = *del;
				pword->m_id = (ULONG*)pbuf;
				pbuf[2] = 0;
				pbuf += 3;
			}
			m_cache->SaveWords(words, pword, m_url.m_urlID, m_url.m_siteID);
			m_cache->SaveHrefs(hrefs, hsz, oldHrefs, oldHsz, m_url.m_urlID, m_url.m_siteID);
		}
		if (changed || (m_url.m_changed & 2) || m_url.m_new || m_url.m_lmod) m_cache->AddChanged(changed, m_url.m_changed & 2 ? 1 : 0, m_url.m_new ? 1 : 0, m_url.m_lmod ? 1 : 0);
		if (oldc) delete oldc;
		if (sz >= 10000) delete buf;
		if (hsz > 1000) delete hrefs;
		if (oldHrefs) delete oldHrefs;
		if (wsize >= 10000) delete words;
		if (oldwCount >= 10000) delete deleted;
	}
	else
	{
		CTimer1 timer(sqlWrite);
		m_cache->m_database->SaveContents(&m_url, 0, 0, NULL, 0, sqlUpdate);
	}
	m_cache->AddRedir(&m_url);
	if (m_cache->m_hfiles) m_cache->m_hfiles->AddLastmod(m_url.m_urlID, httpDate2time_t(m_url.m_last_modified.c_str()));
	m_cache->AddSqlTimes(sqlRead, sqlWrite, sqlUpdate);
}

void CUrlContent::UpdateUrl(int status, int period, int redir)
{
	m_url.m_period = (int)now() + period;
	m_url.m_status = status;
	m_url.m_redir = redir;
	m_url.m_changed |= 1;
}

void CUrlContent::UpdateLongUrl(int status, int period, int changed, int size,
	int hint, char *last_modified, char *etag, char *text, char *title,
	char *content_type, char *charset, char *keywords, char *descript,
	char *digest, char *lang)
{
	m_url.m_period = (int)now() + period;
	m_url.m_status = status;
	m_url.m_docsize = size;
	m_url.m_tag = hint;
	m_url.m_last_modified = last_modified;
	m_url.m_etag = etag;
	m_url.m_txt = text;
	m_url.m_title = title;
	m_url.m_content_type = content_type;
	m_url.m_keywords = keywords;
	m_url.m_description = descript;
	m_url.m_crc = digest;
	m_url.m_lang = lang;
	m_url.m_changed |= 3;
}
