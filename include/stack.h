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

/*  $Id: stack.h,v 1.9 2002/01/09 11:50:15 kir Exp $
	Author: Alexander F. Avdonkin
*/

#ifndef _STACK_H_
#define _STACK_H_

#include <vector>
#include "ccom.h"

using std::vector;

class Stack : public vector<CPtr<CStackElem> >
{
public:
	~Stack()
	{
		for (iterator expr = begin(); expr < end(); expr++)
		{
			expr->~CPtr();
		}
	}

/*
	CStackElem* removeLast()
	{
		return *erase((CPtr<CStackElem>*)&back());
	}
*/
	void removeLast(CPtr<CStackElem>& elem)
	{
		elem = back();
		pop_back();
	}

	CStackElem* last()
	{
		return back();
	}

	void addLast_(CStackElem* elem)
	{
		insert(end(), elem);
	}
};
/*
class CBoolToken : public CStackElem
{
public:
	virtual BOOL Value() = 0;
};

class CFalse : public CBoolToken
{
public:
	virtual char* ClassName() {return "CFalse";};
	virtual BOOL Value() {return false;};

};

class CTrue : public CBoolToken
{
public:
	virtual char* ClassName() {return "CTrue";};
	virtual BOOL Value() {return true;};
};
*/
class CNumToken : public CStackElem
{
public:
	int m_value;

public:
	CNumToken(int value) {m_value = value;};
	virtual char* ClassName() {return "CNumToken";};
};

extern char* Empty;

class CToken : public CStackElem
{
public:
	char* m_token;

public:
	virtual char* ClassName() {return "CToken";};

public:
	CToken()
	{
		m_token = NULL;
	}
	CToken(char* p_token)
	{
		m_token = p_token;
	};
	virtual ~CToken()
	{
		if (m_token && (m_token != Empty))
		delete m_token;
	}
};

class CTokenArray : public CStackElem
{
public:
	Stack m_tokens;

public:
	virtual char* ClassName() {return "CTokenArray";};

public:
	CTokenArray() {};
	CTokenArray(Stack& p_stack, int p_iLast)
	{
		int iFirst = p_stack.size() - p_iLast;
		m_tokens.insert(m_tokens.end(), p_stack.begin() + iFirst, p_stack.end());
		p_stack.resize(iFirst);
/*		for (int i = iFirst; i < p_stack.size(); i++)
		{
			m_tokens.insert(m_tokens.end(), p_stack[i]);
		}*/
//		p_stack.erase((CPtr<CStackElem>*)&p_stack[iFirst], p_stack.end());
	}
	~CTokenArray()
	{
/*		for (int i = 0; i < m_tokens.size(); i++)
		{
			m_tokens[i]->Release();
		}*/
	}
	int size() {return m_tokens.size();};
};

#endif
