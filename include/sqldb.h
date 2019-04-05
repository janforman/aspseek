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

/*  $Id: sqldb.h,v 1.21 2002/12/18 18:38:53 kir Exp $
    Author : Igor Sukhih
*/

#ifndef _SQLDB_H_
#define _SQLDB_H_

#include "aspseek-cfg.h"
#include <pthread.h>
#include <vector>
#include <string.h>
#include <stdio.h>
#include "my_hash_map"
#include "my_hash_set"
#include "documents.h"
#include "logger.h"
#ifdef UNICODE
#include "ucharset.h"
#endif
#include "defines.h"


/* Class hierarchy:
 *
 *	CSQLQuery
 *	+---CMySQLQuery
 *	+---COrclSQLQuery
 *
 *	CSQLAnswer
 *	+---CMySQLAnswer
 *	+---COrclSQLAnswer
 *
 *	CSQLDatabase
 *	+---CMySQLDatabase
 *	+---COrclSQLDatabase
 *	|
 *	+---CSQLDatabaseI
 *	    +---CMySQLDatabaseI
 *	    +---COrclSQLDatabaseI
 */

class CWord;
class CSrcDocument;
class CULONG;
class CULONG100;
class CDeltaFiles;

class CUrlParam;

typedef hash_set<int> CIntSet;



extern std::string DBHost, DBName, DBUser, DBPass, DBType;
extern std::string DBWordDir;
extern std::string Limit;
extern int DBPort;
extern ULONG DeltaBufferSize;
extern ULONG UrlBufferSize;
extern ULONG NextDocLimit;
extern int npages, curUrl;
BYTE* Uncompress(BYTE* cont, ULONG len, unsigned long int & uLen);
void AddUrlLimit(char* url);
void AddStatusLimit(char* url);
void AddTagLimit(char* url);
int ClearURLLimit();
void CalcWordDir();
int ParseDBAddr(const char *addr);
char* UWTable(char* buf, ULONG urlID);
//time_t now();

#define QTYPE_STRESC	0
#define QTYPE_STR	1
#define	QTYPE_INT	2
#define	QTYPE_ULONG	3
#define	QTYPE_BLOB	4
#define	QTYPE_STR2	5
#define QTYPE_DBL	6

struct CQueryParam
{
	int m_type;
	int m_len;
	const char *m_param;
};

class CSQLParam : public std::vector <CQueryParam>
{
public:
	void _addparam(const char *param, int len, int type)
	{
		CQueryParam p;
		p.m_len = len;
		p.m_type = type;
//		if (len)
//		{
			p.m_param = param;
//		}
//		else
//		{
//			p.m_param = NULL;
//		}
		push_back(p);
	}
	void AddParamEsc(const char *param)
	{
		_addparam(param, strlen(param), QTYPE_STRESC);
	}
	void AddParam(const char *param)
	{
		_addparam(param, strlen(param), QTYPE_STR);
	}
	void AddParam(const int *param)
	{
		_addparam((const char*)param, sizeof(int), QTYPE_INT );
	}
	void AddParam(const ULONG *param)
	{
		_addparam((const char*)param, sizeof(ULONG), QTYPE_ULONG);
	}
#ifdef UNICODE
	void AddParam(const WORD *param)
	{
		_addparam((const char*)param, strwlen(param), QTYPE_STR2);
	}
	void AddParam(const WORD *param, int len)
	{
		_addparam((const char*)param, len, QTYPE_STR2);
	}
#else
	void AddParam(const char *param, int len)
	{
		_addparam((const char*)param, len, QTYPE_STR);
	}
#endif
	void AddParamEsc(const char *param, int len)
	{
		_addparam(param, len, QTYPE_BLOB);
	}
	void AddParam(const double *param)
	{
		_addparam((const char*)param, sizeof(double), QTYPE_DBL);
	}
	void Clear()
	{
		clear();
	}
};

