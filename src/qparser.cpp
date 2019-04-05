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

/*  $Id: qparser.cpp,v 1.27 2002/06/15 12:55:03 al Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CQueryParser
*/

#include "affix.h"
#include "qparser.h"
#include "search.h"
#include "sqldb.h"
#include "datasource.h"
#include "stopwords.h"
#include "daemon.h"

#define AND		1000
#define OR		1001
#define SITE	1002
#define LINK	1003
#define IN_BODY		1004
#define IN_DESC		1005
#define IN_KEYWORDS	1006
#define IN_TITLE	1007

// minimal length of pattern word in query (like someth*)
int MinPatLen = 6;

struct {char* m_word; int m_type;}
reservedWords[] = {
{"AND", AND},
{"OR", OR},
{"site:", SITE},
{"link:", LINK},
{"body:", IN_BODY},
{"desc:", IN_DESC},
{"keywords:", IN_KEYWORDS},
{"title:", IN_TITLE}
};

int CountEmpties(CTokenArray* tokens)
{
	int res = 0;
	for (Stack::iterator e = tokens->m_tokens.begin(); e != tokens->m_tokens.end(); e++)
	{
//		if (((CSearchExpression*)(*e).p)->IsEmpty())
		if ((*e)->IsEmpty())
		{
			res++;
		}
	}
	return res;
}

int CountSiteFilters(CTokenArray* tokens)
{
	int res = 0;
	for (Stack::iterator e = tokens->m_tokens.begin(); e != tokens->m_tokens.end(); e++)
	{
		if ((*e)->IsSiteFilter())
		{
			res++;
		}
	}
	return res;
}

int CountStops(CTokenArray* tokens)
{
	int res = 0;
	for (Stack::iterator e = tokens->m_tokens.begin(); e != tokens->m_tokens.end(); e++)
	{
//		if (((CSearchExpression*)(*e).p)->IsStop())
		if ((*e)->IsStop())
		{
			res++;
		}
	}
	return res;
}

int CountNots(CTokenArray* tokens)
{
	int res = 0;
	for (Stack::iterator e = tokens->m_tokens.begin(); e != tokens->m_tokens.end(); e++)
	{
//		if (((CSearchExpression*)(*e).p)->IsNot())
		if ((*e)->IsNot())
		{
			res++;
		}
	}
	return res;
}

ULONG CQueryParser::GetUrlID(const char* url)
{
	ULONG redir;
	ULONG urlID;
	int len = strlen(url);
	const char* u;
	if (len > MAX_URL_LEN)
	{
//		error_("Too long URL");
		return 0;
	}
	if (strstr(url, "://") == NULL)
	{
		if (len > MAX_URL_LEN - 7) return 0;
		char urlh[MAX_URL_LEN + 1];
		sprintf(urlh, "http://%s", url);
		u = urlh;
		urlID = m_database->GetUrlID(urlh, redir);
		len += 7;
	}
	else
	{
		u = url;
		urlID = m_database->GetUrlID(url, redir);
	}
	if (urlID == 0)
	{
		if ((len <= MAX_URL_LEN ) && (u[len - 1] != '/'))
		{
			char urlh[MAX_URL_LEN + 1];
			sprintf(urlh, "%s/", u);
			return GetUrlID(urlh);
		}
		else
		{
			return 0;
		}
	}
	while (redir)
	{
		urlID = m_database->GetUrlID(redir);
		if (urlID == 0)
		{
//			error_("URL not found");
			return 0;
		}
	}
	return urlID;
}

BOOL CQueryParser::query()
{
	BOOL t1;
	BOOL t2;
	int t3;
	t3 = m_stack.size();
	t1 = true;
	while ((t2 = (t1 || OrSymbol())) && boolFactor())
	{
		t1 = false;
	}
	if (t1 ? false : ((t2 ? (unexpectedError(), NULL) : NULL), combineStackTo_(t3),true))
	{
		replaceStackTop_(orCondition_((CTokenArray*)m_stack.last()));
		return true;
	}
	return false;
}

