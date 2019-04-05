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

/*  $Id: excerpts.cpp,v 1.16 2002/05/11 15:27:27 kir Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CExcerpt, CMatchFinder
*/

#include "excerpts.h"
#include "search.h"

//ULONG MaxExcerptLen = 200;

// This function replaces special HTML symbols by spaces and builds map between old and new positions
// Parameters: "s" is the source buffer, "len" is the length of source buffer, "dst" is the destination buffer
// "charset" is the local charset, "offmap" is the binary tree map, with key equal to negated new position,
// and value equal to old positon
// Returns length of output buffer
int RemoveSpecials(char* s, int len, char* dst, int charset, CMapIntToInt* offmap)
{
	int j = 0;
	char* e = dst; *e = 0;
	char sch;
	int fl = 0;
	int last_dif = 0;
	for (int i = 0; i < len; i++)
	{
		char* ps;
		switch (s[i])
		{
		case ' ' : case '\t':
		case '\n': case '\r':
			fl = 1;
			j++; break;

#ifdef UNICODE
		case '&': /* parse specials */
			fl = 1;
			ps = s + i + 1;
			sch = SgmlToChar((const char*&)ps, charset);
			if (sch == 0x20)
			{
				j++;
			}
			else
			{
				if (j)
				{
					*e=' '; e++; j = 0;
				}
				memcpy(e, s + i, (ps - s) - i);
				e += (ps - s) - i;
				*e = 0;
			}
			i = ps - s - 1;
			break;
#else
		case '&': /* parse specials */
			fl = 1;
			if (j)
			{
				*e=' '; e++; j=0;
			}
			i++;
			ps = s + i;
			sch = SgmlToChar((const char*&)ps, charset);
			i = ps - s;
			if (sch)
			{
				if (sch == ' ') j++;
				*e = sch; e++; *e = 0;
			}
			else
			{
				*e = '?'; e++; *e=0;
			}
			break;
#endif
		default:
			if (j)
			{
				fl = 1;
				*e = ' '; e++; j = 0;
			}
			if (fl)
			{
				fl = 0;
				if (offmap && (last_dif != (e - dst) - i))
				{
					// Insert or remove occured
					(*offmap)[dst - e] = i;
					last_dif = (e - dst) - i;
				}
			}
			*e = (s[i] == '\'') ? '`' : s[i];
			e++; *e = 0;
			break;
		}
		if ((offmap == NULL) && j)
		{
			*e = ' '; e++;
		}
	}
	return e - dst;
}

// This method copies part of URL source to the excerpt buffer
// Returns 1 if buffer is full
int CExcerpt::AddText(char* buf, int count, int charset, int spaces)
{
	// First, remove special HTML symbols from the source
	int len6 = count * 2 + 8;
	char* rbuf = Alloca(len6);
	int rcount = RemoveSpecials(buf, count, rbuf, charset, NULL);
	if (m_position + rcount + spaces < (int)m_maxExcerptLen)
	{
		// There is space for text in the buffer
		memcpy(m_buf + m_position, rbuf, rcount);
		m_position += rcount;
		if (spaces)
		{
			m_buf[m_position++] = ' ';
		}
		m_buf[m_position] = 0;
		Freea(rbuf, len6);
		return 0;
	}
	Freea(rbuf, len6);
	return 1;
}

// This method adds word to the linked list of recenlty parsed word
// Parameters are: "wrd" is the word to add, "pos" is the original position in URL content
#ifdef UNICODE
void CMatchFinderBase::AddToList(const WORD* wrd, int pos, int end)
#else
void CMatchFinderBase::AddToList(const char* wrd, int pos)
#endif
{
	CMatchWord* head = NULL;
	if (m_size >= m_maxsize)
	{
		// Remove head object from list and use it
		head = m_head;
		m_head = m_head->m_next;
		if (m_head) m_head->m_prev = NULL;
	}
	else
	{
		// Create new CMatchWord object
		m_size++;
		head = new CMatchWord;
		if (m_head == NULL) m_head = head;
	}
	// Insert object to the tail of list
	head->m_next = NULL;
	head->m_prev = m_tail;
	if (m_tail) m_tail->m_next = head;
	head->m_position = pos;
	head->m_epos = 0;
#ifdef UNICODE
	head->m_end = end;
	memcpy(head->m_word, wrd, sizeof(head->m_word));
#else
	strncpy(head->m_word, wrd, sizeof(head->m_word));
	head->m_word[sizeof(head->m_word) - 1] = 0;
#endif
	m_tail = head;
}

