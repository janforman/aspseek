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

/*  $Id: delmap.h,v 1.8 2003/07/15 00:34:01 matt Exp $
    Author : Alexander Avdonkin
*/

#ifndef _DELMAP_H_
#define _DELMAP_H_

#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "defines.h"

#define DELMAP_SIZE 0x1000

#define DEL_CHUNK_SHIFT 20
#define DEL_CHUNK_SIZE (1 << (DEL_CHUNK_SHIFT - 3))

#if !defined (MAP_ANONYMOUS) && defined (MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

class CDelMap
{
public:
	ULONG* m_chunks[DELMAP_SIZE];
	pthread_mutex_t m_mutex;
	int m_fd;
	int m_empty;

public:
	CDelMap()
	{
		memset(m_chunks, 0, sizeof(m_chunks));
#ifdef MAP_ANONYMOUS
		m_fd = -1;
#else
		m_fd = open("/dev/zero", O_RDWR);
#endif
		pthread_mutex_init(&m_mutex, NULL);
		m_empty = 1;
	}
	int IsSet(ULONG urlID)
	{
		ULONG* chunk = m_chunks[urlID >> DEL_CHUNK_SHIFT];
		return chunk && (chunk[(urlID & ((DEL_CHUNK_SIZE << 3) - 1)) >> 5] & (1 << (urlID & 0x1F)));
	}
	~CDelMap();
	void Set(ULONG urlID);
	void Clear(ULONG urlID);
	void Read(int f);
	void Write(int f);
	void Read();
	void Write();
	void Erase();
};

extern CDelMap DelMap;

class CDelMapReuse
{
public:
	int m_file;
	long m_length;
	ULONG* m_map;
	ULONG m_urlID;
	pthread_mutex_t m_mutex;

public:
	CDelMapReuse()
	{
		m_file = -1;
		m_length = 0;
		m_map = NULL;
		m_urlID = 0;
		pthread_mutex_init(&m_mutex, NULL);
	}
	~CDelMapReuse()
	{
		pthread_mutex_destroy(&m_mutex);
	}
	void Sync();
	void Open();
	void Close();
	ULONG Get();
	void Put(ULONG urlID);
};

extern CDelMapReuse DelMapReuse;

#endif /* _DELMAP_H_ */
