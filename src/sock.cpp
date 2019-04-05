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

/*  $Id: sock.cpp,v 1.25 2002/05/20 11:48:18 kir Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CBufferedSocket, CStringSet
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <errno.h>
#include "sock.h"
#include "logger.h"
#include "templates.h"
#include "ucharset.h"

CStringSet LightWords;
CDaemonSet daemons;

int Recv(int socket, void* buf, int bytes, int flags)
{
	char* b = (char*)buf;

	while (bytes > 0)
	{
		int r = recv(socket, b, bytes, flags);
		if (r < 0)
		{
			aspseek_log(CAT_NET, L_ERR, "Error recv() %s\n", strerror(errno));
			return r;
		}
		if (r == 0) break;
		bytes -= r;
		b += r;
	}
	return b - (char*)buf;
}

int Send(int socket, const void* buf, int bytes, int flags)
{
	const char* b = (const char*)buf;
	while (bytes > 0)
	{
		int r = send(socket, b, bytes, flags);
		if (r < 0)
		{
			logger.log(CAT_NET, L_ERR, "Error send() %s\n", strerror(errno));
			return r;
		}
		if (r == 0) break;
		bytes -= r;
		b += r;
	}
	return b - (char*)buf;
}

void CBufferedSocket::Send(const void* buffer, int bytes)
{
	if (m_socket >= 0)
	{
		if (m_last + bytes <= SOCKET_BUF_SIZE)
		{
			// There is enough room in buffer, just copy bytes to it
			memcpy(m_buffer + m_last, buffer, bytes);
			m_last += bytes;
		}
		else
		{
			if (bytes < SOCKET_BUF_SIZE)
			{
				// Fill the rest of buffer and send it
				int part = SOCKET_BUF_SIZE - m_last;
				memcpy(m_buffer + m_last, buffer, part);
				m_last += part;
				SendRest();
				// Copy unsent bytes to buffer
				memcpy(m_buffer, (char*)buffer + part, bytes - part);
				m_last += bytes - part;
			}
			else
			{
				// First, send buffered bytes, then send specified bytes
				SendRest();
				::Send(m_socket, buffer, bytes, 0);
			}
		}
	}
}

void CBufferedSocket::SendRest()
{
	if (m_last)
	{
		::Send(m_socket, m_buffer, m_last, 0);
		m_last = 0;
	}
}

int CBufferedSocket::Recv(void* buffer, int bytes)
{
	if (m_socket >= 0)
	{
		SendRest();		// Send buffered bytes before recv!
		return ::Recv(m_socket, buffer, bytes, 0);
	}
	else
	{
		return -1;
	}
}

void CBufferedSocket::SendString(const char* string, int len)
{
	Send(&len, sizeof(len));
	Send(string, len);
}

void CBufferedSocket::SendString(const char* string)
{
	SendString(string, strlen(string));
}

int CBufferedSocket::RecvULONG(ULONG& val)
{
	return Recv(&val, sizeof(val));
}

int CBufferedSocket::RecvULONG(long& val)
{
	return Recv(&val, sizeof(val));
}

int CBufferedSocket::RecvULONG(int& val)
{
	return Recv(&val, sizeof(val));
}

int CBufferedSocket::RecvString(char* string)
{
	int len;
	if (Recv(&len, sizeof(len)) == sizeof(len))
	{
		if ((len < 0) || (len > MaxRecvStringLen))
			return -1; // error
		Recv(string, len);
		string[len] = 0;
	}
	return len;
}

int CBufferedSocket::RecvString(string& str)
{
	int len;
	if (Recv(&len, sizeof(len)) == sizeof(len))
	{
		// sanity check
		if ((len < 0) || (len > MaxRecvStringLen))
			return -1; // error
		char* tstr = (char*)alloca(len + 1);
		Recv(tstr, len);
		tstr[len] = 0;
		str = tstr;
	}
	else
	{
		str = "";
	}
	return len;
}

#ifdef UNICODE
int CBufferedSocket::RecvWString(CUWord& str)
{
	int len;
	if (Recv(&len, sizeof(len)) == sizeof(len))
	{
		// sanity check
		if ((len < 0) || (len > MaxRecvStringLen))
			return -1; // error
		WORD* tstr = (WORD*)alloca((len << 1) + 2);
		Recv(tstr, len << 1);
		tstr[len] = 0;
		str = tstr;
	}
	else
	{
		str.m_word[0] = 0;
	}
	return len;
}

void CBufferedSocket::SendWString(const WORD* string)
{
	int len = strwlen(string);
	Send(&len, sizeof(len));
	Send(string, len << 1);
}

#endif

void CBufferedSocket::SendULONG(ULONG val)
{
	Send(&val, sizeof(val));
}

void CBufferedSocket::SendTime_t(time_t val)
{
	Send(&val, sizeof(val));
}

int CBufferedSocket::RecvTime_t(time_t& val)
{
	return Recv(&val, sizeof(val));
}

void CBufferedSocket::SendULONGVector(CULONGVector& vector)
{
	SendULONG(vector.size());
	for (CULONGVector::iterator it = vector.begin(); it != vector.end(); it++)
		SendULONG(*it);
}

int CBufferedSocket::RecvULONGVector(CULONGVector& vector)
{
	int size;
	if (RecvULONG(size) < 0)
		return -1; // error
	vector.resize(size);
	return Recv(&(*vector.begin()), size * sizeof(ULONG));
}

CBufferedSocket::~CBufferedSocket()
{
	// Send buffered bytes and close
	Close();
}

void CBufferedSocket::Close()
{
	if (m_socket >= 0)
	{
		SendRest();
		if( shutdown(m_socket, SHUT_RDWR) ){
			logger.log(CAT_NET, L_ERR,
				"Error in shutdown(): %s\n", strerror(errno));
		}
		close(m_socket);
		m_socket = -1;
	}
}

#ifdef UNICODE
int CStringSet::IsHighlight(const WORD* wrd)
#else
int CStringSet::IsHighlight(const char* wrd)
#endif
{
	if (find(wrd) != end())
	{
		// Non pattern word is found
		return 1;
	}
	else
	{
		// Check for each pattern

#ifdef UNICODE
		for (hash_set<CUWord>::iterator it = m_patterns.begin(); it != m_patterns.end(); it++)
		{
			if (Match<WORD>(wrd, it->Word(), '%', '_'))
#else
		for (hash_set<string>::iterator it = m_patterns.begin(); it != m_patterns.end(); it++)
		{
			if (Match<char>(wrd, it->c_str(), '*', '?'))
#endif
			{
				return 1;
			}
		}

	}
	return 0;
}

void CStringSet::PrintWordInfo(char* winfo)
{
	char* pinfo = winfo;
	for (iterator it = begin(); it != end(); it++)
	{
		pinfo += strlen(pinfo);
		CWordStat& stat = it->second;
#ifdef UNICODE
		sprintf(pinfo, " %s:%lu/%lu", stat.m_output.c_str(), stat.m_urls, stat.m_total);
#else
		sprintf(pinfo, " %s:%lu/%lu", (*it).first.c_str(), stat.m_urls, stat.m_total);
#endif
	}
}

void CStringSet::CollectPatterns()
{
	for (iterator it = begin(); it != end(); it++)
	{
#ifdef UNICODE
		const CUWord& wrd = it->first;
		for (const WORD* pw = wrd.m_word; *pw; pw++)
		{
			if ((*pw == '%') || (*pw == '_'))
			{
				m_patterns.insert(wrd);
				break;
			}
		}
#else
		const string& wrd = it->first;
		if (strpbrk(wrd.c_str(), "*?"))
		{
			m_patterns.insert(wrd);
		}
#endif
	}
}
