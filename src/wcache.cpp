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

/*  $Id: wcache.cpp,v 1.32 2002/08/23 09:09:58 kir Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CByteCounter, CWordCache
*/

#include <signal.h>
#include "wcache.h"
#include "content.h"
#include "index.h"
#include "config.h"
#include "filters.h"
#include "logger.h"
#include "timer.h"
#include "buddy.h"
#include "sqldbi.h"

int term = 0;
extern int thread_stack_size;

long CByteCounter::AddBytes(int bytes)
{
	CLocker lock(&m_mutex);
	struct timeval tm;
	int ni, ni1, n2, i, over;
	unsigned long long time;
	gettimeofday(&tm, NULL);
	time = ((unsigned long long)tm.tv_sec * 1000000 + tm.tv_usec) * SUB_COUNT / m_duration;
	over = (time / SUB_COUNT) > (m_lastTime / SUB_COUNT);
	ni = time % SUB_COUNT;
	ni1 = m_lastTime  % SUB_COUNT;
	n2 = over ? SUB_COUNT - 1 : ni;
	for (i = ni1 + 1; i <= n2; i++)
	{
		m_windowBytes -= m_bytes[i];
		m_bytes[i] = 0;
	}
	if (over)
	{
		n2 = min(ni, ni1);
		for (i = 0; i <= n2; i++)
		{
			m_windowBytes -= m_bytes[i];
			m_bytes[i] = 0;
		}
	}
	m_windowBytes += bytes;
	m_bytes[ni] += bytes;
	m_totalBytes += bytes;
	m_lastTime = time;
	return m_windowBytes;
}

void CByteCounter::WaitBW()
{
	unsigned long long bw = GetMaxBW();
	if (bw > 0)
	{
		bw *= m_duration;
		while ((unsigned long long)AddBytes(0) * 1000000 > bw)
		{
			usleep(10000);
			m_sleeps++;
		}
	}
}

ULONG CByteCounter::GetMaxBW()
{
	return bwSchedule.MaxBandWidth();
}

void CWordCache::Check()
{
	CWordInfo* info = m_lru;
	int count = 0;
	do
	{
		if ((info->m_mru->m_lru != info) || (info->m_lru->m_mru != info))
		{
			logger.log(CAT_ALL, L_ERR, "Error in LRU list\n");
		}
		info = info->m_mru;
		count++;
	}
	while (info != m_lru);
}

int CWordCache::ParseRobots(char *content, char *hostinfo)
{
	CLocker lock(this);
	int myrule=0;
	char *s,*e,*lt;
	m_database->DeleteRobotsFromHost(hostinfo);
	s = GetToken(content, "\r\n", &lt);
	while (s)
	{
		if (*s == '#')
		{
			/* Skip comment */
		}
		else if (!(STRNCASECMP(s, "User-Agent:")))
		{
			myrule = 0;
			if (strstr(s + 11, "*"))
				myrule = 1;
			else
			{
				/* case insensitive substring match */
				e = s + 11;
				while (*e++ != '\0')
					*e = tolower(*e);
				if (strstr(s + 11, USER_AGENT_LC))
					myrule = 1;
			}
			
		}
		else if ((!(STRNCASECMP(s, "Disallow"))) && myrule)
		{
			if ((e = strchr(s + 9, '#'))) *e = 0;
			e = s + 9;
			SKIP(e, " \t"); s = e;
			SKIPN(e, " \t"); *e = 0;
			if (*s)
			{
				m_database->AddRobotsToHost(hostinfo, s);
			}
		}
		s = GetToken(NULL, "\n\r", &lt);
	}
	return IND_OK;
}

