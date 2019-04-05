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

/*  $Id: streams.h,v 1.7 2002/01/09 11:50:15 kir Exp $
	Author: Alexander F. Avdonkin
*/

#ifndef _STREAMS_H_
#define _STREAMS_H_

#include "aspseek-cfg.h"
#include "defines.h"
#include <stddef.h>

#ifdef UNICODE
class CCharsetB;
#endif

class CReadStream
{
public:
	const char* m_source;
	int m_position;
	int m_extra;

public:
	CReadStream(const char* p_source)
	{
		m_source = p_source;
		m_position = 0;
		m_extra = 0;
	};

	CReadStream();
	virtual ~CReadStream();

public:
	BOOL atEnd();
	BOOL peekFor(char c);
#ifdef UNICODE
	int next(BYTE* dest, CCharsetB* charset);
	BYTE nextChar();
#else
	char next();
#endif
	void skip_(int n);
	int position()
	{
		return m_position;
	};

	void position_(int p_position)
	{
		m_extra = 0;
		m_position = p_position;
	}

//	char* GetNumber();
};


class CWriteStream
{
public:
	char* m_buffer;
	int m_iLength;
	int m_iCount;

public:
	CWriteStream()
	{
		m_buffer = NULL;
		m_iLength = 0;
		m_iCount = 0;
	};

	virtual ~CWriteStream();

public:
	void reset();
	void nextPut_(char);
	char* contents();
	int GetPosition()
	{
		return m_iCount;
	}
	void SetPosition(int count)
	{
		m_iCount = count;
	}
//	int position()
};

#endif
