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

/*  $Id: search.h,v 1.32 2002/12/18 18:38:53 kir Exp $
	Author: Alexander F. Avdonkin
*/

#ifndef _SEARCH_H_
#define _SEARCH_H_


// This header file contains definitions of classes used in query parse tree

/* Class hierarchy tree:
 *	CStackElem				Base and root class defined in stack.h
 *	+---CSearchExpression			Abstract class for searching expressions
 *	|	+---CWord1			Class used for searching one word
 *	|	+---CPattern			Class used for searching one pattern
 *	|	+---CMultiSearch		Base class for searching several subexpressions
 *	|	|	+---CAndWords		Class used for searching AND expression
 *	|	|	+---COrWords		Class used for searching OR expression
 *	|	|	|	+---COrWordsF	Class used for searching word forms. Example 'create'->'creates', 'created'
 *	|	|	+---CPhrase		Class used for searching phrase
 *	|	+---CStopWord			Class instantiated when stopword is found
 *	|	+---CEmpty			Class instantiated when word or pattern is not found in database
 *	|	+---CNot			Class used in expressions with minus sign
 *	|	+---CPositionFilter			Class used for HTML section based filtering
 *	+---CSiteFilter				Abstract class used for filtration by site names (used in advanced search)
 *		+---CWordSiteFilter		Class used for filtration by part of site name
 *		+---CMultiSiteFilter		Base class used for filtering by several site values
 *			+---CAndSiteFilter	Class used for filtration by several site name patterns combined by AND
 *			+---COrSiteFilter	Class used for filtration by list site name patterns
 */

/* Auxiliary classes:
 *
 *	CResult					Holds information about sites and urls where search expression is found
 *	CSiteInfo				Holds urls found on particular site
 *	CSites					Class used when sites bitmap for site pattern is being built
 *	CUrlWeight				Class used as parameter to STL sort function when retrieving first N URLs with maximum ranks
 *	CSite1					Class used as parameter to STL sort function when search by pattern is performed
 *	CRanks					Instances of this class holds PageRanks for each URL in database
 */

#include <string>
#include <vector>
#include <map>
#include <set>
#include "my_hash_set"
#include "defines.h"
#include "url.h"
#include "charsets.h"
#include "parser.h"
#include "urlfilter.h"

using std::string;
using std::vector;
using std::map;
using std::set;

class CSQLDatabase;
class CMatchWord;

class CSearchExpression;

class CUrlWeight2;

inline char* stpcpy(char* dst, const char* src)
{
	char* d = dst;
	while (*src)
	{
		*d++ = *src++;
	}
	return d;
}


class CChildVector : public vector<CHAR*>
{
public:
	~CChildVector()
	{
		for (iterator child = begin(); child != end(); child++)
		{
			delete *child;
		}
	}
};

/// Holds urls found on particular site
class CSiteInfo
{
public:
	ULONG m_site;			///< Site ID
	ULONG m_offset;			///< Offset of URLs information in search results buffer
	ULONG m_length;			///< Length of URLs information in search results buffer
	ULONG m_maxwoffset;		///< Offset of URL information with highest rank (or latest lastmod)
	ULONG m_maxwoffset2;		///< Offset of URL information with second highest rank (or latest lastmod)
	ULONG m_urlcount;		///< Count of URLs found in site
};

