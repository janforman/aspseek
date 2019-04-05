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

/*  $Id: urlfilter.h,v 1.9 2002/01/09 11:50:15 kir Exp $
 *  Author: Kir Kolyshkin
 *  Defines classes for filters that exclude some URLs from results.
 *  By this time only CTimeFilter is implemented, which can limit results
 *  to ones that have last modification time within specified period.
 *  TODO: CLangFilter, ÛDocSizeFilter, CAdultFilter ;), whatever...
 */

#ifndef _URLFILTER_H_
#define _URLFILTER_H_

#include <algorithm>
#include "lastmod.h"
#include "logger.h"
#include "time.h"

using std::swap;

/// This is abstract class for URL filter.
class CResultUrlFilter
{
public:
	int m_accDist;
public:
	CResultUrlFilter()
	{
		m_accDist = 1;
	}
	/// Checks if given urlID is acceptable
	virtual bool CheckUrl(const ULONG urlID) = 0;
	ULONG GetBufferWithWeightsS(int size, BYTE* p_buffer, BYTE*& buffer, WORD form);
	ULONG GetBufferWithWeightsS(int size, BYTE* p_buffer, BYTE*& buffer, ULONG mask, WORD form);
	ULONG GetBufferWithWeightsSL(int size, BYTE* p_buffer, BYTE*& buffer);
};

class CStubFilter: public CResultUrlFilter
{
public:
	virtual bool CheckUrl(const ULONG urlID) { return true; } // always ok
};

class CTimeFilter: public CResultUrlFilter
{
public:
	const CLastMods& m_lm;	///< reference to CLastMods object
	WORD m_fr;		///< first date
	WORD m_to;		///< last date
public:
	/// Sets the filter to specified time period
	/// to should be >= than from
	CTimeFilter(const CLastMods& lm, const time_t from, const time_t to): m_lm(lm)
	{
		m_fr = m_lm.time_t2word(from);
		m_to = m_lm.time_t2word(to);
		if (m_fr > m_to)
			swap(m_fr, m_to);
/* debug crap - commented out
		time_t t1, t2;
		t1 = m_lm.word2time_t(m_fr);
		t2 = m_lm.word2time_t(m_to);
		logger.log(CAT_ALL, L_DEBUG, "--- Setting timefilter ---\n");
		logger.log(CAT_ALL, L_DEBUG, "ORIG from: (%d)\t%s", from, ctime(&from));
		logger.log(CAT_ALL, L_DEBUG, "ORIG to:   (%d)\t%s", to, ctime(&to));
		logger.log(CAT_ALL, L_DEBUG, "CNVT from: (%d)\t%s", t1, ctime(&t1));
		logger.log(CAT_ALL, L_DEBUG, "CNVT to:   (%d)\t%s", t2, ctime(&t2));
		logger.log(CAT_ALL, L_DEBUG, "---\n");
*/
	}
	/** Checks if given UrlID's last modification date
	 * is within specified period.
	 * Returns false if UrlID is not within range, true otherwise.
	 */
	virtual bool CheckUrl(const ULONG UrlID);
};

#endif /* _URLFILTER_H_ */
