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

/*  $Id: sock.h,v 1.18 2002/12/18 18:38:53 kir Exp $
	Author: Alexander F. Avdonkin
*/

#ifndef _SOCK_H_
#define _SOCK_H_

#include <string>
#include "aspseek-cfg.h"
#include "my_hash_map"
#include "my_hash_set"
#include "documents.h"
#include "maps.h"
#ifdef UNICODE
#include "ucharset.h"
#endif

using std::string;

/// Max length of received string in CBufferedSocket::RecvString()
const int MaxRecvStringLen = 2048;

/// This class holds daemon IP address and port
class CDaemonAddress
{
public:
	ULONG m_address;	///< IP address of daemon
	short m_port;		///< Port

public:
	CDaemonAddress(ULONG address, int port)
	{
		m_address = address;
		m_port = port;
	}
	int operator==(const CDaemonAddress& d) const
	{
		return (m_address == d.m_address) && (m_port == d.m_port);
	}
};

namespace STL_EXT_NAMESPACE {
	template <> struct hash<CDaemonAddress> {
		size_t operator()(const CDaemonAddress& __s) const
		{
			return __s.m_address + __s.m_port;
		}
	};
}

/// Set of unique daemon parameters
typedef hash_set<CDaemonAddress> CDaemonSet;

extern CDaemonSet daemons;

/// This class holds database statistics for particular query word
class CWordStat
{
public:
	ULONG m_total;	///< Total count of word in all indexed URLs
	ULONG m_urls;	///< Total URL count, where word is present
#ifdef UNICODE
	string m_output;
#endif

public:
	CWordStat()
	{
		m_total = m_urls = 0;
	}
};

/// This class holds databases statistics for each query word
#ifdef UNICODE
class CStringSet : public hash_map<CUWord, CWordStat>
{
public:
	hash_set<CUWord> m_patterns;	///< Collection of unique query words, with symbols '*' and '?'

public:
	int IsHighlight(const WORD* wrd);	///< Returns 1 if given word must be highlighted, takes into account patterns
#else
class CStringSet : public hash_map<string, CWordStat>
{
public:
	hash_set<string> m_patterns;	///< Collection of unique query words, with symbols '*' and '?'

public:
	int IsHighlight(const char* wrd);	///< Returns 1 if given word must be highlighted, takes into account patterns
#endif
	void PrintWordInfo(char* info);		///< Fills string with information for all words
	void CollectPatterns();			///< Collects words with symbols '*' and '?'
};

extern CStringSet LightWords;

#define SOCKET_BUF_SIZE 1024

/// This class represents buffered socket
class CBufferedSocket
{
public:
	int m_socket;				///< Socket itself
	int m_last;				///< Index in buffer, where to put next byte, to be sent
	char m_buffer[SOCKET_BUF_SIZE];		///< Sending buffer

public:
	CBufferedSocket()
	{
		m_socket = -1;
		m_last = 0;
	}
	void Close();				///< Sends buffered bytes and closes socket if not closed yet
	int operator=(int socket)
	{
		m_socket = socket;
		return socket;
	}
	~CBufferedSocket();
	void SendRest();				///< Sends characters, which are put to the buffer before
	void Send(const void* buffer, int bytes);	///< Buffered send
	int Recv(void* buffer, int bytes);		///< Safe recv
#ifdef UNICODE
	void SendWString(const WORD* string);	///< Sends string
	int RecvWString(CUWord& wstr);			///< Receives string
#endif
	void SendString(const char* string, int len);	///< Sends string
	void SendString(const char* string);		///< Sends string
	int RecvString(char* string);			///< Receives string
	int RecvString(string& str);			///< Receives string
	template<int n>
	int RecvString(CFixedString<n>& str)		///< Receives string of fixed size
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

	int RecvULONG(ULONG& val);	///< Receives ULONG
	int RecvULONG(long& val);	///< Receives long
	int RecvULONG(int& val);	///< Receives int
	void SendULONG(ULONG val);	///< Sends ULONG
	void SendULONGVector(CULONGVector& vector);
	int RecvULONGVector(CULONGVector& vector);
	void SendTime_t(time_t val);	///< Sends time_t
	int RecvTime_t(time_t& val);	///< Receives time_t
};

/// Safe recv
int Recv(int socket, void* buf, int bytes, int flags = 0);
/// Safe send
int Send(int socket, const void* buf, int bytes, int flags = 0);

template<class T1>
int Match(const T1* string, const T1* pattern, T1 anySeq, T1 anySym);
//int Match(const T1* string, const T1* pattern, T1 anySeq = '*', T1 anySym = '?');

#endif
