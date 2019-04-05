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

/*  $Id: hrefs.cpp,v 1.12 2002/01/09 11:50:15 kir Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CStoredHrefs
*/

#include "hrefs.h"
#include "index.h"
#include "filters.h"
#include "logger.h"
#include "sqldbi.h"

CStoredHrefs StoredHrefs;
CStringToHrefMap Hrefs;

void AddHref(char *href,int referrer,int hops, int delta)
{
	CUrl url;
	if (url.ParseURL(href))
	{
		logger.log(CAT_ALL, L_WARNING , "Bad url <%s>\n", href);
		return;
	}
	char srv[STRSIZ];
	sprintf(srv, "%s://%s/", url.m_schema, url.m_hostinfo);
	AddHref(href, referrer, hops, srv, delta);
}

void AddHref(char *href,int referrer,int hops, const char* server, int delta)
{
	if (strlen(href) <= MAX_URL_LEN)
	{
		CHref& hr = Hrefs[href];
		hr.m_referrer = referrer;
		hr.m_hops = hops;
		hr.m_server = server;
		hr.m_delta = delta;
	}
}

void CStoredHrefs::AddHref(const char* url, ULONG lost, ULONG urlID)
{
	CLocker lock(this);
	m_lost += lost;
	iterator it = find(url);
	if (it == end())
	{
		if (size() >= m_maxSize)
		{
			m_lru->m_lru->m_mru = m_lru->m_mru;
			m_lru->m_mru->m_lru = m_lru->m_lru;
			CHrefInfo* lru = m_lru->m_mru;
			CURLName name = *(m_lru->m_this);
			erase(name);
			m_lru = lru;
		}
		CHrefInfo& winfo = (*this)[url];
		it = find(url);
		winfo.m_urlID = urlID;
		winfo.m_this = &it->first;
		winfo.m_urlID = urlID;
		if (m_lru == NULL)
		{
			m_lru = &winfo;
			winfo.m_lru = &winfo;
			winfo.m_mru = &winfo;
		}
		else
		{
			winfo.m_mru = m_lru;
			winfo.m_lru = m_lru->m_lru;
			m_lru->m_lru->m_mru = &winfo;
			m_lru->m_lru = &winfo;
		}
	}
	else
	{
		MoveMRU(it);
	}
}

void CStoredHrefs::MoveMRU(iterator& it)
{
	CHrefInfo& winfo = it->second;
	if (winfo.m_lru)
	{
		winfo.m_lru->m_mru = winfo.m_mru;
	}
	if (winfo.m_mru)
	{
		winfo.m_mru->m_lru = winfo.m_lru;
	}
	if (m_lru == &winfo)
	{
		m_lru = winfo.m_mru;
	}
	winfo.m_mru = m_lru;
	winfo.m_lru = m_lru->m_lru;
	m_lru->m_lru->m_mru = &winfo;
	m_lru->m_lru = &winfo;
}

int CStoredHrefs::Contains(const char* url)
{
	CLocker lock(this);
	m_queries++;
	iterator it = find(url);
	if (it != end())
	{
		m_hits++;
		MoveMRU(it);
		return 1;
	}
	else
	{
		return 0;
	}
}
void CStoredHrefs::AddHref1(const char* url, ULONG urlID)
{
	if (size() >= m_maxSize)
	{
		m_lru->m_lru->m_mru = m_lru->m_mru;
		m_lru->m_mru->m_lru = m_lru->m_lru;
		CHrefInfo* lru = m_lru->m_mru;
		CURLName name = *(m_lru->m_this);
		erase(name);
		m_lru = lru;
	}
	CHrefInfo& winfo = (*this)[url];
	winfo.m_urlID = urlID;
	iterator it = find(url);
	winfo.m_this = &it->first;
	if (m_lru == NULL)
	{
		m_lru = &winfo;
		winfo.m_lru = &winfo;
		winfo.m_mru = &winfo;
	}
	else
	{
		winfo.m_mru = m_lru;
		winfo.m_lru = m_lru->m_lru;
		m_lru->m_lru->m_mru = &winfo;
		m_lru->m_lru = &winfo;
	}
}

