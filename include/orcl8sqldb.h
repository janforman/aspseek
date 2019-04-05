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

/*  $Id: orcl8sqldb.h,v 1.6 2002/05/14 11:47:16 kir Exp $
    Author : Igor Sukhih
*/

#ifndef _ORCLSQLDB_H_
#define _ORCLSQLDB_H_

#include <vector>
#include <string.h>
#include "defines.h"
#include "sqldbi.h"
#include "oci.h"

using std::vector;

#define DB_MAXCOLS	12
#define DB_MAXBLOB	6
#define DB_MAXOUT	6

extern CLogger * plogger;

struct COutData
{
	ULONG		m_out;
	OCILobLocator   *m_lobout;
	sb2	m_ind;
	ub2	m_rc;
};

class COrclSQLQuery :  public CSQLQuery
{
public:
	OCIBind		*m_bndin[DB_MAXCOLS];
	OCIBind		*m_bndout[DB_MAXOUT];
	COutData	m_lobout[DB_MAXBLOB];
	COutData	m_out;
public:
	COrclSQLQuery()
	{
		memset(&m_lobout, 0, sizeof(m_lobout));
		memset(&m_out, 0, sizeof(m_out));
		memset(m_bndout, 0, sizeof(m_bndout));
		memset(m_bndin, 0, sizeof(m_bndin));
		m_param = NULL;
	}
	COrclSQLQuery(const char *query, CSQLParam *param) : CSQLQuery(query)
	{
		memset(&m_lobout, 0, sizeof(m_lobout));
		memset(&m_out, 0, sizeof(m_out));
		memset(m_bndout, 0, sizeof(m_bndout));
		memset(m_bndin, 0, sizeof(m_bndin));
		m_param = param;
	}
	int GetOutvalue()
	{
		return m_out.m_out;
	}
	int BindParam(OCIStmt *stmthp, OCIError *m_errhp, OCIEnv *envhp);
	virtual ~COrclSQLQuery()
	{
		for (int i = 0; i< DB_MAXBLOB; i++)
		{
			if (m_lobout[i].m_lobout)
			{
				if (OCIDescriptorFree((dvoid *) m_lobout[i].m_lobout, (ub4) OCI_DTYPE_LOB))
				{
					plogger->log(CAT_ALL, L_ERR, "Error in OCIDescriptorFree\n");
				}
			}
		}
		Clear();
	}
	int WriteBlob(OCISvcCtx *svchp, OCIError *errhp);

	virtual char *MakeQuery();
	virtual CSQLQuery *SetQuery(const char *query, CSQLParam *param)
	{
		return new COrclSQLQuery(query, param);
	}
	virtual CSQLQuery *SetQuery(const char *query)
	{
		return new COrclSQLQuery(query, NULL);
	}
/************************************************/
	virtual CSQLQuery * GetWordIDI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO wordurl(word_id, word) VALUES (next_word_id.nextval, :1) \
RETURNING word_id INTO :out", param);
	}
	virtual CSQLQuery * GetUrlsI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO wordurl(word_id, word) VALUES (next_word_id.nextval, :1) \
RETURNING word_id INTO :out", param);
	}
	virtual CSQLQuery * StoreHref2I(CSQLParam *param)
	{
		return SetQuery("INSERT INTO sites(site_id, site) VALUES(next_site_id.nextval, :1) \
RETURNING site_id INTO :out", param);
	}
	virtual CSQLQuery * StoreHref2I1(CSQLParam *param)
	{
		return SetQuery("INSERT INTO urlword(url_id, site_id, hops, referrer, last_index_time, next_index_time, status, url) VALUES(next_url_id.nextval,:1,:2,:3,:4,:5,0,:6) \
RETURNING url_id INTO :out", param);
	}
	virtual CSQLQuery * StoreHref1I(CSQLParam *param)
	{
		return SetQuery("INSERT INTO sites(site_id, site) VALUES(next_site_id.nextval, :1) \
RETURNING site_id INTO :out", param);
	}
	virtual CSQLQuery * StoreHref1I1(CSQLParam *param)
	{
		return SetQuery("INSERT INTO urlword(url_id, site_id, hops, referrer, last_index_time, next_index_time, status, url) VALUES(next_url_id.nextval,:1,:2,:3,:4,:5,0,:6) \
RETURNING url_id INTO :out", param);
	}
	virtual CSQLQuery * StoreHrefI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO sites(site_id, site) VALUES(next_site_id.nextval,:1) \
