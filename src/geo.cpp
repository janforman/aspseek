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

/*  $Id: geo.cpp,v 1.15 2002/05/28 15:01:02 kir Exp $
 *  Author : Alexander F. Avdonkin
 */

#include "aspseek-cfg.h"

#include "geo.h"
#include "logger.h"
#include "parse.h"
#include "sqldbi.h"
#include "timer.h"
#include "wcache.h"
#include "index.h"

CIPTreeMain countries;
int OnlineGeo = 0;

void CIPTree::AddCountry(ULONG addr, ULONG mask, const char* country, time_t expires)
{
	if (mask)
	{
		CIPTree*& subt = m_sub[addr >> 31];
		if (subt == NULL) subt = new CIPTree;
		if (m_country)
		{
			CCountry* c = m_country;
			m_country = NULL;
			FillCountry(c->c_str(), c->m_expires);
		}
		subt->AddCountry(addr << 1, mask << 1, country, expires);
	}
	else
	{
		AddCountryToAll(country, expires);
	}
}

void CIPTree::FillCountry(const char* country, time_t expires)
{
	if (m_country == NULL)
	{
		if (m_sub[0] || m_sub[1])
		{
			if (m_sub[0] == NULL) m_sub[0] = new CIPTree;
			m_sub[0]->FillCountry(country, expires);
			if (m_sub[1] == NULL) m_sub[1] = new CIPTree;
			m_sub[1]->FillCountry(country, expires);
		}
		else
		{
			m_country = new CCountry;
			*m_country = country;
			m_country->m_expires = expires;
		}
	}
}

void CIPTree::AddCountryToAll(const char* country, time_t expires)
{
	if (m_sub[0] || m_sub[1])
	{
		if (m_sub[0] == NULL) m_sub[0] = new CIPTree;
		if (m_sub[1] == NULL) m_sub[1] = new CIPTree;
		m_sub[0]->AddCountryToAll(country, expires);
		m_sub[1]->AddCountryToAll(country, expires);
	}
	else
	{
		if (m_country == NULL)
		{
			m_country = new CCountry;
		}
		*m_country = country;
		m_country->m_expires = expires;
	}
};

CCountry* CIPTree::GetCountry(ULONG addr)
{
	if (m_country) return m_country;
	CIPTree* subt = m_sub[addr >> 31];
	return subt ? subt->GetCountry(addr << 1) : NULL;
}

void CIPTree::AddRange(ULONG start, ULONG end, ULONG mask, const char* country)
{
	while (true)
	{
		if ((start & mask) != (end & mask))
		{
			if ((start & ~mask) || ((end & ~mask) != ~mask))
			{
				AddRange(start, start | ~mask, mask, country);
				AddRange(end & mask, end, mask, country);
			}
			else
			{
				AddCountry(start, mask << 1, country, now() + COUNTRY_EXPIRATION);
			}
			break;
		}
		if (mask == 0xFFFFFFFF) break;
		mask |= (mask >> 1);
	}
}

void CIPTree::AddRange(ULONG start, ULONG end, const char* country)
{
	AddRange(start, end, 0x80000000, country);
}