#define	SQL_LEN	512
class CSQLQuery
{
public:
	char m_query[SQL_LEN];
	char *m_sqlquery;
	char *m_lastpos;
	int m_qlen;
	ULONG m_limit;
	CSQLParam *m_param;
public:
	CSQLQuery()
	{
		m_query[0] = 0;
		m_lastpos = NULL;
		m_sqlquery = NULL;
		m_qlen = 0;
		m_limit = 0;
		m_param = NULL;
	}
	CSQLQuery(const char *query)
	{
		strcpy(m_query, query);
		m_lastpos = NULL;
		m_sqlquery = NULL;
		m_qlen = 0;
		m_limit = 0;
		m_param = NULL;
	}
	CSQLQuery(const char *query, CSQLParam *param)
	{
		strcpy(m_query, query);
		m_lastpos = NULL;
		m_sqlquery = NULL;
		m_qlen = 0;
		m_limit = 0;
		m_param = param;
	}
	void Clear()
	{
		m_query[0] = 0;
		if (m_sqlquery)
			delete m_sqlquery;
		m_sqlquery = NULL;
		m_qlen = 0;
		m_limit = 0;
	}
	void AddLimit(ULONG lmt)
	{
		m_limit = lmt;
	}
	virtual ~CSQLQuery()
	{
		Clear();
	}
	virtual char *MakeQuery() = 0;
	char *MakeQuery(CSQLParam *param)
	{
		m_param = param;
		return MakeQuery();
	}
	virtual CSQLQuery *SetQuery(const char *query, CSQLParam *param = NULL)
	{
		return NULL;
	}
	void AddQuery(const char*query)
	{
		strcat(m_query, query);
	}
	char *GetQuery(void)
	{
		return m_sqlquery;
	}
	int GetQueryLen()
	{
		return m_qlen;
	}
/************** Query List ***************************/
	virtual char *LimitAnd()
	{
		return " AND (urlword.%s LIKE '%s' ";
	}
	virtual char *LimitOr()
	{
		return " OR urlword.%s LIKE '%s' ";
	}
	virtual CSQLQuery *GetWordIDS(CSQLParam *param)
	{
		return SetQuery("SELECT word_id FROM wordurl WHERE word = :1", param);
	}
	virtual CSQLQuery * GetWordIDL()
	{
		return SetQuery("lock tables wordurl1 write,wordurl write");
	}
	virtual CSQLQuery * GetWordIDI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO wordurl(word) VALUES (:1)", param);
	}
	virtual CSQLQuery * GetWordIDI2(CSQLParam *param)
	{
		return SetQuery("INSERT INTO wordurl1(word, word_id) VALUES (:1, :2)", param);
	}
	virtual CSQLQuery * UnlockTables()
	{
		return SetQuery("unlock tables");
	}
	virtual CSQLQuery * GetContentsS(CSQLParam *param)
	{
		return SetQuery("SELECT words, hrefs, wordCount, content_type, charset FROM %s WHERE url_id = :1", param);
	}
	virtual CSQLQuery * GetSitesS(CSQLParam *param)
	{
		return SetQuery("SELECT sites FROM wordsite WHERE word=:1", param);
	}
	virtual CSQLQuery * GetUrlIDS(CSQLParam *param)
	{
		return SetQuery("SELECT url_id, redir FROM urlword WHERE url=:1", param);
	}
	virtual CSQLQuery * GetUrlIDS1(CSQLParam *param)
	{
		return SetQuery("SELECT url_id, redir FROM urlword WHERE url_id=:1", param);
	}
	virtual CSQLQuery * GetUrlIDS2(CSQLParam *param)
	{
		return SetQuery("SELECT url_id, deleted FROM urlword WHERE url=:1", param);
	}
	virtual CSQLQuery * GetSitesForLinkS(CSQLParam *param)
	{
		return SetQuery("SELECT referrers FROM citation WHERE url_id = :1", param);
	}
	virtual CSQLQuery * GetSitesS1(CSQLParam *param)
	{
		return SetQuery("SELECT urls,word_id,totalcount,urlcount FROM wordurl%s WHERE word_id = :1", param);
	}
	virtual CSQLQuery * GetUrlsS(CSQLParam *param)
	{
		return SetQuery("SELECT urls,word_id,totalcount FROM wordurl WHERE word = :1", param);
	}
	virtual CSQLQuery * GetUrlsI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO wordurl(word) VALUES (:1)", param);
	}
	virtual CSQLQuery * GetUrlsS1(CSQLParam *param)
	{
		return SetQuery("SELECT urls,word_id,totalcount,urlcount FROM wordurl WHERE word_id = :1", param);
	}
	virtual CSQLQuery * CheckPatternS(CSQLParam *param)
	{
		return SetQuery("SELECT count(*) FROM wordurl%s WHERE word LIKE :1 AND totalcount > 0", param);
	}
	virtual CSQLQuery * GetPatternIDs(CSQLParam *param)
	{
#ifdef UNICODE
		return SetQuery( "SELECT word_id,word FROM wordurl WHERE word LIKE :1", param);
#else
		return SetQuery( "SELECT word_id FROM wordurl WHERE word LIKE :1", param);
#endif
	}
	virtual CSQLQuery * GetUrls1S(CSQLParam *param)
	{
		return SetQuery("SELECT urls,word_id,totalcount,urlcount FROM wordurl1 WHERE word_id = :1", param);
	}
	virtual CSQLQuery * SaveWordU(CSQLParam *param)
	{
		return SetQuery("UPDATE wordurl SET urlcount = :1, totalcount = :2, urls = '' WHERE word_id = :3", param);
	}
	virtual CSQLQuery * SaveWordU1(CSQLParam *param)
	{
		return SetQuery("UPDATE wordurl SET urlcount = :1, totalcount = :2, urls = :3 WHERE word_id = :4", param);
	}
	virtual CSQLQuery * DeleteUrlD(CSQLParam *param)
	{
		return SetQuery("DELETE FROM urlword WHERE url_id=:1", param);
	}
	virtual CSQLQuery * FindOriginS(CSQLParam *param)
	{
#ifdef OPTIMIZE_CLONE
		return SetQuery("SELECT url_id FROM urlword WHERE origin=1 AND crc=:1 AND status=200 AND deleted=0 AND url_id!=:2", param);
#else
		return SetQuery("SELECT MIN(url_id) FROM urlword WHERE crc=:1 AND status=200 AND deleted=0 AND url_id<:2", param);
#endif
	}
	virtual CSQLQuery * FindOriginS1(CSQLParam *param)
	{
		return SetQuery("SELECT MIN(url_id) FROM urlword WHERE crc=:1 AND status=200 AND deleted=0 AND url_id<:2 AND site_id=:3", param);
	}
	virtual CSQLQuery * GetDocumentS(CSQLParam *param)
	{
		return SetQuery("SELECT url,url_id,last_index_time,hops,crc,referrer,last_modified,status, site_id,next_index_time,etag FROM urlword WHERE url=:1", param);
	}
	virtual CSQLQuery * GetDocumentS1(CSQLParam *param)
	{
#ifdef OPTIMIZE_CLONE
		return SetQuery("SELECT url,url_id,last_index_time,hops,crc,referrer,last_modified,status, site_id,next_index_time,etag,redir,origin FROM urlword WHERE url_id=:1", param);
#else
		return SetQuery("SELECT url,url_id,last_index_time,hops,crc,referrer,last_modified,status, site_id,next_index_time,etag,redir FROM urlword WHERE url_id=:1", param);
#endif
	}
	virtual CSQLQuery * ClearD1()
	{
		return SetQuery("DELETE from wordurl");
	}
	virtual CSQLQuery * ClearD2()
	{
		return SetQuery("DELETE from wordurl1");
	}
	virtual CSQLQuery * ClearD3()
	{
		return SetQuery("DELETE from robots");
	}
	virtual CSQLQuery * ClearD4()
	{
		return SetQuery("DELETE from sites");
	}
	virtual CSQLQuery * ClearD5()
	{
		return SetQuery("DELETE from urlword");
	}
	virtual CSQLQuery * ClearD6()
	{
		return SetQuery("DELETE from wordsite");
	}
	virtual CSQLQuery * ClearD7()
	{
		return SetQuery("DELETE from citation");
	}
	virtual CSQLQuery * ClearD8(CSQLParam *param)
	{
		return SetQuery("DELETE from urlwords%s", param);
	}
	virtual CSQLQuery * ClearD9()
	{
		return SetQuery("DELETE from urlwords");
	}
	virtual CSQLQuery * MarkForReindexU(CSQLParam *param)
	{
		return SetQuery("UPDATE urlword SET next_index_time = :1 WHERE 1 = 1 %s", param);
	}
	virtual CSQLQuery * GetUrlS(CSQLParam *param)
	{
		return SetQuery("SELECT url FROM urlword where url_id = :1", param);
	}
	virtual CSQLQuery * GetPatternSitesS(CSQLParam *param)
	{
#ifdef UNICODE
		return SetQuery("SELECT urls, totalcount, urlcount, word_id, word FROM wordurl%s WHERE word LIKE :1 AND totalcount != 0", param);
#else
		return SetQuery("SELECT urls, totalcount, urlcount, word_id FROM wordurl%s WHERE word LIKE :1 AND totalcount != 0", param);
#endif
	}
	virtual CSQLQuery * GetPatternS(CSQLParam *param)
	{
		return SetQuery("SELECT urls, totalcount, urlcount, word_id FROM wordurl WHERE word LIKE :1 AND totalcount != 0", param);
	}
	virtual CSQLQuery * GetSiteS(CSQLParam *param)
	{
		return SetQuery("SELECT site_id FROM sites WHERE site=:1", param);
	}
	virtual CSQLQuery * GetSiteS1(CSQLParam *param)
	{
		return SetQuery("SELECT site FROM sites WHERE site_id=:1", param);
	}
	virtual CSQLQuery * GetDocumentS2(CSQLParam *param)
	{
		return SetQuery("SELECT us.description, us.keywords, us.title, us.docsize, u.crc, u.url, us.content_type, u.last_modified, u.etag, us.txt, us.charset%s FROM urlword u, urlwords%s us WHERE u.url_id=:1 AND us.url_id=u.url_id", param);
	}
	virtual CSQLQuery * GetContentS(CSQLParam *param)
	{
		return SetQuery("SELECT url_id FROM urlword WHERE url = :1", param);
	}
	virtual CSQLQuery * GetContentS1(CSQLParam *param)
	{
		return SetQuery("SELECT us.content_type, us.words, us.charset FROM urlwords%s us WHERE us.url_id=:1", param);
	}
	virtual CSQLQuery * GetCloneListS(CSQLParam *param)
	{
		return SetQuery("SELECT url_id, url FROM urlword WHERE crc=:1 AND deleted=0 AND url_id!=:2", param);
	}
	virtual CSQLQuery * AddStatI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO stat(addr,proxy,query,ul,np,ps,urls,sites,start,finish,site,sp,referer) VALUES (:1, :2, :3, :4, :5, :6, :7, :8, :9, :10, :11, :12, :13)", param);
	}
	virtual CSQLQuery * SaveContentsD(CSQLParam *param)
	{
		return SetQuery("DELETE FROM %s WHERE url_id=:1", param);
	}
	virtual CSQLQuery * SaveContentsI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO %s(url_id, wordcount, totalcount, docsize, lang, content_type, charset, words, hrefs, title, txt, keywords, description, deleted) VALUES(:1, :2, :3, :4, :5, :6, :7, :8, :9, :10, :11, :12, :13, :14)", param);
	}
	virtual CSQLQuery * SaveContentsU(CSQLParam *param)
	{
		return SetQuery("UPDATE %s SET docsize=:1,wordcount=:2,totalcount=:3,lang=:4, content_type=:5, charset=:6,title=:7,txt=:8,keywords=:9,description=:10 WHERE url_id=:11", param);
	}
	virtual CSQLQuery * SaveContentsU1(CSQLParam *param)
	{
		return SetQuery("UPDATE urlword SET", param);
	}
	virtual char *SaveContentsU2()
	{
		return " next_index_time = :1, redir = :2 ";
	}
	virtual char *SaveContentsU3()
	{
		return " , status = :3 ";
	}
	virtual char *SaveContentsU4()
	{
		return " last_index_time = :4, tag = :5, crc=:6,last_modified=:7,etag=:8";
	}
	virtual CSQLQuery * MarkDeletedU(CSQLParam *param)
	{
		return SetQuery("UPDATE %s SET deleted=:1 WHERE url_id=:2", param);
	}
	virtual CSQLQuery * SaveDeltaL()
	{
		return SetQuery("lock table wordurl1 write");
	}
	virtual CSQLQuery * SaveDeltaS()
	{
		return SetQuery("SELECT word_id,urls, mod(word_id,100) FROM wordurl1 WHERE length(urls)>0 ORDER BY 3");
	}
	virtual CSQLQuery * SaveDeltaU()
	{
		return SetQuery("UPDATE wordurl1 SET urls='', totalcount=0, urlcount=0 WHERE length(urls)>0");
	}
	virtual CSQLQuery * EmptyDeltaU()
	{
		return SetQuery("UPDATE wordurl1 SET urls='', totalcount=0, urlcount=0 WHERE length(urls)>0");
	}
	virtual CSQLQuery * SaveWord1U(CSQLParam *param)
	{
		return SetQuery("UPDATE wordurl1 SET urlcount = :1, totalcount = :2, urls=:2 WHERE word_id = :4", param);
	}
	virtual CSQLQuery * GetHostsS(CSQLParam *param)
	{
		return SetQuery("SELECT site_id FROM sites WHERE %s ORDER BY site_id", param);
	}
	virtual CSQLQuery * GenerateSpacesS(CSQLParam *param)
	{
		return SetQuery("SELECT space_id FROM spaces WHERE space_id > :1", param);
	}
	virtual CSQLQuery * GenerateSpacesS1(CSQLParam *param)
	{
		return SetQuery("SELECT site_id FROM spaces where space_id = :1 ORDER by 1", param);
	}
	virtual CSQLQuery * GenerateSubsetsS()
	{
		return SetQuery("SELECT subset_id, mask FROM subsets");
	}
	virtual CSQLQuery * GenerateSubsetsS1(CSQLParam *param)
	{
		return SetQuery("SELECT url_id FROM urlword WHERE url LIKE :1 AND status=200 AND deleted=0", param);
	}
	virtual CSQLQuery * PrintPathS(CSQLParam *param)
	{
		return SetQuery("SELECT url_id FROM urlword WHERE url=:1", param);
	}
	virtual CSQLQuery * PrintPathS1(CSQLParam *param)
	{
		return SetQuery("SELECT referrer,url,hops,status FROM urlword WHERE url_id=:1", param);
	}
	virtual CSQLQuery * DeleteTmpD()
	{
		return SetQuery("DELETE FROM tmpurl");
	}
	virtual CSQLQuery * AddTmpI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO tmpurl(url_id, thread) VALUES(:1, :2)", param);
	}
	virtual CSQLQuery * GetStatisticsS(CSQLParam *param)
	{
		return SetQuery("SELECT status, sum(next_index_time<=:1), count(*) FROM urlword WHERE deleted=0 %s GROUP BY status", param);
	}
	virtual CSQLQuery * GetSubsetS(CSQLParam *param)
	{
		return SetQuery("SELECT subset_id FROM subsets WHERE mask=:1", param);
	}
