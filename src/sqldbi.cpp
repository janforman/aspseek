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

/*  $Id: sqldbi.cpp,v 1.26 2002/06/18 11:20:38 kir Exp $
    Author : Alexander F. Avdonkin, Igor Sukhih
*/

#include <algorithm>
#include <vector>
#include <string>
#include <string.h>
#include "aspseek-cfg.h"
#include "sqldbi.h"
#include "parse.h"
#include "index.h"
#include "rank.h"
#include "logger.h"
#include "lastmod.h"
#include "timer.h"
#include "geo.h"
#include "zlib.h"
#include "deltas.h"
#include "content.h"
#include "ucharset.h"
#include "delmap.h"

using std::sort;
using std::vector;
using std::string;

extern hash_set<CURLName> Sites;

typedef struct
{
	ULONG urlID, siteID;
	int time;
} URLN;

CSQLFilter* SQLFilter = NULL;
string Limit;
CStringVector urlLimits, tagLimits, statusLimits;

void AddUrlLimit(char* url)
{
	urlLimits.push_back(url);
}

void AddTagLimit(char* url)
{
	tagLimits.push_back(url);
}

void AddStatusLimit(char* url)
{
	statusLimits.push_back(url);
}

int ClearURLLimit()
{
	Limit = "";
	return 0;
}

char *CalcLimit(CSQLQuery *query, CStringVector& limits, const char* field)
{
	if (limits.size() > 0)
	{
//		sprintf(buf, " AND (urlword.%s LIKE '%s' ", field, limits[0].c_str());
		query->AddQuery(query->LimitAnd());
		CSQLParam p;
		p.AddParam(field);
		p.AddParam(limits[0].c_str());

		for (CStringVector::iterator str = limits.begin() + 1; str != limits.end(); str++)
		{
//			sprintf(buf, " OR urlword.%s LIKE '%s' ", field, str->c_str());
			query->AddQuery(query->LimitOr());
			p.AddParam(field);
			p.AddParam(str->c_str());
		}
//		strcpy(buf, ") ");
		query->AddQuery(")");
		return query->MakeQuery(&p);
	}
	return "";
}

int HasZero(CStringVector& limits)
{
	if (limits.size() == 0) return 1;
	for (CStringVector::iterator s = limits.begin(); s != limits.end(); s++)
	{
		if (atoi(s->c_str()) == 0) return 1;
	}
	return 0;
}

void CalcLimit(CSQLQuery *query)
{
	query->Clear();
	Limit = CalcLimit(query, urlLimits, "url");
	query->Clear();
	Limit += CalcLimit(query, tagLimits, "tag");
	query->Clear();
	Limit += CalcLimit(query, statusLimits, "status");
	if (HasZero(tagLimits) || HasZero(statusLimits))
	{
		if (urlLimits.size() > 0)
		{
			if (urlLimits.size() == 1)
			{
				SQLFilter = new CSQLUrlFilter(urlLimits[0].c_str());
			}
			else
			{
				CSQLOrFilter* of = new CSQLOrFilter;
				for (CStringVector::iterator s = urlLimits.begin(); s != urlLimits.end(); s++)
				{
					of->m_filters.push_back(new CSQLUrlFilter(s->c_str()));
				}
				SQLFilter = of;
			}
		}
		else
		{
			SQLFilter = new CSQLTrueFilter;
		}
	}
	else
	{
		SQLFilter = new CSQLFalseFilter;
	}
}


int CSQLDatabaseI::AddUrls(CSQLQuery *query, double& time, int& maxtime, CIntSet* set)
{
	CSQLAnswer *answ;
	int numr = 0;
	{
		CTimerAdd timer(time);
		answ = sql_query(query);
	}
	if (answ)
	{
		vector<URLN> urls;
		while (answ->FetchRow())
		{
			URLN url;
			answ->GetColumn(0, url.urlID);
			answ->GetColumn(1, url.siteID);
			answ->GetColumn(2, url.time);
			urls.push_back(url);
		}
		delete answ;
		int sz = set ? set->size() : 0;
		for (vector<URLN>::iterator purl = urls.begin(); purl != urls.end(); purl++)
		{
			if ((sz == 0) || (set == NULL) || (set->find(purl->urlID) == set->end()))
			{
				m_squeue.AddURL(purl->siteID, purl->urlID, this);
				numr++;
			}
			if (sz == 0)
			{
				if (purl->time != maxtime)
				{
					maxtime = purl->time;
					if (set) set->clear();
				}
				if (set) set->insert(purl->urlID);
			}
		}
	}
	return numr;
}

ULONG CSQLDatabaseI::GetNextLink(int numthreads)
{
	while (m_squeue.m_inactiveSize < (numthreads << 2))
	{
		int numr = 0;
		double sel = 0;
		CIntSet set;
		int mintime = m_maxtime;
/* "SELECT url_id,site_id,next_index_time FROM urlword WHERE deleted=0 \
AND next_index_time<=%d AND next_index_time > %i %s ORDER BY next_index_time LIMIT %lu",
(int)now(), m_maxtime, Limit.c_str(), NextDocLimit);
*/
		CSQLParam p;
		int tm = now();
		p.AddParam(&tm);
		p.AddParam(&m_maxtime);
		p.AddParam(Limit.c_str());
		CSQLQuery *sqlquery = m_sqlquery->GetNextLinkS(&p);
		sqlquery->AddLimit(NextDocLimit);

		numr = AddUrls(sqlquery, sel, m_maxtime, &set);
		if (numr > 0)
		{
/* "SELECT url_id,site_id,next_index_time FROM urlword \
WHERE deleted=0 AND next_index_time=%d %s", m_maxtime, Limit.c_str());
*/
			CSQLParam p;
			p.AddParam(&m_maxtime);
			p.AddParam(Limit.c_str());
			CSQLQuery *sqlquery = m_sqlquery->GetNextLinkS1(&p);
			numr += AddUrls(sqlquery, sel, m_maxtime, &set);
		}
		if (numr && m_logFile)
		{
			fprintf(m_logFile, "Got next %6i URLs for: %7.3f seconds. Queued docs: %5lu.Time %i-%i.%s\n",
numr, sel, m_squeue.m_qDocs, mintime, m_maxtime,m_available ? "" : "Not available!");
			fflush(m_logFile);
			m_got = 1;
		}
		if (numr == 0)
		{
			m_available = 0;
			break;
		}
	}
	return m_squeue.ExtractURL();
}

ULONG CSQLDatabaseI::GetNextLinkH(int numthreads)
{
	while ((m_maxhops < 0x10000) && (m_squeue.m_inactiveSize < m_squeue.m_failedConns + (numthreads << 2)))
	{
		int numr = 0;
//		CIntSet set;
		double sel = 0;
		int& maxtime = m_maxtimes[m_maxhops];
		int nw = (int)now();
//		if (maxtime >= nw) break;
		if (maxtime < nw)
		{
/* "SELECT url_id,site_id,next_index_time FROM urlword
WHERE deleted=0 AND next_index_time<=%d AND hops=%i AND next_index_time > %i %s
ORDER BY next_index_time", nw, m_maxhops, maxtime, Limit.c_str());
*/
			CSQLParam p;
			p.AddParam(&nw);
			p.AddParam(&m_maxhops);
			p.AddParam(&maxtime);
			p.AddParam(Limit.c_str());
			CSQLQuery *sqlquery = m_sqlquery->GetNextLinkHS(&p);

			numr = AddUrls(sqlquery, sel, maxtime, NULL);
/*			if (numr > 0)
			{
//"SELECT url_id,site_id,next_index_time
//FROM urlword WHERE deleted=0 AND hops=%i AND next_index_time=%d %s",m_maxhops, maxtime, Limit.c_str());
				CSQLParam p;
				p.AddParam(&m_maxhops);
				p.AddParam(&maxtime);
				p.AddParam(Limit.c_str());
				CSQLQuery *sqlquery = m_sqlquery->GetNextLinkHS1(&p);

				numr += AddUrls(sqlquery, sel, maxtime, set);
			}
			else*/
			{
				maxtime = nw;
			}
			if (numr && m_logFile)
			{
				fprintf(m_logFile, "Got next %6i URLs for: %7.3f seconds.Queued docs: %5lu. Hops: %i.%s\n",
numr, sel, m_squeue.m_qDocs, m_maxhops,m_available ? "" : "Not available!");
				fflush(m_logFile);
				m_got = 1;
			}
		}
		if (/*(numr || (m_squeue.m_qDocs == 0)) && */(m_maxhops < m_maxhopsa))
		{
			m_maxhops++;
		}
		else
		{
			break;
		}
/*
		else
		{
			if (m_maxhops < m_maxhopsa)
			{
				m_maxhops++;
			}
			else
			{
				break;
			}
		}*/
	}
	return m_squeue.ExtractURL();
}


