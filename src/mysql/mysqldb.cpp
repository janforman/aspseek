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

/*  $Id: mysqldb.cpp,v 1.11 2003/01/23 17:55:45 kir Exp $
    Author : Alexander F. Avdonkin, Igor Sukhih
*/

#include "aspseek-cfg.h"
#include <mysqld_error.h>
#include <errmsg.h>
#include <unistd.h>
#include "mysqldb.h"
#include "mysqldbi.h"
#include "logger.h"

extern "C" {
CLogger * plogger = NULL;
}


char* memcpyq(char* dest, const char* src, int count)
{
	while (count-- > 0)
	{
		if (*src == '\'')
		{
			*dest++ = '\\';
		}
		else if (*src == '"')
		{
			*dest++ = '\\';
		}
		else if (*src == '\\')
		{
			*dest++ = '\\';
		}
		*dest++ = *src++;
	}
	return dest;
}

char* strcpyq(char* dest, const char* src)
{
	while (*src != 0)
	{
		if (*src == '\'')
		{
			*dest++ = '\\';
		}
		else if (*src == '"')
		{
			*dest++ = '\\';
		}
		else if (*src == '\\')
		{
			*dest++ = '\\';
		}
		*dest++ = *src++;
	}
	return dest;
}

char *CMySQLQuery::MakeQuery()
{
	char *sp, *pprev;
	ULONG i = 0, len = 0, buf_len;

	sp = pprev = m_query;

	buf_len = SQL_LEN;
	if (!m_param)
	{
		m_sqlquery = strdup(m_query);
		m_qlen = strlen(m_sqlquery);
		return m_sqlquery;
	}

	for (unsigned int i = 0; i < m_param->size(); i++)
		switch ((*m_param)[i].m_type)
		{
			case QTYPE_STR2:
				buf_len += (*m_param)[i].m_len << 2;
				break;
			case QTYPE_STRESC:
			case QTYPE_BLOB:
				buf_len += (*m_param)[i].m_len << 1;
				break;
			case QTYPE_STR:
				buf_len += (*m_param)[i].m_len;
			default:
				buf_len += 12;
		}
	m_sqlquery = new char[buf_len];
	while(*sp++)
	{
		if (*sp == '%' || *sp == ':')
		{
			char *sp_t = sp++;
			while (*sp && ((*sp == 's') || (*sp >=0x30 && *sp<=0x39)))
				sp++;
			if (sp_t == sp)
				continue;
			if (i > (m_param->size() - 1))
			{
//				plogger->log(CAT_ALL, L_ERR, "Error in sql number of param = %d, query=%s ", m_param->size(), m_query);
				return NULL;
			}
			memcpy(m_sqlquery + len, pprev, sp_t-pprev);
			len += sp_t-pprev;
			char *ep;
			if ((*m_param)[i].m_param)
			{
				switch ((*m_param)[i].m_type)
				{
				case QTYPE_STR2:
					*(m_sqlquery + len++) = '\'';
					if ((*m_param)[i].m_len)
					{
    						ep = memcpyq(m_sqlquery + len, (*m_param)[i].m_param, (*m_param)[i].m_len << 1);
						len += ep - (m_sqlquery + len);
					}
					*(m_sqlquery + len++) = '\'';
					break;
				case QTYPE_STRESC:
				case QTYPE_BLOB:
					*(m_sqlquery + len++) = '\'';
					if ((*m_param)[i].m_len)
					{
						ep = memcpyq(m_sqlquery + len, (*m_param)[i].m_param, (*m_param)[i].m_len);
						len += ep - (m_sqlquery + len);
					}
					*(m_sqlquery + len++) = '\'';
					break;
				case QTYPE_STR:
					if (*(sp_t + 1) != 's') *(m_sqlquery + len++) = '\'';
					if ((*m_param)[i].m_len)
					{
	    					memcpy(m_sqlquery + len, (*m_param)[i].m_param, (*m_param)[i].m_len);
						len += (*m_param)[i].m_len;
					}
					if (*(sp_t + 1) != 's') *(m_sqlquery + len++) = '\'';
					break;
				case QTYPE_INT:
					len += sprintf(m_sqlquery + len, "%d", *((int*)(*m_param)[i].m_param));
					break;
				case QTYPE_ULONG:
					len += sprintf(m_sqlquery + len, "%lu", *((ULONG*)(*m_param)[i].m_param));
					break;
				case QTYPE_DBL:
					len += sprintf(m_sqlquery + len, "%.3f", *((double*)(*m_param)[i].m_param));
					break;
				}
			}
			else
			{
				len += sprintf(m_sqlquery + len, "NULL");
			}
			pprev = sp;
			i++;
		}
	}
	strcpy(m_sqlquery + len, pprev);
	len += sp-pprev - 1;
	if (m_limit)
	{
		len += sprintf(m_sqlquery + len, " LIMIT %lu ", m_limit);
	}
	if (i != m_param->size())
	{
		plogger->log(CAT_ALL, L_ERR, "Error in sql number of param = %d, query=%s\n", m_param->size(), m_query);
	}
	m_sqlquery[len+1] = 0;
	m_qlen = len;
	return m_sqlquery;
}

