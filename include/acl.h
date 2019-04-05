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

/*  $Id: acl.h,v 1.9 2002/01/09 11:50:14 kir Exp $
 *  Author : Igor Sukhih
 *	Implemented classes: CHost, CIPaccess
 */
#ifndef _ACL_H_
#define _ACL_H_

#include <map>
#include <vector>
#include <sys/types.h>
#include <arpa/inet.h>
#include "defines.h"

using std::map;
using std::vector;

/// Holds ip address and mask
class CHostAddr
{
public:
	ULONG m_host;
	ULONG m_submask;
public:
	CHostAddr()
	{
		m_host = 0;
		m_submask = 0;
	}
};

/// IP-based access control list
class CIpAccess: public map<ULONG, vector<CHostAddr> >
{
public:
	int m_status;
public:
	CIpAccess()
	{
		m_status = 0;
	}
	/** Adds new entry to ACL
	 * "name" can be hostname, IP address, or address/mask pair (in CIDR format)
	 */
	void AddElem(char *name);
	/// Check ACL for given address, returns true if access is allowed
	int IsAllowed(struct in_addr& addr)
	{
		uint32_t net, host;
		if (!m_status)
			return 1;

		net = inet_netof(addr);
		host = inet_lnaof(addr);

		iterator itnet;
		if ((itnet = find(net)) != end())
		{
			for (vector<CHostAddr>::iterator it = itnet->second.begin(); it != itnet->second.end(); it++)
			{
				if ((it->m_host & it->m_submask) == (host & it->m_submask))
				{
					return 1;
				}
			}
		}
		return 0;
	}

	int IsLocalhostOnly();
	/// Returns network class (A - 1, B - 2, C - 3) for given IP
	int GetIpClass(ULONG ip)
	{
		if (((ip & 0xFF000000) >> 24) <= 127)
			return 1;
		else if (((ip & 0xFF000000) >> 24) <= 191)
			return 2;
		else
			return 3;
	}
	/// returns bitmask with right num 1s.
	unsigned int GetSubMask(int num)
	{
		ULONG mask = 0;
		for (int i = 0; i < num; i++)
			mask = (mask << 1) + 1;
		return mask;
	}
};
#endif /* _ACL_H_ */
