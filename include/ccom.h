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

/*  $Id: ccom.h,v 1.8 2004/03/22 13:33:31 kir Exp $
    Author : Alexander F. Avdonkin
*/
#ifndef S_OK
#define E_POINTER 0x80004003L
#define S_OK 0
#endif

#ifndef _CCOM_
#define _CCOM_

#include <assert.h>
#include "defines.h"

typedef unsigned long HRESULT;

class CStackElem
{
int m_refCount;
public:
int AddRef() {return ++m_refCount;};
void Release() {if (--m_refCount == 0) delete this;};
CStackElem() {m_refCount = 0;};
virtual int IsNot() {return 0;};
virtual int IsStop() {return 0;};
virtual int IsEmpty() {return 0;}
virtual int IsSiteFilter() {return 0;};
//      virtual char* ClassName() = 0;
virtual ~CStackElem() {};
};
									

template <class T>
inline T* CPtrAssign(T** pp, T* lp)
{
	if (lp != NULL)
		lp->AddRef();
	if (*pp)
		(*pp)->Release();
	*pp = lp;
	return lp;
}


template <class T>
class _NoAddRefReleaseOnCPtr : public T
{
};


template <class T>
class CPtr
{
public:
	typedef T _PtrClass;
	CPtr()
	{
		p=NULL;
	}
	CPtr(T* lp)
	{
		if ((p = lp) != NULL)
			p->AddRef();
	}
	CPtr(const CPtr<T>& lp)
	{
		if ((p = lp.p) != NULL)
			p->AddRef();
	}
	~CPtr()
	{
		if (p)
		{
#ifdef _DEBUG
			if ((*(DWORD*)p & 0x10000000) != 0x10000000) OLEDB_Assert("1", __FILE__, __LINE__);
#endif
			((CStackElem *)p)->Release();
		}
	}
	void Release()
	{
//		IUnknown* pTemp = p;
//		if (pTemp)
//		{
			p = NULL;
//			pTemp->Release();
//		}
	}
	operator T*() const
	{
		return (T*)p;
	}
	T& operator*() const
	{
		ATLASSERT(p!=NULL);
		return *p;
	}
	//The assert on operator& usually indicates a bug.  If this is really
	//what is needed, however, take the address of the p member explicitly.
	T** operator&()
	{
//		ASSERT(p==NULL);
		return &p;
	}
	_NoAddRefReleaseOnCPtr<T>* operator->() const
	{
		ASSERT(p!=NULL);
		return (_NoAddRefReleaseOnCPtr<T>*)p;
	}
	T* operator=(T* lp)
	{
		return (T*)CPtrAssign(&p, lp);
	}
	T* operator=(const CPtr<T>& lp)
	{
		return (T*)CPtrAssign(&p, lp.p);
	}
	bool operator!() const
	{
		return (p == NULL);
	}
	bool operator<(T* pT) const
	{
		return p < pT;
	}
	bool operator==(T* pT) const
	{
		return p == pT;
	}
	void Attach(T* p2)
	{
		if (p)
			p->Release();
		p = p2;
	}
	T* Detach()
	{
		T* pt = p;
		p = NULL;
		return pt;
	}
	HRESULT CopyTo(T** ppT)
	{
		ATLASSERT(ppT != NULL);
		if (ppT == NULL)
			return E_POINTER;
		*ppT = p;
		if (p)
			p->AddRef();
		return S_OK;
	}
	T* p;
};

#endif
