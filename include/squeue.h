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

/*  $Id: squeue.h,v 1.32 2002/12/18 18:38:53 kir Exp $
	Author: Alexander F. Avdonkin
*/

#ifndef _SQUEUE_H_
#define _SQUEUE_H_

#include <vector>
#include "my_hash_map"
#include "defines.h"
#include "hrefs.h"
#include "sqldb.h"

class CIPTree;

/**
 * This class represent a chunk of elements in the queue of URLs
 * belonging to the same site
 * Instance of this class can store up to 100 URL IDs
 */
class CUrlLinks
{
public:
	CUrlLinks* m_next;		///< Next element in the queue
	int m_first;			///< Index of URL ID in m_urls to pop for indexing
	int m_last;			///< Index in m_urls, where to put next URL, fetched from database or produced by href
	ULONG m_urls[100];		///< Array of URL IDs

public:
	CUrlLinks()
	{
		m_next = NULL;
		m_first = m_last = 0;
	}
};

/**
 * This class represents a queue of URLs to be indexed soon, belonging to the particular site
 * Instances of this class are organized into looped doubly-linked list
 * Whenever particular site is about to be accessed, instance of this class is removed from the list, when
 * access to the site is finished, instance is inserted to the end of list if 1 or more URLs are left in it
 * Also, instances of this class are values of hash table, from which they never removed during indexing
 */
class CSiteUrls
{
public:
	CUrlLinks* m_first;	///< Pointer to the first chunk of URLs
	CUrlLinks* m_last;	///< Pointer to the last chunk of URLs
	ULONG m_count;		///< Total count of URLs in the queue
	ULONG m_siteID;		///< Site ID of URLs in this queue
	ULONG m_max_urls_this_run;	///< Max num of URLs to index this index run
	ULONG m_indexed_this_run;	///< Total count of URLs that have been indexed this run
	ULONG m_max_urls_this_site;	///< Max num of URLs to index for this site this run
	ULONG m_indexed_this_site;	///< Total count of URLs that have been indexed this site
	int m_minDelay;		///< Minimum interval in seconds between accesses to the site
	int m_maxDocs;
	int m_docCnt;
	int m_state;		///< 1 if site is being processed at this time
	int m_connFailed;	///< 1 if last connection failed
	struct timeval m_lastAccess;	///< Last access time for site, this value is set only if (m_minDelay != 0)
	CSiteUrls* m_next;	///< Pointer to the next URLs queue for other site
	CSiteUrls* m_prev;	///< Pointer to the prev URLs queue for other site

public:
	CSiteUrls()
	{
		m_first = NULL;
		m_last = NULL;
		m_next = NULL;
		m_prev = NULL;
		m_count = 0;
		m_max_urls_this_run = (ULONG) -1;
		m_indexed_this_run = 0;
		m_max_urls_this_site = (ULONG) -1;
		m_indexed_this_site = 0;
		m_minDelay = 0;
		m_state = 0;
		m_lastAccess.tv_sec = m_lastAccess.tv_usec = 0;
		m_maxDocs = 1;
		m_docCnt = 0;
		m_connFailed = 0;
	}
	~CSiteUrls()
	{
		CUrlLinks* r;
		while (m_first)
		{
			r = m_first->m_next;
			delete m_first;
			m_first = r;
		}
	}
	void SetLastAccess();	///< Sets last access time to the current time
	/**
	  * Returns number of milliseconds, in which site
	  * can be accessed again or 0 if it can be accessed now
	  */
	int WillBeReady();
	/// Remove this from the linked list
	CSiteUrls* Remove()
	{
		if (m_next) m_next->m_prev = m_prev;
		if (m_prev) m_prev->m_next = m_next;
		return this;
	}
	/// Inserts new URL ID to the end of site queue
	void Insert(ULONG urlID)
	{
		CUrlLinks* links;
		if (m_last == NULL)
		{
			// This is the first element
			links = new CUrlLinks;
			m_first = m_last = links;
		}
		else
		{
			if ((ULONG)m_last->m_last >= sizeof(m_last->m_urls) / sizeof(m_last->m_urls[0]))
			{
				// Chunk is full, create new one
				links = new CUrlLinks;
				m_last->m_next = links;
				m_last = links;
			}
			else
			{
				links = m_last;
			}
		}
		links->m_urls[links->m_last++] = urlID;
		m_count++;
	}
	ULONG RemoveFirst()
	{
		CUrlLinks* r = m_first;
		if (r)
		{
			if (r->m_first < r->m_last)
			{
				// Chunk is not empty, pop first element
				ULONG res = r->m_urls[r->m_first++];
				if ((ULONG)r->m_first >= sizeof(m_first->m_urls) / sizeof(m_first->m_urls[0]))
				{
					// No room are left in the chunk, remove it from queue and delete
					m_first = m_first->m_next;
					delete r;
					if (m_first == NULL) m_last = NULL;
				}
				m_count--;
				return res;
			}
			else
			{
				return 0;
			}
		}
		else
		{
			return 0;
		}
	}
	ULONG FetchPrevious();
	void SetDefaults(CServer* srv);
	int LimitReached(int new_url);
};