// This method stores marks last parsed word as last before HTML tag
// and stores source position of last character in it
void CMatchFinderBase::EndText(int epos)
{
	if (m_tail && (m_tail->m_epos == 0))
	{
		m_tail->m_epos = epos;
	}
}

// This method finds the most distant word from the tail in the linked list of recently parsed words,
// which source position less than position of last word in the list by value less than max_offset
CMatchWord* CMatchFinder::FindStart(int max_offset)
{
	if (m_tail)
	{
		CMatchWord* wrd;
		int start = m_tail->m_position;
		for (wrd = m_tail->m_prev; wrd; wrd = wrd->m_prev)
		{
			int len = (wrd->m_epos == 0 ? start : wrd->m_epos) - wrd->m_position;
			if (max_offset < len) return wrd;
			max_offset -= len;
			start = wrd->m_position;
		}
		return m_head;
	}
	return NULL;
}

void CExcerpt::FormExcerpts(CStringVector& excerpts)
{
	if (m_prev)
	{
		// Recursively call itself to fill buffer for previous words
		m_prev->FormExcerpts(excerpts);
		excerpts.push_back(m_buf);
		return;
	}
	else
	{
		excerpts.push_back(m_buf);
		return;
	}
}

void CMatchFinder::FormExcerpts(CStringVector& excerpts)
{
	m_lastExcerpt->FormExcerpts(excerpts);
}

// This method removes special HTML symbols from range of input buffer
// and appends it to the output buffer
// Returns 1 if output buffer is full
int CMatchFinder::CopyRemove(int start, int len, char* buf, int maxsize, int& total)
{
	int len6 = len * 2 + 6;
	char* rbuf = Alloca(len6);
	int rlen = RemoveSpecials(m_content + start, len, rbuf, m_charset, NULL);
	if (total + rlen > maxsize)
	{
		Freea(rbuf, len6);
		return 1;
	}
	memcpy(buf + total, rbuf, rlen);
	total += rlen;
	Freea(rbuf, len6);
	return 0;
}

// This method fills excerpt buffer starting with position of word "wrd"
int CMatchFinder::FillExcerpt(CMatchWord* wrd, char* buf, int maxsize)
{
	int total = 0;
	int start = 0;
	CMatchWord* pwrd = NULL;
	// Process each word, starting with specified
	while (wrd)
	{
		if (start == 0) start = wrd->m_position;
		if (wrd->m_epos)
		{
			// Last word before HTML tag
			int len = wrd->m_epos - start;
			if (CopyRemove(start, len, buf, maxsize, total))
			{
				pwrd = wrd;
				break;
			}
			m_epos = start + len;
			// Add space instead of tag
			if ((total != 0) && (total + 1 <= maxsize))
			{
				buf[total++] = ' ';
			}
			start = 0;
		}
		pwrd = wrd;
		wrd = wrd->m_next;
	}
	if (start && pwrd)
	{
		// Copy last text part of HTML
#ifdef UNICODE
		int epos = pwrd->m_end;
#else
		int epos = pwrd->m_position + pwrd->Length();
#endif
		int len = epos - start;
		if (CopyRemove(start, len, buf, maxsize, total) == 0)
		{
			m_epos = start + len;
		}
		if (pwrd->m_epos && (total != 0) && (total + 1 <= maxsize))
		{
			buf[total++] = ' ';
		}
	}
	buf[total] = 0;
	return total;
}

