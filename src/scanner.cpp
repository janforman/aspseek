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

/*  $Id: scanner.cpp,v 1.15 2004/03/22 13:25:51 kir Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CScanner

*/
#include "aspseek-cfg.h"

#include <string.h>
#include "scanner.h"
#include "streams.h"


struct {int type; ScannerVoidFunc func;}
funcs[] =
{
{xBinary1, &CScanner::xBinary},
{xLetter1, &CScanner::xLetter},
{xDigit1, &CScanner::xDigit},
{xDelimiter1, &CScanner::xDelimiter},
{xDefault1, &CScanner::xDefault},
{xDoubleQuote1, &CScanner::xDoubleQuote},
{xDollar1, &CScanner::xDollar},
{xSingleQuote1, &CScanner::xSingleQuote},
{xLeftBracket1, &CScanner::xLeftBracket}
#ifdef UNICODE
,{xIdeograph1, &CScanner::xIdeograph}
#endif
};

ScannerVoidFunc xFuncs[maxExtended + 1];

TypeTable typeTable = {
xDelimiter1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDelimiter1,
xDelimiter1,
xDefault1,
xDelimiter1,
xDelimiter1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDelimiter1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDelimiter1,
xDelimiter1,
xDoubleQuote1,
xDelimiter1,
xLetter1,
xDelimiter1,
xDelimiter1,
xDelimiter1,
leftParenthesis,
rightParenthesis,
xLetter1,			// '*' is letter
xBinary1,
xDelimiter1, //ascii54,
xBinary1,
xDelimiter1, //period,
xDelimiter1,
xDigit1,
xDigit1,
xDigit1,
xDigit1,
xDigit1,
xDigit1,
xDigit1,
xDigit1,
xDigit1,
xDigit1,
colon,
xDelimiter1,
xDelimiter1,
xDelimiter1,
xDelimiter1,
xLetter1,
xDelimiter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xDelimiter1,
xDelimiter1,
xDelimiter1,
xDelimiter1,
xDelimiter1,
xDelimiter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xDelimiter1,
verticalBar,
xDelimiter1,
xDelimiter1,
xDelimiter1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xBinary1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xBinary1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xDefault1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1,
xLetter1
};

void CScanner::initScanner()
{
	m_buffer = new CWriteStream;
	m_saveComments = true;
	memcpy(m_typeTable, typeTable, sizeof(m_typeTable));
#ifdef UNICODE
	m_len = 0;
#else
	m_hereChar = 0;
#endif
	m_token = NULL;
	for (int i = 0; i <= maxExtended; i++)
	{
		xFuncs[funcs[i].type] = funcs[i].func;
	}
}

void CScanner::perform_(int p_tokenType)
{
	ScannerVoidFunc func = xFuncs[p_tokenType];
	(this->*func)();
}

int CScanner::step(BYTE* t1)
{
	int len = GetHereChar(t1);
	Next();
	return len;
}

void CScanner::stepChar()
{
	BYTE t[3];
	step(t);
}

void CScanner::scanToken(const char* letters)
{
	if (*letters)
	{
		int old = m_typeTable[(unsigned char)*letters];
		m_typeTable[(unsigned char)*letters] = xLetter1;
		scanToken(letters + 1);
		m_typeTable[(unsigned char)*letters] = old;
	}
	else
	{
		scanToken();
	}
}

char* CScanner::scanToken()
{
	int t1;
	m_delim = 0;
	while (true)
	{
		if (EmptyHere())
		{
			m_mark = (m_prevEnd = m_source->position()) + 1;
			m_tokenType = doIt;
			SetToken((char*)NULL);
			return NULL;
		}
		t1 = 0;
		if ((m_tokenType = GetHereType()) != xDelimiter1)
		{
			break;
		}
		m_delim = 1;
		t1++;
		Next();
	}
	m_prevEnd = (m_mark = m_source->position()) - t1 - 1;
	if (m_tokenType == xLetter1)
	{
		xLetter();
	}
	else
	{
		if (IsExtendedType())
		{
			perform_(m_tokenType);
		}
		else
		{
			SetHereToken();
			Next();
		}
	}
	return m_token;
}

int CScanner::mapCompoundTypes_(int t1)
{
	if (((t1 == compoundWord) || (((t1 == compoundKeyword) || (t1 == compoundBinary)))))
	{
		return t1;
	}
	if (t1 == word)
	{
		return compoundWord;
	}
	if (t1 == binary)
	{
		return compoundBinary;
	}
	if (t1 == keyword)
	{
		return compoundKeyword;
	}
	error_("Unexpected compound token");
	return 0;
}

BOOL CScanner::notify_(char* t1)
{
	return false;
}

BOOL CScanner::offEnd_(char* t1)
{
	return notify_(t1);
}

CScanner* CScanner::on_(CReadStream* t1)
{
	m_source = t1;
	Next();
	m_prevEnd = 0;
	return this;
}

CScanner* CScanner::scan_(CReadStream* t1)
{
	initScanner();
	on_(t1);
	scanToken();
	return this;
}

