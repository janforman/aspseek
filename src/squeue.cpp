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

/*  $Id: squeue.cpp,v 1.47 2002/04/20 11:09:26 al Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CSiteUrls, CSitesQueue, CMySQLDatabaseI
*/

#include <sys/time.h>
#include "squeue.h"
#include "charsets.h"
#include "sqldb.h"
#include "parse.h"
#include "hrefs.h"
#include "rank.h"
#include "index.h"
#include "config.h"
#include "logger.h"
#include "timer.h"
#include "lastmod.h"
//#include "geo.h"

hash_set<CURLName> Sites;
CMapStringToStringVec Robots;
int max_failed_current = 10;

void CSiteUrls::SetLastAccess()
{
	if (m_minDelay)
	{
		gettimeofday(&m_lastAccess, NULL);
	}
}

int CSiteUrls::WillBeReady()
{
	if (m_minDelay)
	{
		double d;
		struct timeval tm;
		gettimeofday(&tm, NULL);
		d = m_minDelay - timedif(tm, m_lastAccess);
		return d > 0 ? (int)(d * 1000) + 1 : 0;
	}
	else
	{
		return 0;
	}
}

void CSitesQueue::Insert(CSiteUrls* urls, CSiteUrls*& current)
{
	if (current == NULL)
	{
		current = urls;
		urls->m_next = urls;
		urls->m_prev = urls;
	}
	else
	{
		urls->m_next = current;
		urls->m_prev = current->m_prev;
		current->m_prev->m_next = urls;
		current->m_prev = urls;
	}
}

ULONG CSiteUrls::FetchPrevious()
{
	char fname[1000];
	sprintf(fname, "%s/siteurls", DBWordDir.c_str());
	FILE* file = fopen(fname, "r");
	m_indexed_this_site = 0;
	if (file)
	{
		if (m_siteID)
		{
			ULONG msite;
			fseek(file, 0, SEEK_END);
			msite = (ftell(file) / sizeof(ULONG));
			if (msite >= m_siteID)
			{
				fseek(file, (m_siteID - 1) * sizeof(ULONG), SEEK_SET);
				fread(&m_indexed_this_site, sizeof(ULONG), 1, file);
			}
		}
		fclose(file);
	}
	return m_indexed_this_site;
}

void CSiteUrls::SetDefaults(CServer* srv)
{
	if (srv)
	{
		m_max_urls_this_run = (ULONG) srv->m_max_urls_per_site_per_index;
		m_max_urls_this_site = (ULONG) srv->m_max_urls_per_site;
		m_minDelay = srv->m_minDelay;
		m_maxDocs = srv->m_maxDocs;
	}
	FetchPrevious();
}

int CSiteUrls::LimitReached(int new_url)
{
	if (new_url && (m_max_urls_this_site != (ULONG) -1) &&
		(m_indexed_this_site >= m_max_urls_this_site))
	{
		return 2;
	}
	if ((m_max_urls_this_run != (ULONG) -1) &&
		(m_indexed_this_run >= m_max_urls_this_run))
	{
		return 1;
	}
	return 0;
}

void CSitesQueue::Insert(CSiteUrls* urls)
{
	Insert(urls, m_current);
}

void CSitesQueue::AddURL(ULONG site_id, ULONG url_id, CSQLDatabase* database)
{
	CSiteUrls* urls;
	iterator it = find(site_id);
	if (it == end())
	{
		CServer* srv=NULL;
		char site[300];
		if (database->GetSite(site_id, site)) srv = FindServer(site);
		urls = &(*this)[site_id];
		urls->m_siteID = site_id;
		urls->SetDefaults(srv);
		m_inactiveSize++;
		Insert(urls);
	}
	else
	{
		urls = &it->second;
		if ((urls->m_count == 0) && (urls->m_state == 0))
		{
			m_inactiveSize++;
			m_failedConns += urls->m_connFailed;
			Insert(urls, urls->m_connFailed ? m_currentFail : m_current);
		}
	}
	urls->Insert(url_id);
	m_qDocs++;
}

void CSitesQueue::AddURL(ULONG site_id, ULONG url_id, const char* url)
{
	CSiteUrls* urls;
	iterator it = find(site_id);
	if (it == end())
	{
		CServer* srv;
		srv = url ? FindServer(url) : NULL;
		urls = &(*this)[site_id];
		urls->m_siteID = site_id;
		urls->SetDefaults(srv);
		m_inactiveSize++;
		Insert(urls);
	}
	else
	{
		urls = &it->second;
		if ((urls->m_count == 0) && (urls->m_state == 0))
		{
			m_inactiveSize++;
			m_failedConns += urls->m_connFailed;
			Insert(urls, urls->m_connFailed ? m_currentFail : m_current);
		}
	}
	urls->Insert(url_id);
	m_qDocs++;
}

