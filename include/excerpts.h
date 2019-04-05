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

/*  $Id: excerpts.h,v 1.15 2002/05/14 11:47:15 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _EXCERPTS_H_
#define _EXCERPTS_H_

#include <string>
#include <map>
#include "defines.h"
#include "charsets.h"
#ifdef UNICODE
#include "ucharset.h"
#endif

using std::string;
using std::map;

//extern ULONG MaxExcerptLen;

class CSearchExpression;
class CSrcDocument;

/**
 * This class is binary tree based map, used to find values for keys
 * greater or equal to the specified
 */
class CMapIntToInt : public map<int, int>
{
public:
	int GetOffset(int off)
	{
		iterator it = lower_bound(-off);
		return it == end() ? off : it->second + off + it->first;
	}
};

/**
 * This class represents word in the linked list of several
 * recently parsed words
 */
class CMatchWord
{
public:
	CMatchWord* m_prev;	///< Previous word in list
	CMatchWord* m_next;	///< Next word in list
	int m_position;		///< Position of word in the source URL content
	int m_epos;		///< Position of end of word in the source for last word before HTML tag
#ifdef UNICODE
	WORD m_word[MAX_WORD_LEN + 1];	///< Word itself
	int m_end;

public:
	int Length()
	{
		return strwlen(m_word);
	}
#else
	char m_word[MAX_WORD_LEN + 1];	///< Word itself

public:
	int Length()
	{
		return strlen(m_word);
	}
#endif
};

/// This class represents excerpt in the linked list of found excerpts.
class CExcerpt
{
public:
	char* m_buf;		///< Excerpt buffer
	int m_position;		///< Position in the buffer for the next words
	CExcerpt* m_prev;	///< Previous excerpt in the linked list
	ULONG m_maxExcerptLen;	///< Maximum excerpt length

public:
	CExcerpt(ULONG maxExcerptLen)
	{
		m_prev = 0;
		m_position = 0;
		m_maxExcerptLen = maxExcerptLen;
		m_buf = new char[maxExcerptLen];
		m_buf[0] = 0;
	}
	~CExcerpt()
	{
		delete m_buf;
	}
	/// Add text to the buffer
	int AddText(char* buf, int count, int charset, int spaces = 0);
	/// Form excerpt string for all excerpts in the list
	void FormExcerpts(CStringVector& excerpts);
};

class CMatchFinderBase
{
public:
	int m_maxsize;			///< Maximal number of recenlty parsed words in the linked list
	int m_size;			///< Current list size
	int m_charset;			///< Local charset
	char* m_content;		///< Original URL content
	CMatchWord* m_tail;		///< Tail of linked list of recently parsed words
	CMatchWord* m_head;		///< Head of linked list of recently parsed words
	CSearchExpression* m_search;	///< Search expression to find
#ifdef UNICODE
	CCharsetB* m_ucharset;
#endif

public:
	CMatchFinderBase(int maxsize)
	{
		m_maxsize = maxsize;
		m_size = 0;
		m_head = m_tail = NULL;
		m_charset = GetDefaultCharset();
#ifdef UNICODE
		m_ucharset = &baseCharset;
#endif
	}
#ifdef UNICODE
	void AddToList(const WORD* wrd, int pos, int end);
#else
	void AddToList(const char* wrd, int pos);
#endif
	virtual int ParseText(char* content, int start_offset, CMapIntToInt* offmap) = 0;
	void EndText(int epos);
	int ParseText1(int len, char* s);
	int ParseHtml();
	void ParseDoc(CSrcDocument* doc);
};

/// Main class used for finding excerpts from source, containing search expression
class CMatchFinder : public CMatchFinderBase
{
public:
	int m_epos;
	int m_match;			///< Desired match degree
	int m_exactFound;		///< Set to 1 if match with desired degree already found
	int m_excerpts;			///< Number of excrepts to find more
	int m_total_excerpts;		///< Total number of excerpts
	ULONG m_maxExcerptLen;		///< Maximum excerpt length;
	CExcerpt* m_lastExcerpt;	///< Last found excerpt

public:
	CMatchFinder(int maxsize, ULONG maxExcerptLen) : CMatchFinderBase(maxsize)
	{
		m_maxsize = maxsize + (maxExcerptLen >> 3);
		m_excerpts = 2;
		m_lastExcerpt = NULL;
		m_epos = 0;
		m_match = 3;
		m_exactFound = 0;
		m_maxExcerptLen = maxExcerptLen;
	}
	void AddExcerpt(CExcerpt* exc)
	{
		exc->m_prev = m_lastExcerpt;
		m_lastExcerpt = exc;
	}
	virtual ~CMatchFinder();
	virtual int ParseText(char* content, int start_offset, CMapIntToInt* offmap);
	int CopyRemove(int start, int len, char* buf, int maxsize, int& total);
	CMatchWord* FindStart(int max_offset);
	int FillExcerpt(CMatchWord* wrd, char* buf, int maxsize);
	void FormExcerpts(CStringVector& excerpts);
	int ExactMatch(ULONG how);
};

/// This class is used to find all position of all search terms in source URL content
class CWordFinder : public CMatchFinderBase
{
public:
	vector<ULONG> m_offsets;	///< Array of found source offsets
	vector<ULONG> m_colors;		///< Array of highlight colors

public:
	CWordFinder(int maxsize) : CMatchFinderBase(maxsize + 2) {};
	virtual ~CWordFinder() {};
	virtual int ParseText(char* content, int start_offset, CMapIntToInt* offmap);
};

#endif /* _EXCERPTS_H_ */
