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

/*  $Id: buddy.cpp,v 1.13 2002/05/11 15:27:27 kir Exp $
    Author : Alexander F. Avdonkin
*/

#include "aspseek-cfg.h"

#include <unistd.h>
#include <sys/types.h>

#include <algorithm>

#include "index.h"
#include "buddy.h"

//CBuddyHeap vwHeap;

void CheckHeap(CBuddyHeap* heap)
{
	for (void** ppool = heap->m_pools; ppool < heap->m_pools + MEM_PAGE_SHIFT - MIN_POOL; ppool++)
	{
//		int pool_num = ppool - heap->m_pools;
		void** pp = ppool;
		FreeHead* pool = (FreeHead*)(*ppool);
		for (; pool; pool = (FreeHead*)pool->m_next)
		{
			if (pool->m_flag != ~0U)
			{
				printf("Free flag is invalid\n");
			}
			if (pool->m_prev != pp)
			{
				printf("Prev flag is invalid\n");
			}
			pp = &pool->m_next;
		}
	}
}

void* CBuddyHeap::AllocPage()
{
	return ANON_MMAP(MEM_PAGE_SIZE)
}

void CBuddyHeap::FreePage(void* page)
{
	munmap((char *)page, MEM_PAGE_SIZE);
}

void* CBuddyHeap::Alloc(int pool)
{
	CLocker lock(&m_mutex);
	void** ppool = m_pools + pool;
	void** ppools = ppool;
	if (*ppool)
	{
		FreeHead* res = (FreeHead*)(*ppool);
		*res->m_prev = res->m_next;
		if (res->m_next) ((FreeHead*)(res->m_next))->m_prev = res->m_prev;
		res->m_flag = 0;
		m_allocated += (MIN_POOL_SIZE << pool);
		return res;
	}
	else
	{
		while ((++ppool < m_pools + MEM_PAGE_SHIFT - MIN_POOL) && (*ppool == NULL));
		FreeHead* res;
		if (ppool >= m_pools + MEM_PAGE_SHIFT - MIN_POOL)
		{
			ppool = m_pools + MEM_PAGE_SHIFT - MIN_POOL - 1;
			*ppool = AllocPage();
			if (*ppool == NULL) return NULL;
			m_pgCount++;
			res = (FreeHead*)(*ppool);
			res->m_flag = ~0U;
			res->m_next = NULL;
			res->m_prev = ppool;
			res->m_pool = MEM_PAGE_SHIFT - MIN_POOL - 1;
		}
		else
		{
			res = (FreeHead*)(*ppool);
		}
		*res->m_prev = res->m_next;
		if (res->m_next) ((FreeHead*)(res->m_next))->m_prev = res->m_prev;
		res->m_flag = 0;
		while (--ppool >= ppools)
		{
			ULONG offset = MIN_POOL_SIZE << (ppool - m_pools);
			FreeHead* buddy = (FreeHead*)((char*)(res) + offset);
			if (*ppool) ((FreeHead*)(*ppool))->m_prev = &buddy->m_next;
			buddy->m_flag = ~0U;
			buddy->m_next = *ppool;
			buddy->m_prev = ppool;
			buddy->m_pool = ppool - m_pools;
			*ppool = buddy;
		}
		m_allocated += (MIN_POOL_SIZE << pool);
		return res;
	}
}

void CBuddyHeap::Free(void* ptr, int pool)
{
	CLocker lock(&m_mutex);
	void** ppool = m_pools + pool;
	FreeHead* fh = (FreeHead*)ptr;
	fh->m_flag = ~0U;
	while (ppool < m_pools + MEM_PAGE_SHIFT - MIN_POOL - 1)
	{
		int pool_num = ppool - m_pools;
		FreeHead* buddy = (FreeHead*)((unsigned long)fh ^ (MIN_POOL_SIZE << (pool_num)));
		if ((buddy->m_flag == ~0U) && (buddy->m_pool == pool_num))
		{
			*buddy->m_prev = buddy->m_next;
			if (buddy->m_next) ((FreeHead*)(buddy->m_next))->m_prev = buddy->m_prev;
			if (fh > buddy) fh = buddy;
			ppool++;
		}
		else
		{
			break;
		}
	}
	if (ppool == m_pools + MEM_PAGE_SHIFT - MIN_POOL - 1)
	{
		FreePage(fh);
		m_pgCount--;
	}
	else
	{
		if (*ppool) ((FreeHead*)(*ppool))->m_prev = &fh->m_next;
		fh->m_next = *ppool;
		fh->m_prev = ppool;
		fh->m_pool = ppool - m_pools;
		*ppool = fh;
	}
	m_allocated -= (MIN_POOL_SIZE << pool);
}