CStackElem* CQueryParser::orCondition_(CTokenArray* t1)
{
	int stps = CountStops(t1);
	int empt = CountEmpties(t1) + stps;
	if (t1->size() - empt > 1)
	{
		COrWords* res = new COrWords;
		res->m_database = m_database;
		res->m_words.reserve(t1->size() - empt);
//		for (CSearchExpression** ent = (CSearchExpression**)t1->m_tokens.begin(); ent != (CSearchExpression**)t1->m_tokens.end(); ent++)
		for (Stack::iterator ent = t1->m_tokens.begin(); ent != t1->m_tokens.end(); ent++)
		{
			if (!(*ent)->IsEmpty() && !(*ent)->IsStop())
			{
				res->m_words.push_back((CSearchExpression*)(ent->p));
				(*ent)->AddRef();
			}
		}
		m_complexPhrase |= 2;
		return res;
	}
//	for (CSearchExpression** ent = (CSearchExpression**)t1->m_tokens.begin(); ent != (CSearchExpression**)t1->m_tokens.end(); ent++)
	for (Stack::iterator ent = t1->m_tokens.begin(); ent != t1->m_tokens.end(); ent++)
	{
		if (!(*ent)->IsEmpty() && !(*ent)->IsStop())
		{
			return *ent;
		}
	}
	return stps? (CStackElem*)new CStopWord : (CStackElem*)new CEmpty(m_database);
}

CStackElem* CQueryParser::andCondition_(CTokenArray* t1)
{
	if (CountEmpties(t1) > 0) return new CEmpty(m_database);
	int stops = CountStops(t1);
	int siteFilters = CountSiteFilters(t1);
	int nots = CountNots(t1);
	int sz = t1->size() - stops - siteFilters;
	CSearchExpression* result = NULL;
	if ((sz <= nots) && (nots > 0))
	{
		error(ER_EMPTYQUERY);
		return NULL;
	}
	if (sz > 1)
	{
		CAndWords* res = new CAndWords;
		res->m_database = m_database;
		res->m_words.reserve(sz);
		res->m_dist.reserve(sz);
		WORD dist = 1;
//		for (CSearchExpression** ent = (CSearchExpression**)t1->m_tokens.begin(); ent != (CSearchExpression**)t1->m_tokens.end(); ent++)
		for (Stack::iterator ent = t1->m_tokens.begin(); ent != t1->m_tokens.end(); ent++)
		{
			if (!(*ent)->IsStop() && !(*ent)->IsSiteFilter())
			{
				if (res->m_words.size() > 0)
				{
					res->m_dist.push_back(dist);
				}
				dist = 1;
				res->m_words.push_back((CSearchExpression*)(ent->p));
				(*ent)->AddRef();
			}
			else
			{
				dist++;
			}
		}
		res->m_dist.push_back(0);
		result = res;
	}
	else
	{
//		for (CSearchExpression** ent = (CSearchExpression**)t1->m_tokens.begin(); ent != (CSearchExpression**)t1->m_tokens.end(); ent++)
		for (Stack::iterator ent = t1->m_tokens.begin(); ent != t1->m_tokens.end(); ent++)
		{
			if (!(*ent)->IsStop() && !(*ent)->IsSiteFilter())
			{
				result = (CSearchExpression*)(ent->p);
				break;
			}
		}
		if (result == NULL) return new CStopWord;
	}
	if (siteFilters > 0)
	{
		CAndSiteFilter* andFilter = NULL;
		if (siteFilters > 1)
		{
			andFilter = new CAndSiteFilter;
			result->m_siteFilter = andFilter;
		}
		for (Stack::iterator ent = t1->m_tokens.begin(); ent != t1->m_tokens.end(); ent++)
		{
			if ((*ent)->IsSiteFilter())
			{
				if (andFilter)
				{
					ent->p->AddRef();
					andFilter->m_filters.push_back((CSiteFilter*)ent->p);
				}
				else
				{
					result->m_siteFilter = (CSiteFilter*)ent->p;
					break;
				}
			}
		}
	}
	return result;
}

BOOL CQueryParser::AndSymbol()
{
	return peekForTypeNoPush_(AND) || peekForType_value_(binary, "+");
}

BOOL CQueryParser::OrSymbol()
{
	return peekForTypeNoPush_(OR) || peekForTypeNoPush_(verticalBar);
}

BOOL CQueryParser::NotSymbol()
{
	int delim = Delimited();
	int minus = peekForType_value_(binary, "-");
	return (delim && !Delimited()) ? minus : false;
}

BOOL CQueryParser::boolTerm(int pos)
{
	if (boolTerm())
	{
		if (pos == 1)
		{
			CPtr<CStackElem> base;
			m_stack.removeLast(base);
			if (base->IsSiteFilter())
			{
				((CSiteFilter*)base.p)->m_ie ^= 1;
				push(base);
			}
			else
			{
				CSearchExpression* expr = (CSearchExpression*)base.p;
				if (!expr->IsEmpty() && !expr->IsStop())
				{
					push(new CNot(expr));
				}
			}
		}
		return true;
	}
	else
	{
		return false;
	}
}