/**
 * Instance of this class stores URL IDs which are to be indexed, grouped by sites
 * Instantiated once as member variable of CMySQLDatabaseI
 * Key of hash table is the site ID, value is the queue of URLs
 */
class CSitesQueue : public hash_map<ULONG, CSiteUrls>
{
public:
	int m_activeSize;		///< Number of sites accessed at this time
	int m_inactiveSize;		///< Number of sites with 1 or more not indexed URLs, not accessed at this time
	int m_failedConns;
	int m_failedProcessed;
	CSiteUrls* m_current;		///< Next site, URLs from which is to be indexed at this time
	CSiteUrls* m_currentFail;	///< Next site, URLs from which is to be indexed at this time
					///< connection to which was failed last time
	ULONG m_qDocs;			///< Total count of URLs in the queue for all sites
	pthread_mutex_t m_mutex;

public:
	CSitesQueue()
	{
		m_current = NULL;
		m_currentFail = NULL;
		m_qDocs = 0;
		m_activeSize = m_inactiveSize = 0;
		m_failedConns = 0;
		m_failedProcessed = 0;
		pthread_mutex_init(&m_mutex, NULL);
	}
	~CSitesQueue()
	{
		pthread_mutex_destroy(&m_mutex);
	}
	/// Inserts URL queue for site to the end of list
	void Insert(CSiteUrls* urls);
	void Insert(CSiteUrls* urls, CSiteUrls*& current);
	/// Adds URL ID to the queue belonging to site_id
	void AddURL(ULONG site_id, ULONG url_id, CSQLDatabase* database);
	void AddURL(ULONG site_id, ULONG url_id, const char* url);
	/// Pops next URL ID to be indexed
	ULONG ExtractURL();
	/// Adds site to the end of queue and sets last access time for it
	void DeleteURL(ULONG siteID, int ignoreOnce = 0);
	void IncrementIndexed(ULONG site_id, const char* url, int new_url);
	void DecrementIndexed(ULONG site_id, const char* url, int new_url);
	int LimitReached(ULONG site_id, const char* url, int new_url);
	void SaveIndexed();
};

/**
 * This class is used as source of URL IDs, indexed at previous indexer run.
 * It uses databases table "tmpurl" as source
 */
class CTmpUrlSource
{
public:
	int m_index;			///< Current index in m_urls
	std::vector<ULONG> m_urls;	///< Array of all URL IDs to index

public:
	CTmpUrlSource()
	{
		m_index = 0;
	}
	ULONG GetNextLink()
	{
		if ((ULONG)m_index < m_urls.size())
		{
			return m_urls[m_index++];
		}
		else
		{
			return 0;
		}
	}
};

class CUrlRanks;
class CUrlRank;

extern hash_set<CURLName> Sites;

extern int max_failed_current;
#endif
