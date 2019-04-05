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

/*  $Id: timer.h,v 1.3 2002/01/09 11:50:15 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _TIMER_H_
#define _TIMER_H_
#include "sys/time.h"
#include "logger.h"

double timedif(struct timeval& tm1, struct timeval& tm);

/// This returns current time in seconds
time_t now(void);

class CTimer
{
public:
	struct timeval m_start;
	const char* m_message;

public:
	CTimer(const char* message = "Time elapsed: ")
	{
		gettimeofday(&m_start, NULL);
		m_message = message;
	}
	~CTimer()
	{
		struct timeval time;
		gettimeofday(&time, NULL);
		logger.log(CAT_TIME, L_DEBUG, "%s%7.3f seconds\n", m_message, timedif(time, m_start));
	}
};

class CTimer1
{
public:
	struct timeval m_start;
	double* m_result;
public:
	CTimer1(double& result)
	{
		gettimeofday(&m_start, NULL);
		m_result = &result;
		}
	virtual void SaveTime(double time)
	{
		*m_result = time;
	}
	void Finish()
	{
		if (m_result)
		{
			struct timeval time;
			gettimeofday(&time, NULL);
			SaveTime(time.tv_sec - m_start.tv_sec + (time.tv_usec - m_start.tv_usec) / 1000000.0);
			m_result = NULL;
		}
	}
	virtual ~CTimer1()
	{
		Finish();
	}
};

class CTimerAdd : public CTimer1
{
public:
	CTimerAdd(double& result) : CTimer1(result) {};
	virtual void SaveTime(double time)
	{
		*m_result += time;
	}
	virtual ~CTimerAdd()
	{
		Finish();
	}
};
#endif /* _TIMER_H_ */