void CSQLDatabaseI::InsertWordSite(const char* word, ULONG* sites, int size)
{
// "INSERT INTO wordsite(word, sites) VALUES('%s', '", word);
//	pquery = memcpyq(pquery, (char*)sites, size << 2);
	CSQLParam p;
	p.AddParam(word);
	p.AddParamEsc((char*)sites, size << 2);
	CSQLQuery *sqlquery = m_sqlquery->InsertWordSiteI(&p);
	sql_real_query(sqlquery);
}

void CSQLDatabaseI::UpdateWordSite(char* hostname, ULONG site_id)
{
	hash_map<CWord, ULONG> hmap;
	char* host = hostname;
	Tolower((unsigned char*)host, CHARSET_USASCII);
	ULONG bit = 1;
	ULONG bits = 32;
	while (host)
	{
		char* dot = strchr(host, '.');
		if (dot) *dot = 0;
		if ((dot ? dot - host : strlen(host)) <= MAX_WORD_LEN) hmap[host] |= bit;
		host = dot ? dot + 1 : NULL;
		bit <<= 1;
		bits--;
	}
	for (hash_map<CWord, ULONG>::iterator it = hmap.begin(); it != hmap.end(); it++)
	{
// "SELECT sites FROM wordsite WHERE word='%s'", it->first.Word());
		CSQLParam p;
		p.AddParam(it->first.Word());
		CSQLQuery *sqlquery = m_sqlquery->UpdateWordSiteS(&p);
		CSQLAnswer *answ = sql_query(sqlquery);
		ULONG* newSites;
		ULONG* pNew;
		if (answ && answ->FetchRow())
		{
			ULONG len = answ->GetLength(0) >> 2;
			newSites = new ULONG[len + 1];
			pNew = newSites;
			ULONG* pold = (ULONG*)answ->GetColumn(0);
			ULONG* eold = pold + len;
			while ((pold < eold) && ((*pold & 0xFFFFFF) < site_id))
			{
				*pNew++ = *pold++;
			}
			*pNew++ = site_id | ((it->second << bits) & 0xFF000000);
			if ((pold < eold) && ((*pold & 0xFFFFFF)== site_id))
			{
				pold++;
			}
			memcpy(pNew, pold, (eold - pold) << 2);
			pNew += (eold - pold);
// "DELETE FROM wordsite WHERE word = '%s'", it->first.Word());
			CSQLParam p;
			p.AddParam(it->first.Word());
			sqlquery = m_sqlquery->UpdateWordSiteD(&p);
			sql_query2(sqlquery);
		}
		else
		{
			newSites = new ULONG[1];
			newSites[0] = site_id | ((it->second << bits) & 0xFF000000);
			pNew = newSites + 1;
		}
		InsertWordSite(it->first.Word(), newSites, pNew - newSites);
		delete newSites;
		if (answ) delete answ;
	}
}

int CSQLDatabaseI::MarkForReindex()
{
//	sprintf(qUpd, "UPDATE urlword SET next_index_time = %d WHERE 1 = 1 %s", (int)now(), Limit.c_c_str());
	CSQLParam p;
	int tm = now();
	p.AddParam(&tm);
	p.AddParam(Limit.c_str());
	CSQLQuery *sqlquery = m_sqlquery->MarkForReindexU(&p);
	if (sql_queryw2(sqlquery)) return IND_ERROR;
	return IND_OK;
}

char* HTTPErrMsg(int code)
{
	switch(code)
	{
	case 0:   return("Not indexed yet");
	case 200: return("OK");
	case 204: return("No content");
	case 301: return("Moved Permanently");
	case 302: return("Moved Temporarily");
	case 303: return("See Other");
	case 304: return("Not Modified");
	case 300: return("Multiple Choices");
	case 305: return("Use Proxy (proxy redirect)");
	case 400: return("Bad Request");
	case 401: return("Unauthorized");
	case 402: return("Payment Required");
	case 403: return("Forbidden");
	case 404: return("Not found");
	case 405: return("Method Not Allowed");
	case 406: return("Not Acceptable");
	case 407: return("Proxy Authentication Required");
	case 408: return("Request Timeout");
	case 409: return("Conflict");
	case 410: return("Gone");
	case 411: return("Length Required");
	case 412: return("Precondition Failed");
	case 413: return("Request Entity Too Large");
	case 414: return("Request-URI Too Long");
	case 415: return("Unsupported Media Type");
	case 500: return("Internal Server Error");
	case 501: return("Not Implemented");
	case 502: return("Bad Gateway");
	case 505: return("Protocol Version Not Supported");
	case 503: return("Service Unavailable");
	case 504: return("Gateway Timeout");
	default:  return("Unknown status");
	}
}

void StatProc(int code, int expired, int total)
{
	if (code >= 0)
	{
		printf("%10d %10d %10d %s\n", code, expired, total, HTTPErrMsg(code));
	}
	else
	{
		printf("   -----------------------------\n");
		printf("%10s %10d %10d\n\n\n", "Total", expired, total);
	}
}

int CSQLDatabaseI::GetStatistics()
{
	int expired_total = 0;
	int total_total = 0;

	printf("\n ASPseek database statistics\n\n");
	printf("%10s %10s %10s\n", "Status", "Expired", "Total");
	printf("   -----------------------------\n");
// "SELECT status, sum(next_index_time<=%d), count(*) FROM urlword WHERE 1=1 %s
// GROUP BY status", (int)now(), Limit.c_str());
	CSQLParam p;
	int tm = now();
	p.AddParam(&tm);
	p.AddParam(Limit.c_str());
	CSQLQuery *sqlquery = m_sqlquery->GetStatisticsS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	while (answ && answ->FetchRow())
	{
		int status, sum, count;
		answ->GetColumn(0, status);
		answ->GetColumn(1, sum);
		answ->GetColumn(2, count);

		StatProc(status, sum, count);
		total_total += count;
		expired_total += sum;
	}
	StatProc(-1, expired_total, total_total);
	if (answ) delete answ;
	return(IND_OK);
}

void CSQLDatabaseI::GenerateWordSite()
{
	logger.log(CAT_ALL, L_INFO, "Generating word site .");
//  "SELECT site, site_id FROM sites order by site_id");
	CSQLQuery *sqlquery = m_sqlquery->GenerateWordSiteS();
	CSQLAnswer *answ = sql_query(sqlquery);
	if(!answ)
		return;
	hash_map<CWord, vector<ULONG> > words;
	hash_map<CWord, vector<ULONG> >::iterator wit;
	while (answ && answ->FetchRow())
	{
		CUrl url;
		hash_map<CWord, ULONG> hmap;
		ULONG site_id;
		answ->GetColumn(1, site_id);
		url.ParseURL(answ->GetColumn(0));
		char* host = url.m_hostinfo;
		Tolower((unsigned char*)host, CHARSET_USASCII);
		ULONG bit = 1;
		ULONG bits = 32;
		while (host)
		{
			char* dot = strchr(host, '.');
			if (dot) *dot = 0;
			if ((dot ? dot - host : strlen(host)) <= MAX_WORD_LEN) hmap[host] |= bit;
			host = dot ? dot + 1 : NULL;
			bit <<= 1;
			bits--;
		}
		for (hash_map<CWord, ULONG>::iterator it = hmap.begin(); it != hmap.end(); it++)
		{
			words[it->first].push_back(site_id | ((it->second << bits) & 0xFF000000));
		}
	}
	if (answ) delete answ;
	logger.log(CAT_ALL, L_INFO, ".");
// "SELECT word FROM wordsite");
	sqlquery = m_sqlquery->GenerateWordSiteS1();
	answ = sql_query(sqlquery);
	while (answ && answ->FetchRow())
	{
		if (strlen(answ->GetColumn(0)) <= MAX_WORD_LEN)
		{
			wit = words.find(answ->GetColumn(0));
// "DELETE FROM wordsite WHERE word = '%s'", row[0]);
			CSQLParam p;
			p.AddParam(answ->GetColumn(0));
			sqlquery = m_sqlquery->GenerateWordSiteD(&p);
			sql_query2(sqlquery);
			if (wit != words.end())
			{
				vector<ULONG>& sites = wit->second;
				InsertWordSite(answ->GetColumn(0), &(*sites.begin()), sites.size());
				words.erase(wit);
			}
		}
	}
	for (wit = words.begin(); wit != words.end(); wit++)
	{
		vector<ULONG>& sites = wit->second;
		InsertWordSite(wit->first.Word(), &(*sites.begin()), sites.size());
	}
	if (answ) delete answ;
	logger.log(CAT_ALL, L_INFO, ". done\n");
}