int thr = 0;
/*
ULONG CWordCache::GetWordID(const CHAR* word)
{
	CLocker lock(&m_mutexw);
	iterator it = find(word);
	if (it == end())
	{
		// Word is not found in cache
		m_lost++;
#ifdef UNICODE
		CUWord w(word);
#else
		CWord w(word);
#endif
		ULONG wordID = m_database->GetWordID(w, IsRealSaving());	// Get word id from database
		if (size() >= m_maxSize)
		{
			// Cache size reached maximal specified value, remove least recenlty used word and erase it
			m_lru->m_lru->m_mru = m_lru->m_mru;
			m_lru->m_mru->m_lru = m_lru->m_lru;
			CWordInfo* lru = m_lru->m_mru;
#ifdef UNICODE
			CUWord word = *(m_lru->m_this);
#else
			CWord word = *(m_lru->m_this);
#endif
			erase(word);
			m_lru = lru;
		}
		CWordInfo& winfo = (*this)[w];	// Add new word
		winfo.m_wordID = wordID;
		it = find(w);
		winfo.m_this = &it->first;
		// Insert word to the tail of LRU list
		if (m_lru == NULL)
		{
			m_lru = &winfo;
			winfo.m_lru = &winfo;
			winfo.m_mru = &winfo;
		}
		else
		{
			winfo.m_mru = m_lru;
			winfo.m_lru = m_lru->m_lru;
			m_lru->m_lru->m_mru = &winfo;
			m_lru->m_lru = &winfo;
		}
		return wordID;
	}
	else
	{
		// Word is found in the cache
		m_hits++;
//		const CWord& word = it->first;
		CWordInfo& winfo = it->second;
		// Make it the most recent
		if (winfo.m_lru)
		{
			winfo.m_lru->m_mru = winfo.m_mru;
		}
		if (winfo.m_mru)
		{
			winfo.m_mru->m_lru = winfo.m_lru;
		}
		if (m_lru == &winfo)
		{
			m_lru = winfo.m_mru;
		}
		winfo.m_mru = m_lru;
		winfo.m_lru = m_lru->m_lru;
		m_lru->m_lru->m_mru = &winfo;
		m_lru->m_lru = &winfo;
		return winfo.m_wordID;
	}
}
*/
ULONG CWordCache::GetWordID(const CHAR* word)
{
	ULONG wordID;
start:
	pthread_mutex_lock(&m_mutexw);
	iterator it = find(word);
	if (it == end())
	{
		// Word is not found in cache
		m_lost++;
#ifdef UNICODE
		CUWord w(word);
#else
		CWord w(word);
#endif
		if (size() >= m_maxSize)
		{
			// Cache size reached maximal specified value, remove least recenlty used word and erase it
			CWordInfo* lrut = m_lru;
			if (lrut == NULL) goto uw;
			while (lrut->m_refs)
			{
				lrut = lrut->m_mru;
				if (lrut == m_lru)
				{
uw:
					m_waits1++;
					pthread_mutex_unlock(&m_mutexw);
					usleep(1000000);
					goto start;
				}
			}

			CWordInfo* lru = lrut->m_mru;
			if (lru != lrut)
			{
				lrut->m_lru->m_mru = lrut->m_mru;
				lrut->m_mru->m_lru = lrut->m_lru;
			}
#ifdef UNICODE
			CUWord word = *(lrut->m_this);
#else
			CWord word = *(lrut->m_this);
#endif
			erase(word);
			if (lrut == m_lru)
			{
				m_lru = lrut == lru ? NULL : lru;
			}
		}
		CWordInfo& winfo = (*this)[w];	// Add new word with empty m_this
		winfo.m_refs++;
		int inserted;
		pthread_mutex_unlock(&m_mutexw);
		wordID = GetDatabaseW()->GetWordID(w, inserted, IsRealSaving());	// Get word id from database
		pthread_mutex_lock(&m_mutexw);
		if (inserted) m_inserts++;
		winfo.m_wordID = wordID;
		it = find(w);
		winfo.m_this = &it->first;
		// Insert word to the tail of LRU list
		if (m_lru == NULL)
		{
			m_lru = &winfo;
			winfo.m_lru = &winfo;
			winfo.m_mru = &winfo;
		}
		else
		{
			if (inserted && (winfo.m_waiting == NULL))
			{
				winfo.m_lru = m_lru;
				winfo.m_mru = m_lru->m_mru;
				m_lru->m_mru->m_lru = &winfo;
				m_lru->m_mru = &winfo;
				m_lru = &winfo;
			}
			else
			{
				winfo.m_mru = m_lru;
				winfo.m_lru = m_lru->m_lru;
				m_lru->m_lru->m_mru = &winfo;
				m_lru->m_lru = &winfo;
			}
		}
		for (CEventLink* ev = winfo.m_waiting; ev; ev = ev->m_next)
		{
			ev->m_event.Post();
		}
		winfo.m_waiting = NULL;
		winfo.m_refs--;
	}
	else
	{
		// Word is found in the cache
		m_hits++;
//		const CWord& word = it->first;
		CWordInfo& winfo = it->second;
		if (winfo.m_this == NULL)
		{
			CEventLink evLink;
			evLink.m_next = winfo.m_waiting;
			winfo.m_waiting = &evLink;
			winfo.m_refs++;
			m_waits++;
			pthread_mutex_unlock(&m_mutexw);
			evLink.m_event.Wait();
			pthread_mutex_lock(&m_mutexw);
			winfo.m_refs--;
			if (winfo.m_this == NULL)
			{
				printf("Bug\n");
			}
			wordID = winfo.m_wordID;
		}
		else
		{
			// Make it the most recent
			if (winfo.m_lru)
			{
				winfo.m_lru->m_mru = winfo.m_mru;
			}
			if (winfo.m_mru)
			{
				winfo.m_mru->m_lru = winfo.m_lru;
			}
			if (m_lru == &winfo)
			{
				m_lru = winfo.m_mru;
			}
			winfo.m_mru = m_lru;
			winfo.m_lru = m_lru->m_lru;
			m_lru->m_lru->m_mru = &winfo;
			m_lru->m_lru = &winfo;
			wordID = winfo.m_wordID;
		}
	}
	pthread_mutex_unlock(&m_mutexw);
	return wordID;
}

