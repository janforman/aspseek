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

/*  $Id: sqldbi.h,v 1.11 2002/05/14 11:47:16 kir Exp $
    Author : Igor Sukhih
*/

#ifndef _SQLDBI_H_
#define _SQLDBI_H_

#include <pthread.h>
#include <vector>
#include <string.h>
#include <stdio.h>
//#include "content.h"
#include "defines.h"
#include "sqldb.h"
#include "config.h"
#include "datetime.h"
#include "squeue.h"
//#include "geo.h"

using std::vector;

class CIPTree;
class CUrlRanks;
class CUrlRank;
class CWordCache;
//extern hash_set<CURLName> Sites;

class CSQLDatabaseI : public virtual CSQLDatabase
{
public:
	CSitesQueue m_squeue; ///< Queue of URLs to index
	int m_maxtime;
	int m_maxhops;
	int m_maxhopsa;
	hash_map<int, int> m_maxtimes;
	int m_available;
	int m_got;

public:
	CSQLDatabaseI()
	{
		m_maxtime = 0;
		m_maxhops = 0;
		m_maxhopsa = 0;
		m_available = 1;
		m_got = 0;
	}

	void CalcTotal();			///< Calculates total number of non-empty indexed URLs
	void LoadRanks(CUrlRanks& ranks);	///< Loads outgoing hyperlinks for all URLs from database, saves lastmod file by the way
	void SaveLinks(CUrlRanks& ranks);	///< Saves reverse hyperlink index to the database
	void SaveLink(const CUrlRank& rank);	///< Saves hyperlinks to particular URL
	void SaveContents(CUrlParam* content, ULONG wordCount, int changed, ULONG* hrefs, int hsz, double& sqlUpdate);
	/// StoreHref saves given URL to the database. Used in "real-time" indexing
	void StoreHref(const char* url, CHref& href, const char* server, int* maxtime);
	/// StoreHref1 saves given URL to the database. Used for storing URLs, produced by "Server" config commands
	ULONG StoreHref1(const char* url, CHref& href, const char* server, int hopsord, ULONG& lost);
	/// StoreHref2 saves given URL to the database. Used for storing URLs, produced by outgoing hyperlinks
	ULONG StoreHref2(const char* url, CHref& href, const char* server, int hopsord, ULONG& lost);
	/// StoreHrefs saves all URLs, produced by "Server" config commands to the database.
	void StoreHrefs(int hops);
	void GetTmpUrls(CTmpUrlSource& tmpus);	///< Reads all URL IDs from database table "tmpurl"
	void GenerateWordSite();		///< Builds "wordsite" table from "sites"
	void UpdateWordSite(char* hostname, ULONG site_id);	///< Updates "wordsite" table on "real-time" indexing
	/// Inserts 1 record into "wordsite" table
	void InsertWordSite(const char* wrd, ULONG* sites, int size);
	/// Adds URLs, produced by given query to the queue of URLs
	int AddUrls(CSQLQuery *query, double& time, int& maxtime, CIntSet* set);
	/// Returns next URL ID to index, when ordering by hops is on
	ULONG GetNextLinkH(int numthreads);
	/// Returns next URL ID to index, when ordering by next index time is on
	ULONG GetNextLink(int numthreads);
	int GetUrlID(const char* url, CHref& href, int nit, ULONG& urlID, ULONG& lost);
	void MarkDeleted(ULONG urlID, int val = 1);
	void LoadCountries(CIPTree* tree);
	void AddCountry(const char* country, ULONG addr, ULONG mask);
	void ReplaceCountry(ULONG istart, const char* country);
	void NarrowCountry(ULONG istart, ULONG mask, int off);
	void FindMaxHops();
	void LoadRobotsOfHost(char* hostinfo);
	void LoadRobots();
	void AddRobotsToHost(char* host, char*s);
	void DeleteRobotsFromHost(char* host);
	void DeleteUrlFromSpace(const char* curl, ULONG space);
	ULONG DeleteUrls(CWordCache* wcache, const char *limit);
	void UpdateOrigin(ULONG urlID);
	void AddUrlToSpace(const char* curl, ULONG space);
	int MarkForReindex();
	int GetStatistics();
	void LoadSites();
	void CheckHrefDeltas();
	int Startup();
	void CheckCharsetGuesser();
#ifdef TWO_CONNS
	virtual CSQLDatabaseI* GetAnother() {return this;};
#endif
	void BuildSiteURLs();
};

typedef CSQLDatabaseI * PSQLDatabaseI;

// In each DB driver, you need to provide this function:
extern "C" {
	PSQLDatabaseI newDataBaseI(void); /* return new CSomeSQLDatabaseI; */
}

class CSQLFilter
{
public:
	virtual int Match(const char* url) = 0;
};

class CSQLFalseFilter : public CSQLFilter
{
public:
	virtual int Match(const char* url) {return 0;};
};

class CSQLTrueFilter : public CSQLFilter
{
public:
	virtual int Match(const char* url) {return 1;};
};

class CSQLUrlFilter : public CSQLFilter
{
public:
	string m_pattern;

public:
	CSQLUrlFilter(const char* pattern)
	{
		m_pattern = pattern;
	}
	virtual int Match(const char* url);
	virtual ~CSQLUrlFilter(){}
};

class CSQLOrFilter : public CSQLFilter
{
public:
	vector<CSQLFilter*> m_filters;

public:
	virtual int Match(const char* url);
	virtual ~CSQLOrFilter(){}

};

extern CSQLFilter* SQLFilter;

#endif /* _SQLDBI_H_ */