BOOL CQueryParser::boolFactor()
{
	BOOL t1;
	BOOL t2;
	int t3;
	t3 = m_stack.size();
	t1 = true;
	BOOL _not = 0;
	BOOL _and = 0;
	BOOL cmplx = 0;
	while ((((t2 = (t1 || (_and = AndSymbol(), _not = NotSymbol(), true))) && (boolTerm(_not)))))
	{
		cmplx = cmplx || _and || _not;
		t1 = false;
	}
	if (t1 ? false : ((t2 ? (unexpectedError(), NULL) : NULL) , combineStackTo_(t3),true))
	{
		replaceStackTop_(andCondition_((CTokenArray*)m_stack.last()));
		if (cmplx) m_complexPhrase |= 2;
		return true;
	}
	return false;
}

CStackElem* CQueryParser::getWord(char* elem)
{
	ULONG word_id;
#ifdef UNICODE
	WORD uword[MAX_WORD_LEN + 1];
	int len = m_ucharset->RecodeStringLower((BYTE*)elem, uword, MAX_WORD_LEN);
	if (len > MAX_WORD_LEN)
	{
		return new CEmpty(m_database);
	}
	for (WORD* puw = uword; *puw; puw++)
	{
		switch (*puw)
		{
		case '*':
			*puw = '%'; break;
		case '?':
			*puw = '_'; break;
		}
	}
	if (m_wordMap.find(uword) == m_wordMap.end())
#else
	Tolower((unsigned char*)elem, GetDefaultCharset());
	if (m_wordMap.find(elem) == m_wordMap.end())
#endif
	{
#ifdef UNICODE
		if ((strcmp(elem, "*") == 0) || Stopwords.IsStopWord(uword))
#else
		if ((strcmp(elem, "*") == 0) || Stopwords.IsStopWord(elem))
#endif
		{
			return new CStopWord;
		}
		struct timeval tm, tm1;
		char* pat;
		if ((pat = strpbrk(elem, "*?")))
		{
			if (pat - elem >= MinPatLen)
			{
				string orig = elem;
				while (*pat != 0)
				{
					switch (*pat)
					{
					case '*': *pat = '%'; break;
					case '?': *pat = '_'; break;
					}
					pat++;
				}
				ULONG wc = 0, wc1 = 0;
				CSearchExpression* se;
				gettimeofday(&tm, NULL);
				vector<ULONG>* wordIDs = NULL;
				if (strlen(elem) <= (MAX_WORD_LEN << 1))
				{
#ifdef UNICODE
					if (CompactStorage)
					{
						wordIDs = m_database->GetPatternIDs(uword);
						wc = wordIDs ? wordIDs->size() : 0;
					}
					else
					{
						wc = m_database->CheckPattern(uword);
					}
					wc1 = m_database->CheckPattern(uword, 1);
#else
					if (CompactStorage)
					{
						wordIDs = m_database->GetPatternIDs(elem);
						wc = wordIDs ? wordIDs->size() : 0;
					}
					else
					{
						wc = m_database->CheckPattern(elem);
					}
					wc1 = m_database->CheckPattern(elem, 1);
#endif
				}
				if (wc || wc1)
				{
					gettimeofday(&tm1, NULL);
					CPattern* pt;
#ifdef UNICODE
					se = pt = new CPattern(uword, orig, wc, wc1, m_database, m_filter);
					m_lightWords[uword] = se;
#else
					se = pt = new CPattern(elem, orig, wc, wc1, m_database, m_filter);
					m_lightWords[orig] = se;
#endif
					pt->m_wordIDs = wordIDs;
					pt->m_checkTime = timedif(tm1, tm);
				}
				else
				{
					se = new CEmpty(m_database);
				}
#ifdef UNICODE
				m_wordMap[uword] = se;
#else
				m_wordMap[orig] = se;
#endif
				return se;
			}
			else
			{
				error(ER_TOOSHORT);
			}
		}
		else
		{
			CSearchExpression* se = NULL;
			switch (m_search_mode)
			{
			case SEARCH_MODE_EXACT:
				gettimeofday(&tm, NULL);
#ifdef UNICODE
				if ((strlen(elem) <= MAX_WORD_LEN) && m_database->GetWordID(uword, word_id))
#else
				if ((strlen(elem) <= MAX_WORD_LEN) && m_database->GetWordID(elem, word_id))
#endif
				{
					gettimeofday(&tm1, NULL);
					CWord1* word1 = new CWord1(word_id, m_filter);
					word1->m_word = elem;
					word1->m_database = m_database;
					word1->m_checkTime = timedif(tm1, tm);
					se = word1;
#ifdef UNICODE
					memcpy(word1->m_uword, uword, sizeof(uword));
					m_lightWords[uword] = se;
#else
					m_lightWords[elem] = se;
#endif
				}
				else
				{
					se = new CEmpty(m_database);
				}
				break;
			case SEARCH_MODE_FORMS:
				{
					hash_set<CSWord> forms;
#ifdef UNICODE
					FindForms(uword, forms, m_wordform_langs.c_str());
#else
					FindForms(elem, forms, m_wordform_langs.c_str());
#endif
					vector<CSearchExpression*> seForms;
					int exact = -1;
					for (hash_set<CSWord>::iterator it = forms.begin(); it != forms.end(); it++)
					{
						CMapStringToWord1::iterator itw = m_wordMapE.find(it->c_str());
						if (itw == m_wordMapE.end())
						{
							gettimeofday(&tm, NULL);
							if ((STRLEN(it->c_str()) <= MAX_WORD_LEN) && m_database->GetWordID(it->c_str(), word_id))
							{
#ifdef UNICODE
								if (strwcmp(it->c_str(), uword) == 0) exact = seForms.size();
#else
								if (strcmp(it->c_str(), elem) == 0) exact = seForms.size();
#endif
								CWord1* word1 = new CWord1(word_id, m_filter);
#ifdef UNICODE
								memcpy(word1->m_uword, it->c_str(), sizeof(uword));
#else	//	TO DO
								word1->m_word = it->c_str();
#endif
								word1->m_database = m_database;
								word1->m_checkTime = timedif(tm1, tm);
								m_wordMapE[it->c_str()] = word1;
								seForms.push_back(word1);
								m_lightWords[it->c_str()] = word1;
							}
							else
							{
								m_wordMapE[it->c_str()] = NULL;
							}
						}
						else
						{
							if (itw->second)
							{
#ifdef UNICODE
								if (strwcmp(it->c_str(), uword) == 0) exact = seForms.size();
#else
								if (strcmp(it->c_str(), elem) == 0) exact = seForms.size();
#endif
								seForms.push_back(itw->second);
							}
						}
					}
					if (seForms.size() > 0)
					{
						if (seForms.size() == 1)
						{
							se = seForms[0];
						}
						else
						{
							COrWordsF* oforms = new COrWordsF;
							oforms->m_words.insert(oforms->m_words.begin(), seForms.begin(), seForms.end());
							oforms->m_exactForm = exact;
							oforms->m_database = m_database;
							for (vector<CSearchExpression*>::iterator expr = oforms->m_words.begin(); expr != oforms->m_words.end(); expr++)
							{
								(*expr)->AddRef();
							}
							se = oforms;
						}
					}
					else
					{
						se = new CEmpty(m_database);
					}
				}
				break;
			}
#ifdef UNICODE
			m_wordMap[uword] = se;
#else
			m_wordMap[elem] = se;
#endif
			return se;
		}
	}
	else
	{
#ifdef UNICODE
		return m_wordMap[uword];
#else
		return m_wordMap[elem];
#endif
	}
	return NULL;
}