void CWordCache::IncConnections(int fd)
{
	CLocker lock(&m_mutexs);
	m_connections++;
	if (fd > m_maxfd) m_maxfd = fd;
}

void CWordCache::DecConnections()
{
	CLocker lock(&m_mutexs);
	m_connections--;
}

void CWordCache::AddSqlTimes(double sqlRead, double sqlWrite, double sqlUpdate)
{
	CLocker lock(&m_mutexs);
	m_sqlRead += sqlRead;
	m_sqlWrite += sqlWrite;
	m_sqlUpdate += sqlUpdate;
}

void CWordCache::SaveWords(CULONG* words, CULONG* ewords, ULONG urlID, ULONG siteID)
{
	if (m_files)
	{
		// Save to the main database through delta files
		m_files->SaveWords((CULONG100*)words, (CULONG100*)ewords, urlID, siteID);
	}
	else
	{
		// Save to the "real-time" database, process each word
		for (CULONG* word = words; word < ewords; word++)
		{
			CLocker lock(m_database);
			CSQLQuery *sqlquery = m_database->m_sqlquery->SaveDeltaL();
			m_database->sql_queryw2(sqlquery);
			BYTE* oldContents;
			ULONG counts[2];
			// Retrieve old word info from "wordurl1" database table
			ULONG len = m_database->GetUrls1(*word->m_id, &oldContents, counts);
			BYTE* contents = new BYTE[len + ((((WORD*)word->m_id)[2] + 3) << 1)];
			BYTE* pcontents = contents;
			ULONG urlOffset = oldContents ? *(ULONG*)oldContents : 4;
			ULONG urlOff = urlOffset;
			ULONG* sites = new ULONG[(urlOffset >> 2) + 2];
			ULONG* psites = sites;
			// Combine old and new word info
			if (oldContents)
			{
				ULONG* eolds = (ULONG*)(oldContents + urlOffset - 4);
				ULONG nsite = siteID;
				ULONG nurl = urlID;
				for (ULONG* olds = (ULONG*)oldContents; (olds < eolds) || (nsite == siteID); )
				{
					if ((olds < eolds) && (olds[1] < nsite))
					{
						*psites++ = urlOffset;
						*psites++ = olds[1];
						ULONG urlLen = olds[2] - olds[0];
						memcpy(pcontents, oldContents + olds[0], urlLen);
						pcontents += urlLen;
						urlOffset += urlLen;
						olds += 2;
					}
					else
					{
						if ((olds < eolds) && (olds[1] > nsite))
						{
							*psites++ = urlOffset;
							*psites++ = nsite;
							WORD wc = ((WORD*)word->m_id)[2];
							if (wc)
							{
								ULONG urlLen = (wc + 1) << 1;
								*(ULONG*)pcontents = urlID;
								pcontents += 4;
								memcpy(pcontents, word->m_id + 1, urlLen);
								pcontents += urlLen;
								urlOffset += urlLen + 4;
								counts[0] += wc;
								counts[1]++;
							}
							nsite = 0xFFFFFFFF;
						}
						else
						{
							*psites++ = urlOffset;
							*psites++ = nsite;
							int ss = (olds < eolds) && (olds[1] == nsite);
							WORD* oldUrl = ss ? (WORD*)(oldContents + olds[0]) : NULL;
							WORD* eoldu = ss ? (WORD*)(oldContents + olds[2]) : NULL;
							while ((oldUrl < eoldu) || (nurl == urlID))
							{
								if ((oldUrl < eoldu) && (*(ULONG*)oldUrl < nurl))
								{
									ULONG len = (oldUrl[2] + 3) << 1;
									memcpy(pcontents, oldUrl, len);
									pcontents += len;
									urlOffset += len;
									oldUrl += oldUrl[2] + 3;
								}
								else
								{
									WORD wc = ((WORD*)word->m_id)[2];
									if (wc)
									{
										ULONG urlLen = (wc + 1) << 1;
										*(ULONG*)pcontents = urlID;
										pcontents += 4;
										memcpy(pcontents, word->m_id + 1, urlLen);
										pcontents += urlLen;
										urlOffset += urlLen + 4;
									}
									if (ss && (*(ULONG*)oldUrl == nurl))
									{
										oldUrl += oldUrl[2] + 3;
										counts[0] += wc - ((WORD*)oldUrl)[2];
										if (wc == 0) counts[1]--;
									}
									else
									{
										if (wc)
										{
											counts[0] += wc;
											counts[1]++;
										}
									}
									nurl = 0xFFFFFFFF;
								}
							}
							olds += 2;
							nsite = 0xFFFFFFFF;
						}
					}
				}
				*psites++ = urlOffset;
				int ch = ((BYTE*)psites - (BYTE*)sites) - urlOff;
				if (ch != 0)
				{
					for (ULONG* s = sites; s <= psites; s += 2)
					{
						*s += ch;
					}
				}
				delete oldContents;
			}
			else
			{
				psites[0] = 0xC;
				psites[1] = siteID;
				WORD wc = ((WORD*)word->m_id)[2];
				ULONG len = (wc + 1) << 1;
				*(ULONG*)pcontents = urlID;
				pcontents += 4;
				memcpy(pcontents, word->m_id + 1, len);
				pcontents += len;
				urlOffset += len + 4;
				psites[2] = 0xC + len + 4;
				psites += 3;
				counts[0] = wc;
				counts[1] = 1;
			}
			// Save new word info
			m_database->SaveWord1(*word->m_id, contents, urlOffset - urlOff, (BYTE*)sites, (BYTE*)psites - (BYTE*)sites, counts);
			sqlquery = m_database->m_sqlquery->UnlockTables();
			m_database->sql_queryw2(sqlquery);
			delete sites;
			delete contents;
		}
	}
}

