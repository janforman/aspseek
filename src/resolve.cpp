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

/*  $Id: resolve.cpp,v 1.30 2002/08/26 01:37:20 matt Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CAddrCache, CResolver, CResolverList
*/

#include "aspseek-cfg.h"
#include <signal.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <time.h>
#include "resolve.h"
#include "index.h"
#include "logger.h"
#include "timer.h"

// for Solaris 8
#ifndef INADDR_NONE
#define INADDR_NONE -1
#endif

time_t AddressExpiry = 3600;
CResolverList resolverList;
CAddrCache addrCache;

void termr(int x)
{
}

#if defined(_AIX) || defined(HAVE_GLIBC2_STYLE_GETHOSTBYNAME_R)
#if defined(HAVE_GLIBC2_STYLE_GETHOSTBYNAME_R)
#define GETHOSTBYNAME_BUFF_SIZE 2048
#else
#define GETHOSTBYNAME_BUFF_SIZE sizeof(struct hostent_data)
#endif /* defined(HAVE_GLIBC2_STYLE_GETHOSTBYNAME_R) */

#else
#ifdef HAVE_GETHOSTBYNAME_R_WITH_HOSTENT_DATA
#define GETHOSTBYNAME_BUFF_SIZE sizeof(hostent_data)
#define my_gethostbyname_r(A,B,C,D,E) gethostbyname_r((A),(B),(hostent_data*) (C))
#else
#define GETHOSTBYNAME_BUFF_SIZE 2048
#define my_gethostbyname_r(A,B,C,D,E) gethostbyname_r((A),(B),(C),(D),(E))
#endif /* HAVE_GETHOSTBYNAME_R_WITH_HOSTENT_DATA */
#endif /* defined(_AIX) || defined(HAVE_GLIBC2_STYLE_GETHOSTBYNAME_R) */

// Broken my_gethostbyname_r
#undef HAVE_GETHOSTBYNAME_R

char *h_err_str[] = {
	"Host not found",
	"The requested name is valid but does not have an IP address",
	"A non-recoverable name server error occurred",
	"A temporary error occurred on an authoritative name. Try again later",
	"Unknown error"
};

const char *my_hstrerror(int herr)
{
	switch(herr)
	{
		case HOST_NOT_FOUND:
			return h_err_str[0];
		case NO_ADDRESS:
//		case NO_DATA:
			return h_err_str[1];
		case NO_RECOVERY:
			return h_err_str[2];
		case TRY_AGAIN:
			return h_err_str[3];
		default:
			return h_err_str[4];
	}
}

void CResolverList::Init(int count)
{
	struct sigaction sigtermr;
	struct sigaction sigintr;

	sigtermr.sa_handler=termr;
	sigintr.sa_handler = termr;
	sigemptyset(&sigtermr.sa_mask);
	sigemptyset(&sigintr.sa_mask);
	sigtermr.sa_flags=0;
	sigintr.sa_flags = 0;

	CResolver** resolvers = (CResolver**)alloca(count * sizeof(CResolver*));
	for (int i = 0; i < count; i++)
	{
		CResolver* resolver = new CResolver;
		resolvers[i] = resolver;
		resolver->m_process = fork();
		if (resolver->m_process == 0)
		{
			close(resolver->m_rpipe[0]);
			close(resolver->m_spipe[1]);
			resolver->m_rpipe[0] = -1;
			resolver->m_spipe[1] = -1;
			for (int j = 0; j < i; j++)
			{
				CResolver* prev = resolvers[j];
				close(prev->m_rpipe[0]);
				close(prev->m_spipe[1]);
			}
			sigaction(SIGTERM, &sigtermr, NULL);
			sigaction(SIGINT, &sigintr, NULL);
			resolver->Resolve();
			delete resolver;
			exit(0);
		}
		else
		{
			close(resolver->m_rpipe[1]);
			close(resolver->m_spipe[0]);
			resolver->m_rpipe[1] = -1;
			resolver->m_spipe[0] = -1;
		}
	}
	for (int i = 0; i < count; i++)
	{
		if (resolvers[i]->m_process > 0)
		{
			resolvers[i]->Insert(&m_free);
			m_count++;
		}
		else
		{
			delete resolvers[i];
		}
	}
	m_freeCount = m_count;
}

CResolverList::CResolverList()
{
	pthread_mutex_init(&m_mutex, NULL);
	m_free = NULL;
	m_count = 0;
	m_freeCount = 0;
}

CResolverList::~CResolverList()
{
//	int free = m_free ? 1 : 0;
//	if (free) printf("Starting terminating resolver processes\n");
	while (m_free)
	{
		CResolver* resolver = m_free->Remove();
		resolver->Terminate();
//		printf("Process: %i terminated\n", resolver->m_process);
		delete resolver;
	}
	pthread_mutex_destroy(&m_mutex);
//	if (free) printf("Finishing terminating resolver processes\n");
}

