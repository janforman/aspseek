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

/*  $Id: resolve.h,v 1.18 2002/12/18 18:38:53 kir Exp $
	Author: Alexander F. Avdonkin
*/

#ifndef _RESOLVER_H_
#define _RESOLVER_H_


#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
// #include "my_hash_map"
#include "defines.h"
#include "maps.h"


class CWordCache;


/** Class to resolve DNS names to IP addresses.
 * Instantiated once for each resolver process.
 * It countinuously processes requests.
 */
class CResolver
{
public:
	int m_rpipe[2];		///< Pair of pipes to receive request
	int m_spipe[2];		///< Pair of pipes to send results of resolving
	CResolver* m_next;	///< Next resolver in the linked list of objects
	CResolver** m_prev;	///< Address of pointer to this resolver in the linked list
	pid_t m_process;	///< 0 in resolver process, pid of resolver in parent process. Value, returned by fork

public:
	CResolver()
	{
		pipe(m_spipe);
		pipe(m_rpipe);
		m_next = NULL;
		m_prev = NULL;
		m_process = 0;
	}
	~CResolver()
	{
		if (m_rpipe[0] >= 0) close(m_rpipe[0]);
		if (m_rpipe[1] >= 0) close(m_rpipe[1]);
		if (m_spipe[0] >= 0) close(m_spipe[0]);
		if (m_spipe[1] >= 0) close(m_spipe[1]);
	}
	/// Remove resolver from linked list																	*/
	CResolver* Remove()
	{
		if (m_prev)
		{
			*m_prev = m_next;
		}
		return this;
	}
	/// Insert resolver to the linked list, after the specified element
	void Insert(CResolver** prev)
	{
		m_next = *prev;
		m_prev = prev;
		*m_prev = this;
	}
	void Resolve();		///< Continuously processes requests for resolving
	void Terminate();	///< Terminates resolving process
};

/// This class is instantiated once as global variable. It is used for simultaneous IP address resolving.
class CResolverList
{
public:
	pthread_mutex_t m_mutex;	///< Mutex for protecting resolvers linked list
	CResolver* m_free;		///< First free resolver in the linked list
	int m_count, m_freeCount;	///< Total and free resolvers count respectively

public:
	CResolverList();
	~CResolverList();
	void Init(int count);	///< Creates "count" resover processes
	/// This method finds free resolver process and uses it resolve given host name into IP address
	int GetHostByName(CWordCache* wordCache, const char* name, struct in_addr& address, int* family, int timeout);
};

/// Instance of this class holds information about address for particular host name in the address cache
class CAddr
{
public:
	/** Expiration time of IP address
	 * Set to time at the moment of last resolving
	 * plus AddressExpiry config parameter
	 */
	ULONG m_expiryTime;
	struct in_addr m_address;	///< IP address for particular host name
	sa_family_t m_sin_family;	///< Address family, returned by gethostbyname


public:
	CAddr()
	{
		memset(&m_address, 0, sizeof(m_address));
	}
};

/** Cache of IP addresses.
 * This class is instantiated once as global variable. It holds cache
 * of IP address for all encountered hosts. After resoving host name,
 * its IP address is cached and used for subsequent resolution during time
 * specified by "AddressExpiry" config parameter
 */
class CAddrCache : public hash_map<std::string, CAddr>
{
public:
	pthread_mutex_t m_mutex;	///< Mutex to protect hash table

public:
	int StillValid(char* name);	///< Not used
	CAddrCache()
	{
		pthread_mutex_init(&m_mutex, NULL);
	}
	~CAddrCache()
	{
		pthread_mutex_destroy(&m_mutex);
	}
	/// GetHostByName looks up for specified host name, checks its expiration, resolves it if required and returns IP address
	int GetHostByName(CWordCache* wordCache, const char* name, struct in_addr& address, sa_family_t* family, int timeout);
};

extern CResolverList resolverList;	///< The only instance of resolver list

/// "Connect" is "connect" with specified time-out
int Connect(int socket, struct sockaddr* sin, int size, int timeout);
/// Connects to the specified host, port with specified time-out
int OpenHost(CWordCache& wordCache, const char *hostname, int port, int timeout);
int GetHostByName(CWordCache* wordCache, const char* hostname, struct in_addr& address, sa_family_t* family, int timeout);

#endif