void CSQLDatabaseI::GetTmpUrls(CTmpUrlSource& tmpus)
{
// "SELECT url_id FROM tmpurl");
	CSQLQuery *sqlquery = m_sqlquery->GetTmpUrlsS();
	CSQLAnswer *answ = sql_query(sqlquery);
//Fixme:
//	tmpus.m_urls.reserve(answ->GetNumRows());
	while (answ && answ->FetchRow())
	{
		ULONG url_id;
		answ->GetColumn(0, url_id);
		tmpus.m_urls.push_back(url_id);
	}
	if (answ) delete answ;

}

int CSQLDatabaseI::GetUrlID(const char* url, CHref& href, int nit, ULONG& urlID, ULONG& lost)
{
	int ret = 0;
//	"SELECT url_id, deleted FROM urlword WHERE url='");
//	pq = strcpyq(pq, url);
//	strcpy(pq, "'");
	CSQLParam p;
	p.AddParamEsc(url);
	CSQLQuery *sqlquery = m_sqlquery->GetUrlIDS2(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	if (answ && answ->FetchRow())
	{
		int del;
		answ->GetColumn(0, urlID);
		answ->GetColumn(1, del);
		lost++;
		if (del != 0)
		{
// "UPDATE urlword SET hops=%d,referrer=%d,last_index_time=%ld,next_index_time=%d,status=0,deleted=0 WHERE url_id=%ld",
// href.m_hops, href.m_referrer, now(), nit, urlID);
			CSQLParam p;
			p.AddParam(&href.m_hops);
			p.AddParam(&href.m_referrer);
			int tm = now();
			p.AddParam(&tm);
			p.AddParam(&nit);
			p.AddParam(&urlID);
			sqlquery = m_sqlquery->GetUrlIDU(&p);
			sql_query2(sqlquery);
			ret = 1;
			DelMap.Clear(urlID);
		}
	}
	else
	{
		urlID = 0;
	}
	if (answ) delete answ;
	return ret;
}

ULONG CSQLDatabaseI::StoreHref2(const char* url, CHref& href, const char* server, int hopsord, ULONG& lost)
{
	ULONG site_id;
// "SELECT site_id FROM sites WHERE site='%s'", server);
	CSQLParam p;
	p.AddParam(server);
	CSQLQuery *sqlquery = m_sqlquery->GetSiteS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	if (answ && answ->FetchRow())
	{
		answ->GetColumn(0, site_id);
	}
	else
	{
// "INSERT INTO sites(site) VALUES('%s')", server);
		CSQLParam p;
		p.AddParam(server);
		sqlquery = m_sqlquery->StoreHref2I(&p);
		site_id = sql_query1(sqlquery);
	}
	if (answ) delete answ;
	int nit = now() - href.m_delta;
// "INSERT INTO urlword(site_id, hops, referrer, last_index_time, next_index_time, status, url) VALUES(%ld,%d,%d,%ld,%d,0,'",
// site_id, href.m_hops, href.m_referrer, now(), nit);
//	char* pq = strcpyq(qIns + strlen(qIns), url);
getNext:
	ULONG urlIDp = DelMapReuse.Get();
	ULONG urlID;
	int err;
	p.Clear();
	p.AddParam(&site_id);
	p.AddParam(&href.m_hops);
	p.AddParam(&href.m_referrer);
	int tm = now();
	p.AddParam(&tm);
	p.AddParam(&nit);
	p.AddParamEsc(url);
	if (urlIDp == 0)
	{
		sqlquery = m_sqlquery->StoreHref2I1(&p);
		urlID = sql_query1(sqlquery, &err);
	}
	else
	{
		p.AddParam(&urlIDp);
		sqlquery = m_sqlquery->StoreHref2I2(&p);
		sql_query1(sqlquery, &err);
		urlID = urlIDp;
	}

	int m = (err != DB_ER_DUP_ENTRY);
	if (m == 0)
	{
		m = GetUrlID(url, href, nit, urlID, lost);
		if (urlID == 0) goto getNext;
		if (urlID != urlIDp) DelMapReuse.Put(urlIDp);
	}
//	if ((err != DB_ER_DUP_ENTRY) || GetUrlID(url, href, nit, urlID, lost))
	if (m)
	{
		int* maxtime = hopsord ? &m_maxtimes[href.m_hops] : &m_maxtime;
		if (href.m_hops > m_maxhopsa)
		{
			m_maxhopsa = href.m_hops;
		}
		if (maxtime)
		{
			if ((*maxtime >= nit) || (hopsord && (href.m_hops < m_maxhops)))
			{
				if (SQLFilter && SQLFilter->Match(url))
				{
					CLocker lock(&m_squeue.m_mutex);
					m_squeue.AddURL(site_id, urlID, url);
				}
			}
			else
			{
				m_available = 1;
			}
		}
	}
	return urlID;
}

ULONG CSQLDatabaseI::StoreHref1(const char* url, CHref& href, const char* server, int hopsord, ULONG& lost)
{
	ULONG site_id;
// "SELECT site_id FROM sites WHERE site='%s'", server);
	CSQLParam p;
	p.AddParam(server);
	CSQLQuery *sqlquery = m_sqlquery->GetSiteS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);

	if (answ && answ->FetchRow())
	{
		answ->GetColumn(0, site_id);
	}
	else
	{
// "INSERT INTO sites(site) VALUES('%s')", server);
		CSQLParam p;
		p.AddParam(server);
		sqlquery = m_sqlquery->StoreHref1I(&p);
		site_id = sql_query1(sqlquery);
	}
	if (answ) delete answ;
	int nit = now() - href.m_delta;
// "INSERT INTO urlword(site_id, hops, referrer, last_index_time, next_index_time, status, url) VALUES(%ld,%d,%d,%ld,%d,0,'",
//site_id, href.m_hops, href.m_referrer, now(), nit);
//	char* pq = strcpyq(qIns + strlen(qIns), url);
getNext:
	ULONG urlIDp = DelMapReuse.Get();
	ULONG urlID;
	int err;
	p.Clear();
	p.AddParam(&site_id);
	p.AddParam(&href.m_hops);
	p.AddParam(&href.m_referrer);
	int tm = now();
	p.AddParam(&tm);
	p.AddParam(&nit);
	p.AddParamEsc(url);
	if (urlIDp == 0)
	{
		sqlquery = m_sqlquery->StoreHref1I1(&p);
		urlID = sql_query1(sqlquery, &err);
	}
	else
	{
		p.AddParam(&urlIDp);
		sqlquery = m_sqlquery->StoreHref1I2(&p);
		sql_query1(sqlquery, &err);
		urlID = urlIDp;
	}

	int b = (err != DB_ER_DUP_ENTRY);
	if (b == 0)
	{
		b = GetUrlID(url, href, nit, urlID, lost);
		if (urlID == 0) goto getNext;
		if (urlID != urlIDp) DelMapReuse.Put(urlIDp);
	}
	StoredHrefs.AddHref(url, err == DB_ER_DUP_ENTRY ? 1 : 0, urlID);
	if (b)
	{
		int* maxtime = hopsord ? &m_maxtimes[href.m_hops] : &m_maxtime;
		if (maxtime)
		{
			if (*maxtime >= nit)
			{
				if (m_logFile) fprintf(m_logFile, "Added URL: %lu\n", urlID);
				m_squeue.AddURL(site_id, urlID, url);
			}
			else
			{
				m_available = 1;
			}
			if (href.m_hops < m_maxhops) m_maxhops = href.m_hops;
		}
		return urlID;
	}
	return 0;
}

void CSQLDatabaseI::StoreHref(const char* url, CHref& href, const char* server, int* maxtime)
{
	ULONG site_id;
// "SELECT site_id FROM sites WHERE site='%s'", server);
	CSQLParam p;
	p.AddParam(server);
	CSQLQuery *sqlquery = m_sqlquery->GetSiteS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);

	if (answ && answ->FetchRow())
	{
		answ->GetColumn(0, site_id);
	}
	else
	{
// "INSERT INTO sites(site) VALUES('%s')", server);
		CSQLParam p;
		p.AddParam(server);
		sqlquery = m_sqlquery->StoreHrefI(&p);
		site_id = sql_query1(sqlquery);
	}
	if (answ) delete answ;
	int nit = now() - href.m_delta;