/************************************************/
	virtual CSQLQuery * GetNextLinkS(CSQLParam *param)
	{
		return SetQuery("SELECT url_id,site_id,next_index_time FROM urlword WHERE deleted=0 AND next_index_time<=:1 AND next_index_time > :2 %s ORDER BY next_index_time", param);
	}
	virtual CSQLQuery * GetNextLinkS1(CSQLParam *param)
	{
		 return SetQuery("SELECT url_id,site_id,next_index_time FROM urlword WHERE deleted=0 AND next_index_time=:1 %s", param);
	}
	virtual CSQLQuery * GetNextLinkHS(CSQLParam *param)
	{
		return SetQuery("SELECT url_id,site_id,next_index_time FROM urlword WHERE deleted=0 AND next_index_time<=:1 AND hops=:2 AND next_index_time > :3 %s ORDER BY next_index_time", param);
	}
	virtual CSQLQuery * GetNextLinkHS1(CSQLParam *param)
	{
		return SetQuery("SELECT url_id,site_id,next_index_time FROM urlword WHERE deleted=0 AND hops=:1 AND next_index_time=:2 %s", param);
	}
	virtual CSQLQuery * InsertWordSiteI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO wordsite(word, sites) VALUES(:1, :2)", param);
	}
	virtual CSQLQuery * UpdateWordSiteS(CSQLParam *param)
	{
		return SetQuery("SELECT sites FROM wordsite WHERE word=:1", param);
	}
	virtual CSQLQuery * UpdateWordSiteD(CSQLParam *param)
	{
		return SetQuery("DELETE FROM wordsite WHERE word = :1", param);
	}
	virtual CSQLQuery * GenerateWordSiteS()
	{
		return SetQuery("SELECT site, site_id FROM sites order by site_id");
	}
	virtual CSQLQuery * GenerateWordSiteS1()
	{
		return SetQuery("SELECT word FROM wordsite");
	}
	virtual CSQLQuery * GenerateWordSiteD(CSQLParam *param)
	{
		return SetQuery("DELETE FROM wordsite WHERE word = :1", param);
	}
	virtual CSQLQuery * GetTmpUrlsS()
	{
		return SetQuery("SELECT url_id FROM tmpurl");
	}
	virtual CSQLQuery * GetUrlIDU(CSQLParam *param)
	{
		return SetQuery("UPDATE urlword SET hops=:1,referrer=:2,last_index_time=:3, next_index_time=:4,status=0,deleted=0 WHERE url_id=:5", param);
	}
	virtual CSQLQuery * StoreHref2S(CSQLParam *param)
	{
		return SetQuery("SELECT site_id FROM sites WHERE site=:1", param);
	}
	virtual CSQLQuery * StoreHref2I(CSQLParam *param)
	{
		return SetQuery("INSERT INTO sites(site) VALUES(:1)", param);
	}
	virtual CSQLQuery * StoreHref2I1(CSQLParam *param)
	{
		return SetQuery("INSERT INTO urlword(site_id, hops, referrer, last_index_time, next_index_time, status, url) VALUES(:1,:2,:3,:4,:5,0,:6)", param);
	}
	virtual CSQLQuery * StoreHref2I2(CSQLParam *param)
	{
		return SetQuery("INSERT INTO urlword(site_id, hops, referrer, last_index_time, next_index_time, status, url, url_id) VALUES(:1,:2,:3,:4,:5,0,:6,:7)", param);
	}
	virtual CSQLQuery * StoreHref1S(CSQLParam *param)
	{
		return SetQuery("SELECT site_id FROM sites WHERE site=:1", param);
	}
	virtual CSQLQuery * StoreHref1I(CSQLParam *param)
	{
		return SetQuery("INSERT INTO sites(site) VALUES(:1)", param);
	};
	virtual CSQLQuery * StoreHref1I1(CSQLParam *param)
	{
		return SetQuery("INSERT INTO urlword(site_id, hops, referrer, last_index_time, next_index_time, status, url) VALUES(:1,:2,:3,:4,:5,0,:6)", param);
	}
	virtual CSQLQuery * StoreHref1I2(CSQLParam *param)
	{
		return SetQuery("INSERT INTO urlword(site_id, hops, referrer, last_index_time, next_index_time, status, url, url_id) VALUES(:1,:2,:3,:4,:5,0,:6,:7)", param);
	}
	virtual CSQLQuery * StoreHrefS(CSQLParam *param)
	{
		return SetQuery("SELECT site_id FROM sites WHERE site=:1", param);
	}
	virtual CSQLQuery * StoreHrefI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO sites(site) VALUES(:1)", param);
	}
	virtual CSQLQuery * StoreHrefI1(CSQLParam *param)
	{
		return SetQuery("INSERT INTO urlword(site_id, hops, referrer, last_index_time, next_index_time, status, url) VALUES(:1,:2,:3,:4,:5,0,:6)", param);
	}
	virtual CSQLQuery * SaveLinkD(CSQLParam *param)
	{
		return SetQuery("DELETE FROM citation WHERE url_id = :1", param);
	}
	virtual CSQLQuery * SaveLinkI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO citation(url_id, referrers) VALUES(:1, :2)", param);
	}
	virtual CSQLQuery * CalcTotalS(CSQLParam *param)
	{
		return SetQuery("SELECT count(*) FROM urlwords%s", param);
	}
	virtual CSQLQuery * LoadRanksS()
	{
		return SetQuery("SELECT MAX(url_id) FROM urlword");
	}
	virtual CSQLQuery * LoadRanksS1()
	{
		return SetQuery("SELECT url_id, redir, site_id, last_modified FROM urlword WHERE status!=0");
	}
	virtual CSQLQuery * LoadRanksS2(CSQLParam *param)
	{
		return SetQuery("SELECT url_id, hrefs FROM urlwords%s WHERE length(hrefs) > 0", param);
	}
	virtual CSQLQuery * LoadSitesS()
	{
		return SetQuery("SELECT site from sites");
	}
	virtual CSQLQuery * FindMaxHopsS(CSQLParam *param)
	{
		return SetQuery("SELECT max(hops) from urlword where 1=1 %s", param);
	}
	virtual CSQLQuery * LoadRobotsS()
	{
		return SetQuery("SELECT hostinfo,path FROM robots");
	}
	virtual CSQLQuery * LoadRobotsOfHostS(CSQLParam *param)
	{
		return SetQuery("SELECT path FROM robots WHERE hostinfo = :1", param);
	}
	virtual CSQLQuery * DeleteRobotsFromHostD(CSQLParam *param)
	{
		return SetQuery("DELETE FROM robots WHERE hostinfo=:1", param);
	}
	virtual CSQLQuery * AddRobotsToHostI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO robots(hostinfo,path) VALUES (:1, :2)", param);
	}
	virtual CSQLQuery * DeleteUrlFromSpaceL()
	{
		return SetQuery("lock tables spaces write, sites read");
	}
	virtual CSQLQuery * DeleteUrlFromSpaceS(CSQLParam *param)
	{
		return SetQuery("SELECT site_id FROM sites WHERE site=:1", param);
	}
	virtual CSQLQuery * DeleteUrlFromSpaceS1(CSQLParam *param)
	{
		return SetQuery("SELECT site_id FROM spaces WHERE site_id=:1 and space_id=:2", param);
	}
	virtual CSQLQuery * DeleteUrlFromSpaceD(CSQLParam *param)
	{
		return SetQuery("DELETE FROM spaces WHERE site_id=:1 AND space_id=:2", param);
	}
	virtual CSQLQuery * AddUrlToSpaceL()
	{
		return SetQuery("lock tables spaces write, sites read");
	}
	virtual CSQLQuery * AddUrlToSpaceS(CSQLParam *param)
	{
		return SetQuery("SELECT site_id FROM sites WHERE site=:1", param);
	}
	virtual CSQLQuery * AddUrlToSpaceS1(CSQLParam *param)
	{
		return SetQuery("SELECT site_id FROM spaces WHERE site_id=:1 and space_id=:2", param);
	}
	virtual CSQLQuery * AddUrlToSpaceI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO spaces(site_id,space_id) VALUES (:1, :2)", param);
	}
	virtual CSQLQuery * AddCountryI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO countries(country, addr, mask, expires) VALUES(:1, :2, :3, :4)", param);
	}
	virtual CSQLQuery * LoadCountriesS()
	{
		return SetQuery("SELECT country, addr, mask, expires FROM countries");
	}
	virtual CSQLQuery * ReplaceCountryU(CSQLParam *param)
	{
		return SetQuery("UPDATE countries SET country = :1, expires = :2 WHERE addr = :3", param);
	}
	virtual CSQLQuery * NarrowCountryU(CSQLParam *param)
	{
		return SetQuery("UPDATE countries SET addr=:1, mask=:2 WHERE addr=:3", param);
	}
	virtual CSQLQuery * EDownloadCountryS(CSQLParam *param)
	{
		return SetQuery("SELECT max(addr) from countries1 where addr <= :1", param);
	}
	virtual CSQLQuery * EDownloadCountryS1(CSQLParam *param)
	{
		return SetQuery("SELECT mask, country FROM countries1 WHERE addr = :1", param);
	}
	virtual CSQLQuery * SaveCountriesIfNotEmptyS()
	{
		return SetQuery("SELECT addr FROM countries");
	}
	virtual CSQLQuery * TestCountriesS()
	{
		return SetQuery("SELECT addr,host from addr where substring(addr,1,3) in ('202', '203')");
	}
	virtual CSQLQuery * CheckSyncS()
	{
		return SetQuery("SELECT addr, mask, country FROM countries ORDER by 1");
	}
	virtual CSQLQuery * AddToCacheI(CSQLParam *param)
	{
		return SetQuery("INSERT INTO rescache(qkey, urls) VALUES(:1, :2)", param);
	}
	virtual CSQLQuery * GetCachedS(CSQLParam *param)
	{
		return SetQuery("SELECT urls FROM rescache WHERE qkey = :1", param);
	}
	virtual CSQLQuery * ClearCacheD()
	{
		return SetQuery("DELETE FROM rescache ");
	}
	virtual CSQLQuery * DeleteContentD(CSQLParam *param)
	{
		return SetQuery("DELETE FROM %s WHERE url_id = :1", param);
	}
	virtual CSQLQuery * UpdateOriginU(CSQLParam *param)
	{
		return SetQuery("UPDATE urlword SET origin=2 WHERE url_id=:1", param);
	}
	virtual CSQLQuery * DeleteUrlsS(CSQLParam *param)
	{
		return SetQuery("SELECT url_id, site_id, url, crc, origin, status FROM urlword WHERE deleted=0 %s", param);
	}
	virtual CSQLQuery * UpdateOrigin(CSQLParam *param)
	{
		return SetQuery("UPDATE urlword SET origin=0 WHERE url_id=:1", param);
	}
	virtual CSQLQuery * SiteURLsS()
	{
		return SetQuery("SELECT site_id, COUNT(*) FROM urlword WHERE status != 0 GROUP BY site_id");
	}
};

