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

/*  $Id: search.cpp,v 1.61 2002/06/15 12:55:04 al Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CStackElem, CSearchExpression, CWord1, CPattern,
	CMultiSearch, CAndWords, COrWords, CPhrase, CStopWord, CEmpty, CNot,
	CSiteFilter, CWordSiteFilter, CMultiSiteFilter, CAndSiteFilter,
	COrSiteFilter, CResult, CSiteInfo, CSites, CUrlWeight, CSite1, CRanks.

*/


#include <stdio.h>
#include <algorithm>
#include <sys/time.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "search.h"
#include "sqldb.h"
#include "charsets.h"
#include "documents.h"
#include "qparser.h"
#include "excerpts.h"
#include "daemon.h"
#include "lastmod.h"
#include "urlfilter.h"
#include "sock.h"
#include "cache.h"

using namespace std;

extern CLastMods last_mods;
extern bool use_lastmod;
extern CCache cache;

// Format of buffer with URL info with weights
// Size (bytes)			Description
// 4				URL ID
// 2				Weight
// 2				Position count
// 2*position count		Array of positions (sorted)
// ---Next URL info for URL with ID greater than previous-----------

// Format of word position in positions array
// Bits		Description
// 0-13		Absolute position, relative to the beginning of HTML section
// 14-15	HTML section, values:
//		3-title, 2-keywords, 1-description, 0-body or for plain text

CRanks ranks;	// Holds PageRank array

/** This method splits site pattern stored in m_site member into domain names
 * of different levels, finds array of sites stored in field wordsite.sites
 * for each domain name and builds final site bitmap. Highest byte of array
 * element is bitmap of positions of domains in site name, highest bit is for
 * domain of first level, lowest 3 bytes is site ID.
 */
void CWordSiteFilter::GetMap1()
{
	char wrd[33];
	int pos = 0;
	const char* host = m_site.c_str();
	map<int, CSites> sites;
	map<int, CSites>::iterator sit;
	ULONG minSite = 0;
	ULONG maxSite = ~0;
	m_maxSite = 0;
	// Process each domain in pattern
	while (host)
	{
		ULONG len;
		const char* dot = strchr(host, '.');
		len = dot == NULL ? strlen(host) : dot - host;
		if ((len < sizeof(wrd)) && (len > 0)) // Ignore domain names longer than 32 symbols
		{
			ULONG* wsites = NULL;
			ULONG siteLen;
			memcpy(wrd, host, len);
			wrd[len] = 0;
			m_database->GetSites(wrd, wsites, siteLen);
			if ((wsites == NULL) || (siteLen == 0)) return;	// No sites found for domain
			CSites& sitesw = sites[pos];
			sitesw.m_sites = wsites;
			sitesw.m_size = siteLen;
			if (minSite < (wsites[0] & 0xFFFFFF)) minSite = wsites[0] & 0xFFFFFF;
			if (maxSite > (wsites[siteLen - 1] & 0xFFFFFF) + 1) maxSite = (wsites[siteLen - 1] & 0xFFFFFF) + 1;
			if (maxSite <= minSite) return;	//No sites found for part of pattern or whole pattern
		}
		pos++;
		host = dot ? host = dot + 1 : NULL;
	}
	if (sites.size() == 0) return; // No sites found for pattern
	// Now build array of 1-byte bitmaps for each site
	BYTE* positions = (BYTE*)alloca(maxSite - minSite);
	// First initialize by 1
	memset(positions, 0xFF, maxSite - minSite);
	// Process each domain name
	for (sit = sites.begin(); sit != sites.end(); sit++)
	{
		ULONG* psite;
		ULONG* esite = sit->second.m_sites + sit->second.m_size;
		int pr = 0;
		int sh = 24 + sit->first;
		// Process each site for current domain
		for (psite = sit->second.m_sites; psite < esite; psite++)
		{
			ULONG site = *psite & 0xFFFFFF;
			if ((site >= minSite) && (site < maxSite))
			{
				int ind = site - minSite;
				// zero space after previous site
				memset(positions + pr, 0, ind - pr);	
				positions[ind] &= (*psite >> sh);
				pr = ind + 1;
			}
		}
		// zero space after last site
		memset(positions + pr, 0, maxSite - minSite - pr);
	}
	// Now build final bitmap
	m_minSite = minSite >> 5;
	m_maxSite = ((maxSite - 1) >> 5) + 1;
	m_siteMap = new ULONG[m_maxSite - m_minSite];
	memset(m_siteMap, 0, (m_maxSite - m_minSite) << 2);
	for (sit = sites.begin(); sit != sites.end(); sit++)
	{
		ULONG* psite;
		ULONG* esite = sit->second.m_sites + sit->second.m_size;
		for (psite = sit->second.m_sites; psite < esite; psite++)
		{
			ULONG site = *psite & 0xFFFFFF;
			if ((site >= minSite) && (site < maxSite))
			{
				if (positions[site - minSite])
				{
					m_siteMap[(site >> 5) - m_minSite] |= (1 << (site & 0x1F));
				}
			}
		}
	}
}

/// This method filters site array defined by parameters
/// by zeroing bits not matching site pattern
void CSiteFilter::FilterSites(ULONG* sites, ULONG minsite, ULONG maxsite)
{
	GetMap();	// Build site map first
	if (m_siteMap)
	{
		ULONG mins, maxs;
		mins = max(minsite, m_minSite);
		maxs = min(maxsite, m_maxSite);
		ULONG x = m_ie ? ~0 : 0;
		if (m_ie == 0)
		{
			// When include only, zero head and tail of bitmap if necessary
			if (m_minSite > minsite)
			{
				memset(sites, 0, (min(m_minSite, maxsite) - minsite) << 2);
			}
			if (m_maxSite < maxsite)
			{
				memset(sites + max(m_maxSite, minsite) - minsite, 0, (maxsite - max(m_maxSite, minsite)) << 2);
			}
		}
		if (maxs > mins)
		{
			// Process all elements of bitmap
			for (ULONG i = mins; i < maxs; i++)
			{
				sites[i - minsite] &= (m_siteMap[i - m_minSite] ^ x);
			}
		}
	}
	else
	{
		// Zero everything if "include only" and pattern not found
		if (m_ie == 0)
		{
			memset(sites, 0, (maxsite - minsite) << 2);
		}
	}
}

void COrSiteFilter::GetMap1()
{
	ULONG mins = ~0, maxs = 0;
	// First, build site bitmap for each subexpression
	for (vector<CSiteFilter*>::iterator f = m_filters.begin(); f != m_filters.end(); f++)
	{
		(*f)->GetMap();
		if ((*f)->m_minSite < mins) mins = (*f)->m_minSite;
		if ((*f)->m_maxSite > maxs) maxs = (*f)->m_maxSite;
	}
	m_minSite = mins;
	m_maxSite = maxs;
	// Allocate and intialize site bitmap with zeroes
	m_siteMap = new ULONG[maxs - mins];
	memset(m_siteMap, 0, (maxs - mins) << 2);
	// Combine all bitmaps by OR
	for (vector<CSiteFilter*>::iterator f = m_filters.begin(); f != m_filters.end(); f++)
	{
		ULONG sto = (*f)->m_minSite - mins;
		ULONG sz = (*f)->m_maxSite - (*f)->m_minSite;
		for (ULONG i = 0; i < sz; i++, sto++)
		{
			m_siteMap[sto] |= (*f)->m_siteMap[i];
		}
	}
}

void CAndSiteFilter::GetMap1()
{
	ULONG minExclude = ~0;
	ULONG maxExclude = 0;
	// First, build site bitmap for each subexpression
	for (vector<CSiteFilter*>::iterator f = m_filters.begin(); f != m_filters.end(); f++)
	{
		(*f)->GetMap();
		if ((*f)->m_ie == 0)
		{
			if ((*f)->m_minSite > m_minSite) m_minSite = (*f)->m_minSite;
			if ((*f)->m_maxSite < m_maxSite) m_maxSite = (*f)->m_maxSite;
			if (m_minSite >= m_maxSite) return;		// "Include only", no sites found matching several expressions
		}
		else
		{
			if ((*f)->m_minSite < minExclude) minExclude = (*f)->m_minSite;
			if ((*f)->m_maxSite > maxExclude) maxExclude = (*f)->m_maxSite;
		}
	}
	m_ie = m_maxSite == (ULONG)~0 ? 1 : 0;	// Type of this filter is "exclude" if all subexpressions have type "exclude"
	ULONG mins = m_ie ? minExclude : m_minSite;
	ULONG sz = (m_ie ? maxExclude : m_maxSite) - mins;
	if (m_ie)
	{
		m_minSite = minExclude;
		m_maxSite = maxExclude;
	}
	// Allocate and intialize site bitmap with zeroes or ones depending on type
	m_siteMap = new ULONG[sz];
	memset(m_siteMap, m_ie ? 0 : 0xFF, sz << 2);
	// Process all subexpressions
	for (vector<CSiteFilter*>::iterator f = m_filters.begin(); f != m_filters.end(); f++)
	{
		if ((*f)->m_siteMap)
		{
			if ((*f)->m_ie == 0)
			{
				ULONG sfrom = mins - (*f)->m_minSite;
				for (ULONG i = 0; i < sz; i++, sfrom++)
				{
					m_siteMap[i] &= (*f)->m_siteMap[sfrom];
				}
			}
			else
			{
				if (m_ie == 0)
				{
					ULONG sfr = m_minSite > (*f)->m_minSite ? m_minSite - (*f)->m_minSite : 0;
					ULONG sto = m_minSite > (*f)->m_minSite ? 0 : (*f)->m_minSite - m_minSite;
					ULONG sz;
					if (m_minSite > (*f)->m_minSite)
					{
						if (m_minSite >= (*f)->m_maxSite) continue;
						sz = (*f)->m_maxSite - m_minSite;
					}
					else
					{
						if (m_maxSite <= (*f)->m_minSite) continue;
						sz = m_maxSite - (*f)->m_minSite;
					}
					for (ULONG i = 0; i < sz; i++, sfr++, sto++)
					{
						m_siteMap[sto] &= ~((*f)->m_siteMap[sfr]);
					}
				}
				else
				{
					ULONG sto = (*f)->m_minSite - mins;
					ULONG szf = (*f)->m_maxSite - (*f)->m_minSite;
					for (ULONG i = 0; i < szf; i++, sto++)
					{
						m_siteMap[sto] |= (*f)->m_siteMap[i];
					}
				}
			}
		}
	}
}

void CWord1::PrintResults(int indent)
{
	printf("%*s%32s", indent, "", m_word.c_str());
	if (m_sresult[0]) printf(" %10li", m_sresult[0]->m_counts[1]);
	printf("\n");
}

CMatchWord* CWord1::Match(CMatchWord* wrd, ULONG& how)
{
	how = ~0;
#ifdef UNICODE
	return strwcmp(wrd->m_word, m_uword) == 0 ? wrd : NULL;
#else
	return strcmp(wrd->m_word, m_word.c_str()) == 0 ? wrd : NULL;
#endif
}

void CPattern::PrintResults(int indent)
{
	printf("%*s%32s", indent, "", m_pattern.c_str());
	if (m_sresult[0]) printf(" %10li  %7.3f", m_sresult[0]->m_counts[1], m_combineTime);
	printf("\n");
}

CMatchWord* CPattern::Match(CMatchWord* mword, ULONG& how)
{
	how = ~0;
#ifdef UNICODE
	return ::SQLMatchW(mword->m_word, mword->Length(), m_sqlPattern) ? mword : NULL;
#else
	return ::Match<char>(mword->m_word, m_pattern.c_str(), '*', '?') ? mword : NULL;
#endif
}

/** This method builds a result with site bitmap from array of results.
 * Parameters: "size" is array size, "res" is array of input results
 * Return CResult object with combited site bitmap
 * Called from CMultiSearch::GetSites and CPattern::GetSites
 */