//"INSERT INTO urlword(site_id, hops, referrer, last_index_time, next_index_time, status, url) VALUES(%ld,%d,%d,%ld,%d,0,'",
//site_id, href.m_hops, href.m_referrer, now(), nit);
//	char* pq = strcpyq(qIns + strlen(qIns), url);
	int err;
	p.Clear();
	p.AddParam(&site_id);
	p.AddParam(&href.m_hops);
	p.AddParam(&href.m_referrer);
	int tm = now();
	p.AddParam(&tm);
	p.AddParam(&nit);
	p.AddParamEsc(url);
	sqlquery = m_sqlquery->StoreHrefI1(&p);
	ULONG urlID = sql_query1(sqlquery, &err);
	StoredHrefs.AddHref(url, err == DB_ER_DUP_ENTRY ? 1 : 0, urlID);
	if (err != DB_ER_DUP_ENTRY)
	{
		if (maxtime)
		{
			if (*maxtime >= nit)
			{
				if (m_logFile) fprintf(m_logFile, "Added URL: %lu\n", urlID);
				m_squeue.AddURL(site_id, urlID, url);
			}
			else
			{
				m_available = 1;
			}
			if (href.m_hops < m_maxhops) m_maxhops = href.m_hops;
		}
	}
}

void CSQLDatabaseI::StoreHrefs(int hopsord)
{
	CStringToHrefMap::iterator it;
	for (it = Hrefs.begin(); it != Hrefs.end(); it++)
	{
		CHref& href = it->second;
		ULONG lost = 0;
		ULONG urlID = StoreHref1(it->first.c_str(), href, href.m_server.c_str(), hopsord, lost);
		if (urlID) href.m_urlID = urlID;
	}
	Hrefs.clear();
}

void CSQLDatabaseI::SaveLink(const CUrlRank& rank)
{
// "DELETE FROM citation WHERE url_id = %lu", rank.m_urlID);
	CSQLParam p;
	p.AddParam(&rank.m_urlID);
	CSQLQuery *sqlquery = m_sqlquery->SaveLinkD(&p);
	sql_query2(sqlquery);
	ULONG refcount = rank.m_refend - rank.m_referrers;
	if (refcount > 0)
	{
		CUrlRankPtrS* rankp = new CUrlRankPtrS[refcount];
		CUrlRankPtrS* prank = rankp;
		CUrlRankPtrS* erank;
		for (CUrlRank** r = rank.m_referrers; r < rank.m_refend; r++, prank++)
		{
			prank->m_rank = *r;
		}
		erank = prank;

		sort<CUrlRankPtrS*>(rankp, erank);
		ULONG* sitebuf = new ULONG[(refcount << 1) + 4];
		ULONG* urlbuf = new ULONG[refcount];
		ULONG* ps = sitebuf;
		ULONG* pu = urlbuf;
		prank = rankp;
		while (prank < erank)
		{
			ULONG site_id = prank->m_rank->m_siteID;
			*ps++ = (BYTE*)pu - (BYTE*)urlbuf;
			*ps++ = site_id;
			while ((prank < erank) && (site_id == prank->m_rank->m_siteID))
			{
				*pu++ = prank->m_rank->m_urlID;
				prank++;
			}
		}
		*ps++ = (BYTE*)pu - (BYTE*)urlbuf;
		ULONG* es = ps;
		ULONG d = (BYTE*)ps - (BYTE*)sitebuf;
		for (ps = sitebuf; ps < es; ps += 2)
		{
			*ps += d;
		}
//  "INSERT INTO citation(url_id, referrers) VALUES(%lu, '", rank.m_urlID);
// pquery = memcpyq(pquery, (char*)sitebuf, (BYTE*)es - (BYTE*)sitebuf);
// pquery = memcpyq(pquery, (char*)urlbuf, (BYTE*)pu - (BYTE*)urlbuf);
		p.Clear();
		p.AddParam(&rank.m_urlID);
		char *buf = new char[((BYTE*)es - (BYTE*)sitebuf) + ((BYTE*)pu - (BYTE*)urlbuf)];
		memcpy(buf, (char*)sitebuf, (BYTE*)es - (BYTE*)sitebuf);
		memcpy(buf + ((BYTE*)es - (BYTE*)sitebuf), (char*)urlbuf, (BYTE*)pu - (BYTE*)urlbuf);

		p.AddParamEsc((char*)buf, ((BYTE*)es - (BYTE*)sitebuf) + ((BYTE*)pu - (BYTE*)urlbuf));

//		p.AddParamEsc((char*)sitebuf, (BYTE*)es - (BYTE*)sitebuf);
//		p.AddParamEsc((char*)urlbuf, (BYTE*)pu - (BYTE*)urlbuf, 1);
		sqlquery = m_sqlquery->SaveLinkI(&p);
		sql_real_query(sqlquery);
		delete buf;
		delete sitebuf;
		delete urlbuf;
		delete rankp;
	}
}

void CSQLDatabaseI::SaveLinks(CUrlRanks& ranks)
{
	logger.log(CAT_ALL, L_INFO, "Saving citation    [");
	size_t s = ranks.size();
	int n = s / 50 + 1; // we want about 50 ....s
	int i = 0;
	for (CUrlRanks::iterator it = ranks.begin(); it != ranks.end(); it++)
	{
		SaveLink(*it);
		if (!(i++ % n))
			logger.log(CAT_ALL, L_INFO, ".");
	}
	logger.log(CAT_ALL, L_INFO, "] done.\n");
}

void CSQLDatabaseI::CalcTotal()
{
	ULONG total = 0;
	for (int i = 0; i < 16; i++)
	{
		char buf[3];
// "SELECT count(*) FROM urlwords%02i", i);
		sprintf(buf, "%02i", i);
		CSQLParam p;
		p.AddParam(buf);
		CSQLQuery *sqlquery = m_sqlquery->CalcTotalS(&p);
		CSQLAnswer *answ = sql_query(sqlquery);
;
		if (answ && answ->FetchRow())
		{
			ULONG total_t;
			answ->GetColumn(0, total_t);
			total += total_t;
		}
		if (answ) delete answ;
	}

	string fntotal = DBWordDir + "/total";
	FILE* f = fopen(fntotal.c_str(), "w");
	if (f)
	{
		fprintf(f, "%lu", total);
		fclose(f);
	}
	else
	{
		logger.log(CAT_ALL, L_ERR, "Couldn't open file: %s\n", fntotal.c_str());
	}
}

void CSQLDatabaseI::LoadRanks(CUrlRanks& ranks)
{
	logger.log(CAT_ALL, L_INFO, "Loading ranks      [");
	ranks.Load();
//	ULONG j = 0;
	ULONG maxUrlId = 0;
// ("SELECT MAX(url_id) FROM urlword");
	CSQLQuery *sqlquery = m_sqlquery->LoadRanksS();
	CSQLAnswer *answ = sql_query(sqlquery);

	if (answ && answ->FetchRow())
	{
		answ->GetColumn(0, maxUrlId);
//		logger.log(CAT_ALL, L_ERR, "\nmaxUrlId = %lu\n", maxUrlId);
	}
	if (answ) delete answ;

// "SELECT url_id, redir, site_id, last_modified FROM urlword WHERE status!=0");
	sqlquery = m_sqlquery->LoadRanksS1();
	answ = sql_query(sqlquery);
	if (answ)
	{
//		ULONG num_res = answ->GetNumRows();
//		int n = num_res / 18 + 1; // we want 18 ...s
		CMapULONGToULONG& redir = ranks.m_redir;
		time_t* lmbuf = new time_t[maxUrlId + 1];
		memset((void *)lmbuf, 0, (maxUrlId + 1) * sizeof(time_t));
		time_t t, maxt = 0;
		time_t t_limit = now() + 60 * 60 * 24; // now + 1 day
		while (answ->FetchRow())
		{
			ULONG from, to;
			answ->GetColumn(0, from);
			if (!answ->GetColumn(1, to))
			{
				to = 0;
			}
			t = httpDate2time_t(answ->GetColumn(3));
			if (t != BAD_DATE)
			{
				if (t > t_limit)
					t = t_limit;
				lmbuf[from] = t;
				if (t > maxt)
					maxt = t;
			}
			ULONG f_siteID;
			if (!answ->GetColumn(2, f_siteID))
			{
				f_siteID = 0;
			}
			ranks[from].m_siteID = f_siteID;
//			if (!(j++ % n))
//				logger.log(CAT_ALL, L_INFO, ".");
			if (to != 0)
			{
				ULONG t = to;
				int r = 1;
				CULONGSet set;
				while (true)
				{
					CMapULONGToULONG::iterator it = redir.find(t);
					if (it == redir.end()) break;
					ULONG t1 = it->second;
					if ((t1 == from) || (set.find(t1) != set.end()))
					{
						r = 0;
						break;
					}
					set.insert(t1);
					t = t1;
				}
				if (r) redir[from] = to;
			}
		}
		// Save lastmod file
		CLastMods lm;
		lm.Init(DBWordDir.c_str());
		lm.reserve(maxUrlId + 1);
		lm.CalcOurEpoch(maxt);
		lm.ReadTimesFromBuf(lmbuf, maxUrlId);
		lm.Save();
		delete lmbuf;
		if (answ) delete answ;
	}
	for (int i = 0; i < 16; i++)
	{
		logger.log(CAT_ALL, L_INFO, "."); // more 16 ...s
		char buf[3];
// "SELECT url_id, hrefs FROM urlwords%02i WHERE length(hrefs) > 0", i);
		sprintf(buf, "%02i", i);
		CSQLParam p;
		p.AddParam(buf);
		CSQLQuery *sqlquery = m_sqlquery->LoadRanksS2(&p);
		answ = sql_query(sqlquery);
		while (answ && answ->FetchRow())
		{
			ULONG len = answ->GetLength(1);
			if (len > 0)
			{
//				ULONG from = atoi(row[0]);
//				CUrlRank& rank = ranks[from];
				ULONG* to = (ULONG*)answ->GetColumn(1);
				for (int j = len >> 2; j > 0; j--)
				{
					ULONG r = ranks.Redir(*to++);
					if (r == 0)
					{
						logger.log(CAT_ALL, L_ERR, "ERROR in %s:%d: Refferring to 0\n", __FILE__, __LINE__);
					}
					ranks.IncRefCount(r);
				}
			}

		}
		if (answ) delete answ;
	}


	ranks.Allocate();
	for (int i = 0; i < 16; i++)
	{
		char buf[3];
		logger.log(CAT_ALL, L_INFO, "."); // more 16 ...s
// "SELECT url_id, hrefs FROM urlwords%02i WHERE length(hrefs) > 0", i);
		sprintf(buf, "%02i", i);
		CSQLParam p;
		p.AddParam(buf);
		sqlquery = m_sqlquery->LoadRanksS2(&p);
		answ = sql_query(sqlquery);

		if (!answ)
			return;
//		answ->SeekRow(0);
		while (answ && answ->FetchRow())
		{
			ULONG len = answ->GetLength(1);
			if (len > 0)
			{
				ULONG from;
				answ->GetColumn(0, from);
				ULONG* to = (ULONG*)answ->GetColumn(1);
				CUrlRanks::iterator ifr = ranks.find(from);
				if (ifr != ranks.end())
				{
					CULONGSet set;
					for (int j = len >> 2; j > 0; j--)
					{
						ULONG r = ranks.Redir(*to++);
						set.insert(r);
					}
					for (CULONGSet::iterator it = set.begin(); it != set.end(); it++)
					{
						CUrlRanks::iterator ito = ranks.find(*it);
						if (ito != ranks.end())
						{
							ranks.AddLinkFromTo(const_cast<CUrlRank&>(*ifr), const_cast<CUrlRank&>(*ito));
						}
					}
				}
			}
		}
		if (answ) delete answ;
	}
	logger.log(CAT_ALL, L_INFO, "] done.\n");
}

