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

/* $Id: geo.h,v 1.10 2002/01/09 11:50:14 kir Exp $
 * Author : Alexander F. Avdonkin
 */

#ifndef _GEO_H_
#define _GEO_H_

#include <pthread.h>
#include "documents.h"

#define COUNTRY_EXPIRATION (7 * 86400)
extern int OnlineGeo;

class CCountry : public CFixedString<20>
{
public:
	time_t m_expires;

public:
	void operator=(const char* src)
	{
		CFixedString<20>::operator=(src);
	}
};

class CWordCache;
class CSQLDatabaseI;

class CMutex
{
public:
	pthread_mutex_t m_mutex;

public:
	CMutex()
	{
		pthread_mutex_init(&m_mutex, NULL);
	}
	~CMutex()
	{
		pthread_mutex_destroy(&m_mutex);
	}
	void Lock()
	{
		pthread_mutex_lock(&m_mutex);
	}
	void Unlock()
	{
		pthread_mutex_unlock(&m_mutex);
	}
};

class CIPTree
{
public:
	CIPTree* m_sub[2];
	CCountry* m_country;
	CMutex m_mutex;
public:
	CIPTree()
	{
		m_sub[0] = NULL;
		m_sub[1] = NULL;
		m_country = NULL;
	}
	void Lock()
	{
		if (OnlineGeo) m_mutex.Lock();
	}

	void Unlock()
	{
		if (OnlineGeo) m_mutex.Unlock();
	}

	void FillCountry(const char* country, time_t expires);
	void AddCountryToAll(const char* country, time_t expires);
	void AddCountry(ULONG addr, ULONG mask, const char* country, time_t expires);
	CCountry* GetCountry(ULONG addr);
	void AddRange(ULONG start, ULONG end, ULONG mask, const char* country);
	void AddRange(ULONG start, ULONG end, const char* country);
	void AddRange(char* string, const char* country);
	void SaveCountries(CSQLDatabaseI* database, ULONG addr, ULONG mask);
	void ReplaceCountry(ULONG addr, ULONG start, ULONG end, ULONG istart, ULONG mask, const char* country, CSQLDatabaseI* database);
	int GetCountry(CWordCache& wcache, ULONG addr, ULONG iaddr, ULONG mask, char* country);
};

class CIPTreeMain : public CIPTree
{
public:
	pthread_mutex_t mutex;

public:
	CIPTreeMain()
	{
		pthread_mutex_init(&mutex, NULL);
	}
	~CIPTreeMain()
	{
		pthread_mutex_destroy(&mutex);
	}
	const char* GetCountry(CWordCache& wcache, ULONG addr);
	void ImportCountries(const char* file);
	void SaveCountries(CSQLDatabaseI* database);
	void LoadCountries(CSQLDatabaseI* database);
	void LoadCountriesIfEmpty(CSQLDatabaseI* database);
	void SaveCountriesIfNotEmpty(CSQLDatabaseI* database);
	int GetCountry(CWordCache& wcache, ULONG addr, char* country);
};

void SaveCountriesIfNotEmpty(CSQLDatabaseI* database);
void LoadCountriesIfEmpty(CSQLDatabaseI* database);
void ImportCountries(const char* file);
const char* GetCountry(CWordCache& wcache, ULONG addr);
int GetCountry(CWordCache& wcache, ULONG addr, char* country);
void TestCountries();
void TestCountries(CWordCache& wcache);

#endif