BOOL CQueryParser::Word()
{
	if (peekForType_(word))
	{
		CPtr<CToken> word1;
		m_stack.removeLast((CPtr<CStackElem>&)word1);
		char* token = word1->m_token;
		push(getWord(token));
		return true;
	}
	return false;
}

BOOL CQueryParser::subQuery()
{
	BOOL t1;
	BOOL t2;
	int t3;
	t3 = m_stack.size();
	t1 = true;
	while ((t2 = (t1 || OrSymbol())) && subBoolTerm())
	{
		t1 = false;
	}
	if (t1 ? false : ((t2 ? (unexpectedError(), NULL) : NULL), combineStackTo_(t3),true))
	{
		replaceStackTop_(orCondition_((CTokenArray*)m_stack.last()));
		return true;
	}
	return false;
}

CStackElem* CQueryParser::getPhrase(CTokenArray* tokens)
{
	if (CountEmpties(tokens) > 0) return new CEmpty(m_database);
	int stops = CountStops(tokens);
	int size;
	if ((size = tokens->size() - stops) > 1)
	{
		WORD pos = 0;
		CPhrase* res = new CPhrase;
		res->m_database = m_database;
		res->m_words.reserve(size);
		res->m_positions.reserve(size + 1);
		for (Stack::iterator it = tokens->m_tokens.begin(); it != tokens->m_tokens.end(); it++, pos++)
		{
			CSearchExpression* ent = (CSearchExpression*)(*&(*it));
			if (!ent->IsStop())
			{
				res->m_words.push_back(ent);
				res->m_positions.push_back(pos);
				ent->AddRef();
				if (ent->Complex()) m_complexPhrase |= 1;
			}
		}
		res->m_positions.push_back(pos);
		return res;
	}
	// FIXME: gcc3
	for (Stack::iterator it = tokens->m_tokens.begin(); it != tokens->m_tokens.end(); it++)
	{
		CSearchExpression* ent = (CSearchExpression*)(*&(*it));
		if (!ent->IsStop())
		{
			return ent;
		}
	}
	return new CStopWord;
}