void CSQLDatabaseI::LoadSites()
{
// "SELECT site from sites");
	CSQLQuery *sqlquery = m_sqlquery->LoadSitesS();
	CSQLAnswer *answ = sql_query(sqlquery);
	while (answ && answ->FetchRow())
	{
		Sites.insert(answ->GetColumn(0));
	}
	if (answ) delete answ;
}

void CSQLDatabaseI::FindMaxHops()
{
//  "SELECT max(hops) from urlword where 1=1 %s", Limit.c_str());
	CSQLParam p;
//	p.AddParam(Limit.c_str());
	p.AddParam("");
	CSQLQuery *sqlquery = m_sqlquery->FindMaxHopsS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	if (answ && answ->FetchRow() && answ->GetColumn(0))
	{
		answ->GetColumn(0, m_maxhopsa);
	}
	if (answ) delete answ;
}

int CSQLDatabaseI::Startup()
{
	LoadRobots();
	LoadSites();
	FindMaxHops();
//	SaveCountriesIfNotEmpty(this);
//	LoadCountriesIfEmpty(this);
	return 1;
}

void CSQLDatabaseI::LoadRobots()
{
	Robots.clear();
// "SELECT hostinfo,path FROM robots");
	CSQLQuery *sqlquery = m_sqlquery->LoadRobotsS();
	CSQLAnswer *answ = sql_query(sqlquery);
	while (answ && answ->FetchRow())
	{
		Robots[answ->GetColumn(0)].push_back(answ->GetColumn(1));
	}
	if (answ) delete answ;
}

void CSQLDatabaseI::LoadRobotsOfHost(char* hostinfo)
{
	if ((strlen(hostinfo) <= MAX_URL_LEN))
	{
//  "SELECT path FROM robots WHERE hostinfo = '");
//		pquery = strcpyq(pquery, hostinfo);

		CStringVector& robots = Robots[hostinfo];
		CSQLParam p;
		p.AddParamEsc(hostinfo);
		CSQLQuery *sqlquery = m_sqlquery->LoadRobotsOfHostS(&p);
		CSQLAnswer *answ = sql_query(sqlquery);
		robots.clear();
		while (answ && answ->FetchRow())
		{
			robots.push_back(answ->GetColumn(0));
		}
		if (answ) delete answ;
	}
}

void CSQLDatabaseI::DeleteRobotsFromHost(char *host)
{
	if (strlen(host) <= MAX_URL_LEN)
	{
//  "DELETE FROM robots WHERE hostinfo='");
//		pquery = strcpyq(pquery, host);
		CSQLParam p;
		p.AddParamEsc(host);
		CSQLQuery *sqlquery = m_sqlquery->DeleteRobotsFromHostD(&p);
		sql_query2(sqlquery);
	}
}

void CSQLDatabaseI::AddRobotsToHost(char *host, char *s)
{
	if ((strlen(host) <= MAX_URL_LEN) && (strlen(s) <= MAX_URL_LEN))
	{
// "INSERT INTO robots (hostinfo,path) VALUES ('");
//		pquery = strcpyq(pquery, host);
//		pquery += sprintf(pquery, "', '");
//		pquery = strcpyq(pquery, s);

		CSQLParam p;
		p.AddParamEsc(host);
		p.AddParamEsc(s);
		CSQLQuery *sqlquery = m_sqlquery->AddRobotsToHostI(&p);
		sql_query2(sqlquery);
	}
}

void CSQLDatabaseI::DeleteUrlFromSpace(const char* curl, ULONG space)
{
	CUrl url;
	if ((strlen(curl) <= MAX_URL_LEN) && (url.ParseURL(curl) == 0))
	{
//"lock tables spaces write, sites read");
		CSQLQuery *sqlquery = m_sqlquery->DeleteUrlFromSpaceL();
		sql_query2(sqlquery);
// "SELECT site_id FROM sites WHERE site='%s://", url.m_schema);
//		pq = strcpyq(pq, url.m_hostinfo);
//		strcpy(pq, "/'");
		char urlp[(MAX_URL_LEN + 1) << 1];
		sprintf(urlp, "%s://%s/", url.m_schema, url.m_hostinfo);
		CSQLParam p;
		p.AddParamEsc(urlp);
		sqlquery = m_sqlquery->DeleteUrlFromSpaceS(&p);
		CSQLAnswer *answ = sql_query(sqlquery);

		if (answ && answ->FetchRow())
		{
// "SELECT site_id FROM spaces WHERE site_id=%s and space_id=%lu", row[0], space);
			CSQLParam p;
			p.AddParam(answ->GetColumn(0));
			p.AddParam(&space);
			sqlquery = m_sqlquery->DeleteUrlFromSpaceS1(&p);

			CSQLAnswer *answ1 = sql_query(sqlquery);
			if (answ1 && answ1->FetchRow())
			{
//  "DELETE FROM spaces WHERE site_id=%s AND space_id=%lu", row[0], space);
				CSQLParam p;
				p.AddParam(answ->GetColumn(0));
				p.AddParam(&space);
				sqlquery = m_sqlquery->DeleteUrlFromSpaceD(&p);
				sql_query2(sqlquery);

				char fname[1000];
				sprintf(fname, "%s/subsets/space%lu", DBWordDir.c_str(), space);
				FILE* f = fopen(fname, "r+");
				if (f)
				{
					ULONG site_id;
					answ->GetColumn(0, site_id);
					ULONG nmins = site_id >> 5;
					ULONG mins;
					fread(&mins, 1, sizeof(mins), f);
					if (nmins >= mins)
					{
						ULONG off = (nmins - mins + 1) << 2;
						fseek(f, off, SEEK_SET);
						long noff = ftell(f);
						if ((ULONG)noff == off)
						{
							ULONG elem;
							fread(&elem, 1, sizeof(elem), f);
							elem &= ~(1 << (site_id & 0x1F));
							fseek(f, off, SEEK_SET);
							fwrite(&elem, 1, sizeof(elem), f);
						}
					}
					fclose(f);
					printf("Status: OK\n");
				}
			}
			else
			{
				printf("Status: Warning. Site doesn't belongs to space\n");
			}
			if (answ1) delete answ1;
		}
		else
		{
			printf("Status: Error. Site not found\n");
		}
		sqlquery = m_sqlquery->UnlockTables();
		sql_query2(sqlquery);
		if (answ) delete answ;
	}
}

