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

/*  $Id: sqldb.cpp,v 1.19 2003/05/08 22:01:50 matt Exp $
    Author : Alexander F. Avdonkin, Igor Sukhih
*/

#include "aspseek-cfg.h"
#include <unistd.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>
#include "sqldb.h"
#include "logger.h"
#include "index.h"
#include "zlib.h"
#include "timer.h"

typedef std::vector<ULONG> CULONGVector;

string DBHost, DBName, DBUser, DBPass, DBType = "mysql";
string DBWordDir;
extern string DataDir;

int DBPort;
int npages = -1;
int curUrl = 0;


ULONG UrlBufferSize = 0;
ULONG DeltaBufferSize = 0x200;		// in Kb
ULONG NextDocLimit = 1000;

int Multi = 1;
int HiLo = 0;
int UtfStorage = 0;

#ifdef UNICODE
int SQLMatch(const char* string, int len, const WORD* pattern);
#endif

void CalcWordDir()
{
	if (DBWordDir.size() == 0)
		DBWordDir = DataDir+"/"+DBName;
}

int ParseDBAddr(const char *addr)
{
	int len = strlen(addr) + 1;

	char *specific = new char[len] ;
	char *hostinfo = new char[len];
	char *hostname = new char[len];
	char *auth = new char[len];
	char *dbtype = new char[len];
	char *dbname = new char[len];
	char *dbhost = new char[len];
	char *dbuser = new char[len];
	char *dbpass = new char[len];

	dbtype[0]=0;
	auth[0] = 0;
	specific[0]=0;
	hostinfo[0]=0;
	hostname[0]=0;
	dbname[0]=0;
	dbuser[0]=0;
	dbpass[0]=0;
	dbhost[0]=0;

	if(sscanf(addr,"%[^:]:%s", dbtype , specific) == 2)
	{
		switch (sscanf(specific, "//%[^/]/%s", hostinfo, dbname))
		{
			case 1: break;
			case 2:
				char *ch;
				if ((ch = strrchr(dbname, '/')))
					*ch = '\0';
					break;
			default: return 1;
		}

		switch (sscanf(hostinfo, "%[^@]@%s", auth, hostname))
		{
			case 1:
				strcpy(hostname, auth);
				break;
			case 2:
				sscanf(auth, "%[^:]:%s", dbuser, dbpass);
				break;
			default: break;
		}

		sscanf(hostname, "%[^:]:%d", dbhost, &DBPort);
	}
	if(dbtype[0])
		DBType = dbtype;
	if(dbname[0])
		DBName = dbname;
	if(dbuser[0])
		DBUser = dbuser;
	if(dbpass[0])
		DBPass = dbpass;
	if(dbhost[0])
		DBHost = dbhost;

	delete dbname;
	delete dbhost;
	delete dbuser;
	delete dbpass;
	delete dbtype;
	delete auth;
	delete specific;
	delete hostinfo;
	delete hostname;

	return 0;
}

#ifdef UNICODE
char* CSQLDatabase::CopyWPattern(char* pQ, const WORD* elem)
{
	for (const WORD* c = elem; *c; c++)
	{
		if ((*c == '_') || (*c == '%'))
		{
			*pQ++ = *c;
			*pQ++ = *c;
		}
		else
		{
			if (HiLo)
			{
				HiByteFirst((BYTE*)pQ, *c);
			}
			else
			{
				memcpy(pQ, (char*)c, sizeof(WORD));
			}
			pQ += 2;
		}
	}
	*(WORD*)pQ = 0;
	pQ += 2;
	return pQ;
}

BYTE* CopyUtfPattern(BYTE* pQ, const WORD* elem)
{
	for (const WORD* c = elem; *c; c++)
	{
		if ((*c == '_') || (*c == '%'))
		{
			*pQ++ = '%';
		}
		else
		{
			Utf8Code(pQ, *c);
		}
	}
	*pQ++ = 0;
	return pQ;
}
#endif

CSQLAnswer *CSQLDatabase::sql_query(CSQLQuery *query)
{
	CLocker lock(this);
	return sql_queryw(query);
}

CSQLAnswer *CSQLDatabase::sql_real_queryr(CSQLQuery *query)
{
	CLocker lock(this);
	return sql_real_querywr(query);
}

ULONG CSQLDatabase::sql_query1(CSQLQuery *query, int* err)
{
	CLocker lock(this);
	return sql_queryw1(query, err);
}

int CSQLDatabase::sql_query2(CSQLQuery *query, int* err)
{
	CLocker lock(this);
	return sql_queryw2(query, err);
}

int CSQLDatabase::sql_real_query(CSQLQuery *query)
{
	CLocker lock(this);
	return sql_real_queryw(query);
}

#ifdef UNICODE
void CopyUtf(BYTE* dest, const WORD* src)
{
	while (*src)
	{
		Utf8Code(dest, *src++);
	}
	*dest = 0;
}
#endif

#ifdef UNICODE
int CSQLDatabase::GetWordID(const WORD *cword, ULONG& word_id)
#else
int CSQLDatabase::GetWordID(const char *cword, ULONG& word_id)
#endif
{
//	sprintf(qSel, "SELECT word_id FROM wordurl WHERE word = '%s'", cword);
	CSQLParam p;
#ifdef UNICODE
	WORD* rword;
	if (UtfStorage)
	{
		BYTE* utfWord;
		ULONG len = strwlen(cword);
		utfWord = (BYTE*)alloca(len * 3 + 1);
		CopyUtf(utfWord, cword);
		p.AddParam((char*)utfWord);
	}
	else
	{
		if (HiLo)
		{
			ULONG len = strwlen(cword);
			rword = (WORD*)alloca((len + 1) << 1);
			p.AddParam(HiByteFirst(rword, cword), len);
		}
		else
		{
			p.AddParam(cword);
		}
	}
#else
	p.AddParam(cword);
#endif
	CSQLQuery *sqlquery = m_sqlquery->GetWordIDS(&p);
	CSQLAnswer *answ = sql_real_queryr(sqlquery);
	if (answ && answ->FetchRow())
	{
		answ->GetColumn(0, word_id);
		if (answ) delete answ;
		return 1;
	}
	if (answ) delete answ;
	return 0;
}

#ifdef UNICODE
ULONG CSQLDatabase::GetWordID(CUWord& wword, int& inserted, int real)
#else
ULONG CSQLDatabase::GetWordID(CWord& wword, int& inserted, int real)
#endif
{
	ULONG word_id;
	CLocker lock(this);
//	sql_queryw2("lock tables wordurl1 write,wordurl write");
	CSQLQuery *sqlquery = m_sqlquery->GetWordIDL();
	sql_queryw2(sqlquery);
//	sprintf(qSel, "SELECT word_id FROM wordurl WHERE word = '%s'", wword.Word());
	CSQLParam p;
	const CHAR* rw = NULL;
	ULONG len = STRLEN(wword.Word());
#ifdef UNICODE
	WORD* rword;
	BYTE* utfWord = NULL;
	if (UtfStorage)
	{
		utfWord = (BYTE*)alloca(len * 3 + 1);
		CopyUtf(utfWord, wword.Word());
		p.AddParam((char*)utfWord);
	}
	else
	{
		if (HiLo)
		{
			rword = (WORD*)alloca((len + 1) << 1);
			rw = HiByteFirst(rword, wword.Word());
		}
		else
		{
			rw = wword.Word();
		}
		p.AddParam(rw, len);
	}
#else
	rw = wword.Word();
	p.AddParam(rw, len);
#endif
	sqlquery = m_sqlquery->GetWordIDS(&p);
	CSQLAnswer *answ = sql_real_querywr(sqlquery);
	if (answ && answ->FetchRow())
	{
		answ->GetColumn(0, word_id);
		inserted = 0;
	}
	else
	{
//		sprintf(qIns, "INSERT INTO wordurl(word) VALUES ('%s')", wword.Word());
		CSQLParam p;
#ifdef UNICODE
		if (UtfStorage)
			p.AddParam((char*)utfWord);
		else
#endif
			p.AddParam(rw, len);
		sqlquery = m_sqlquery->GetWordIDI(&p);
		word_id = sql_real_queryw1(sqlquery);
		inserted = 1;
	}
	if (real)
	{
//		sprintf(qIns, "INSERT INTO wordurl1(word, word_id) VALUES ('%s', %lu)", wword.Word(), word_id);
		CSQLParam p;
#ifdef UNICODE
		if (UtfStorage)
			p.AddParam((char*)utfWord);
		else
#endif
			p.AddParam(rw, len);
		p.AddParam(&word_id);
		sqlquery = m_sqlquery->GetWordIDI2(&p);
		sql_real_queryw2(sqlquery);
	}
//	sql_queryw2("unlock tables");
	sqlquery = m_sqlquery->UnlockTables();
	sql_queryw2(sqlquery);
	if (answ) delete answ;
	return word_id;
}

