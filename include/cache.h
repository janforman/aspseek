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

/*  $Id: cache.h,v 1.7 2002/01/09 11:50:14 kir Exp $
    Author : Igor Sukhih.
*/

#ifndef _CACHE_
#define _CACHE_
#include <pthread.h>
#include "defines.h"
#include "sqldb.h"
#include <map>
#include "sock.h"
#include "url.h"
#include "crc32.h"

using std::map;

struct CCacheData
{
	int m_tm;
	int m_cnt;
	int m_saved;
	int m_ulen;
	char *m_urls;
};

class CCache
{
public:
	int m_status;
	ULONG m_localsize;
	ULONG m_urlssize;
	pthread_mutex_t m_mutex;
	CSQLDatabase *m_database;
	map <ULONG, CCacheData> m_cache;
public:
	CCache()
	{
		pthread_mutex_init(&m_mutex, NULL);
		m_status = 0;
		m_localsize = 100;
		m_urlssize = 200;
		m_database = NULL;
	}
	~CCache()
	{
		ClearCache();
		pthread_mutex_destroy(&m_mutex);
	}
	void ClearCache();
	void CacheOn()
	{
		m_status = 1;
	}
	void SetDb(CSQLDatabase *database)
	{
		m_database = database;
	}
	char *GetCached(ULONG key, ULONG n);
	void SetLocalSize(int size)
	{
		m_localsize = size;
	}
	void SetUrlsSize(int size)
	{
		m_urlssize = size;
	}
	void AddToCache(ULONG key, ULONG totalSiteCount, int rescount, ULONG nTotal, ULONG urlCount,
			int m_complexPhrase, char *urls, int gr, CStringSet *LightWords);
	int SaveCache();
	void Clear();
	void Delete();
	ULONG GetUrls(ULONG nFirst, CUrlW* purls, ULONG ucnt, int sites = 0);
};
#endif