int CMatchFinder::ExactMatch(ULONG how)
{
	int form = ((m_match & 4) == 0) || (how & 0x80000000);
	switch (m_match & 3)
	{
	case 0:
		// No subphrases found
		return form;
	case 1: case 2:
		// 1 or more subphrases found
		return (how > 0) && form;
	case 3:
		// Phase found
		return ((how & 0x3FFFFFFF) >= 0x3FFFFFFE) && form;
	}
	return 0;
}

// This method parses a piece of text
// Parameters:
// "content" is the text to parse
// "start_offset" is the position of text in URL source
// "map" is binary tree map, with key equal new position
// and value equal to original position in the URL source
int CMatchFinder::ParseText(char* content, int start_offset, CMapIntToInt* offmap)
{
	char *s, *lt;
	if (content)
	{
		int epos = 0;
		lt = content;
		if (m_epos)
		{
			m_epos = start_offset;
		}
		while (true)
		{
			// Find next word
			int len;
#ifdef UNICODE
			WORD uword[MAX_WORD_LEN + 1];
			int r = GetUWord((unsigned char**)&lt, m_ucharset, uword, &s);
			if (r == 1) break;
			if (r == 0)
			{
				ULONG how;
				len = strwlen(uword);
				int start = start_offset + (offmap ? offmap->GetOffset(s - content) : s - content);
#else
			CGetWord gw;
			s = gw.GetWord((unsigned char**)&lt, m_charset);
			if (s == NULL) break;
			len = strlen(s);
			if (len < MAX_WORD_LEN + 1)	// Ignore long words
			{
				char wrd[MAX_WORD_LEN + 1];
				ULONG how;
				// Find original word position
				int start = start_offset + (offmap ? offmap->GetOffset(s - content) : s - content);
				strcpy(wrd, s);
				Tolower((unsigned char*)wrd, m_charset);
#endif
				int ep = start_offset + (offmap ? offmap->GetOffset(lt - content) : lt - content);
				if (m_epos && m_lastExcerpt)
				{
					// At least one excerpt is found and it is not full, add word to it
//					int ep = start + len;
					if (m_lastExcerpt->AddText(m_content + m_epos, ep - m_epos, m_charset))
					{
						// Excerpt is full
						m_epos = 0;
						// Specified number of excerpts is found
						// and excerpt with desired match degree is found
						if ((m_excerpts == 0) && m_exactFound) return 1;
					}
					else
					{
						m_epos = ep;
					}
				}
#ifdef UNICODE
				AddToList(uword, start, ep);
#else
				AddToList(wrd, start);
#endif
				epos = ep;
				if ((m_epos == 0) && m_search && m_tail && m_search->Match(m_tail, how))
				{
					// Last words match search expression and previous excerpt is full
					CExcerpt* exc;
					if (m_excerpts > 0)
					{
						// There are excerpts to find more, create new one
						exc = new CExcerpt(m_maxExcerptLen);
						AddExcerpt(exc);
						--m_excerpts;
					}
					else
					{
						// All excerpts found, but desired match degree is not found, overwrite the last excerpt
						exc = m_lastExcerpt;
						exc->m_position = 0;
					}
					// Fill excerpt with recently parsed URL content
					CMatchWord* wrd = FindStart(m_maxExcerptLen >> 1);
					exc->m_position = FillExcerpt(wrd, exc->m_buf, m_maxExcerptLen - 1);
					if ((m_exactFound == 0) && ExactMatch(how))
					{
						// Desired match found
						m_exactFound = 1;
					}
				}
			}
		}
		if (epos != 0)
		{
			int ep = (offmap ? offmap->GetOffset(lt - content) : lt - content) + start_offset;
			if (m_epos && m_lastExcerpt)
			{
				// Put last non alphanumeric HTML content to the excerpt buffer
				if (m_lastExcerpt->AddText(m_content + m_epos, ep - m_epos, m_charset, 1))
				{
					m_epos = 0;
					if ((m_excerpts == 0) && m_exactFound) return 1;
				}
				else
				{
					m_epos = ep;
				}
			}
			// Set end of text piece
			EndText(ep);
		}
	}
	return 0;
}

// This method parses a piece of source HTML content
int CMatchFinderBase::ParseText1(int len, char* s)
{
	CMapIntToInt offmap;
	int len6 = 2 * len + 6;
	char* tmp = Alloca(len6);
	// Remove special HTML symbols and build offset map
	RemoveSpecials(s, len, tmp, m_charset, &offmap);
	// Parse text
	int p = ParseText(tmp, s - m_content, &offmap);
	Freea(tmp, len6);
	return p;
}

/* HTML parser states */
#define HTML_UNK	0
#define HTML_TAG	1
#define HTML_TXT	2
#define HTML_COM	3

// This method parses source URL content and finds excerpts.
// It returns as soon as required excerpts are built
int CMatchFinderBase::ParseHtml()
{
	int len, inbody, inscript, intitle, instyle, state;
	int index = 1;
	int noindex = 0;
	char *s = 0, *e = 0, *lt;
	inbody = inscript = intitle = instyle = 0;
	state = HTML_UNK;
	s = m_content;
	WORD code;
#ifdef UNICODE
	const BYTE* e1;
	const BYTE* s1 = (const BYTE*)s;
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
			if (!strcasecmp(tag.m_name, "body")) inbody = 1;
			else if (!strcasecmp(tag.m_name, "/body")) inbody = 0;
			else if (!strcasecmp(tag.m_name, "script")) inscript = 1;
			else if (!strcasecmp(tag.m_name, "/script")) inscript = 0;
			else if (!strcasecmp(tag.m_name, "style")) instyle = 1;
			else if (!strcasecmp(tag.m_name, "/style")) instyle = 0;
			else if (!strcasecmp(tag.m_name, "title")) intitle = 1;
			else if (!strcasecmp(tag.m_name, "/title")) intitle = 0;
			else if ((!strcasecmp(tag.m_name, "meta")) && tag.m_equiv && tag.m_content)
			{
				if (!strcasecmp(tag.m_equiv, "keywords"))
				{
					if (ParseText(tag.m_content, t + s - m_content, NULL)) return 1;
					state = HTML_UNK;
				}
				else if (!strcasecmp(tag.m_equiv, "description"))
				{
					if (ParseText(tag.m_content, t + s - m_content, NULL)) return 1;
					state = HTML_UNK;
				}
				else if (!strcasecmp(tag.m_equiv, "robots"))
				{
					lt = tag.m_content;
					while (true)
					{
						CGetWord gw;
						e = gw.GetWord((unsigned char**)&lt, m_charset);
						if (e == NULL) break;
						if (!strcasecmp(e, "ALL"))
						{
							index = 1;
						}
						else if (!strcasecmp(e, "NONE"))
						{
							index = 0;
						}
						else if (!strcasecmp(e, "NOINDEX"))
						{
							index = 0;
						}
						else if (!strcasecmp(e, "INDEX"))
						{
							index = 1;
						}
					}
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
			// check for ending noindex tags
			if (noindex) // check for ending noindex tags
			{
				if (!STRNCASECMP(s, "<!--/noindex-->"))
					noindex &= ~1;
				else if (!STRNCASECMP(s, "<!--/htdig_noindex-->"))
					noindex &= ~2;
				else if (!STRNCASECMP(s, "<!--/UdmComment-->"))
					noindex &= ~4;
			}
			// check for starting noindex tags
			if (!STRNCASECMP(s, "<!--noindex-->"))
				noindex |= 1;
			else if (!STRNCASECMP(s, "<!--htdig_noindex-->"))
				noindex |= 2;
			else if (!STRNCASECMP(s, "<!--UdmComment-->"))
				noindex |= 4;

#ifdef UNICODE
			s1 = UStrStrE(m_ucharset, s1, (const BYTE*)"-->");
#else
			e = strstr(s, "-->");
			len = e ? e - s + 3 :strlen(s);

			/* This is for debug pusposes
			tmp=(char*)malloc(len+1);
			strncpy(tmp,s,len);tmp[len]=0;
			printf("com: '%s'\n",tmp);
			*/

			s += len;
#endif
			state = HTML_UNK;
			break;

		case HTML_TXT: /* text */
#ifdef UNICODE
			e1 = (const BYTE*)s;
			while ((e = (char*)e1, code = m_ucharset->NextChar(e1)) && code != '<');
			while (inscript && code && (STRNCASECMP(e, "</script")))
			{
				while ((e = (char*)e1, code = m_ucharset->NextChar(e1)) && (code != '<'));
			}
#else
			e = strchr(s, '<');
			while (inscript && e && (STRNCASECMP(e, "</script")))
				e = strchr(e + 1, '<');
#endif
			int len = e ? e - s : strlen(s);
			if (!inscript && !intitle && !instyle && !noindex)
			{
				// Don't find exceprts in title, scripts and text surrounded by <style>
				if (ParseText1(len, s)) return 1;
			}
#ifdef UNICODE
			s1 = (BYTE*)e;
#else
			s += len;
#endif
			state = HTML_UNK;
			break;
		}
	}
	return 0;
}

void CMatchFinderBase::ParseDoc(CSrcDocument* doc)
{
	// Always parse source as HTML, regardless of content type not to put
	// special HTML symbols to the excerpts
	m_content = doc->m_content;
#ifdef UNICODE
	m_ucharset = GetUCharset(doc->m_charset.c_str());
#endif
	ParseHtml();
}

CMatchFinder::~CMatchFinder()
{
	while (m_head)
	{
		CMatchWord* head = m_head;
		m_head = m_head->m_next;
		delete head;
	}
	while (m_lastExcerpt)
	{
		CExcerpt* exc = m_lastExcerpt;
		m_lastExcerpt = m_lastExcerpt->m_prev;
		delete exc;
	}
}

int CWordFinder::ParseText(char* content, int start_offset, CMapIntToInt* offmap)
{
	char *s, *lt;
	if (content && offmap)
	{
		int added = 0;
		lt = content;
		while (true)
		{
			// Find next word
			CMatchWord* firstw;
			ULONG color = 0;
#ifdef UNICODE
			WORD uword[MAX_WORD_LEN + 1];
			int len;
			int r = GetUWord((unsigned char**)&lt, m_ucharset, uword, &s);
			if (r == 1) break;
			if (r == 0)
			{
				len = strwlen(uword);
				int start = start_offset + (offmap->GetOffset(s - content));
				AddToList(uword, start, start_offset + offmap->GetOffset(lt - content));
#else
			CGetWord gw;
			s = gw.GetWord((unsigned char**)&lt, m_charset);
			if (s == NULL) break;
			int len = strlen(s);
			if (len < MAX_WORD_LEN + 1)	// Ignore long words
			{
				char wrd[MAX_WORD_LEN + 1];
				// Find original word position
				int start = start_offset + (offmap->GetOffset(s - content));
				strcpy(wrd, s);
				Tolower((unsigned char*)wrd, m_charset);
				AddToList(wrd, start);
#endif
				added++;
				if (m_search && m_tail && ((firstw = m_search->Match(color, m_tail))))
				{
					// Last words match search expression
					while (firstw)
					{
						int fp = firstw->m_position;
						while (firstw && (firstw->m_epos == 0))
						{
							firstw = firstw->m_next;
						}
						m_offsets.push_back(fp);
//						m_offsets.push_back(firstw ? firstw->m_epos : start + len);
						m_offsets.push_back(firstw ? firstw->m_epos : start_offset + offmap->GetOffset(lt - content));
						m_colors.push_back(color);
						if (firstw) firstw = firstw->m_next;
					}
				}
			}
		}
		if (added)
		{
			// Set end of text piece
			int ep = (offmap->GetOffset(lt - content)) + start_offset;
			EndText(ep);
		}
	}
	return 0;
}