/****************************************/
CSQLAnswer *CMySQLDatabase::sql_queryw(CSQLQuery *query)
{

	CMySQLAnswer *answ;
	if (safe_query(query->MakeQuery()))
	{
		if (mysql_errno(&m_my) != ER_DUP_ENTRY)
		{
			plogger->log(CAT_SQL, L_ERR, "Error: %s in <%s>\n", mysql_error(&m_my), query->m_query);
		}
		delete query;
		return NULL;
	}
	else
	{
//		plogger->log(CAT_SQL, L_DEBUG, "w->%s\n", query->m_sqlquery);
		MYSQL_RES *res;
		delete query;
		if ((res = mysql_store_result(&m_my)))
		{
			return (answ = new CMySQLAnswer(res));
		}
		else
		{
			return NULL;
		}
	}
}

CSQLAnswer *CMySQLDatabase::sql_real_querywr(CSQLQuery *query)
{

	CMySQLAnswer *answ;
	char* q = query->MakeQuery();
	if (safe_real_query(q, query->GetQueryLen()))
	{
		if (mysql_errno(&m_my) != ER_DUP_ENTRY)
		{
			plogger->log(CAT_SQL, L_ERR, "Error: %s in <%s>\n", mysql_error(&m_my), query->m_query);
		}
		delete query;
		return NULL;
	}
	else
	{
//		plogger->log(CAT_SQL, L_DEBUG, "w->%s\n", query->m_sqlquery);
		MYSQL_RES *res;
		delete query;
		if ((res = mysql_store_result(&m_my)))
		{
			return (answ = new CMySQLAnswer(res));
		}
		else
		{
			return NULL;
		}
	}
}

int CMySQLDatabase::safe_query(char* query)
{
//printf("-->%s\n", query);
	int res = mysql_query(&m_my, query);
	if (res)
	{
		if ((mysql_errno(&m_my) == CR_SERVER_LOST) ||
			(mysql_errno(&m_my) == CR_SERVER_GONE_ERROR ) ||
			(mysql_errno(&m_my) == ER_SERVER_SHUTDOWN))
		{
			usleep(5000000);
			return mysql_query(&m_my, query);
		}

	}
	return res;
}

ULONG CMySQLDatabase::sql_real_queryw1(CSQLQuery *query, int* err)
{
	char* q = query->MakeQuery();
	if (safe_real_query(q, query->GetQueryLen()))
	{
		int er = mysql_errno(&m_my);
		if (err)
		{
			*err = GetError(er);
		}
		if (er != ER_DUP_ENTRY)
		{
			plogger->log(CAT_SQL, L_ERR, "Error: %s <%s>\n", mysql_error(&m_my), query->m_query);
		}
		delete query;
		return 0;
	}
	else
	{
	//		plogger->log(CAT_SQL, L_DEBUG, "w1->%s\n", query->m_sqlquery);

		if (err) *err = 0;
		delete query;
		return mysql_insert_id(&m_my);
	}
}