/// Holds information about sites and urls where search expression is found
class CResult
{
public:
	ULONG* m_sites;			///< Array of sites read from word file or blob
	WORD* m_urls;			///< Information about URLs read from word file or blob
	ULONG* m_siteSet;		///< Prelimnary bitmap of sites where search expression can be found
	CSiteInfo* m_siteInfos;		///< Array of final information about found sites
	ULONG m_infoCount;		///< Count of sites in array m_infoCount
	ULONG m_setSize;		///< Size of preliminary bitmap expressed in ULONG count
	ULONG m_minSite;		///< First site in preliminary bitmap shifted to the right by 5
	ULONG m_psite;			///< Current value of site ID used in search in array m_sites
	ULONG m_counts[2];		///< Total count ([0]) and URL count ([1]) where word is present in whole database
	FILE* m_source;			///< File with information about word, NULL if information is in database BLOB
	BYTE* m_partBuf;		///< Buffer used for partial reading of word information from file
	ULONG m_partTo;			///< Max site ID of URL information in buffer
	ULONG m_maxSite;		///< Maximal URL info length for all sites
	ULONG m_siteLen;
	int m_not;			///< Set to 1 if result generated by CNot

public:
	CResult()
	{
		m_sites = NULL;
		m_urls = NULL;
		m_siteSet = NULL;
		m_siteInfos = NULL;
		m_psite = 0;
		m_infoCount = 0;
		m_source = NULL;
		m_partBuf = NULL;
		m_counts[0] = m_counts[1] = 0;
		m_maxSite = 0;
		m_minSite = 0;
		m_not = 0;
		m_setSize = 0;
		m_siteLen = 0;
	}
	~CResult()
	{
		if (m_sites) delete m_sites;
		if (m_urls) delete m_urls;
		if (m_siteSet) delete m_siteSet;
		if (m_siteInfos) delete m_siteInfos;
		if (m_partBuf) delete m_partBuf;
		if (m_source) fclose(m_source);
	}
	void Zero()
	{
		m_sites = NULL;
		m_urls = NULL;
		m_siteSet = NULL;
		m_source = NULL;
	}
	void MinMaxSite(ULONG* minmax)
	{
		minmax[0] = m_minSite;
		minmax[1] = m_minSite + m_setSize;
	}
	void CreateSet();				///< Builds bitmap from site array
	void Normalize();				///< Removes trailing and leading zeroes from sitesbitmap
	ULONG* ComputeSizes(CResult* srcResult);	///< Returns array of URL info sizes for all sites found
	void Reorganize(CResult* srcResult);		///< Builds final site info array
	ULONG GetResultForSite(ULONG site, BYTE*& p_buf, ULONG mask, CResultUrlFilter* filter);		///< Returns information about URLs for site
	ULONG GetResultForSiteL(ULONG site, BYTE*& p_buf, CResultUrlFilter* filter);
	CSiteInfo* GetSiteInfo(ULONG site);		///< Finds appropriate site info in the array of final site infos for given site
	BYTE* GetUrls(CSiteInfo* info);			///< Retuns pointer to URLs information for give site info pointer
	/// Builds complete final info about all sites and urls found for expression
	ULONG GetFinalResult(CSearchExpression* src, int total, ULONG* urls, int slen, int real, int gr, int sortBy);
	/// Returns information about first nFirst URLs or sites with highest ranks or latest lastmod dates
	void GetUrls(ULONG& nFirst, ULONG& urlCount, CUrlW*& p_urls, int gr, int sortBy);
	void GetUrls(CUrlWeight2* start, CUrlWeight2*& dst, int gr, int sortBy);
	/// Filters sites by web space
	void SelectSpace(ULONG* space, ULONG min, ULONG max);
	/// Filters sites by array of site IDs
	void SelectSites(vector<ULONG>* sites, int num);
	int NotEmpty()
	{
		return m_infoCount > 0;
	}
};

/**
 * Abstract class used for filtration by site names (used in advanced search).
 * Instances of classes derived from CSiteFilter are created
 * when "site: sitename" constructs are found in search query.
 */
class CSiteFilter : public CStackElem
{
public:
	int m_ie;			///< (0) Include (1) Exclude
	ULONG* m_siteMap;		///< Bitmap of sites
	ULONG m_minSite;		///< First site in bitmap shifted to the right by 5
	ULONG m_maxSite;		///< Last site + 1 in bitmap shifted to the right by 5

public:
	CSiteFilter()
	{
		m_ie = 0;
		m_siteMap = NULL;
		m_minSite = 0;
		m_maxSite = ~0;
	}
	~CSiteFilter()
	{
		if (m_siteMap) delete m_siteMap;
	}
	/// Used to retrieve site bitmap once
	void GetMap()
	{
		if ((m_siteMap == NULL) && (m_maxSite == (ULONG)~0)) GetMap1();
	}
	/// Filters given sites
	void FilterSites(ULONG* sites, ULONG minsite, ULONG maxsite);
	virtual int IsSiteFilter() {return 1;};
	virtual void GetMap1() = 0;	///< Used to build site bitmap for different site filter expressions
	/// Returns 1 if given site matches filter expression
	int SiteFilter(ULONG site)
	{
		if (m_siteMap)
		{
			ULONG s5 = site >> 5;
			ULONG xr = m_ie ? ~0 : 0;
			return (s5 >= m_minSite) && (s5 < m_maxSite) ? (m_siteMap[s5 - m_minSite] ^ xr) & (1 << (site & 0x1F)) : m_ie;
		}
		else
		{
			return m_ie;
		}
	}
	virtual CHAR *NormalizedText()
	{
		return NULL;
	}
};

/// Class used when sites bitmap for site pattern is being built
class CSites
{
public:
	ULONG* m_sites;		///< Array of site IDs
	ULONG m_size;		///< Array size

public:
	CSites()
	{
		m_size = 0;
		m_sites = NULL;
	}
	~CSites()
	{
		if (m_sites) delete m_sites;
	}
};

/// Class used for filtration by part of site name
class CWordSiteFilter : public CSiteFilter
{
public:
	CSQLDatabase* m_database;	///< database
	string m_site;			///< site pattern

public:
	CWordSiteFilter()
	{
		m_database = NULL;
	}
	virtual void GetMap1();
	virtual CHAR *NormalizedText()
	{
		CHAR* result = new CHAR[m_site.size() + 6];
#ifdef UNICODE
		stpbwcpy(stpbwcpy(result, "site:"), m_site.c_str());
#else
		sprintf(result, "site:%s", m_site.c_str());
#endif
		return result;
	}
};