CResult* CSearchExpression::GetSites(int size, CResult** res)
{
	// First compute minimum and maximum possible site ID for result
	int i;
	ULONG* minmax = (ULONG*)alloca(size * sizeof(ULONG[2]));
	ULONG minmaxr[2];
	res[0]->MinMaxSite(minmax);
	minmaxr[0] = minmax[0];
	minmaxr[1] = minmax[1];
	for (i = 1; i < size; i++)
	{
		if (res[i]->m_not == 0)
		{
			res[i]->MinMaxSite(minmax + (i << 1));
			MinMax(minmaxr, minmax + (i << 1));					// virtual call
			if (minmaxr[1] < minmaxr[0]) return new CResult;	// No sites found, return empty CResult
		}
	}
	// Now allocate CResult and its site bitmap
	CResult* resl = new CResult;
	resl->m_setSize = minmaxr[1] - minmaxr[0];
	resl->m_siteSet = new ULONG[resl->m_setSize];
	resl->m_minSite = minmaxr[0];
	// Initialize bitmap, InitValue() is virtual
	memset(resl->m_siteSet, InitValue(), resl->m_setSize << 2);
	// Process each input result
	for (i = 0; i < size; i++)
	{
		if (res[i]->m_not == 0)		// Ignore "-" prefixed expressions
		{
			ULONG minf, mint, maxf;
			if (minmaxr[0] > minmax[i << 1])
			{
				minf = minmaxr[0] - minmax[i << 1];
				mint = 0;
			}
			else
			{
				mint = minmax[i << 1] - minmaxr[0];
				minf = 0;
			}
			maxf = min(minmaxr[1], minmax[(i << 1) + 1]);
			maxf = maxf >= minmax[i << 1] ? maxf - minmax[i << 1] : 0;
			ULONG* resSet = resl->m_siteSet + mint;
			ULONG* srcSet = res[i]->m_siteSet;
			ULONG m, t;
			for (t = 0, m = minf; m < maxf; m++, t++)
			{
				CombineSite(resSet + t, srcSet[m]);	// virtual call
			}
		}
	}
	return resl;
}

void CPattern::MinMax(ULONG* minmax, ULONG* minmax1)
{
	if (minmax1[0] < minmax[0]) minmax[0] = minmax1[0];
	if (minmax1[1] > minmax[1]) minmax[1] = minmax1[1];
}

// This struct is used in phrase matching algorithm
// and combining sorted arrays into one by OR
typedef struct
{
	WORD* start;	// Start of array
	WORD* end;		// End of array
	int sph;
	// sph is:
	//  3 for found phrase
	//  2 for found 2 or more subphrases
	//  1 for found 1 subphrase
	//  0 when no subphrases found
	ULONG dist;		// distance to the next word,
					// takes into account stopwords, following this word
} PosRange;

WORD* FillSort(PosRange* pos, int size, WORD* pbuf, WORD maxpos)
{
	if (size == 1)
	{
		while ((pos->start < pos->end) && (*pos->start <= maxpos))
		{
			if (*pos->start < maxpos) *pbuf++ = *pos->start;
			pos->start++;
		}
	}
	else
	{
		while ((pos->start < pos->end) && (*pos->start <= maxpos))
		{
			if (*pos->start < maxpos)
			{
				pbuf = FillSort(pos + 1, size - 1, pbuf, *pos->start);
				*pbuf++ = *pos->start;
			}
			pos->start++;
		}
		pbuf = FillSort(pos + 1, size - 1, pbuf, maxpos);
	}
	return pbuf;
}

// This function takes array of sorted arrays and builds sorted array
// including all unique elements of input arrays
// Parameters: "pos" is array of input arrays, "size" is array count,
// "pbuf" is result buffer
// Returns pointer to the end of resulting buffer
// Recursively calls itself and above version of FillSort
WORD* FillSort(PosRange* pos, int size, WORD* pbuf)
{
	if (size == 1)
	{
		while (pos->start < pos->end)
		{
			*pbuf++ = *pos->start++;
		}
	}
	else
	{
		while (pos->start < pos->end)
		{
			pbuf = FillSort(pos + 1, size - 1, pbuf, *pos->start);
			*pbuf++ = *pos->start++;
		}
		pbuf = FillSort(pos + 1, size - 1, pbuf);
	}
	return pbuf;
}

inline void OrCombineWeights(WORD& dest, WORD src)
{
	dest |= (src & 0x8000);
	if ((dest & 0x7FFF) < (src & 0x7FFF)) dest = (src & 0x7FFF) | (dest & 0x8000);
}

// This function takes array of URL infos, combines them by OR
// and produces resulting URL info
// Parameters are:
// "size" is size of array,
// "buffers" is array of input buffers,
// "size" is array of sizes of input buffers,
// "p_buf" is result buffer
// Returns length of result buffer
ULONG GetOrContents(ULONG size, BYTE** buffers, ULONG* sizes, ULONG** urls, BYTE* p_buf)
{
	// First allocate and build array with pointers to buffer ends
	ULONG i;
	WORD* pbuf = (WORD*)p_buf;
	BYTE** ebuffers = (BYTE**)alloca(size * sizeof(BYTE*));
	PosRange* posr = (PosRange*)alloca(size * sizeof(PosRange));
	for (i = 0; i < size; i++)
	{
		ebuffers[i] = buffers[i] ? buffers[i] + sizes[i] : NULL;
	}
	do
	{
		// First find mimimal URL ID
		ULONG i, i1;
		ULONG minurl = 0xFFFFFFFF;
		ULONG** purl = urls;
		for (i = 0; i < size; purl++, i++)
		{
			if ((*purl) && (**purl < minurl))
			{
				minurl = **purl;
				i1 = i;
			}
		}
		if (minurl == 0xFFFFFFFF) break;	// End of all buffers is reached
		*(ULONG*)pbuf = minurl;				// Copy URL ID to the result
		BYTE** ebuf = ebuffers;
		WORD* pwei = pbuf + 2;
		*pwei = 0;
		WORD* psize = pbuf + 3;
		pbuf += 4;
		int cnt = 0;
		PosRange* pposr = posr;
		// Find all buffers containing found URL ID, and compute maximal weight of them
		for (i = 0, purl = urls; i < size; i++, purl++, ebuf++)
		{
			if ((*purl) && (**purl == minurl))
			{
//				WORD wei = (((WORD*)*purl)[2]);
//				if (wei > *pwei) *pwei = wei;
				OrCombineWeights(*pwei, ((WORD*)*purl)[2]);
				int bytes = (((WORD*)*purl)[3]);
				pposr->start = (WORD*)(*purl) + 4;
				pposr->end = pposr->start + bytes;
				pposr++;
				cnt++;
				(WORD*&)*purl += ((WORD*)(*purl))[3] + 4;
				if ((BYTE*)(*purl) >= *ebuf) *purl = 0;
			}
		}
		if (cnt > 1)
		{
			// More than 1 buffer is found, combine them
			WORD* pb = pbuf;
			pbuf = FillSort(posr, cnt, pbuf);
			*psize = pbuf - pb;
		}
		else
		{
			// Only 1 buffer is found, just copy positions array and positions count
			*psize = posr[0].end - posr[0].start;
			memcpy(pbuf, posr[0].start, *psize << 1);
			pbuf += *psize;
		}
	}
	while (true);
	return (BYTE*)pbuf - (BYTE*)p_buf;
}

/** This functions does the same as GetOrContents,
 * but using different algorithm of combining positions arrays.
 */
ULONG GetOrContentsP(ULONG size, BYTE** buffers, ULONG* sizes, ULONG** urls, BYTE* p_buf)
{
	ULONG i;
	WORD* pbuf = (WORD*)p_buf;
	BYTE** ebuffers = (BYTE**)alloca(size * sizeof(BYTE*));
	for (i = 0; i < size; i++)
	{
		ebuffers[i] = buffers[i] ? buffers[i] + sizes[i] : NULL;
	}
	do
	{
		ULONG i, i1;
		ULONG minurl = 0xFFFFFFFF;
		ULONG** purl = urls;
		for (i = 0; i < size; purl++, i++)
		{
			if ((*purl) && (**purl < minurl))
			{
				minurl = **purl;
				i1 = i;
			}
		}
		if (minurl == 0xFFFFFFFF) break;
		*(ULONG*)pbuf = minurl;
		BYTE** ebuf = ebuffers;
		WORD* pwei = pbuf + 2;
		*pwei = 0;
		WORD* psize = pbuf + 3;
		pbuf += 4;
		int cnt = 0;
		for (i = 0, purl = urls; i < size; i++, purl++, ebuf++)
		{
			if ((*purl) && (**purl == minurl))
			{
//				WORD wei = (((WORD*)*purl)[2]);
//				if (wei > *pwei) *pwei = wei;
				OrCombineWeights(*pwei, ((WORD*)*purl)[2]);
				int bytes = (((WORD*)*purl)[3]);
				memcpy(pbuf, (WORD*)(*purl) + 4, bytes * sizeof(WORD));
				pbuf += bytes;
				cnt++;
				(WORD*&)*purl += ((WORD*)(*purl))[3] + 4;
				if ((BYTE*)(*purl) >= *ebuf) *purl = 0;
			}
		}
		WORD* pb = psize + 1;
		if (cnt > 1)
		{
			sort<WORD*>(pb, pbuf);
			WORD* pbw = pb;
			WORD p = *pb;
			*pbw++ = p;
			while (++pb < pbuf)
			{
				if (p != *pb)
				{
					p = *pb;
					*pbw++ = p;
				}
			}
			pb = psize + 1;
			*psize = pbw - pb;
			pbuf = pbw;
		}
		else
		{
			*psize = pbuf - pb;
		}
	}
	while (true);
	return (BYTE*)pbuf - (BYTE*)p_buf;
}

/** This function takes array of results, combines them by OR and produces final URL info
 * Parameters are:
 * "start" is the array of input infos,
 * "size" is the size of array,
 * "p_buf" is reference to the result buffer
 * Returns size of output buffer
 */
ULONG GetResultForSite(CSite1* start, int size, BYTE*& p_buf, ULONG mask, CResultUrlFilter* filter)
{
	// First allocate and compute input results
	int i;
	ULONG* sizes = (ULONG*)alloca(size * sizeof(ULONG));
	BYTE** buffers = (BYTE**)alloca(size * sizeof(BYTE*));
	ULONG** urls = (ULONG**)alloca(size * sizeof(ULONG*));
	ULONG tsize = 0;
	for (i = 0; i < size; i++)
	{
		sizes[i] = start[i].m_result->GetResultForSite(start[i].m_sinfo->m_site, buffers[i], mask, filter);
		urls[i] = sizes[i] > 0 ? (ULONG*)buffers[i] : NULL;
		tsize += sizes[i];
	}
	// Combine input results
	p_buf = new BYTE[tsize];
	ULONG res = GetOrContentsP(size, buffers, sizes, urls, p_buf);
	for (i = 0; i < size; i++)
	{
		if (buffers[i]) delete buffers[i];
	}
	return res;
}

ULONG CPattern::GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask)
{
	// First, find first index in array of sites
	// where site ID of element is greater or equal to site
	while ((m_psite < m_count1) && (m_sites1[m_psite].m_sinfo->m_site < site))
	{
		m_psite++;
	}
	CSite1* start = m_sites1 + m_psite;
	// Find last index in array of sites, where site ID of element is equal to site
	while ((m_psite < m_count1) && (m_sites1[m_psite].m_sinfo->m_site == site))
	{
		m_psite++;
	}
	CSite1* end = m_sites1 + m_psite;
	int count = end - start;
	switch (count)
	{
	case 0:
		// No word is found
		p_buf = NULL;
		return 0;
	case 1:
		// Only one word is found, use is as source of result
		return start->m_result->GetResultForSite(site, p_buf, mask, m_filter);
	default:
		// More than one word is found, combine URL information from all of them
		return ::GetResultForSite(start, count, p_buf, mask, m_filter);
	}
	return 0;
};

void CPattern::ReorganizeResults(CResult* res, int real)
{
	// First, build site info array for each word matching pattern
	m_count1 = 0;
	CResult** ress = m_results;
	for (ULONG i = 0; i < m_wc; i++, ress++)
	{
		res->Reorganize(*ress);
		m_count1 += (*ress)->m_infoCount;
	}
	if (m_sites1) delete m_sites1;
	// Copy and sort information for all all sites, where any word matching pattern can be found
	m_sites1 = new CSite1[m_count1];
	CSite1* psite = m_sites1;
	ress = m_results;
	for (ULONG i = 0; i < m_wc; i++, ress++)
	{
		for (ULONG j = 0; j < (*ress)->m_infoCount; j++)
		{
			psite->m_result = *ress;
			psite->m_sinfo = (*ress)->m_siteInfos + j;
			psite++;
		}
	}
	sort(m_sites1, psite);
	m_psite = 0;
}