class CSQLAnswer
{
public:
	virtual int FetchRow() = 0;
	virtual char *GetColumn(int clm) = 0;
	virtual int GetColumn(int clm, ULONG& val) = 0;
	virtual int GetColumn(int clm, int& val) = 0;
	virtual ULONG GetLength(int clm) = 0;
	virtual void FreeResult(void) = 0;
	virtual ~CSQLAnswer(){}
};

void CalcLimit(CSQLQuery *query);

#define	DB_ER_UNKNOWN	0
#define	DB_ER_DUP_ENTRY	1

class CSQLDatabase
{
public:
	pthread_mutex_t mutex;
	double m_fileSaveTime, m_sqlSaveTime, m_fileGetTime, m_sqlGetTime;
	FILE* m_logFile;
	ULONG m_qDocs;
	ULONG m_activeSlows;
	ULONG m_maxSlows;
	char m_str_error[256];
	ULONG m_maxblob;
	CSQLQuery *m_sqlquery;
public:
	CSQLDatabase()
	{
		ClearTimes();
		m_activeSlows = 0;
		m_maxSlows = 0;
		m_qDocs = 0;
		m_logFile = NULL;
		m_str_error[0] = 0;
		m_sqlquery = NULL;
		m_maxblob = 1000; // Fixme
		pthread_mutex_init(&mutex, NULL);
	}
	virtual ~CSQLDatabase()
	{
		CloseDb();
		pthread_mutex_destroy(&mutex);
		if (m_logFile) fclose(m_logFile);
	}
	virtual void CloseDb(){}
	CSQLAnswer *sql_query(CSQLQuery *query);
	CSQLAnswer *sql_real_queryr(CSQLQuery *query);
	virtual CSQLAnswer *sql_queryw(CSQLQuery *query) = 0;
	virtual CSQLAnswer *sql_real_querywr(CSQLQuery *query) {return sql_queryw(query); };
	/* inline */ ULONG sql_query1(CSQLQuery *query, int* err = NULL);
	virtual ULONG sql_queryw1(CSQLQuery *query, int* err = NULL) = 0;
	virtual ULONG sql_real_queryw1(CSQLQuery *query, int* err = NULL) {return sql_queryw1(query, err);};
	/* inline */ int sql_query2(CSQLQuery *query, int* err = NULL);
	virtual int sql_queryw2(CSQLQuery *query, int* err = NULL) = 0;
	virtual int sql_real_queryw2(CSQLQuery *query, int* err = NULL) {return sql_queryw2(query, err);};
	/* inline */ int sql_real_query(CSQLQuery *query);
	virtual int sql_real_queryw(CSQLQuery *query) = 0;
	virtual int GetError(int err) = 0;
	virtual char *DispError() = 0;
	virtual int Connect(const char* host, const char* user, const char* password, const char* dbname, int port=0) = 0;