ULONG CWordCache::GetHref(const char* chref, CServer* srv, int referrer, int hops, const char* server, int delta)
{
	return StoredHrefs.GetHref(m_database, srv, chref, referrer, hops, server, m_flags & FLAG_SORT_HOPS ? 1 : 0, delta);
}

void CWordCache::AddChanged(int changed, int changed1, int unew, int changed2)
{
	CLocker lock(&m_mutexs);
	m_changed += changed;
	m_changed1 += changed1;
	m_new += unew;
	m_changed2 += changed2;
}

// This function writes various indexing statistics every 100 seconds
void CWordCache::AddSize(int size)
{
	struct timeval tm;
	double dif;
	ULONG psize, pdocs, ch, ch1, ch2, unew, phits = 0, pmiss = 0, pins = 0, hq = 0, hhits = 0, hlost = 0;
	double sqlRead=0, sqlWrite=0, sqlUpdate=0, hrefs=0, origin=0;
	{
		CLocker lock(&m_mutexs);
		m_totalSize += size;
		psize = (m_partSize += size);
		pdocs = ++m_partDocs;
		ch = m_changed;
		ch1 = m_changed1;
		ch2 = m_changed2;
		unew = m_new;
		gettimeofday(&tm, NULL);
		if ((dif = timedif(tm, m_lastSave)) > 100)
		{
			m_lastSave = tm;
			m_partSize = 0;
			m_partDocs = 0;
			m_changed = 0;
			m_changed1 = 0;
			m_changed2 = 0;
			m_new = 0;
			sqlRead = m_sqlRead;
			sqlWrite = m_sqlWrite;
			sqlUpdate = m_sqlUpdate;
			hrefs = m_hrefs;
			origin = m_origin;
			phits = m_hits;
			pmiss = m_lost;
			pins = m_inserts;
			hq = StoredHrefs.m_queries;
			hhits = StoredHrefs.m_hits;
			hlost = StoredHrefs.m_lost;
			StoredHrefs.m_queries = 0;
			StoredHrefs.m_hits = 0;
			StoredHrefs.m_lost = 0;

			m_hits = m_lost = 0, m_inserts = 0;
			m_sqlRead = m_sqlWrite = m_sqlUpdate = m_hrefs = m_origin = 0;
		}
	}
	if ((dif > 100) && (m_database->m_logFile))
	{
		if (((m_logWrites++ % 10) == 0) || (m_database->m_got))
		{
			fprintf(m_database->m_logFile, "%8s %5s %5s %5s %5s %5s %10s %8s %8s %7s %6s %6s %6s\n",
			"Sec", "Count", "Ch",  "Ch1", "Ch2", "New", "Size", "HQ", "Hr hits", "HR lost", "W hit","W miss", "W ins");
			m_database->m_got = 0;
		}
		fprintf(m_database->m_logFile, "% 7.3f %5lu %5lu %5lu %5lu %5lu %10lu %8lu %8lu %7lu %6lu %6lu %6lu\n",
			dif, pdocs, ch, ch1, ch2, unew, psize, hq, hhits, hlost, phits, pmiss, pins);
		Filters.m_time = 0;
		fflush(m_database->m_logFile);
	}
}