void CSQLDatabaseI::AddUrlToSpace(const char* curl, ULONG space)
{
	CUrl url;
	if ((strlen(curl) <= MAX_URL_LEN) && (url.ParseURL(curl) == 0))
	{
// "lock tables spaces write, sites read");
		CSQLQuery *sqlquery = m_sqlquery->AddUrlToSpaceL();
		sql_query2(sqlquery);
/* "SELECT site_id FROM sites WHERE site='%s://", url.m_schema);
		pq = strcpyq(pq, url.m_hostinfo);
		strcpy(pq, "/'");
*/
		char urlp[(MAX_URL_LEN + 1)  << 1];
		sprintf(urlp, "%s://%s/", url.m_schema, url.m_hostinfo);
		CSQLParam p;
		p.AddParamEsc(urlp);
		sqlquery = m_sqlquery->AddUrlToSpaceS(&p);
		CSQLAnswer *answ = sql_query(sqlquery);

		if (answ && answ->FetchRow())
		{
// "SELECT site_id FROM spaces WHERE site_id=%s and space_id=%lu", row[0], space);
			CSQLParam p;
			p.AddParam(answ->GetColumn(0));
			p.AddParam(&space);
			sqlquery = m_sqlquery->AddUrlToSpaceS1(&p);
			CSQLAnswer *answ1 = sql_query(sqlquery);
			if (answ1 && answ1->FetchRow())
			{
				logger.log(CAT_ALL, L_ERR, "Status: Warning. Site already belongs to space\n");
			}
			else
			{
				ULONG site_id;
				answ->GetColumn(0, site_id);
// "INSERT INTO spaces(site_id,space_id) VALUES (%s, %lu)", row[0], space);
				CSQLParam p;
				p.AddParam(answ->GetColumn(0));
				p.AddParam(&space);
				sqlquery = m_sqlquery->AddUrlToSpaceI(&p);
				sql_query2(sqlquery);
				char fname1[1000], fname[1000];
				sprintf(fname1, "%s/subsets/space%lu.1", DBWordDir.c_str(), space);
				sprintf(fname, "%s/subsets/space%lu", DBWordDir.c_str(), space);
				FILE* f = fopen(fname, "r");
				FILE* fo = fopen(fname1, "w");
				if (fo)
				{
					ULONG mins;
					ULONG nmins = site_id >> 5;
					ULONG elem;
					if (f)
					{
						fread(&mins, 1, sizeof(mins), f);
						if (nmins < mins)
						{
							ULONG z = 0;
							fwrite(&nmins, 1, sizeof(nmins), fo);
							elem = 1 << (site_id & 0x1F);
							fwrite(&elem, 1, sizeof(elem), fo);
							nmins++;
							while (nmins < mins)
							{
								fwrite(&z, 1, sizeof(z), fo);
								nmins++;
							}
						}
						else
						{
							fwrite(&mins, 1, sizeof(mins), fo);
							while (mins < nmins)
							{
								elem = 0;
								fread(&elem, 1, sizeof(elem), f);
								fwrite(&elem, 1, sizeof(elem), fo);
								mins++;
							}
							elem = 0;
							fread(&elem, 1, sizeof(elem), f);
							elem |= (1 << (site_id & 0x1F));
							fwrite(&elem, 1, sizeof(elem), fo);
						}
						while (fread(&elem, 1, sizeof(elem), f) > 0)
						{
							fwrite(&elem, 1, sizeof(elem), fo);
						}
						fclose(f);
					}
					else
					{
						fwrite(&nmins, 1, sizeof(nmins), fo);
						elem = 1 << (site_id & 0x1F);
						fwrite(&elem, 1, sizeof(elem), fo);
					}
					fclose(fo);
					unlink(fname);
					rename(fname1, fname);
					printf("Status: OK\n");
				}
			}
			if (answ1) delete answ1;
		}
		else
		{
			printf("Status: Error. Site not found\n");
		}
		sqlquery = m_sqlquery->UnlockTables();
		sql_query2(sqlquery);
		if (answ) delete answ;
	}
}

void CSQLDatabaseI::AddCountry(const char* country, ULONG addr, ULONG mask)
{
//  "INSERT INTO countries(country, addr, mask, expires) VALUES('%s', %lu, %lu, %li)",
// country, addr, mask, now() + 7 * 64800);
	CSQLParam p;
	p.AddParam(country);
	p.AddParam(&addr);
	p.AddParam(&mask);
	int tm = (int)now() + 7 * 64800;
	p.AddParam(&tm);
	CSQLQuery *sqlquery = m_sqlquery->AddCountryI(&p);
	sql_query2(sqlquery);
}

void CSQLDatabaseI::LoadCountries(CIPTree* tree)
{
// "SELECT country, addr, mask, expires FROM countries");
	CSQLQuery *sqlquery = m_sqlquery->LoadCountriesS();
	CSQLAnswer *answ = sql_query(sqlquery);
	while (answ && answ->FetchRow())
	{
		ULONG addr, mask, expires;
		sscanf(answ->GetColumn(1), "%lu", &addr);
		sscanf(answ->GetColumn(2), "%lu", &mask);
		sscanf(answ->GetColumn(3), "%lu", &expires);
		tree->AddCountry(addr, mask, answ->GetColumn(0), expires);
	}
	if (answ) delete answ;

}

void CSQLDatabaseI::ReplaceCountry(ULONG istart, const char* country)
{
//	sprintf(query, "UPDATE countries SET country = '%s', expires = %li WHERE addr = %lu", country, now() + COUNTRY_EXPIRATION, istart);
	CSQLParam p;
	p.AddParam(country);
	int tm = now() + COUNTRY_EXPIRATION;
	p.AddParam(&tm);
	p.AddParam(&istart);
	CSQLQuery *sqlquery = m_sqlquery->ReplaceCountryU(&p);
	sql_query2(sqlquery);
}
void CSQLDatabaseI::NarrowCountry(ULONG istart, ULONG mask, int off)
{
//	sprintf(query, "UPDATE countries SET addr=%lu, mask=%lu WHERE addr=%lu", istart + (off ? mask : 0), ~((mask | (mask - 1)) >> 1), istart);
	CSQLParam p;
	ULONG istart1 = istart + (off ? mask : 0);
	p.AddParam(&istart1);
	ULONG istart2 = ~((mask | (mask - 1)) >> 1);
	p.AddParam(&istart2);
	p.AddParam(&istart);
	CSQLQuery *sqlquery = m_sqlquery->NarrowCountryU(&p);
	sql_query2(sqlquery);
}

void CheckHref(ULONG url, CBufferedFile& citations, ULONG prevOffset, ULONG endOffset, ULONG clen, char* refs)
{
	ULONG size = endOffset - prevOffset;
	int eq = 0;
	if (size == clen)
	{
		char* hr = (char*)alloca(size);
//		fseek(citations, prevOffset, SEEK_SET);
//		fread(hr, 1, size, citations);
		citations.Seek(prevOffset, SEEK_SET);
		citations.Read(hr, size);
		eq = memcmp(hr, refs, size) == 0;
	}
	if ((size != clen) || (eq == 0))
	{
		printf("Wrong citations for URL: %lu\n", url);
	}
}
void CSQLDatabaseI::CheckHrefDeltas()
{
	CSQLQuery* query = m_sqlquery->SetQuery("SELECT url_id, referrers FROM citation ORDER by mod(url_id, 4), url_id");
	CSQLAnswer *answ = sql_query(query);
	if (answ)
	{
		printf("Checking citations...\n");
		int availc = answ->FetchRow();
		while (availc)
		{
			char fname[1000], fnamec[1000];
			ULONG urlID;
			answ->GetColumn(0, urlID);
			ULONG f = urlID % NUM_HREF_DELTAS;
			CCitationFile file(f, 0, fname, fnamec);
			ULONG url, endOffset, prevOffset = 0;
			int avail = file.GetNextUrl(url, endOffset);
			while (availc && (f == (urlID % NUM_HREF_DELTAS)))
			{
				if ((avail == 0) || (url > urlID))
				{
					printf("URL %lu is absent\n", urlID);
					availc = answ->FetchRow();
					if (availc) answ->GetColumn(0, urlID);
				}
				else if (url < urlID)
				{
					if (endOffset > prevOffset)
					{
						printf("Extra URL %lu\n", url);
					}
					prevOffset = endOffset;
					avail = file.GetNextUrl(url, endOffset);
				}
				else
				{
					CheckHref(url, file.m_citations, prevOffset, endOffset, answ->GetLength(1), answ->GetColumn(1));
					availc = answ->FetchRow();
					if (availc) answ->GetColumn(0, urlID);
					prevOffset = endOffset;
					avail = file.GetNextUrl(url, endOffset);
				}
			}
		}
		delete answ;
	}
}