ULONG* CPattern::ComputeSizes(CResult* result, int real)
{
	if (m_wordCount[real] > 0)
	{
		// Compute array of sizes by sites for each word matching pattern, and compute total size for each site
		ULONG sz = result->m_setSize << 5;
		ULONG* res = result->ComputeSizes(m_results[0]);
		for (ULONG i = 1; i < m_wordCount[real]; i++)
		{
			ULONG* res1 = result->ComputeSizes(m_results[i]);
			for (ULONG j = 0; j < sz; j++)
			{
				res[j] += res1[j];
			}
			delete res1;
		}
		return res;
	}
	else
	{
		return NULL;
	}
}

int Comp100(ULONG x, ULONG y)
{
	int r = (x % 100) - (y % 100);
	return r == 0 ? x < y : r < 0;
}

ULONG GetSites(ULONG word_id, FILE* f, FILE* fs, ULONG*& sites, ULONG* counts)
{
	ULONG off = (word_id / 100) * sizeof(WordInd);
	ULONG numSites = 0;
	if (off > 0) fseek(f, off - sizeof(WordInd), SEEK_SET);
	if ((off == 0) || (ftell(f) == (signed long)(off - sizeof(WordInd))))
	{
		WordInd ui[2];
		int elems;
		if (off > 0)
		{
			elems = fread(ui, sizeof(WordInd), 1, f);
		}
		else
		{
			ui[0].m_offset = 0;
			elems = 1;
		}
		if (elems && fread(ui + 1, sizeof(WordInd), 1, f))
		{
			counts[0] += ui[1].m_totalCount;
			counts[1] += ui[1].m_urlCount;
			numSites = (ui[1].m_offset - ui[0].m_offset) / (sizeof(ULONG) << 1);
			if (numSites > 0)
			{
				sites = new ULONG[(numSites << 1) + 1];
				if (ui[0].m_offset > 0) fseek(fs, ui[0].m_offset - sizeof(ULONG), SEEK_SET);
				if (ui[0].m_offset == 0)
				{
					sites[0] = 0;
				}
				else
				{
					fread(sites, sizeof(ULONG), 1, fs);
				}
				fread(sites + 1, sizeof(ULONG) << 1, numSites, fs);
			}
		}
	}
	return numSites;
}

void GetPatternSites(vector<ULONG>* wordIDs, ULONG** sites, WORD** urls, ULONG* counts, FILE** files, ULONG* siteLens)
{
	ULONG** psites = sites;
	WORD** purls = urls;
	FILE** pfiles = files;
	ULONG* pLen = siteLens;
	counts[0] = counts[1] = 0;
	if (wordIDs && wordIDs->size())
	{
		ULONG* wb = &(*wordIDs->begin());
		ULONG* we = &(*wordIDs->end());
		sort(wb, we, Comp100);
		while (wb < we)
		{
			ULONG nfile = *wb % 100;
			char ind[1000], sitesn[1000], urlsn[1000];
			sprintf(ind, "%s/%02luw/ind", DBWordDir.c_str(), nfile);
			sprintf(sitesn, "%s/%02luw/sites", DBWordDir.c_str(), nfile);
			sprintf(urlsn, "%s/%02luw/urls", DBWordDir.c_str(), nfile);
			FILE* f = fopen(ind, "r");
			FILE* fs = fopen(sitesn, "r");
			FILE* fu = fopen(urlsn, "r");
			int open = f && fs && fu;
			while ((wb < we) && (nfile == (*wb % 100)))
			{
				*psites = NULL;
				*purls = NULL;
				*pLen = 0;
				*pfiles = NULL;
				if (open)
				{
					ULONG numSites = ::GetSites(*wb, f, fs, *psites, counts);
					if (numSites > 0)
					{
						*pLen = numSites * (sizeof(ULONG) << 1) + sizeof(ULONG);
						ULONG urlLen = (*psites)[numSites << 1] - (*psites)[0];
						if (urlLen < 0x10000)
						{
							*purls = (WORD*)(new char[urlLen]);
							fseek(fu, (*psites)[0], SEEK_SET);
							fread(*purls, 1, urlLen, fu);
						}
						else
						{
							*pfiles = fopen(urlsn, "r");
						}
					}
				}
				wb++;
				psites++;
				purls++;
				pfiles++;
				pLen++;
			}
			if (f) fclose(f);
			if (fs) fclose(fs);
			if (fu) fclose(fu);
		}
	}
}

CResult* CPattern::GetSites(int real)
{
	FreeResults();
	ULONG& wc = m_wordCount[real];
	if (wc > 0)
	{
		// First, build results with sites bitmap for each word matching pattern
		m_results = new CResult*[wc];
		ULONG** sites = (ULONG**)alloca(wc * sizeof(ULONG*));
		WORD** urls = (WORD**)alloca(wc * sizeof(WORD*));
		FILE** files = (FILE**)alloca(wc * sizeof(FILE*));
		ULONG* siteLens = (ULONG*)alloca(wc * sizeof(ULONG));
		ULONG counts[2];
		int compact = CompactStorage && (real == 0);
		int newSize;
		if (compact)
		{
			GetPatternSites(m_wordIDs, sites, urls, counts, files, siteLens);
			newSize = wc;
		}
		else
		{
#ifdef UNICODE
			newSize = m_database->GetPatternSites(m_sqlPattern, wc, sites, urls, counts, files, real);
#else
			newSize = m_database->GetPatternSites(m_sqlPattern.c_str(), wc, sites, urls, counts, files, real);
#endif
		}
		for (int i = 0; i < newSize; i++)
		{
			m_results[i] = new CResult;
			m_results[i]->m_sites = sites[i];
			m_results[i]->m_urls = urls[i];
			m_results[i]->m_source = files[i];
			if (sites[i]) m_results[i]->m_siteLen = compact ? siteLens[i] : sites[i][0];
			m_results[i]->CreateSet();
		}
		// Combine results
		CResult* res = CSearchExpression::GetSites(newSize, m_results);
		res->m_counts[0] = counts[0];
		res->m_counts[1] = counts[1];
		wc = newSize;
		m_wc = newSize;
		return res;
	}
	else
	{
		m_wc = 0;
		return new CResult;
	}
}

void CMultiSearch::SetColorOrder(ULONG& order)
{
	vector<CSearchExpression*>::iterator ent;
	for (ent = m_words.begin(); ent != m_words.end(); ent++)
	{
		(*ent)->SetColorOrder(order);
	}
}

int CMultiSearch::MaxSize()
{
	vector<CSearchExpression*>::iterator ent;
	int max = 0;
	for (ent = m_words.begin(); ent != m_words.end(); ent++)
	{
		int m = (*ent)->MaxSize();
		if (m > max) max = m;
	}
	return max;
}

CMatchWord* COrWords::Match(CMatchWord* mword, ULONG& how)
{
	vector<CSearchExpression*>::iterator ent;
	int m = 0;
	how = 0;
	// Test each subexpression for matching
	for (ent = m_words.begin(); ent != m_words.end(); ent++)
	{
		ULONG how1;
		if ((*ent)->Match(mword, how1))
		{
			// Calculate maximum matching degree
			how |= (how1 & MatchMask(ent - m_words.begin()));
			if ((how1 & 0x3FFFFFFF) > (how & 0x3FFFFFFF)) how = (how1 & 0x7FFFFFFF) | (how & 0x80000000);
			m = 1;
		}
	}
	return m ? mword : NULL;
};

CMatchWord* COrWords::Match(ULONG& color, CMatchWord* mword)
{
	vector<CSearchExpression*>::iterator ent;
	// Test each subexpression for matching
	for (ent = m_words.begin(); ent != m_words.end(); ent++)
	{
		ULONG scolor;
		CMatchWord* swrd;
		if ((swrd = (*ent)->Match(scolor, mword)))
		{
			color = scolor;
			return swrd;
		}
	}
	return NULL;
};

CMatchWord* CAndWords::Match(ULONG& color, CMatchWord* mword)
{
	vector<WORD>::iterator dist = m_dist.end() - 1;
	vector<CSearchExpression*>::iterator ent;
	CMatchWord* wrd = mword;
	CMatchWord* lmwrd = NULL;
	// Test each subexpression for match from end to start
	for (ent = m_words.end() - 1; ent >= m_words.begin(); ent--, dist--)
	{
		// Advandce pointer in the linked list by count equal to distance value
		ULONG scolor;
		CMatchWord* swrd;
		if (wrd != mword)
		{
			for (WORD i = *dist; i > 0; i--)
			{
				wrd = wrd->m_prev;
				if (wrd == NULL) return NULL;
			}
		}
		if ((swrd = (*ent)->Match(scolor, wrd)))
		{
			color = scolor;
			lmwrd = swrd;
		}
		else
		{
			if (lmwrd) return lmwrd;
			wrd = mword;
		}
	}
	return lmwrd;
}

inline void AndCombineDegree(ULONG& dest, ULONG src)
{
	dest &= (src | 0x7FFFFFFF);
	if ((dest & 0x3FFFFFFF) < (src & 0x3FFFFFFF)) dest = (dest & 0x80000000) | (src & 0x7FFFFFFF);
}

CMatchWord* CAndWords::Match(CMatchWord* mword, ULONG& how)
{
	ULONG how1;
	int phl = 0;	//Sub phrase length
	int br = 0;		//Break count
	int mw = 0;		//Match word count
	int pos;
	vector<WORD>::iterator dist = m_dist.end() - 1;
	vector<CSearchExpression*>::iterator ent;
	CMatchWord* wrd = mword;
	CMatchWord* lmwrd = NULL;
	how = 0x80000000;
	// Test each subexpression for match from end to start
	for (ent = m_words.end() - 1, pos = m_words.size() - 1; ent >= m_words.begin(); ent--, pos--, dist--)
	{
		// Advandce pointer in the linked list by count equal to distance value
		for (WORD i = *dist; i > 0; i--)
		{
			wrd = wrd->m_prev;
			if (wrd == NULL) return 0;
		}
		if ((*ent)->Match(wrd, how1))
		{
			lmwrd = wrd;
			mw++;
			if ((how1 & 0x3FFFFFFF) == 0x3FFFFFFF)
			{
				phl++;
			}
			else
			{
//				if (how < how1) how = how1;
				AndCombineDegree(how, how1);
				if (phl > 1)
				{
					how1 = (pos << 14) | (pos + phl);
//					if (how < how1) how = how1;
					AndCombineDegree(how, how1);
				}
				phl = 0;
			}
		}
		else
		{
			// Increase break count
			br++;
			if (phl > 1)
			{
				// Sub phrase found
				how = (pos << 16) | (pos + phl);
			}
			phl = 0;
		}
	}
	if (br && ((how & 0x3FFFFFFF) == 0) && (phl > 1))
	{
		how = (pos << 14) | (pos + phl);
	}
	if ((how & 0x3FFFFFFF) == 0x3FFFFFFF) how--;
	if (br == 0)
	{
		// Phrase match found
		how = (how & 0x80000000) | 0x3FFFFFFE;
	}
	return mw ? lmwrd : NULL;
}

void CMultiSearch::ReorganizeResults(CResult* res, int real)
{
	// Call ReorganizeResults in each subexpresion
	vector<CSearchExpression*>::iterator ent;
	for (ent = m_words.begin(); ent != m_words.end(); ent++)
	{
		(*ent)->ReorganizeResults(res, real);
	}
}

ULONG* CMultiSearch::ComputeSizes(CResult* result, int real)
{
	ULONG sz = result->m_setSize << 5;
	// Compute array of URL info sizes for each site for each subexpression and combine it
	ULONG* res = m_words[0]->ComputeSizes(result, real);
	int i;
	vector<CSearchExpression*>::iterator ent;
	for (i = 1, ent = m_words.begin() + 1; ent != m_words.end(); ent++, i++)
	{
		ULONG* res1 = (*ent)->ComputeSizes(result, real);
		if (res1)
		{
			for (ULONG j = 0; j < sz; j++)
			{
				CombineSizes(res + j, res1[j]);		// virtual call
			}
			delete res1;
		}
	}
	return res;
}

CResult* CMultiSearch::GetSites(int real)
{
	// Get sites for each subexpression and combine it
	CResult** res = (CResult**)alloca(m_words.size() * sizeof(CResult*));
	vector<CSearchExpression*>::iterator ent;
	int i;
	for (i = 0, ent = m_words.begin(); ent != m_words.end(); ent++, i++)
	{
		res[i] = (*ent)->GetSites1(real);
	}
	return CSearchExpression::GetSites(m_words.size(), res);
}

