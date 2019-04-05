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

/*  $Id: cache.cpp,v 1.10 2002/06/18 12:23:22 kir Exp $
 *   Author : Igor Sukhih
 *	Implemented classes: CCache
 */

#include "cache.h"
#include "defines.h"
#include "string.h"
#include "md5.h"
#include "timer.h"
#include "daemon.h"

ULONG CCache::GetUrls(ULONG nFirst, CUrlW* urls, ULONG ucnt, int sites)
{
	ULONG cnt = 0, site_id;
	CUrlW* purls = urls;
	ULONG i;
	for (i = 0; i < nFirst; i++)
	{
		site_id = purls->m_siteID;
		while(site_id == purls->m_siteID)
		{
			if (cnt >= ucnt)
			{
				if (sites)
				{
					return i;
				}
				else
				{
					return cnt;
				}
			}
			cnt++;
			purls++;
		}
	}
	if (sites)
	{
		return i;
	}
	else
	{
		return cnt;
	}
}

char * CCache::GetCached(ULONG key, ULONG n)
{
	map<ULONG, CCacheData>::iterator it;

	if (!m_status || key == 0)
		return NULL;

	{
		CLocker lock(&m_mutex);
		// Get from local cache
		if ((it = m_cache.find(key)) != m_cache.end())
		{
			if (*(ULONG*)it->second.m_urls)
			{
				if (GetUrls(n, (CUrlW*)(it->second.m_urls + 6 * sizeof(ULONG)),
					*((ULONG*)(it->second.m_urls) + 2), 1) < n)
				{
					return NULL;
				}
			}
			else
			{
				if ((*((ULONG*)(it->second.m_urls) + 2)) < n)
				{
					return NULL;
				}
			}
			it->second.m_cnt++;
			return it->second.m_urls;
		}
	}
	// Get from global cache
	char *urls = NULL;
	CSQLParam p;
	p.AddParam(&key);
	CSQLQuery *sqlquery = m_database->m_sqlquery->GetCachedS(&p);
	CSQLAnswer *answ = m_database->sql_query(sqlquery);

	if (answ && answ->FetchRow())
	{
		CLocker lock(&m_mutex);

		CCacheData data;
		data.m_cnt = 0;
		data.m_saved = 1;
		if (m_cache.size() >= m_localsize)
		{
			Clear();
		}
		ULONG ulen = answ->GetLength(0);
		data.m_ulen = ulen;
		urls = data.m_urls = new char[ulen];
		memcpy(data.m_urls, answ->GetColumn(0), ulen);

		delete answ;
		if (*(ULONG*)data.m_urls)
		{
			if (GetUrls(n, (CUrlW*)(data.m_urls + 6 * sizeof(ULONG)),
					*((ULONG*)(data.m_urls) + 2), 1))
			{
				delete urls;
				return NULL;
			}
		}
		else
		{
			if (n > (*((ULONG*)(data.m_urls) + 2)))
			{
				delete urls;
				return NULL;
			}
		}

		m_cache[key] = data;
		return urls;
	}
	delete answ;
	return NULL;
}


void CCache::Clear()
{
	map<ULONG, CCacheData>::iterator it, it_tmp;

	SaveCache();
	it = it_tmp = m_cache.begin();
	int cnt = it->second.m_cnt;
	for(; it != m_cache.end(); it++)
	{
		if (cnt <= it->second.m_cnt)
		{
			if (it->second.m_tm < it_tmp->second.m_tm)
			{
				cnt = it->second.m_cnt;
				it_tmp = it;
			}
		}
	}

	delete it_tmp->second.m_urls;
	m_cache.erase(it_tmp);
}

void CCache::AddToCache(ULONG key, ULONG totalSiteCount, int rescount, ULONG nTotal,
	ULONG urlCount, int complexPhrase, char *urls, int gr, CStringSet *LightWords)
{
	if (!m_status)
		return;
	map<ULONG, CCacheData>::iterator it;

	// Get from local cache
	CLocker lock(&m_mutex);
	if ((it = m_cache.find(key)) == m_cache.end())
	{
		ULONG ulen = rescount * sizeof(CUrlW) + sizeof(ULONG) * 6 + LightWords->size() * sizeof(ULONG) * 2;
		char *buf = new char[ulen];
		ULONG *pbuf = (ULONG*)buf;
		*pbuf = gr; pbuf++;
		*pbuf = totalSiteCount; pbuf++;
		*pbuf = rescount; pbuf++;
		*pbuf = nTotal; pbuf++;
		*pbuf = urlCount; pbuf++;
		*pbuf = complexPhrase; pbuf++;
		memcpy((char*)pbuf, urls, rescount * sizeof(CUrlW));
		pbuf = (ULONG*)((char*)pbuf + (rescount * sizeof(CUrlW)));
		*pbuf = LightWords->size(); pbuf++;
		for (CStringSet::iterator it = LightWords->begin(); it != LightWords->end(); it++)
		{
			CWordStat& s = it->second;
			*pbuf = s.m_urls; pbuf++;
			*pbuf = s.m_total; pbuf++;
		}

		CCacheData cdata;

		cdata.m_urls = buf;
		cdata.m_tm = now();
		cdata.m_ulen = ulen;
		cdata.m_saved = 0;
		cdata.m_cnt = 0;

		if (m_cache.size() >= m_localsize)
		{
			Clear();
		}
		m_cache[key] = cdata;
	}
}

int CCache::SaveCache()
{
	map<ULONG, CCacheData>::iterator it;
	for(it = m_cache.begin(); it != m_cache.end(); it++)
	{
		if (!it->second.m_saved)
		{
			ULONG key = it->first;
			it->second.m_saved = 1;
			CSQLParam p;
			p.AddParam(&key);
			p.AddParamEsc(it->second.m_urls, it->second.m_ulen);
			CSQLQuery *sqlquery = m_database->m_sqlquery->AddToCacheI(&p);
			m_database->sql_real_query(sqlquery);
		}
	}
	return 0;
}

void CCache::ClearCache()
{
	map<ULONG, CCacheData>::iterator it;
	CLocker lock(&m_mutex);
	for(it = m_cache.begin(); it != m_cache.end(); it++)
	{
		delete it->second.m_urls;
	}
	m_cache.clear();
}