CHrefInfo* CStoredHrefs::AddEmptyHref(const char* url)
{
	if (size() >= m_maxSize)
	{
		CHrefInfo* lrut = m_lru;
		if (lrut == NULL) goto uw;
		while (lrut->m_refs)
		{
			lrut = lrut->m_mru;
			if (lrut == m_lru)
			{
uw:
				m_waits1++;
				pthread_mutex_unlock(&m_mutex);
				usleep(1000000);
				return NULL;
			}
		}
		CHrefInfo* lru = lrut->m_mru;
		if (lru != lrut)
		{
			lrut->m_lru->m_mru = lrut->m_mru;
			lrut->m_mru->m_lru = lrut->m_lru;
		}
		CURLName name = *(lrut->m_this);
		erase(name);
		if (lrut == m_lru)
		{
			m_lru = lrut == lru ? NULL : lru;
		}
	}
	return &((*this)[url]);
}

void CStoredHrefs::SetHref(const char* url, CHrefInfo* hinfo)
{
	iterator it = find(url);
	hinfo->m_this = &it->first;
	if (m_lru == NULL)
	{
		m_lru = hinfo;
		hinfo->m_lru = hinfo;
		hinfo->m_mru = hinfo;
	}
	else
	{
		hinfo->m_mru = m_lru;
		hinfo->m_lru = m_lru->m_lru;
		m_lru->m_lru->m_mru = hinfo;
		m_lru->m_lru = hinfo;
	}
}

ULONG CStoredHrefs::GetHref(CSQLDatabaseI* database, CServer* srv, const char* chref, int referrer, int hops, const char* server, int hopsord, int delta)
{
	if (srv && srv->m_replace)
	{
		char nhref[MAX_URL_LEN + 2];
		if (srv->m_replace->Replace(chref, nhref, MAX_URL_LEN + 1))
		{
			chref = nhref;
		}
	}
	return GetHref(database, chref, referrer, hops, server, hopsord, delta);
}

ULONG CStoredHrefs::GetHref(CSQLDatabaseI* database, const char* chref, int referrer, int hops, const char* server, int hopsord, int delta)
{
	if (strlen(chref) <= MAX_URL_LEN)
	{
start:
		ULONG urlID;
		pthread_mutex_lock(&m_mutex);
		m_queries++;
		iterator it = find(chref);
		if (it != end())
		{
			m_hits++;
			CHrefInfo& hinfo = it->second;
			if (hinfo.m_this == NULL)
			{
				CEventLink evLink;
				evLink.m_next = hinfo.m_waiting;
				hinfo.m_waiting = &evLink;
				hinfo.m_refs++;
				m_waits++;
				pthread_mutex_unlock(&m_mutex);
				evLink.m_event.Wait();
				pthread_mutex_lock(&m_mutex);
				hinfo.m_refs--;
				if (hinfo.m_this == NULL)
				{
					printf("Bug\n");
				}
				urlID = hinfo.m_urlID;
			}
			else
			{
				MoveMRU(it);
				urlID = hinfo.m_urlID;
			}
		}
		else
		{
			CHref href;
			href.m_delta = delta;
			href.m_hops = hops;
			href.m_referrer = referrer;
			CHrefInfo* hinfo = AddEmptyHref(chref);
			if (hinfo == NULL) goto start;
			hinfo->m_refs++;
			pthread_mutex_unlock(&m_mutex);
			urlID = database->StoreHref2(chref, href, server, hopsord, m_lost);
			pthread_mutex_lock(&m_mutex);
			hinfo->m_urlID = urlID;
			SetHref(chref, hinfo);
			for (CEventLink* ev = hinfo->m_waiting; ev; ev = ev->m_next)
			{
				ev->m_event.Post();
			}
			hinfo->m_waiting = NULL;
			hinfo->m_refs--;
		}
		pthread_mutex_unlock(&m_mutex);
		return urlID;
	}
	else
	{
		return 0;
	}
}