char* UWTable(char* buf, ULONG urlID)
{
	if (Multi)
	{
		sprintf(buf, "urlwords%02lu", urlID & 15);
	}
	else
	{
		strcpy(buf, "urlwords");
	}
	return buf;
}

ULONG CSQLDatabase::GetContents(ULONG UrlID, BYTE** pContents, ULONG& wordCount, ULONG** pHrefs, ULONG* hsz, char* content_type, string& charset)
{
	ULONG len;
	char uwt[20];
//	sprintf(qSel, "SELECT words, hrefs, wordCount, content_type FROM %s WHERE url_id = %li", UWTable(uwt, UrlID), UrlID);
	CSQLParam p;
	p.AddParam(UWTable(uwt, UrlID));
	p.AddParam(&UrlID);
	CSQLQuery *sqlquery = m_sqlquery->GetContentsS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);

	if (answ && answ->FetchRow())
	{
		if (!answ->GetColumn(2, wordCount))
		{
			wordCount = 0;
		}

		len = answ->GetLength(0);
		if (len > 0)
		{
			*pContents = new BYTE[len + 1];
			memcpy(*pContents, answ->GetColumn(0), len);
			(*pContents)[len] = 0;
		}
		else
		{
			*pContents = NULL;
		}
		if (pHrefs && hsz)
		{
			*hsz = answ->GetLength(1);
			if (*hsz)
			{
				*pHrefs = new ULONG[*hsz >> 2];
				memcpy(*pHrefs, answ->GetColumn(1), *hsz);
			}
			else
			{
				*pHrefs = NULL;
			}
		}
		if (content_type)
		{
			strcpy(content_type, answ->GetColumn(3));
		}
		charset = answ->GetColumn(4);
	}
	else
	{
		wordCount = 0;
		*pContents = NULL;
		len = 0;
		if (pHrefs && hsz)
		{
			*hsz = 0;
			*pHrefs = NULL;
		}
		if (content_type)
		{
			content_type[0] = 0;
		}
	}
	if (answ) delete answ;
	return len;
}

void CSQLDatabase::GetSites(const char* wrd, ULONG*& sites, ULONG& siteLen)
{
//	"SELECT sites FROM wordsite WHERE word='%s'", wrd);
	siteLen = 0;
	CSQLParam p;
	p.AddParam(wrd);
	CSQLQuery *sqlquery = m_sqlquery->GetSitesS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	if (answ && answ->FetchRow())
	{
		siteLen = (answ->GetLength(0)) >> 2;
		if (siteLen)
		{
			sites = new ULONG[siteLen];
			memcpy(sites, answ->GetColumn(0), siteLen << 2);
		}
	}
	if (answ) delete answ;
}

ULONG CSQLDatabase::GetUrlID(const char* url, ULONG& redir)
{
	ULONG urlID;
	if (strlen(url) > MAX_URL_LEN)
	{
		return 0;
	}
	else
	{
//		"SELECT url_id, redir FROM urlword where url='");
		CSQLParam p;
		p.AddParamEsc(url);
		CSQLQuery *sqlquery = m_sqlquery->GetUrlIDS(&p);
		CSQLAnswer *answ = sql_query(sqlquery);
		if (answ && answ->FetchRow())
		{
			answ->GetColumn(0, urlID);
			if (!answ->GetColumn(1, redir))
			{
				redir = 0;
			}
		}
		else
		{
			urlID = 0;
		}
		if (answ) delete answ;
		return urlID;
	}
}