RETURNING site_id INTO :out", param);
	}
	virtual CSQLQuery * StoreHrefI1(CSQLParam *param)
	{
		return SetQuery("INSERT INTO urlword(url_id, site_id, hops, referrer, last_index_time, next_index_time, status, url) VALUES(next_url_id.nextval,:1,:2,:3,:4,:5,0,:6) \
RETURNING url_id INTO :out", param);
	}
	virtual CSQLQuery * GetWordIDI2(CSQLParam *param)
	{
		return SetQuery("INSERT INTO wordurl1(word, word_id) VALUES (next_word_id.nextval, :2) \
RETURNING word_id INTO :out", param);
	}
	virtual CSQLQuery * CheckPatternS(CSQLParam *param)
	{
		return SetQuery( "SELECT count(*) FROM wordurl%s WHERE word LIKE :1 AND totalcount > 0", param);
	}
#ifdef UNICODE
	virtual CSQLQuery * SaveContentsI(CSQLParam *param)
	{
		return SetQuery( "INSERT INTO %s(url_id, wordcount, totalcount, docsize, lang, content_type, charset, words, hrefs, title, txt, keywords, description) VALUES(:1, :2, :3, :4, :5, :6, :7, empty_blob(), empty_blob(), empty_blob(), empty_blob(), empty_blob(), empty_blob()) \
RETURNING words, hrefs, title, txt, keywords, description INTO :bind1, :bind2, :bind3, :bind4, :bind5, :bind6", param);
	}
#else
	virtual CSQLQuery * SaveContentsI(CSQLParam *param)
	{
		return SetQuery( "INSERT INTO %s(url_id, wordcount, totalcount, docsize, lang, content_type, charset, words, hrefs, title, txt, keywords, description) VALUES(:1, :2, :3, :4, :5, :6, :7, empty_blob(), empty_blob(), :10, :11, :12, :13) \
RETURNING words, hrefs INTO :bind1, :bind2", param);
	}
#endif
	virtual CSQLQuery * SaveWord1U(CSQLParam *param)
	{
	return SetQuery( "UPDATE wordurl1 SET urlcount = :1, totalcount = :2  urls = empty_blob() WHERE word_id =:4 \
RETURNING urls INTO :bind1", param);
	}
	virtual CSQLQuery * InsertWordSiteI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO wordsite(word, sites) VALUES(:1, empty_blob()) \
RETURNING sites INTO :bind1", param);
	}
	virtual CSQLQuery * SaveLinkI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO citation(url_id, referrers) VALUES(:1, empty_blob()) \
RETURNING referrers INTO :bind1", param);
	}
	virtual CSQLQuery * SaveDeltaU()
	{
		return SetQuery( "UPDATE wordurl1 SET urls=empty_blob(), totalcount=0, urlcount=0 where urls is not null");
	}
	virtual CSQLQuery * EmptyDeltaU()
	{
		return SetQuery( "UPDATE wordurl1 SET urls=empty_blob(), totalcount=0, urlcount=0 where urls is not null");
	}
	virtual CSQLQuery * SaveWordU(CSQLParam *param)
	{
		return SetQuery( "UPDATE wordurl SET urlcount = :1, totalcount = :2, urls = empty_blob() WHERE word_id = :3", param);
	}
	virtual CSQLQuery * SaveWordU1(CSQLParam *param)
	{
		return SetQuery( "UPDATE wordurl SET urlcount = :1, totalcount = :2, urls = empty_blob() WHERE word_id = :3 \
RETURNING urls INTO :bind1", param);
	}
	virtual CSQLQuery * LoadRanksS2(CSQLParam *param)
	{
		return SetQuery("SELECT url_id, hrefs FROM urlwords%s WHERE hrefs is not null", param);
	}
	virtual CSQLQuery * SaveDeltaS()
	{
		return SetQuery("SELECT word_id,urls, mod(word_id,100) FROM wordurl1 where urls is not null ORDER BY 3");
	}
	virtual CSQLQuery * ClearD1()
	{
		return SetQuery( "TRUNCATE TABLE wordurl");
	}
	virtual CSQLQuery * ClearD2()
	{
		return SetQuery( "TRUNCATE TABLE wordurl1");
	}
	virtual CSQLQuery * ClearD3()
	{
		return SetQuery( "TRUNCATE TABLE robots");
	}
	virtual CSQLQuery * ClearD4()
	{
		return SetQuery( "TRUNCATE TABLE sites");
	}
	virtual CSQLQuery * ClearD5()
	{
		return SetQuery( "TRUNCATE TABLE urlword");
	}
	virtual CSQLQuery * ClearD6()
	{
		return SetQuery( "TRUNCATE TABLE wordsite");
	}
	virtual CSQLQuery * ClearD7()
	{
		return SetQuery( "TRUNCATE TABLE citation");
	}
	virtual CSQLQuery * ClearD8(CSQLParam *param)
	{
		return SetQuery( "TRUNCATE TABLE urlwords%s", param);
	}
	virtual CSQLQuery * ClearD9()
	{
		return SetQuery( "TRUNCATE TABLE urlwords");
	}
	virtual CSQLQuery * GetStatisticsS(CSQLParam *param)
	{
		return SetQuery("SELECT status, SUM(DECODE(SIGN(:1-next_index_time),-1,0,1,1)), count(*) FROM urlword WHERE deleted=0 %s GROUP BY status", param);
	}
	virtual CSQLQuery * AddStatI(CSQLParam *param)
	{
		return SetQuery( "INSERT INTO stat(addr,proxy,query,ul,np,ps,urls,sites,\"start\",finish,site,sp,referer) VALUES (:1, :2, :3, :4, :5, :6, :7, :8, :9, :10, :11, :12, :13)", param);
	}
	virtual CSQLQuery * AddToCacheI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO rescache(qkey, urls) VALUES(:1, empty_blob()) \