int GetPool(ULONG size)
{
	int pool = 0;
	ULONG psize = MIN_POOL_SIZE;
	while (psize < size)
	{
		psize <<= 1;
		pool++;
	}
	return pool;
}

void* CBuddyHeap::AllocBytes(ULONG size)
{
	int pool = GetPool(size);
	return pool < MEM_PAGE_SHIFT - MIN_POOL - 1 ? Alloc(pool) : ANON_MMAP(size);
}

void CBuddyHeap::FreeBytes(void* ptr, ULONG size)
{
	int pool = GetPool(size);
	if (pool < MEM_PAGE_SHIFT - MIN_POOL - 1)
	{
		Free(ptr, pool);
	}
	else
	{
		munmap((char *)ptr, size);
	}
}

void CWordBuddyVector::push_back(WORD value)
{
	if (m_data == NULL)
	{
		m_data = (WORD*)m_heap->Alloc(0);
		m_data[0] = 0;
		m_allocated = MIN_POOL_SIZE / sizeof(WORD);
//		logger.log(CAT_ALL, L_INFO, "Allocated 8 elements at %08x\n", m_data);
	}
	if (m_allocated == m_size)
	{
		WORD* olddata = m_data;
		m_data = (WORD*)m_heap->AllocBytes(m_allocated * sizeof(WORD) << 1);
		memcpy(m_data, olddata, m_allocated * sizeof(WORD));
		m_heap->FreeBytes(olddata, m_allocated * sizeof(WORD));
		m_allocated <<= 1;
//		logger.log(CAT_ALL, L_INFO, "Reallocated %lu elements from %08x to %08x\n", m_allocated, olddata, m_data);
	}
	m_data[m_size++] = value;
//	printf("Pushing back value: %u\n", value);
//	CheckHeap(m_heap);
}

CWordBuddyVector::~CWordBuddyVector()
{
	if (m_data)
	{
		m_heap->FreeBytes(m_data, m_allocated * sizeof(WORD));
//		logger.log(CAT_ALL, L_INFO, "Freed %lu elements at %08x\n", m_allocated, m_data);
	}
}

#ifdef TEST_BUDDY

int compare(void* a, void* b)
{
	return ((ULONG)a % 0x1003) < ((ULONG)b % 0x1003);
}

void BuddyTest(int count)
{
	CBuddyHeap heap;
//	hash_set<ULONG> elems;
	void** elems = new void*[count];
	for (int i = 0; i < count; i++)
	{
		elems[i] = heap.AllocBytes(16 << (((i * 11) % 7) + 3));
//		elems[i] = heap.Alloc(0);
//		elems.insert((ULONG)heap.Alloc(0));
//		*(int*)(elems[i]) = i;
	}
//	heap.FreeBytes(elems[0], 16);
//	elems[0] = heap.AllocBytes(32);
	printf("Allocated: %lu, pages: %lu\n", heap.m_allocated, heap.m_pgCount);
//	sort<void**>(elems, elems + count, compare);
	for (int i = 0; i < count; i++)
//	for (hash_set<ULONG>::iterator it = elems.begin(); it != elems.end(); it++)
	{
		heap.FreeBytes(elems[i], (16 << ((i * 11) % 7) + 3));
//		heap.FreeBytes(elems[i], 16);
//		heap.Free((void*)*it, 0);
	}
//	heap.FreeBytes(elems[0], 32);
	delete elems;
	printf("Allocated: %lu, pages: %lu\n", heap.m_allocated, heap.m_pgCount);
}

void VectorTest()
{
	CBuddyHeap h;
	{
	CWordBuddyVector v;
	v.m_heap = &h;
	printf("Size: %i\n", v.size());
	v.push_back(0xffff);
	printf("Size: %i\n", v.size());
	v.push_back(0xffff);
	printf("Size: %i\n", v.size());
	v.push_back(2);
	for (WORD* w = v.begin(); w != v.end(); w++)
		printf("%u ", *w);
	printf("\n");
	for (int i = 0; i < 50000; i++)
		v.push_back(i);
	printf("Size: %i\n", v.size());
	}

}

/*
main(int args, char** argv)
{
	BuddyTest(argv[1] ? atoi(argv[1]) : 100000);
	BuddyTest(argv[1] ? atoi(argv[1]) : 100000);
}


main()
{
	VectorTest();
}
*/

#endif /* TEST_BUDDY */