	void ClearTimes()
	{
		m_fileSaveTime = m_sqlSaveTime = m_fileGetTime = m_sqlGetTime = 0;
	}
#ifdef UNICODE
	int GetWordID(const WORD *cword, ULONG& word_id);
	ULONG GetWordID(CUWord& wword, int& inserted, int real);
	int GetContent(const char* url, char*& content, char* content_type, char* charset);
	int CheckPattern(WORD *elem, int real = 0);
	std::vector<ULONG>* GetPatternIDs(WORD *elem);
	ULONG GetPatternSites(const WORD *pattern, ULONG maxSize, ULONG **sites, WORD **urls, ULONG* counts, FILE** files, int real);
	virtual const WORD* HiByteFirst(WORD* dst, const WORD* src) {return src;};
	virtual void HiByteFirst(BYTE* dst, WORD src) {*(WORD*)dst = src;};
	char* CopyWPattern(char* pQ, const WORD* elem);
#else
	int GetWordID(const char *cword, ULONG& word_id);
	ULONG GetWordID(CWord& wword, int& inserted, int real);
	int GetContent(const char* url, char*& content, char* content_type);
	int CheckPattern(char *elem, int real = 0);
	std::vector<ULONG>* GetPatternIDs(char *elem);
	ULONG GetPatternSites(const char *pattern, ULONG maxSize, ULONG **sites, WORD **urls, ULONG* counts, FILE** files, int real);
#endif
	ULONG GetContents(ULONG UrlID, BYTE** pContents, ULONG& wordCount, ULONG** pHrefs, ULONG* hsz, char* content_type, std::string& charset);
	void GetSites(const char* wrd, ULONG*& sites, ULONG& siteLen);
	void GetSites(ULONG wordID, ULONG *&sites, WORD *&urls, ULONG *counts, FILE*& source, int real);
	ULONG GetUrlID(const char* url, ULONG& redir);
	ULONG GetUrlID(ULONG& redir);
	void GetSitesForLink(ULONG urlID, ULONG *&sites, WORD *&urls);
	ULONG GetUrls(CSQLQuery *query, BYTE** pContents, ULONG& wordID, int& iSource, ULONG* counts = NULL);
	ULONG GetUrls(CWord& wword, BYTE** pContents, ULONG& wordID, int& iSource);
	ULONG GetUrls(ULONG wordID, BYTE** pContents, int& iSource, ULONG* counts);
	ULONG GetUrls1(ULONG wordID, BYTE** pContents, ULONG* counts);
	void SaveWord(ULONG wordID, BYTE* urls, ULONG urlLen, ULONG ucount,ULONG tcount, int iSource);
	ULONG FindOrigin(char *crc, ULONG urlID, ULONG siteID);
	void DeleteUrl(ULONG url);
	CDocument* GetDocument(const char* url);
	CDocument* GetDocument(ULONG urlID);
	int Clear();
	void PrintTimes();
	int GetUrl(ULONG urlID, std::string &url);
	ULONG GetPattern(const char *pattern, ULONG maxSize, BYTE **buffers, ULONG *sizes, ULONG *counts);
	int GetSite(const char *host, ULONG &siteID);
	int GetSite(int site, char* name);
	int GetDocument(ULONG urlID, CSrcDocument &doc, int content = 0);
	void GetCloneList(const char *crc, ULONG origin, CSrcDocVector &docs, ULONG siteID);
	void AddStat(const char* proxy, const char* addr, const char *pquery,
		const char* ul, int np, int ps, int urls, int sites, struct timeval &start,
		struct timeval &finish, const char* spaces, int site, const char* referer);
	void SaveDelta(CDeltaFiles* files);
	void EmptyDelta();
	void SaveWord1(ULONG wordID, BYTE* urls, ULONG urlLen, BYTE* sites,ULONG siteLen, ULONG* counts);
	void SaveWord(ULONG wordID, BYTE* urls, ULONG urlLen, BYTE* sites, ULONG siteLen, ULONG ucount, ULONG tcount, int iSource);
	std::vector<ULONG>* GetHosts(const char* hostmask, int& nums);
	void GenerateSpaces();
	void GenerateSubsets();
	void ClearCache();
	void PrintPath(const char* url);
	void DeleteTmp();
	void AddTmp(ULONG urlID, int thread);
	int GetSubset(char* subset);
	void OpenLog(char* logname);
	void DeleteContent(ULONG url_id);
	void UpdateOrigin(ULONG url_id);
};

typedef CSQLDatabase * PSQLDatabase;

// In each DB driver, you need to provide this function:
extern "C" {
	PSQLDatabase newDataBase(void); /* return new CSomeSQLDatabase; */
}

int SQLMatchW(const WORD* str, int len, const WORD* pattern);
BYTE* CopyUtfPattern(BYTE* pQ, const WORD* elem);
void CopyUtf(BYTE* dest, const WORD* src);

extern int HiLo;
extern int UtfStorage;

#endif /* _SQLDB_H_ */