// Read from descriptor fd retrying if interrupted.
static int safe_read(int fd, void *buf, size_t count)
{
	int rd;
	if (count <= 0)
		return count;
#ifdef EINTR
	do
	{
		rd = read(fd, buf, count);
	}
	while (rd < 0 && errno == EINTR);
#else
	rd = read(fd, buf, count);
#endif
	return rd;
}

// Write to descriptor fd retrying if interrupted.
static int safe_write(int fd, const void *buf, size_t count)
{
	int wr;
	if (count <= 0)
		return count;
#ifdef EINTR
	do
	{
		wr = write(fd, buf, count);
	}
	while (wr < 0 && errno == EINTR);
#else
	wr = write(fd, buf, count);
#endif
	return wr;
}

int Read(int p, void* buf, int len)
{
	int left = len;
	while (left)
	{
		int rd;
		rd = safe_read(p, (char*)buf + len - left, left);
		if (rd < 0) break;
		left -= rd;
	}
	return 1;
}

int CResolverList::GetHostByName(CWordCache* wordCache, const char* name, struct in_addr& address, int* family, int timeout)
{
	if (*name == 0) return 1;
	int result = 0;
	CResolver* resolver = NULL;
	while (true)
	{
		{
			CLocker lock(this);
			if (m_free)
			{
				resolver = m_free->Remove();
				m_freeCount--;
				break;
			}
		}
		logger.log(CAT_ALL, L_WARN, "Waiting for resolver\n");
		usleep(1000000);
	}
	wordCache->BeginGetHost(resolver->m_process);
	int len = strlen(name);
	safe_write(resolver->m_spipe[1], &len, sizeof(len));
	safe_write(resolver->m_spipe[1], &timeout, sizeof(timeout));
	safe_write(resolver->m_spipe[1], name, len);
	Read(resolver->m_rpipe[0], &result, sizeof(result));
	if (result == 0)
	{
		Read(resolver->m_rpipe[0], &address, sizeof(in_addr));
		Read(resolver->m_rpipe[0], family, sizeof(int));
	}
	wordCache->EndGetHost(resolver->m_process);
	{
		CLocker lock(this);
		resolver->Insert(&m_free);
		m_freeCount++;
	}
	return result;
}

void CResolver::Terminate()
{
	int len = 0;
	int st;
	safe_write(m_spipe[1], &len, sizeof(len));
	waitpid(m_process, &st, 0);
}

extern struct sigaction sigalrm;

void CResolver::Resolve()
{
	sigaction(SIGALRM, &sigalrm, NULL);
	while (true)
	{
		int hlen;
		char name[1000];
		int len = safe_read(m_spipe[0], &hlen, sizeof(hlen));
		if (len > 0)
		{
			if (hlen == 0)
			{
				logger.log(CAT_ALL, L_INFO, "Resolver process %i received terminate command and exited\n", getpid());
				break;
			}
			int timeout = 1;
			Read(m_spipe[0], &timeout, sizeof(timeout));
			Read(m_spipe[0], name, hlen);
			name[hlen] = 0;
			struct hostent* he;
			double time;
			alarm(timeout);
			{
				CTimer1 timer(time);
				he = gethostbyname(name);
			}
			alarm(0);
			if (time > timeout + 5)
			{
				printf("Actual gethostbyname timeout: %7.3f\n", time);
			}
			int result = (he == NULL);
			safe_write(m_rpipe[1], &result, sizeof(result));
			if (result == 0)
			{
				safe_write(m_rpipe[1], he->h_addr, sizeof(in_addr));
				safe_write(m_rpipe[1], &he->h_addrtype, sizeof(he->h_addrtype));
			}
		}
		else
		{
			logger.log(CAT_ALL, L_ERROR, "Read pipe in resolver process %i is broken, exiting\n", getpid());
			break;
		}
	}
}