ULONG CMySQLDatabase::sql_queryw1(CSQLQuery *query, int* err)
{
	int res = safe_query(query->MakeQuery());
	ULONG ret = mysql_insert_id(&m_my);
	delete query;
	if (res)
	{
		int er = mysql_errno(&m_my);
		if (er != ER_DUP_ENTRY)
		{
			plogger->log(CAT_SQL, L_ERR, "Error: %s <%s>\n", mysql_error(&m_my), query->m_query);
		}
		if (err)
		{
			*err = GetError(er);
		}
		return 0;
	}
	else
	{
	//	plogger->log(CAT_SQL, L_DEBUG, "w1->%s\n", query->m_sqlquery);
		if (err) *err = 0;
		return ret;
	}
}


int CMySQLDatabase::sql_real_queryw2(CSQLQuery *query, int* err)
{
	char* q = query->MakeQuery();
	int res = safe_real_query(q, query->GetQueryLen());
	//	plogger->log(CAT_SQL, L_DEBUG, "w2->%s\n", query->m_sqlquery);
	delete query;
	if (err)
	{
		if (res)
		{
			*err = GetError(mysql_errno(&m_my));
		}
		else
		{
			*err = 0;
		}
	}
	return res;
}

int CMySQLDatabase::sql_queryw2(CSQLQuery *query, int* err)
{
	int res = safe_query(query->MakeQuery());
	//	plogger->log(CAT_SQL, L_DEBUG, "w2->%s\n", query->m_sqlquery);
	delete query;
	if (err)
	{
		if (res)
		{
		*err = GetError(mysql_errno(&m_my));
		}
		else
		{
			*err = 0;
		}
	}
	return res;
}
int CMySQLDatabase::sql_real_queryw(CSQLQuery *query)
{
	char *q = query->MakeQuery();
	int res = safe_real_query(q, query->GetQueryLen());
//	plogger->log(CAT_SQL, L_DEBUG, "rw[%d]%s\n", query->GetQueryLen(), query->m_sqlquery);
	if (res)
	{
		if (mysql_errno(&m_my) != ER_DUP_ENTRY)
		{
			plogger->log(CAT_SQL, L_ERR, "Error: %s <%s>\n", mysql_error(&m_my), query->m_query);
		}
	}
	delete query;
	return res;
}

int CMySQLDatabase::safe_real_query(char* query, int len)
{
	int res = mysql_real_query(&m_my, query, len);
	if (res)
	{
		if ((mysql_errno(&m_my) == CR_SERVER_LOST) ||
			(mysql_errno(&m_my) == CR_SERVER_GONE_ERROR ) ||
			(mysql_errno(&m_my) == ER_SERVER_SHUTDOWN))
		{
			usleep(5000000);
			return mysql_real_query(&m_my, query, len);
		}
	}
	return res;
}

int CMySQLDatabase::Connect(const char* host, const char* user,
	const char* password, const char* dbname, int port)
{
	mysql_init(&m_my);

#if MYSQL_VERSION_ID >= 50003
        my_bool reconnect = 1;
	mysql_options(&m_my, MYSQL_OPT_RECONNECT, &reconnect);
#endif
	return mysql_real_connect(&m_my, host, user, password, dbname,
		port, NULL, 0) != NULL;
}

char* CMySQLDatabase::DispError(void)
{
	const char *str;
	int len;
	str = mysql_error(&m_my);
	len = strlen(str);
	strncpy(m_str_error, str, (len > 255) ? 255 : len);
	return m_str_error;
}

#ifdef UNICODE
const WORD* CMySQLDatabase::HiByteFirst(WORD* dst, const WORD* src)
{
	BYTE* d = (BYTE*)dst;
	while (*src)
	{
		*d++ = *src >> 8;
		*d++ = *src & 0xFF;
		src++;
	}
	*(WORD*)d = 0;
	return dst;
}

void CMySQLDatabase::HiByteFirst(BYTE* dst, WORD src)
{
	dst[0] = src >> 8;
	dst[1] = src & 0xFF;
}

#endif

void CMySQLDatabase::CloseDb()
{
	mysql_close(&m_my);
}

// Functions for use after dlopen()ing
PSQLDatabase newDataBase(void)
{
	return new CMySQLDatabase;
}

PSQLDatabaseI newDataBaseI(void)
{
	return new CMySQLDatabaseI;
}