void CSQLDatabaseI::BuildSiteURLs()
{
	FILE* file;
	char fname[1000];
	CMapULONGToULONG counts;
	ULONG maxSite = 0;
	logger.log(CAT_ALL, L_INFO, "Generating Site URLs .");
	CSQLQuery *sqlquery = m_sqlquery->SiteURLsS();
	CSQLAnswer *answ = sql_query(sqlquery);
	while (answ && answ->FetchRow())
	{
		ULONG site_id;
		answ->GetColumn(0, site_id);
		ULONG* count = &counts[site_id];
		answ->GetColumn(1, *count);
		if (site_id > maxSite) maxSite = site_id;
	}
	if (answ) delete answ;
	logger.log(CAT_ALL, L_INFO, ".");
	if (!maxSite)   // Nothing to do...
	{
		logger.log(CAT_ALL, L_INFO, ". done\n");
		return;
	}
	sprintf(fname, "%s/siteurls", DBWordDir.c_str());
	file = fopen(fname, "r+");
	if (file == NULL)
	{
		file = fopen(fname, "w+");
	}
	if (file)
	{
		ULONG* site_urls = new ULONG[maxSite];
		memset(site_urls, 0, maxSite * sizeof(ULONG));
		CMapULONGToULONG::iterator ite = counts.end();
		for (CMapULONGToULONG::iterator it = counts.begin(); it != ite; it++)
		{
			site_urls[it->first - 1] = it->second;
		}
		fseek(file, 0, SEEK_SET);
		fwrite(site_urls, sizeof(ULONG), maxSite, file);
		fclose(file);
		delete site_urls;
	}
	logger.log(CAT_ALL, L_INFO, ". done\n");
}

void CSQLDatabaseI::MarkDeleted(ULONG urlID, int val)
{
	char uwt[20];
// "UPDATE urlword SET deleted = 1 WHERE url_id=%lu", urlID);
	CSQLParam p;
	p.AddParam("urlword");
	p.AddParam(&val);
	p.AddParam(&urlID);
	CSQLQuery *sqlquery = m_sqlquery->MarkDeletedU(&p);
	sql_query2(sqlquery);

// "UPDATE %s SET deleted = 1 WHERE url_id=%lu", UWTable(uwt, urlID), urlID);
	p.Clear();
	p.AddParam(UWTable(uwt, urlID));
	p.AddParam(&val);
	p.AddParam(&urlID);
	sqlquery = m_sqlquery->MarkDeletedU(&p);
	sql_query2(sqlquery);
	DelMap.Set(urlID);
}

ULONG CSQLDatabaseI::DeleteUrls(CWordCache* wcache, const char *limit)
{
// SELECT url_id, site_id, url, crc, origin FROM urlword WHERE url LIKE '%s';
#ifdef OPTIMIZE_CLONE
	ULONG ucnt = 0;
	CSQLParam p;
	p.AddParam(limit);
	CSQLQuery* sqlquery = m_sqlquery->DeleteUrlsS(&p);
	CSQLAnswer *answ = sql_query(sqlquery);
	while (answ && answ->FetchRow())
	{
		ucnt += 1;
		ULONG urlID, siteID, originID;
		char *crc, *url;
		int status, new_url;
		CUrl curl;

		answ->GetColumn(0, urlID);
		answ->GetColumn(1, siteID);
		url = answ->GetColumn(2);
		crc = answ->GetColumn(3);
		answ->GetColumn(4, originID);
		answ->GetColumn(5, status);
		new_url = (status == 0);

		curl.ParseURL(url);
		CServer* CurSrv = curl.FindServer();

		if (originID == 1)
		{
			CUrlContent ucontent;
			ucontent.m_cache = wcache;
			ucontent.m_url.m_urlID = urlID;
			ucontent.m_url.m_siteID = siteID;
			ucontent.m_url.m_crc = crc;

			curl.SetNewOrigin(crc, &ucontent, CurSrv, wcache);
		}

		wcache->DeleteWordsFromURL(urlID, siteID, CurSrv);

		wcache->MarkDeleted(urlID, siteID, url, new_url, 2);

		logger.log(CAT_ALL, L_ERR, "Delete url: %s\n", url);
	}
	if (answ) delete answ;
	return ucnt;
#else
	logger.log(CAT_ALL, L_ERR, "Deletion urls limit work only with --enable-fast-clones\n");
	return 0;
#endif
}

void CSQLDatabaseI::UpdateOrigin(ULONG urlID)
{
	CSQLParam p;
	p.AddParam(&urlID);
	CSQLQuery* query = m_sqlquery->UpdateOrigin(&p);
	sql_query2(query);
}

int Compress(BYTE* contents, ULONG size, unsigned long int & csize, BYTE*& compressed)
{
	csize = size + (size / 1000) + 12;
	compressed = new BYTE[csize];
	return compress2(compressed, &csize, contents, size, 5);
}

