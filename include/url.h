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

/*  $Id: url.h,v 1.3 2002/01/17 13:16:17 al Exp $
	Author: Alexander F. Avdonkin
	Uses parts of UdmSearch code
*/

#ifndef _URL_H_
#define _URL_H_

class CUrlW
{
public:
	ULONG m_urlID;
	ULONG m_siteID;
	ULONG m_count;
	ULONG m_weight;
};

class CPUrlW
{
public:
	CUrlW* m_purlw;

public:
	int operator<(const CPUrlW& uw) const
	{
		if (m_purlw->m_weight == uw.m_purlw->m_weight)
		{
			int res = (int)(m_purlw->m_count >> 24) - (int)(uw.m_purlw->m_count >> 24);
			if (res == 0)
			{
				res = m_purlw->m_urlID - uw.m_purlw->m_urlID;
			}
			return res;
		}
		else
		{
			return m_purlw->m_weight > uw.m_purlw->m_weight;
		}
	}
};

class CPUrlWM
{
public:
	CUrlW* m_purlw;

public:
	int operator<(const CPUrlWM& uw) const
	{
		int res = (m_purlw->m_count >> 24) - (uw.m_purlw->m_count >> 24);
		if (res == 0)
		{
			return m_purlw < uw.m_purlw;
		}
		return res < 0;
	}
};

#endif
