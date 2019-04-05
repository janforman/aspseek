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

/*  $Id: lastmod.h,v 1.13 2002/01/09 11:50:14 kir Exp $
 *  Author: Kir Kolyshkin
 */

#ifndef _LASTMOD_H_
#define _LASTMOD_H_

#include <time.h>
#include <unistd.h>	// for close()
#include <vector>
#include <pthread.h>
#include "defines.h"
#include "datetime.h"
#include "misc.h"

using std::vector;

typedef vector<WORD> CTimeVector;

/** This class holds last modification dates for UrlIDs
 * Date is WORD (2 bytes long unsigned) value, holds
 * number of hours since OUR_EPOCH, which is defined
 * in datetime.h. WORD is enough to save values for period
 * of about 7 years and a half.
 */
class CLastMods : public CTimeVector
{
public:
	/// Opened lastmod file, -1 if not opened
	int m_f;
	/// lastmod file version since last loading
	ULONG m_ver;
	/// offset from UNIX epoch to our_epoch in seconds
	time_t m_our_epoch;
	/// mutex for locking during ReLoad()
	pthread_mutex_t m_mutex;
public:
	/// Creates new CLastMod, unitialized
	CLastMods(void) : m_f(-1), m_ver(0), m_our_epoch(0)
	{
		pthread_mutex_init(&m_mutex, NULL);
	}
	/// Closes lastmod file. You should Save() it before if you need.
	~CLastMods(void)
	{
		Close();
		pthread_mutex_destroy(&m_mutex);
	}
	/// Opens or creates lastmod file
	int Init(const char *path);
	/// Saves lastmod file
	int Save(void);
	/// Loads lastmod file
	int Load(void);
	/// Reloads if file version is changed
	int ReLoad(void);
	/// Returns maximum UrlID, -1 if array is empty
	ULONG maxID(void)
	{
		return size() - 1;
	}
	/// Calculates offset from UNIX epoch to be used in our time representation
	void CalcOurEpoch(time_t t)
	{
		m_our_epoch = (t / 3600 + 1) * 3600 - WORD_MAX * 3600;
	}
	/// Converts t to time_t value
	const time_t word2time_t(const value_type t) const
	{
		return t * 3600 + m_our_epoch;
	}
	/// Converts t to our internal representation
	const value_type time_t2word(const time_t t) const
	{
		if (t == BAD_DATE)
		{
			return 0; // no date
		}
		else
		if (t > (m_our_epoch + 3600)) // past our epoch (+ 3600 is here to prevent 0 result)
		{
			if (t > m_our_epoch + WORD_MAX * 3600) // past the end of our epoch
				return WORD_MAX; // max value
			else
				return (value_type)( ( t - m_our_epoch) / 3600 );
		}
		else
			return 1; // older than m_our_epoch
	}
	/// Fills lastmods from time_t array, s is last element number
	void ReadTimesFromBuf(time_t* buf, const ULONG s)
	{
		clear(); // just to be sure
		ULONG size = s;
		while ( (size > 0) && (buf[size]==0) )
			size--;	// trim trailing zeroes
		for(ULONG i = 0; i <= size; i++)
		{
			push_back(time_t2word(buf[i]));
		}
	}
	/// Returns lastmod of UrlId n
	time_t getTime(const size_type n){ return word2time_t((*this)[n]); }
	/// Closes lastmod file
	int Close(void)
	{
		if (m_f >= 0)
		{
			close(m_f);
			m_f = -1;
		}
		return 1; // always ok
	}
};

#endif /* _LASTMOD_H_ */