ULONG CSQLDatabase::GetUrlID(ULONG& redir)
{
//	"SELECT url_id, redir FROM urlword where url_id=%lu", redir);
	ULONG urlID;
	CSQLParam p;
	p.AddParam(&redir);
	CSQLQuery *sqlquery = m_sqlquery->GetUrlIDS1(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	if (answ && answ->FetchRow())
	{
		answ->GetColumn(0, urlID);
		if (!answ->GetColumn(1, redir))
		{
			redir = 0;
		}
	}
	else
	{
		urlID = 0;
	}
	if (answ) delete answ;
	return urlID;
}

void CSQLDatabase::GetSitesForLink(ULONG urlID, ULONG *&sites, WORD *&urls)
{
// "SELECT referrers FROM citation WHERE url_id = %lu", urlID);
	CSQLParam p;
	p.AddParam(&urlID);
	CSQLQuery *sqlquery = m_sqlquery->GetSitesForLinkS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	if (answ && answ->FetchRow())
	{
		ULONG len = answ->GetLength(0);
		if (len > 0)
		{
			ULONG sitelen = *(ULONG*)answ->GetColumn(0);
			sites = new ULONG[sitelen >> 2];
			memcpy(sites, answ->GetColumn(0), sitelen);
			urls = (WORD*)(new BYTE[len - sitelen]);
			memcpy(urls, answ->GetColumn(0) + sitelen, len - sitelen);
		}
		else
		{
			urls = NULL;
			sites = NULL;
			len = 0;
		}
	}
	else
	{
		urls = NULL;
		sites = NULL;
	}
	if (answ) delete answ;
}

void CSQLDatabase::GetSites(ULONG wordID, ULONG *&sites, WORD *&urls, ULONG *counts, FILE*& source, int real)
{
	ULONG len;
// "SELECT urls,word_id,totalcount,urlcount FROM wordurl%s WHERE word_id = %lu", real ? "1" : "", wordID);
	CSQLParam p;
	p.AddParam(real ? "1" : "");
	p.AddParam(&wordID);
	CSQLQuery *sqlquery = m_sqlquery->GetSitesS1(&p);

	struct timeval tm1, tm;
	gettimeofday(&tm, NULL);
	CSQLAnswer *answ = sql_query(sqlquery);
	gettimeofday(&tm1, NULL);
	m_sqlGetTime += timedif(tm1, tm);
	if (answ && answ->FetchRow())
	{
		len = answ->GetLength(0);
		if (counts)
		{
			if (!answ->GetColumn(2, counts[0]))
			{
				counts[0] = 0;
			}
			if (!answ->GetColumn(3, counts[1]))
			{
				counts[1] = 0;
			}
		}
		if (len > 0)
		{
			if (len > 4)
			{
				ULONG sitelen = *(ULONG*)answ->GetColumn(0);
				sites = new ULONG[sitelen >> 2];
				memcpy(sites, answ->GetColumn(0), sitelen);
				urls = (WORD*)(new BYTE[len - sitelen]);
				memcpy(urls, answ->GetColumn(0) + sitelen, len - sitelen);
			}
			else
			{
				urls = NULL;
				sites = NULL;
				len = 0;
			}
		}
		else
		{
			ULONG cnt, res;
			res = answ->GetColumn(2, cnt);
			if (res && cnt > 0)
			{
				char fname[1000];
				sprintf(fname, "%s/%02luw/%lu", DBWordDir.c_str(), wordID % 100, wordID);
				source = fopen(fname, "r");
				if (source)
				{
					gettimeofday(&tm, NULL);
					fseek(source, 0, SEEK_END);
					len = ftell(source);
					if (len > 0)
					{
						ULONG sitelen;
						fseek(source, 0, SEEK_SET);
						fread(&sitelen, 4, 1, source);
						sites = new ULONG[sitelen >> 2];
						sites[0] = sitelen;
						fread(sites + 1, sitelen - 4, 1, source);
					}
					gettimeofday(&tm1, NULL);
					m_fileGetTime += timedif(tm1, tm);
				}
				else
				{
					logger.log(CAT_ALL, L_ERR, "Error open file: %s\n", fname);
				}
			}
		}
	}
	else
	{
		urls = NULL;
		sites = NULL;
		len = 0;
	}
	if (answ) delete answ;
}

ULONG CSQLDatabase::GetUrls(CSQLQuery *query, BYTE** pContents, ULONG& wordID, int& iSource, ULONG* counts)
{
	ULONG len;

	struct timeval tm1, tm;
	gettimeofday(&tm, NULL);
	CSQLAnswer *answ = sql_query(query);
	gettimeofday(&tm1, NULL);
	m_sqlGetTime += timedif(tm1, tm);
	if (answ && answ->FetchRow())
	{
		len = answ->GetLength(0);
		answ->GetColumn(1, wordID);
		if (counts)
		{
			if (!answ->GetColumn(2, counts[0]))
			{
				counts[0] = 0;
			}
			if (!answ->GetColumn(3, counts[1]))
			{
				counts[1] = 0;
			}
		}
		iSource = 0;
		if (len > 0)
		{
			*pContents = new BYTE[len];
			memcpy(*pContents, answ->GetColumn(0), len);
		}
		else
		{
			*pContents = NULL;
			ULONG cnt, res;
			res = answ->GetColumn(2, cnt);
			if (res && cnt > 0)
			{
				char fname[1000];
				sprintf(fname, "%s/%02luw/%lu", DBWordDir.c_str(), wordID % 100, wordID);
				FILE* file = fopen(fname, "r");
				if (file)
				{
					gettimeofday(&tm, NULL);
					fseek(file, 0, SEEK_END);
					len = ftell(file);
					if (len > 0)
					{
						fseek(file, 0, SEEK_SET);
						*pContents = new BYTE[len];
						fread(*pContents, len, 1, file);
					}
					fclose(file);
					gettimeofday(&tm1, NULL);
					m_fileGetTime += timedif(tm1, tm);
					iSource = 1;
				}
			}
		}
	}
	else
	{
		wordID = 0;
		*pContents = NULL;
		len = 0;
	}
	if (answ) delete answ;
	return len;
}
/*
ULONG CSQLDatabase::GetUrls(CWord& wword, BYTE** pContents, ULONG& wordID, int& iSource)
{
// "SELECT urls,word_id,totalcount FROM wordurl WHERE word = '%s'", wword.Word());
	CSQLParam p;
	p.AddParam(wword.Word());
	CSQLQuery *sqlquery = m_sqlquery->GetUrlsS(&p);
	int len = GetUrls(sqlquery, pContents, wordID, iSource);
	if (wordID == 0)
	{
//		"INSERT INTO wordurl(word) VALUES ('%s')", wword.Word());
		CSQLParam p;
		p.AddParam(wword.Word());
		sqlquery = m_sqlquery->GetUrlsI(&p);
		wordID = sql_query1(sqlquery);
	}
	return len;
}
*/
ULONG CSQLDatabase::GetUrls(ULONG wordID, BYTE** pContents, int& iSource, ULONG* counts)
{
	ULONG word;
//	"SELECT urls,word_id,totalcount,urlcount FROM wordurl WHERE word_id = %lu", wordID);
	CSQLParam p;
	p.AddParam(&wordID);
	CSQLQuery *sqlquery = m_sqlquery->GetUrlsS1(&p);
	return GetUrls(sqlquery, pContents, word, iSource, counts);
}

#ifdef UNICODE
int CSQLDatabase::CheckPattern(WORD *elem, int real)
#else
int CSQLDatabase::CheckPattern(char *elem, int real)
#endif
{
	ULONG nr = 0;
//	sprintf(qSel, "SELECT count(*) FROM wordurl%s WHERE word LIKE '%s' AND totalcount > 0", real ? "1" : "", elem);
	CSQLParam p;
	p.AddParam(real ? "1" : "");
	ULONG len = STRLEN(elem);
#ifdef UNICODE
	if (UtfStorage)
	{
		BYTE* rw;
		rw = (BYTE*)alloca(len * 3 + 1);
		CopyUtfPattern(rw, elem);
		p.AddParam((char*)rw);
	}
	else
	{
		const CHAR* rw;
		rw = (WORD*)alloca((len + 1) << 1);
		CopyWPattern((char*)rw, elem);
		p.AddParam(rw, len);
	}
#else
	p.AddParam(elem, len);
#endif
	CSQLQuery *sqlquery = m_sqlquery->CheckPatternS(&p);
	CSQLAnswer *answ = sql_real_queryr(sqlquery);
	if (answ && answ->FetchRow())
	{
		if (!answ->GetColumn(0, nr))
		{
			nr = 0;
		}
	}
	if (answ) delete answ;
	return nr;
}

#ifdef UNICODE
vector<ULONG>* CSQLDatabase::GetPatternIDs(WORD *elem)
#else
vector<ULONG>* CSQLDatabase::GetPatternIDs(char *elem)
#endif
{
//	ULONG nr = 0;
//	sprintf(qSel, "SELECT word_id FROM wordurl WHERE word LIKE '%s', elem);
	CSQLParam p;
	ULONG len = STRLEN(elem);
#ifdef UNICODE
	if (UtfStorage)
	{
		BYTE* rw;
		rw = (BYTE*)alloca(len * 3 + 1);
		CopyUtfPattern(rw, elem);
		p.AddParam((char*)rw);
	}
	else
	{
		const CHAR* rw;
		rw = (WORD*)alloca((len + 1) << 1);
		CopyWPattern((char*)rw, elem);
		p.AddParam(rw, len);
	}
#else
	p.AddParam(elem, len);
#endif
	CSQLQuery *sqlquery = m_sqlquery->GetPatternIDs(&p);
	CSQLAnswer *answ = sql_real_queryr(sqlquery);
	if (answ)
	{
		vector<ULONG>* wordIDs = new vector<ULONG>;
		while (answ->FetchRow())
		{
#ifdef UNICODE
			if (SQLMatch(answ->GetColumn(1), answ->GetLength(1), elem) == 0) continue;
#endif
			ULONG wordID;
			answ->GetColumn(0, wordID);
			wordIDs->push_back(wordID);
		}
		delete answ;
		return wordIDs;
	}
	return NULL;
}

ULONG CSQLDatabase::GetUrls1(ULONG wordID, BYTE** pContents, ULONG* counts)
{
	ULONG len;
//	sprintf(qSel, "SELECT urls,word_id,totalcount,urlcount FROM wordurl1 WHERE word_id = %lu", wordID);
	CSQLParam p;
	p.AddParam(&wordID);
	CSQLQuery *sqlquery = m_sqlquery->GetUrls1S(&p);

	CSQLAnswer *answ = sql_queryw(sqlquery);
	if (answ && answ->FetchRow())
	{
		len = answ->GetLength(0);
		answ->GetColumn(1, wordID);
		if (counts)
		{
			if (!answ->GetColumn(2, counts[0]))
			{
				counts[0] = 0;
			}
			if (!answ->GetColumn(3, counts[1]))
			{
				counts[1] = 0;
			}
		}
		if (len > 0)
		{
			*pContents = new BYTE[len];
			memcpy(*pContents, answ->GetColumn(0), len);
		}
		else
		{
			*pContents = NULL;
		}
	}
	else
	{
		wordID = 0;
		*pContents = NULL;
		len = 0;
	}
	if (answ) delete answ;
	return len;
}

void CSQLDatabase::SaveWord(ULONG wordID, BYTE* urls, ULONG urlLen, ULONG ucount, ULONG tcount, int iSource)
{
	if (urlLen > m_maxblob)
	{
		char fname[1000], fname1[1000];
		sprintf(fname, "%s/%02luw/%lu", DBWordDir.c_str(), wordID % 100, wordID);
		sprintf(fname1, "%s.1", fname);
		FILE* file = fopen(fname1, "w");
		if (file)
		{
			struct timeval tm, tm1;
			gettimeofday(&tm, NULL);
			fwrite(urls, urlLen, 1, file);
			fclose(file);
			unlink(fname);
			rename(fname1, fname);
			gettimeofday(&tm1, NULL);
			m_fileSaveTime += timedif(tm1, tm);
// "UPDATE wordurl SET urlcount = %lu, totalcount = %lu, urls = '' WHERE word_id = %lu", ucount, tcount, wordID);
			CSQLParam p;
			p.AddParam(&ucount);
			p.AddParam(&tcount);
			p.AddParam(&wordID);
			CSQLQuery *sqlquery = m_sqlquery->SaveWordU(&p);
			sql_query2(sqlquery);
		}
	}
	else
	{
/* "UPDATE wordurl SET urlcount = %lu, totalcount = %lu, urls='", ucount, tcount);
		pq = pQuery + strlen(pQuery);
		pq = memcpyq(pq, (char*)urls, urlLen);
		sprintf(pq, "' WHERE word_id = %lu", wordID);
		pq += strlen(pq);
*/
		CSQLParam p;
		p.AddParam(&ucount);
		p.AddParam(&tcount);
		p.AddParamEsc((char*)urls, urlLen);
		p.AddParam(&wordID);
		CSQLQuery *sqlquery = m_sqlquery->SaveWordU1(&p);

		struct timeval tm, tm1;
		gettimeofday(&tm, NULL);
		sql_real_query(sqlquery);
		gettimeofday(&tm1, NULL);
		m_sqlSaveTime += timedif(tm1, tm);
		if (iSource == 1)
		{
			char fname[1000];
			sprintf(fname, "%s/%02luw/%lu", DBName.c_str(), wordID % 100, wordID);
			unlink(fname);
		}
	}
}

void CSQLDatabase::DeleteUrl(ULONG url)
{
//	sprintf(qbuf, "DELETE FROM urlword WHERE url_id=%lu", url);
	CSQLParam p;
	p.AddParam(&url);
	CSQLQuery *sqlquery = m_sqlquery->DeleteUrlD(&p);
	sql_query2(sqlquery);
}

ULONG CSQLDatabase::FindOrigin(char *crc, ULONG urlID, ULONG siteID = 0)
{
	int origin = 0;
	if (!crc) return 0;
	if (*crc == 0) return 0;

//	"SELECT MIN(url_id) FROM urlword WHERE crc='%s' AND status=200 AND deleted=0 AND url_id<%lu", crc, urlID);
	CSQLParam p;
	p.AddParam(crc);
	p.AddParam(&urlID);
	CSQLQuery *sqlquery = m_sqlquery->FindOriginS(&p);
#ifdef LIMIT_CLONES_BY_SITE
//	" AND site_id=%lu", siteID);
	if (siteID)
	{
		sqlquery->AddQuery(" AND site_id=:3 ");
		p.AddParam(&siteID);
	}
#endif
	CSQLAnswer *answ = sql_query(sqlquery);
	if (answ && answ->FetchRow())
	{
		if (!answ->GetColumn(0, origin))
		{
			origin = 0;
		}
	}
	if (answ) delete answ;
	return origin;
}

int IsRobots(const char* url)
{
	int len = strlen(url);
	if (len >= 10)
	{
		return memcmp(url + len - 10, "robots.txt", 10) == 0;
	}
	else
	{
		return 0;
	}
}

CDocument* CSQLDatabase::GetDocument(const char* url)
{
// Called only from CWordCache::RealIndex, where length of "url" is checked
/* "SELECT url,url_id,last_index_time,hops,crc,	referrer,last_modified,status,site_id,
next_index_time,etag FROM urlword u WHERE url='");
	pq = strcpyq(pq, url);
	strcpy(pq, "'");
*/
	CSQLParam p;
	p.AddParamEsc(url);
	CSQLQuery *sqlquery = m_sqlquery->GetDocumentS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	CDocument* doc = NULL;
	if (answ && answ->FetchRow())
	{
		doc = new CDocument;
		doc->m_crc = answ->GetColumn(4);
		answ->GetColumn(3, doc->m_hops);
		answ->GetColumn(5, doc->m_referrer);
		if (answ->GetColumn(6))
			doc->m_last_modified = answ->GetColumn(6);
		if (answ->GetColumn(10))
			doc->m_etag = answ->GetColumn(10);
		answ->GetColumn(1, doc->m_urlID);
		answ->GetColumn(7, doc->m_status);
		answ->GetColumn(8, doc->m_siteID);
		answ->GetColumn(2, doc->m_last_index_time);
		answ->GetColumn(9, doc->m_next_index_time);
		doc->m_url = answ->GetColumn(0);
		doc->m_slow = IsRobots(doc->m_url.c_str());
	}
	if (answ) delete answ;
	return doc;
}

CDocument* CSQLDatabase::GetDocument(ULONG urlID)
{
// sprintf(qSel, "SELECT url,url_id,last_index_time,hops,crc,referrer,last_modified,
//  status,site_id,next_index_time,etag FROM urlword WHERE url_id=%lu", urlID);
	CSQLParam p;
	p.AddParam(&urlID);
	CSQLQuery *sqlquery = m_sqlquery->GetDocumentS1(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	CDocument* doc = NULL;
	if (answ && answ->FetchRow())
	{
		doc = new CDocument;
		doc->m_crc = answ->GetColumn(4);
		answ->GetColumn(3, doc->m_hops);
		answ->GetColumn(5, doc->m_referrer);
		if (answ->GetColumn(6))
			doc->m_last_modified = answ->GetColumn(6);
		if (answ->GetColumn(10))
			doc->m_etag = answ->GetColumn(10);
		answ->GetColumn(1, doc->m_urlID);
		answ->GetColumn(7, doc->m_status);
		answ->GetColumn(8, doc->m_siteID);
		answ->GetColumn(2, doc->m_last_index_time);
		answ->GetColumn(9, doc->m_next_index_time);
		doc->m_url = answ->GetColumn(0);
		doc->m_slow = IsRobots(doc->m_url.c_str());
		answ->GetColumn(11, doc->m_redir);
#ifdef OPTIMIZE_CLONE
		answ->GetColumn(12, doc->m_origin);
#endif
	}
	if (answ) delete answ;
	return doc;
}

int CSQLDatabase::Clear()
{
	int res = IND_OK;
	CSQLQuery *sqlquery = m_sqlquery->ClearD1();
// "DELETE from wordurl"
	if (sql_queryw2(sqlquery)) res = IND_ERROR;
	sqlquery = m_sqlquery->ClearD2();
// "DELETE from wordurl1"
	if (sql_queryw2(sqlquery)) res = IND_ERROR;
	sqlquery = m_sqlquery->ClearD3();
// ,"DELETE from robots"))
	if (sql_queryw2(sqlquery)) res = IND_ERROR;
		sqlquery = m_sqlquery->ClearD4();
// "DELETE from sites"))
	if (sql_queryw2(sqlquery)) res = IND_ERROR;
	sqlquery = m_sqlquery->ClearD5();
// "DELETE from urlword"))
	if (sql_queryw2(sqlquery)) res = IND_ERROR;
	sqlquery = m_sqlquery->ClearD6();
// "DELETE from wordsite"))
	if (sql_queryw2(sqlquery)) res = IND_ERROR;
	sqlquery = m_sqlquery->ClearD7();
// "DELETE from citation"))
	if (sql_queryw2(sqlquery)) res = IND_ERROR;
	if (Multi)
	{
		for (ULONG u = 0; u < 16; u++)
		{
//			"DELETE from urlwords%02lu", u);
		char id[3];
			sprintf(id, "%02lu", u);
			CSQLParam p;
			p.AddParam(id);
			sqlquery = m_sqlquery->ClearD8(&p);
			if (sql_queryw2(sqlquery)) res = IND_ERROR;
		}
	}
	else
	{
//		"DELETE from urlwords";
		sqlquery = m_sqlquery->ClearD9();
		if (sql_queryw2(sqlquery)) res = IND_ERROR;
	}
	return res;
}

int CSQLDatabase::GetUrl(ULONG urlID, string &url)
{
//	sprintf(qSel, "SELECT url FROM urlword where url_id = %lu", urlID);
	CSQLParam p;
	p.AddParam(&urlID);
	CSQLQuery *sqlquery = m_sqlquery->GetUrlS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	if (answ && answ->FetchRow())
	{
		url = answ->GetColumn(0);
		if (answ) delete answ;
		return 1;
	}
	if (answ) delete answ;
	return 0;
}

void CSQLDatabase::PrintTimes()
{
	if (m_logFile)
	{
		fprintf(m_logFile, "File read: %8.3f, File write: %8.3f, SQL read: %8.3f, SQL write:  %8.3f\n", m_fileGetTime, m_fileSaveTime, m_sqlGetTime, m_sqlSaveTime);
	}
}

#ifdef UNICODE
ULONG CSQLDatabase::GetPatternSites(const WORD *pattern, ULONG maxSize, ULONG **sites, WORD **urls, ULONG* counts, FILE** files, int real)
#else
ULONG CSQLDatabase::GetPatternSites(const char *pattern, ULONG maxSize, ULONG **sites, WORD **urls, ULONG* counts, FILE** files, int real)
#endif
{
// "SELECT urls, totalcount, urlcount, word_id FROM wordurl%s WHERE word LIKE '%s'
// AND totalcount != 0 LIMIT %lu", real ? "1" : "", pattern, maxSize);
	CSQLParam p;
	p.AddParam(real ? "1" : "");
	ULONG len = STRLEN(pattern);
#ifdef UNICODE
	if (UtfStorage)
	{
		BYTE* rw;
		rw = (BYTE*)alloca(len * 3 + 1);
		CopyUtfPattern(rw, pattern);
		p.AddParam((char*)rw);
	}
	else
	{
		const CHAR* rw;
		rw = (WORD*)alloca((len + 1) << 1);
		CopyWPattern((char*)rw, pattern);
		p.AddParam(rw, len);
	}
#else
	p.AddParam(pattern, len);
#endif
	CSQLQuery *sqlquery = m_sqlquery->GetPatternSitesS(&p);
	sqlquery->AddLimit(maxSize);
	CSQLAnswer *answ = sql_real_queryr(sqlquery);

	ULONG size = 0;
	counts[0] = counts[1] = 0;
	while ((answ && answ->FetchRow()))
	{
		ULONG c;
#ifdef UNICODE
		if (SQLMatch(answ->GetColumn(4), answ->GetLength(4), pattern) == 0) continue;
#endif
		if (answ->GetColumn(1, c))
		{
			counts[0] += c;
		}
		if (!answ->GetColumn(2, c))
		{
			counts[1] += c;
		}
		ULONG len = answ->GetLength(0);
		sites[size] = NULL;
		urls[size] = NULL;
		if (len > 0)
		{
			ULONG sitelen = *(ULONG*)answ->GetColumn(0);
			sites[size] = new ULONG[sitelen >> 2];
			memcpy(sites[size], answ->GetColumn(0), sitelen);
			urls[size] = (WORD*)(new BYTE[len - sitelen]);
			memcpy(urls[size], answ->GetColumn(0) + sitelen, len - sitelen);
			files[size] = NULL;
		}
		else
		{
			ULONG cnt, res;
			res = answ->GetColumn(2, cnt);
			if (res && (cnt > 0))
			{
				char fname[1000];
				ULONG wordID;
				answ->GetColumn(3, wordID);
				sprintf(fname, "%s/%02luw/%lu", DBWordDir.c_str(), wordID % 100, wordID);
				FILE* file = fopen(fname, "r");
				if (file)
				{
					CTimerAdd timer(m_fileGetTime);
					fseek(file, 0, SEEK_END);
					len = ftell(file);
					if (len > 0)
					{
						ULONG sitelen;
						fseek(file, 0, SEEK_SET);
						fread(&sitelen, 4, 1, file);
						sites[size] = new ULONG[sitelen >> 2];
						sites[size][0] = sitelen;
						fread(sites[size] + 1, sitelen - 4, 1, file);
					}
				}
				files[size] = file;
			}
		}
		size++;
	}
	if (answ) delete answ;
	return size;
}

ULONG CSQLDatabase::GetPattern(const char *pattern, ULONG maxSize, BYTE **buffers, ULONG *sizes, ULONG *counts)
{
// "SELECT urls, totalcount, urlcount, word_id FROM wordurl WHERE word LIKE '%s'
// AND totalcount != 0 LIMIT %lu", pattern, maxSize);
	CSQLParam p;
	p.AddParam(pattern);
	CSQLQuery *sqlquery = m_sqlquery->GetPatternS(&p);
	sqlquery->AddLimit(maxSize);
	CSQLAnswer *answ = sql_query(sqlquery);
	ULONG size = 0;
	while ((answ && answ->FetchRow()))
	{
		if (!answ->GetColumn(1, counts[size << 1]))
		{
			counts[size << 1] = 0;
		}
		if (!answ->GetColumn(2, counts[(size << 1) + 1]))
		{
			counts[(size << 1) + 1] = 0;
		}
		ULONG len = answ->GetLength(0);
		if (len > 0)
		{
			buffers[size] = new BYTE[len];
			memcpy(buffers[size], answ->GetColumn(0), len);
			sizes[size] = len;
		}
		else
		{
			buffers[size] = NULL;
			if (counts[size << 1] > 0)
			{
				char fname[1000];
				ULONG id;
				answ->GetColumn(3, id);
				sprintf(fname, "%s/%02luw/%s", DBWordDir.c_str(), id % 100, answ->GetColumn(3));
				FILE* file = fopen(fname, "r");
				if (file)
				{
					fseek(file, 0, SEEK_END);
					len = ftell(file);
					sizes[size] = len;
					if (len > 0)
					{
						fseek(file, 0, SEEK_SET);
						buffers[size] = new BYTE[len];
						fread(buffers[size], len, 1, file);
					}
					fclose(file);
				}
			}
		}
		size++;
	}
	if (answ) delete answ;
	return size;
}

int CSQLDatabase::GetSite(const char *host, ULONG &siteID)
{
	if (strlen(host) > MAX_URL_LEN)
	{
		return 0;
	}
	else
	{
//		"SELECT site_id FROM sites WHERE site='%s'", host);
		CSQLParam p;
		p.AddParam(host);
		CSQLQuery *sqlquery = m_sqlquery->GetSiteS(&p);
		CSQLAnswer *answ = sql_query(sqlquery);
		if ((answ && answ->FetchRow()))
		{
			answ->GetColumn(0, siteID);
			if (answ) delete answ;
			return 1;
		}
		if (answ) delete answ;
		return 0;
	}
}

int CSQLDatabase::GetSite(int site, char* name)
{
//	sprintf(sqlquery, "SELECT site FROM sites WHERE site_id=%u", site);
	CSQLParam p;
	p.AddParam(&site);
	CSQLQuery *sqlquery = m_sqlquery->GetSiteS1(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	if ((answ && answ->FetchRow()))
	{
		strcpy(name, answ->GetColumn(0));
		if (answ) delete answ;
		return 1;
	}
	if (answ) delete answ;
	return 0;
}

BYTE* Uncompress(BYTE* cont, ULONG len, unsigned long int & uLen)
{
	if (cont && (len >= 4))
	{
		BYTE* uc;
		uLen = *(ULONG*)cont;
		if (uLen == 0xFFFFFFFF)
		{
			uLen = len - 4;
			uc = new BYTE[uLen + 1];
			memcpy(uc, cont + 4, uLen);
		}
		else
		{
			uc = new BYTE[uLen + 1];
			uncompress(uc, &uLen, cont + 4, len);
		}
		uc[uLen] = 0;
		return uc;
	}
	else
	{
		uLen = 0;
		return NULL;
	}
}

int CSQLDatabase::GetDocument(ULONG urlID, CSrcDocument &doc, int content)
{
/* "SELECT us.description, us.keywords, us.title, us.docsize, u.crc, u.url,\
 us.content_type, u.last_modified, u.etag, us.txt%s FROM urlword u, urlwords%02lu us WHERE \
 u.url_id=%lu AND us.url_id=u.url_id", content ? ",us.words" : "", urlID & 15, urlID);
*/
	CSQLParam p;
	p.AddParam(content ? ",us.words" : "");
	char buf[3];
	sprintf(buf, "%02lu", urlID & 15);
	p.AddParam(buf);
	p.AddParam(&urlID);
	CSQLQuery *sqlquery = m_sqlquery->GetDocumentS2(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	if (answ && answ->FetchRow())
	{
		doc.m_descr = answ->GetColumn(0);
		doc.m_keywords = answ->GetColumn(1);
		doc.m_title = answ->GetColumn(2);
		answ->GetColumn(3, doc.m_size);
		doc.m_crc = answ->GetColumn(4);
		doc.m_url = answ->GetColumn(5);
		doc.m_content_type = answ->GetColumn(6);
		doc.m_last_modified = answ->GetColumn(7);
		if (answ->GetColumn(8)) doc.m_etag = answ->GetColumn(8);
		doc.m_text = answ->GetColumn(9);
		if (content && answ->GetColumn(11))
		{
			unsigned long int uLen;
			doc.m_content = (char*)Uncompress((BYTE*)answ->GetColumn(11), answ->GetLength(11), uLen);
			if (doc.m_content == (answ->GetColumn(11) + 4)) doc.m_content = strdup(doc.m_content);
		}
#ifdef UNICODE
		doc.m_charset = answ->GetColumn(10);
#endif
		if (answ) delete answ;
		return 1;
	}
	else
	{
		if (answ) delete answ;
		return 0;
	}
}

#ifdef UNICODE
int CSQLDatabase::GetContent(const char* url, char*& content, char* content_type, char* charset)
#else
int CSQLDatabase::GetContent(const char* url, char*& content, char* content_type)
#endif
{
	if (strlen(url) > MAX_URL_LEN)
	{
		content = NULL;
		return 0;
	}
	else
	{
// "SELECT url_id FROM urlword WHERE url = '");
//		pquery = strcpyq(pquery, url);
	ULONG urlID;
	CSQLParam p;
		p.AddParamEsc(url);
		CSQLQuery *sqlquery = m_sqlquery->GetContentS(&p);
		CSQLAnswer *answ = sql_query(sqlquery);
		if ((answ && answ->FetchRow()))
		{
			answ->GetColumn(0, urlID);
		}
		else
		{
			content = NULL;
			if (answ) delete answ;
			return 0;
		}
		if (answ) delete answ;
// "SELECT us.content_type, us.words FROM urlwords%02lu us WHERE us.url_id=%lu", urlID & 15, urlID);
		content = NULL;
		p.Clear();
		char buf[3];
		sprintf(buf, "%02lu", urlID & 15);
		p.AddParam(buf);
		p.AddParam(&urlID);
		sqlquery = m_sqlquery->GetContentS1(&p);
		answ = sql_query(sqlquery);
		if (answ && answ->FetchRow())
		{
			strcpy(content_type, answ->GetColumn(0) ? answ->GetColumn(0) : "");
			if (answ->GetColumn(1))
			{
				unsigned long int uLen;
				content = (char*)Uncompress((BYTE*)answ->GetColumn(1), answ->GetLength(1), uLen);
			}
#ifdef UNICODE
			strcpy(charset, answ->GetColumn(2));
#endif
		}
		else
		{
			content = NULL;
			if (answ) delete answ;
			return 0;
		}
		if (answ) delete answ;
		return 1;
	}
}

void CSQLDatabase::GetCloneList(const char *crc, ULONG origin, CSrcDocVector &docs, ULONG siteID)
{
// "SELECT url_id, url FROM urlword WHERE crc='%s' AND deleted=0 AND url_id!=%lu LIMIT 10", crc, origin);
	CSQLParam p;
	p.AddParam(crc);
	p.AddParam(&origin);
	CSQLQuery *sqlquery = m_sqlquery->GetCloneListS(&p);
#ifdef LIMIT_CLONES_BY_SITE
//	len += sprintf(qbuf + len, " AND site_id=%lu", siteID);
	sqlquery->AddQuery(" AND site_id=:3 ");
	p.AddParam(&siteID);
#endif
#ifdef OPTIMIZE_CLONE
	sqlquery->AddQuery(" AND origin = 0 ");
#endif
	sqlquery->AddLimit(10);

	CSQLAnswer *answ = sql_query(sqlquery);
	while (answ && answ->FetchRow())
	{
		CSrcDocument* doc = new CSrcDocument;
		answ->GetColumn(0, doc->m_urlID);
		doc->m_url = answ->GetColumn(1);
		docs.push_back(doc);
	}
	if (answ) delete answ;
}

void CSQLDatabase::AddStat(const char* proxy, const char* addr, const char *pquery,
		const char* ul, int np, int ps, int urls, int sites, struct timeval &start,
		struct timeval &finish, const char* spaces, int site, const char* referer)
{
	char csite[20];
	double start_tm = start.tv_sec + (double)start.tv_usec / 1000000.0;
	double finish_tm = finish.tv_sec + (double)finish.tv_usec / 1000000.0;

	if (site == 0)
	{
		strcpy(csite, "NULL");
	}
	else
	{
		sprintf(csite, "%i", site);
	}
/*	char* pq = query + sprintf(query, "INSERT INTO stat(addr, proxy, query, ul, np, ps, urls,\
sites, start, finish, site, sp, referer) VALUES ('%s', '%s', '%s', '%s', %i, %i, %i, %i, %lu.%03lu, \
%lu.%03lu, %s, '",addr ? addr : proxy, addr ? proxy : "", pquery, ul, np, ps, urls, sites, \
 start.tv_sec, start.tv_usec / 1000, finish.tv_sec, finish.tv_usec / 1000, csite);
	pq = strcpyq(pq, spaces);
	pq += sprintf(pq, "','");
	pq = strcpyq(pq, referer);
	pq += sprintf(pq, "')");
*/
	CSQLParam p;
	p.AddParamEsc(addr ? addr : proxy);
	p.AddParamEsc(addr ? proxy : "");
	p.AddParamEsc(pquery);
	p.AddParamEsc(ul);
	p.AddParam(&np);
	p.AddParam(&ps);
	p.AddParam(&urls);
	p.AddParam(&sites);
	p.AddParam(&start_tm);
	p.AddParam(&finish_tm );
	p.AddParam(&site);
	p.AddParamEsc(spaces);
	p.AddParamEsc(referer);
	CSQLQuery *sqlquery = m_sqlquery->AddStatI(&p);
	sql_query2(sqlquery);
}

//Fixme "check needed"
void CSQLDatabase::SaveDelta(CDeltaFiles* files)
{
	int res;
	CLocker lock(this);
// "lock table wordurl1 write");
	CSQLQuery *sqlquery = m_sqlquery->SaveDeltaL();
	sql_queryw2(sqlquery);

// "SELECT word_id,urls,mod(word_id,100) FROM wordurl1 WHERE length(urls)>0 ORDER BY 3");
	sqlquery = m_sqlquery->SaveDeltaS();
	CSQLAnswer *answ = sql_queryw(sqlquery);

	if (!answ )
		return;
	res = answ->FetchRow();
	while (res)
	{
		int w100, w100_;
		answ->GetColumn(2, w100);
		w100_ = w100;
		CLocker(files, w100);
		FILE* f = files->m_files[w100];
		while (res && (w100 == (answ->GetColumn(2, w100_), w100_)))
		{
			ULONG* sites = (ULONG*)answ->GetColumn(1);
			if (sites)
			{
				ULONG word_id;
				answ->GetColumn(0, word_id);
				ULONG urlOffset = sites[0];
				ULONG* esites = ((ULONG*)((char*)answ->GetColumn(1) + urlOffset)) - 1;
				for (; sites < esites; sites += 2)
				{
					WORD* url = (WORD*)((BYTE*)answ->GetColumn(1) + sites[0]);
					WORD* urle = (WORD*)((BYTE*)answ->GetColumn(1) + sites[2]);
					while (url < urle)
					{
						WORD cnt = 1;
						fwrite(sites + 1, 4, 1, f);
						fwrite(url, 4, 1, f);
						fwrite(&cnt, 2, 1, f);
						fwrite(&word_id, 4, 1, f);
						fwrite(url + 2, 2, url[2] + 1, f);
						url += url[2] + 3;
					}
				}
			}
			res = answ->FetchRow();
		}
	}
// "UPDATE wordurl1 SET urls='', totalcount=0, urlcount=0 WHERE length(urls)>0");
	sqlquery = m_sqlquery->SaveDeltaU();
	sql_queryw2(sqlquery);
//	sql_queryw2("unlock tables");
	sqlquery = m_sqlquery->UnlockTables();
	sql_queryw2(sqlquery);
	if (answ) delete answ;
}

void CSQLDatabase::EmptyDelta()
{
// "UPDATE wordurl1 SET urls='', totalcount=0, urlcount=0 WHERE length(urls)>0");
	CSQLQuery *sqlquery = m_sqlquery->EmptyDeltaU();
	sql_query2(sqlquery);

}

void CSQLDatabase::SaveWord1(ULONG wordID, BYTE* urls, ULONG urlLen, BYTE* sites, ULONG siteLen, ULONG* counts)
{
/* "UPDATE wordurl1 SET urlcount = %lu, totalcount = %lu, urls='", counts[1], counts[0]);
	pq = pQuery + strlen(pQuery);
	pq = memcpyq(pq, (char*)sites, siteLen);
	pq = memcpyq(pq, (char*)urls, urlLen);
	sprintf(pq, "' WHERE word_id = %lu", wordID);
	pq += strlen(pq);
*/
	char *buf;
	CSQLParam p;
	p.AddParam(&counts[1]);
	p.AddParam(&counts[0]);

	buf = (char*)alloca(siteLen + urlLen);
	memcpy(buf, (char*)sites, siteLen);
	memcpy(buf + siteLen, (char*)urls, urlLen);
	p.AddParamEsc(buf, siteLen + urlLen);

//	p.AddParamEsc((char*)sites, siteLen);
//	p.AddParamEsc((char*)urls, urlLen, 1);
	p.AddParam(&wordID);
	CSQLQuery *sqlquery = m_sqlquery->SaveWord1U(&p);
	sql_real_queryw(sqlquery);
}

void CSQLDatabase::SaveWord(ULONG wordID, BYTE* urls, ULONG urlLen, BYTE* sites, ULONG siteLen, ULONG ucount, ULONG tcount, int iSource)
{
	if (urlLen + siteLen > m_maxblob)
	{
		char fname[1000], fname1[1000];
		sprintf(fname, "%s/%02luw/%lu", DBWordDir.c_str(), wordID % 100, wordID);
		sprintf(fname1, "%s.1", fname);
		FILE* file = fopen(fname1, "w");
		if (file)
		{
			struct timeval tm, tm1;
			gettimeofday(&tm, NULL);
			fwrite(sites, siteLen, 1, file);
			fwrite(urls, urlLen, 1, file);
			fclose(file);
			unlink(fname);
			rename(fname1, fname);
			gettimeofday(&tm1, NULL);
			m_fileSaveTime += timedif(tm1, tm);
// "UPDATE wordurl SET urlcount = %lu, totalcount = %lu, urls = '' WHERE word_id = %lu", ucount, tcount, wordID);
			CSQLParam p;
			p.AddParam(&ucount);
			p.AddParam(&tcount);
			p.AddParam(&wordID);
			CSQLQuery *sqlquery = m_sqlquery->SaveWordU(&p);
			sql_query2(sqlquery);
		}
	}
	else
	{
/* "UPDATE wordurl SET urlcount = %lu, totalcount = %lu, urls='", ucount, tcount);
		pq = pQuery + strlen(pQuery);
		pq = memcpyq(pq, (char*)sites, siteLen);
		pq = memcpyq(pq, (char*)urls, urlLen);
		sprintf(pq, "' WHERE word_id = %lu", wordID);
*/
		CSQLParam p;
		p.AddParam(&ucount);
		p.AddParam(&tcount);

		char *buf = (char*)alloca(siteLen + urlLen);
		memcpy(buf, (char*)sites, siteLen);
		memcpy(buf + siteLen, (char*)urls, urlLen);
		p.AddParamEsc(buf, siteLen + urlLen);

//		p.AddParamEsc((char*)sites, siteLen);
//		p.AddParamEsc((char*)urls, urlLen, 1);
		p.AddParam(&wordID);
		CSQLQuery *sqlquery = m_sqlquery->SaveWordU1(&p);

		struct timeval tm, tm1;
		gettimeofday(&tm, NULL);
		sql_real_query(sqlquery);
		gettimeofday(&tm1, NULL);
		m_sqlSaveTime += timedif(tm1, tm);
		if (iSource == 1)
		{
			char fname[1000];
			sprintf(fname, "%s/%02luw/%lu", DBWordDir.c_str(), wordID % 100, wordID);
			unlink(fname);
		}
	}
}

vector <ULONG>* CSQLDatabase::GetHosts(const char* hostmask, int& nums)
{
// hostmask is limited by total CGI query length, which is limited to 2000
// "SELECT site_id FROM sites where %s ORDER BY site_id", hostmask);
	CSQLParam p;
	p.AddParam(hostmask);
	CSQLQuery *sqlquery = m_sqlquery->GetHostsS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);

	vector <ULONG> *sites = NULL;
	ULONG site_id;
	nums = 0;
	while (answ && answ->FetchRow())
	{
		if (!nums)
		{
			sites = new vector <ULONG>;
		}
		answ->GetColumn(0, site_id);
		sites->push_back(site_id);
		nums++;
	}
	if (answ) delete answ;
	return sites;
}

void CSQLDatabase::GenerateSpaces()
{
	int spacen = 0;
	CSQLAnswer *answ;
	while (true)
	{
// "SELECT space_id FROM spaces where space_id > %i LIMIT 1", spacen);
		CSQLParam p;
		p.AddParam(&spacen);
		CSQLQuery *sqlquery = m_sqlquery->GenerateSpacesS(&p);
		sqlquery->AddLimit(1);
		answ = sql_query(sqlquery);
		if (!answ)
			break;
		if (!answ->FetchRow())
		{
			delete answ;
			break;
		}
		answ->GetColumn(0, spacen);
		if (answ) delete answ;
		logger.log(CAT_ALL, L_INFO, "Generating space %i ...", spacen);
// "SELECT site_id FROM spaces where space_id = %i ORDER by 1", spacen);
		p.Clear();
		p.AddParam(&spacen);
		sqlquery = m_sqlquery->GenerateSpacesS1(&p);
		answ = sql_query(sqlquery);
		vector <ULONG> sitevec;
		while(answ && answ->FetchRow())
		{
			ULONG site_id;
			answ->GetColumn(0, site_id);
			sitevec.push_back(site_id);
		}
		if (answ) delete answ;
		ULONG rows, mins, maxs;
		rows = sitevec.size();
		mins = sitevec[0];
		maxs = sitevec[rows - 1];
		mins >>= 5;
		maxs >>= 5;
		int len = maxs - mins + 1;
		ULONG* space = new ULONG[len];
		memset(space, 0, len << 2);
		for (ULONG i = 0; i < rows; i++)
		{
			space[(sitevec[i] >> 5) - mins] |= (1 << (sitevec[i] & 0x1F));
		}
		char fname1[1000], fname[1000];
		sprintf(fname1, "%s/subsets/space%i.1", DBWordDir.c_str(), spacen);
		sprintf(fname, "%s/subsets/space%i", DBWordDir.c_str(), spacen);
		FILE* f = fopen(fname1, "w");
		if (f)
		{
			if (space)
			{
				fwrite(&mins, 4, 1, f);
				fwrite(space, 4, len, f);
			}
			fclose(f);
		}
		unlink(fname);
		rename(fname1, fname);
		delete space;
		logger.log(CAT_ALL, L_INFO, " done (%lu sites)\n", rows);
	}
}

void CSQLDatabase::GenerateSubsets()
{
	char dir[1000];
// "SELECT subset_id, mask FROM subsets");
	CSQLQuery *sqlquery = m_sqlquery->GenerateSubsetsS();
	CSQLAnswer *answ = sql_query(sqlquery);
	if (!answ)
		return;
	sprintf(dir, "%s/subsets", DBWordDir.c_str());
	mkdir(dir, 0775);
	while (answ && answ->FetchRow())
	{
		logger.log(CAT_ALL, L_INFO, "Generating subset %s ...", answ->GetColumn(1));
//	subset.masks filed is varchar(128)
// "SELECT url_id FROM urlword WHERE url LIKE '%s' AND status=200 AND deleted=0 ORDER BY url_id", row[1]);
		CSQLParam p;
		p.AddParam(answ->GetColumn(1));
		sqlquery = m_sqlquery->GenerateSubsetsS1(&p);
		CSQLAnswer *answ1 = sql_query(sqlquery);
		CULONGVector urlsvec;
		while (answ1 && answ1->FetchRow())
		{
			ULONG url_id;
			answ1->GetColumn(0, url_id);
			urlsvec.push_back(url_id);
		}
		sort(urlsvec.begin(), urlsvec.end());
		int rows = urlsvec.size();
		char fname1[1000], fname[1000];
		ULONG subset_id;
		answ->GetColumn(0, subset_id);
		sprintf(fname1, "%s/subsets/sub%lu.1", DBWordDir.c_str(), subset_id);
		sprintf(fname, "%s/subsets/sub%lu", DBWordDir.c_str(), subset_id);
		FILE* f = fopen(fname1, "w");
		if (f)
		{
			if (urlsvec.size())
			{
				fwrite(&(*urlsvec.begin()), 4, rows, f);
			}
			fclose(f);
		}
		unlink(fname);
		rename(fname1, fname);
		if (answ1) delete answ1;
		logger.log(CAT_ALL, L_INFO, " done (%d URLs)\n", rows);
	}
	if (answ) delete answ;
}

void CSQLDatabase::ClearCache()
{
	CSQLParam p;
	CSQLQuery *sqlquery = m_sqlquery->ClearCacheD();
	sql_query2(sqlquery);
}


void CSQLDatabase::PrintPath(const char* url)
{
	if (strlen(url) > MAX_URL_LEN)
	{
		logger.log(CAT_ALL, L_ERR, "Too long URL\n");
	}
	else
	{
// "SELECT url_id FROM urlword WHERE url='%s'", url);
		CSQLParam p;
		p.AddParam(url);
		CSQLQuery *sqlquery = m_sqlquery->PrintPathS(&p);
		CSQLAnswer *answ = sql_query(sqlquery);
		if (answ && answ->FetchRow())
		{
			printf("URL Id: %s\n", answ->GetColumn(0));
			printf("Hops Status URL\n");
			int referrer;
			do
			{
// "SELECT referrer,url,hops,status FROM urlword WHERE url_id=%s", row[0]);
				ULONG url_id;
				answ->GetColumn(0, url_id);
				CSQLParam p;
				p.AddParam(&url_id);
				sqlquery = m_sqlquery->PrintPathS1(&p);
				if (answ) delete answ;
				answ = sql_query(sqlquery);
				if (!answ->FetchRow()) break;
				int hops, status;
				answ->GetColumn(2, hops);
				answ->GetColumn(3, status);
				printf("%4i %6i %s\n", hops, status, answ->GetColumn(1));
			}
			while ((answ->GetColumn(0, referrer), referrer) != 0);
		}
		else
		{
			logger.log(CAT_ALL, L_ERR, "URL not found: %s\n", url);
		}
		if (answ) delete answ;
	}
}

void CSQLDatabase::DeleteTmp()
{
//	"DELETE FROM tmpurl");
	CSQLQuery *sqlquery = m_sqlquery->DeleteTmpD();
	sql_query1(sqlquery);
}

void CSQLDatabase::AddTmp(ULONG urlID, int thread)
{
//	"INSERT INTO tmpurl(url_id, thread) VALUES(%lu, %i)", urlID, thread);
	CSQLParam p;
	p.AddParam(&urlID);
	p.AddParam(&thread);
	CSQLQuery *sqlquery = m_sqlquery->AddTmpI(&p);
	sql_query1(sqlquery);
}

int CSQLDatabase::GetSubset(char* subset)
{
	if (strlen(subset) > MAX_URL_LEN)
	{
		return 0;
	}
	else
	{
/* "SELECT subset_id FROM subsets WHERE mask='");
		pquery = strcpyq(pquery, subset);
		strcpy(pquery, "'");
*/
		CSQLParam p;
		p.AddParamEsc(subset);
		CSQLQuery *sqlquery = m_sqlquery->GetSubsetS(&p);
		CSQLAnswer *answ = sql_query(sqlquery);
		if (answ && answ->FetchRow())
		{
			ULONG sid;
			if (!answ->GetColumn(0, sid))
			{
				sid = 0;
			}
			if (answ) delete answ;
			return sid;
		}
		if (answ) delete answ;
		return 0;
	}
}

void CSQLDatabase::OpenLog(char* logname)
{
	string dir = DBWordDir+"/"+logname;
	m_logFile = fopen(dir.c_str(), "a");
	if (m_logFile)
	{
		fprintf(m_logFile, "New indexing session started at: %li\n", now());
		fflush(m_logFile);
	}
}

int SQLMatchW(const WORD* string, int len, const WORD* pattern)
{
	const WORD* stringIndex, *stringStartScan;
	const WORD* patternIndex, *patternStartScan;
	stringIndex = string;
	patternIndex = pattern;
	stringStartScan = NULL;
	patternStartScan = NULL;

	while (*patternIndex)
	{
		WORD p = *patternIndex++;
		if (p == '%')
		{
			if (*patternIndex == 0) return 1;
			stringStartScan = stringIndex;
			patternStartScan = patternIndex;
		}
		else
		{
			if (stringIndex - string >= len) return 0;
			WORD t = *stringIndex++;
			if (((t != p) && (p != '_')) || ((*patternIndex == 0) && (stringIndex - string < len)))
			{
				if (stringStartScan == NULL) return 0;
				stringIndex += patternStartScan - patternIndex + 1;
				patternIndex = patternStartScan;
			}
		}
	}
	if (stringIndex - string < len) return 0;
	return 1;
}

#ifdef UNICODE
int SQLMatch(const char* string, int len, const WORD* pattern)
{
	if (UtfStorage)
	{
		WORD* rw = (WORD*)alloca(len << 1);
		WORD* pw = rw;
		const BYTE* ps = (const BYTE*)string;
		while (ps - (BYTE*)string < len)
		{
			*pw++ = UCode(ps);
		}
		return SQLMatchW(rw, pw - rw, pattern);
	}
	else
	{
		return SQLMatchW((const WORD*)string, len >> 1, pattern);
	}
}
#endif