ULONG GetAddr(char* string)
{
	ULONG a[4];
	if (sscanf(string, "%lu.%lu.%lu.%lu", a, a + 1, a + 2, a + 3) == 4)
	{
		return (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
	}
	else
	{
		return 0;
	}
}

ULONG GetEnd(ULONG start)
{
	ULONG end = start;
	for (ULONG mask = 0xFF; mask; mask <<= 8)
	{
		if ((start & mask) == 0)
		{
			end |= mask;
		}
		else
		{
			break;
		}
	}
	return end;
}

void CIPTree::AddRange(char* string, const char* country)
{
	char start[100], end[100];
	int c = sscanf(string, "%[^-]%[^-]", start, end);
	if (c > 0)
	{
		ULONG ipstart = GetAddr(start);
		ULONG ipend = c == 2 ? GetAddr(start) : GetEnd(ipstart);
		if (ipstart && ipend)
		{
			AddRange(ipstart, ipend, country);
		}
	}
}

int EDownloadCountry(CWordCache& wcache, ULONG addr, ULONG* range, char* country)
{
	int got = 0;
	CSQLParam p;
	p.AddParam(&addr);
//	sprintf(query, "SELECT max(addr) from countries1 where addr <= %lu", addr);
	CSQLQuery *sqlquery = wcache.m_database->m_sqlquery->EDownloadCountryS(&p);
	CSQLAnswer *answ = wcache.m_database->sql_queryw(sqlquery);
	if (answ && answ->FetchRow() && answ->GetColumn(0))
	{
		ULONG addrm;
		sscanf(answ->GetColumn(0), "%lu", &addrm);
		delete answ;
//		sprintf(query, "SELECT mask, country FROM countries1 WHERE addr = %lu", addrm);
		p.Clear();
		p.AddParam(&addrm);
		CSQLQuery *sqlquery = wcache.m_database->m_sqlquery->EDownloadCountryS1(&p);
		CSQLAnswer *answ = wcache.m_database->sql_queryw(sqlquery);
		answ->FetchRow();
		ULONG mask;
		sscanf(answ->GetColumn(0), "%lu", &mask);
		if ((addr & mask) == (addrm & mask))
		{
			strcpy(country, answ->GetColumn(1));
			range[0] = addrm;
			range[1] = addrm - mask + 1;
			got = 3;
		}
		if (answ)
		{
			delete answ;
			answ = NULL;
		}
	}
	if (answ) delete answ;
	return got;
}

int DownloadCountry(CWordCache& wcache, ULONG addr, ULONG* range, char* country)
{
	int got = 0;
	char url[200];
	country[0] = 0;
	sprintf(url, "http://netgeo.caida.org/perl/netgeo.cgi?target=%lu.%lu.%lu.%lu", addr >> 24, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF);
	CUrl curl;
	CServer srv;
	curl.ParseURL(url);
	char buf[0x2000];
	int size = curl.HTTPGetUrl(wcache, &srv, buf, sizeof(buf) - 1, ALLOW, "");
	if (size > 0)
	{
		buf[size] = 0;
		char* content = Find2CRs(buf, size);
		if (content)
		{
			char cr = *content;
			*content = 0;
			content += cr == '\r' ? 4 : 2;
			while (*content)
			{
				char* e = strchr(content, '\n');
				if (e == NULL) break;
				*e = 0;
				if (STRNCMP(content, "NUMBER:") == 0)
				{
					ULONG s[4], e[4];
					if (sscanf(content + 7, "%lu.%lu.%lu.%lu - %lu.%lu.%lu.%lu", s, s + 1, s + 2, s + 3, e, e + 1, e + 2, e + 3) == 8)
					{
						got |= 1;
						range[0] = (((((s[0] << 8) + s[1]) << 8) + s[2]) << 8) + s[3];
						range[1] = (((((e[0] << 8) + e[1]) << 8) + e[2]) << 8) + e[3];
					}
					if (got == 3) break;
				}
				else if (STRNCMP(content, "COUNTRY:") == 0)
				{
					char* pc = country;
					char* c = content + 8;
					while (*c == ' ') c++;
					while (isalpha(*c) && (pc - country < 9))
					{
						*pc++ = *c++;
					}
					*pc = 0;
					if (pc > country) got |= 2;
					if (got == 3) break;
				}
				content = e + 1;
			}
		}
	}
	return got;
}

void CIPTree::ReplaceCountry(ULONG addr, ULONG start, ULONG end, ULONG istart, ULONG mask, const char* country, CSQLDatabaseI* database)
{
	int over = (start <= istart) && (end >= istart + (mask << 1) - 1);
	int ind = addr & mask ? 1 : 0;
	if (m_sub[0] || m_sub[1])
	{
		CIPTree*& ipTree = m_sub[ind];
		if (ipTree == NULL) ipTree = new CIPTree;
		ipTree->ReplaceCountry(addr, start, end, istart + (ind ? mask : 0), mask >> 1, country, database);
	}
	else
	{
		if (over)
		{
			int exists = m_country != NULL;
			if (!exists) m_country = new CCountry;
			*m_country = country;
			m_country->m_expires = now() + COUNTRY_EXPIRATION;
			if (exists)
			{
				database->ReplaceCountry(istart, country);
			}
			else
			{
				database->AddCountry(country, istart, ~(mask | (mask - 1)));
			}
		}
		else
		{
			CIPTree* ipTree;
			ipTree = new CIPTree;
			m_sub[ind] = ipTree;
			if (m_country)
			{
				ipTree = new CIPTree;
				ipTree->m_country = m_country;
				m_sub[ind ^ 1] = ipTree;
				database->NarrowCountry(istart, mask, ind ^ 1);
				ipTree = m_sub[ind];
				ipTree->m_country = new CCountry;
				*ipTree->m_country = m_country->c_str();
				ipTree->m_country->m_expires = m_country->m_expires;
				database->AddCountry(m_country->c_str(), istart + (ind ? mask : 0), ~((mask | (mask - 1)) >> 1));
				m_country = NULL;
			}
			ipTree->ReplaceCountry(addr, start, end, istart + (ind ? mask : 0), mask >> 1, country, database);
		}
	}
}

int hits = 0, downloads = 0;

int CIPTree::GetCountry(CWordCache& wcache, ULONG addr, ULONG iaddr, ULONG mask, char* country)
{
	Lock();
	int ind = addr & mask ? 1 : 0;
	CIPTree* subt = m_sub[ind];
	if (m_country)
	{
		if (OnlineGeo && (m_country->m_expires < now())) goto getCountry;
		strcpy(country, m_country->c_str());
		Unlock();
		hits++;
		return 0;
	}
	if (subt)
	{
		Unlock();
		return subt->GetCountry(wcache, addr, iaddr + (ind ? mask : 0), mask >> 1, country);
	}
	else
	{
		if (OnlineGeo)
		{
getCountry:
			ULONG range[2];
			char ncountry[20];
			downloads++;
			if (DownloadCountry(wcache, addr, range, ncountry) == 3)
			{
				if ((m_country == NULL) || (strcmp(ncountry, m_country->c_str())))
				{
					ReplaceCountry(addr, range[0], range[1], iaddr, mask, ncountry, wcache.m_database);
				}
				strcpy(country, ncountry);
				Unlock();
				return 0;
			}
			else
			{
				if (m_country)
				{
					strcpy(country, m_country->c_str());
					Unlock();
					return 0;
				}
				else
				{
					ReplaceCountry(addr, addr, addr, iaddr, mask, "", wcache.m_database);
					Unlock();
					return 1;
				}
			}
		}
		else
		{
			Unlock();
			return 1;
		}
	}
}

int CIPTreeMain::GetCountry(CWordCache& wcache, ULONG addr, char* country)
{
	return CIPTree::GetCountry(wcache, addr, 0, 0x80000000, country);
}

const char* CIPTreeMain::GetCountry(CWordCache& wcache, ULONG addr)
{
	CCountry* country;
//	CLocker lock(&mutex);
	country = CIPTree::GetCountry(addr);
	if ((country == NULL) || (country->m_expires < now()))
	{
		char ncountry[20];
		ULONG range[2];
		if (DownloadCountry(wcache, addr, range, ncountry) == 3)
		{
			if ((country == NULL) || strcmp(ncountry, country->c_str()))
			{
				ReplaceCountry(addr, range[0], range[1], 0, 0x80000000, country->c_str(), wcache.m_database);
			}
		}
	}
	return country ? country->c_str() : NULL;
}

void CIPTreeMain::ImportCountries(const char* file)
{
	char str[1000];
	FILE* f = fopen(file, "r");
	if (f)
	{
		while (fgets(str, sizeof(str), f))
		{
			char country[10];
			char start[100], end[100];
			int c = sscanf(str, "%[^-]-%[^- ] %s", start, end, country);
			if (c == 3)
			{
				AddRange(GetAddr(start), GetAddr(end), country);
			}
		}
		fclose(f);
	}
}

void CIPTree::SaveCountries(CSQLDatabaseI* database, ULONG addr, ULONG mask)
{
	if (m_sub[0] || m_sub[1])
	{
		ULONG smask = (mask >> 1) | 0x80000000;
		if (m_sub[0]) m_sub[0]->SaveCountries(database, addr, smask);
		if (m_sub[1]) m_sub[1]->SaveCountries(database, addr | (smask & (~mask)), smask);
	}
	else
	{
		if (m_country) database->AddCountry(m_country->c_str(), addr, mask);
	}
}

void CIPTreeMain::SaveCountries(CSQLDatabaseI* database)
{
	CIPTree::SaveCountries(database, 0, 0);
}

void CIPTreeMain::SaveCountriesIfNotEmpty(CSQLDatabaseI* database)
{
//	sql_query("SELECT addr FROM countries LIMIT 1");
	CSQLQuery *sqlquery = database->m_sqlquery->SaveCountriesIfNotEmptyS();
	sqlquery->AddLimit(1);
	CSQLAnswer *answ = database->sql_queryw(sqlquery);

	if (answ)
	{
		if (!answ->FetchRow())
		{
			if (m_sub[0] || m_sub[1])
			{
				CIPTree::SaveCountries(database, 0, 0);
			}
		}
	}
	if (answ) delete answ;
}

void CIPTreeMain::LoadCountries(CSQLDatabaseI* database)
{
	database->LoadCountries(this);
}

void CIPTreeMain::LoadCountriesIfEmpty(CSQLDatabaseI* database)
{
	if ((m_sub[0] == NULL) && (m_sub[1] == NULL))
	{
		LoadCountries(database);
	}
}

void SaveCountriesIfNotEmpty(CSQLDatabaseI* database)
{
	countries.SaveCountriesIfNotEmpty(database);
}

void LoadCountriesIfEmpty(CSQLDatabaseI* database)
{
	countries.LoadCountriesIfEmpty(database);
}

void ImportCountries(const char* file)
{
	countries.ImportCountries(file);
}

const char* GetCountry(CWordCache& wcache, ULONG addr)
{
	return countries.GetCountry(wcache, addr);
}

int GetCountry(CWordCache& wcache, ULONG addr, char* country)
{
	return countries.GetCountry(wcache, addr, country);
}

class CResMut
{
public:
	CSQLAnswer* m_result;
	CWordCache* m_wcache;
	CMutex m_mutex;
	int m_count;

public:
	CResMut()
	{
		m_count = 0;
	}
	CSQLAnswer* GetRow()
	{
		CLocker lock(&m_mutex.m_mutex);
		if (m_result->FetchRow())
		{
			return m_result;
		}
		else
		{
			return NULL;
		}
	}
	int Inc()
	{
		CLocker lock(&m_mutex.m_mutex);
		return ++m_count;
	}
};

void* TestCountry(void* p)
{
	CResMut* rm = (CResMut*)p;
	char country[20];
	CSQLAnswer* answ;
	while ((answ = rm->GetRow()))
	{
		if ((GetCountry(*rm->m_wcache, GetAddr(answ->GetColumn(0)), country)) || (strcmp(country, "SG")))
		{
//				printf("%s %s %s\n", country ? country : "No country", row[0], row[1]);
		}
		if (rm->Inc() % 100 == 0)
		{
			printf("Hits: %i, downloads: %i\n", hits, downloads);
		}
	}
	return p; // FIXME?
}

class CAns
{
public:
	CSQLAnswer* m_res;
	ULONG m_addr, m_mask;
	char* m_country;
	int m_eof;

public:
	CAns()
	{
		m_eof = 0;
	}
	void GetRow()
	{
		if (m_eof == 0)
		{
//			ULONG daddr, dmask;
			if (m_res->FetchRow())
			{
				sscanf(m_res->GetColumn(0), "%lu", &m_addr);
				sscanf(m_res->GetColumn(1), "%lu", &m_mask);
				m_country = m_res->GetColumn(2);
			}
			else
			{
				m_eof = 1;
			}
		}
	}
	void CheckSync(CIPTree* ipTree, ULONG addr, ULONG mask);
};

void CAns::CheckSync(CIPTree* ipTree, ULONG addr, ULONG mask)
{
	if (ipTree->m_country)
	{
		if (m_eof)
		{
			printf("End of DB table: addr: %08lx, mask: %08lx\n", addr, mask);
		}
		else
		{
			if (m_addr > addr)
			{
				printf("Discrepancy: mask: %08lx, dmask: %08lx, addr: %08lx, daddr: %08lx, country: %s, dcountry: %s\n", mask, m_mask ^ (m_mask << 1), addr, m_addr, ipTree->m_country->c_str(), m_country);
			}
			else
			{
				while (!m_eof && (m_addr <= addr))
				{
					if ((addr != m_addr) || (mask != (m_mask ^ (m_mask << 1))) || strcmp(m_country, ipTree->m_country->c_str()))
					{
						printf("Discrepancy: mask: %08lx, dmask: %08lx, addr: %08lx, daddr: %08lx, country: %s, dcountry: %s\n", mask, m_mask ^ (m_mask << 1), addr, m_addr, ipTree->m_country->c_str(), m_country);
					}
					GetRow();
				}
			}
		}
	}
	else
	{
		mask = mask ? mask >> 1 : 0x80000000;
		if (ipTree->m_sub[0]) CheckSync(ipTree->m_sub[0], addr, mask);
		if (ipTree->m_sub[1]) CheckSync(ipTree->m_sub[1], addr + mask, mask);
	}
}

void CheckSync(CSQLDatabaseI* database)
{
	CAns ans;
//	ans.m_res = database->sql_query("SELECT addr, mask, country FROM countries ORDER by 1");
	CSQLQuery *sqlquery = database->m_sqlquery->CheckSyncS();
	ans.m_res = database->sql_query(sqlquery);

	ans.GetRow();
	ans.CheckSync(&countries, 0, 0);
	if (ans.m_eof == 0)
	{
		printf("Records from DB were not loaded starting from addr: %08lx, mask: %08lx\n", ans.m_addr, ans.m_mask);
	}
	delete ans.m_res;
}

void TestCountries(CWordCache& wcache)
{
	OnlineGeo = 1;
//	MYSQL_RES* res = wcache.m_database->sql_query("SELECT addr,host from addr where substring(addr,1,3) in ('202', '203')");
	CSQLQuery *sqlquery = wcache.m_database->m_sqlquery->TestCountriesS();
	CSQLAnswer *answ = wcache.m_database->sql_query(sqlquery);

	if (answ)
	{
		pthread_t threads[50];
		CResMut rm;
		rm.m_result = answ;
		rm.m_wcache = &wcache;
		for (int i = 0; i < 50; i++)
		{
			pthread_create(threads + i, NULL, ::TestCountry, &rm);
		}
		for (int i = 0; i < 50; i++)
		{
			void* p;
			pthread_join(threads[i], &p);
		}

//		TestCountry(&rm);
		CheckSync(wcache.m_database);
		delete answ;
	}
}
