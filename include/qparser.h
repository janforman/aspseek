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

/*  $Id: qparser.h,v 1.16 2002/12/18 18:38:53 kir Exp $
	Author: Alexander F. Avdonkin
*/

#ifndef _QPARSER_H_
#define _QPARSER_H_

#include <string>
#include "my_hash_map"
#include "parser.h"
#include "protocol.h"
#include "urlfilter.h"


class CSearchExpression;
class CSQLDatabase;
class CStringSet;
class CWord1;

#ifdef UNICODE
#include "ucharset.h"
typedef hash_map<CUWord, CPtr<CSearchExpression> > CMapStringToExpr;
typedef hash_map<CUWord, CPtr<CWord1> > CMapStringToWord1;
#else
typedef hash_map<std::string, CPtr<CSearchExpression> > CMapStringToExpr;
typedef hash_map<std::string, CPtr<CWord1> > CMapStringToWord1;
#endif

//typedef hash_map<string, CPtr<CSearchExpression> > CMapStringToExpr1;

class CTempType
{
public:
	int m_char;
	int m_old;
	int* m_oldPtr;

public:
	CTempType(int chr, int type, int* typeTable)
	{
		m_char = chr;
		m_old = typeTable[chr];
		m_oldPtr = typeTable + chr;
		*m_oldPtr = type;
	}
	CTempType(int chr, int* typeTable)
	{
		m_char = chr;
		m_old = typeTable[chr];
		m_oldPtr = typeTable + chr;
		*m_oldPtr = xLetter1;
	}
	~CTempType()
	{
		*m_oldPtr = m_old;
	}
};

class CQueryParser : public CGeneralParser
{
public:
	CSQLDatabase* m_database;
	CMapStringToExpr m_wordMap;
	CMapStringToWord1 m_wordMapE;
	CMapStringToExpr m_lightWords;
	int m_search_mode;
	std::string m_wordform_langs;
	int m_complexPhrase;
	std::string m_error;
	/** Pointer to URL filter - used in getWord
	 * when instantiating new CWord1 or CPattern
	 */
	CResultUrlFilter* m_filter;

public:
	CQueryParser(CResultUrlFilter* filter)
	{
		m_complexPhrase = 0;
		m_search_mode = SEARCH_MODE_EXACT;
		m_filter = filter;
	}
	void Clear()
	{
		m_wordMap.clear();
	}
	ULONG PosMask();
	void AddWords(CStringSet& light);
	virtual void initReserved();
	CSearchExpression* ParseQuery(const char *szQuery);
	CStackElem* getPhrase(CTokenArray* tokens);
	BOOL subBoolTerm();
	BOOL subQuery();
	BOOL phrase();
	BOOL phraseContent();
	CStackElem* getWord(char* elem);
	BOOL Word();
	BOOL query();
	BOOL boolFactor();
	BOOL boolTerm(int pos);
	BOOL boolTerm();
	BOOL boolTerm_();
	CStackElem* orCondition_(CTokenArray* t1);
	CStackElem* andCondition_(CTokenArray* t1);
	BOOL OrSymbol();
	BOOL AndSymbol();
	BOOL NotSymbol();
	BOOL siteFilter();
	BOOL siteQuery();
	BOOL siteSubQuery();
	BOOL siteOrQuery();
	CStackElem* orSites(CTokenArray* tokens);
	ULONG GetUrlID(const char* url);
	BOOL linkTo();
	void removeLast(CPtr<CSearchExpression>& expr)
	{
		m_stack.removeLast((CPtr<CStackElem>&)expr);
	}
};

#endif