ULONG CSitesQueue::ExtractURL()
{
	int f = (m_currentFail && (m_failedProcessed < max_failed_current)) || (m_current == NULL);
	CSiteUrls*& current = f ? m_currentFail : m_current;
	if (current)
	{
		int ms;
		if ((ms = current->WillBeReady()) != 0)
		{
			current = current->m_next;
			return ~0;
		}
		CSiteUrls* urls = current->Remove();
		current = current->m_next == current ? NULL : current->m_next;
		m_inactiveSize--;
		m_activeSize++;
		urls->m_state = 1;
		if (f)
		{
			m_failedProcessed++;
		}
		return urls->RemoveFirst();
	}
	else
	{
		return 0;
	}
}

void CSitesQueue::DeleteURL(ULONG siteID, int index_result)
{
	CSiteUrls& urls = (*this)[siteID];
	m_activeSize--;
	urls.m_state = 0;
	int lastFailed = urls.m_connFailed;
	urls.m_connFailed = (index_result == NET_CANT_CONNECT) || (index_result == NET_CANT_RESOLVE) ? 1 : 0;
	if (urls.m_count != 0)
	{
		m_inactiveSize++;
		m_failedConns += (urls.m_connFailed - lastFailed);
		CSiteUrls*& current = urls.m_connFailed ? m_currentFail : m_current;
		if ((++urls.m_docCnt >= urls.m_maxDocs) || (index_result < 0))
		{
			urls.m_docCnt = 0;
			Insert(&urls, current);
		}
		else
		{
			Insert(&urls, current);
			current = current->m_prev;
		}
	}
	else
	{
		m_failedConns -= lastFailed;
//		urls.m_connFailed = 0;
	}
	m_failedProcessed -= lastFailed;
	urls.SetLastAccess();
	m_qDocs--;
}

void CSitesQueue::IncrementIndexed(ULONG site_id, const char* url, int new_url)
{
	CSiteUrls* urls;
	iterator it = find(site_id);
	if (it == end())
	{
		CServer* srv;
		srv = url ? FindServer(url) : NULL;
		urls = &(*this)[site_id];
		urls->m_siteID = site_id;
		urls->SetDefaults(srv);
	}
	else
	{
		urls = &it->second;
	}
	urls->m_indexed_this_run++;
	if (new_url)
	{
		urls->m_indexed_this_site++;
	}
}

void CSitesQueue::DecrementIndexed(ULONG site_id, const char* url, int new_url)
{
	CSiteUrls* urls;
	iterator it = find(site_id);
	if (it == end())
	{
		CServer* srv;
		srv = url ? FindServer(url) : NULL;
		urls = &(*this)[site_id];
		urls->m_siteID = site_id;
		urls->SetDefaults(srv);
	}
	else
	{
		urls = &it->second;
	}
	if (!new_url && urls->m_indexed_this_site)
	{
		urls->m_indexed_this_site--;
	}
}

int CSitesQueue::LimitReached(ULONG site_id, const char* url, int new_url)
{
	CSiteUrls* urls;
	iterator it = find(site_id);
	if (it == end())
	{
		CServer* srv;
		srv = url ? FindServer(url) : NULL;
		urls = &(*this)[site_id];
		urls->m_siteID = site_id;
		urls->SetDefaults(srv);
	}
	else
	{
		urls = &it->second;
	}
	int ret = urls->LimitReached(new_url);
	return ret;
}

void CSitesQueue::SaveIndexed()
{
	FILE* file;
	char fname[1000];
	ULONG maxSite = 0;
	CSiteUrls* urls;
	iterator ite = end();
	for (iterator it = begin(); it != ite; it++)
	{
		urls = &it->second;
		if (urls->m_siteID > maxSite) maxSite = urls->m_siteID;
	}
	if (!maxSite)   // Nothing to do...
	{
		return;
	}
	sprintf(fname, "%s/siteurls", DBWordDir.c_str());
	file = fopen(fname, "r+");
	if (file == NULL)
	{
		file = fopen(fname, "w+");
	}
	if (file)
	{
		ULONG ch = 0;
		double htime = 0.0;
		fseek(file, 0, SEEK_END);
		ULONG msite = (ftell(file) / sizeof(ULONG));
		if (msite > maxSite) maxSite = msite;
		if (!maxSite)   // Nothing to do...
		{
			fclose(file);
			return;
		}
		logger.log(CAT_ALL, L_INFO, "Saving site counts file ...");
		{
			CTimerAdd time(htime);
			ULONG* site_urls = new ULONG[maxSite];
			memset(site_urls, 0, maxSite * sizeof(ULONG));
			fseek(file, 0, SEEK_SET);
			fread(site_urls, sizeof(ULONG), msite, file);
			ite = end();
			for (iterator it = begin(); it != ite; it++)
			{
				urls = &it->second;
				site_urls[urls->m_siteID - 1] = urls->m_indexed_this_site;
				ch++;
			}
			fseek(file, 0, SEEK_SET);
			fwrite(site_urls, sizeof(ULONG), maxSite, file);
			fclose(file);
			delete site_urls;
		}
		logger.log(CAT_ALL, L_INFO, " %lu site%s in %0.3f.\n", ch, (ch == 1) ? "" : "s", htime);
	}
}

