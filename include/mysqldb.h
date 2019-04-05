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

/*  $Id: mysqldb.h,v 1.7 2002/05/14 11:47:16 kir Exp $
    Author : Alexander F. Avdonkin, Igor Sukhih
*/

#ifndef _MYSQLDB_H_
#define _MYSQLDB_H_

#include <vector>
#include <mysql.h>
#include <string.h>
#include <mysqld_error.h>
#include "defines.h"
#include "sqldb.h"

using std::vector;

class CMySQLQuery : public CSQLQuery
{
public:
	CMySQLQuery(){}
	CMySQLQuery(const char *query, CSQLParam *param) : CSQLQuery(query)
	{
		m_param = param;
	}
	virtual ~CMySQLQuery()
	{
		Clear();
	}
	virtual char *MakeQuery();
	virtual CSQLQuery *SetQuery(const char *query, CSQLParam *param)
	{
		return new CMySQLQuery(query, param);
	}
};

class CMySQLAnswer : public CSQLAnswer
{
public:
	MYSQL_RES *m_res;
	MYSQL_ROW m_row;
	unsigned long int *m_lengths;
public:
	CMySQLAnswer()
	{
		m_res = NULL;
		m_lengths = NULL;
	}
	CMySQLAnswer(MYSQL_RES *res)
	{
		m_lengths = NULL;
		m_res = res;
	}
	virtual ~CMySQLAnswer()
	{
		FreeResult();
	}
	virtual void FreeResult(void)
	{
		if (m_res)
		{
			mysql_free_result(m_res);
		}
		m_res = 0;
		m_lengths = NULL;
	}
	virtual int FetchRow()
	{
		if (m_res && (m_row = mysql_fetch_row(m_res)))
		{
			m_lengths = NULL;
			return 1;
		}
		else
		{
			return 0;
		}
	}

	virtual char *GetColumn(int clm)
	{
		return m_row[clm];
	}
	virtual int GetColumn(int clm, ULONG& val)
	{
		if (m_row[clm])
		{
			val = strtoul(m_row[clm], NULL, 10);
			return 1;
		}
		else
		{
			val = 0;
			return 0;
		}
	}
	virtual int GetColumn(int clm, int& val)
	{
		if (m_row[clm])
		{
			val = atoi(m_row[clm]);
			return 1;
		}
		else
		{
			val = 0;
			return 0;
		}
	}
	virtual ULONG GetLength(int clm)
	{
		if (!m_lengths && m_res)
		{
			m_lengths = mysql_fetch_lengths(m_res);
		}
		return m_lengths[clm];
	}
/*	virtual ULONG GetNumRows()
	{
		return mysql_num_rows(m_res);
	}
	virtual void SeekRow(ULONG row)
	{
		mysql_data_seek(m_res, row);
	}
*/
};

class CMySQLDatabase : public virtual CSQLDatabase
{
public:
	MYSQL m_my;
public:
	CMySQLDatabase()
	{
		m_sqlquery = new CMySQLQuery;
	}
	virtual ~CMySQLDatabase()
	{
		delete m_sqlquery;
		CloseDb();
	}
#ifdef UNICODE
	virtual const WORD* HiByteFirst(WORD* dst, const WORD* src);
	virtual void HiByteFirst(BYTE* dst, WORD src);
#endif
	virtual CSQLAnswer *sql_queryw(CSQLQuery *query);
	virtual CSQLAnswer *sql_real_querywr(CSQLQuery *query);
	virtual ULONG sql_real_queryw1(CSQLQuery *query, int* err = NULL);
	virtual int sql_real_queryw2(CSQLQuery *query, int* err = NULL);
	virtual ULONG sql_queryw1(CSQLQuery *query, int* err = NULL);
	virtual int sql_queryw2(CSQLQuery *query, int* err = NULL);
	virtual int sql_real_queryw(CSQLQuery *query);
	inline int safe_query(char* query);
	inline int safe_real_query(char* query, int len);

	virtual int GetError(int err)
	{
		switch(err)
		{
		case ER_DUP_ENTRY:
			return DB_ER_DUP_ENTRY;
		default:
			return DB_ER_UNKNOWN;
		}
	}
	virtual int Connect(const char* host, const char* user, const char* password, const char* dbname, int port=0);
	virtual void CloseDb();
	virtual char* DispError();
};
#endif