BOOL CQueryParser::phraseContent()
{
	BOOL t1;
	int t2;
	t2 = m_stack.size();
	t1 = false;
	CTempType tp1('-', xDelimiter1, m_typeTable);
	CTempType tp2('+', xDelimiter1, m_typeTable);
	while (subQuery())
	{
		t1 = true;
	}
	if ((t1 && (combineStackTo_(t2),true)))
	{
		replaceStackTop_(getPhrase((CTokenArray*)m_stack.last()));
		return true;
	}
	return false;
}

BOOL CQueryParser::phrase()
{
	backupSave();
	if (peekForTypeNoPush_(xDoubleQuote1))
	{
		if (phraseContent() && peekForTypeNoPush_(xDoubleQuote1))
		{
			backupDiscard();
			return true;
		}
		else
		{
			error(ER_NOQUOTED);
		}
	}
	else
	{
		backupRestore();
		return false;
	}
	return false;
}

BOOL CQueryParser::subBoolTerm()
{
	if (Word())
	{
		return true;
	}
	backupSave();
	if (peekForTypeNoPush_(leftParenthesis))
	{
		if (subQuery() && peekForTypeNoPush_(rightParenthesis))
		{
			backupDiscard();
			return true;
		}
		else
		{
			error(ER_NOPARENTHESIS);
		}
	}
	else
	{
		backupRestore();
		return false;
	}
	return false;
}

CStackElem* CQueryParser::orSites(CTokenArray* t1)
{
	if (t1->size() > 1)
	{
		COrSiteFilter* res = new COrSiteFilter;
		res->m_filters.reserve(t1->size());
		for (Stack::iterator it = t1->m_tokens.begin(); it != t1->m_tokens.end(); it++)
		{
			CSiteFilter* ent = (CSiteFilter*)(*&(*it));
			res->m_filters.push_back(ent);
			ent->AddRef();
		}
		return res;
	}
	else
	{
		return *t1->m_tokens.begin();
	}
}

BOOL CQueryParser::siteFilter()
{
	BOOL res = false;
//	CTempType t1('-', m_typeTable);
//	CTempType t2('.', m_typeTable);
	if (peekForTypeNoPush_(SITE, ".-") && peekForType_(word))
	{
		CPtr<CToken> site;
		m_stack.removeLast((CPtr<CStackElem>&)site);
		CWordSiteFilter* limit = new CWordSiteFilter;
		limit->m_database = m_database;
		limit->m_site = site->m_token;
		push(limit);
		res = true;
	}
	return res;
}

BOOL CQueryParser::linkTo()
{
	CTempType t1('.', m_typeTable);
	CTempType t2(':', m_typeTable);
	CTempType t3('/', m_typeTable);
	CTempType t4('?', m_typeTable);
	CTempType t5('-', m_typeTable);
	CTempType t6('_', m_typeTable);
	CTempType t7('&', m_typeTable);
	CTempType t8('@', m_typeTable);
	BOOL res = false;
	if (peekForTypeNoPush_(LINK) && peekForType_(word))
	{
		CPtr<CToken> url;
		m_stack.removeLast((CPtr<CStackElem>&)url);
		ULONG urlID = GetUrlID(url->m_token);
		if (urlID)
		{
			CLinkTo* link = new CLinkTo(m_filter);
			link->m_database = m_database;
			link->m_urlID = urlID;
			push(link);
		}
		else
		{
			push(new CEmpty(m_database));
		}
		res = true;
	}
	return res;
}