RETURNING urls INTO :bind1", param);
	}
	virtual CSQLQuery * GetWordIDL()
	{
		return NULL;
	}
	virtual CSQLQuery * UnlockTables()
	{
		return NULL;
	}
	virtual CSQLQuery * SaveDeltaL()
	{
		return NULL;
	}
	virtual CSQLQuery * DeleteUrlFromSpaceL()
	{
		return NULL;
	}
	virtual CSQLQuery * AddUrlToSpaceL()
	{
		return NULL;
	}
};
#define BUF_OUT_SIZE	256
struct OrclRes {
	int     m_ncols;
	int	m_currow;
	int	m_limit;
	char    *m_cdefbuff[DB_MAXCOLS];
	int	m_idefbuff[DB_MAXCOLS];
	OCILobLocator	*m_lob[DB_MAXCOLS];
	char	*m_lobbuf[DB_MAXCOLS];
	int 	m_out;
	int     m_colsize[DB_MAXCOLS];
	sb2     m_indbuff[DB_MAXCOLS];  /* Indicators for NULLs */
};

class COrclSQLAnswer : public CSQLAnswer
{
public:
	OrclRes		*m_res;
	OCIStmt    	*m_stmthp;
	OCIError	*m_errhp;
	OCISvcCtx	*m_svchp;
public:
	COrclSQLAnswer()
	{
		m_res = NULL;
	}
	COrclSQLAnswer(OCISvcCtx *svchp, OCIStmt *stmthp, OCIError *errhp, OrclRes *res)
	{
		m_svchp = svchp;
		m_res = res;
		m_stmthp = stmthp;
		m_errhp = errhp;
	}

	virtual ~COrclSQLAnswer()
	{
		FreeResult();
	}
	virtual void FreeResult(void)
	{
		if (m_res)
		{
			for(int i = 0; i < m_res->m_ncols; i++)
			{
				if (m_res->m_cdefbuff[i])
				{
					delete m_res->m_cdefbuff[i];
				}
				else if (m_res->m_lobbuf[i])
				{
					delete m_res->m_lobbuf[i];
				}
				if (m_res->m_lob[i])
				{
					if (OCIDescriptorFree((dvoid *) m_res->m_lob[i], (ub4) OCI_DTYPE_LOB))
					{
						plogger->log(CAT_ALL, L_ERR, "Error in OCIDescriptorFree\n");
					}
				}
			}
		}
		delete m_res;
		if (m_stmthp)
		{
			OCIHandleFree(m_stmthp, OCI_HTYPE_STMT);
		}
		m_stmthp = NULL;
	}
	void ClearBlob()
	{
		for(int i = 0; i < m_res->m_ncols; i++)
		{
			if (m_res->m_lobbuf[i])
			{
				delete m_res->m_lobbuf[i];
				m_res->m_lobbuf[i] = NULL;
			}
		}
	}
	virtual int FetchRow()
	{
		sword status;
		ClearBlob();
		if (m_res->m_limit && (m_res->m_currow >= m_res->m_limit))
		{
			return 0;
		}
		if ((status = OCIStmtFetch(m_stmthp, m_errhp, 1, OCI_FETCH_NEXT, OCI_DEFAULT)) && status != OCI_SUCCESS_WITH_INFO)
		{
			if (status != OCI_NO_DATA)
			{
				sb4 errcode;
				char str[255];
				OCIErrorGet(m_errhp, 1, NULL,  &errcode,  (text *)str, 255, OCI_HTYPE_ERROR);
				plogger->log(CAT_ALL, L_ERR, "Error OCIStmtFetch %s\n", str);
			}
			return 0;
		}
		m_res->m_currow++;
		return 1;
	}