/// Base class used for filtering by several site values
class CMultiSiteFilter : public CSiteFilter
{
public:
	vector<CSiteFilter*> m_filters;	///< Array of filters

public:
	virtual ~CMultiSiteFilter()
	{
		for (vector<CSiteFilter*>::iterator f = m_filters.begin(); f != m_filters.end(); f++)
		{
			(*f)->Release();
		}
	}
	virtual CHAR *NormalizedText()
	{
		int cnt = 0;
		CChildVector childs;
		int tlen = 0;
		for (vector<CSiteFilter*>::iterator ent = m_filters.begin(); ent != m_filters.end(); ent++)
		{
			CHAR *p = (*ent)->NormalizedText();
			if (p == NULL)
			{
				return NULL;
			}
			childs.push_back(p);
			tlen += STRLEN(p);
		}
		CHAR* buf = new CHAR[tlen + m_filters.size() * 3 + 3];
		CHAR* pbuf = buf;
		pbuf = STPBWCPY(pbuf, "(");
		for (CChildVector::iterator child = childs.begin(); child != childs.end(); child++)
		{
			pbuf = STPCPY(pbuf, *child);
			if (cnt) pbuf = STPBWCPY(pbuf, GetOperation());
			cnt++;
		}
		if (cnt)
		{
			STPBWCPY(pbuf, ")");
		}
		*pbuf = 0;
		return buf;
		
	}
	virtual const char* GetOperation() = 0;
};

/// Class used for filtration by several site name patterns combined by AND
class CAndSiteFilter : public CMultiSiteFilter
{
public:
	virtual void GetMap1();
	virtual const char* GetOperation() {return " a ";}
};

/// Class used for filtration by list site name patterns
class COrSiteFilter : public CMultiSiteFilter
{
public:
	virtual void GetMap1();
	virtual const char* GetOperation() {return " o ";}
};

/// Class used as parameter to STL sort function when retrieving first N URLs with maximum ranks
class CUrlWeight
{
public:
	WORD* m_url;		///< Pointer to URL info
	int m_siteWeight;	///< Priority of site
	ULONG m_weight;		///< Rank
	CSiteInfo* m_si;	///< Pointer to site info

public:
	int operator<(const CUrlWeight& ui) const
	{
		int res = ui.m_siteWeight - m_siteWeight;
		if (res)
		{
			return res < 0;
		}
		else
		{
			if (ui.m_weight == m_weight)
			{
				return *(ULONG*)m_url < *(ULONG*)ui.m_url;
			}
			else
			{
				return ui.m_weight < m_weight;
			}
		}
	}
};

class CUrlWeight2 : public CUrlWeight
{
public:
	WORD* m_url2;
	WORD* m_urlr;
	WORD* m_urlr2;
	CSiteInfo* m_si2;
};

/// Abstract class for searching expressions
class CSearchExpression : public CStackElem
{
public:
	CSQLDatabase* m_database;	///< database
	CResult* m_sresult[2];		///< Results from main database ([0]) and "real-time" database
	CPtr<CSiteFilter> m_siteFilter;	///< Site filter or NULL if there is no site filter
	ULONG m_order;			///< Index for highlight color
public:
	CSearchExpression()
	{
		m_sresult[0] = m_sresult[1] = NULL;
		m_order = 0;
	}
	virtual ~CSearchExpression()
	{
		if (m_sresult[0]) delete m_sresult[0];
		if (m_sresult[1]) delete m_sresult[1];
	}
	virtual int IsNot() {return 0;};		///< Only CNot returns 1
	virtual ULONG InitValue() {return 0;};		///< Must return initial value for site bitmap
	CResult* GetSites(int size, CResult** res);
	virtual CResult* GetSites(int real) = 0;	///< Must return result with sites bitmap
	/// Used to call GetSites once
	CResult* GetSites1(int real)
	{
		if (m_sresult[real] == NULL)
		{
			CResult* res;
			res = m_sresult[real] = GetSites(real);
			if (m_siteFilter && res && res->m_siteSet)
			{
				m_siteFilter->FilterSites(res->m_siteSet, res->m_minSite, res->m_minSite + res->m_setSize);
			}
		}
		return m_sresult[real];
	}
	void PrintResults() {PrintResults(0);};			///< Used for debugging
	virtual void PrintResults(int indent) {};		///< Used for debugging
	virtual void MinMax(ULONG* minmax, ULONG* minmax1) {};	///< Update minimal and maximal value of site ID
	virtual void CombineSite(ULONG* res, ULONG src) {};	///< ORs or ANDs *res with src
	virtual ULONG* ComputeSizes(CResult* result, int real) = 0;	///< Return array of URLs info sizes by for each site
	virtual void ReorganizeResults(CResult* res, int real) = 0;	///< Build final site info array in m_sresult[real]
	/// Return information about URLs found in specified site
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask) = 0;
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real)
	{
		return GetResultForSite(site, p_buf, real, 0x800F);
	}
	virtual int Complex() {return 1;};
	// Return maximum length of expression in words as can appear in URL content
	virtual int MaxSize() {return 1;};
	/// Must set and increment order for all subexpressions
	virtual void SetColorOrder(ULONG& order)
	{
		if (m_order == 0)
		{
			m_order = order++;
		}
	}
	/// Return first word in sequence if expression matches given word, or NULL otherwise
	virtual CMatchWord* Match(CMatchWord* mword, ULONG& how) {return NULL;};
	virtual CMatchWord* Match(ULONG& color, CMatchWord* mword)
	{
		ULONG how;
		color = m_order;
		return Match(mword, how);
	};
	/**
	 * Returns results about all URLs and sites found in main
	 * and "real-time" database, taking into account supplied URL filters
	 * and URL spaces.
	 */
	ULONG GetResult(const char* urls, int site, int gr, int sortBy, CULONGSet& spaces, CResult** res);
	int ExtractSubset(char* path);	///< Returns subset ID for URL mask
	/// Returns 1 if given site matches site filter
	int SiteFilter(ULONG site)
	{
		return m_siteFilter ? m_siteFilter->SiteFilter(site) : 1;
	}
	/// Returns information about URLs found in specified site, taking into account site filter
	ULONG GetResultForSiteFiltered(ULONG site, BYTE*& p_buf, int real, ULONG mask)
	{
		if (SiteFilter(site))
		{
			return GetResultForSite(site, p_buf, real, mask);
		}
		else
		{
			p_buf = 0;
			return 0;
		}
	}
	virtual int GetResultSize(int origSize)
	{
		return origSize + (origSize >> 2);
	}
	CHAR* NormalizedQuery()
	{
		CHAR* sf = NULL;
		CHAR* q = NULL;
		if (m_siteFilter)
		{
			sf = m_siteFilter->NormalizedText();
		}
		q = NormalizedText();
		CHAR* result = new CHAR[(sf ? STRLEN(sf) : 0) + (q ? STRLEN(q) : 0) + 2];
		CHAR* pr = result;
		if (sf)
		{
			pr = STPCPY(pr, sf);
			*pr++ = 0x20;
			delete sf;
		}
		if (q)
		{
			pr = STPCPY(pr, q);
			delete q;
		}
		*pr = 0;
		return result;
	}
	virtual CHAR *NormalizedText()
	{
		return NULL;
	}
};

