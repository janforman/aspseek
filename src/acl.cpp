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

/*  $Id: acl.cpp,v 1.10 2002/05/11 15:27:27 kir Exp $
 *   Author : Igor Sukhih
 *	Implemented classes: CIpAccess
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include "acl.h"
#include "logger.h"

// for Solaris 8
#ifndef INADDR_NONE
#define INADDR_NONE -1
#endif

void CIpAccess::AddElem(char *name)
{
	uint32_t ip, hip;
	class CHostAddr host;
	char str1[256];
	ULONG net;
	int ip1, ip2, ip3, ip4, mask;

	ip1 = ip2 = ip3 = ip4 = mask = 0;
	m_status = 1;

	if (sscanf(name, "%d.%d.%d.%d/%d", &ip1, &ip2, &ip3, &ip4, &mask) >= 4)
	{
		sprintf(str1, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
		if ((ip = inet_addr(str1)) == INADDR_NONE)
		{
			logger.log(CAT_ALL, L_ERR, "Bad AllowFrom address %s\n", name);
			return;
		}
	}
	else if (sscanf(name, "%s", str1))
	{
		struct hostent *he;
		if (!(he = gethostbyname(str1)))
		{
			logger.log(CAT_ALL, L_ERR, "Bad AllowFrom address %s\n", name);
			return;
		}
		memcpy(&ip, he->h_addr, sizeof(uint32_t));
	}
	else
	{
		logger.log(CAT_ALL, L_ERR, "Bad AllowFrom address %s\n", name);
		return;
	}


	hip = ntohl(ip);
	switch(GetIpClass(hip))
	{
	case 1:
		net = hip >> 24;
		if (mask > 8 && mask <= 32)
			host.m_submask = GetSubMask(mask - 8) << (32 - mask);
		else if (!(hip & 0xFFFFFF))
			host.m_submask = 0;
		else
			host.m_submask = 0xFFFFFF;
		break;
	case 2:
		net = hip >> 16;
		if (mask > 16 && mask <= 32)
			host.m_submask = GetSubMask(mask - 16) << (32 - mask);
		else if (!(hip & 0xFFFF))
			host.m_submask = 0;
		else
			host.m_submask = 0xFFFF;
		break;
	case 3:
		net = hip >> 8;
		if (mask > 24 && mask <= 32)
			host.m_submask = GetSubMask(mask - 24) << (32 - mask);
		else if (!(hip & 0xFF))
			host.m_submask = 0;
		else
			host.m_submask = 0xFF;
		break;
	}

	host.m_host = host.m_submask & hip;
	(*this)[net].push_back(host);
	return;
}

int CIpAccess::IsLocalhostOnly()
{
	uint32_t net = 127;
	uint32_t host = 1;

	if (!size())
	{
		return 1;
	}
	iterator itnet;
	if ((size() == 1) && (itnet = find(net)) != end())
	{
		for (vector<CHostAddr>::iterator it = itnet->second.begin(); it != itnet->second.end(); it++)
		{
			if (it->m_host == host)
			{
				m_status = 0;
				return 1;
			}
		}
	}
	return 0;
}
