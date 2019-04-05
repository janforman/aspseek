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

/*  $Id: index.h,v 1.17 2002/12/18 18:38:53 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _INDEX_H_
#define _INDEX_H_

#include <string.h> // for strerror
#include <vector>
#include <string>
#include <map>
#include <pthread.h>
#include "defines.h"
#include "charsets.h"
#include "parse.h"
#include "hrefs.h"
#include "squeue.h"
#include "wcache.h"
#include "deltas.h"
#include "resolve.h"

#define DEFAULT_LOGNAME "logs.txt"

class CStoredHrefs;

/// This class holds value of maximal bandwidth, beginning from specified time of day
class CBW
{
public:
	int m_start;		///< Beginning of period, in seconds (GMT 0:00)
	ULONG m_maxBandwidth;	///< Maximal value of bandwidth (bytes/sec)
};

/**
 * This class holds day schedule of maximal bandwidth
 * Key of map is the end of period
 */
class CBWSchedule : public std::map<int, CBW>
{
public:
	ULONG m_defaultBandwidth;	///< Value to be used, if current time of day doesn't fall to any of specified periods

public:
	CBWSchedule()
	{
		m_defaultBandwidth = 0;
	}
	void AddInterval(int start, int end, ULONG bandwidth);
	ULONG MaxBandWidth();
};

class CLocker
{
public:
	pthread_mutex_t* m_mutex;
public:
	void Init(pthread_mutex_t* mutex)
	{
		m_mutex = mutex;
		int err;
		if ((err = pthread_mutex_lock(mutex)) != 0)
		{
			printf("Error locking mutex: %s\n", strerror(err));
		}
	}
	CLocker(CDeltaFiles* files, int index)
	{
		Init(files->m_mutex + index);
	}
	CLocker(CStoredHrefs* shrefs)
	{
		Init(&shrefs->m_mutex);
	}
	CLocker(CWordCache* cache)
	{
		Init(&cache->m_mutex);
	}
	CLocker(CSQLDatabase* db)
	{
		Init(&db->mutex);
	}
	CLocker(CResolverList* list)
	{
		Init(&list->m_mutex);
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

#endif /* _INDEX_H_ */