void CMultiSearch::PrintResults(int indent)
{
	for (vector<CSearchExpression*>::iterator ent = m_words.begin(); ent != m_words.end(); ent++)
	{
		(*ent)->PrintResults(indent + 1);
	}
	printf("%*s%32s", indent, "", GetName());
	if (m_sresult[0]) printf(" %10li %7.3f", m_sresult[0]->m_counts[1], m_combineTime);
	printf("\n");
}

// This functions leaves information about those URLs,
// which only found in "surls" array
// Parameters are:
// "buf" is the initial buffer with URL info,
// "size" is size of "buf",
// "surls" is array of filter URL IDs,
// "slen" is the count of URL IDs.
// Returns new length of buffer
ULONG FilterResult(BYTE* buf, int size, ULONG* surls, int slen)
{
	WORD* sbuf = (WORD*)buf;
	WORD* wbuf = (WORD*)buf;
	ULONG* es = surls + slen;
	WORD* ebuf = (WORD*)(buf + size);
	// Scan two buffers
	while ((sbuf < ebuf) && (surls < es))
	{
		if (*(ULONG*)sbuf == *surls)
		{
			// URL ID matches filter, copy info and advance both pointers
			WORD len = sbuf[3] + 4;
			if (wbuf != sbuf)
			{
				memcpy(wbuf, sbuf, len << 1);
			}
			sbuf += len;
			wbuf += len;
			surls++;
		}
		else if (*(ULONG*)sbuf > *surls)
		{
			// Advance only filter pointer
			surls++;
		}
		else
		{
			// Advence only buffer pointer
			sbuf += sbuf[3] + 4;
		}
	}
	return (BYTE*)wbuf - (BYTE*)buf;
}

ULONG CResult::GetFinalResult(CSearchExpression* src, int total, ULONG* surls, int slen, int real, int gr, int sortBy)
{
	BYTE* urls = new BYTE[src->GetResultSize(total)];	// Allocate buffer of maximal possible size
	WORD* urlsn = (WORD*)urls;
	CSiteInfo* siteInfos = new CSiteInfo[m_setSize << 5];	// Allocate site info array of maximal possible size
	CSiteInfo* sinfo = siteInfos;
	ULONG* set;
	ULONG* eset = m_siteSet + m_setSize;
	ULONG baseSite = m_minSite << 5;
	ULONG urlCount = 0;
	// Reload ranks and lastmod if needed
	if (ranks.ReLoad() < 0)
	{
		logger.log(CAT_FILE, L_WARN, "Can't reload ranks file: %s\n", strerror(errno));
	}
	if (use_lastmod)
	{
		if (last_mods.ReLoad() < 0)
			logger.log(CAT_FILE, L_WARN, "Can't reload lastmod file: %s\n", strerror(errno));
	}
	ULONG r = ranks.size();
	ULONG ls = last_mods.size();
	vector<WORD>::iterator rnk = ranks.begin();
	// Process entire site bitmap
	for (set = m_siteSet; set < eset; set++)
	{
		if (*set)
		{
			// Non-zero element found, process all bits
			for (ULONG j = 1; j != 0; j <<= 1)
			{
				if (*set & j)
				{
					// Get URLs info for site
					BYTE* buf = NULL;
					ULONG size = src->GetResultForSite(baseSite, buf, real);
					if (surls)
					{
						// Apply directory filter
						size = FilterResult(buf, size, surls, slen);
					}
					if (size > 0)
					{
						// Find two URLs with maximal weights
						BYTE* maxw = NULL;
						BYTE* maxw2 = NULL;
						sinfo->m_urlcount = 0;
						WORD* pb = (WORD*)buf;
						WORD* eb = (WORD*)(buf + size);
						ULONG maxwt = 0, maxwt2 = 0;
						maxw = buf;
						while (pb < eb)
						{
							ULONG w=0;
							switch (sortBy)
							{
								case SORT_BY_RATE:
									w = (pb[2] << 16) + (*(ULONG*)pb < r ? rnk[*(ULONG*)pb] : 0);
									break;
								case SORT_BY_DATE:
									w = *(ULONG*)pb < ls ? last_mods[*(ULONG*)pb] : 0;
							}
							if (w > maxwt)
							{
								if (maxwt > maxwt2)
								{
									maxwt2 = maxwt;
									maxw2 = maxw;
								}
								maxwt = w;
								maxw = (BYTE*)pb;
							}
							else if (w > maxwt2)
							{
								maxwt2 = w;
								maxw2 = (BYTE*)pb;
							}
							pb += pb[3] + 4;
							sinfo->m_urlcount++;
						}
						urlCount += sinfo->m_urlcount;
						sinfo->m_offset = urls - (BYTE*)urlsn;
						sinfo->m_site = baseSite;
						if ((gr == 0) || (sinfo == siteInfos))
						{
							// If grouping is off or site is first in result set, then copy entire URLs info to the result
							memcpy(urls, buf, size);
							sinfo->m_maxwoffset = sinfo->m_offset + maxw - buf;
							sinfo->m_maxwoffset2 = maxw2 ? sinfo->m_offset + maxw2 - buf : ~0;
							sinfo->m_length = size;
							urls += size;
						}
						else
						{
							// Otherwise, copy information only for 2 URLs with highest ranks
							memcpy(urls, maxw, 6);
							*(WORD*)(urls + 6) = 0;
							sinfo->m_maxwoffset = urls - (BYTE*)urlsn;
							sinfo->m_length = 8;
							urls += 8;
							if (maxw2)
							{
								// More than 1 URL is found for site
								memcpy(urls, maxw2, 6);
								*(WORD*)(urls + 6) = 0;
								sinfo->m_maxwoffset2 = urls - (BYTE*)urlsn;
								sinfo->m_length += 8;
								urls += 8;
							}
							else
							{
								sinfo->m_maxwoffset2 = ~0;
							}
						}
						sinfo++;
					}
					if (buf) delete buf;
				}
				baseSite++;
			}
		}
		else
		{
			baseSite += 32;
		}
	}
	if (m_siteInfos) delete m_siteInfos;
	m_siteInfos = siteInfos;
	if (m_urls) delete m_urls;
	m_urls = urlsn;
	m_infoCount = sinfo - m_siteInfos;
	return urlCount;
}

ULONG* CResult::ComputeSizes(CResult* srcResult)
{
	ULONG* res = new ULONG[m_setSize << 5];
	memset(res, 0, m_setSize << 7);
	CSiteInfo* si = srcResult->m_siteInfos;
	CSiteInfo* ei = si + srcResult->m_infoCount;
	ULONG minSite = m_minSite << 5;
	for (; si < ei; si++)
	{
		res[si->m_site - minSite] = si->m_length;
	}
	return res;
}

void CResult::Reorganize(CResult* srcResult)
{
	if ((srcResult->m_sites) && (srcResult->m_siteInfos == NULL))
	{
		ULONG srcSize = ((srcResult->m_siteLen >> 2) - 1) >> 1;
		ULONG newSize = m_setSize << 5;
		if (newSize > srcSize) newSize = srcSize;
		CSiteInfo* sites = new CSiteInfo[newSize];
		CSiteInfo* psite = sites;
		ULONG* site = srcResult->m_sites;
		ULONG* esrc = site + (srcSize << 1);
		ULONG minsite = m_minSite;
		ULONG maxsite = minsite + m_setSize;
		for (; site < esrc; site += 2)
		{
			ULONG st = site[1] >> 5;
			if ((st >= minsite) && (st < maxsite))
			{
				if (m_siteSet[st - minsite] & (1 << (site[1] & 0x1F)))
				{
					psite->m_site = site[1];
					psite->m_offset = site[0];
					psite->m_length = site[2] - site[0];
					if (psite->m_length > srcResult->m_maxSite) srcResult->m_maxSite = psite->m_length;
					psite++;
				}
			}
		}
		srcResult->m_siteInfos = sites;
		srcResult->m_infoCount = psite - sites;
		m_psite = 0;
	}
}

CSiteInfo* CResult::GetSiteInfo(ULONG site)
{
	// Find element in m_siteInfo array with value of site ID greater or equal site parameter
	while ((m_psite < m_infoCount) && (m_siteInfos[m_psite].m_site < site))
	{
		m_psite++;
	}
	return (m_psite < m_infoCount) && (m_siteInfos[m_psite].m_site == site) ? m_siteInfos + m_psite : NULL;
}

ULONG MaxPartSize = 0x100000;
ULONG MaxGap = 0x10000;

BYTE* CResult::GetUrls(CSiteInfo* info)
{
	if (m_urls)
	{
		// If URLs info for word is in database BLOB, then it is already read
		return (BYTE*)m_urls + info->m_offset - m_sites[0];
	}
	else
	{
		// Otherwise we have to read it from file
		if ((m_partBuf == NULL) || (info->m_site > m_partTo))
		{
			// Info has not been read ahead yet
			ULONG bufsize = max(MaxPartSize, m_maxSite);
			if (m_partBuf == NULL) m_partBuf = new BYTE[bufsize];
			ULONG size = 0;
			ULONG psite = m_psite;
			BYTE* pbuf = m_partBuf;
			// Read ahead URLs information for several sites
			CSiteInfo* esi = m_siteInfos + m_infoCount;
			CSiteInfo* si = m_siteInfos + psite;
			while (si < esi)
			{
				CSiteInfo* psi = si;
				long d = pbuf - m_partBuf - si->m_offset;
				size += si->m_length;
				if (size > bufsize) break;
				ULONG noff = si->m_offset + si->m_length;
				while ((++si < esi) && (si->m_offset - noff <= MaxGap))
				{
					ULONG noff1 = si->m_offset + si->m_length;
					size += noff1 - noff;
					if (size > bufsize) break;
					noff = noff1;
				}
				fseek(m_source, psi->m_offset, SEEK_SET);
				fread(pbuf, 1, noff - psi->m_offset, m_source);
				pbuf += noff - psi->m_offset;
				while (psi < si)
				{
					psi->m_offset += d;
					m_partTo = psi->m_site;
					psi++;
				}
			}
/*
			while (psite < m_infoCount)
			{
				CSiteInfo* si = m_siteInfos + psite;
				size += si->m_length;
				if (size > bufsize) break;	// Partial URLs buffer is full
				fseek(m_source, si->m_offset, SEEK_SET);
				fread(pbuf, 1, si->m_length, m_source);
				si->m_offset = pbuf - m_partBuf;
				pbuf += si->m_length;
				m_partTo = si->m_site;
				psite++;
			}*/
		}
		return m_partBuf + info->m_offset;
	}
}

ULONG CResult::GetResultForSite(ULONG site, BYTE*& p_buf, ULONG mask, CResultUrlFilter* filter)
{
	CSiteInfo* info = GetSiteInfo(site);
	if (info)
	{
		BYTE* buf = GetUrls(info);	// Find original URLs info for site
		// Convert to internal format
		ULONG len = (mask & 0xF) == 0xF ? filter->GetBufferWithWeightsS(info->m_length, buf, p_buf, mask & 0x8000) : filter->GetBufferWithWeightsS(info->m_length, buf, p_buf, mask, mask & 0x8000);
		return len;
	}
	else
	{
		p_buf = NULL;
		return 0;
	}
}

ULONG CResult::GetResultForSiteL(ULONG site, BYTE*& p_buf, CResultUrlFilter* filter)
{
	CSiteInfo* info = GetSiteInfo(site);
	if (info)
	{
		BYTE* buf = GetUrls(info);
		ULONG len = filter->GetBufferWithWeightsSL(info->m_length, buf, p_buf);
		return len;
	}
	else
	{
		p_buf = NULL;
		return 0;
	}
}

ULONG CWord1::GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask)
{
	return GetSites1(real)->GetResultForSite(site, p_buf, mask, m_filter);
}

void CWord1::ReorganizeResults(CResult* res, int real)
{
	res->Reorganize(GetSites1(real));
}

ULONG* CWord1::ComputeSizes(CResult* result, int real)
{
	return result->ComputeSizes(GetSites1(real));
}

void CResult::Normalize()
{
	if (m_setSize > 0)
	{
		if (m_siteSet[0] == 0)
		{
			// Remove heading zeroes
			ULONG i;
			for (i = 1; (i < m_setSize) && (m_siteSet[i] == 0); i++);
			memcpy(m_siteSet, m_siteSet + i, (m_setSize - i) << 2);
			m_setSize -= i;
			m_minSite += i;
		}
		// Remove trailing zeroes
		while ((m_setSize > 0) && (m_siteSet[m_setSize - 1] == 0)) m_setSize--;
	}
}