int CAddrCache::GetHostByName(CWordCache* wordCache, const char* name, struct in_addr& address, sa_family_t* family, int timeout)
{
	while (true)
	{
		pthread_mutex_lock(&m_mutex);
		iterator it = find(name);
		if (it == end())
		{
Ins:		struct hostent host;
#ifdef HAVE_GETHOSTBYNAME_R
			struct hostent *phost;
			char buf[GETHOSTBYNAME_BUFF_SIZE];
			int err;
#endif
			CAddr& addr = (*this)[name];
			pthread_mutex_unlock(&m_mutex);
			int r=0;
			in_addr caddr;
			if (resolverList.m_count == 0)
			{
				wordCache->BeginGetHost(getpid());
#ifdef HAVE_GETHOSTBYNAME_R
				r = my_gethostbyname_r((char*)name, &host, buf, sizeof(buf), &phost, &err);
				if ((r == 0) && (host.h_addr))
					memcpy(&caddr, host.h_addr, sizeof(in_addr));
				else
					logger.log(CAT_NET, L_WARNING, "Can't resolve hostname %s: %s\n", name, my_hstrerror(h_errno));
#else
				struct hostent *h2;
				pthread_mutex_lock(&m_mutex);
				h2=gethostbyname(name);
				if (h2)
				{
					r = 0;
					memcpy(&host, h2, sizeof(host));
					if (host.h_addr)
						memcpy(&caddr, host.h_addr, sizeof(in_addr));
				}
				else
					logger.log(CAT_NET, L_WARNING, "Can't resolve hostname %s: %s\n", name, my_hstrerror(h_errno));
				pthread_mutex_unlock(&m_mutex);
#endif
				wordCache->EndGetHost(getpid());
			}
			else
			{
				r = resolverList.GetHostByName(wordCache, name, caddr, &host.h_addrtype, timeout);
			}
			if ((r == 0) && (caddr.s_addr != 0))
			{
				struct timeval time;
				pthread_mutex_lock(&m_mutex);
				gettimeofday(&time, NULL);
				*family = addr.m_sin_family = host.h_addrtype;
				memcpy(&addr.m_address, &caddr, sizeof(in_addr));
				address = addr.m_address;
				addr.m_expiryTime = time.tv_sec + AddressExpiry;
				pthread_mutex_unlock(&m_mutex);
				return 0;
			}
			else
			{
				pthread_mutex_lock(&m_mutex);
				erase(name);
				pthread_mutex_unlock(&m_mutex);
				return NET_CANT_RESOLVE;
			}
		}
		else
		{
			CAddr* addr;
			struct timeval time;
			while (addr = &it->second, addr->m_address.s_addr == 0)
			{
				pthread_mutex_unlock(&m_mutex);
				if (term) return NET_CANT_RESOLVE;
				usleep(1000000);
				pthread_mutex_lock(&m_mutex);
				it = find(name);
				if (it == end()) goto Ins;
			}
			gettimeofday(&time, NULL);
			if ((long)addr->m_expiryTime > time.tv_sec)
			{
				address = addr->m_address;
				*family = addr->m_sin_family;
				pthread_mutex_unlock(&m_mutex);
				return 0;
			}
			else
			{
				erase(name);
				pthread_mutex_unlock(&m_mutex);
			}
		}
	}
}

int CAddrCache::StillValid(char *name)
{
	iterator it = find(name);
	struct timeval time;
	gettimeofday(&time, NULL);
	return (it != end()) && ((long)it->second.m_expiryTime < time.tv_sec + 1);
}

int Connect(int socket, struct sockaddr* sin, int size, int timeout)
{
	alarm(timeout);
	int res = connect(socket, sin, size);
	alarm(0);
	return res;
}

int GetHostByName(CWordCache* wordCache, const char* hostname, struct in_addr& address, sa_family_t* family, int timeout)
{
	if ((address.s_addr = inet_addr(hostname)) != INADDR_NONE)
	{
		*family = AF_INET;
		return 0;
	}
	else
	{
		return addrCache.GetHostByName(wordCache, hostname, address, family, timeout);
	}
}

int OpenHost(CWordCache& wordCache, const char *hostname, int port, int timeout)
{
	int net;
//	struct hostent host, *phost;
	struct sockaddr_in sin;

#if (WIN32|WINNT)
	startWSA();
#endif

	memset(&sin, 0, sizeof(sin));

	if (port)
	{
		sin.sin_port = htons((u_short)port);
	}
	else
	{
		return NET_ERROR;
	}

	unsigned char* addr;

	int r = GetHostByName(&wordCache, wordCache.m_flags & FLAG_LOCAL ? "127.0.0.1" : hostname, sin.sin_addr, &sin.sin_family, timeout);
//	if ((sin.sin_addr.s_addr=inet_addr(hostname)) != INADDR_NONE)
//	{
//		sin.sin_family=AF_INET;
//	}
//	else
//	{
//		int r = addrCache.GetHostByName(&wordCache, hostname, sin.sin_addr, &sin.sin_family, timeout);
		if (r)
		{
			return r;
		}
//	}
	addr = (unsigned char*)&sin.sin_addr.s_addr;
//	int one = 1;
	net = socket(AF_INET, SOCK_STREAM, 0);
	wordCache.BeginConnect();
	int res = Connect(net, (struct sockaddr *)&sin, sizeof (sin), timeout);
	wordCache.EndConnect();
	if (res)
	{
		close(net);
		logger.log(CAT_NET, L_WARNING, "Can't connect to host: %s (%u.%u.%u.%u): %s\n", hostname, addr[0], addr[1], addr[2], addr[3], strerror(errno));
		return NET_CANT_CONNECT;
	}
	return net;
}