class CPositionFilter : public CSearchExpression
{
public:
	ULONG m_mask;
	CPtr<CSearchExpression> m_base;

public:
	CPositionFilter(CSearchExpression* expr, ULONG mask, CSQLDatabase* database)
	{
		m_base = expr;
		m_mask = mask;
		m_database = database;
	}
	virtual ULONG InitValue() {return m_base->InitValue();};
	virtual CResult* GetSites(int real) {return m_base->GetSites(real);};
	virtual void PrintResults(int indent) {m_base->PrintResults();};
	virtual void MinMax(ULONG* minmax, ULONG* minmax1) {m_base->MinMax(minmax, minmax1);};
	virtual void CombineSite(ULONG* res, ULONG src) {m_base->CombineSite(res, src);};
	virtual ULONG* ComputeSizes(CResult* result, int real) {return m_base->ComputeSizes(result, real);};
	virtual void ReorganizeResults(CResult* res, int real) {m_base->ReorganizeResults(res, real);};
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask)
	{
		return m_base->GetResultForSite(site, p_buf, real, m_mask & mask);
	};
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real)
	{
		return m_base->GetResultForSite(site, p_buf, real, m_mask);
	}
	virtual int Complex() {return 1;};
	virtual int MaxSize() {return m_base->MaxSize();};
	virtual void SetColorOrder(ULONG& order)
	{
		m_base->SetColorOrder(order);
	}
	/*	Match must return first word in sequence if expression matches given word, or NULL otherwise	*/
	virtual CMatchWord* Match(CMatchWord* mword, ULONG& how) {return m_base->Match(mword, how);};
	virtual CMatchWord* Match(ULONG& color, CMatchWord* mword)
	{
		return m_base->Match(color, mword);
	}
#ifdef UNICODE
	virtual WORD *NormalizedText()
	{
		WORD *buf = NULL;
		WORD *p = m_base->NormalizedText();
		if (p)
		{
			int len = strwlen(p);
			buf = new WORD[len + 2];
			strwcpy(buf, p);
			buf[len] = m_mask;
			buf[len + 1] = 0;
			delete p;
		}
		return buf;
	}
#else
	virtual char *NormalizedText()
	{
		char *buf = NULL;
		char *p = m_base->NormalizedText();
		if (p)
		{
			int len = strlen(p);
			buf = new char[len + 10];
			strcpy(buf, p);
			sprintf(buf + len, "%lu", m_mask);
			delete p;
		}
		return buf;
	}
#endif
};

/// Class instantiated when stopword is found
class CStopWord : public CSearchExpression
{
	virtual CResult* GetSites(int real) {return NULL;};
	virtual int IsStop() {return 1;};
	virtual void PrintResults(int indent)
	{
		printf("Couldn't exclude stop word(s)\n");
	}
	virtual ULONG* ComputeSizes(CResult* result, int real) {return NULL;};
	virtual void ReorganizeResults(CResult* res, int real) {};
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask) {return 0;};
};