void CResult::CreateSet()
{
	if (m_sites)
	{
		// Build sites bitmap
		ULONG esite = m_siteLen >> 2;
		ULONG firstSite = m_sites[1] >> 5;
		ULONG lastSite = m_sites[esite - 2] >> 5;
		m_setSize = lastSite - firstSite + 1;
		m_siteSet = new ULONG[m_setSize];
		memset(m_siteSet, 0, m_setSize << 2);
		m_minSite = firstSite;
		ULONG* esites = m_sites + esite;
		for (ULONG* s = m_sites + 1; s < esites; s += 2)
		{
			m_siteSet[(*s >> 5) - firstSite] |= (1 << (*s & 0x1F));
		}
	}
	else
	{
		m_minSite = 0;
	}
}

ULONG GetSites(ULONG word_id, ULONG*& sites, WORD*& urls, ULONG* counts, FILE*& source)
{
	ULONG siteLen = 0;
	ULONG nfile = word_id % 100;
	char ind[1000], sitesn[1000], urlsn[1000];
	sprintf(ind, "%s/%02luw/ind", DBWordDir.c_str(), nfile);
	sprintf(sitesn, "%s/%02luw/sites", DBWordDir.c_str(), nfile);
	sprintf(urlsn, "%s/%02luw/urls", DBWordDir.c_str(), nfile);
	FILE* f = fopen(ind, "r");
	FILE* fs = fopen(sitesn, "r");
	FILE* fu = fopen(urlsn, "r");
	if (f && fs && fu)
	{
		setvbuf(fu, NULL, _IONBF, 0);
		counts[0] = counts[1] = 0;
		ULONG numSites = ::GetSites(word_id, f, fs, sites, counts);
		if (numSites)
		{
			siteLen = numSites * (sizeof(ULONG) << 1) + sizeof(ULONG);
			ULONG urlLen = sites[numSites << 1] - sites[0];
			if (urlLen < 0x10000)
			{
				urls = (WORD*)(new char[urlLen]);
				fseek(fu, sites[0], SEEK_SET);
				fread(urls, 1, urlLen, fu);
				fclose(fu);
			}
			else
			{
				source = fu;
			}
		}
		else
		{
			fclose(fu);
		}
	}
	if (f) fclose(f);
	if (fs) fclose(fs);
	return siteLen;
}

CResult* CWord1::GetSites(int real)
{
	CResult* res = new CResult;
	// First read sites array, where word can be found from database
	if (CompactStorage && (real == 0))
	{
		res->m_siteLen = ::GetSites(m_word_id, res->m_sites, res->m_urls, res->m_counts, res->m_source);
	}
	else
	{
		m_database->GetSites(m_word_id, res->m_sites, res->m_urls, res->m_counts, res->m_source, real);
		if (res->m_sites) res->m_siteLen = res->m_sites[0];
	}
	res->CreateSet();	// Then build bitmap
	return res;
}

ULONG* CLinkTo::ComputeSizes(CResult* result, int real)
{
	return result->ComputeSizes(GetSites1(real));
}

void GetSitesForLink(ULONG urlID, ULONG *&sites, WORD *&urls)
{
	ULONG n = urlID % NUM_HREF_DELTAS;
	char fname[1000], fnamec[1000];
	sprintf(fname, "%s/citations/citation%lu", DBWordDir.c_str(), n);
	sprintf(fnamec, "%s/citations/ccitation%lu", DBWordDir.c_str(), n);
	int citation = open(fname, O_RDONLY);
	if (citation > 0)
	{
		FILE* ccitation = fopen(fnamec, "r");
		if (ccitation)
		{
			off_t off = (urlID / NUM_HREF_DELTAS) * sizeof(ULONG);
			off_t soff = off > 0 ? off - sizeof(ULONG) : off;
			ULONG coff[2];
			coff[0] = 0;
			if (lseek(citation, soff, SEEK_SET) == soff)
			{
				int len = off > 0 ? sizeof(ULONG) << 1 : sizeof(ULONG);
				if (read(citation, off > 0 ? coff : coff + 1, len) == len)
				{
					if (coff[1] > coff[0])
					{
						if (off != 0) fseek(ccitation, coff[0], SEEK_SET);
						ULONG ss;
						if (fread(&ss, sizeof(ULONG), 1, ccitation))
						{
							ULONG ns = ss / sizeof(ULONG);
							sites = new ULONG[ns];
							sites[0] = ss;
							fread(sites + 1, 1, ss - sizeof(ULONG), ccitation);
							ULONG urlLen = coff[1] - sites[0];
							urls = (WORD*)(new BYTE[urlLen]);
							fread(urls, 1, urlLen, ccitation);
						}
					}
				}
			}
			fclose(ccitation);
		}
		close(citation);
	}
}

CResult* CLinkTo::GetSites(int real)
{
	if (real == 0)
	{
		CResult* res = new CResult;
		if (IncrementalCitations)
		{
			GetSitesForLink(m_urlID, res->m_sites, res->m_urls);
		}
		else
		{
			m_database->GetSitesForLink(m_urlID, res->m_sites, res->m_urls);
		}
		if (res->m_sites) res->m_siteLen = res->m_sites[0];
		res->CreateSet();
		return res;
	}
	else
	{
		return NULL;
	}
}

ULONG CLinkTo::GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask)
{
	return GetSites1(real)->GetResultForSiteL(site, p_buf, m_filter);
}

void CLinkTo::ReorganizeResults(CResult* res, int real)
{
	if (real == 0)
	{
		res->Reorganize(GetSites1(real));
	}
}

void CAndWords::MinMax(ULONG* minmax, ULONG* minmax1)
{
	if (minmax1[0] > minmax[0]) minmax[0] = minmax1[0];
	if (minmax1[1] < minmax[1]) minmax[1] = minmax1[1];
}

ULONG COrWordsF::GetMask(ULONG mask, int index)
{
	return (m_exactForm == -1) || (m_exactForm == index) ? mask | 0x8000 : mask & 0x7FFF;
}

ULONG COrWordsF::MatchMask(int index)
{
	return (m_exactForm == -1) || (m_exactForm == index) ? 0x80000000 : 0;
}

int CMultiSearch::GetResultsForSite(ULONG site, BYTE** buffers, ULONG* sizes, ULONG** urls, int& maxsize, int* counts, int real, ULONG mask)
{
	vector<CSearchExpression*>::iterator ent;
	maxsize = 0;
	ULONG* psize = sizes;
//	int ret = 0;
	int ind = 0;
	// First, obtain results for site for all subexpressions without "-" prefix
	for (ent = m_words.begin(); ent != m_words.end(); ent++)
	{
		if (!(*ent)->IsNot())
		{
			*psize = (*ent)->GetResultForSiteFiltered(site, buffers[ind], real, GetMask(mask, ent - m_words.begin()));
			if (*psize > 0)
			{
				urls[ind] = (ULONG*)buffers[ind];
				IterateMaxSize(maxsize, *psize);
				ind++;
				psize++;
			}
			else
			{
				if (buffers[ind]) delete buffers[ind];
			}
		}
	}
	counts[0] = ind;
	// Now, obtain results for site for all subexpressions with "-" prefix
	for (ent = m_words.begin(); ent != m_words.end(); ent++)
	{
		if ((*ent)->IsNot())
		{
			*psize = (*ent)->GetResultForSiteFiltered(site, buffers[ind], real, mask);
			urls[ind] = (ULONG*)buffers[ind];
			ind++;
			psize++;
		}
	}
	counts[1] = ind - counts[0];
	return ind;
}

int GetW(PosRange* pos, int size, WORD maxpos)
{
	if (size == 1)
	{
		while ((pos->start < pos->end) && (*pos->start < maxpos)) pos->start++;
		return 1;
	}
	else
	{
		int s;
		int maxl = 1;
		PosRange* pos1 = pos + 1;
		while ((pos->start < pos->end) && (*pos->start <= maxpos))
		{
			WORD mp1 = *pos->start + pos->dist;
			s = GetW(pos1, size - 1, mp1) + 1;
			if ((pos1->start < pos1->end) && (*pos1->start == mp1))
			{
				if (maxl < s) maxl = s;
				if (pos[s - 1].sph < s) pos[s - 1].sph = s;
			}
			if (*pos->start == maxpos) break;
			pos->start++;
		}
		return maxl;
	}
}

// This function takes array of word positions array, each array describes
// positions for appropriate subexpression in AND expression, calculates
// maximum subphrase length for each subexprssion
// and returns maximum subphrase length.
// Parameters are:
// "pos" is the pointer to structures,
//       each describing sorted array of word positions
// "size" is the count of arrays
// Return value: maximum subphrase length
int GetWR(PosRange* pos, int size)
{
	if (size == 1)
	{
		return 1;
	}
	else
	{
		int s;
		int maxl = 1;
		PosRange* pos1 = pos + 1;
		while (pos->start < pos->end)
		{
			WORD mp1 = *pos->start + pos->dist;
			s = GetW(pos1, size - 1, mp1) + 1;
			if ((pos1->start < pos1->end) && (*pos1->start == mp1))
			{
				if (maxl < s) maxl = s;
				if (pos[s - 1].sph < s) pos[s - 1].sph = s;	// Break found, because subphrase length is less than subexpression index
			}
			pos->start++;
		}
		s = GetWR(pos1, size - 1);
		if (maxl < s) maxl = s;
		return maxl;
	}
}

/** This function calculates URL weight for AND expression, based on position
 * arrays for each of neighboring subexpressions, having not empty positions
 * array (Word, Pattern, OR expression)
 * Parameters are:
 * "pos" is the pointer to structures, each describing sorted array of word positions
 * "pose" points to the end of array (pose - pos is the neighboring subexpression count)
 * Return value is the highest word of ULONG value, describing total URL weight
 */
inline WORD GetWeight(PosRange* pos, PosRange* pose)
{
	int size = pose - pos;
	int maxl = (size == 1) ? 1: GetWR(pos, size);
	WORD minp = 0xFFFF;
	int sphc = 0;
	for ( ; pos < pose; pos++)
	{
		if (pos->end[-1] < minp) minp = pos->end[-1];	// Find minimal position value
		if (pos->sph) sphc++;	// Increment subphrase count
	}
	return ((minp & 0xC000) >> 1) | (maxl == size ? 0x1800 : sphc > 1 ? 0x1000 : sphc << 11) | (minp & 0xC000 == 0 ? 0 : (~minp & 0x7FF));
}

/** This function calculates URL weight for AND expression, based on array of pointers to URL info each subexpression
 * Parameters are: "urls" is the pointer to the array of pointers, pointing to the particular URL info for each subexpression
 * "size" is the total subexpression count, "dist" is the array of distances between subexpression and next subexpression
 * Return value is the highest word of ULONG value, describing total URL weight
 */
inline WORD GetWeight(ULONG** urls, int size, WORD* dist, PosRange* pr)
{
	int i, s;
	WORD minw = 0x7FFF;
	PosRange* ppr;
	WORD form = 0x8000;
	for (i = 0, ppr = pr; i < size; i++, dist++)
	{
		WORD* pcnt = (WORD*)(urls[i] + 1);
		form &= pcnt[0];
		if (pcnt[1] > 0)
		{
			// Not empty positions array, prepare PosRange struct
			ppr->start = pcnt + 2;
			ppr->end = ppr->start + pcnt[1];
			ppr->sph = 0;
			ppr->dist = *dist;
			ppr++;
		}
		else
		{
			// Empty positions array found
			WORD wt = pcnt[0] & 0x7FFF;	// Weight of subexpression with empty positions array
			if ((s = ppr - pr) >= 1)
			{
				WORD wt1 = s == 1 ? pr->start[-2] & 0x7FFF : GetWeight(pr, ppr);	// Calculate weight for neighboring subexpressions
				if (wt1 < wt) wt = wt1;
			}
			ppr = pr;
			if (minw > wt) minw = wt;
		}
	}
	if ((s = ppr - pr) >= 1)
	{
		WORD wt = s == 1 ? pr->start[-2] & 0x7FFF : GetWeight(pr, ppr);	// Calculate weight for last neighboring subexpressions
		if (minw > wt) minw = wt;
	}
	return minw | form;
}