void CScanner::xLetter()
{
	int t1=0;
	char* t2;
	BYTE t3[3];
	int len;
	m_buffer->reset();
	PutHereChar();
	while ((((len = Next(t3) != 0) && (((t1 = GetType(t3, len)) == xLetter1) || (t1 == xDigit1)))))
	{
		Put(t3, len);
	}
	if (t1 == period)
	{
		if (!(((Next(), EmptyHere()) == 0) || (m_tokenType = GetHereType()) == xDelimiter1))
		{
			if (((m_tokenType == xLetter1) || (m_tokenType == xBinary1)))
			{
				Put(t3, len);
				t2 = m_buffer->contents();
				perform_(m_tokenType);
				InsertPrefix(t2);
				m_tokenType = mapCompoundTypes_(m_tokenType);
				return;
			}
			m_source->skip_(-1);
		}
	}
	if (t1 == colon)
	{
		Put(t3, len);
		len = Next(t3);
		m_tokenType = keyword;
	}
	else
	{
		m_tokenType = word;
	}
	SetHereChar(t3, len);
	SetToken(m_buffer->contents());
	return;
}

#ifdef UNICODE
void CScanner::xIdeograph()
{
	int t1 = 0;
	BYTE t2[3];
	const BYTE* t;
	int len = 0;
	CWordSet* dic = m_ucharset->GetDictionary();
	const CWordLetter* letter = NULL;
	int useDic = dic == NULL ? 0 : 1;
	if (useDic) letter = dic->Find(m_ucharset->UCode(t = m_hereChar), letter);
	m_buffer->reset();
	PutHereChar();
	int wp = m_buffer->GetPosition();
	int rp = m_source->position();
	if (letter)
	{
		while (((((len = Next(t2)) != 0) && (((t1 = GetType(t2, len)) == xIdeograph1)))) && ((useDic == 0) || (letter = dic->Find(m_ucharset->UCode(t = t2), letter))))
		{
			Put(t2, len);
			if (letter->m_end)
			{
				wp = m_buffer->GetPosition();
				rp = m_source->position();
			}
		}
	}
	if (useDic)
	{
		m_buffer->SetPosition(wp);
		m_source->position_(rp);
		len = Next(t2);
	}
	m_tokenType = word;
	SetHereChar(t2, len);
	SetToken(m_buffer->contents());
}
#endif

void CScanner::xBinary()
{
	BYTE t1[3];
	int len;
	m_tokenType = binary;
	len = GetHereChar(t1);
	if ((((Next(), EmptyHere()) != 0) && (((GetHereType()) == xBinary1) && (IsHere('-')))))
	{
		BYTE t2[3];
		int len2 = step(t2);
		SetToken(t1, len, t2, len2);
	}
	else
	{
		SetToken(t1, len);
	}
}

void CScanner::xDefault()
{
	notify_("Unknown character");
}

void CScanner::xDelimiter()
{
	scanToken();
}

void CScanner::xDollar()
{
	BYTE t1[3];
	int len = Next(t1);
	SetToken(t1, len);
	Next();
	m_tokenType = character;
}

void CScanner::xDigit()
{
	xLetter();
/*
	m_tokenType = number;
	if (m_hereChar != 0)
	{
		m_source->skip_(-1);
	}
	SetToken(m_source->GetNumber());
	m_hereChar = m_source->next();
*/
}

void CScanner::SetBuffer(CWriteStream* p_buffer)
{
	if (m_buffer)
	{
		delete m_buffer;
	}
	m_buffer = p_buffer;
}

void CScanner::xLeftBracket()
{
	BYTE t1[3];
	int len;
	len = Next(t1);
	if (len == 0)
	{
		offEnd_("Unmatched string quote");
		return;
	}
	m_buffer->reset();
	while ((len != 1) || (t1[0] != ']'))
	{
		Put(t1, len);
		len = Next(t1);
		if (len == 0)
		{
			offEnd_("Unmatched string quote");
			return;
		}
	}
	Next();
	SetToken(m_buffer->contents());
	m_tokenType = String2;
}

void CScanner::xDoubleQuote()
{
	SetHereToken();
	Next();
/*
	char t1;
	t1 = m_source->next();
	if (t1 == 0)
	{
		offEnd_("Unmatched string quote");
		return;
	}
	m_buffer->reset();
	while (!(((t1 == '"') && ((t1 = m_source->next()) != '"'))))
	{
		m_buffer->nextPut_(t1);
		t1 = m_source->next();
		if (t1 == 0)
		{
			offEnd_("Unmatched string quote");
			return;
		}
	}
	m_hereChar = t1;
	SetToken(m_buffer->contents());
	m_tokenType = String2;*/
}

void CScanner::xSingleQuote()
{
	BYTE t1[3];
	int len;
	len = Next(t1);
	if (len == 0)
	{
		offEnd_("Unmatched string quote");
		return;
	}
	m_buffer->reset();
	while (!(((t1[0] == '\'') && ((len = Next(t1), t1[0]) != '\''))))
	{
		Put(t1, len);
		len = Next(t1);
		if (len == 0)
		{
			offEnd_("Unmatched string quote");
			return;
		}
	}
	SetHereChar(t1, len);
	SetToken(m_buffer->contents());
	m_tokenType = String;
}

void CScanner::error_(char* message)
{
	notify_(message);
}

void InitTypeTable(unsigned char* wordch)
{
	for (int i = 0x80; i < 0x100; i++)
	{
		typeTable[i] = wordch[i >> 3] & (1 << (i & 7)) ? xLetter1 : xDelimiter1;
	}
}