/// Class instantiated when word or pattern is not found in database
class CEmpty : public CSearchExpression
{
public:
	CEmpty(CSQLDatabase* database)
	{
		m_database = database;
	}
	virtual int IsEmpty() {return 1;}
	virtual CResult* GetSites(int real) {return NULL;};
	virtual void PrintResults(int indent)
	{
		printf("Empty\n");
	}
	virtual ULONG* ComputeSizes(CResult* result, int real) {return NULL;};
	virtual void ReorganizeResults(CResult* res, int real) {};
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask) {return 0;};
};


/**
 * Class used as parameter to STL sort function when search by pattern is performed.
 * Array of CSite1 is instantiated for every site of every word matching
 * search pattern. This array is sorted to group information about
 * different words matching pattern by sites.
 */
class CSite1
{
public:
	CSiteInfo* m_sinfo;	///< site info pointer
	CResult* m_result;	///< pointer to result of particular word matching pattern

public:
	int operator<(const CSite1& site1) const
	{
		return m_sinfo->m_site < site1.m_sinfo->m_site;
	}
};

/// Class used in expressions with minus sign
class CNot : public CSearchExpression
{
public:
	CPtr<CSearchExpression> m_base;		///< Base expression to be negated

public:
	CNot(CSearchExpression* base)
	{
		m_base = base;
	}
	virtual ~CNot()
	{
		if (m_sresult[0])
		{
			m_sresult[0]->Zero();
		}
		if (m_sresult[1])
		{
			m_sresult[1]->Zero();
		}
	}
	virtual CResult* GetSites(int real);
	virtual ULONG* ComputeSizes(CResult* result, int real);
	virtual void ReorganizeResults(CResult* res, int real);
	virtual int IsNot() {return 1;};
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask);
#ifdef UNICODE
	virtual WORD *NormalizedText()
	{
		WORD *buf = NULL;
		WORD *p = m_base->NormalizedText();
		if (p)
		{
			int len = strwlen(p);
			buf = new WORD[len + 2];
			strwcpy(buf, p);
			memcpy((char*)(buf + len), "-\0\0\0", 4);
			delete p;
		}
		return buf;
	}
#else
	virtual char *NormalizedText()
	{
		char *buf = NULL;
		char *p = m_base->NormalizedText();
		if (p)
		{
			int len = strlen(p);
			buf = new char[len + 2];
			strcpy(buf, p);
			memcpy(buf + len, "-\0", 2);
			delete p;
		}
		return buf;
	}
#endif
};

/// Class is used as base class for CWord1, CPattern and CLinkTo
class CSearchExprWithFilter: public CSearchExpression
{
public:
	CResultUrlFilter* m_filter;	///< Pointer to URL filter
public:
//	CSearchExprWithFilter(void) : CSearchExpression() { m_filter = NULL; }
	CSearchExprWithFilter(CResultUrlFilter* filter) : CSearchExpression() { m_filter = filter; }
//	void SetFilter(CResultUrlFilter* filter) { m_filter = filter; }
};

/// Class used for searching one pattern
class CPattern : public CSearchExprWithFilter
{
public:
	ULONG m_wordCount[2];	///< Count of words matching pattern in main ([0]) and "real-time" ([1]) databases
	ULONG m_wc;		///< Current value of m_wordCount
	double m_readTime, m_checkTime, m_combineTime;	///< time values used for profiling
#ifdef UNICODE
	WORD m_sqlPattern[MAX_WORD_LEN + 1];
#else
	string m_sqlPattern;	///< SQL pattern built from query pattern
#endif
	string m_pattern;	///< Query pattern
	CResult** m_results;	///< Array of results for each word matching pattern
	CSite1* m_sites1;	///< Auxiliary array of site-word info, sorted by site IDs
	ULONG m_count1;		///< Size of m_sites1 array
	ULONG m_psite;		///< Counter in m_sites1 array
	vector<ULONG>* m_wordIDs;

public:
	void PrintResults(int indent);
#ifdef UNICODE
	CPattern(const WORD* p_sqlPattern, const string& p_pattern, ULONG wordCount, ULONG wordCount1, CSQLDatabase* database, CResultUrlFilter* filter)
: CSearchExprWithFilter(filter)
	{
		memcpy(m_sqlPattern, p_sqlPattern, sizeof(m_sqlPattern));
#else
	CPattern(const string& p_sqlPattern, const string& p_pattern, ULONG wordCount, ULONG wordCount1, CSQLDatabase* database, CResultUrlFilter* filter)
: CSearchExprWithFilter(filter)
	{
		m_sqlPattern = p_sqlPattern;
#endif
		m_wordCount[0] = wordCount;
		m_wordCount[1] = wordCount1;
		m_pattern = p_pattern;
		m_database = database;
		m_sites1 = NULL;
		m_count1 = 0;
		m_wc = 0;
		m_results = NULL;
		m_wordIDs = NULL;
	}
	void FreeResults()
	{
		if (m_results)
		{
			for (ULONG i = 0; i < m_wc; i++)
			{
				if (m_results[i]) delete m_results[i];
			}
			delete m_results;
			m_results = NULL;
		}
	}
	virtual ~CPattern()
	{
		if (m_sites1) delete m_sites1;
		if (m_wordIDs) delete m_wordIDs;
		FreeResults();
	}
	CResult* GetSites(int real);							/*	Prepares m_results and m_sites arrays	*/
	virtual void MinMax(ULONG* minmax, ULONG* minmax1);
	virtual ULONG* ComputeSizes(CResult* result, int real);
	virtual void CombineSite(ULONG* res, ULONG src)
	{
		*res |= src;
	}
	virtual void ReorganizeResults(CResult* res, int real);
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask);
	virtual int Complex() {return 0;};
	virtual CMatchWord* Match(CMatchWord* mword, ULONG& how);
#ifdef UNICODE
	virtual WORD *NormalizedText()
	{
		WORD *buf = new WORD[MAX_WORD_LEN + 1];
		memcpy(buf, m_sqlPattern, sizeof(m_sqlPattern));
#else
	virtual char *NormalizedText()
	{
		char *buf = new char[m_sqlPattern.size() + 1];
		strcpy(buf, m_sqlPattern.c_str());
#endif
		return buf;
	}
};


/// Class used for searching one word
class CWord1 : public CSearchExprWithFilter
{
public:
	ULONG m_word_id;		///< Word ID in database
	double m_readTime, m_checkTime;	///< Times used for profiling
	string m_word;			///< Search word
#ifdef UNICODE
	WORD m_uword[MAX_WORD_LEN + 1];
#endif

public:
	CWord1(ULONG word_id, CResultUrlFilter* filter)
: CSearchExprWithFilter(filter)
	{
		m_word_id = word_id;
	}
	virtual CResult* GetSites(int real);
	virtual void PrintResults(int indent);
	virtual ULONG* ComputeSizes(CResult* result, int real);
	virtual void ReorganizeResults(CResult* res, int real);
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask);
	virtual int Complex() {return 0;};

