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

/*  $Id: wcache.h,v 1.17 2002/12/18 18:38:53 kir Exp $
	Author: Alexander F. Avdonkin
*/

#ifndef _WCACHE_H_
#define _WCACHE_H_

#include <sys/time.h>
#include <pthread.h>
#include "defines.h"
#include "my_hash_set"
#include "my_hash_map"
#ifdef UNICODE
#include "ucharset.h"
#else
#include "charsets.h"
#endif
//#include "parse.h"
//#include "content.h"
//#include "sqldbi.h"

class CDeltaFiles;
class CHrefDeltaFilesR;
class CDocument;

typedef hash_set<pid_t> CPidSet;
class CSQLDatabaseI;
class CEventLink;
class CUrlParam;
class CServer;

/// This is auxiliary class, used as template parameter to STL sort function
class CULONG
{
public:
	ULONG* m_id;

public:
	int operator<(const CULONG& l) const
	{
		return *m_id < *(l.m_id);
	}
};

/// This is auxiliary class, used as template parameter to STL sort function
class CULONG100 : public CULONG
{
public:
	int operator<(const CULONG100& l) const
	{
		return (*m_id % 100) < (*(l.m_id) % 100);
	}
};

/// This class hold information about particular word in word cache, used as value in the hash map
class CWordInfo
{
public:
	ULONG m_wordID;		///< Word ID as in "wordurl" database table
	CWordInfo* m_lru;	///< Pointer to the less recently used word
	CWordInfo* m_mru;	///< Pointer to the more recently used word
#ifdef UNICODE
	const CUWord* m_this;
#else
	const CWord* m_this;	///< Pointer to the key in hash map
#endif
	CEventLink* m_waiting;
	int m_refs;

public:
	CWordInfo()
	{
		m_mru = m_lru = NULL;
		m_this = NULL;
		m_waiting = NULL;
		m_refs = 0;
	}
};

#define SUB_COUNT 50

/// This class is implemented to calculate number of bytes received from the network during last N microseconds
class CByteCounter
{
public:
	unsigned long long m_lastTime;	///< Time of last addition or check
	int m_duration;			///< Time frame duration in microseconds
	int m_windowBytes;		///< Total number of bytes received during time frame
	int m_totalBytes;		///< Total number bytes received
	int m_bytes[SUB_COUNT];		///< Total number bytes received for each of subintervals
	int m_sleeps;			///< Sleep count, used for debugging
	pthread_mutex_t m_mutex;	///< Mutex for protecting whole instance

public:
	CByteCounter()
	{
		m_windowBytes = 0;
		memset(m_bytes, 0, sizeof(m_bytes));
		m_duration = 10000000;
		m_lastTime = 0;
		m_totalBytes = 0;
		m_sleeps = 0;
		pthread_mutex_init(&m_mutex, NULL);
	}
	~CByteCounter()
	{
		pthread_mutex_destroy(&m_mutex);
	}
	long AddBytes(int bytes);
	void WaitBW();
	ULONG GetMaxBW();
};

class CTmpUrlSource;

/**
 * Main indexing class. This class is hash map "word->word_id" with LRU list,
 * it limits total number of words to the specified value. Whenever info
 * about particular word is requested, this word becomes the most recent.
 * If total number of words reaches maximal value, then least recently used
 * word is erased from cache. If word is not found in cache, then its info
 * is retrieved from "wordurl" database table and this word becomes
 * the most recent.
 * This class is used for other indexing actions. Shared by all threads.
 * Instantiated once as local variable.
 */
