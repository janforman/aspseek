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

/*  $Id: buddy.h,v 1.12 2002/05/14 11:47:15 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _BUDDY_
#define _BUDDY_

#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "aspseek-cfg.h"
#include "defines.h"

#if !defined (MAP_ANONYMOUS) && defined (MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

#ifdef MAP_ANONYMOUS
#define ANON_MMAP(size) \
mmap(NULL, (size), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, m_fd, 0);
#else
#include <unistd.h> /* for open(), close() */
#define ANON_MMAP(size) \
mmap(NULL, (size), PROT_READ|PROT_WRITE, MAP_PRIVATE, m_fd, 0);
#endif

#if (SIZEOF_VOID_P == 4)
#define MIN_POOL 3
#elif (SIZEOF_VOID_P == 8)
#define MIN_POOL 4
#else
#error Size of 'void *' is neither 4 nor 8 - please contact the developers
#endif

#define MIN_POOL_SIZE (1 << (MIN_POOL + 1))

typedef struct
{
	ULONG m_flag;
	void* m_next;
	void** m_prev;
	int m_pool;
} FreeHead;

class CBuddyHeap
{
public:
	pthread_mutex_t m_mutex;
	void* m_pools[MEM_PAGE_SHIFT - MIN_POOL];
	unsigned long m_pgCount;
	unsigned long m_allocated;
	int m_fd;

public:
	CBuddyHeap()
	{
		m_pgCount = 0;
		m_allocated = 0;
		memset(m_pools, 0, sizeof(m_pools));
		pthread_mutex_init(&m_mutex, NULL);
#ifdef MAP_ANONYMOUS
		m_fd = -1;
#else
		m_fd = open("/dev/zero", O_RDWR);
#endif
	}
	~CBuddyHeap()
	{
		pthread_mutex_destroy(&m_mutex);
#ifndef MAP_ANONYMOUS
		close(m_fd);
#endif
	}
	void* Alloc(int pool);
	void Free(void* ptr, int pool);
	void* AllocPage();
	void FreePage(void* page);
	void* AllocBytes(ULONG size);
	void FreeBytes(void* ptr, ULONG size);
};

class CWordBuddyVector
{
public:
	WORD* m_data;
	int m_size;
	int m_allocated;
	CBuddyHeap* m_heap;

public:
	CWordBuddyVector()
	{
		m_data = NULL;
		m_size = 1;
		m_allocated = 0;
		m_heap = NULL;
	}
	~CWordBuddyVector();
	WORD* begin()
	{
		return m_data + 1;
	}
	WORD* end()
	{
		return m_data + m_size;
	}
	int size()
	{
		return m_size - 1;
	}
	void push_back(WORD value);
};

//extern CBuddyHeap vwHeap;

#endif