	virtual CMatchWord* Match(CMatchWord* mword, ULONG& how);
#ifdef UNICODE
	virtual WORD *NormalizedText()
	{
		WORD *buf = new WORD[MAX_WORD_LEN + 1];
		memcpy(buf, m_uword, sizeof(m_uword));
#else
	virtual char *NormalizedText()
	{
		char *buf = new char[m_word.size() + 1];
		strcpy(buf, m_word.c_str());
#endif
		return buf;
	}
};

/// FIXME!
class CLinkTo : public CSearchExprWithFilter
{
public:
	ULONG m_urlID;

public:
	CLinkTo(CResultUrlFilter* filter): CSearchExprWithFilter(filter) {}
	virtual CResult* GetSites(int real);
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask);
	void ReorganizeResults(CResult* res, int real);
	ULONG* ComputeSizes(CResult* result, int real);
	virtual int GetResultSize(int origSize)
	{
		return origSize << 1;
	}
};


/// Base class for searching several subexpressions
class CMultiSearch : public CSearchExpression
{
public:
	double m_combineTime;			///< Time used for profiling
	vector<CSearchExpression*> m_words;	///< Array of subexpressions, stop words are not included

public:
	virtual ~CMultiSearch()
	{
		for (vector<CSearchExpression*>::iterator ent = m_words.begin(); ent != m_words.end(); ent++)
		{
			(*ent)->Release();
		}
	}
	virtual CResult* GetSites(int real);
	virtual void IterateMaxSize(int& maxsize, int size) = 0;	///< Compute new value of maximum URL info size
	virtual void PrintResults(int indent);
	virtual char* GetName() = 0;
	virtual ULONG* ComputeSizes(CResult* result, int real);
	virtual void CombineSizes(ULONG* res, ULONG src) = 0;		///< Compute new value of maximum URL info size
	virtual void ReorganizeResults(CResult* res, int real);
	virtual int MaxSize();
	virtual void SetColorOrder(ULONG& order);			///< Set and increment order for all subexpressions
	/// Returns URLs information for particular site for each subexprssion. Called from derived classes
	int GetResultsForSite(ULONG site, BYTE** buffers, ULONG* sizes, ULONG** urls, int& maxsize, int* counts, int real, ULONG mask);
	virtual ULONG GetMask(ULONG mask, int index) {return mask;};
	virtual CHAR *NormalizedText()
	{
		return NULL;
	}
};

/**
 * Class used for searching AND expression.
 * Instantiated when list of subexpressions is encountered in query
 */
class CAndWords : public CMultiSearch
{
public:
	/**
	 * Array of distances between word and next word for each word.
	 * Set to 0 for last word.
	 * Minimal value is 1, takes into account stop words detected
	 * between normal words
	 * Used in phrase matching algorithm
	 */
	vector<WORD> m_dist;

public:
	virtual char* GetName() {return "AND";};
	virtual void MinMax(ULONG* minmax, ULONG* minmax1);
	virtual ULONG InitValue() {return 0xFFFFFFFF;}
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask);
	virtual void CombineSite(ULONG* res, ULONG src)
	{
		*res &= src;
	}
	virtual void IterateMaxSize(int& maxsize, int size)
	{
		if (size > maxsize) maxsize = size;
	}
	virtual void CombineSizes(ULONG* res, ULONG src)
	{
		if (src < *res) *res = src;
	}
	virtual CMatchWord* Match(CMatchWord* mword, ULONG& how);
	virtual CMatchWord* Match(ULONG& color, CMatchWord* mword);
