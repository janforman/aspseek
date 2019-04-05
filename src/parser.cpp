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

/*  $Id: parser.cpp,v 1.13 2002/06/15 12:55:03 al Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CGeneralParser
*/

#include "aspseek-cfg.h"

#include "parser.h"
#include "streams.h"

BOOL CGeneralParser::peekForType_(int t1)
{
	if (m_tokenType == t1)
	{
		m_stack.insert(m_stack.end(), new CToken(m_token));
		m_token = NULL;
		scanToken();
		return true;
	}
	return false;
}

BOOL CGeneralParser::peekForType_value_(int t1, char* t2)
{
	if (((m_tokenType == t1) && (strcmp(m_token, t2) == 0)))
	{
		scanToken();
		return true;
	}
	return false;
}

BOOL CGeneralParser::peekForTypeNoPush_(int t1)
{
	if (m_tokenType == t1)
	{
		scanToken();
		return true;
	}
	return false;
}

BOOL CGeneralParser::peekForTypeNoPush_(int t1, const char* letters)
{
	if (m_tokenType == t1)
	{
		scanToken(letters);
		return true;
	}
	return false;
}

BOOL CGeneralParser::popStackTo_(int t1)
{
	if (t1 > (int)m_stack.size())
	{
		return false;
	}
	if (t1 < (int)m_stack.size())
	{
		for (int i = m_stack.size() - 1; i >= t1; i--)
		{
			if (m_stack[i]) m_stack[i]->Release();
		}
		m_stack.erase(m_stack.begin() + t1, m_stack.end());
	}
	return true;
}

void CGeneralParser::replaceStackTop_(CStackElem* t1)
{
	m_stack.back() = t1;
}


void CGeneralParser::push(CStackElem *elem)
{
	m_stack.push_back(elem);
}

void CGeneralParser::combineStackTo_(int t1)
{
	int t2;
	t2 = m_stack.size();
	if (t1 == t2)
	{
		m_stack.insert(m_stack.end(), new CTokenArray);
		return;
	}
	if (((t1 > t2) || (t1 < 0)))
	{
		error_("Invalid size for combining stack");
	}
	CTokenArray *p = new CTokenArray(m_stack, t2 - t1);
	m_stack.insert(m_stack.end(), p);
	return;
}

void CGeneralParser::backupSave()
{
	m_backupStack.insert(m_backupStack.end(), sourcePosition());
	m_backupStack.insert(m_backupStack.end(), m_stack.size());
}

BOOL CGeneralParser::backupRestore()
{
	popStackTo_(m_backupStack.back());
	m_backupStack.pop_back();
	sourcePosition_(m_backupStack.back());
	m_backupStack.pop_back();
	return false;
}

BOOL CGeneralParser::backupDiscard()
{
	m_backupStack.pop_back();
	m_backupStack.pop_back();
	return true;
}

int CGeneralParser::sourcePosition()
{
	return m_mark - 1;
}

void CGeneralParser::sourcePosition_(int t1)
{
	m_source->position_(t1);
	stepChar();
	scanToken();
	return;
}

void CGeneralParser::xLetter()
{
	int t1=0;
	BYTE t2[3];
	int len;
	m_buffer->reset();
	PutHereChar();
	while (((((len = Next(t2)) != 0) && (((t1 = GetType(t2, len)) == xLetter1) || (t1 == xDigit1) || (t1 == colon)))))
	{
		Put(t2, len);
		if (t1 == colon)
		{
			len = Next(t2);
			break;
		}
	}
	if (((len != 0) && (t1 == m_keywordMarker)))
	{
		Put(t2, len);
		len = Next(t2);
		m_tokenType = keyword;
	}
	else
	{
		m_tokenType = word;
	}
	SetHereChar(t2, len);
	SetToken(m_buffer->contents());
	string token(m_token);
	MapStringToInt::iterator iter = m_reservedWords.find(token);
	if (iter != m_reservedWords.end())
	{
		m_tokenType = iter->second;
	}
}

void CGeneralParser::initScanner()
{
	CScanner::initScanner();
	initReserved();
	m_keywordMarker = -1;
}

BOOL CGeneralParser::notify_(char* mes)
{
	if (m_backupStack.size() > 0)
	{
		return false;
	}
	error(mes);
	return false;
}

BOOL CGeneralParser::offEnd_(char* t1)
{
	return notify_(t1);
}

void CGeneralParser::error(char *p_message)
{
	throw new CParserException(1, p_message);
}