// This function takes array of URL info buffers, combines them by AND
// and produces resulting URL info
// Parameters are:
// "size" is size of array,
// "buffers" is the array of input buffers,
// "ebuffers" is the array of input buffers ends,
// "urls" is the current pointers to the buffers start,
// "dist" is the array of distances between subexpression and next subexprssion,
// "p_buf" is result buffer.
// Returns length of result buffer
ULONG AndResults(int size, ULONG* sizes, BYTE** buffers, BYTE** ebuffers, ULONG** urls, BYTE*& p_buf, WORD* dist)
{
	int i;
	WORD* pbuf = (WORD*)p_buf;
	ULONG counts[2] = {0, 0};
	PosRange* pr = (PosRange*)alloca(size * sizeof(PosRange));
	do
	{
		// Find maximal URL ID, and compare all current URL IDs
		ULONG** purl = urls + 1;
		ULONG furl = **urls;
		ULONG maxurl = furl;
		int same = 1;
		for (i = 1; i < size; purl++, i++)
		{
			if (furl != **purl)
			{
				if (**purl > maxurl) maxurl = **purl;
				same = 0; // URLs are not the same
			}
		}
		BYTE** ebuf = ebuffers;
		if (same)
		{
			// URL matches expression, find its weight and store info to the result buffer
			*(ULONG*)pbuf = furl;
			pbuf[2] = GetWeight(urls, size, dist, pr);
			pbuf[3] = 0;	// No positions for AND expression
			counts[1]++;
			int fin = 0;
			// Advance pointers for each subexpression and detect buffer end for any of them
			for (i = 0, purl = urls; i < size; purl++, ebuf++, i++)
			{
				(WORD*&)*purl += ((WORD*)(*purl))[3] + 4;
				if ((BYTE*)*purl >= *ebuf) fin = 1;
			}
			pbuf += 4;
			if (fin) break;	// Buffer end for some subexpression is found
		}
		else
		{
			// Advance pointers for each subexpression with current URL ID less than maxmial
			for (i = 0, purl = urls; i < size; purl++, i++)
			{
				// Advance pointer until value of URL ID will be greater or equal to maximal
				while (**purl < maxurl)
				{
					(WORD*&)*purl += ((WORD*)(*purl))[3] + 4;
					if ((BYTE*)*purl >= ebuf[i]) goto end;	// Buffer end detected
				}
			}
		}
	}
	while (true);
end:
	// Free original buffers
	for (i = 0; i < size; i++)
	{
		delete buffers[i];
	}
	return (BYTE*)pbuf - (BYTE*)p_buf;
}

ULONG CAndWords::GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask)
{
	int i, maxsize, size = m_words.size();
	ULONG* sizes = (ULONG*)alloca(size * sizeof(ULONG));
	BYTE** buffers = (BYTE**)alloca(size * sizeof(BYTE*));
	BYTE** ebuffers = (BYTE**)alloca(size * sizeof(BYTE*));
	ULONG** urls = (ULONG**)alloca(size * sizeof(ULONG*));
	int cts[2];
	int size1;
	// Obtain info for each subexpression
	if ((size1 = GetResultsForSite(site, buffers, sizes, urls, maxsize, cts, real, mask)) < size)
	{
		for (int i = 0; i < size1; i++)
		{
			if (buffers[i]) delete buffers[i];
		}
		p_buf = NULL;
		return 0;
	}
	for (i = 0; i < size; i++)
	{
		ebuffers[i] = buffers[i] + sizes[i];
	}
	struct timeval tm, tm1;
	gettimeofday(&tm, NULL);
	ULONG rlen = 0;
	if (cts[0] > 1)
	{
		// Combine results for subexpressions without "-" prefix
		p_buf = new BYTE[maxsize];
		rlen = AndResults(cts[0], sizes, buffers, ebuffers, urls, p_buf, &(*m_dist.begin()));
	}
	else if (cts[0] == 1)
	{
		// Get results for the first and the only subexpression without "-" prefix
		rlen = sizes[0];
		p_buf = buffers[0];
	}
	if (cts[1] > 0)
	{
		// Process each subexpression with "-" prefix
		for (i = cts[0]; i < cts[0] + cts[1]; i++)
		{
			ULONG nLen = sizes[i];
			BYTE* notbuf = buffers[i];
			if (nLen > 0)
			{
				WORD* p_bufe = (WORD*)(p_buf + rlen);
				WORD* pb = (WORD*)p_buf;
				WORD* notbufe = (WORD*)(notbuf + nLen);
				WORD* nb = (WORD*)notbuf;
				BYTE* dst = p_buf;
				int f = 0;
				// Scan buffer with results and exclude buffer sumultaneously
				while ((pb < p_bufe) && (nb < notbufe))
				{
					if (*(ULONG*)pb == *(ULONG*)nb)
					{
						// Advance result buffer pointer
						pb += pb[3] + 4;
						nb += nb[3] + 4;
						f = 1;
					}
					else if (*(ULONG*)pb < *(ULONG*)nb)
					{
						WORD len = pb[3] + 4;
						if (f)
						{
							// Copy URL info, only if 1 or more previous URLs was filtered out
							memcpy(dst, pb, len << 1);
						}
						// Advance both pointers
						dst += (len << 1);
						pb += len;
					}
					else
					{
						// Advance result buffer pointer
						nb += nb[3] + 4;
					}
				}
				int rest = (BYTE*)p_bufe - (BYTE*)pb;
				if (rest > 0)
				{
					// Process the rest of result buffer
					if (f)
					{
						memcpy(dst, pb, rest);
					}
					dst += rest;
				}
				rlen = dst - p_buf;
			}
			if (notbuf) delete notbuf;
		}
	}
	gettimeofday(&tm1, NULL);
	m_combineTime += timedif(tm1, tm);
	return rlen;
}

void COrWords::MinMax(ULONG* minmax, ULONG* minmax1)
{
	if (minmax1[0] < minmax[0]) minmax[0] = minmax1[0];
	if (minmax1[1] > minmax[1]) minmax[1] = minmax1[1];
}

ULONG COrWords::GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask)
{
	int i, maxsize, size = m_words.size();
	ULONG* sizes = (ULONG*)alloca(size * sizeof(ULONG));
	BYTE** buffers = (BYTE**)alloca(size * sizeof(BYTE*));
	ULONG** urls = (ULONG**)alloca(size * sizeof(ULONG*));
	int cts[2];
	size = GetResultsForSite(site, buffers, sizes, urls, maxsize, cts, real, mask);
	p_buf = new BYTE[maxsize];
	struct timeval tm1, tm;
	gettimeofday(&tm, NULL);
	ULONG ret = GetOrContents(size, buffers, sizes, urls, p_buf);
	for (i = 0; i < size; i++)
	{
		if (buffers[i]) delete buffers[i];
	}
	gettimeofday(&tm1, NULL);
	m_combineTime += timedif(tm1, tm);
	return ret;
}

CMatchWord* CPhrase::Match(CMatchWord* mword, ULONG& how)
{
	CSearchExpression* last = m_words.back();
	WORD position = m_positions.end()[-2];
	ULONG how1;
	ULONG form = 0x80000000;
	// Check if last expression in phrase matches mword
	if (last->Match(mword, how1))
	{
		vector<CSearchExpression*>::iterator ent;
		CMatchWord* wrd = mword;
		vector<WORD>::iterator ppos;
		// Check match for each subexpression of phrase in reverse order
		for (ent = m_words.end() - 2, ppos = m_positions.end() - 3; ent >= m_words.begin(); --ent, --ppos)
		{
			// Advance pointer in the linked list of neighboring origial words
			for (; position != *ppos; position--)
			{
				wrd = wrd->m_prev;
				if (wrd == NULL) return NULL;
			}
			if (!(*ent)->Match(wrd, how1)) return NULL;
			form &= (how1 | 0x7FFFFFFF);
		}
		how = form | 0x3FFFFFFE;
		return wrd;
	}
	return NULL;
}

void CPhrase::MinMax(ULONG* minmax, ULONG* minmax1)
{
	if (minmax1[0] > minmax[0]) minmax[0] = minmax1[0];
	if (minmax1[1] < minmax[1]) minmax[1] = minmax1[1];
}

inline int IsPhrase(PosRange* pos, int size, WORD maxpos)
{
	int e;
	while ((e = pos->end >= pos->start ? *pos->end - maxpos : -1) > 0) pos->end--;
	return (e == 0) && ((size == 1) || IsPhrase(pos + 1, size - 1, *pos->end + pos->dist));
}

// This function takes array of word positions array, each array describes
// positions for appropriate subexpression in phrase, finds phrase,
// and calculates maximal position of phrase in URL	content.
// Parameters are:
// "urls" is the pointer to the array of pointers to URL info, each describing sorted array of word positions
// "size" is the count of arrays
// "maxpos" is the output parameter, filled with maximal position in URL, where phrase is found
// Return value: 1 if phrase found, otherwise 0
inline int IsPhrase(ULONG** urls, int size, WORD* positions, WORD& maxpos, PosRange* pos)
{
	PosRange* ppos = pos;
	ULONG** purl = urls;
	for (int i = 0; i < size; i++, purl++, ppos++, positions++)
	{
		ppos->start = (WORD*)(*purl) + 4;
		ppos->end = ppos->start + ppos->start[-1] - 1;
		ppos->dist = positions[1] - positions[0];
	}
	while (pos->end >= pos->start)
	{
		if (IsPhrase(pos + 1, size - 1, *pos->end + pos->dist))
		{
			maxpos = *pos->end;
			if (maxpos & 0xC000) maxpos ^= 0x3FFF; else maxpos = 0x3000;
			return 1;
		}
		pos->end--;
	}
	return 0;
}

ULONG CPhrase::GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask)
{
	int i, maxsize, size = m_words.size();
	ULONG* sizes = (ULONG*)alloca(size * sizeof(ULONG));
	BYTE** buffers = (BYTE**)alloca(size * sizeof(BYTE*));
	BYTE** ebuffers = (BYTE**)alloca(size * sizeof(BYTE*));
	ULONG** urls = (ULONG**)alloca(size * sizeof(ULONG*));
	PosRange* pos = (PosRange*)alloca(size * sizeof(PosRange));
	vector<WORD>::iterator positions = m_positions.begin();
	int cts[2];
	int size1;
	// Find results for each subexpresssion
	if ((size1 = GetResultsForSite(site, buffers, sizes, urls, maxsize, cts, real, mask)) < size)
	{
		for (int i = 0; i < size1; i++)
		{
			if (buffers[i]) delete buffers[i];
		}
		p_buf = NULL;
		return 0;
	}
	for (i = 0; i < size; i++)
	{
		ebuffers[i] = buffers[i] + sizes[i];
	}
	p_buf = new BYTE[maxsize];
	WORD* pbuf = (WORD*)p_buf;
	struct timeval tm, tm1;
	gettimeofday(&tm, NULL);
	do
	{
		// Find maximal URL ID, and compare all current URL IDs
		ULONG** purl = urls + 1;
		ULONG furl = **urls;
		ULONG maxurl = furl;
		int same = 1;
		for (i = 1; i < size; purl++, i++)
		{
			if (furl != **purl)
			{
				if (**purl > maxurl) maxurl = **purl;
				same = 0;
			}
		}
		if (same)
		{
			// URL matches expression, find if phrase of subexpressions can be found in it
			WORD maxp;
			if (IsPhrase(urls, size, &(*positions), maxp, pos))
			{
				*(ULONG*)pbuf = furl;
				pbuf[2] = maxp;
				pbuf[3] = 0;	// No positions array for phrase
				pbuf += 4;
			}
			BYTE** ebuf = ebuffers;
			// Advance pointers for each subexpression in phrase and detect buffer end for any of them
			for (i = 0, purl = urls; i < size; purl++, ebuf++, i++)
			{
				(WORD*&)*purl += ((WORD*)(*purl))[3] + 4;
				if ((BYTE*)(*purl) >= *ebuf) goto end;	// Buffer end found
			}
		}
		else
		{
			// Advance pointers for each subexpression with current URL ID less than maxmial
			BYTE** ebuf = ebuffers;
			for (i = 0, purl = urls; i < size; purl++, ebuf++, i++)
			{
				// Advance pointer until value of URL ID will be greater or equal to maximal
				while (**purl < maxurl)
				{
					(WORD*&)*purl += ((WORD*)(*purl))[3] + 4;
					if ((BYTE*)(*purl) >= *ebuf) goto end;	// Buffer end found
				}
			}
		}
	}
	while (true);
end:
	// Free orginal buffers
	for (i = 0; i < size; i++)
	{
		delete buffers[i];
	}
	gettimeofday(&tm1, NULL);
	m_combineTime += timedif(tm1, tm);
	return (BYTE*)pbuf - (BYTE*)p_buf;
}