/* old version
ULONG CStoredHrefs::GetHref(CSQLDatabaseI* database, const char* chref, int referrer, int hops, const char* server, int hopsord, int delta)
{
	if (strlen(chref) <= MAX_URL_LEN)
	{
		CLocker lock(this);
		m_queries++;
		iterator it = find(chref);
		if (it != end())
		{
			m_hits++;
			MoveMRU(it);
			return it->second.m_urlID;
		}
		else
		{
			CHref href;
			href.m_delta = delta;
			href.m_hops = hops;
			href.m_referrer = referrer;
			ULONG urlID = database->StoreHref2(chref, href, server, hopsord, m_lost);
			AddHref1(chref, urlID);
			return urlID;
		}
	}
	else
	{
		return 0;
	}
}
*/

/* test stuff - commented out
typedef struct
{
	CStoredHrefs* hrefCache;
	CSQLDatabaseI* database;
	vector<CURLName*> hrefs;
	vector<CHref> hrefIDs;
} TestC;

typedef struct
{
	TestC* test;
	ULONG step;
} TestS;

void* TestHrefCache1(void* ptr)
{
	TestS* testS = (TestS*)ptr;
	TestC* test = testS->test;
	int sz = test->hrefs.size();
	int ind = 0;
	for (int i = 0; i < sz; i++)
	{
		const char* url = test->hrefs[ind]->c_str();
		CHref& href = test->hrefIDs[ind];
		ind += testS->step;
		if (ind >= sz) ind -= sz;
		ULONG hrefID = test->hrefCache->GetHref(test->database, url, href.m_referrer, href.m_hops, href.m_server.c_str(), 0, 0);
		if (hrefID != href.m_urlID)
		{
			printf("Error, actual ID: %lu, cached %lu\n", href.m_urlID, hrefID);
		}
	}
	// FIXME!!!
	return NULL;
}

#define NUM_THREADS 50

void TestHrefCacheN(CSQLDatabaseI* database)
{
	TestC test;
	test.hrefCache = &StoredHrefs;
	test.database = database;
	TestS tests[NUM_THREADS];
	ULONG primes[NUM_THREADS];
	primes[0] = 1;
	primes[1] = 2;
	CSQLQuery *sqlquery = database->m_sqlquery->SetQuery("SELECT url, url_id, referrer, hops from urlword LIMIT 1000");
	CSQLAnswer* answ = database->sql_query(sqlquery);
	while (answ->FetchRow())
	{
		CHref href;
		const char* url = answ->GetColumn(0);
		answ->GetColumn(1, href.m_urlID);
		answ->GetColumn(2, href.m_referrer);
		answ->GetColumn(3, href.m_hops);
		href.m_server = FindServer(url)->m_url;
		test.hrefs.push_back(new CURLName(url));
		test.hrefIDs.push_back(href);
	}
	delete answ;
	pthread_t threads[NUM_THREADS];
	int n;
	ULONG pr = 3;
	for (n = 2; n < NUM_THREADS; n++)
	{
np:
		for (ULONG* ppr = primes + 1; *ppr * *ppr <= pr; ppr++)
		{
			if ((pr % *ppr) == 0)
			{
				pr += 2;
				goto np;
			}
		}
		primes[n] = pr;
		pr += 2;
	}
	for (n = 0; n < NUM_THREADS; n++)
	{
		tests[n].test = &test;
		tests[n].step = primes[n];
		if (pthread_create(threads + n, NULL, ::TestHrefCache1, tests + n) != 0) break;
	}
	for (int i = 0; i < n; i++)
	{
		void* res;
		pthread_join(threads[i], &res);
	}
	printf("Size: %lu. Queries: %lu, hits: %lu, misses: %lu, waits: %lu, waits of LRU: %lu\n", StoredHrefs.size(), StoredHrefs.m_hits + StoredHrefs.m_lost, StoredHrefs.m_hits, StoredHrefs.m_lost, StoredHrefs.m_waits, StoredHrefs.m_waits1);
}
*/