void CWordCache::DeleteWordsFromURL(ULONG urlID, ULONG siteID, CServer* curSrv)
{
	CWordContent wold;
	BYTE* oldContents;
	ULONG oldwCount;
	char content_type[50];
	string charset;
	unsigned long int uLen;
	ULONG* oldHrefs = NULL;
	ULONG oldHsz;
	// Retrieve old URL content
	ULONG oldLen = m_database->GetContents(urlID, (BYTE**)&oldContents, oldwCount, &oldHrefs, &oldHsz, content_type, charset);
	BYTE* uncompr = Uncompress(oldContents, oldLen, uLen);
	// Create set of words from old content
	wold.Parse(uncompr, content_type, this, curSrv, charset.c_str());

	// Prepare buffers with word IDs for CDeltaFiles::SaveWords
	WORD* buf = new WORD[wold.size() * 3];
	CULONG100* words = new CULONG100[wold.size()];
	CULONG100* pword = words;
	WORD* pbuf = buf;
	for (CWordContent::iterator it = wold.begin(); it != wold.end(); it++)
	{
		pword->m_id = (ULONG*)pbuf;
		pword++;
		*(ULONG*)pbuf = GetWordID(it->Word());
		pbuf[2] = 0;	// SaveWords treats word count of 0, as command to remove word from reverse index for the specified URL
		pbuf += 3;
	}
	// Save info to the delta files
	m_files->SaveWords(words, pword, urlID, siteID);
	if (uncompr - oldContents != 4) delete uncompr;
	delete oldContents;
	if (m_hfiles)
	{
		m_hfiles->LockFiles();
		if (oldHrefs)
		{
			oldHsz /= sizeof(ULONG);
			ULONG* hr = new ULONG[oldHsz];
			ULONG* phr = hr;
			for (ULONG i = 0; i < oldHsz; i++)
			{
				*phr++ = oldHrefs[i] | 0x80000000;
			}
			m_hfiles->SaveHrefs(hr, phr, urlID, siteID);
			delete oldHrefs;
			delete hr;
		}
	}
	delete buf;
	delete words;
}

void CWordCache::AddRedir(CUrlParam* up)
{
	if (m_hfiles)
	{
		if (up->m_oldRedir != up->m_redir)
		{
			if (up->m_redir)
			{
				m_hfiles->AddRedir(up->m_urlID, up->m_redir);
			}
			if (up->m_oldRedir)
			{
				m_hfiles->AddRedir(up->m_urlID | 0x80000000, up->m_oldRedir);
			}
		}
	}
}

#ifdef UNICODE
typedef CUWord CCWord;
#else
typedef CWord CCWord;
#endif

/* test stuff - commented out

typedef struct
{
	CWordCache* wordCache;
	vector<CCWord*> words;
	vector<ULONG> wordIDs;
} TestC;

typedef struct
{
	TestC* test;
	ULONG step;
} TestS;

void* TestCache1(void* ptr)
{
	TestS* testS = (TestS*)ptr;
	TestC* test = testS->test;
	int sz = test->words.size();
	int ind = 0;
	for (int i = 0; i < sz; i++)
	{
		const CHAR* word = test->words[ind]->Word();
		ULONG awordID = test->wordIDs[ind];
		ind += testS->step;
		if (ind >= sz) ind -= sz;
		ULONG wordID = test->wordCache->GetWordID(word);
		if (wordID != awordID)
		{
			printf("Error, actual ID: %lu, cached %lu\n", awordID, wordID);
		}
	}
	// FIXME!!!
	return NULL;
}

void PrintWord(const WORD* w)
{
	while (*w)
	{
		printf("%04x ", *w++);
	}
	printf("\n");
}

#define NUM_THREADS 50

void TestCacheN(CSQLDatabaseI* database)
{
	CWordCache cache(WordCacheSize);
	cache.m_database = database;
	TestC test;
	test.wordCache = &cache;
	TestS tests[NUM_THREADS];
	ULONG primes[NUM_THREADS];
	primes[0] = 1;
	primes[1] = 2;
	CSQLQuery *sqlquery = database->m_sqlquery->SetQuery("SELECT word, word_id from wordurl LIMIT 1000");
	CSQLAnswer* answ = database->sql_query(sqlquery);
	while (answ->FetchRow())
	{
		ULONG wordID;
		answ->GetColumn(1, wordID);
#ifdef UNICODE
		WORD ww[MAX_WORD_LEN + 1];
		ULONG len = answ->GetLength(0);
		unsigned char* pword = (unsigned char*)answ->GetColumn(0);
		if (HiLo)
		{
			for (ULONG i = 0; i < len >> 1; i++, pword += 2)
			{
				ww[i] = (pword[0] << 8) | pword[1];
			}
		}
		else
		{
			memcpy(ww, pword, len);
		}
		ww[len >> 1] = 0;
		test.words.push_back(new CCWord(ww));
		test.wordIDs.push_back(wordID);
#else
		test.words.push_back(new CCWord(answ->GetColumn(0)));
		test.wordIDs.push_back(wordID);
#endif
	}
	delete answ;
	pthread_t threads[NUM_THREADS];
	int n;
	ULONG pr = 3;
	for (n = 2; n < NUM_THREADS; n++)
	{
np:
		for (ULONG* ppr = primes + 1; *ppr * *ppr <= pr; ppr++)
		{
			if ((pr % *ppr) == 0)
			{
				pr += 2;
				goto np;
			}
		}
		primes[n] = pr;
		pr += 2;
	}
	for (n = 0; n < NUM_THREADS; n++)
	{
		tests[n].test = &test;
		tests[n].step = primes[n];
		if (pthread_create(threads + n, NULL, ::TestCache1, tests + n) != 0) break;
	}
	for (int i = 0; i < n; i++)
	{
		void* res;
		pthread_join(threads[i], &res);
	}
	printf("Size: %lu. Queries: %lu, hits: %lu, misses: %lu, waits: %lu, waits of LRU: %lu\n", cache.size(), cache.m_hits + cache.m_lost, cache.m_hits, cache.m_lost, cache.m_waits, cache.m_waits1);
}
*/