CResult* CNot::GetSites(int real)
{
	CResult* result = m_base->GetSites1(real);
	m_sresult[real] = new CResult;
	memcpy(m_sresult[real], result, sizeof(CResult));
	m_sresult[real]->m_not = 1;
	return m_sresult[real];
}

ULONG* CNot::ComputeSizes(CResult* result, int real)
{
	return NULL;
}

void CNot::ReorganizeResults(CResult* res, int real)
{
	m_base->ReorganizeResults(res, real);
}

ULONG CNot::GetResultForSite(ULONG site, BYTE*& p_buf, int real, ULONG mask)
{
	return m_base->GetResultForSite(site, p_buf, real, mask);
}

// Form ULONG value describing weight or URL
// Parameters are:
// "wp" is high word of weight based on position of expression in HTML and phrase match value
// "rnk" is the PageRank of URL
// Return value is the final value of weight
ULONG CombineWeight(WORD wp, WORD rnk)
{
	return wp & 0x6000 ? ((wp << 16) + rnk) : ((wp & 0xF800) << 16) + (rnk << 11);
}

int TotalSiteCount(CResult** res)
{
	if (res[0] == NULL) return res[1] ? res[1]->m_infoCount : 0;
	if (res[1] == NULL) return res[0] ? res[0]->m_infoCount : 0;
	int result = res[1]->m_infoCount;
	CSiteInfo* ss = res[1]->m_siteInfos;
	CSiteInfo* ls = ss + result;
	CSiteInfo* s = res[0]->m_siteInfos;
	CSiteInfo* lsi = s + res[0]->m_infoCount;
	while (s < lsi)
	{
		while ((ss < ls) && (ss->m_site < s->m_site)) ss++;
		if (ss < ls)
		{
			if (ss->m_site != s->m_site)
				result++;
		}
		else
		{
			result += (lsi - s);
			break;
		}
		s++;
	}
	return result;
}

void GetUrls(CResult** res, ULONG& nFirst, ULONG& totalResCount, CUrlW*& p_urls, int gr, int sortBy)
{
	ULONG count = 0;
	ULONG r = ranks.size();
	ULONG ls = last_mods.size();
	vector<WORD>::iterator rnk = ranks.begin();
	for (int i = 0; i < 2; i++)
	{
		CResult* result = res[i];
		if (result && (result->m_infoCount > 0))
		{
			CSiteInfo* lastInfo = result->m_siteInfos + result->m_infoCount - 1;
			count += gr ? result->m_infoCount : (lastInfo->m_offset + lastInfo->m_length) / 8;
		}
	}
	CUrlWeight2* urls = new CUrlWeight2[count];
	CUrlWeight2* purl = urls;
	if (res[0]) res[0]->GetUrls(urls, purl, gr, sortBy);
	if (res[1]) res[1]->GetUrls(urls, purl, gr, sortBy);
	totalResCount = purl - urls;
	if (nFirst > totalResCount) nFirst = totalResCount;
	// Partially sort URLs by weight, to obtain first nFirst URLs with highest ranks
	partial_sort<CUrlWeight2*>(urls, urls + nFirst, purl);
	// Process all nFirst URLs with highest ranks
	for (purl = urls; purl < urls + nFirst; purl++, p_urls++)
	{
		p_urls->m_urlID = *(ULONG*)purl->m_url;
		p_urls->m_weight = purl->m_weight;
		p_urls->m_siteID = purl->m_si->m_site;
		if (gr)
		{
			p_urls->m_count = purl->m_si->m_urlcount;
			if (purl->m_si2) p_urls->m_count += purl->m_si2->m_urlcount;
			WORD* url2 = purl->m_url2;
			if (url2 == NULL)
			{
				if (purl->m_urlr) url2 = purl->m_urlr;
				if ((url2 == NULL) || (*(ULONG*)url2 == p_urls->m_urlID)) url2 = purl->m_urlr2;
			}
			// If grouping by sites is on and more than 1 URL is found on the site, then store information
			// about second URL of site with second highest rank
			if (url2)
			{
				p_urls++;
				p_urls->m_urlID = *(ULONG*)url2;
				switch (sortBy)
				{
					case SORT_BY_RATE:
						p_urls->m_weight = CombineWeight(url2[2], p_urls->m_urlID < r ? rnk[p_urls->m_urlID] : 0);
						break;
					case SORT_BY_DATE:
						purl->m_weight = p_urls->m_urlID < ls ? last_mods[p_urls->m_urlID] : 0;
						break;
				}

				p_urls->m_siteID = purl->m_si->m_site;
				p_urls->m_count = purl->m_si->m_urlcount;
			}
		}
		else
		{
			p_urls->m_count = 0;
		}
	}
	delete urls;
}

void CResult::GetUrls(CUrlWeight2* start, CUrlWeight2*& dst, int gr, int sortBy)
{
	if (m_infoCount == 0) return;
	CUrlWeight2* estart = dst;
	ULONG r = ranks.size();
	ULONG ls = last_mods.size();
	vector<WORD>::iterator rnk = ranks.begin();
	CSiteWeights::iterator sw = siteWeights.begin();
	CSiteWeights::iterator esw = siteWeights.end();
	// Process all sites in the result set
	CSiteInfo* lastInfo = m_siteInfos + m_infoCount - 1;
	for (CSiteInfo* si = m_siteInfos; si <= lastInfo; si++)
	{
		while ((sw < esw) && (sw->m_siteID < si->m_site)) sw++;
		int siteWeight = (sw < esw) && (sw->m_siteID == si->m_site) ? sw->m_weight : 0;
		if (gr)
		{
			while ((start < estart) && (start->m_si->m_site < si->m_site)) start++;
			ULONG weight = 0;
			WORD* url = (WORD*)((BYTE*)m_urls + si->m_maxwoffset);
			WORD* url2 = si->m_maxwoffset2 == ~0UL ? NULL : (WORD*)((BYTE*)m_urls + si->m_maxwoffset2);
			switch (sortBy)
			{
				case SORT_BY_RATE:
					weight = CombineWeight(url[2], *(ULONG*)url < r ? rnk[*(ULONG*)url] : 0);
					break;
				case SORT_BY_DATE:
					weight = *(ULONG*)url < ls ? last_mods[*(ULONG*)url] : 0;
					break;
			}
			if ((start < estart) && (start->m_si->m_site == si->m_site))
			{
				start->m_si2 = si;
				start->m_urlr = (WORD*)((BYTE*)m_urls + si->m_maxwoffset);
				start->m_urlr2 =  si->m_maxwoffset2 == ~0UL ? NULL : (WORD*)((BYTE*)m_urls + si->m_maxwoffset2);
				if (start->m_weight < weight) start->m_weight = weight;
			}
			else
			{
				dst->m_url = url;
				dst->m_weight = weight;
				dst->m_si = si;
				dst->m_siteWeight = siteWeight;
				dst->m_si2 = NULL;
				dst->m_url2 = url2;
				dst->m_urlr = NULL;
				dst->m_urlr2 = NULL;
				dst++;
			}
		}
		else
		{
			// Otherwise, copy information about all URLs
			WORD* url = (WORD*)((BYTE*)m_urls + si->m_offset);
			WORD* eurl = (WORD*)((BYTE*)url + si->m_length);
			while (url < eurl)
			{
				while ((start < estart) && ((start->m_si->m_site < si->m_site) ||
					((start->m_si->m_site == si->m_site) &&
					(*(ULONG*)start->m_url < *(ULONG*)url)))) start++;
				ULONG weight = 0;
				switch (sortBy)
				{
					case SORT_BY_RATE:
						weight = CombineWeight(url[2], *(ULONG*)url < r ? rnk[*(ULONG*)url] : 0);
						break;
					case SORT_BY_DATE:
						weight = *(ULONG*)url < ls ? last_mods[*(ULONG*)url] : 0;
						break;
				}
				if (!((start < estart) && (start->m_si->m_site == si->m_site) && (*(ULONG*)start->m_url == *(ULONG*)url)))
				{
					dst->m_weight = weight;
					dst->m_url = url;
					dst->m_si = si;
					dst->m_siteWeight = siteWeight;
					dst->m_url2 = NULL;
					dst->m_si2 = NULL;
					dst++;
				}
				url += url[3] + 4;
			}
		}
	}
}

/** This method returns information about first nFirst URLs or sites with highest ranks.
 * Parameters are:
 * "nFirst" is the count of results, updated if actual count of results is less
 * "urlCount" is the total result count,
 * "p_urls" is the out buffer
 * "gr" == 1 if grouping by sites is on
 * "sortBy" is sorting criteria; can be SORT_BY_RATE or SORT_BY_DATE
 */
void CResult::GetUrls(ULONG& nFirst, ULONG& urlCount, CUrlW*& p_urls, int gr, int sortBy)
{
	if (m_infoCount == 0)
	{
		// Result set is empty
		nFirst = 0;
		return;
	}
	// Prepare buffer with URL infos
	CSiteInfo* lastInfo = m_siteInfos + m_infoCount - 1;
	ULONG count = gr ? m_infoCount : (lastInfo->m_offset + lastInfo->m_length) / 8;

	CUrlWeight* urls = new CUrlWeight[count];
	CUrlWeight* purl = urls;
	ULONG r = ranks.size();
	ULONG ls = last_mods.size();
	CRanks::iterator rnk = ranks.begin();
	CSiteWeights::iterator sw = siteWeights.begin();
	CSiteWeights::iterator esw = siteWeights.end();
	// Process all sites in the result set
	for (CSiteInfo* si = m_siteInfos; si <= lastInfo; si++)
	{
		while ((sw < esw) && (sw->m_siteID < si->m_site)) sw++;
		int siteWeight = (sw < esw) && (sw->m_siteID == si->m_site) ? sw->m_weight : 0;
		if (gr)
		{
			purl->m_url = (WORD*)((BYTE*)m_urls + si->m_maxwoffset);
			switch (sortBy)
			{
				case SORT_BY_RATE:
					purl->m_weight = CombineWeight(purl->m_url[2], *(ULONG*)purl->m_url < r ? rnk[*(ULONG*)purl->m_url] : 0);
					break;
				case SORT_BY_DATE:
					purl->m_weight = *(ULONG*)purl->m_url < ls ? last_mods[*(ULONG*)purl->m_url] : 0;
			}
			purl->m_si = si;
			purl->m_siteWeight = siteWeight;
			purl++;
		}
		else
		{
			// Otherwise, copy information about all URLs
			WORD* url = (WORD*)((BYTE*)m_urls + si->m_offset);
			WORD* eurl = (WORD*)((BYTE*)url + si->m_length);
			while (url < eurl)
			{
				purl->m_url = url;
				purl->m_si = si;
				switch (sortBy)
				{
					case SORT_BY_RATE:
						purl->m_weight = CombineWeight(url[2], *(ULONG*)url < r ? rnk[*(ULONG*)url] : 0);
						break;
					case SORT_BY_DATE:
						purl->m_weight = *(ULONG*)url < ls ? last_mods[*(ULONG*)url] : 0;
				}
				purl->m_siteWeight = siteWeight;
				url += url[3] + 4;
				purl++;
			}
		}
	}
	urlCount = purl - urls;
	if (nFirst > urlCount) nFirst = urlCount;
	// Partially sort URLs by weight, to obtain first nFirst URLs with highest ranks
	partial_sort(urls, urls + nFirst, purl);
	// Process all nFirst URLs with highest ranks
	for (purl = urls; purl < urls + nFirst; purl++, p_urls++)
	{
		p_urls->m_urlID = *(ULONG*)purl->m_url;
		p_urls->m_weight = purl->m_weight;
		p_urls->m_siteID = purl->m_si->m_site;
		if (gr)
		{
			p_urls->m_count = purl->m_si->m_urlcount;
			// If grouping by sites is on and more than 1 URL is found on the site, then store information
			// about second URL of site with second highest rank
			if ((purl->m_si->m_urlcount > 1) && (purl->m_si->m_maxwoffset2 != (ULONG)~0))
			{
				p_urls++;
				WORD* url2 = (WORD*)((BYTE*)m_urls + purl->m_si->m_maxwoffset2);
				p_urls->m_urlID = *(ULONG*)url2;
				switch (sortBy)
				{
					case SORT_BY_RATE:
						p_urls->m_weight = CombineWeight(url2[2], p_urls->m_urlID < r ? rnk[p_urls->m_urlID] : 0);
						break;
					case SORT_BY_DATE:
						purl->m_weight = p_urls->m_urlID < ls ? last_mods[p_urls->m_urlID] : 0;
				}

				p_urls->m_siteID = purl->m_si->m_site;
				p_urls->m_count = purl->m_si->m_urlcount;
			}
		}
		else
		{
			p_urls->m_count = 0;
		}
	}
	delete urls;
}

