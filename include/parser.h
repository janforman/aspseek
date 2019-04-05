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

/*  $Id: parser.h,v 1.10 2002/06/15 12:55:03 al Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _PARSER_H_
#define _PARSER_H_

#include <exception>
#include "maps.h"
#include "stack.h"
#include "scanner.h"

using std::exception;

class CParserException : public exception
{
public:
	HRESULT m_code;
	char* m_message;

public:
	CParserException(HRESULT p_code, char* p_message)
	{
		m_code = p_code;
		m_message = p_message;
	}
};

typedef std::vector<int> BackupStack;

class CGeneralParser : public CScanner
{
public:
	Stack m_stack;
	BackupStack m_backupStack;
	MapStringToInt m_reservedWords;
	int m_keywordMarker;

public:
	BOOL signedNumber();
	void replaceStackTop_(CStackElem* t1);
	virtual BOOL notify_(char* t1);
	virtual BOOL offEnd_(char* t1);
	BOOL popStackTo_(int t1);
	BOOL backupRestore();
	void unexpectedError() {};
	BOOL peekForType_value_(int t1, char* t2);
	void method();
	int sourcePosition();
	BOOL peekForTypeNoPush_(int t1, const char* letters);
	BOOL peekForTypeNoPush_(int t1);
	BOOL backupDiscard();
	void sourcePosition_(int t1);
	virtual void xLetter();
	void combineStackTo_(int t1);
	void backupSave();
	virtual void initScanner();
	BOOL peekForType_(int t1);
	BOOL expected_(char* t1) {return notify_(t1);};
	BOOL keywords();
	virtual void initReserved() {};

public:
	void error(char* p_message);
	void push(CStackElem* elem);
	int Delimited()
	{
		return m_delim;
	}
};
#endif /* _PARSER_H_ */