CDocument* CWordCache::GetNextDocument()
{
	ULONG urlID;
	int thr = max(m_threadCount, 2);
	do
	{
		int nomore;
		{
			// Try to get URL ID of next document to index
			CLocker lock(&m_database->m_squeue.m_mutex);
			urlID = m_tmpSrc ? m_tmpSrc->GetNextLink() : m_flags & FLAG_SORT_HOPS ? m_database->GetNextLinkH(thr) : m_database->GetNextLink(thr);
			nomore = (m_activeLoads == 0) && (m_database->m_squeue.m_activeSize == 0);
		}
		if (urlID == ULONG(~0))
		{
			// Bandwidth limit reached, wait
			usleep(10000);
			continue;
		}
		if (urlID != 0) break;				// URL ID found
		if (term || nomore)
		{
			return NULL;	// Indexer terminated or no more URLs can be found
		}
		usleep(1000000);					// All available sites are being indexed at this time, wait
	}
	while (true);
	return urlID == 0 ? NULL : m_database->GetDocument(urlID);	// Retrieve document by URL ID
}
/*
void CWordCache::IncActiveLoads()
{
	CLocker lock(this);
	m_activeLoads++;
}
void CWordCache::DecActiveLoads()
{
	CLocker lock(this);
	m_activeLoads--;
}
*/
void CWordCache::CancelConnects()
{
	CLocker lock(&m_mutexc);
	CPidSet::iterator it;
	// Send ALRM signal, to all threads, trying to connect or resolve host name. Works only under Linux
	for (it = m_connecting.begin(); it != m_connecting.end(); it++)
	{
		kill(*it, SIGALRM);
		logger.log(CAT_ALL, L_INFO, "Alarm sent to: %i\n", *it);
	}
	for (it = m_gettingHost.begin(); it != m_gettingHost.end(); it++)
	{
		kill(*it, SIGALRM);
		logger.log(CAT_ALL, L_INFO, "Alarm sent to: %i\n", *it);
	}
}

void CWordCache::BeginConnect()
{
	CLocker lock(&m_mutexc);
	m_connecting.insert(getpid());
}

void CWordCache::EndConnect()
{
	CLocker lock(&m_mutexc);
	m_connecting.erase(getpid());
}

void CWordCache::BeginGetHost(pid_t thread)
{
	CLocker lock(&m_mutexc);
	m_connecting.insert(thread);
}

void CWordCache::EndGetHost(pid_t thread)
{
	CLocker lock(&m_mutexc);
	m_connecting.erase(thread);
}

void InitStack()
{
	char x[0x40000];
	memset(x, 0, sizeof(x));
}

