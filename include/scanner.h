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

/*  $Id: scanner.h,v 1.12 2002/06/18 11:31:18 kir Exp $
	Author: Alexander F. Avdonkin
*/

#ifndef _SCANNER_H_
#define _SCANNER_H_

#include "streams.h"
#ifdef UNICODE
#include "ucharset.h"
#endif

//typedef std::vector<char*> StringArray;
typedef int TypeTable[0x100];

#define xBinary1 0
#define xLetter1 1
#define xDigit1 2
#define xDelimiter1 3
#define xDefault1 4
#define xDoubleQuote1 5
#define xDollar1 6
#define xSingleQuote1 7
#define xLeftBracket1 8
#define xIdeograph1 9

#define maxExtended 9

#define compoundWord 10
#define compoundKeyword 1
#define compoundBinary 12
#define number 13
#define doIt 14
#define binary 15
#define period 16
#define leftParenthesis 17
#define colon 18
#define rightParenthesis 19
#define Minus 20
#define verticalBar 21
#define semicolon 22
#define upArrow 23
#define BitOr 24
#define Assoc 25
#define GT 26
#define LT 27
#define rightBrace 28
#define syntax 29
#define DoIt 30
#define DoItIn 31
#define literalQuote 32
#define leftBrace 34
#define Class 35
#define leftArrow 36
#define newSelector 37
#define word 38
#define keyword 39
#define literal 40
#define character 41
#define ascii41 42
#define ascii52 43
#define ascii54 44
#define ascii100 45
#define String2 46
#define rightBracket 47
#define String 48

extern char* Empty;

class CScanner
{
public:
	CReadStream* m_source;
	int m_mark;
	int m_prevEnd;
#ifdef UNICODE
	BYTE m_hereChar[3];
	BYTE m_len;
#else
	unsigned char m_hereChar;
#endif
	char* m_token;
	int m_tokenType;
	BOOL m_saveComments;
	CWriteStream* m_buffer;
	int m_typeTable[0x100];
	int m_delim;
#ifdef UNICODE
	CCharsetB* m_ucharset;
#endif

public:
#ifdef UNICODE
	void xIdeograph();
	void PutHereChar()
	{
		for (int i = 0; i < m_len; i++)
		{
			m_buffer->nextPut_(m_hereChar[i]);
		}
	}
	int Next(BYTE* buf)
	{
		return m_source->next(buf, m_ucharset);
	}
	void Put(BYTE* buf, int len)
	{
		for (int i = 0; i < len; i++)
		{
			m_buffer->nextPut_(buf[i]);
		}
	}
	void SetHereChar(BYTE* buf, int len)
	{
		memcpy(m_hereChar, buf, len);
		m_len = len;
	}
	int GetHereChar(BYTE* buf)
	{
		memcpy(buf, m_hereChar, m_len);
		return m_len;
	}
	void Next()
	{
		m_len = Next(m_hereChar);
	}
	int EmptyHere()
	{
		return m_len == 0;
	}
	int GetHereType()
	{
		return GetType(m_hereChar, m_len);
	}
	int IsHere(BYTE c)
	{
		return (m_len == 1) && (m_hereChar[0] == c);
	}
	void SetHereToken()
	{
		SetToken(m_hereChar, m_len);
	}
#else
	void PutHereChar()
	{
		m_buffer->nextPut_(m_hereChar);
	}
	int Next(BYTE* buf)
	{
		*buf = m_source->next();
		return *buf ? 1 : 0;
	}
	void Put(BYTE* buf, int len)
	{
		m_buffer->nextPut_(*buf);
	}
	void SetHereChar(BYTE* buf, int len)
	{
		m_hereChar = *buf;
	}
	int GetHereChar(BYTE* buf)
	{
		*buf = m_hereChar;
		return 1;
	}
	void Next()
	{
		Next(&m_hereChar);
	}
	int EmptyHere()
	{
		return m_hereChar == 0;
	}
	int GetHereType()
	{
		return GetType(&m_hereChar, 1);
	}
	int IsHere(BYTE c)
	{
		return m_hereChar == c;
	}
	void SetHereToken()
	{
		SetToken(&m_hereChar, 1);
	}
#endif
	virtual void xLetter();
	void xLetter0() {xLetter();};
	virtual void initScanner();
	void xDefault();
//	CArray* scanPositionsFor_inString_(void t1, void t2);
	virtual BOOL notify_(char* t1);
	CScanner* scan_(CReadStream* t1);
//	CScanner* scanLitVec();
//	CArray* scanFieldNames_(void t1);
	char* scanToken();
	void scanToken(const char* letters);
	int step(BYTE* buf);
	void stepChar();
	BOOL offEnd_(char* t1);
	CScanner* scanLitKeywords();
//	char* scanTokens_(char* t1);
	CScanner* on_(CReadStream* t1);
	int mapCompoundTypes_(int t1);
	void xBinary();
//	COrderedCollection* breakIntoTokens_(void t1);
	void xDelimiter();
	void xDoubleQuote();
//	CScanner* xLitQuote();
	void xDollar();
	void xLeftBracket();
	void xSingleQuote();
	CScanner* scanLitToken();
	void xDigit();
	void SetBuffer(CWriteStream* p_buffer);

	void error_(char*);
	void perform_(int p_tokenType);
	BOOL IsExtendedType() {return m_tokenType <= maxExtended;}

public:
	CScanner()
	{
		m_buffer = NULL;
		m_token = NULL;
#ifdef UNICODE
		m_ucharset = &baseCharset;
#endif
	}

	virtual ~CScanner()
	{
		SetToken((char*)NULL);
		delete m_buffer;
	}

	void SetToken(char* p_token)
	{
		if (m_token && (m_token != Empty))
		{
			delete m_token;
		}
		m_token = p_token;
	}

	void SetToken(BYTE* p_token, int len)
	{
		if (m_token)
		{
			delete m_token;
		}
		m_token = new char[len + 1];
		memcpy(m_token, p_token, len);
		m_token[len] = 0;
	}

	void SetToken(BYTE* p_token1, int len1, BYTE* p_token2, int len2)
	{
		if (m_token)
		{
			delete m_token;
		}
		m_token = new char[len1 + len2 + 1];
		memcpy(m_token, p_token1, len1);
		memcpy(m_token + len1, p_token2, len2);
		m_token[len1 + len2] = 0;
	}

	void InsertPrefix(char* p_prefix)
	{
		int tl, pl;
		char* token = m_token;
		m_token = new char[(tl = strlen(token)) + (pl = strlen(p_prefix)) + 1];
		memcpy(m_token, p_prefix, pl);
		memcpy(m_token + pl, token, tl + 1);
		delete token;
	}
	int GetType(unsigned char* c, int len)
	{
#ifdef UNICODE
		if (*c < 0x80)
		{
			return m_typeTable[*c];
		}
//		return m_ucharset->UCodeLower(*c) ? xLetter1 : xDelimiter1;
		switch (m_ucharset->SymbolType(c, len))
		{
		case 1:
			return xLetter1;
		case 2:
			return xIdeograph1;
		default:
			return xDelimiter1;
		}
#else
		return m_typeTable[*c];
#endif
	}
};

typedef void (CScanner::*ScannerVoidFunc)();

void InitTypeTable(unsigned char* wordch);

#endif /* _SCANNER_H_ */
