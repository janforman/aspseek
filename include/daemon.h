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

/*  $Id: daemon.h,v 1.17 2002/05/14 11:47:15 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _DAEMON_H_
#define _DAEMON_H_

#include "event.h"
#include "sock.h"
#include <pthread.h>
#include <string>
#ifdef UNICODE
#include "ucharset.h"
#endif

using std::string;

class CSQLDatabase;
class CSearchExpression;

class CSrcDocumentS : public CSrcDocument
{
public:
	void Send(CBufferedSocket& socket);
#ifdef UNICODE
	void RecodeTo(CCharsetB* charset);
	void GetHilightPos(CCharsetB* charset, CStringSet& lightWords);
#endif
};

/// This class is obsolete
/* So get rid of it ;)
class CMyDataSource : public CDataSource
{
public:
	CSQLDatabase* m_database;

public:
	CMyDataSource(CSQLDatabase* db)
	{
		m_database = db;
	}
	virtual void GetCloneList(const char *crc, CUrlW* origin, CSrcDocVector &docs);
};
*/

class CWorkerThreadList;

/// Class CWorkerThread is instantiated when new request is processed and no other threads are available
class CWorkerThread
{
public:
	CEvent m_req;			///< CEvent object, posted on request
	pthread_t m_thread;		///< Thread ID
	int m_socket;			///< Socket returned by accept
	CSQLDatabase* m_database;	///< Database associated with thread
	CWorkerThread* m_next;		///< Next CWorkerThread in the linked list of threads
	CWorkerThread** m_prev;		///< Pointer to the location of pointer to this CWorkerThread in the preivious object linked list of threads
	CWorkerThreadList* m_parent;	///< Pointer to the linked list object holding worker threads

public:
	CWorkerThread()
	{
		m_next = NULL;
		m_prev = NULL;
		m_parent = NULL;
		m_thread = 0;
		m_socket = 0;
	}
	/// This method removes current object from linked list
	CWorkerThread* Remove()
	{
		if (m_prev)
		{
			*m_prev = m_next;
		}
		return this;
	}
	/// This method inserts current object after specified in the linked list
	void Insert(CWorkerThread** prev)
	{
		m_next = *prev;
		m_prev = prev;
		*m_prev = this;
	}
};

#define MAX_WAITING_SOCKETS 1024

/**
 * Class CWorkerThreadList holds linked list of worker thread objects
 * and loop buffer of sockets that waits for processing.
 * It limits maximum number of worker threads by value of MaxThreads.
 * Instatiated once as local variable, shared by all worker threads.
 */
class CWorkerThreadList
{
public:
	pthread_mutex_t m_mutex;			///< Mutex protecting linked list
	CWorkerThread* m_first;				///< First worker thread in the list
	int m_waitingSockets[MAX_WAITING_SOCKETS];	///< Loop buffer for sockets that are waiting for processing
	int m_firstWaiting, m_lastWaiting;		///< Indexes of first and last sockets in loop buffer
	int m_maxThreads;				///< Maximal number of worker threads
	int m_threads;					///< Current number of worker threads

public:
	CWorkerThreadList()
	{
		pthread_mutex_init(&m_mutex, NULL);
		m_first = NULL;
		m_firstWaiting = m_lastWaiting = 0;
		m_threads = 0;
		m_maxThreads = 10;
	}
	~CWorkerThreadList()
	{
		pthread_mutex_destroy(&m_mutex);
	}
	void InsertSocket(int socket)
	{
		m_waitingSockets[m_lastWaiting++] = socket;
		m_lastWaiting %= MAX_WAITING_SOCKETS;
	}
	/// Returns 1 if there are at least one waiting socket in loop buffer
	int IsWaiting()
	{
		return m_firstWaiting != m_lastWaiting;
	}
	/// Removes first waiting socket from loop buffer and returns it
	int PopFirstWaiting()
	{
		int sock = m_waitingSockets[m_firstWaiting++];
		m_firstWaiting %= MAX_WAITING_SOCKETS;
		return sock;
	}
	/// Process incoming request, source of request is socket "csock"
	void processReq(CSQLDatabase* database, int csock);
};

/** Auxiliary class for simple mutex locking.
 * Locks mutex, specified by parameter of constructor, and unlocks it
 * at destructor, i. e. at the end of scope, where variable of this class
 * is declared.
 */
class CLocker
{
public:
	pthread_mutex_t* m_mutex;
public:
	void Init(pthread_mutex_t* mutex)
	{
		m_mutex = mutex;
		pthread_mutex_lock(mutex);
	}
	CLocker(pthread_mutex_t* mutex)
	{
		Init(mutex);
	}
	~CLocker()
	{
		pthread_mutex_unlock(m_mutex);
	}
};

/// Class describing search context. Instantiated once for each worker thread as local variable
class CSearchContext
{
public:
	CSQLDatabase* m_database;		///< Source database
	CBufferedSocket m_socket;		///< Buffered socket for current request
	string m_error;				///< Query error message
#ifdef UNICODE
	CCharsetB* m_outCharset;
	CStringSet* m_lightWords;
#endif

public:
	CSearchContext(CSQLDatabase* database, int socket)
	{
		m_database = database;
		m_socket = socket;
	}
	/// Starts processing extra requests from client
	void ProcessCommands(CSearchExpression* src);
	void GetDocuments();
	/// Retrieves URL source from database and sends it to socket
	void PrintCached(const char* url, CSearchExpression* expr);
};

class CSiteWeight
{
public:
	ULONG m_siteID;
	int m_weight;

public:
	CSiteWeight(ULONG siteID, int weight)
	{
		m_siteID = siteID;
		m_weight = weight;
	}
	int operator<(const CSiteWeight& sw) const
	{
		return m_siteID < sw.m_siteID;
	}
};

typedef vector<CSiteWeight> CSiteWeights;

extern CSiteWeights siteWeights;
extern int IncrementalCitations;
extern int CompactStorage;

#endif
