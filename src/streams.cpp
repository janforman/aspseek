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

/*  $Id: streams.cpp,v 1.11 2002/01/09 11:50:15 kir Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CReadStream, CWriteStream
*/

#include <ctype.h>
#include <string.h>
#include "streams.h"
#ifdef UNICODE
#include "ucharset.h"
#endif
#include "misc.h"

CReadStream::CReadStream()
{

}

CReadStream::~CReadStream()
{

}

#ifdef UNICODE
int CReadStream::next(BYTE* dest, CCharsetB* charset)
{
	BYTE* d = dest;
	BYTE c = nextChar();
	if (c)
	{
		*d++ = c;
		int len = charset->CharLen(c);
		for (int i = 1; i < len; i++)
		{
			c = nextChar();
			if (c)
			{
				*d++ = c;
			}
			else
			{
				memset(d, 0, len - i);
				break;
			}
		}
		return len;
	}
	else
	{
		return 0;
	}
}

BYTE CReadStream::nextChar()
#else
char CReadStream::next()
#endif
{
	char r = m_source[m_position];
	if (r) m_position++; else m_extra++;
	return (BYTE)r;
}

void CReadStream::skip_(int n)
{
	if (n < 0)
	{
		if (m_extra > 0)
		{
			int extra = min(m_extra, -n);
			n += extra;
			m_extra -= extra;
		}
		m_position += n;
		if (m_position < 0) m_position = 0;
	}
	else
	{
		while ((n-- > 0) && (m_source[++m_position]));
	}
}
/*
char* CReadStream::GetNumber()
{
	CWriteStream ws;
	char c;
	if (atEnd()) return NULL;
	if (peekFor('-'))
	{
		ws.nextPut_('-');
	}
	while (isdigit(c = next()))
	{
		ws.nextPut_(c);
	}
	if (c == '.')
	{
		ws.nextPut_(c);
		while (isdigit(c = next()))
		{
			ws.nextPut_(c);
		}
	}
	skip_(-1);
	return ws.contents();
}
*/
BOOL CReadStream::peekFor(char c)
{
	if (c == 0)
	{
		return false;
	}
	else
	{
		if (m_source[m_position] == c)
		{
			m_position++;
			return true;
		}
		else
		{
			return false;
		}
	}
}

BOOL CReadStream::atEnd()
{
	return m_source[m_position] == 0;
}

CWriteStream::~CWriteStream()
{
	if (m_buffer)
	{
		delete m_buffer;
	}
}

void CWriteStream::reset()
{
	if (m_buffer)
	{
		delete m_buffer;
		m_buffer = NULL;
	}
	m_iCount = 0;
}

void CWriteStream::nextPut_(char c)
{
	if (m_iCount + 1 >= m_iLength)
	{
		int iLength;
		iLength = m_iLength + (m_iLength >> 1);
		if (iLength < 10) iLength = 10;
		char* chNewBuff = new char[iLength];
		if (m_buffer != NULL)
		{
			memcpy(chNewBuff, m_buffer, m_iLength);
			delete m_buffer;
		}
		m_buffer = chNewBuff;
		m_iLength = iLength;
	}
	m_buffer[m_iCount++] = c;
}

char* Empty = "";

char* CWriteStream::contents()
{
	if (m_iLength == 0) return Empty;
	m_buffer[m_iCount] = 0;
	char* buffer = m_buffer;
	m_buffer = NULL;
	m_iLength = 0;
	m_iCount = 0;
	return buffer;
}
