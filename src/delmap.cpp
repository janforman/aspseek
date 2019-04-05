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

/*  $Id: delmap.cpp,v 1.5 2002/01/29 11:20:47 al Exp $
 *  Author: Alexander Avdonkin
 */

#include "delmap.h"
#include "buddy.h"
#include "index.h"

CDelMap DelMap;
CDelMapReuse DelMapReuse;

CDelMap::~CDelMap()
{
	for (int i = 0; i < DELMAP_SIZE; i++)
	{
		if (m_chunks[i])
		{
			munmap((char *)m_chunks[i], DEL_CHUNK_SIZE);
		}
	}
#ifndef MAP_ANONYMOUS
	close(m_fd);
#endif
	pthread_mutex_destroy(&m_mutex);
}

void CDelMap::Set(ULONG urlID)
{
	CLocker lock(&m_mutex);
	ULONG*& chunk = m_chunks[urlID >> DEL_CHUNK_SHIFT];
	if (chunk == NULL)
	{
		chunk = (ULONG*)ANON_MMAP(DEL_CHUNK_SIZE)
		memset(chunk, 0, DEL_CHUNK_SIZE);
	}
	chunk[(urlID & ((DEL_CHUNK_SIZE << 3) - 1)) >> 5] |= (1 << (urlID & 0x1F));
	m_empty = 0;
}

void CDelMap::Clear(ULONG urlID)
{
	CLocker lock(&m_mutex);
	ULONG*& chunk = m_chunks[urlID >> DEL_CHUNK_SHIFT];
	if (chunk == NULL) return;
	chunk[(urlID & ((DEL_CHUNK_SIZE << 3) - 1)) >> 5] &= ~(1 << (urlID & 0x1F));
}

void CDelMap::Read(int f)
{
	char zero[DEL_CHUNK_SIZE];
	int ind = 0;
	while (read(f, zero, DEL_CHUNK_SIZE) == DEL_CHUNK_SIZE)
	{
		for (ULONG* p = (ULONG*)zero; p < (ULONG*)(zero + DEL_CHUNK_SIZE); p++)
		{
			if (*p)
			{
				m_chunks[ind] = (ULONG*)ANON_MMAP(DEL_CHUNK_SIZE)
				memcpy(m_chunks[ind], zero, DEL_CHUNK_SIZE);
				m_empty = 0;
				break;
			}
		}
		ind++;
	}
}

void CDelMap::Write(int f)
{
	int m = DELMAP_SIZE - 1;
	while ((m >= 0) && (m_chunks[m] == NULL)) m--;
	char zero[DEL_CHUNK_SIZE];
	memset(zero, 0, sizeof(zero));
	for (int i = 0; i <= m; i++)
	{
		if (m_chunks[i])
		{
			write(f, m_chunks[i], DEL_CHUNK_SIZE);
		}
		else
		{
			write(f, zero, DEL_CHUNK_SIZE);
		}
	}
}

void CDelMap::Read()
{
	char fname[PATH_MAX];
	int f;
	sprintf(fname, "%s/delmap", DBWordDir.c_str());
	if ((f = open(fname, O_RDONLY)))
	{
		Read(f);
		close(f);
	}
}

void CDelMap::Write()
{
	char fname[PATH_MAX];
	int f;
	sprintf(fname, "%s/delmap", DBWordDir.c_str());
	if ((f = open(fname, O_WRONLY | O_CREAT, 0664)))
	{
		Write(f);
		close(f);
	}
}

void CDelMap::Erase()
{
	char fname[PATH_MAX];
	sprintf(fname, "%s/delmap", DBWordDir.c_str());
	char fnamer[PATH_MAX];
	sprintf(fnamer, "%s/delmapr", DBWordDir.c_str());
	int d = open(fnamer, O_RDWR | O_CREAT, 0664);
	if (d >= 0)
	{
		int m = DELMAP_SIZE - 1;
		while ((m >= 0) && (m_chunks[m] == NULL)) m--;
		ULONG buf[DEL_CHUNK_SIZE / sizeof(ULONG)];
		for (int i = 0; i <= m; i++)
		{
			ULONG** chunk = m_chunks + i;
			if (*chunk)
			{
				if (read(d, buf, sizeof(buf)) == sizeof(buf))
				{
					ULONG* pb = buf;
					for (ULONG* pc = *chunk; pc < *chunk + DEL_CHUNK_SIZE / sizeof(ULONG); )
					{
						*pb++ |= *pc++;
					}
					lseek(d, -sizeof(buf), SEEK_CUR);
					write(d, buf, sizeof(buf));
				}
				else
				{
					write(d, *chunk, DEL_CHUNK_SIZE);
				}
			}
			else
			{
				if (lseek(d, DEL_CHUNK_SIZE, SEEK_CUR) < DEL_CHUNK_SIZE * (i + 1))
				{
					memset(buf, 0, sizeof(buf));
					lseek(d, DEL_CHUNK_SIZE * i, SEEK_SET);
					write(d, buf, sizeof(buf));
				}
			}
		}
		close(d);
	}
	unlink(fname);
}

void CDelMapReuse::Open()
{
	char fname[PATH_MAX];
	sprintf(fname, "%s/delmapr", DBWordDir.c_str());
	m_file = open(fname, O_RDWR);
	if (m_file >= 0)
	{
		m_length = lseek(m_file, 0, SEEK_END);
		if (m_length > 0)
		{
			m_map = (ULONG*)mmap(NULL, m_length, PROT_READ | PROT_WRITE, MAP_SHARED, m_file, 0);
			m_urlID = 1;
		}
	}
}

void CDelMapReuse::Close()
{
	if (m_map)
	{
		Sync();
		munmap((char *)m_map, m_length);
		close(m_file);
	}
}

ULONG CDelMapReuse::Get()
{
	if ((m_map == NULL) || (m_length == 0)) return 0;
	CLocker lock(&m_mutex);
	ULONG off = m_urlID >> 5;
	if ((off << 2) >= (ULONG)m_length) return 0;
	ULONG* pmap = m_map + off;
	ULONG mask = 1 << (m_urlID & 0x1F);
	if (*pmap & ~(mask - 1))
	{
		for (; mask; mask <<= 1, m_urlID++)
		{
			if (*pmap & mask)
			{
				*pmap &= ~mask;
				return m_urlID++;
			}
		}
	}
	m_urlID = (m_urlID | ((sizeof(ULONG) << 3) - 1)) + 1;
	for (off++, pmap++; (off << 2) < (ULONG)m_length; pmap++, off++, m_urlID += (sizeof(ULONG) << 3))
	{
		if (*pmap)
		{
			for (mask = 1; mask; mask <<= 1, m_urlID++)
			{
				if (*pmap & mask)
				{
					*pmap &= ~mask;
					return m_urlID++;
				}
			}
		}
	}
	return 0;
}

void CDelMapReuse::Put(ULONG urlID)
{
	if ((m_map == NULL) || (m_length == 0)) return;
	CLocker lock(&m_mutex);
	ULONG off = urlID >> 5;
	if ((off << 2) < (ULONG)m_length)
	{
		m_map[off] |= (1 << (urlID & 0x1F));
		if (m_urlID > urlID) m_urlID = urlID;
	}
}

void CDelMapReuse::Sync()
{
	if (m_map) msync((char *)m_map, m_length, MS_SYNC);
}