BOOL CQueryParser::siteOrQuery()
{
	BOOL t1;
	BOOL t2;
	int t3;
	t3 = m_stack.size();
	t1 = true;
	while ((t2 = (t1 || OrSymbol())) && siteQuery())
	{
		t1 = false;
	}
	if (t1 ? false : ((t2 ? (unexpectedError(), NULL) : NULL), combineStackTo_(t3),true))
	{
		replaceStackTop_(orSites((CTokenArray*)m_stack.last()));
		return true;
	}
	return false;
}

BOOL CQueryParser::siteSubQuery()
{
	backupSave();
	if (peekForTypeNoPush_(leftParenthesis))
	{
		if (siteOrQuery())
		{
			if (peekForTypeNoPush_(rightParenthesis))
			{
				backupDiscard();
				return true;
			}
			else
			{
				error(ER_NOPARENTHESIS);
			}
		}
		else
		{
			backupRestore();
			return false;
		}
	}
	else
	{
		backupRestore();
		return false;
	}
	return false;
}

BOOL CQueryParser::siteQuery()
{
	return siteSubQuery() || siteFilter();
}

ULONG CQueryParser::PosMask()
{
	ULONG pm = 0;
	ULONG pm1;
	while ((pm1 = 1, peekForTypeNoPush_(IN_BODY)) || (pm1 = 2, peekForTypeNoPush_(IN_DESC)) ||
		(pm1 = 4, peekForTypeNoPush_(IN_KEYWORDS)) || (pm1 = 8, peekForTypeNoPush_(IN_TITLE)))
	{
		pm |= pm1;
	}
	return pm;
}

BOOL CQueryParser::boolTerm()
{
	ULONG pm = PosMask();
	if (boolTerm_())
	{
		if ((pm != 0) && (pm != 0xF))
		{
			CPtr<CSearchExpression> expr;
			removeLast(expr);
			if (expr->IsSiteFilter())
			{
				push(expr);
			}
			else
			{
				push(new CPositionFilter(expr, pm, m_database));
			}
		}
		return true;
	}
	else
	{
		return false;
	}
}

BOOL CQueryParser::boolTerm_()
{
	if (Word())	return true;
	if (siteQuery()) return true;
	backupSave();
	if ((phrase() ? (backupDiscard()) : (backupRestore())))
	{
		return true;
	}
	backupSave();
	if (peekForTypeNoPush_(leftParenthesis))
	{
		if (query() && peekForTypeNoPush_(rightParenthesis))
		{
			backupDiscard();
			return true;
		}
		else
		{
			error(ER_NOPARENTHESIS);
		}
	}
	else
	{
		backupRestore();
		return false;
	}
	return false;
}

void CQueryParser::initReserved()
{
	for (ULONG i = 0; i < sizeof(reservedWords) / sizeof(reservedWords[0]); i++)
	{
		m_reservedWords[reservedWords[i].m_word] = reservedWords[i].m_type;
	}
}

void CQueryParser::AddWords(CStringSet &light)
{
	for (CMapStringToExpr::iterator it = m_lightWords.begin(); it != m_lightWords.end(); it++)
	{
		if (it->second)
		{
			CSearchExpression* src = it->second;
			CWordStat& stat = light[it->first];
			CResult* res = src->m_sresult[0];
			CResult* res1 = src->m_sresult[1];
			stat.m_urls = (res ? res->m_counts[1] : 0) + (res1 ? res1->m_counts[1] : 0);
			stat.m_total = (res ? res->m_counts[0] : 0) + (res1 ? res1->m_counts[0] : 0);
#ifdef UNICODE
			stat.m_output = m_ucharset->RecodeFrom(it->first.Word());
#endif
		}
	}
}

CSearchExpression* CQueryParser::ParseQuery(const char *szQuery)
{
	try
	{
		CReadStream strm(szQuery);
		scan_(&strm);
		if (linkTo() || query())
		{
			if (m_tokenType == doIt)
			{
				CPtr<CSearchExpression> stat;
				m_stack.removeLast((CPtr<CStackElem>&)stat);
				if (stat->IsStop())
				{
					m_error = ER_STOPWORDS;
					return NULL;
				}
				stat->AddRef();
				return stat;
			}
			else
			{
				m_error = ER_EXTRASYMBOL;
				return NULL;
			}
		}
		else
		{
			m_error = ER_EMPTYQUERY;
			return NULL;
		}
	}
	catch(CParserException* p_exception)
	{
		m_error = p_exception->m_message;
		delete p_exception;
		return NULL;
	}
}