void CWordCache::RealIndex(const char* url, int addtospace)
{
	if (strlen(url) <= MAX_URL_LEN)
	{
		if (m_flags & FLAG_CHECKFILTERS)
		{
			char reason[STRSIZ] = "";
			if (FilterType(url, reason, this) == DISALLOW)
			{
				printf("Status: Filtered. Reason: %s\n", reason);
				return;
			}
		}
		CUrl curURL;
		if (curURL.ParseURL(url))
		{
			logger.log(CAT_ALL, L_WARNING, "Bad URL: %s\n", url);
			return;
		}

		logger.log(CAT_ALL, L_INFO, "Adding URL: %s\n", url);
		CHref href;
		char srv[1000];
		char robots[1000];
		sprintf(srv, "%s://%s/", curURL.m_schema, curURL.m_hostinfo);
		sprintf(robots, "%srobots.txt", srv);
		GetHref(robots, NULL, 0, 0, srv, 86400);
		CUrl rUrl;
		if (rUrl.ParseURL(robots))
		{
			return;
		}
		CDocument* rdoc = m_database->GetDocument(robots);
		if (rdoc)
		{
			// Download and save document to the "real-time" database
			char* content = new char[_max_doc_size + 3];
			rUrl.HTTPGetUrlAndStore(*this, content, _max_doc_size, *rdoc);
			delete rdoc;
		}

		href.m_hops = 0;
		href.m_referrer = 0;
		href.m_delta = -86400;
		// Add URL to the database
		m_database->StoreHref(url, href, srv, NULL);
		CDocument* doc = m_database->GetDocument(url);	// Receive document by URL
		if (doc)
		{
			// Download and save document to the "real-time" database
			char* content = new char[_max_doc_size + 3];
			curURL.HTTPGetUrlAndStore(*this, content, _max_doc_size, *doc);
			m_database->UpdateWordSite(curURL.m_hostinfo, doc->m_siteID);
			if (addtospace) m_database->AddUrlToSpace(url, addtospace);
			delete doc;
			printf("Status: OK\n");
		}
	}
}

void CWordCache::Index()
{
	// Store hrefs, added while reading "Server" commands from config
	m_database->StoreHrefs(m_flags & FLAG_SORT_HOPS);
	string url;
	CDocument* doc;
	// Allocate download buffer
	char* content = new char[_max_doc_size + 3];
	memset(content, 0, _max_doc_size + 3);
	// Process all available documents
	while ((term == 0) && (doc = GetNextDocument()))
	{
		CUrl curURL;
		if (!curURL.ParseURL(doc->m_url.c_str()))
		{
			logger.log(CAT_ALL, L_INFO, "Adding URL: %s\n", doc->m_url.c_str());
			curURL.HTTPGetUrlAndStore(*this, content, _max_doc_size, *doc);
			++curUrl;
		}
		m_database->m_squeue.DeleteURL(doc->m_siteID);
		delete doc;
		if ((npages > 0) && (curUrl >= npages)) break;
	}
	StoredHrefs.clear();
	clear();
	delete content;
}

typedef struct
{
	CWordCache* m_cache;
	int m_thread;
	pthread_t m_threadt;
} ThreadCache;

void* Index1(void* param)
{
	ThreadCache* thch = (ThreadCache*)param;
	return thch->m_cache->Index1(thch->m_thread);
}

void CWordCache::IndexN(int num_threads)
{
	// Store hrefs, added while reading "Server" commands from config
	m_database->StoreHrefs(m_flags & FLAG_SORT_HOPS);
	// Allocate thread parameters
	ThreadCache* threads = (ThreadCache*)alloca(num_threads * sizeof(ThreadCache));
	m_threadCount = num_threads;
	int n;
	m_startTime = now();
	// Initialize thread parameters and start indexing threads
	for (n = 0; n < num_threads; n++)
	{
		threads[n].m_cache = this;
		threads[n].m_thread = n;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		if (thread_stack_size)
			pthread_attr_setstacksize(&attr, thread_stack_size);
		if (pthread_create(&threads[n].m_threadt, &attr,
			::Index1, threads + n) != 0) break;
	}
	// Wait all threads to finish
	for (int i = 0; i < n; i++)
	{
		void* res;
		pthread_join(threads[i].m_threadt, &res);
	}
}

void* CWordCache::Index1(int thread)
{
	CDocument* doc;
	char lurl[200] = "";
	double time=0, etime=0;
	struct timeval tm;
	char* content = NULL;
	while (content == NULL)
	{
		try
		{
			CLocker lock(this);
			content = new char[_max_doc_size + 3];
		}
		catch(...)
		{
			logger.log(CAT_ALL, L_INFO, "Exception caught in new, thread: %i\n", thread);
			usleep(1000000);
		}
	}
//	InitStack();
	// Process all available documents
	while ((term == 0) && (doc = GetNextDocument()))
	{
		CUrl curURL;
		int size = 0;
		{
			CLocker lock(&m_database->m_squeue.m_mutex);
			m_activeLoads++;
		}
		// Write URL statistics
		logger.log(CAT_ALL, L_INFO, "(%2i %2i %2i %2i %2i %2i %2i %2i) Adding URL: %s\n", thread, m_activeLoads, m_database->m_squeue.m_activeSize, m_database->m_squeue.m_inactiveSize, m_connections, m_database->m_squeue.m_failedConns, m_connecting.size(), resolverList.m_freeCount, doc->m_url.c_str());
		if ((m_flags & FLAG_TMPURL) == 0) m_database->AddTmp(doc->m_urlID, thread);	// Save URL for possible repeat
		if (!curURL.ParseURL(doc->m_url.c_str()))
		{
			strncpy(lurl, doc->m_url.c_str(), sizeof(lurl) - 1);
			size = curURL.HTTPGetUrlAndStore(*this, content, _max_doc_size, *doc);
			time = curURL.m_time.tv_sec + curURL.m_time.tv_usec / 1000000.0;
			etime = curURL.m_etime.tv_sec + curURL.m_etime.tv_usec / 1000000.0;
			curUrl++;
		}
		gettimeofday(&tm, NULL);
		{
			CLocker lock(&m_database->m_squeue.m_mutex);
			CTimerAdd timer(m_hrefs);
			m_activeLoads--;
			if ((m_flags & FLAG_TMPURL) == 0) m_database->m_squeue.DeleteURL(doc->m_siteID, size);
		}
		delete doc;
		if ((npages > 0) && (curUrl >= npages)) break;
		if (size > 100000)
		{
			delete content;
			content = new char[_max_doc_size + 3];
		}
	}
	if (npages > 0)
	{
		CancelConnects();
	}
	delete content;
	logger.log(CAT_ALL, L_INFO, "Ended thread: %i. Start: %13.3f. End: %13.3f-%13.3f. Duration: %8.3f. URL: %s\n", thread, time, etime, tm.tv_sec + tm.tv_usec / 1000000.0, etime - time, lurl);
	return NULL;
}