#ifdef UNICODE
class CWordCache : public hash_map<CUWord, CWordInfo>
#else
class CWordCache : public hash_map<CWord, CWordInfo>
#endif
{
public:
	CSQLDatabaseI* m_database;			///< Database object
#ifdef TWO_CONNS
	CSQLDatabaseI* m_databasew;			///< Second database object for maniputaions with wordurl()
	CSQLDatabaseI* GetDatabaseW() {return m_databasew;};
#else
	CSQLDatabaseI* GetDatabaseW() {return m_database;};
#endif
	CDeltaFiles* m_files;				///< Delta files object, NULL if saving to "real-time" database occurs
	CHrefDeltaFilesR* m_hfiles;
	pthread_mutex_t m_mutex, m_mutexs, m_mutexw, m_mutexc;	///< Mutexes for different protections
	int m_threadCount;				///< Total thread count to run
	CPidSet m_connecting;				///< Set of threads currently connecting
	CPidSet m_gettingHost;				///< Set of threads currently resolving host name
	CWordInfo* m_lru;				///< Least recently used word in cache
	ULONG m_maxSize;				///< Maximal number of words in the cache
	ULONG m_queries, m_hits, m_lost, m_inserts;	///< Number of word info queries and hits respectively
	ULONG m_waits, m_waits1;
	double m_sqlRead, m_sqlWrite, m_sqlUpdate, m_hrefs, m_origin;	///< Statistics times
	int m_flags;					///< Bitmap of indexer flags
	int m_activeLoads;				///< Number of threads, currently processing URL (downloading or saving)
	int m_connections;				///< Number of threads, connected to any site
	ULONG m_totalSize;				///< Total sum of document sizes, received
	ULONG m_totalDocs;				///< Total number of docs received
	int m_startTime;				///< Not used
	ULONG m_partSize;				///< Total sum of document sizes, received, since last statistics save
	ULONG m_partDocs;				///< Total number of docs, received, since last statistics save
	ULONG m_changed;				///< Total number of docs, where changes found from last indexing, since last statistics save
	ULONG m_changed1;				///< Total number of docs, for which server did not return status 304, since last statistics save
	ULONG m_new;					///< Total number of docs, which changed status from 0 to non-zero, since last statistics save
	ULONG m_changed2;				///< Total number of docs, which had non-empty "Last-Modified" for which server did not return status 304, since last statistics save
	ULONG m_logWrites;
	int m_maxfd;					///< Maximal value of socket ID, used for debugging
	struct timeval m_lastSave;			///< Time of last statistics saving
	int local_charset;				///< Index of local character set, to which downloaded URLs are recoded
	CTmpUrlSource* m_tmpSrc;			///< URLs source used to repeat last indexing sequence, used for debugging
	CByteCounter m_byteCounter;			///< Object for calculation byte count for last time frame

public:
	CWordCache(ULONG maxSize);
	~CWordCache();
	int IsRealSaving()
	{
		return m_files == NULL;
	}
	void AddChanged(int changed, int changed1, int unew, int changed2);
	/// Finds URL ID of the specfied URL
	ULONG GetHref(const char* chref, CServer* srv, int referrer, int hops, const char* server, int delta = 0);
	/// Writes info to delta files, to exclude specified URL from reverse index
	void DeleteWordsFromURL(ULONG urlID, ULONG siteID, CServer* curSrv);
	/// Saves specified words info for the specified URL to the delta files
	void SaveWords(CULONG* words, CULONG* ewords, ULONG urlID, ULONG siteID);
	void SaveHrefs(ULONG* hrefs, ULONG size, ULONG* oldHrefs, ULONG oldSize, ULONG urlID, ULONG siteID);
	void AddRedir(CUrlParam* up);
	void CancelConnects();
	void BeginConnect();
	void EndConnect();
	void BeginGetHost(pid_t thread);
	void EndGetHost(pid_t thread);
	/// RealIndex indexes url to the "real-time" database
	void RealIndex(const char* url, int addtospace);
	void Index();			///< Indexes URLs by single thread
	void IndexN(int num_threads);	///< Indexes URLs by specified number of threads
	void* Index1(int thread);	///< Main thread funciton for indexing
	CDocument* GetNextDocument();	///< Returns next documents for indexing
//	void IncActiveLoads();
//	void DecActiveLoads();
	void Check();
	int ParseRobots(char *content, char *hostinfo);		///< Parses robots.txt file
	ULONG GetWordID(const CHAR* word);			///< Finds word ID for word using word cache
	void AddSize(int size);
	void IncConnections(int fd);
	void DecConnections();
	void AddSqlTimes(double sqlRead, double sqlWrite, double sqlUpdate);
 void IncrementIndexed(ULONG site_id, const char* url, int new_url);
 int LimitReached(ULONG site_id, const char* url, int new_url);
 void MarkDeleted(ULONG utlID, ULONG site_id, const char* url, int new_url, int val = 1);


};

extern int term;

void TestCacheN(CSQLDatabaseI* database);

#endif