	char *ReadBlob(OCILobLocator *lob);
	virtual char *GetColumn(int clm)
	{
		if (m_res->m_cdefbuff[clm])
		{
			if (!m_res->m_indbuff[clm])
			{
				return m_res->m_cdefbuff[clm];
			}
			else if (m_res->m_indbuff[clm] == -1)
			{
				return "";
			}
			return NULL;
		}
		else if (m_res->m_lob[clm])
		{
			if (m_res->m_lobbuf[clm])
			{
				return m_res->m_lobbuf[clm];
			}

			if ((m_res->m_lobbuf[clm] = ReadBlob(m_res->m_lob[clm])))
			{
				return m_res->m_lobbuf[clm];
			}
			else
			{
				return "";
			}
		}
		return NULL;
	}
	virtual int GetColumn(int clm, ULONG& val)
	{
		if (!m_res->m_indbuff[clm])
		{
			val =  m_res->m_idefbuff[clm];
			return 1;
		}
		return 0;
	}
	virtual int GetColumn(int clm, int& val)
	{
		if (!m_res->m_indbuff[clm])
		{
			val = m_res->m_idefbuff[clm];
			return 1;
		}
		return 0;
	}
	virtual ULONG GetLength(int clm)
	{
		if (m_res->m_lob[clm])
		{
			ub4 loblen = 0;
			if (OCILobGetLength(m_svchp, m_errhp, m_res->m_lob[clm], &loblen))
			{
				plogger->log(CAT_ALL, L_ERR, "Error in OCILobGetLength\n");
			}
			return loblen;
		}
		else
		{
			return m_res->m_colsize[clm];
		}
	}
};

class COrclSQLDatabase : public virtual CSQLDatabase
{
public:
	OCIEnv		*m_envhp;
	OCIError	*m_errhp;
	OCISvcCtx	*m_svchp;
public:
	COrclSQLDatabase()
	{
		if (OCIInitialize(OCI_THREADED | OCI_OBJECT, NULL, NULL, NULL, NULL))
		{
			plogger->log(CAT_ALL, L_ERR,"FAILED: OCIInitialize()\n");
		}
		if (OCIEnvInit((OCIEnv **) &m_envhp, (ub4) OCI_DEFAULT, (size_t) 0, NULL ))
		{
			plogger->log(CAT_ALL, L_ERR,"FAILED: OCIEnvInit()\n");
		}
		if (OCIHandleAlloc(m_envhp, (dvoid **) &m_errhp, OCI_HTYPE_ERROR, 0, NULL ))
		{
			plogger->log(CAT_ALL, L_ERR,"FAILED: OCIHandleAlloc() on  errhp\n");
		}
		m_svchp = NULL;
		m_sqlquery = new COrclSQLQuery;
		m_maxblob = 4000;
	}
	virtual ~COrclSQLDatabase()
	{
		delete m_sqlquery;
		CloseDb();
	}
	virtual CSQLAnswer *sql_queryw(CSQLQuery *query);
	virtual CSQLAnswer *sql_real_querywr(CSQLQuery *query){return sql_queryw(query); };
	virtual ULONG sql_real_queryw1(CSQLQuery *query, int* err = NULL){return sql_queryw1(query, err);};
	virtual int sql_real_queryw2(CSQLQuery *query, int* err = NULL){return sql_queryw2(query, err);};
	virtual ULONG sql_queryw1(CSQLQuery *query, int* err = NULL);
	virtual int sql_queryw2(CSQLQuery *query, int* err = NULL);
	virtual int sql_real_queryw(CSQLQuery *query){return sql_queryw1(query);};
	OrclRes * GetRes()
	{
		OrclRes *res = new OrclRes;
		for(int i = 0; i < DB_MAXCOLS; i++)
		{
			res->m_cdefbuff[i] = NULL;
			res->m_lob[i] = NULL;
			res->m_lobbuf[i] = NULL;
			res->m_colsize[i] = 0;
			res->m_indbuff[i] = 0;
			res->m_idefbuff[i] = 0;

		}
		res->m_ncols = 0;
		res->m_currow = 0;
		res->m_limit = 0;

		return res;
	}

	virtual int GetError(int err)
	{
		switch(err)
		{
		case 1:
			return DB_ER_DUP_ENTRY;
		default:
			return DB_ER_UNKNOWN;
		}
	}
	virtual int Connect(const char* host, const char* user, const char* password, const char* dbname, int port=0);
	virtual void CloseDb();
	virtual char* DispError();
};
#endif /* _ORCLSQLDB_H_ */