#ifdef UNICODE
CWordCache::CWordCache(ULONG maxSize) : hash_map<CUWord, CWordInfo>(maxSize)
#else
CWordCache::CWordCache(ULONG maxSize) : hash_map<CWord, CWordInfo>(maxSize)
#endif
{
	pthread_mutex_init(&m_mutex, NULL);
	pthread_mutex_init(&m_mutexs, NULL);
	pthread_mutex_init(&m_mutexw, NULL);
	pthread_mutex_init(&m_mutexc, NULL);
	m_lru = NULL;
	m_queries = m_hits = m_lost = m_inserts = 0;
	m_maxSize = maxSize;
	m_threadCount = 0;

	local_charset = GetDefaultCharset();
	m_activeLoads = 0;
	m_connections = 0;
	m_totalSize = m_totalDocs = 0;
	m_partSize = 0;
	m_partDocs = 0;
	m_changed = 0;
	m_changed1 = 0;
	m_changed2 = 0;
	m_new = 0;
	m_logWrites = 0;
	m_maxfd = 0;
	m_waits = 0;
	m_waits1 = 0;
	m_sqlRead = m_sqlWrite = m_sqlUpdate = m_hrefs = m_origin = 0;
	m_files = NULL;
	m_hfiles = NULL;
	m_tmpSrc = NULL;
	gettimeofday(&m_lastSave, NULL);
}

void CWordCache::SaveHrefs(ULONG* hrefs, ULONG size, ULONG* oldHrefs, ULONG oldSize, ULONG urlID, ULONG siteID)
{
	if (m_hfiles)
	{
		ULONG* hr = new ULONG[size + oldSize];
		ULONG* phr = hr;
		for (ULONG i = 0; i < size; i++)
		{
			*phr++ = hrefs[i];
		}
		if (oldHrefs)
		{
			ULONG* pnew = hrefs;
			ULONG* enew = hrefs + size;
			ULONG* pold = oldHrefs;
			ULONG* eold = oldHrefs + oldSize;
			while (pold < eold)
			{
				while ((pnew < enew) && (*pnew < *pold)) pnew++;
				if ((pnew >= enew) || (*pnew > *pold))
				{
					*phr++ = *pold | 0x80000000;
				}
				pold++;
			}
		}
		m_hfiles->SaveHrefs(hr, phr, urlID, siteID);
		delete hr;
	}
}

CWordCache::~CWordCache()
{
	pthread_mutex_destroy(&m_mutex);
	pthread_mutex_destroy(&m_mutexs);
	pthread_mutex_destroy(&m_mutexw);
	pthread_mutex_destroy(&m_mutexc);
}

void CWordCache::IncrementIndexed(ULONG site_id, const char* url, int new_url)
{
	CLocker lock(&m_database->m_squeue.m_mutex);
	m_database->m_squeue.IncrementIndexed(site_id, url, new_url);
}

int CWordCache::LimitReached(ULONG site_id, const char* url, int new_url)
{
	CLocker lock(&m_database->m_squeue.m_mutex);
	return m_database->m_squeue.LimitReached(site_id, url, new_url);
}

void CWordCache::MarkDeleted(ULONG urlID, ULONG site_id, const char* url, int new_url, int val)
{
	{
		CLocker lock(&m_database->m_squeue.m_mutex);
		m_database->m_squeue.DecrementIndexed(site_id, url, new_url);
	}
	m_database->MarkDeleted(urlID, val);
}