void CSQLDatabaseI::SaveContents(CUrlParam* content, ULONG wordCount,
	int changed, ULONG* hrefs, int hsz, double& sqlUpdate)
{
	unsigned long int csize = 0;
	BYTE* compressed = NULL;
	BYTE* compressed1 = NULL;
	unsigned long int size = content->m_size;
	char *buf;
	if (changed)
	{
		if ((Compress((BYTE*)content->m_content, size, csize, compressed1) != Z_OK) || (csize > size))
		{
			csize = content->m_size;
			compressed = (BYTE*)content->m_content;
			size = 0xFFFFFFFF;
		}
		else
		{
			compressed = compressed1;
		}
	}
	if (changed || (content->m_changed & (2 | 8)))
	{
		char uwt[20];
		UWTable(uwt, content->m_urlID);
		if (changed || (content->m_changed & 8))
		{
//			sprintf(query, "DELETE FROM %s WHERE url_id=%lu", uwt, content->m_urlID);
			CSQLParam p;
			p.AddParam(uwt);
			p.AddParam(&content->m_urlID);
			CSQLQuery *sqlquery = m_sqlquery->SaveContentsD(&p);
			sql_query2(sqlquery);

/* "INSERT INTO %s(url_id, wordcount, totalcount, docsize, lang, content_type, charset, words, \
hrefs, title, txt, keywords, description, deleted) VALUES(%lu, %lu, %lu, %u, '%s', '",
uwt, content->m_urlID, wordCount, content->m_totalCount, content->m_docsize, content->m_lang.c_str());
			pquery = strcpyq(pquery, content->m_content_type.c_str());
			STRCPY(pquery, "','");
			pquery = strcpyq(pquery, content->m_charset.c_str());
			STRCPY(pquery, "','");
			pquery = memcpyq(pquery, (char*)&size, sizeof(size));
			pquery = memcpyq(pquery, (char*)compressed, csize);
			STRCPY(pquery, "','");
			if (hrefs)
			{
				pquery = memcpyq(pquery, (char*)hrefs, hsz << 2);
			}
			STRCPY(pquery, "','");
			pquery = strcpyq(pquery, content->m_title.c_str());
			STRCPY(pquery, "','");
			pquery = strcpyq(pquery, content->m_txt.c_str());
			STRCPY(pquery, "','");
			pquery = strcpyq(pquery, content->m_keywords.c_str());
			STRCPY(pquery, "','");
			pquery = strcpyq(pquery, content->m_description.c_str());
			STRCPY(pquery, "')");
*/
			p.Clear();
			p.AddParam(uwt);
			p.AddParam(&content->m_urlID);
			p.AddParam(&wordCount);
			p.AddParam(&content->m_totalCount);
			p.AddParam(&content->m_docsize);
			p.AddParam(content->m_lang.c_str());
			p.AddParamEsc(content->m_content_type.c_str());
			p.AddParamEsc(content->m_charset.c_str());
/*			if (content->m_changed & 8)
			{
				buf = (char*)alloca(sizeof(ULONG));
				*(ULONG*)buf = 0xFFFFFFFF;
				p.AddParamEsc(buf, sizeof(ULONG));
			}
			else*/
			{
				if (csize >= 20000)
				{
					buf = new char[sizeof(size) + csize];
				}
				else
				{
					buf = (char*)alloca(sizeof(size) + csize);
				}

				memcpy(buf, (char*)&size, sizeof(size));
				memcpy(buf+sizeof(size), (char*)compressed, csize);
				p.AddParamEsc((char*)buf, csize+ sizeof(size));
			}

//			p.AddParamEsc((char*)&size, sizeof(size));
//			p.AddParamEsc((char*)compressed, csize, 1);
			if (hrefs)
			{
				p.AddParamEsc((char*)hrefs, hsz << 2);
			}
			else
			{
				p.AddParam("");
			}
#ifdef UNICODE
			p.AddParamEsc(content->m_title.c_str(), (int)content->m_title.size());
			p.AddParamEsc(content->m_txt.c_str(), (int)content->m_txt.size());
			p.AddParamEsc(content->m_keywords.c_str(), (int)content->m_keywords.size());
			p.AddParamEsc(content->m_description.c_str(), (int)content->m_description.size());
#else
			p.AddParamEsc(content->m_title.c_str());
			p.AddParamEsc(content->m_txt.c_str());
			p.AddParamEsc(content->m_keywords.c_str());
			p.AddParamEsc(content->m_description.c_str());
#endif
			ULONG ud = content->m_changed & 8 ? 1 : 0;
			p.AddParam(&ud);
			sqlquery = m_sqlquery->SaveContentsI(&p);
			sql_real_query(sqlquery);
			if (csize >= 20000)
			{
				delete buf;
			}
		}
		else
		{
/* "UPDATE %s SET docsize=%u,wordcount=%u,totalcount=%u,lang='%s',content_type='",
uwt, content->m_docsize, wordCount, content->m_totalCount, content->m_lang.c_str());
			pquery = strcpyq(pquery, content->m_content_type.c_str());
			STRCPY(pquery, "',charset = '");
			pquery = strcpyq(pquery, content->m_charset.c_str());
			STRCPY(pquery, "',title = '");
			pquery = strcpyq(pquery, content->m_title.c_str());
			STRCPY(pquery, "',txt='");
			pquery = strcpyq(pquery, content->m_txt.c_str());
			STRCPY(pquery, "',keywords='");
			pquery = strcpyq(pquery, content->m_keywords.c_str());
			STRCPY(pquery, "',description='");
			pquery = strcpyq(pquery, content->m_description.c_str());
			sprintf(pquery, "' WHERE url_id=%lu", content->m_urlID);
			pquery += strlen(pquery);
*/
			CSQLParam p;
			p.AddParam(uwt);
			p.AddParam(&content->m_docsize);
			p.AddParam(&wordCount);
			p.AddParam(&content->m_totalCount);
			p.AddParam(content->m_lang.c_str());
			p.AddParamEsc(content->m_content_type.c_str());
			p.AddParamEsc(content->m_charset.c_str());
			p.AddParamEsc(content->m_title.c_str());
			p.AddParamEsc(content->m_txt.c_str());
			p.AddParamEsc(content->m_keywords.c_str());
			p.AddParamEsc(content->m_description.c_str());
			p.AddParam(&content->m_urlID);
			CSQLQuery *sqlquery = m_sqlquery->SaveContentsU(&p);
			sql_real_query(sqlquery);
		}
	}
	if (content->m_changed)
	{
		CTimer1 timer(sqlUpdate);
		CSQLParam p;
//		strcpy(sqlquery, "UPDATE urlword SET ");
		CSQLQuery *sqlquery = m_sqlquery->SaveContentsU1(&p);
		if (content->m_changed & 1)
		{
//			pquery += sprintf(pquery, "next_index_time = %u, redir = %u", content->m_period, content->m_redir);
			sqlquery->AddQuery(sqlquery->SaveContentsU2());
			p.AddParam(&content->m_period);
			p.AddParam(&content->m_redir);
			if (content->m_status != 304)
			{
//				pquery += sprintf(pquery, ", status = %u", content->m_status);
				sqlquery->AddQuery(sqlquery->SaveContentsU3());
				p.AddParam(&content->m_status);
			}
		}
		if (content->m_changed & 2)
		{
			if (content->m_changed & 1)
			{
				sqlquery->AddQuery(",");

			}
/*			sprintf(pquery, "last_index_time = %lu, tag = %u, crc='%s',",
				now(), content->m_tag,	content->m_crc.c_str());
			pquery += strlen(pquery);
			STRCPY(pquery, "last_modified='");
			pquery = strcpyq(pquery, content->m_last_modified.c_str());
			strcpy(pquery, "'");
			pquery += strlen(pquery);

			STRCPY(pquery, ",etag='");
			pquery = strcpyq(pquery, content->m_etag.c_str());
			strcpy(pquery, "'");
			pquery += strlen(pquery);
*/
			sqlquery->AddQuery(sqlquery->SaveContentsU4());
			int tm = now();
			p.AddParam(&tm);
			p.AddParam(&content->m_tag);
			p.AddParam(content->m_crc.c_str());
			p.AddParamEsc(content->m_last_modified.c_str());
			p.AddParamEsc(content->m_etag.c_str());
#ifdef OPTIMIZE_CLONE
			if ((content->m_changed & 4) == 0)
			{
				sqlquery->AddQuery(", origin=:9");
				p.AddParam(&content->m_origin);
			}
#endif
		}
		if (content->m_changed & 4)
		{
#ifdef OPTIMIZE_CLONE
			sqlquery->AddQuery(" origin=:9");
			p.AddParam(&content->m_origin);
#endif
		}

//		sprintf(pquery, " WHERE url_id=%lu", content->m_urlID);

		sqlquery->AddQuery(" WHERE url_id=:8 ");
		p.AddParam(&content->m_urlID);
		sql_real_query(sqlquery);
	}
	if (compressed1) delete compressed1;
}


void CSQLDatabaseI::CheckCharsetGuesser()
{
#ifdef USE_CHARSET_GUESSER
	ULONG nexturl = 0, fl = 1;
	ULONG urlcnt = 0, totalurl = 0, bad = 0, totalbad = 0;

	struct timeval tm, lastSave, firstSave;
	double dif;

	gettimeofday(&tm, NULL);
	lastSave = firstSave = tm;
	while (fl){
		CSQLParam p;
		p.AddParam(&nexturl);
		CSQLQuery* query = m_sqlquery->SetQuery("SELECT url_id, url FROM urlword WHERE status=200 AND deleted!=1 AND url_id>:1", &p);
		query->AddLimit(1000);
		CSQLAnswer *answ = sql_query(query);
		fl = 0;
		while (answ && answ->FetchRow())
		{
			BYTE* content;
			unsigned long int uLen;
			ULONG urlid, wordCount;
			char content_type[50];
			string charset_db;
			int size, charset_gid;
			char *url;

			answ->GetColumn(0, urlid);
			url = answ->GetColumn(1);

			nexturl = urlid;
			size = GetContents(urlid, (BYTE**)& content, wordCount, NULL, NULL, content_type, charset_db);
			BYTE* uncompr = Uncompress(content, size, uLen);
			if (uncompr)
			{
				charset_gid = langs.GetLang((char*)uncompr);
#ifdef UNICODE
				if (charset_gid != CHARSET_USASCII && GetUCharset(charset_db.c_str()) != GetUCharset(charset_gid))
#else
				if (charset_gid != CHARSET_USASCII && charset_gid != GetCharset(charset_db.c_str()))
#endif
				{
					logger.log(CAT_ALL, L_ERR, "Url: %s charset %s - %s\n", url, charset_db.c_str(),
#ifdef UNICODE
GetCharsetName(charset_gid)
#else
charsetMap.CharsetsStr(charset_gid)
#endif
);
					bad++;
					totalbad++;
				}
				delete uncompr;
			}
			urlcnt++;
			totalurl++;
			gettimeofday(&tm, NULL);
			if ((dif = timedif(tm, lastSave)) > 60)
			{
				logger.log(CAT_ALL, L_ERR, "Sec: %7.3f Urls: %d Bad: %d \n", dif, urlcnt, bad);
				lastSave = tm;
				urlcnt = 0;
				bad = 0;
			}
			if (content) delete content;
			fl = 1;
		}
		if (answ) delete answ;

	}
	gettimeofday(&tm, NULL);
	dif = timedif(tm, lastSave);
	logger.log(CAT_ALL, L_ERR, "Sec: %7.3f Urls: %d Bad: %d \n", dif, urlcnt, bad);
	dif = timedif(tm, firstSave);
	logger.log(CAT_ALL, L_ERR, "Total sec: %7.3f Total urls: %d Total bad: %d Last url_id: %d\n", dif, totalurl, totalbad, nexturl);

#else
	logger.log(CAT_ALL, L_ERR, "Charset guesser not enabled\n");
#endif
}

int CSQLUrlFilter::Match(const char* url)
{
	return ::Match<char>(url, m_pattern.c_str(), '%', '_');
}

int CSQLOrFilter::Match(const char* url)
{
	for (vector<CSQLFilter*>::iterator filter = m_filters.begin(); filter != m_filters.end(); filter++)
	{
		if ((*filter)->Match(url)) return 1;
	}
	return 0;
}
