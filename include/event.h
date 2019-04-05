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

/*  $Id: event.h,v 1.3 2002/01/28 10:44:17 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _EVENT_H_
#define _EVENT_H_

#include <pthread.h>
#include "defines.h"
#include <errno.h>

/// Class used for communication between threads
class CEvent
{
public:
	pthread_mutex_t		m_mutex;	///< Mutex sem & attribute
	pthread_condattr_t	m_cattr;	///< CondVar attribute
	pthread_cond_t		m_cv;		///< Condition variable
	int			m_signal;	///< Signal

public:
	CEvent()
	{
		if (pthread_condattr_init(&m_cattr)) return;
		if (pthread_mutex_init(&m_mutex, NULL))
		{
			pthread_condattr_destroy(&m_cattr);
			return;
		}
		if (pthread_cond_init(&m_cv, &m_cattr))
		{
			pthread_condattr_destroy(&m_cattr);
			return;
		}
		pthread_condattr_destroy(&m_cattr);
		m_signal = 0;
	}
	~CEvent()
	{
		pthread_mutex_destroy(&m_mutex);
		pthread_cond_destroy(&m_cv);
	}
	/// Post event
	void Post()
	{
		Lock();
		PostN();
		Unlock();
	}
	/// Post event without locking mutex
	void PostN()
	{
		if (m_signal == 0)
		{
			m_signal = 1;
			pthread_cond_signal(&m_cv);
		}
	}
	void Lock()
	{
		pthread_mutex_lock(&m_mutex);
	}
	void Unlock()
	{
		pthread_mutex_unlock(&m_mutex);
	}
	/// Wait for event
	int Wait(ULONG timeOut = 0xFFFFFFFF)
	{
		struct timespec to;
		int	ret = 0;
//		int	CurTime;

#define NANOSECS 1000000000 ///< number of nano secs in a second

		pthread_mutex_lock(&m_mutex);
		switch (timeOut)
		{
			case 0xFFFFFFFF:
			while (m_signal == 0)
			{
				if (( ret = pthread_cond_wait(&m_cv, &m_mutex)) != 0)
				{
					if (ret != EINTR)
					{
						break;
					}
				}
			}
			break;

			case 0:
				sched_yield();
				break;

			default:
				if ((to.tv_sec = timeOut / 1000) < 1)
					to.tv_sec++; /// At least 1 sec for Solaris threads
				to.tv_sec += time(NULL);
				to.tv_nsec = 0;
				while (m_signal == 0)
				{
				if ((ret = pthread_cond_timedwait(&m_cv, &m_mutex, &to)) != 0)
				{
					if (/* ( ret == ETIME ) || */( ret == ETIMEDOUT ))
						break;

					if (ret != EINTR)
					{
						break;
					}
				}
			}
			break;
		}
		m_signal = 0;

		pthread_mutex_unlock(&m_mutex);
		if (ret) return -1;
		return ret;
	}
};

class CEventLink
{
public:
	CEvent m_event;
	CEventLink* m_next;

public:
	CEventLink()
	{
		m_next = NULL;
	}
};

#endif /* _EVENT_H_ */