#ifdef UNICODE
	virtual WORD *NormalizedText()
	{
		int cnt = 0;
		CChildVector childs;
		int tlen = 0;
		for (vector<CSearchExpression*>::iterator ent = m_words.begin(); ent != m_words.end(); ent++)
		{
			WORD *p = (*ent)->NormalizedText();
			if (p == NULL)
			{
				return NULL;
			}
			childs.push_back(p);
			tlen += strwlen(p);
		}
		WORD* buf = new WORD[tlen + m_words.size() * 3 + 3];
		WORD* pbuf = buf;
		for (CChildVector::iterator child = childs.begin(); child != childs.end(); child++)
		{
			pbuf = stpwcpy(pbuf, *child);
			memcpy(pbuf, cnt ? " a  " : "(a  ", 4);
			pbuf += 2;
			cnt++;
		}
		if (cnt)
		{
			memcpy(pbuf - 1, ") ", 2);
		}
		*pbuf = 0;
		return buf;
	}
#else
	virtual char *NormalizedText()
	{
		int len = 0, cnt = 0;
		char *buf = new char[MAX_WORD_LEN * m_words.size() + m_words.size() * 3 + 3];
		buf[0] = 0;
		for (CSearchExpression** ent = m_words.begin(); ent != m_words.end(); ent++)
		{
			char *p = (*ent)->NormalizedText();
			if (p == NULL)
			{
				delete buf;
				return NULL;
			}
			strcpy(buf + len, p);
			len += strlen(buf + len);
			memcpy((char*)(buf + len), cnt ? " a " : "(a ", 3);
			len += 3;
			cnt ++;
			delete p;
		}
		if (cnt)
		{
			memcpy((char*)(buf + len - 3), ")\0", 2);
		}
		return buf;
	}
#endif
};

/**
 * Class used for searching OR expression.
 * Instantiated when list of subexpressions
 * delimited by "|" or "OR" keyword is encountered in query.
 */
class COrWords : public CMultiSearch
{
public:
	virtual char* GetName() {return "OR";};
	virtual void MinMax(ULONG* minmax, ULONG* minmax1);
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask);
	virtual void CombineSite(ULONG* res, ULONG src)
	{
		*res |= src;
	}
	virtual void IterateMaxSize(int& maxsize, int size)
	{
		maxsize += size;
	}
	virtual void CombineSizes(ULONG* res, ULONG src)
	{
		*res += src;
	}
	virtual CMatchWord* Match(CMatchWord* mword, ULONG& how);
	CMatchWord* Match(ULONG& color, CMatchWord* mword);
	virtual ULONG MatchMask(int index) {return 0x80000000;};
#ifdef UNICODE
	virtual WORD *NormalizedText()
	{
		hash_set<CUWord> oruw;
		int len = 0, cnt = 0;
		for (vector<CSearchExpression*>::iterator ent = m_words.begin(); ent != m_words.end(); ent++)
		{
			WORD *p = (*ent)->NormalizedText();
			if (p == NULL)
			{
				return NULL;
			}
			CUWord uw(p);
			oruw.insert(uw);
			delete p;
		}
		WORD *buf = new WORD[MAX_WORD_LEN * m_words.size() + m_words.size() * 3 + 3];
		for(hash_set<CUWord>::iterator it = oruw.begin(); it != oruw.end(); it++)
		{
			strwcpy(buf + len, it->Word());
			len += strwlen(buf + len);
			memcpy((char*)(buf + len), cnt ? " o  " : "(o  ", 4);
			len += 2;
			cnt ++;
		}
		if (cnt)
		{
			memcpy((char*)(buf + len - 2), ") \0\0", 4);
		}
		return buf;
	}
#else
	virtual char *NormalizedText()
	{
		set<string> orw;
		int len = 0, cnt = 0;;
		for (CSearchExpression** ent = m_words.begin(); ent != m_words.end(); ent++)
		{
			char *p = (*ent)->NormalizedText();
			if (p == NULL)
			{
				return NULL;
			}
			orw.insert(p);
			delete p;
		}
		char *buf = new char[MAX_WORD_LEN * m_words.size() + m_words.size() * 3 + 3];
		for(set<string>::iterator it = orw.begin(); it != orw.end(); it++)
		{
			strcpy(buf + len, it->c_str());
			len += strlen(buf + len);
			memcpy(buf + len, cnt ? " o " : "(o ", 3);
			len += 3;
			cnt ++;
		}
		if (cnt)
		{
			memcpy(buf + len - 3, ")\0", 2);
		}
		return buf;
	}