void CResult::SelectSpace(ULONG* space, ULONG mins, ULONG maxs)
{
	ULONG minrsite = max(mins, m_minSite);
	ULONG maxrsite = min(maxs, m_minSite + m_setSize);
	if (maxrsite < minrsite)
	{
		m_setSize = 0;
		m_minSite = 0;
		if (m_siteSet) m_siteSet[0] = 0;
	}
	else
	{
		ULONG newLen = maxrsite - minrsite + 1;
		ULONG* newSet = new ULONG[newLen];
		ULONG m;
		for (m = minrsite; m < maxrsite; m++)
		{
			newSet[m - minrsite] = m_siteSet[m - m_minSite] & space[m - mins];
		}
		delete m_siteSet;
		m_siteSet = newSet;
		m_setSize = maxrsite - minrsite;
		m_minSite = minrsite;
	}
}

void CResult::SelectSites(vector<ULONG> *sites, int num)
{
	ULONG minsite = m_minSite << 5;
	ULONG maxsite = minsite + (m_setSize << 5);
	ULONG minrsite = max((*sites)[0], minsite);
	ULONG maxrsite = min((*sites)[num - 1] + 1, maxsite);
	if (maxrsite <= minrsite)
	{
		m_setSize = 0;
		m_minSite = 0;
		if (m_siteSet) m_siteSet[0] = 0;
	}
	else
	{
		ULONG min5 = minrsite >> 5;
		ULONG max5 = (maxrsite >> 5) + 1;
		ULONG* siteSet = new ULONG[max5 - min5];
		memset(siteSet, 0, (max5 - min5) << 2);
		for (int i = 0; i < num; i++)
		{
			ULONG site = (*sites)[i];
			if ((site >= minrsite) && (site <= maxrsite))
			{
				if (m_siteSet[(site >> 5) - m_minSite] & (1 << (site & 0x1F)))
				{
					siteSet[(site >> 5) - min5] |= (1 << (site & 0x1F));
				}
			}
		}
		delete m_siteSet;
		m_siteSet = siteSet;
		m_setSize = max5 - min5;
		m_minSite = min5;
	}
}

int CSearchExpression::ExtractSubset(char* path)
{
	char* ds = strstr(path, "://");
	if (ds == NULL) return 0;
	char* sl = strchr(ds + 3, '/');
	if (sl == NULL) return 0;
	if (sl[1] == '#')
	{
		// Path is set in the format <site#N> where N is the subset number
		sl[1] = 0;
		return atoi(sl + 2);
	}
	else
	{
		if (sl[1] != 0)
		{
			// Path is set in the format <URL%>, find subset number from database table "subsets"
			int subset = m_database->GetSubset(path);
			sl[1] = 0;
			return subset;
		}
		else
		{
			// Search is restricted by whole site
			return 0;
		}
	}
}

ULONG CSearchExpression::GetResult(const char* urls, int site, int gr, int sortBy, CULONGSet& spaces, CResult** res)
{
	vector<ULONG> *hosts = NULL;
	int nums;
	CULONGSet subsets;
	ULONG* space = NULL;
	ULONG min, max;
	if (site > 0)
	{
		// Search by one site, possibly followed by link "more results from"
		min = site >> 5;
		max = min + 1;
	}
	else
	{
		min = ~0;
		max = 0;
	}
	res[0] = res[1] = NULL;
	if (spaces.size() > 0)
	{
		// Search by set of web spaces
		vector<ULONG*> sets;
		sets.reserve(spaces.size());
		int emp = 0;
		// Process each space
		for (CULONGSet::iterator sp = spaces.begin(); sp != spaces.end(); sp++)
		{
			char fname[1000];
			sprintf(fname, "%s/subsets/space%lu", DBWordDir.c_str(), *sp);
			FILE* f = fopen(fname, "r");
			if (f)
			{
				ULONG* set = NULL;
				fseek(f, 0, SEEK_END);
				int len = ftell(f);
				fseek(f, 0, SEEK_SET);
				if (site == 0)
				{
					// Read all bitmap if search is not restricted by 1 site
					set = new ULONG[(len >> 2) + 1];
					fread(set, 4, 1, f);
					set[1] = set[0] + (len >> 2) - 1;
					fread(set + 2, 1, len - 4, f);
				}
				else
				{
					// Find only one ULONG in the space bitmap, which includes bit for site
					ULONG start;
					ULONG mask;
					fread(&start, 4, 1, f);
					if (min >= start)
					{
						fseek(f, (min - start) << 2, SEEK_CUR);
						if (fread(&mask, 1, 4, f) == 4)
						{
							set = new ULONG[3];
							set[0] = min;
							set[1] = set[0] + 1;
							set[2] = mask & (1 << (site & 0x1F));
						}
					}
				}
				fclose(f);
				if (set)
				{
					sets.push_back(set);
					// Update min and max possible site ID values
					if (set[0] < min) min = set[0];
					if (set[1] > max) max = set[1];
				}
				else
				{
					emp = 1;	// Empty set is found
				}
			}
		}
		if (sets.size() > 0)
		{
			// Build final bitmap for all sites belonging to any of specified web spaces
			ULONG spacelen = max - min + 1;
			space = new ULONG[spacelen];
			memset(space, 0, spacelen << 2);
			for (vector<ULONG*>::iterator set = sets.begin(); set != sets.end(); set++)
			{
				ULONG start = (*set)[0] - min;
				ULONG* end = *set + (*set)[1] - (*set)[0] + 2;
				ULONG* rs = space + start;
				for (ULONG* s = *set + 2; s < end; s++, rs++)
				{
					*rs |= *s;
				}
			}
		}
		else if (emp)
		{
			return 0;	// Only empty web spaces found
		}
	}
	else if (site != 0)
	{
		// Build small bitmap from 1 bit if search is restricted by one site
		space = new ULONG[1];
		*space = 1 << (site & 0x1F);
	}
	if (*urls)
	{
		// Search is restricted by URL masks or site names, delimited by 1 space symbol
		char urlsc[10000];
		strcpy(urlsc, urls);
		char urlsm[10000];
		// Find first URL mask
		char* ps = strchr(urlsc, ' ');
		if (ps) *ps = 0;
		int subid = ExtractSubset(urlsc);
		if (subid > 0)
		{
			// Search is restricted by directory of site
//			logger.log(CAT_ALL, L_WARN, "Found subset %s (number %d)\n", urlsc, subid);
			subsets.insert(subid);
		}
		else
			logger.log(CAT_ALL, L_WARN, "Subset %s not found\n", urlsc);

		// Form first SQL WHERE subexpression
		char* purl = urlsm + sprintf(urlsm, "site LIKE '%s'", urlsc);	//TO DO proper escaping
		// Process all next URL masks
		while (ps)
		{
			char *s = ++ps;
			ps = strchr(s, ' ');
			if (ps) *ps = 0;
			subid = ExtractSubset(s);
			if (subid > 0)
			{
				// Search is restricted by directory of site
				subsets.insert(subid);
			}
			sprintf(purl, " OR site LIKE '%s'", s);
			purl += strlen(purl);
		}
		// Find array of site IDs
		hosts = m_database->GetHosts(urlsm, nums);
		if (hosts == NULL)
		{
			return 0;
		}
	}
	int ssize = subsets.size();
	ULONG* turls = NULL;
	int totalLen = 0;
	ULONG** surls = NULL;
	int* slen = NULL;
	if (ssize)
	{
		// Search is restricted by directories of site(s)
		surls = (ULONG**)alloca(ssize * sizeof(ULONG*));
		slen = (int*)alloca(ssize * sizeof(int));
		int i = 0;
		// Read subsets for each URL mask
		for (CULONGSet::iterator it = subsets.begin(); it != subsets.end(); it++, i++)
		{
			char fname[1000];
			sprintf(fname, "%s/subsets/sub%lu", DBWordDir.c_str(), *it);
			FILE* f = fopen(fname, "r");
			if (f)
			{
				fseek(f, 0, SEEK_END);
				slen[i] = ftell(f) >> 2;
				totalLen += slen[i];
				fseek(f, 0, SEEK_SET);
				surls[i] = new ULONG[slen[i]];
				fread(surls[i], 4, slen[i], f);
				fclose(f);
			}
			else
			{
				surls[i] = NULL;
				slen[i] = 0;
			}
		}
		// Prepare sorted array of all URL IDs in all subsets
		turls = new ULONG[totalLen];
		ULONG* pt = turls;
		for (i = 0; i < ssize; i++)
		{
			if (surls[i])
			{
				memcpy(pt, surls[i], slen[i] << 2);
				pt += slen[i];
				delete surls[i];
			}
		}
		if (ssize > 1) sort<ULONG*>(turls, pt);
	}
	ULONG urlCount = 0;
	// Process main database (real=0) and "real-time" database (real=1)
	for (int real = 0; real <= 1; real++)
	{
		// Obtain sites, where expression can be found
		res[real] = GetSites1(real);
		CResult* rs = res[real];
		if (rs == NULL) continue;	// No sites found
		rs->Normalize();
		if (hosts)
		{
			// Restrict site set by site ID array if search is restricted by URL masks or site names
			rs->SelectSites(hosts, nums);
		}
		if (space)
		{
			// Restrict site set web space bitmap if search is restricted by web spaces
			rs->SelectSpace(space, min, max);
		}
		// Prepare m_siteInfo array
		if (rs->m_setSize > 0)
		{
			ReorganizeResults(rs, real);
			// Compute maximal result buffer sizes for each site in the set
			ULONG* sizes = ComputeSizes(rs, real);
			ULONG sz = rs->m_setSize << 5;
			ULONG total = 0;
			// Compute total buffer size
			for (ULONG i = 0; i < sz; i++)
			{
				total += sizes[i];
			}
			delete sizes;
			// Obtain final info about sites and URLs, which match expression
			urlCount += rs->GetFinalResult(this, total, turls, totalLen, real, gr, sortBy);
		}
	}
	if (hosts)
	{
		hosts->clear();
		delete hosts;
	}
	if (space) delete space;
	if (turls) delete turls;
	return urlCount;
}

void CRanks::LoadRanks(int f)
{
	lseek(f, 0, SEEK_END);
	int size = (fsize(f) - sizeof(m_version)) >> 2;
	if (size >= 0)
	{
		resize(size);
		float buf[0x4000];
		lseek(f, sizeof(m_version), SEEK_SET);
		iterator r = begin();
		int n;
		while ((n = read(f, buf, sizeof(buf)) / sizeof(float)) > 0)
		{
			for (int i = 0; i < n; i++)
			{
				// Convert float value into WORD value to save space
				if (buf[i] == 0)
					*r++ = (WORD)0;
				else
					*r++ = (WORD)(log(buf[i] * 10) * 2000);
			}
		}
		logger.log(CAT_ALL, L_INFO, "Loaded %lu values from ranks file v.%lu\n", size, m_version);
	}
}

void CRanks::LoadRanks()
{
	string fname(DBWordDir);
	fname.append(IncrementalCitations ? "/ranksd" : "/ranks");
	m_file = open(fname.c_str(), O_RDONLY);
	if (m_file != -1)
	{
		if (read(m_file, &m_version, sizeof(m_version)) == sizeof(m_version))
		{
			LoadRanks(m_file);
		}
		else
		{
			logger.log(CAT_FILE, L_WARN, "Can't read from file file %s: %s", fname.c_str(), strerror(errno));
		}
	}
	else
	{
		logger.log(CAT_FILE, L_WARN, "Can't open file %s: %s\n", fname.c_str(), strerror(errno));
	}
}

int CRanks::ReLoad()
{
	if (m_file != -1)
	{
		CLocker lock(&m_mutex);
		ULONG version;
		lseek(m_file, 0, SEEK_SET);
		if (read(m_file, &version, sizeof(version)) == sizeof(version))
		{
			if (version != m_version)
			{
				// index computed new ranks
				logger.log(CAT_FILE, L_DEBUG, "Reloading ranks (version was %d, now %d)\n", m_version, version);
				m_version = version;
				LoadRanks(m_file);
				cache.ClearCache();
				return 1; // changed
			}
			else
				return 0; // not changed
		}
		else
		{
			logger.log(CAT_FILE, L_ERR, "Can't read ranks file: %s\n", strerror(errno));
			return -1; // error
		}
	}
	else
		return -1; // no opened file :(
}
