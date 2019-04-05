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

/*  $Id: rank.h,v 1.13 2002/12/18 18:38:53 kir Exp $
	Author: Alexander F. Avdonkin
*/

#ifndef _RANK_H_
#define _RANK_H_

#include "aspseek-cfg.h"
#include "defines.h"
#include "deltas.h"
#include "my_hash_map"
#include "my_hash_set"

class CUrlRank
{
public:
	ULONG m_urlID;
	ULONG m_out;
	ULONG m_siteID;
	float m_rank, m_rank1;
//	vector<CUrlRank*> m_referrers;
	CUrlRank** m_referrers;
	union
	{
		ULONG m_count;
		CUrlRank** m_refend;
	};

public:
	CUrlRank(ULONG urlID)
	{
		m_urlID = urlID;
		m_siteID = 0;
		m_rank = 1;
		m_out = 0;
		m_referrers = NULL;
		m_count = 0;
	}
	~CUrlRank()
	{
		if (m_referrers)
			delete m_referrers;
	}
	bool operator==(const CUrlRank& rank) const
	{
		return m_urlID == rank.m_urlID;
	}
	bool operator<(const CUrlRank& rank) const
	{
		return m_siteID == rank.m_siteID ? m_urlID < rank.m_urlID : m_siteID < rank.m_siteID;
	}
	float AvgRank()
	{
		return m_rank / m_out;
	}
	void Allocate()
	{
		if (m_count > 0)
		{
			m_referrers = new CUrlRank*[m_count];
			m_refend = m_referrers;
		}
	}
	void Sort();
};

class CUrlRankPtrU
{
public:
	CUrlRank* m_rank;

public:
	int operator<(const CUrlRankPtrU r) const
	{
		int res = (m_rank->m_urlID % NUM_HREF_DELTAS) - (r.m_rank->m_urlID % NUM_HREF_DELTAS);
		if (res == 0)
		{
			return m_rank->m_urlID < r.m_rank->m_urlID;
		}
		else
		{
			return res < 0;
		}
	}
};

class CUrlRankPtrS
{
public:
	const CUrlRank* m_rank;

public:
	bool operator<(const CUrlRankPtrS& r) const
	{
		return m_rank->m_siteID == r.m_rank->m_siteID ? m_rank->m_urlID < r.m_rank->m_urlID : m_rank->m_siteID < r.m_rank->m_siteID;
	}
};

class CUrlRankPtr
{
public:
	const CUrlRank* m_rank;

public:
	bool operator<(const CUrlRankPtr& r) const
	{
		return m_rank->m_rank > r.m_rank->m_rank;
	}
};

class CUrlRankPtrR : public CUrlRankPtr
{
public:
	bool operator<(const CUrlRankPtr& r) const
	{
		return m_rank->m_rank < r.m_rank->m_rank;
	}
};

namespace STL_EXT_NAMESPACE {
	template <> struct hash<CUrlRank>
	{
		size_t operator()(const CUrlRank& __s) const
		{
			return __s.m_urlID;
		}
	};
}

typedef hash_map<ULONG, ULONG> CMapULONGToULONG;

class CUrlRanks : public hash_set<CUrlRank>
{
public:
	ULONG m_total;
	ULONG m_max;
	CMapULONGToULONG m_redir;

public:
	CUrlRanks()
	{
		m_total = 0;
		m_max = 0;
	}
	CUrlRank& operator[](ULONG urlID)
	{
		insert(urlID);
		iterator it = find(urlID);
		return const_cast<CUrlRank&>(*it);
	}
	ULONG Redir(ULONG urlID)
	{
		CMapULONGToULONG::iterator it = m_redir.find(urlID);
		return it == m_redir.end() ? urlID : Redir(it->second);
	}
/*	void AddLinkFromTo(ULONG from, ULONG to)
	{
		CUrlRank& lfrom = (*this)[from];
		CUrlRank& lto = (*this)[to];
		lfrom.m_out++;
		lto.m_referrers.push_back(&lfrom);
		m_total++;
	}*/
	void IncRefCount(ULONG to)
	{
		(*this)[to].m_count++;
		if (to > m_max) m_max = to;
	}
	void AddLinkFromTo(ULONG from, ULONG to)
	{
		CUrlRank& lfrom = (*this)[from];
		CUrlRank& lto = (*this)[to];
		lfrom.m_out++;
		*lto.m_refend++ = &lfrom;
		m_total++;
	}
	void AddLinkFromTo(CUrlRank& from, CUrlRank& to)
	{
		from.m_out++;
		*to.m_refend++ = &from;
		m_total++;
	}
	void Allocate();
	void Iterate();
	void Save();
	void Load();
	void PrintInOut();
	void Sort();
};

#endif /* _RANK_H_ */