#endif

};

/**
 * Class used for searching word forms.
 * Instantiated when more than one form of word is found in the database
 * Example: if we search 'create' and both word 'create' or 'created' is found in the "wordurl",
 * then this class is instantiated and member m_exactForm is set so, that m_words[m_exactForm] is CWord1(create)
 */

class COrWordsF : public COrWords
{
public:
	int m_exactForm;	//	Index of exact word forms

public:
	virtual ULONG GetMask(ULONG mask, int index);
	virtual int Complex() {return 0;};
	virtual void SetColorOrder(ULONG& order)
	{
		return CSearchExpression::SetColorOrder(order);
	}
	virtual CMatchWord* Match(ULONG& color, CMatchWord* mword)
	{
		return CSearchExpression::Match(color, mword);
	}
	virtual ULONG MatchMask(int index);
};

/**
 * Class used for searching phrase.
 * Instantiated where double-quoted prhase is encountered in query.
 */
class CPhrase : public CMultiSearch
{
public:
	/**
	 * Array of subexpression positions for each subexpression,
	 * relative to first.
	 * Takes into account stop words found between normal words.
	 * Size of array is size of subexpressions array + 1
	 */
	vector<WORD> m_positions;

public:
	virtual char* GetName() {return "Phrase";};
	virtual void MinMax(ULONG* minmax, ULONG* minmax1);
	virtual ULONG InitValue() {return 0xFFFFFFFF;}
	virtual ULONG GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask);
	virtual void CombineSite(ULONG* res, ULONG src)
	{
		*res &= src;
	}
	virtual void IterateMaxSize(int& maxsize, int size)
	{
		if (size > maxsize) maxsize = size;
	}
	virtual void CombineSizes(ULONG* res, ULONG src)
	{
		if (src < *res) *res = src;
	}
	virtual int MaxSize() {return m_words.size();}
	virtual CMatchWord* Match(CMatchWord* mword, ULONG& how);
	virtual void SetColorOrder(ULONG& order)
	{
		if (m_order == 0)
		{
			m_order = order++;
		}
	}
#ifdef UNICODE
	virtual WORD *NormalizedText()
	{
		int cnt = 0;

		CChildVector childs;
		int tlen = 0;
		for (vector<CSearchExpression*>::iterator ent = m_words.begin(); ent != m_words.end(); ent++)
		{
			WORD *p = (*ent)->NormalizedText();
			if (p == NULL)
			{
				return NULL;
			}
			childs.push_back(p);
			tlen += strwlen(p);
		}
		WORD* buf = new WORD[tlen + m_words.size() * 3 + 3];
		WORD* pbuf = buf;
		for (CChildVector::iterator child = childs.begin(); child != childs.end(); child++)
		{
			pbuf = stpwcpy(pbuf, *child);
			memcpy(pbuf, cnt ? " a  " : "\"a  ", 4);
			pbuf += 2;
			cnt++;
		}
		if (cnt)
		{
			memcpy(pbuf - 1, "\" ", 2);
		}
		*pbuf = 0;
		return buf;
	}
#else
	virtual char *NormalizedText()
	{
		int len = 0, cnt = 0;
		char *buf = new char[MAX_WORD_LEN * m_words.size() + m_words.size() * 3 + 3];
		buf[0] = 0;
		for (CSearchExpression** ent = m_words.begin(); ent != m_words.end(); ent++)
		{
			char *p = (*ent)->NormalizedText();
			if (p == NULL)
			{
				return NULL;
			}
			strcpy(buf + len, p);
			len += strlen(buf + len);
			memcpy((char*)(buf + len), cnt ? " a " : "\"a ", 3);
			len += 3;
			cnt ++;
			delete p;
		}
		if (cnt)
		{
			memcpy((char*)(buf + len - 3), "\"\0", 2);
		}
		return buf;
	}
#endif
};

/**
 * Instances of this class holds PageRanks for each URL in database.
 * Instantiated statically in variable ranks
 */
class CRanks : public vector<WORD>
{
public:
	/**
	 * version number, read from ranks file, value in file is updated
	 * by indexer after each ranks calculation, checked before each query.
	 */
	ULONG m_version;
	int m_file;		///< File of PageRanks
	pthread_mutex_t m_mutex;

public:
	CRanks()
	{
		pthread_mutex_init(&m_mutex, NULL);
		m_file = -1;
		m_version = 0;
	}
	~CRanks()
	{
		pthread_mutex_destroy(&m_mutex);
		if (m_file != -1)
		{
			close(m_file);
		}
	}
	void LoadRanks();		///< Loads PageRanks from rank file
	void LoadRanks(int f);		///< Loads ranks from PageRank file
	/** Reloads ranks if file was changed, returns
	 *  1 if reloaded
	 * -1 on error
	 *  0 if not changed
	 */
	int ReLoad();
};

void GetUrls(CResult** res, ULONG& nFirst, ULONG& totalResCount, CUrlW*& p_urls, int gr, int sortBy);
int TotalSiteCount(CResult** res);

extern CRanks ranks;

#endif
