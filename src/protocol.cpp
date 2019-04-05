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

/*  $Id: protocol.cpp,v 1.5 2002/06/18 10:00:52 kir Exp $
    Author : Alexander F. Avdonkin
*/

#include "protocol.h"
#include "crc32.h"
#include <stdio.h>

void CCgiQuery::Send(CBufferedSocket& sock)
{
	sock.SendString(m_query_words.c_str());
	sock.SendString(m_results.c_str());
	sock.SendString(m_cached.c_str());
	sock.SendString(m_include_words.c_str());
	sock.SendString(m_exclude_words.c_str());
	sock.SendString(m_includeSites.c_str());
	sock.SendString(m_excludeSites.c_str());
	sock.SendString(m_ul_unescaped.c_str());
	sock.SendString(m_tmplp.c_str());
	sock.SendString(m_wordform_langs.c_str());
	sock.SendULONG(m_format);
	sock.SendULONG(m_np);
	sock.SendULONG(m_ps);
	sock.SendULONG(m_inres);
	sock.SendULONG(m_posmask);
	sock.SendULONG(m_take);
	sock.SendULONG(m_iph);
	sock.SendULONG(m_xph);
	sock.SendULONG(m_gr);
	sock.SendULONG(m_site);
	sock.SendTime_t(m_t_from);
	sock.SendTime_t(m_t_to);
	sock.SendULONG(m_search_mode);
	sock.SendULONG(m_sort_by);
	sock.SendULONG(m_accDist);
#ifdef UNICODE
	sock.SendString(m_charset.c_str());
#endif
	sock.SendULONG(m_spaces.size());
	for (CULONGSet::iterator it = m_spaces.begin(); it != m_spaces.end(); it++)
	{
		sock.SendULONG(*it);
	}
}

int CCgiQuery::Recv(CBufferedSocket& sock)
{
	if (sock.RecvString(m_query_words) < 0)
		return -1;
	if (sock.RecvString(m_results) < 0)
		return -1;
	if (sock.RecvString(m_cached) < 0)
		return -1;
	if (sock.RecvString(m_include_words) < 0)
		return -1;
	if (sock.RecvString(m_exclude_words) < 0)
		return -1;
	if (sock.RecvString(m_includeSites) < 0)
		return -1;
	if (sock.RecvString(m_excludeSites) < 0)
		return -1;
	if (sock.RecvString(m_ul_unescaped) < 0)
		return -1;
	if (sock.RecvString(m_tmplp) < 0)
		return -1;
	if (sock.RecvString(m_wordform_langs) < 0)
		return -1;
	sock.RecvULONG(m_format);
	sock.RecvULONG(m_np);
	sock.RecvULONG(m_ps);
	sock.RecvULONG(m_inres);
	sock.RecvULONG(m_posmask);
	sock.RecvULONG(m_take);
	sock.RecvULONG(m_iph);
	sock.RecvULONG(m_xph);
	sock.RecvULONG(m_gr);
	sock.RecvULONG(m_site);
	sock.RecvTime_t(m_t_from);
	sock.RecvTime_t(m_t_to);
	sock.RecvULONG(m_search_mode);
	sock.RecvULONG(m_sort_by);
	sock.RecvULONG(m_accDist);
#ifdef UNICODE
	sock.RecvString(m_charset);
#endif
	ULONG ssize;
	sock.RecvULONG(ssize);
	for (ULONG i = 0; i < ssize; i++)
	{
		ULONG sp;
		sock.RecvULONG(sp);
		m_spaces.insert(sp);
	}
	return 0; // ok
}

#define MAX_LONG_STRSIZE 12	// 10 digits + sign + single space.

ULONG CCgiQuery::MakeQueryKey(char *query)
{
	int bufsize, qlen, len;
	char *buf;
	if (query)
	{
#ifdef UNICODE
		qlen = strwlen((WORD*)query) * 2;
#else
		qlen = strlen(query);
#endif
		bufsize = qlen + 2;
		bufsize += 8 * MAX_LONG_STRSIZE;
		bufsize += m_includeSites.size() ? m_includeSites.size() + 1 : 2;
		bufsize += m_cspaces[0] ? strlen(m_cspaces) + 1 : 2;
		bufsize += m_ul_unescaped.size() ? m_ul_unescaped.size() + 1 : 2;
		bufsize += m_excludeSites.size() ? m_excludeSites.size() + 1 : 2;
		if ((buf = (char *) alloca(bufsize)) == NULL) return 0;
/* ad fm fr gr is s spN st t to ul xs */
		len = snprintf(buf, bufsize - qlen,
			"%d %d %d %d %s %d %s %d %d %d %s %s ",
			m_accDist, m_search_mode, (int)m_t_from, m_gr,
			m_includeSites.size() ? m_includeSites.c_str() : "*",
			m_sort_by, m_cspaces, m_site, m_take, (int)m_t_to,
			m_ul_unescaped.size()? m_ul_unescaped.c_str() : "*",
			m_excludeSites.size() ? m_excludeSites.c_str() : "*");
		memcpy(buf + len, (char*)query, qlen);
		buf[len + qlen] = 0;
		return crc32(buf, len + qlen);
	}
	else
	{
		return 0;
	}
}
