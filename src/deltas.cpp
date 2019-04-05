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

/*  $Id: deltas.cpp,v 1.35 2002/07/18 14:10:12 al Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CDeltaFiles
*/

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "sqldb.h"
#include "deltas.h"
#include "wcache.h"
#include "index.h"
#include "logger.h"
#include "timer.h"
#include "lastmod.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <algorithm>
#include "delmap.h"

extern int thread_stack_size;

using std::sort;

CDeltaFiles* deltaFiles = NULL;
CHrefDeltaFilesR* hdeltaFiles = NULL;
ULONG NumIterations = 10;
int CompactStorage = 1;

CRevFile::CRevFile(int index, int newFlag, char** fnames)
{
	char dir[1000];
	const char* ending = newFlag ? ".1" : "";
	int openMode = newFlag ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY;
	sprintf(dir, "%s/%02iw/", DBWordDir.c_str(), index);
	sprintf(fnames[0], "%sind%s", dir, ending);
	m_io = m_ind.Open(fnames[0], openMode, DeltaBufferSize << 10, 0644) == 0;
	sprintf(fnames[1], "%ssites%s", dir, ending);
	m_so = m_sites.Open(fnames[1], openMode, DeltaBufferSize << 10, 0644) == 0;
	sprintf(fnames[2], "%surls%s", dir, ending);
	m_uo = m_urls.Open(fnames[2], openMode, UrlBufferSize << 10, 0644) == 0;
	if (m_io && m_so && m_uo)
	{
		m_curInd.m_offset = 0;
		m_curWord1 = index;
		m_urlOff = 0;
		m_siteOff = 0;
		m_prevOffset = 0;
		if (!newFlag)
		{
			m_avail = 1;
			ReadNextWord();
			ReadNextSite();
			ReadNextUrl();
		}
	}
	else
	{
		Close();
		m_avail = 0;
	}
}

void CRevFile::ReadNextWord()
{
	if (m_io)
	{
		while (m_avail)
		{
			ULONG pOff = m_curInd.m_offset;
			m_curWord = m_curWord1;
			m_curWord1 += 100;
			m_avail = m_ind.Read(&m_curInd, sizeof(m_curInd)) == sizeof(m_curInd);
			if (!m_avail || (m_curInd.m_offset != pOff)) break;
		}
	}
}

void CRevFile::ReadNextSite()
{
	if (m_so && m_avail)
	{
		m_avail = m_sites.Read(&m_curSite, sizeof(m_curSite)) == sizeof(m_curSite);
		m_siteOff += sizeof(m_curSite);
	}
}

void CRevFile::ReadNextUrl()
{
	if (m_uo && m_avail)
	{
		m_avail = m_urls.Read(&m_curUrl, URLINDSIZE) == URLINDSIZE;
		m_urlOff += URLINDSIZE;
	}
}

void CRevFile::SkipUrlContent()
{
	if (m_uo && m_avail)
	{
		WORD count = m_curUrl.m_count & 0x3FFF;
		m_urls.Seek(count * sizeof(WORD), SEEK_CUR);
		m_urlOff += count * sizeof(WORD);
	}
}

void CRevFile::CopyToUrl(ULONG wordID, ULONG siteID, ULONG urlID, CRevFile& output)
{
	if (m_avail && (m_curWord == wordID) && (m_curSite.m_siteID == siteID))
	{
		while (m_avail && (m_siteOff <= m_curInd.m_offset) && (m_urlOff < m_curSite.m_offset) && (m_curUrl.m_urlID < urlID))
		{
			WORD buf[0x10000];
			WORD count = m_curUrl.m_count & 0x3FFF;
			WORD acnt = m_urls.Read(buf, sizeof(WORD) * count) / sizeof(WORD);
			m_urlOff += sizeof(WORD) * count;
			if (!DelMap.IsSet(m_curUrl.m_urlID))
			{
				output.m_urls.Write(&m_curUrl, URLINDSIZE);
				output.m_urlOff += URLINDSIZE;
				output.m_urls.Write(buf, acnt * sizeof(WORD));
				output.m_urlOff += sizeof(WORD) * acnt;
				output.m_curInd.m_totalCount += acnt;
				output.m_curInd.m_urlCount++;
			}
			ReadNextUrl();
		}
	}
}

void CRevFile::CopyToSite(ULONG wordID, ULONG siteID, CRevFile& output)
{
	if (m_avail && (m_curWord == wordID))
	{
		while (m_avail && (m_siteOff <= m_curInd.m_offset) && (m_curSite.m_siteID < siteID))
		{
			ULONG off = output.m_urlOff;
			while (m_avail && (m_urlOff < m_curSite.m_offset))
			{
				WORD buf[0x10000];
				WORD count = m_curUrl.m_count & 0x3FFF;
				WORD acnt = m_urls.Read(buf, count * sizeof(WORD)) / sizeof(WORD);
				m_urlOff += acnt * sizeof(WORD);
				if (!DelMap.IsSet(m_curUrl.m_urlID))
				{
					output.m_urls.Write(&m_curUrl, URLINDSIZE);
					output.m_urlOff += URLINDSIZE;
					output.m_urls.Write(buf, acnt * sizeof(WORD));
					output.m_urlOff += acnt * sizeof(WORD);
					output.m_curInd.m_totalCount += acnt;
					output.m_curInd.m_urlCount++;
				}
				ReadNextUrl();
			}
			if (output.m_urlOff > off)
			{
				output.WriteSite(m_curSite.m_siteID);
			}
			ReadNextSite();
		}
	}
}

void CRevFile::CopyToWord(ULONG wordID, CRevFile& output)
{
	while (m_avail && (m_curWord < wordID))
	{
		CopyToSite(m_curWord, 0xFFFFFFFF, output);
//		m_curInd.m_offset = output.m_siteOff;
		output.WriteTill(m_curWord);
		output.m_curInd = m_curInd;
		output.m_curInd.m_offset = output.m_siteOff;
//		fwrite(&output.m_curInd, sizeof(m_curInd), 1, output.m_ind);
		output.m_ind.Write(&output.m_curInd, sizeof(m_curInd));
		output.m_prevOffset = output.m_siteOff;
		output.m_curWord1 += 100;
		m_prevOffset = output.m_siteOff;
		ReadNextWord();
	}
}

void CRevFile::WriteTill(ULONG wordID)
{
	while (m_curWord1 < wordID)
	{
		WordInd wi;
		wi.m_offset = m_prevOffset;
		wi.m_siteCount = 0;
		wi.m_urlCount = 0;
		wi.m_totalCount = 0;
		m_ind.Write(&wi, sizeof(wi));
		m_curWord1 += 100;
	}
}

void CRevFile::Write(ULONG urlID, ULONG* ppos, ULONG len)
{
	if (!DelMap.IsSet(urlID))
	{
		ULONG len1 = (len + 1) * sizeof(WORD);
		m_urls.Write(&urlID, sizeof(ULONG));
		m_urls.Write(ppos, len1);
		m_urlOff += len1 + sizeof(ULONG);
		m_curInd.m_totalCount += len;
		m_curInd.m_urlCount++;
	}
}

void CRevFile::WriteSite(ULONG siteID)
{
	m_curInd.m_siteCount++;
	m_sites.Write(&siteID, sizeof(ULONG));
	m_sites.Write(&m_urlOff, sizeof(ULONG));
	m_siteOff += (sizeof(ULONG) << 1);
}

void CheckBefore(CRevFile& input, ULONG wordID, ULONG& prevOff)
{
	while ((input.m_avail) && (input.m_curWord < wordID))
	{
		if (input.m_curInd.m_offset != prevOff)
		{
			logger.log(CAT_ALL, L_DEBUG, "Extra word: %lu\n", input.m_curWord);
			prevOff = input.m_curInd.m_offset;
		}
		input.ReadNextWord();
	}
}

void CheckWord(CRevFile& input, ULONG urlCount, ULONG totalCount, char* urls, int len, ULONG& prevOffset, ULONG& prevsOffset)
{
	if (urlCount != input.m_curInd.m_urlCount)
	{
		printf("Wrong URL count: %lu-%lu for word: %lu\n", urlCount, input.m_curInd.m_urlCount, input.m_curWord);
	}
	if (totalCount != input.m_curInd.m_totalCount)
	{
		printf("Wrong total count: %lu-%lu for word: %lu\n", totalCount, input.m_curInd.m_totalCount, input.m_curWord);
	}
	int del = 0;
	if (len == 0)
	{
		char name[1000];
		sprintf(name, "%s/%02luw/%lu", DBWordDir.c_str(), input.m_curWord % 100, input.m_curWord);
		int file = open(name, O_RDONLY);
		if (file < 0)
		{
			printf("Can't open file: %s\n", name);
			return;
		}
		len = lseek(file, 0, SEEK_END);
		lseek(file, 0, SEEK_SET);
		urls = new char[len];
		read(file, urls, len);
		close(file);
		del = 1;
	}
	ULONG* dsites = (ULONG*)urls;
	ULONG dslen = dsites[0];
	ULONG* dse = (ULONG*)(urls + dslen);
//	fseek(input.m_sites, prevOffset, SEEK_SET);
	input.m_sites.Seek(prevOffset, SEEK_SET);
	ULONG slen = input.m_curInd.m_offset - prevOffset;
	ULONG* sites = (ULONG*)alloca(slen);
//	fread(sites, 1, slen, input.m_sites);
	input.m_sites.Read(sites, slen);
	ULONG nsOffset;
	nsOffset = slen > 0 ? sites[(slen >> 2) - 1] : prevsOffset;
	if (slen + sizeof(ULONG) != dslen)
	{
		printf("Site length discrepancy for word: %lu, %lu-%lu\n", input.m_curWord, dslen, slen + (ULONG)sizeof(ULONG));
	}
	else
	{
		ULONG* ds;
		ULONG* s;
		for (ds = dsites + 1, s = sites; ds < dse; ds += 2, s += 2)
		{
			if (s[0] != ds[0])
			{
				printf("Wrong site for word: %lu, %lu-%lu\n", input.m_curWord, ds[0], s[0]);
			}
			if (s[1] - prevsOffset != ds[1] - dslen)
			{
				printf("Wrong URL offset for word: %lu, site: %lu, %lu-%lu\n", input.m_curWord, ds[0], ds[1] - dslen, s[1] - prevsOffset);
			}
		}
		ULONG ulen = nsOffset - prevsOffset;
		if (ulen != len - dslen)
		{
			printf("URL length discrepancy for word: %lu, %lu-%lu\n", input.m_curWord, len - dslen, ulen);
		}
		else
		{
			char* nurls = new char[ulen];
//			fseek(input.m_urls, prevsOffset, SEEK_SET);
			input.m_urls.Seek(prevsOffset, SEEK_SET);
//			fread(nurls, 1, ulen, input.m_urls);
			input.m_urls.Read(nurls, ulen);
			if (memcmp(nurls, urls + dslen, ulen) != 0)
			{
				printf("Wrong URLs for word: %lu\n", input.m_curWord);
			}
			delete nurls;
		}
	}
	prevsOffset = nsOffset;
	prevOffset += slen;
	if (del) delete urls;
}

void CheckDeltas(CSQLAnswer* answ, int nfile, int& avail)
{
	char inamei[1000], inamest[1000], inameu[1000];
	char* inames[] = {inamei, inamest, inameu};
	CRevFile input(nfile, 0, inames);
	if (!(input.m_io && input.m_uo && input.m_so))
	{
		printf("Reverse files are absent\n");
		return;
	}
	ULONG prevOff = 0;
	ULONG prevsoff = 0;
	int c = 0;
	while (1)
	{
		ULONG wordID;
		if (!avail) break;
		answ->GetColumn(0, wordID);
		if ((int)(wordID % 100) > nfile) break;
		CheckBefore(input, wordID, prevOff);
		if (input.m_curWord == wordID)
		{
			c++;
			ULONG uc, tc;
			answ->GetColumn(1, uc);
			answ->GetColumn(2, tc);
			CheckWord(input, uc, tc, answ->GetColumn(3), answ->GetLength(3), prevOff, prevsoff);
			input.ReadNextWord();
		}
		else
		{
			printf("Word is absent: %lu\n", wordID);
		}
		avail = answ->FetchRow();
	}
	CheckBefore(input, 0xFFFFFFFF, prevOff);
	printf("Checked %i words\n", c);
}

void CheckDeltas(CSQLDatabase* database)
{
	CSQLQuery* query = database->m_sqlquery->SetQuery("SELECT word_id, urlcount, totalcount, urls FROM wordurl where urlcount > 0 order by mod(word_id,100), word_id");
	CSQLAnswer* answ = database->sql_query(query);
	int avail = answ->FetchRow();
	for (int i = 0; i < 100; i++)
	{
		printf("Checking delta: %i...\n", i);
		::CheckDeltas(answ, i, avail);
	}
	if (answ)
	{
		delete answ;
	}
}

CDeltaFiles::CDeltaFiles(int checkDeltas)
{
	int deltas = checkDeltas ? CheckDeltas() : 0;
	if (deltas)
	{
		logger.log(CAT_FILE, L_ERR, "Index has not been stopped safely, checking of delta files will be performed now\n");
	}
	for (ULONG i = 0; i < sizeof(m_files) / sizeof(m_files[0]); i++)
	{
		char name[1000];
		pthread_mutex_init(m_mutex + i, NULL);
		sprintf(name, "%s/deltas/d%02lu", DBWordDir.c_str(), i);
		if (!(m_files[i] = fopen(name, deltas ? "r+" : "a+")))
		{
			if (!(m_files[i] = fopen(name, "a+")))
			{
				logger.log(CAT_FILE, L_ERR, "Can't open delta file %s: %s\n", name, strerror(errno));
				exit(1);
			}
		}
		else
		{
			if (deltas)
			{
				logger.log(CAT_FILE, L_INFO, "Checking delta file: %i...", i);
				CheckDelta(m_files[i]);
				logger.log(CAT_FILE, L_INFO, "done\n");
			}
		}
		if (checkDeltas)
		{
			m_buf[i] = new char[DeltaBufferSize * 1024];
			memset(m_buf[i], 0, DeltaBufferSize * 1024);
			setvbuf(m_files[i], m_buf[i], _IOFBF, DeltaBufferSize * 1024);
		}
		else
		{
			m_buf[i] = NULL;
			setvbuf(m_files[i], NULL, _IONBF, 0);
		}
	}
	deltaFiles = this;
}

void CDeltaFiles::DeleteBuffers()
{
	for (ULONG i = 0; i < sizeof(m_files) / sizeof(m_files[0]); i++)
	{
		if (m_buf[i])
		{
			setvbuf(m_files[i], NULL, _IONBF, 0);
			delete m_buf[i];
			m_buf[i] = NULL;
		}
	}
}

CDeltaFiles::~CDeltaFiles()
{
	deltaFiles = NULL;
	for (ULONG i = 0; i < sizeof(m_files) / sizeof(m_files[0]); i++)
	{
		fclose(m_files[i]);
		pthread_mutex_destroy(m_mutex + i);
		if (m_buf[i])
			delete m_buf[i];
	}
	CloseDeltas();
}

void CDeltaFiles::SaveWords(CULONG100* words, CULONG100* ewords, ULONG urlID, ULONG siteID)
{
	sort<CULONG100*>(words, ewords);
	while (words < ewords)
	{
		ULONG f = (*words->m_id) % 100;
		CULONG100* wrd = words;
		WORD cnt = 0;
		while ((wrd < ewords) && (f == ((*wrd->m_id) % 100)))
		{
			cnt++;
			wrd++;
		}
		FILE* file = m_files[f];
		CLocker lock(this, f);
		fwrite(&siteID, 1, sizeof(siteID), file);
		fwrite(&urlID, 1, sizeof(siteID), file);
		fwrite(&cnt, 1, sizeof(cnt), file);
		for (WORD i = 0; i < cnt; i++)
		{
			fwrite(words->m_id, 2, (((WORD*)words->m_id)[2] & 0x3FFF) + 3, file);
			words++;
		}
	}
}

void CDeltaFiles::Save(CSQLDatabase *database)
{
	CTimer ttimer("Total time : ");
	DeleteBuffers(); // to save some memory
	logger.log(CAT_ALL, L_INFO, "Saving real-time database ...");
	database->SaveDelta(this);
	logger.log(CAT_ALL, L_INFO, " done.\n");
	logger.log(CAT_ALL, L_INFO, "Saving delta files");
	if (logger.getLevel() < L_DEBUG)
		logger.log(CAT_ALL, L_INFO, " [");
	else
		logger.log(CAT_ALL, L_DEBUG, "\n");
	double totalReadTime = 0, totalSortTime = 0;
	ULONG maxMem = 0;
	for (int i = 0; i < 100; i++)
	{
		ULONG wcount;
		if (logger.getLevel() < L_DEBUG)
		{
			if (i % 2)
				logger.log(CAT_ALL, L_INFO, ".");
		}
		else
			logger.log(CAT_ALL, L_DEBUG, "Saving delta file %3i ...", i);
		{
			// nested block for timer
			CTimer timer("took:");
			double sortTime, readTime;
			ULONG memory;
			wcount = CompactStorage ? Save(i, readTime, sortTime, memory) : Save(i, database, readTime, sortTime, memory);
			totalReadTime += readTime;
			totalSortTime += sortTime;
			if (memory > maxMem) maxMem = memory;
			if (logger.getLevel() >= L_DEBUG)
			{
				logger.log(CAT_ALL, L_DEBUG, " done (saved %lu words).\nRead time: %7.3f, sort time: %7.3f, memory: %lu. ", wcount, readTime, sortTime, memory);
			}
		}
	}

	if (logger.getLevel() < L_DEBUG)
	{
		logger.log(CAT_ALL, L_INFO, "] done.\n");
	}
	else
	{
		logger.log(CAT_ALL, L_INFO, "Total read time: %8.2f, sort time: %8.2f, max memory: %lu\n", totalReadTime, totalSortTime, maxMem);
	}

//	::CheckDeltas(database);

	if (CompactStorage)
	{
		logger.log(CAT_ALL, L_INFO, "Deleting 'deleted' records from urlword[s] ...");
		ULONG urlID1 = 0;
		char* param = new char[100000];
		int tcnt = 0;
		for (ULONG** dm = DelMap.m_chunks; dm < DelMap.m_chunks + DELMAP_SIZE; dm++)
		{
			if (*dm)
			{
				ULONG urlID = urlID1;
				for (ULONG* d = *dm; d < *dm + (DEL_CHUNK_SIZE / sizeof(ULONG)); d += 0x100)
				{
					ULONG urlID0 = urlID;
					char* pq;
					int cnt = 0;
					param[0] = 0;
					pq = param;
					for (ULONG* d1 = d; d1 < d + 0x100; d1++)
					{
						if (*d1)
						{
							for (ULONG m = 1; m; m <<= 1, urlID++)
							{
								if (*d1 & m)
								{
									pq += sprintf(pq, "%lu,", urlID);
									cnt++;
								}
							}
						}
						else
						{
							urlID += (sizeof(ULONG) << 3);
						}
					}
					if (cnt)
					{
						tcnt += cnt;
						pq[-1] = 0;
						CSQLParam p;
						p.AddParam(param);
						database->sql_query1(database->m_sqlquery->SetQuery("DELETE FROM urlword WHERE deleted!=0 AND url_id IN(%s)", &p));
						ULONG mask = 1;
						for (ULONG w = 0; w < 16; w++, mask <<= 1)
						{
							ULONG urlID2 = urlID0 + w;
							cnt = 0;
							param[0] = 0;
							pq = param;
							for (ULONG* d1 = d; d1 < d + 0x100; d1++, urlID2 += (sizeof(ULONG) << 3))
							{
								if (*d1 & mask)
								{
									pq += sprintf(pq, "%lu,", urlID2);
									cnt++;
								}
								if (*d1 & (mask << 16))
								{
									pq += sprintf(pq, "%lu,", urlID2 + 16);
									cnt++;
								}
							}
							if (cnt)
							{
								pq[-1] = 0;
								char query[100];
								sprintf(query, "DELETE FROM urlwords%02lu WHERE deleted!=0 AND url_id IN (%%s)", w);
								CSQLParam p1;
								p1.AddParam(param);
								database->sql_query1(database->m_sqlquery->SetQuery(query, &p1));
							}
						}
					}
				}
			}
			urlID1 += (DEL_CHUNK_SIZE << 3);
		}
		delete param;
		logger.log(CAT_ALL, L_INFO, " done. (%i records deleted)\n", tcnt);
	}


}

ULONG CDeltaFiles::Save(int nfile, CSQLDatabase *database, double& readTime, double& sortTime, ULONG& memory)
{
	FILE* file = m_files[nfile];
	WORD* buf = NULL;
	ULONG wcount = 0;
	readTime = 0;
	sortTime = 0;
	memory = 0;
	ULONG wmem = 0;
	int len = 0;
	{
		CLocker lock(this, nfile);
		CTimerAdd time(readTime);
		fseek(file, 0, SEEK_END);
		len = ftell(file);
		if (len == 0) return 0;
		buf = (WORD*)(new char[len]);
		fseek(file, 0, SEEK_SET);
		fread(buf, 1, len, file);
		fseek(file, 0, SEEK_SET);
		ftruncate(fileno(file), 0);
		memory = len;
	}
	if (buf)
	{
		int cnt = 0;
		WORD* pbuf = buf;
		WORD* ebuf = pbuf + (len >> 1);
		while (pbuf < ebuf)
		{
			if (pbuf + 5 <= ebuf)
			{
				WORD wc = pbuf[4];
				pbuf += 5;
				WORD* ppbuf = pbuf;
				while ((pbuf < ebuf) && (wc-- > 0))
				{
					cnt++;
					ppbuf = pbuf;
					pbuf += (pbuf[2] & 0x3FFF) + 3;
				}
				if (pbuf > ebuf)
				{
					cnt--;
					ebuf = ppbuf;
					break;
				}
			}
			else
			{
				ebuf = pbuf;
				break;
			}
		}
		pbuf = buf;
		memory += cnt * sizeof(CWordSU);
		CWordSU* wsu = new CWordSU[cnt];
		CWordSU* pwsu = wsu;
		while (pbuf < ebuf)
		{
			ULONG* site = (ULONG*)pbuf;
			WORD wc = pbuf[4];
			pbuf += 5;
			while ((pbuf < ebuf) && (wc-- > 0))
			{
				pwsu->m_site = site;
				pwsu->m_word = (ULONG*)pbuf;
				pbuf += (pbuf[2] & 0x3FFF) + 3;
				pwsu++;
			}
		}
		{
			CTimerAdd timer(sortTime);
			sort<CWordSU*>(wsu, pwsu);
		}
		CWordSU* ewsu = pwsu;
		CWordSU* swsu = wsu + 1;
		CWordSU* ppwsu = wsu;
		pwsu = wsu + 1;
		while (pwsu < ewsu)
		{
			if (*pwsu != *ppwsu)
			{
				*swsu++ = *pwsu;
				ppwsu = pwsu;
			}
			pwsu++;
		}
		ewsu = swsu;
		pwsu = wsu;
		while (pwsu < ewsu)
		{
			ULONG word = *pwsu->m_word;
			wcount++;
			CWordSU* pwsu1 = pwsu;
			ULONG sites = 0, urls = 0, totalcount = 0;
			while ((pwsu1 < ewsu) && (*pwsu1->m_word == word))
			{
				ULONG site = pwsu1->m_site[0];
				sites++;
				while ((pwsu1 < ewsu) && (*pwsu1->m_word == word) && (site == pwsu1->m_site[0]))
				{
					urls++;
					totalcount += ((WORD*)pwsu1->m_word)[2] & 0x3FFF;
					pwsu1++;
				}
			}
			ULONG newLen = urls * 6 + totalcount * 2;
			int iSource;
			ULONG* oldContent = NULL;
			ULONG counts[2];
			ULONG oldLen = database->GetUrls(word, (BYTE**)&oldContent, iSource, counts);
			ULONG* pold = oldContent;
			ULONG* eold = oldLen > 0 ? (ULONG*)((BYTE*)pold + pold[0] - sizeof(ULONG)) : pold;
			ULONG* siten = new ULONG[eold - pold + sites * 2 + 1];
			ULONG* sitep = siten;
			WORD* urln = (WORD*)(new char[oldLen + newLen]);
			WORD* urlnp = urln;
			ULONG urlCount = 0, totalCount = 0;
			int aold, anew;
			while (aold = pold < eold, anew = (pwsu < ewsu) && (*pwsu->m_word == word), aold || anew)
			{
				WORD* urlnpo = urlnp;
				ULONG site;
				if (aold && anew)
				{
					if (pold[1] == pwsu->m_site[0])
					{
//						*sitep++ = pold[1];
						site = pold[1];
						WORD* oldu = (WORD*)((BYTE*)oldContent + pold[0]);
						WORD* olde = (WORD*)((BYTE*)oldContent + pold[2]);
						ULONG site = pwsu->m_site[0];
						int aoldu, anewu;
						while (aoldu = oldu < olde, anewu = (pwsu < ewsu) && (*pwsu->m_word == word) && (pwsu->m_site[0] == site), aoldu || anewu)
						{
							if (aoldu && anewu)
							{
								if (*(ULONG*)oldu == pwsu->m_site[1])
								{
									WORD len = ((WORD*)pwsu->m_word)[2] & 0x3FFF;
									if (len > 0)
									{
										*(ULONG*)urlnp = pwsu->m_site[1];
										urlCount++;
										totalCount += len;
										memcpy(urlnp + 2, pwsu->m_word + 1, (len + 1) * 2);
										urlnp += len + 3;
									}
									oldu += (oldu[2] & 0x3FFF) + 3;
									pwsu++;
								}
								else if (*(ULONG*)oldu < pwsu->m_site[1])
								{
									int len = (oldu[2] & 0x3FFF) + 3;
									urlCount++;
									totalCount += oldu[2] & 0x3FFF;
									memcpy(urlnp, oldu, len * 2);
									urlnp += len;
									oldu += len;
								}
								else
								{
									WORD len = ((WORD*)pwsu->m_word)[2] & 0x3FFF;
									if (len > 0)
									{
										*(ULONG*)urlnp = pwsu->m_site[1];
										urlCount++;
										totalCount += len;
										memcpy(urlnp + 2, pwsu->m_word + 1, (len + 1) * 2);
										urlnp += len + 3;
									}
//									oldu += oldu[2] + 3;
									pwsu++;
								}
							}
							else if (aoldu)
							{
								int len = (oldu[2] & 0x3FFF) + 3;
								urlCount++;
								totalCount += oldu[2] & 0x3FFF;
								memcpy(urlnp, oldu, len * 2);
								urlnp += len;
								oldu += len;
							}
							else
							{
								WORD len = ((WORD*)pwsu->m_word)[2] & 0x3FFF;
								if (len > 0)
								{
									*(ULONG*)urlnp = pwsu->m_site[1];
									urlCount++;
									totalCount += len;
									memcpy(urlnp + 2, pwsu->m_word + 1, (len + 1) * 2);
									urlnp += len + 3;
								}
//								oldu += oldu[2] + 3;
								pwsu++;
							}
						}
						pold += 2;
					}
					else if (pold[1] < pwsu->m_site[0])
					{
//						*sitep++ = pold[1];
						site = pold[1];
						WORD* purl = (WORD*)((BYTE*)oldContent + pold[0]);
						WORD* eurl = (WORD*)((BYTE*)oldContent + pold[2]);
						while (purl < eurl)
						{
							urlCount++;
							totalCount += purl[2] & 0x3FFF;
							purl += (purl[2] & 0x3FFF) + 3;
						}
						memcpy(urlnp, (BYTE*)oldContent + pold[0], pold[2] - pold[0]);
						urlnp += ((pold[2] - pold[0]) >> 1);
						pold += 2;
					}
					else
					{
//						ULONG site = pwsu->m_site[0];
//						*sitep++ = site;
						site = pwsu->m_site[0];
						while ((pwsu < ewsu) && (*pwsu->m_word == word) && (pwsu->m_site[0] == site))
						{
							WORD len = ((WORD*)pwsu->m_word)[2] & 0x3FFF;
							if (len > 0)
							{
								*(ULONG*)urlnp = pwsu->m_site[1];
								urlCount++;
								totalCount += len;
								memcpy(urlnp + 2, pwsu->m_word + 1, (len + 1) * 2);
								urlnp += len + 3;
							}
							pwsu++;
						}
					}
				}
				else if (aold)
				{
//					*sitep++ = pold[1];
					site = pold[1];
					WORD* purl = (WORD*)((BYTE*)oldContent + pold[0]);
					WORD* eurl = (WORD*)((BYTE*)oldContent + pold[2]);
					while (purl < eurl)
					{
						urlCount++;
						totalCount += purl[2] & 0x3FFF;
						purl += (purl[2] & 0x3FFF) + 3;
					}
					memcpy(urlnp, (BYTE*)oldContent + pold[0], pold[2] - pold[0]);
					urlnp += ((pold[2] - pold[0]) >> 1);
					pold += 2;
				}
				else
				{
					site = pwsu->m_site[0];
//					*sitep++ = site;
					while ((pwsu < ewsu) && (*pwsu->m_word == word) && (pwsu->m_site[0] == site))
					{
						WORD len = ((WORD*)pwsu->m_word)[2] & 0x3FFF;
						if (len > 0)
						{
							*(ULONG*)urlnp = pwsu->m_site[1];
							urlCount++;
							totalCount += len;
							memcpy(urlnp + 2, pwsu->m_word + 1, (len + 1) * 2);
							urlnp += len + 3;
						}
						pwsu++;
					}
				}
				if (urlnp > urlnpo)
				{
					*sitep++ = (BYTE*)urlnpo - (BYTE*)urln;
					*sitep++ = site;
				}
			}
			*sitep++ = (BYTE*)urlnp - (BYTE*)urln;
			ULONG sitelen = (sitep - siten) << 2;
			for (ULONG* sitep1 = siten; sitep1 < sitep; sitep1 += 2)
			{
				*sitep1 += sitelen;
			}
			ULONG umem = (BYTE*)urlnp - (BYTE*)urln;
			ULONG smem = (BYTE*)sitep - (BYTE*)siten;
			database->SaveWord(word, (BYTE*)urln, umem, (BYTE*)siten, smem, urlCount, totalCount, iSource);
			umem += smem + oldLen;
			if (umem > wmem) wmem = umem;
			delete urln;
			delete siten;
			if (oldContent) delete oldContent;
		}
		delete wsu;
		delete buf;
	}
	memory += wmem;
	return wcount;
}

ULONG CDeltaFiles::Save(int nfile, double& readTime, double& sortTime, ULONG& memory)
{
	FILE* file = m_files[nfile];
	WORD* buf = NULL;
	ULONG wcount = 0;
	readTime = 0;
	sortTime = 0;
	memory = 0;
//	ULONG wmem = 0;
	int len = 0;
	{
		CLocker lock(this, nfile);
		CTimerAdd time(readTime);
		fseek(file, 0, SEEK_END);
		len = ftell(file);
		if ((len == 0) && (DelMap.m_empty)) return 0;
		if (len != 0)
		{
			buf = (WORD*)(new char[len]);
			fseek(file, 0, SEEK_SET);
			fread(buf, 1, len, file);
			fseek(file, 0, SEEK_SET);
			ftruncate(fileno(file), 0);
			memory = len;
		}
	}
	if (buf || (!DelMap.m_empty))
	{
		char inamei[1000], inamest[1000], inameu[1000];
		char onamei[1000], onamest[1000], onameu[1000];
		char* inames[] = {inamei, inamest, inameu};
		char* onames[] = {onamei, onamest, onameu};
		CRevFile input(nfile, 0, inames);
		CRevFile output(nfile, 1, onames);
		if (buf)
		{
		int cnt = 0;
		WORD* pbuf = buf;
		WORD* ebuf = pbuf + (len >> 1);
		while (pbuf < ebuf)
		{
			if (pbuf + 5 <= ebuf)
			{
				WORD wc = pbuf[4];
				pbuf += 5;
				WORD* ppbuf = pbuf;
				while ((pbuf < ebuf) && (wc-- > 0))
				{
					cnt++;
					ppbuf = pbuf;
					pbuf += (pbuf[2] & 0x3FFF) + 3;
				}
				if (pbuf > ebuf)
				{
					cnt--;
					ebuf = ppbuf;
					break;
				}
			}
			else
			{
				ebuf = pbuf;
				break;
			}
		}
		pbuf = buf;
		memory += cnt * sizeof(CWordSU);
		CWordSU* wsu = new CWordSU[cnt];
		CWordSU* pwsu = wsu;
		while (pbuf < ebuf)
		{
			ULONG* site = (ULONG*)pbuf;
			WORD wc = pbuf[4];
			pbuf += 5;
			while ((pbuf < ebuf) && (wc-- > 0))
			{
				pwsu->m_site = site;
				pwsu->m_word = (ULONG*)pbuf;
				pbuf += (pbuf[2] & 0x3FFF) + 3;
				pwsu++;
			}
		}
		{
			CTimerAdd timer(sortTime);
			sort<CWordSU*>(wsu, pwsu);
		}
		CWordSU* ewsu = pwsu;
		CWordSU* swsu = wsu + 1;
		CWordSU* ppwsu = wsu;
		pwsu = wsu + 1;
		while (pwsu < ewsu)
		{
			if (*pwsu != *ppwsu)
			{
				*swsu++ = *pwsu;
				ppwsu = pwsu;
			}
			pwsu++;
		}
		ewsu = swsu;
		pwsu = wsu;
		while (pwsu < ewsu)
		{
			wcount++;
			ULONG wordID = *pwsu->m_word;
			input.CopyToWord(wordID, output);
			output.m_curInd.m_siteCount = 0;
			output.m_curInd.m_urlCount = 0;
			output.m_curInd.m_totalCount = 0;
			while ((pwsu < ewsu) && (wordID == *pwsu->m_word))
			{
				ULONG siteID = pwsu->Site();
				input.CopyToSite(wordID, siteID, output);
				ULONG off = output.m_urlOff;
				while ((pwsu < ewsu) && (wordID == *pwsu->m_word) && (siteID == pwsu->Site()))
				{
					ULONG urlID = pwsu->URL();
					input.CopyToUrl(wordID, siteID, urlID, output);
					WORD len = ((WORD*)pwsu->m_word)[2] & 0x3FFF;
					if (len > 0)
					{
						output.Write(pwsu->URL(), pwsu->m_word + 1, len);
					}
					if ((input.m_curWord == wordID) && (input.m_siteOff <= input.m_curInd.m_offset) && (input.m_curSite.m_siteID == siteID) && (input.m_urlOff < input.m_curSite.m_offset) && (input.m_curUrl.m_urlID == urlID))
					{
						input.SkipUrlContent();
						input.ReadNextUrl();
					}
					pwsu++;
				}
				input.CopyToUrl(wordID, siteID, 0xFFFFFFFF, output);
				if (output.m_urlOff > off)
				{
					output.WriteSite(siteID);
				}
				if ((input.m_curWord == wordID) && (input.m_siteOff <= input.m_curInd.m_offset) && (input.m_curSite.m_siteID == siteID)) input.ReadNextSite();
			}
			input.CopyToSite(wordID, 0xFFFFFFFF, output);
			output.WriteTill(wordID);
			output.m_curInd.m_offset = output.m_siteOff;
			output.m_ind.Write(&output.m_curInd, sizeof(output.m_curInd));
			output.m_prevOffset = output.m_siteOff;
			output.m_curWord1 += 100;
			if (input.m_curWord == wordID) input.ReadNextWord();
		}
		delete wsu;
		delete buf;
		}
		input.CopyToWord(0xFFFFFFFF, output);
		input.Close();
		output.Close();
		unlink(inamei);
		unlink(inamest);
		unlink(inameu);
		rename(onamei, inamei);
		rename(onamest, inamest);
		rename(onameu, inameu);
	}
	return wcount;
}

void CDeltaFiles::Flush()
{
	for (int i = 0; i < 100; i++)
	{
		fflush(m_files[i]);
	}
}

int CDeltaFiles::CheckDeltas()
{
	// Open "delta" file and lock it
	char fname[1000];
	sprintf(fname, "%s/delta", DBWordDir.c_str());
	m_deltaFd = open(fname, O_CREAT | O_RDWR, 0600);
	if (m_deltaFd < 0)
		m_deltaFd = open(fname, O_RDWR);
	if (m_deltaFd > 0)
	{
		short delta = 0;
		read(m_deltaFd, &delta, sizeof(delta));
		lseek(m_deltaFd, 0, SEEK_SET);
		short ndelta = 1;
		write(m_deltaFd, &ndelta, sizeof(delta));
		return delta;
	}
	else
	{
		logger.log(CAT_ALL, L_ERR, "Error opening file %s: %s\n", fname, strerror(errno));
		return 1;
	}
}

void CDeltaFiles::CloseDeltas()
{
	if (m_deltaFd > 0)
	{
		short delta = 0;
		lseek(m_deltaFd, 0, SEEK_SET);
		write(m_deltaFd, &delta, sizeof(delta));
		close(m_deltaFd);
	}
//	return 1; // FIXME
}

void CDeltaFiles::CheckDelta(FILE* f)
{
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	WORD* buf = (WORD*)(new char[len]);
	fseek(f, 0, SEEK_SET);
	fread(buf, 1, len, f);
	WORD* pbuf = buf;
	WORD* ebuf = (WORD*)(((char*)buf) + len);
	WORD* ebuf1 = ebuf;
	WORD* pwc = NULL;
	while (pbuf < ebuf)
	{
		if (pbuf + 8 < ebuf)
		{
			pwc = pbuf + 4;
			WORD wc = *pwc;
			WORD rwc = 0;
			pbuf += 5;
			WORD* ppbuf = pbuf;
			while ((pbuf + 3 < ebuf) && (wc-- > 0))
			{
				rwc++;
				ppbuf = pbuf;
				pbuf += (pbuf[2] & 0x3FFF) + 3;
			}
			if (pbuf > ebuf)
			{
				*pwc = rwc - 1;
				ebuf = ppbuf;
				break;
			}
			else if (wc != 0xFFFF)
			{
				*pwc = rwc;
				ebuf = pbuf;
				break;
			}
		}
		else
		{
			pwc = NULL;
			ebuf = pbuf;
			break;
		}
	}
	if (pwc)
	{
		fseek(f, (char*)pwc - (char*)buf, SEEK_SET);
		fwrite(pwc, 1, sizeof(WORD), f);
		logger.log(CAT_FILE, L_INFO, "Last word count: %u at: %i...", *pwc, (char*)pwc - (char*)buf);
	}
	fflush(f);
	if (ebuf != ebuf1)
	{
		int nlen = (char*)ebuf - (char*)buf;
		ftruncate(fileno(f), nlen);
		logger.log(CAT_FILE, L_INFO, "truncated to length: %i...", nlen);
	}
	fseek(f, 0, SEEK_END);
	delete buf;
}

#define HREF_BUF_SIZE 0x80000

CCitationFile::CCitationFile(int index, int newFlag, char* fname, char* fnamec, const char* name)
{
	int openMode = newFlag ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY;
	m_current = index;
	m_curOffset = 0;
	sprintf(fname, "%s/citations/%s%i%s", DBWordDir.c_str(), name, index, newFlag ? ".1" : "");
	sprintf(fnamec, "%s/citations/c%s%i%s", DBWordDir.c_str(), name, index, newFlag ? ".1" : "");
//	m_indCit = fopen(fname, newFlag ? "w" :"r");
//	m_citations = fopen(fnamec, newFlag ? "w" : "r");
	m_io = m_indCit.Open(fname, openMode, HREF_BUF_SIZE, 0664) == 0;
	m_co = m_citations.Open(fnamec, openMode, HREF_BUF_SIZE, 0664) == 0;
//	if (m_indCit && m_citations)
	if (!m_io || !m_co)
//	{
//		m_citBuf = new char[HREF_BUF_SIZE];
//		m_indBuf = new char[HREF_BUF_SIZE];
//		setvbuf(m_indCit, m_indBuf, HREF_BUF_SIZE, _IOFBF);
//		setvbuf(m_citations, m_citBuf, HREF_BUF_SIZE, _IOFBF);
//	}
//	else
	{
		Close();
//		if (m_indCit) fclose(m_indCit);
//		if (m_citations) fclose(m_citations);
//		m_indCit = NULL;
//		m_citations = NULL;
//		m_citBuf = NULL;
//		m_indBuf = NULL;
	}
}

int CCitationFile::GetNextUrl(ULONG& urlID, ULONG& endOffset)
{
//	if (m_indCit)
	if (m_io)
	{
//		int r = fread(&endOffset, sizeof(endOffset), 1, m_indCit);
		int r = m_indCit.Read(&endOffset, sizeof(endOffset)) == sizeof(endOffset);
		if (r)
		{
			urlID = m_current;
			m_current += NUM_HREF_DELTAS;
		}
		return r;
	}
	else
	{
		return 0;
	}
}

void CCitationFile::WriteInd(ULONG url)
{
//	ULONG noff = ftell(m_citations);
	ULONG noff = m_citations.Tell();
	WriteIndTill(url, m_curOffset);
//	fwrite(&noff, 1, sizeof(noff), m_indCit);
	m_indCit.Write(&noff, sizeof(noff));
	m_current += NUM_HREF_DELTAS;
	m_curOffset = noff;
}

void CCitationFile::CopyTo(CCitationFile& nCit, ULONG start, ULONG end, ULONG url)
{
	if (end > start)
	{
		ULONG size = end - start;
		char* buf = size > 10000 ? new char[size] : (char*)alloca(size);
//		fread(buf, 1, size, m_citations);
//		fwrite(buf, 1, size, nCit.m_citations);
		m_citations.Read(buf, size);
		nCit.m_citations.Write(buf, size);
		if (size > 10000) delete buf;
	}
	nCit.WriteInd(url);
}

void CalcBufSizes(CHrefSU*& hs, CHrefSU* end, int& count, int& siteCount)
{
	siteCount = 1;
	count = 0;
	ULONG site = hs->Site();
	ULONG url = hs->m_url & 0x7FFFFFFF;
	while ((hs < end) && (url == (hs->m_url & 0x7FFFFFFF)))
	{
		if ((hs->m_url & 0x80000000) == 0)
		{
			count++;
			if (site != hs->Site())
			{
				siteCount++;
				site = hs->Site();
			}
		}
		hs++;
	}
}

void CCitationFile::SaveUrl(CHref1*& start, CHref1* end)
{
	ULONG url = start->m_from;
	while ((start < end) && (url == start->m_from))
	{
		if ((start->m_to & 0x80000000) == 0)
		{
//			fwrite(&start->m_to, sizeof(ULONG), 1, m_citations);
			m_citations.Write(&start->m_to, sizeof(ULONG));
		}
		start++;
	}
	WriteInd(url);
}

void CCitationFile::SaveUrl(CHrefSU*& start, CHrefSU* end)
{
	int count;
	int siteCount;
	CHrefSU* hs = start;
	ULONG url = start->m_url & 0x7FFFFFFF;
	CalcBufSizes(hs, end, count, siteCount);
	ULONG* urlBuf = count > 1000 ? new ULONG[count] : (ULONG*)alloca(count * sizeof(ULONG));
	ULONG* siteBuf = siteCount > 1000 ? new ULONG[(siteCount << 1) + 1] : (ULONG*)alloca(((siteCount << 1) + 1) * sizeof(ULONG));
	ULONG* purl = urlBuf;
	ULONG* psite = siteBuf;
	ULONG uoff = 0;
	while (start < hs)
	{
		ULONG site = start->Site();
		ULONG ouoff = uoff;
		while ((start < hs) && (site == start->Site()))
		{
			if ((start->m_url & 0x80000000) == 0)
			{
				*purl++ = start->URL();
				uoff += 4;
			}
			start++;
		}
		if (uoff > ouoff)
		{
			*psite++ = ouoff;
			*psite++ = site;
		}
	}
	if ((psite > siteBuf) && (purl > urlBuf))
	{
		*psite++ = uoff;
		ULONG off = (char*)psite - (char*)siteBuf;
		for (ULONG* s = siteBuf; s < psite; s += 2)
		{
			*s += off;
		}
		m_citations.Write(siteBuf, off);
		m_citations.Write(urlBuf, (char*)purl - (char*)urlBuf);
	}
	WriteInd(url);
	if (count > 1000) delete urlBuf;
	if (siteCount > 1000) delete siteBuf;
}

void CCitationFile::WriteIndTill(ULONG urlID, ULONG offset)
{
	while (m_current < urlID)
	{
//		fwrite(&offset, 1, sizeof(ULONG), m_indCit);
		m_indCit.Write(&offset, sizeof(ULONG));
		m_current += NUM_HREF_DELTAS;
	}
}

void CopyOld(char* buf, ULONG* pbuf, ULONG*& psite, ULONG*& purl, ULONG& offset)
{
	*psite++ = offset;
	*psite++ = pbuf[1];
	memcpy(purl, buf + pbuf[0], pbuf[2] - pbuf[0]);
	offset += pbuf[2] - pbuf[0];
	purl += (pbuf[2] - pbuf[0]) / sizeof(ULONG);
}

void CopyNew(CHrefSU*& hs, CHrefSU* end, ULONG*& psite, ULONG*& purl, ULONG& offset)
{
	ULONG site = hs->Site();
	ULONG offsetSave = offset;
	while ((hs < end) && (hs->Site() == site))
	{
		if ((hs->m_url & 0x80000000) == 0)
		{
			*purl++ = hs->URL();
			offset += sizeof(ULONG);
		}
		hs++;
	}
	if (offset > offsetSave)
	{
		*psite++ = offsetSave;
		*psite++ = site;
	}
}

void CCitationFile::Merge(CCitationFile& nCit, ULONG start, ULONG end, CHref1*& pwsu, CHref1* ewsu)
{
	ULONG url = pwsu->m_from;
	ULONG oUrl;
//	int ao = (start < end) && fread(&oUrl, sizeof(ULONG), 1, m_citations);
	int ao = (start < end) && (m_citations.Read(&oUrl, sizeof(ULONG)) == sizeof(ULONG));
	if (ao) start += sizeof(ULONG);
	while (ao || ((pwsu < ewsu) && (pwsu->m_from == url)))
	{
		if ((ao == 0) || (((pwsu < ewsu) && (pwsu->m_from == url)) && (pwsu->To() < oUrl)))
		{
			if ((pwsu->m_to & 0x80000000) == 0)
			{
//				fwrite(&pwsu->m_to, sizeof(ULONG), 1, nCit.m_citations);
				nCit.m_citations.Write(&pwsu->m_to, sizeof(ULONG));
			}
			pwsu++;
		}
		else if (((pwsu >= ewsu) || (pwsu->m_from != url)) || (pwsu->To() > oUrl))
		{
//			fwrite(&oUrl, sizeof(ULONG), 1, nCit.m_citations);
			nCit.m_citations.Write(&oUrl, sizeof(ULONG));
//			ao = (start < end) && fread(&oUrl, sizeof(ULONG), 1, m_citations);
			ao = (start < end) && (m_citations.Read(&oUrl, sizeof(ULONG)) == sizeof(ULONG));
			if (ao) start += sizeof(ULONG);
		}
		else
		{
			if ((pwsu->m_to & 0x80000000) == 0)
			{
//				fwrite(&oUrl, sizeof(ULONG), 1, nCit.m_citations);
				nCit.m_citations.Write(&oUrl, sizeof(ULONG));
			}
			pwsu++;
//			ao = (start < end) && fread(&oUrl, sizeof(ULONG), 1, m_citations);
			ao = (start < end) && (m_citations.Read(&oUrl, sizeof(ULONG)) == sizeof(ULONG));
			if (ao) start += sizeof(ULONG);
		}
	}
	nCit.WriteInd(url);
}

void CCitationFile::Merge(CCitationFile& nCit, ULONG start, ULONG end, CHrefSU*& pwsu, CHrefSU* ewsu)
{
	int count, siteCount;
	ULONG size = end - start;
	ULONG url = pwsu->m_url & 0x7FFFFFFF;
	ULONG* buf = size > 1000 ? new ULONG[size] : (ULONG*)alloca(size);
//	fread(buf, 1, size, m_citations);
	m_citations.Read(buf, size);
	ULONG* pbuf = buf;
	ULONG* ebuf = (ULONG*)((char*)buf + buf[0] - sizeof(ULONG));
	CHrefSU* hs = pwsu;
	CalcBufSizes(hs, ewsu, count, siteCount);
	siteCount += (buf[0] - sizeof(ULONG)) >> 3;
	count += (size - buf[0]) >> 2;
	ULONG* siteBuf = siteCount > 1000 ? new ULONG[(siteCount << 1) + 1] : (ULONG*)alloca(((siteCount << 1) + 1) * sizeof(ULONG));
	ULONG* urlBuf = count > 1000 ? new ULONG[count] : (ULONG*)alloca(count * sizeof(ULONG));
	ULONG* psite = siteBuf;
	ULONG* purl = urlBuf;
	ULONG offset = 0;
	while ((pbuf < ebuf) || (pwsu < hs))
	{
		if ((pbuf < ebuf) && (pwsu < hs))
		{
			if (pbuf[1] == pwsu->Site())
			{
				ULONG* url = (ULONG*)((char*)buf + pbuf[0]);
				ULONG* eurl = (ULONG*)((char*)buf + pbuf[2]);
				int anew;
				ULONG off = offset;
				while ((anew = ((pwsu < hs) && (pwsu->Site() == pbuf[1]))) || (url < eurl))
				{
					if (anew && (url < eurl))
					{
						if (pwsu->URL() == *url)
						{
							if ((pwsu->m_url & 0x80000000) == 0)
							{
								*purl++ = *url;
								offset += sizeof(ULONG);
							}
							pwsu++;
							url++;
						}
						else if (pwsu->URL() < *url)
						{
							if ((pwsu->m_url & 0x80000000) == 0)
							{
								*purl++ = pwsu->URL();
								offset += sizeof(ULONG);
							}
							pwsu++;
						}
						else
						{
							*purl++ = *url++;
							offset += sizeof(ULONG);
						}
					}
					else if (anew)
					{
						if ((pwsu->m_url & 0x80000000) == 0)
						{
							*purl++ = pwsu->URL();
							offset += sizeof(ULONG);
						}
						pwsu++;
					}
					else
					{
						*purl++ = *url++;
						offset += sizeof(ULONG);
					}
				}
				if (offset > off)
				{
					*psite++ = off;
					*psite++ = pbuf[1];
				}
				pbuf += 2;
			}
			else if (pbuf[1] < pwsu->Site())
			{
				CopyOld((char*)buf, pbuf, psite, purl, offset);
				pbuf += 2;
			}
			else
			{
				CopyNew(pwsu, hs, psite, purl, offset);
			}
		}
		else if (pbuf < ebuf)
		{
			CopyOld((char*)buf, pbuf, psite, purl, offset);
			pbuf += 2;
		}
		else
		{
			CopyNew(pwsu, hs, psite, purl, offset);
		}
	}
	if ((purl > urlBuf) && (psite > siteBuf))
	{
		*psite++ = offset;
		ULONG off = (char*)psite - (char*)siteBuf;
		for (ULONG* s = siteBuf; s < psite; s += 2)
		{
			*s += off;
		}
//		fwrite(siteBuf, 1, off, nCit.m_citations);
//		fwrite(urlBuf, 1, (char*)purl - (char*)urlBuf, nCit.m_citations);
		nCit.m_citations.Write(siteBuf, off);
		nCit.m_citations.Write(urlBuf, (char*)purl - (char*)urlBuf);
	}
	nCit.WriteInd(url);
	if (size > 1000) delete buf;
	if (count > 1000) delete urlBuf;
	if (siteCount > 1000) delete siteBuf;
}

CHrefDeltaFilesR::CHrefDeltaFilesR()
{
	m_redir = NULL;
	m_files = NULL;
	m_lmDelta = NULL;
	pthread_mutex_init(&m_mutex, NULL);
	pthread_mutex_init(&m_rmutex, NULL);
	pthread_mutex_init(&m_lmutex, NULL);
}

CHrefDeltaFiles::CHrefDeltaFiles()
{
	m_redirBuf = NULL;
	m_deltaBuf = NULL;
	m_lmDeltaBuf = NULL;
	hdeltaFiles = this;
}

void CHrefDeltaFilesR::SetBuffers()
{
	setvbuf(m_redir, NULL, _IONBF, 0);
	setvbuf(m_files, NULL, _IONBF, 0);
	setvbuf(m_lmDelta, NULL, _IONBF, 0);
}

void CHrefDeltaFilesR::LockFiles()
{
	m_l_redir.Lock(fileno(m_redir));
	m_l_files.Lock(fileno(m_files));
	m_l_lmDelta.Lock(fileno(m_lmDelta));
	fseek(m_redir, 0, SEEK_END);
	fseek(m_files, 0, SEEK_END);
	fseek(m_lmDelta, 0, SEEK_END);
}

void CHrefDeltaFilesR::UnlockFiles()
{
	m_l_redir.Unlock();
	m_l_files.Unlock();
	m_l_lmDelta.Unlock();
}

void CHrefDeltaFiles::SetBuffers()
{
	m_redirBuf = new char[0x10000];
	setvbuf(m_redir, m_redirBuf, _IOFBF, 0x10000);
	m_deltaBuf = new char[DeltaBufferSize * 1024];
	setvbuf(m_files, m_deltaBuf, _IOFBF, DeltaBufferSize * 1024);
	m_lmDeltaBuf = new char[0x10000];
	setvbuf(m_lmDelta, m_lmDeltaBuf, _IOFBF, 0x10000);
}

int CHrefDeltaFilesR::Open(const char* flags)
{
	char name[1000];
	const char* namep = GetNameP();
	sprintf(name, "%s/deltas/r%s", DBWordDir.c_str(), namep);
	m_redir = fopen(name, flags);
	if (m_redir == NULL)
	{
		logger.log(CAT_FILE, L_ERR, "Can't open redir delta file %s: %s\n", name, strerror(errno));
	}
	sprintf(name, "%s/deltas/h%s", DBWordDir.c_str(), namep);
	if (!(m_files = fopen(name, flags)))
	{
		logger.log(CAT_FILE, L_ERR, "Can't open href delta file %s: %s\n", name, strerror(errno));
	}
	sprintf(name, "%s/deltas/lm%s", DBWordDir.c_str(), namep);
	m_lmDelta = fopen(name, flags);
	if (m_lmDelta == NULL)
	{
		logger.log(CAT_FILE, L_ERR, "Can't open lastmod delta file %s: %s\n", name, strerror(errno));
	}
	if (m_redir && m_files && m_lmDelta)
	{
		SetBuffers();
		return 1;
	}
	else
	{
		return 0;
	}
}

void CHrefDeltaFilesR::AddRedir(ULONG from, ULONG to)
{
	CLocker lock(&m_rmutex);
	fwrite(&from, 1, sizeof(from), m_redir);
	fwrite(&to, 1, sizeof(from), m_redir);
}

CRedir* CHrefDeltaFilesR::SaveRedir(CRedirMap& redirMap, ULONG& redirLen)
{
	char fname[1000], fnameo[1000];
	sprintf(fnameo, "%s/citations/redir", DBWordDir.c_str());
	sprintf(fname, "%s.1", fnameo);
	FILE* fo = fopen(fnameo, "r");
	FILE* fn = fopen(fname, "w");
	ULONG len = 0;
	ULONG ao = 0;
	if (fo)
	{
		fseek(fo, 0, SEEK_END);
		len = ftell(fo) / (2 * sizeof(ULONG));
		fseek(fo, 0, SEEK_SET);
		ao = 1;
	}
	redirLen = ftell(m_redir) / (2 * sizeof(ULONG));
	len += redirLen;
	fseek(m_redir, 0, SEEK_SET);
	CRedir* redird = new CRedir[redirLen];
	fread(redird, redirLen, sizeof(CRedir), m_redir);
	CRedir* pd = redird;
	CRedir* pde = redird + redirLen;
	sort<CRedir*>(pd, pde);
	redirMap.m_redir = new ULONG[len << 1];
	ULONG url[2];
	ULONG* pNew = redirMap.m_redir;
	if (ao) ao = fread(url, sizeof(ULONG) << 1, 1, fo);
	while ((pd < pde) || ao)
	{
		if ((ao == 0) || ((pd < pde) && (url[0] > pd->From())))
		{
			if ((pd->m_from & 0x80000000) == 0)
			{
				*pNew++ = pd->m_from;
				*pNew++ = pd->m_to;
			}
			pd++;
		}
		else if ((pd >= pde) || (url[0] < (pd->From())))
		{
			*pNew++ = url[0];
			*pNew++ = url[1];
			ao = fread(url, sizeof(ULONG) << 1, 1, fo);
		}
		else
		{
			if ((pd->m_from & 0x80000000) == 0)
			{
				*pNew++ = url[0];
				*pNew++ = url[1];
			}
			pd++;
			ao = fread(url, sizeof(ULONG) << 1, 1, fo);
		}
	}
	redirMap.m_size = (pNew - redirMap.m_redir) >> 1;
	fwrite(redirMap.m_redir, redirMap.m_size, sizeof(ULONG) << 1, fn);
	if (fo) fclose(fo);
	fclose(fn);
	unlink(fnameo);
	rename(fname, fnameo);
	ftruncate(fileno(m_redir), 0);
	return redird;
}

void CHrefDeltaFilesR::Flush()
{
	fflush(m_redir);
	fflush(m_files);
	fflush(m_lmDelta);
}

void CHrefDeltaFilesR::SaveHrefs(ULONG* hrefs, ULONG* ehrefs, ULONG urlID, ULONG siteID)
{
	WORD cnt = ehrefs - hrefs;
	if (cnt > 0)
	{
		CLocker lock(&m_mutex);
		fwrite(&siteID, 1, sizeof(siteID), m_files);
		fwrite(&urlID, 1, sizeof(siteID), m_files);
		fwrite(&cnt, 1, sizeof(cnt), m_files);
		fwrite(hrefs, cnt, sizeof(ULONG), m_files);
	}
}

void CHrefDeltaFilesR::AddRedirs(CRedir4* start, CRedir4* end)
{
	while (start < end)
	{
		char fname[1000], fnameo[1000];
		ULONG num = start->DeltaNum();
		CCitationFile oCit(num, 0, fname, fnameo);
		while ((start < end) && (num == start->DeltaNum()))
		{
//			if (oCit.m_citations && oCit.m_indCit)
			if (oCit.m_co && oCit.m_io)
			{
				if ((start->m_from & 0x80000000) == 0)
				{
					ULONG ioff = start->From() / NUM_HREF_DELTAS;
					ULONG d = ioff > 0 ? 1 : 0;
					ULONG offs[2];
					long off = (ioff - d) * sizeof(ULONG);
//					fseek(oCit.m_indCit, off, SEEK_SET);
//					if (off == ftell(oCit.m_indCit))
					if ((ULONG)off == oCit.m_indCit.Seek(off, SEEK_SET))
					{
						offs[0] = 0;
//						if (fread(offs + 1 - d, sizeof(ULONG), 1 + d, oCit.m_indCit) == 1 + d)
						if (oCit.m_indCit.Read(offs + 1 - d, sizeof(ULONG) * (1 + d)) == sizeof(ULONG) * (1 + d))
						{
							if (offs[1] > offs[0])
							{
//								fseek(oCit.m_citations, offs[0], SEEK_SET);
								oCit.m_citations.Seek(offs[0], SEEK_SET);
								ULONG uoff;
//								fread(&uoff, sizeof(ULONG), 1, oCit.m_citations);
								oCit.m_citations.Read(&uoff, sizeof(ULONG));
								ULONG* sites = new ULONG[uoff / sizeof(ULONG)];
								sites[0] = uoff;
//								fread(sites + 1, 1, uoff - sizeof(ULONG), oCit.m_citations);
								oCit.m_citations.Read(sites + 1, uoff - sizeof(ULONG));
								ULONG* esite = (ULONG*)((char*)sites + uoff) - 1;
								for (ULONG* ps = sites; ps < esite; ps += 2)
								{
									for (ULONG i = ps[0]; i < ps[2]; i += sizeof(ULONG))
									{
										ULONG url;
										WORD cnt = 2;
//										fread(&url, 1, sizeof(ULONG), oCit.m_citations);
										oCit.m_citations.Read(&url, sizeof(ULONG));
										fwrite(ps + 1, sizeof(ULONG), 1, m_files);
										fwrite(&url, sizeof(ULONG), 1, m_files);
										fwrite(&cnt, sizeof(WORD), 1, m_files);
										fwrite(&start->m_to, sizeof(ULONG), 1, m_files);
										ULONG rmu = start->m_from | 0x80000000;
										fwrite(&rmu, sizeof(ULONG), 1, m_files);
									}
								}
								delete sites;
							}
						}
					}
				}
			}
			start++;
		}
	}
}

#define RANK_NAME "ranksd"
#define DAMPING_FACTOR 0.85

void CalcRanksD(WORD count)
{
	ULONG maxUrl = 0;
	WORD i;
	FILE** icitations = (FILE**)alloca(count * sizeof(FILE*));
	int num_dots = 1;
	for (i = 0; i < count; i++)
	{
		char fname[1000];
		sprintf(fname, "%s/citations/citationd%u", DBWordDir.c_str(), i);
		icitations[i] = fopen(fname, "r");
		if (icitations[i])
		{
			fseek(icitations[i], 0, SEEK_END);
			ULONG murl = (ftell(icitations[i]) / sizeof(ULONG)) * count + i;
			if (murl > maxUrl) maxUrl = murl;
		}
	}
	logger.log(CAT_ALL, L_INFO, "."); num_dots++;
	char fname[1000];
	sprintf(fname, "%s/%s", DBWordDir.c_str(), RANK_NAME);
	FILE* ranks = fopen(fname, "r+");
	if (ranks == NULL)
	{
		ranks = fopen(fname, "w+");
	}
	fseek(ranks, 0, SEEK_SET);
	ULONG version;
	if (fread(&version, sizeof(ULONG), 1, ranks) == 0) version = 0;
	version++;
	fseek(ranks, 0, SEEK_END);
	long off = ftell(ranks);
	ULONG murl = off > (long)(sizeof(float) + sizeof(ULONG)) ? ((off - sizeof(float) - sizeof(ULONG)) / sizeof(float)) : 0;
	if (murl > maxUrl) maxUrl = murl;
	float* oRanks1 = new float[maxUrl + 1];
	float* oRanks2 = new float[maxUrl + 1];
	float* oRank[2] = {oRanks1, oRanks2};
	int iR = 0;
	float* oRanks = NULL;
	float* nRanks = new float[maxUrl + 1];
	ULONG* offsets = new ULONG[maxUrl + 1];
	oRanks1[0] = 1;
	if (murl > 0)
	{
		fseek(ranks, sizeof(ULONG), SEEK_SET);
		fread(oRanks1, sizeof(float), murl + 1, ranks);
	}
	for (ULONG u = murl + 1; u <= maxUrl; u++)
	{
		oRanks1[u] = 1;
	}
	memcpy(oRanks2, oRanks1, (maxUrl + 1) * sizeof(float));
	logger.log(CAT_ALL, L_INFO, "."); num_dots++;
	FILE** citations = (FILE**)alloca(count * sizeof(FILE*));
	ULONG* lengths = (ULONG*)alloca(count * sizeof(ULONG));
	for (i = 0; i < count; i++)
	{
		ULONG lastOff;
		FILE* f = icitations[i];
		ULONG url = i;
		ULONG* poff = offsets + i;
		if (f)
		{
			fseek(f, 0, SEEK_SET);
			while (fread(poff, sizeof(ULONG), 1, f))
			{
				poff += count;
				url += count;
			}
			lastOff = poff[-count];
			fclose(f);
		}
		else
		{
			lastOff = 0;
		}
		while (url <= maxUrl)
		{
			*poff = lastOff;
			poff += count;
			url += count;
		}
		sprintf(fname, "%s/citations/ccitationd%u", DBWordDir.c_str(), i);
		citations[i] = fopen(fname, "r");
		logger.log(CAT_ALL, L_INFO, "."); num_dots++;
	}
	// We want no more than 50 dots
	int div_dots = (NumIterations * count) / (50 - num_dots) + 1;
	num_dots = 0;

	for (WORD i = 0; i < count; i++)
	{
		if (citations[i])
		{
			fseek(citations[i], 0, SEEK_END);
			lengths[i] = ftell(citations[i]);
		}
		else
		{
			lengths[i] = 0;
		}
	}

	for (ULONG iter = 0; iter < NumIterations; iter++)
	{
		CTimer timer("took: ");
		logger.log(CAT_FILE, L_DEBUG, "Iteration: %lu...", iter);
		ULONG i;
		for (i = 0; i <= maxUrl; i++)
		{
			nRanks[i] = 0;
		}
		for (WORD i = 0; i < count; i++)
		{
			if (lengths[i] == 0) continue;
			ULONG* pc = (ULONG*)mmap(NULL, lengths[i], PROT_READ, MAP_SHARED, fileno(citations[i]), 0);
			ULONG* pcit = pc;

			ULONG prevOff = 0;
			ULONG* pOff = offsets + i;
			for (ULONG url = i; url <= maxUrl; url += count)
			{
				ULONG out = (*pOff - prevOff) / sizeof(ULONG);
				if (out > 0)
				{
					float d = (oRanks1[url] + oRanks2[url]) / (out * 2);
					while (prevOff < *pOff)
					{
						ULONG urlTo;
						urlTo = *pcit++;
						if (urlTo <= maxUrl)
						{
							nRanks[urlTo] += d;
						}
						prevOff += sizeof(ULONG);
					}
				}
				pOff += count;
			}
			if (!(num_dots++ % div_dots))
				logger.log(CAT_ALL, L_INFO, ".");
			munmap((char *)pc, lengths[i]);
		}
		float maxDiff = 0;
		ULONG murl = 0;
		float mold = 0, mnew = 0;
		oRanks = oRank[iR];
		iR ^= 1;
		for (i = 0; i <= maxUrl; i++)
		{
			float nRank = DAMPING_FACTOR * nRanks[i] + (1 - DAMPING_FACTOR);
			float oR = oRank[iR][i];
			float diff = abs(oR - nRank) / nRank;
			if (diff > maxDiff)
			{
				maxDiff = diff;
				murl = i;
				mold = oR;
				mnew = nRank;
			}
			oRanks[i] = nRank;
		}
		logger.log(CAT_FILE, L_DEBUG, "Accuracy: %6.4f, URL: %lu, old: %6.3f, new: %6.3f ", (double)maxDiff, murl, (double)mold, (double)mnew);
	}
	fseek(ranks, sizeof(ULONG), SEEK_SET);
	fwrite(oRanks, sizeof(float), maxUrl + 1, ranks);
	fseek(ranks, 0, SEEK_SET);
	fwrite(&version, sizeof(ULONG), 1, ranks);
	for (WORD i = 0; i < count; i++)
	{
		if (citations[i]) fclose(citations[i]);
//		delete buffs[i];
	}
	fclose(ranks);
	delete oRanks1;
	delete oRanks2;
	delete nRanks;
	delete offsets;
	logger.log(CAT_ALL, L_INFO, ".");
}

void CHrefDeltaFilesR::Save()
{
	FILE* files[NUM_HREF_DELTAS];
	FILE* dfiles[NUM_HREF_DELTAS];
	char* buf[NUM_HREF_DELTAS];
	char* dbuf[NUM_HREF_DELTAS];
	{
		logger.log(CAT_ALL, L_INFO, "Saving real-time ...");
		CTimer timer("took:");
		SaveReal();
		logger.log(CAT_ALL, L_INFO, " done\n");
	}
	{
		CRedirMap redirs;
		ULONG redirLen;
		CRedir* redird;
		{
			logger.log(CAT_ALL, L_INFO, "Saving redirects ...");
			CTimer timer("took:");
			redird = SaveRedir(redirs, redirLen);
			logger.log(CAT_ALL, L_INFO, " done\n");
		}
		sort<CRedir4*>((CRedir4*)redird, (CRedir4*)redird + redirLen);
		long eoff = ftell(m_files);
		AddRedirs((CRedir4*)redird, (CRedir4*)redird + redirLen);

		logger.log(CAT_ALL, L_INFO, "Splitting href delta file ...");
		CTimer timer("took:");
		Split(NUM_HREF_DELTAS, files, dfiles, buf, dbuf, redirs, eoff);
		delete redird;
		logger.log(CAT_ALL, L_INFO, " done\n");
	}
	logger.log(CAT_ALL, L_INFO, "Saving href delta files ...");
	logger.log(CAT_ALL, L_DEBUG, "\n");
	for (int i = 0; i < NUM_HREF_DELTAS; i++)
	{
		ULONG wcount;
		{
	// nested block for timer
			logger.log(CAT_ALL, L_DEBUG, "  Saving href delta file %2i ...", i);
			CTimer timer("took:");
			wcount = Save(i, files[i]);
			logger.log(CAT_ALL, L_DEBUG, " done (saved %lu hrefs). ", wcount);
		}
	}
	logger.log(CAT_ALL, L_INFO, " done\n");

	logger.log(CAT_ALL, L_INFO, "Saving direct href delta files ...");
	logger.log(CAT_ALL, L_DEBUG, "\n");
	for (int i = 0; i < NUM_HREF_DELTAS; i++)
	{
		{
	// nested block for timer
			logger.log(CAT_ALL, L_DEBUG, "  Saving direct href delta file %2i ...", i);
			CTimer timer("took:");
			SaveDirect(i, dfiles[i]);
			logger.log(CAT_ALL, L_DEBUG, " done. ");
		}
	}
	for (int i = 0; i < NUM_HREF_DELTAS; i++)
	{
		fclose(files[i]);
		fclose(dfiles[i]);
		delete buf[i];
		delete dbuf[i];
	}
	logger.log(CAT_ALL, L_INFO, " done\n");

	{
		logger.log(CAT_ALL, L_INFO, "Calculating ranks  [.");
		CTimer timer("took:");
		CalcRanksD(NUM_HREF_DELTAS);
		logger.log(CAT_ALL, L_INFO, "] done.\n");
	}

	{
		logger.log(CAT_ALL, L_INFO, "Saving lastmods ...");
		CTimer timer("took:");
		SaveLastmods();
		logger.log(CAT_ALL, L_INFO, " done\n");
	}

}

void AppendFromToTruncate(FILE* from, FILE* to)
{
	char buf[0x1000];
	int count;
	fseek(from, 0, SEEK_SET);
	fseek(to, 0, SEEK_END);
	while ((count = fread(buf, 1, sizeof(buf), from)))
	{
		fwrite(buf, 1, count, to);
	}
	ftruncate(fileno(from), 0);
}

void CHrefDeltaFilesR::SaveReal()
{
	CHrefDeltaFilesR hreal;
	if (hreal.Open())
	{
		hreal.LockFiles();
		AppendFromToTruncate(hreal.m_files, m_files);
		AppendFromToTruncate(hreal.m_redir, m_redir);
		AppendFromToTruncate(hreal.m_lmDelta, m_lmDelta);
		hreal.UnlockFiles();
	}
}

void CHrefDeltaFilesR::Split(WORD count, FILE** files, FILE** dfiles, char** bufs, char** dbufs, CRedirMap& redirMap, long eoff)
{
	for (int i = 0; i < count; i++)
	{
		char fname[1000];
		sprintf(fname, "%s/deltas/h%i", DBWordDir.c_str(), i);
		files[i] = fopen(fname, "w+");
		char*& buf = bufs[i];
		buf = new char[DeltaBufferSize << 10];
		setvbuf(files[i], buf, _IOFBF, DeltaBufferSize << 10);
		sprintf(fname, "%s/deltas/dh%i", DBWordDir.c_str(), i);
		dfiles[i] = fopen(fname, "w+");
		char*& dbuf = dbufs[i];
		dbuf = new char[DeltaBufferSize << 10];
		setvbuf(dfiles[i], dbuf, _IOFBF, DeltaBufferSize << 10);
	}
	fseek(m_files, 0, SEEK_SET);
	ULONG url[2];
	ULONG* urlto = new ULONG[0x10000];
	WORD* counts = (WORD*)alloca(sizeof(WORD) * count);
	long off = 0;
	while (fread(url, sizeof(url), 1, m_files))
	{
		off += sizeof(url);
		WORD i;
		WORD cnt;
		if (fread(&cnt, sizeof(cnt), 1, m_files) == 0) break;
		off += sizeof(cnt);
		WORD acnt = fread(urlto, sizeof(ULONG), cnt, m_files);
		off += acnt * sizeof(ULONG);
		memset(counts, 0, sizeof(WORD) * count);
		for (i = 0; i < acnt; i++)
		{
			if (off <= eoff)
			{
				urlto[i] = redirMap.GetRedirTo(urlto[i] & 0x7FFFFFFF) | (urlto[i] & 0x80000000);
			}
			counts[urlto[i] % count]++;
		}
		for (WORD c = 0; c < count; c++)
		{
			if (counts[c] > 0)
			{
				fwrite(url, sizeof(url), 1, files[c]);
				fwrite(counts + c, sizeof(WORD), 1, files[c]);
				for (int i = 0; i < acnt; i++)
				{
					if ((urlto[i] % count) == c)
					{
						fwrite(urlto + i, sizeof(ULONG), 1, files[c]);
					}
				}
			}
		}
		FILE* df = dfiles[url[1] % count];
		fwrite(url + 1, sizeof(ULONG), 1, df);
		fwrite(&acnt, sizeof(acnt), 1, df);
		fwrite(urlto, sizeof(ULONG), acnt, df);
		if (acnt < cnt) break;
	}
	delete urlto;
	ftruncate(fileno(m_files), 0);
}

ULONG CHrefDeltaFilesR::SaveDirect(int index, FILE* file)
{
	long offs = ftell(file);
	if (offs == 0) return 0;
	fseek(file, 0, SEEK_SET);
	CHref1* hr = new CHref1[offs / sizeof(ULONG)];
	CHref1* phr = hr;
	ULONG maxUrl = 0;
	ULONG from;
	while (fread(&from, sizeof(ULONG), 1, file))
	{
		WORD cnt;
		fread(&cnt, sizeof(WORD), 1, file);
		for (WORD i = 0; i < cnt; i++)
		{
			if (fread(&phr->m_to, sizeof(ULONG), 1, file) == 0) goto exitLoop;
			phr->m_from = from;
			phr++;
		}
	}
exitLoop:
	ftruncate(fileno(file), 0);
	if (phr > hr)
	{
		sort<CHref1*>(hr, phr);
		char fnamen[1000], fnamenc[1000], fnameo[1000], fnameoc[1000];
		CCitationFile oCit(index, 0, fnameo, fnameoc, "citationd");
		CCitationFile nCit(index, 1, fnamen, fnamenc, "citationd");
		CHref1* swsu = hr + 1;
		CHref1* ppwsu = hr;
		CHref1* pwsu = hr + 1;
		while (pwsu < phr)
		{
			if (*pwsu != *ppwsu)
			{
				*swsu++ = *pwsu;
				ppwsu = pwsu;
			}
			pwsu++;
		}
		phr = swsu;
		pwsu = hr;

		ULONG prevOffset = 0;
		ULONG oUrl = 0;
		ULONG offset;
		int oavail = oCit.GetNextUrl(oUrl, offset);
		while (oavail || (pwsu < phr))
		{
			if ((oavail == 0) || ((pwsu < phr) && (oUrl > pwsu->m_from)))
			{
				nCit.SaveUrl(pwsu, phr);
				maxUrl = pwsu->m_from;
			}
			else if ((pwsu >= phr) || (oUrl < pwsu->m_from))
			{
				oCit.CopyTo(nCit, prevOffset, offset, oUrl);
				prevOffset = offset;
				oavail = oCit.GetNextUrl(oUrl, offset);
				maxUrl = oUrl;
			}
			else
			{
				if (prevOffset == offset)
				{
					nCit.SaveUrl(pwsu, phr);
				}
				else
				{
					oCit.Merge(nCit, prevOffset, offset, pwsu, phr);
				}
				prevOffset = offset;
				oavail = oCit.GetNextUrl(oUrl, offset);
				maxUrl = oUrl;
			}
		}
		oCit.Close();
		nCit.Close();
		unlink(fnameo);
		rename(fnamen, fnameo);
		unlink(fnameoc);
		rename(fnamenc, fnameoc);
	}
	delete hr;
	return maxUrl;
}

int CHrefDeltaFilesR::Save(int index, FILE* file)
{
	ULONG wcount = 0;
	ULONG* fromBuf = NULL;
	CHrefSU* toBuf = NULL;
	CHrefSU* ewsu = NULL;
	char fnamen[1000], fnamenc[1000], fnameo[1000], fnameoc[1000];
	int len = 0;
	{
		CLocker lock(&m_mutex);
		fseek(file, 0, SEEK_END);
		len = ftell(file);
		if (len == 0) return 0;
		fromBuf = new ULONG[len / sizeof(ULONG)];
		toBuf = new CHrefSU[len / sizeof(ULONG)];
		ewsu = toBuf;
		fseek(file, 0, SEEK_SET);
		ULONG* pfrom = fromBuf;
		while (fread(pfrom, sizeof(ULONG) << 1, 1, file))
		{
			WORD count;
			if (fread(&count, sizeof(count), 1, file) == 0) break;
			for (WORD i = 0; i < count; i++)
			{
				ULONG to;
				if (fread(&to, sizeof(to), 1, file) == 0) goto exitLoop;
				ewsu->m_url = to;
				ewsu->m_site = pfrom;
				ewsu++;
			}
			pfrom += 2;
		}
exitLoop:
		fseek(file, 0, SEEK_SET);
		ftruncate(fileno(file), 0);
		logger.log(CAT_ALL, L_DEBUG, " (memory: %u+%u=%u)", (char*)pfrom - (char*)fromBuf, (char*)ewsu - (char*)toBuf, ((char*)pfrom - (char*)fromBuf) + ((char*)ewsu - (char*)toBuf));
	}
	CCitationFile oCit(index, 0, fnameo, fnameoc);
	CCitationFile nCit(index, 1, fnamen, fnamenc);
	if (toBuf)
	{
		CHrefSU* pwsu;
		sort<CHrefSU*>(toBuf, ewsu);
		CHrefSU* swsu = toBuf + 1;
		CHrefSU* ppwsu = toBuf;
		pwsu = toBuf + 1;
		while (pwsu < ewsu)
		{
			if (*pwsu != *ppwsu)
			{
				*swsu++ = *pwsu;
				ppwsu = pwsu;
			}
			pwsu++;
		}
		ewsu = swsu;
		pwsu = toBuf;
		ULONG prevOffset = 0;
		ULONG oUrl = 0;
		ULONG offset;
		int oavail = oCit.GetNextUrl(oUrl, offset);
		while (pwsu < ewsu)
		{
			ULONG url = pwsu->m_url & 0x7FFFFFFF;
			wcount++;
			if ((oavail == 0) || (oUrl > url))
			{
				nCit.SaveUrl(pwsu, ewsu);
			}
			else if (oUrl < url)
			{
				oCit.CopyTo(nCit, prevOffset, offset, oUrl);
				prevOffset = offset;
				oavail = oCit.GetNextUrl(oUrl, offset);
			}
			else
			{
				if (prevOffset == offset)
				{
					nCit.SaveUrl(pwsu, ewsu);
				}
				else
				{
					oCit.Merge(nCit, prevOffset, offset, pwsu, ewsu);
				}
				prevOffset = offset;
				oavail = oCit.GetNextUrl(oUrl, offset);
			}
		}
		while (oavail)
		{
			oCit.CopyTo(nCit, prevOffset, offset, oUrl);
			prevOffset = offset;
			oavail = oCit.GetNextUrl(oUrl, offset);
		}
		delete toBuf;
		delete fromBuf;
	}
	oCit.Close();
	nCit.Close();
	unlink(fnameo);
	rename(fnamen, fnameo);
	unlink(fnameoc);
	rename(fnamenc, fnameoc);
	return wcount;
}

void CHrefDeltaFilesR::AddLastmod(ULONG url, time_t lm)
{
	CLocker lock(&m_lmutex);
	fwrite(&url, sizeof(url), 1, m_lmDelta);
	fwrite(&lm, sizeof(lm), 1, m_lmDelta);
}

const WORD time_t2word(const time_t t, const time_t our_epoch)
{
	if (t == BAD_DATE)
	{
		return 0; // no date
	}
	else
	if (t > (our_epoch + 3600)) // past our epoch (+ 3600 is here to prevent 0 result)
	{
		if (t > our_epoch + WORD_MAX * 3600) // past the end of our epoch
			return WORD_MAX; // max value
		else
			return ( ( t - our_epoch) / 3600 );
	}
	else
		return 1; // older than m_our_epoch
}

void CHrefDeltaFilesR::SaveLastmods()
{
	fseek(m_lmDelta, 0, SEEK_END);
	long off = ftell(m_lmDelta);
	ULONG count = off / (sizeof(CUrlLm));
	if (count == 0) return;
	CUrlLm* ul = new CUrlLm[count];
	fseek(m_lmDelta, 0, SEEK_SET);
	fread(ul, sizeof(CUrlLm), count, m_lmDelta);
	ftruncate(fileno(m_lmDelta), 0);
	time_t maxt = 0;
	time_t t_limit = now() + 60 * 60 * 24; // now + 1 day
	ULONG maxUrl = 0;
	CUrlLm* mu;
	for (CUrlLm* u = ul; u < ul + count; u++)
	{
		if (u->m_lm != BAD_DATE)
		{
			if (u->m_lm > t_limit)
				u->m_lm = t_limit;
			if (u->m_lm > maxt)
				maxt = u->m_lm;
		}
		if (u->m_urlID > maxUrl)
		{
			maxUrl = u->m_urlID;
			mu = u;
		}
	}
	time_t our_epoch = (maxt / 3600 + 1) * 3600 - WORD_MAX * 3600;
	char fname[1000];
	sprintf(fname, "%s/lastmodd", DBWordDir.c_str());
	int f = open(fname, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
	if (f < 0)
	{
		logger.log(CAT_FILE, L_WARN, "Can't open file %s: %s\n", fname, strerror(errno));
		return;
	}
	long fe = lseek(f, 0, SEEK_END);
	ULONG numUrl = fe > (long)(sizeof(ULONG) + sizeof(time_t)) ? (fe - (sizeof(ULONG) + sizeof(time_t))) / sizeof(WORD) : 0;
	WORD* times = new WORD[max(numUrl, maxUrl + 1)];
	ULONG ver;
	if (numUrl > 0)
	{
		time_t cur_epoch;
		lseek(f, 0, SEEK_SET);
		read(f, &ver, sizeof(ver));
		read(f, &cur_epoch, sizeof(cur_epoch));
		read(f, times, numUrl * sizeof(WORD));
		ver++;
		if (cur_epoch < our_epoch)
		{
			WORD d = (our_epoch - cur_epoch) / 3600;
			for (ULONG i = 0; i < numUrl; i++)
			{
				WORD& time = times[i];
				if ((time > 1) && (time < WORD_MAX))
				{
					time -= d;
				}
			}
		}
		else
		{
			our_epoch = cur_epoch;
		}
		lseek(f, sizeof(ULONG), SEEK_SET);
		write(f, &our_epoch, sizeof(our_epoch));
	}
	else
	{
		lseek(f, 0, SEEK_SET);
		ver = 1;
		write(f, &ver, sizeof(ver));
		write(f, &our_epoch, sizeof(our_epoch));
	}
	if (maxUrl >= numUrl)
	{
		memset(times + numUrl, 0, (maxUrl - numUrl + 1) * sizeof(WORD));
	}
	for (CUrlLm* u = ul; u < ul + count; u++)
	{
		times[u->m_urlID] = time_t2word(u->m_lm, our_epoch);
	}
	write(f, times, max(numUrl, maxUrl + 1) * sizeof(WORD));
	if (ver > 1)
	{
		lseek(f, 0, SEEK_SET);
		write(f, &ver, sizeof(ver));

	}
	delete times;
	delete ul;
}

void CHrefDeltaFilesR::Close()
{
	if (m_redir) fclose(m_redir);
	if (m_files) fclose(m_files);
	if (m_lmDelta) fclose(m_lmDelta);
	m_redir = NULL;
	m_files = NULL;
	m_lmDelta = NULL;
}

CHrefDeltaFilesR::~CHrefDeltaFilesR()
{
	Close();
	pthread_mutex_destroy(&m_mutex);
	pthread_mutex_destroy(&m_rmutex);
	pthread_mutex_destroy(&m_lmutex);
}

CHrefDeltaFiles::~CHrefDeltaFiles()
{
	Close();
	if (m_redirBuf) delete m_redirBuf;
	if (m_deltaBuf) delete m_deltaBuf;
	if (m_lmDeltaBuf) delete m_lmDeltaBuf;
	hdeltaFiles = NULL;
}

ULONG* CRedirMap::Find(ULONG url)
{
	if (m_size == 0) return NULL;
	int lo = 0;
	int hi = m_size - 1;
	while (hi >= lo)
	{
		ULONG mid = (hi + lo) >> 1;
		if (url > m_redir[mid << 1])
		{
			lo = mid + 1;
		}
		else
		{
			hi = mid - 1;
		}
	}
	return m_redir[lo << 1] == url ? m_redir + (lo << 1) : NULL;
}

inline int Find(ULONG* start, ULONG* end, ULONG u)
{
	for (; start < end; start++)
	{
		if (*start == u) return 1;
	}
	return 0;
}

ULONG CRedirMap::GetRedirTo(ULONG url)
{
	ULONG* r;
//	CULONGSet set;
	ULONG urls[0x1000];
	ULONG *purl = urls;
	ULONG u = url;
	do
	{
		r = Find(u);
		if (r == NULL) break;
		if ((r[1] == url) || /*(set.find(r[1]) != set.end())*/ ::Find(urls, purl, r[1]))
		{
			return url;
		}
		u = r[1];
//		set.insert(u);
		*purl++ = u;
	}
	while (true);
	return u;
}

int CBufferedFile::Open(const char* name, int flags, ULONG bufSize, mode_t mode)
{
	if (m_file >= 0)
	{
		return -1;
	}
	m_flags = flags;
	m_file = open(name, flags, mode);
	if (m_file < 0)
	{
		return m_file;
	}
	m_bufSize = bufSize;
	m_bytesInBuffer = 0;
	m_offsetInBuffer = 0;
	m_bufOffset = 0;
	m_dirty = 0;
	m_eof = 0;
	m_buffer = new char[bufSize];
	return 0;
}

int CBufferedFile::Close()
{
	if (m_file >= 0)
	{
		Flush();
		close(m_file);
		m_file = -1;
		delete m_buffer;
	}
	return 0;
}

CBufferedFile::~CBufferedFile()
{
	Close();
}

ULONG CBufferedFile::Read(void* buf, ULONG count)
{
	if (m_offsetInBuffer + count <= m_bytesInBuffer)
	{
		memcpy(buf, m_buffer + m_offsetInBuffer, count);
		m_offsetInBuffer += count;
		return count;
	}
	else
	{
		ULONG rd = 0;
		if (m_offsetInBuffer < m_bytesInBuffer)
		{
			rd = m_bytesInBuffer - m_offsetInBuffer;
			memcpy(buf, m_buffer + m_offsetInBuffer, rd);
			buf += rd;
			m_offsetInBuffer += rd;
			count -= rd;
		}
		if (m_eof) return rd;
		if (m_bytesInBuffer > 0) m_bufOffset += m_bufSize;
		while (count >= m_bufSize)
		{
			ULONG rd1 = read(m_file, buf, m_bufSize);
			rd += rd1;
			if (rd1 < m_bufSize)
			{
				memcpy(m_buffer, buf, rd1);
				m_bytesInBuffer = rd1;
				m_offsetInBuffer = rd1;
				m_eof = 1;
				return rd;
			}
			count -= m_bufSize;
			buf += m_bufSize;
			m_bufOffset += m_bufSize;
		}
		m_bytesInBuffer = read(m_file, m_buffer, m_bufSize);
		m_offsetInBuffer = 0;
		if (m_bytesInBuffer < m_bufSize) m_eof = 1;
		if (count > 0)
		{
			if (count > m_bytesInBuffer) count = m_bytesInBuffer;
			memcpy(buf, m_buffer, count);
			m_offsetInBuffer = count;
			rd += count;
		}
		return rd;
	}
}

ULONG CBufferedFile::Write(void* buf, ULONG count)
{
	if (m_offsetInBuffer + count <= m_bufSize)
	{
		memcpy(m_buffer + m_offsetInBuffer, buf, count);
		m_offsetInBuffer += count;
		if (m_offsetInBuffer > m_bytesInBuffer) m_bytesInBuffer = m_offsetInBuffer;
		m_dirty = 1;
		return count;
	}
	else
	{
		ULONG wr = 0;
		if (m_offsetInBuffer < m_bufSize)
		{
			wr = m_bufSize - m_offsetInBuffer;
			memcpy(m_buffer + m_offsetInBuffer, buf, wr);
			m_bytesInBuffer = m_bufSize;
			m_offsetInBuffer = m_bufSize;
			buf += wr;
			count -= wr;
			m_dirty = 1;
		}
		Flush();
		m_bufOffset += m_bufSize;
		while (count >= m_bufSize)
		{
			ULONG wr1 = write(m_file, buf, m_bufSize);
			wr += wr1;
			if (wr1 < m_bufSize)
			{
				memcpy(m_buffer, buf, wr1);
				m_bytesInBuffer = wr1;
				m_offsetInBuffer = wr1;
				m_eof = 1;
				return wr;
			}
			count -= m_bufSize;
			buf += m_bufSize;
			m_bufOffset += m_bufSize;
		}
		m_bytesInBuffer = 0;
		m_offsetInBuffer = 0;
		if (count > 0)
		{
			memcpy(m_buffer, buf, count);
			m_bytesInBuffer = count;
			m_offsetInBuffer = count;
			wr += count;
			m_dirty = 1;
		}
		return wr;
	}
}

void CBufferedFile::Flush()
{
	if (m_dirty)
	{
		write(m_file, m_buffer, m_bytesInBuffer);
		m_dirty = 0;
		Sync();
	}
}

ULONG CBufferedFile::Seek(long offset, int whence)
{
	ULONG newOffset = 0;
	ULONG curOffset = m_bufOffset + m_offsetInBuffer;
	switch (whence)
	{
	case SEEK_SET:
		newOffset = offset;
		break;
	case SEEK_CUR:
		newOffset = curOffset + offset;
		break;
	}
	if ((newOffset >= m_bufOffset) && (newOffset <= m_bufOffset + m_bufSize))
	{
		if (newOffset > m_bufOffset + m_bytesInBuffer)
		{
			newOffset = m_bufOffset + m_bytesInBuffer;
		}
		m_offsetInBuffer = newOffset - m_bufOffset;
		return newOffset;
	}
	Flush();
	ULONG bOffset = (newOffset / m_bufSize) * m_bufSize;
	ULONG lOffset = lseek(m_file, bOffset, SEEK_SET);
	if (lOffset < bOffset)
	{
		bOffset = (lOffset / m_bufSize) * m_bufSize;
		lseek(m_file, bOffset, SEEK_SET);
	}
	m_bytesInBuffer = read(m_file, m_buffer, m_bufSize);
	m_bufOffset = bOffset;
	if (newOffset > m_bufOffset + m_bytesInBuffer) newOffset = m_bufOffset + m_bytesInBuffer;
	m_offsetInBuffer = newOffset - m_bufOffset;
	return newOffset;
}

#ifdef USE_CSYNC_BUFFERED_FILE

void* SyncFile(void* param)
{
	return ((CSyncBufferedFile*)param)->SyncFile();
}

void* CSyncBufferedFile::SyncFile()
{
	while (m_event.Wait())
	{
		if (m_finish) return NULL;
		fdatasync(m_file);
	}
	return NULL;
}

CSyncBufferedFile::CSyncBufferedFile()
{
	m_finish = 0;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	if (thread_stack_size)
		pthread_attr_setstacksize(&attr, thread_stack_size);
	pthread_create(&m_thread, &attr, ::SyncFile, this);
}

CSyncBufferedFile::~CSyncBufferedFile()
{
	void* res;
	m_finish = 1;
	m_event.Post();
	pthread_join(m_thread, &res);
}

void CSyncBufferedFile::Sync()
{
	m_event.Post();
}

#ifdef TEST_CBUFFERFILE
void TestBufFiles(const char* rn, const char* wn, ULONG bufsize)
{
	CBufferedFile fr, fw;
	char* buf = (char*)alloca(bufsize);
	if ((fr.Open(rn, O_RDONLY, 0x1000) == 0) && (fw.Open(wn, O_RDWR | O_TRUNC | O_CREAT, 0x1000) == 0))
	{
		ULONG rd, trd = 0;
		while ((rd = fr.Read(buf, bufsize)))
		{
			fw.Write(buf, rd);
			trd += rd;
		}
		fr.Close();
		fw.Close();
		int ir = open(rn ,O_RDONLY);
		int iw = open(wn, O_RDONLY);
		if ((ir < 0) || (iw < 0))
		{
			printf("Can't open files second time\n");
			return;
		}
		long rl = lseek(ir, 0, SEEK_END);
		long wl = lseek(iw, 0, SEEK_END);
		if (rl != wl)
		{
			printf("Lengths of files differ: old: %li, new: %li, read: %li\n", rl, wl, trd);
			close(ir);
			close(iw);
			return;
		}
		char* mr = (char*)mmap(NULL, rl, PROT_READ, MAP_SHARED, ir, 0);
		char* mw = (char*)mmap(NULL, wl, PROT_READ, MAP_SHARED, iw, 0);
		if (memcmp(mr, mw, rl))
		{
			printf("Files are different\n");
		}
		munmap(mr, rl);
		munmap(mw, wl);
		close(ir);
		close(iw);
		printf("Test finished\n");
	}
	else
	{
		printf("Can't open files\n");
	}
}

void TestBufFiles2(const char* rn, const char* wn, const char* ctrl, ULONG bufsize, ULONG skip)
{
	CBufferedFile fr, fw;
	char* buf = (char*)alloca(bufsize);
	FILE* ictrl;
	FILE* ir;
	if ((fr.Open(rn, O_RDONLY, 0x1000) == 0) && (fw.Open(wn, O_RDWR | O_TRUNC | O_CREAT, 0x1000) == 0)
	  && ((ictrl = fopen(ctrl, "w+")) != NULL) && ((ir = fopen(rn, "r")) != NULL))
	{
		ULONG rd, trd = 0;
		while ((rd = fr.Read(buf, bufsize)))
		{
			fw.Write(buf, rd);
			trd += rd;
			fr.Seek(skip, SEEK_CUR);
		}
		while ((rd = fread(buf, 1, bufsize, ir)))
		{
			fwrite(buf, 1, rd, ictrl);
			fseek(ir, skip, SEEK_CUR);
		}
		fr.Close();
		fw.Close();
		fclose(ictrl);
		fclose(ir);
		ictrl = fopen(ctrl, "r");
		int iw = open(wn, O_RDONLY);
		if ((iw < 0) || (ictrl == NULL))
		{
			printf("Can't open files second time\n");
			return;
		}
		long wl = lseek(iw, 0, SEEK_END);
		fseek(ictrl, 0, SEEK_END);
		long cl = ftell(ictrl);
		if (cl != wl)
		{
			printf("Lengths of files differ: control: %li, new: %li, read: %li\n", cl, wl, trd);
			close(iw);
			close(cl);
			return;
		}
		char* mr = (char*)mmap(NULL, cl, PROT_READ, MAP_SHARED, fileno(ictrl), 0);
		char* mw = (char*)mmap(NULL, wl, PROT_READ, MAP_SHARED, iw, 0);
		if (memcmp(mr, mw, wl))
		{
			printf("Files are different\n");
		}
		munmap(mr, cl);
		munmap(mw, wl);
		close(iw);
		fclose(ictrl);
		printf("Test finished\n");
	}
	else
	{
		printf("Can't open files\n");
	}
}

ULONG bufs[] = {1, 4, 5, 123, 3456, 0x1000, 0x2000, 0x2345, 0x20000, 0x23445};

void TestsBufFiles(const char* file)
{
	char fw[PATH_MAX], ctrl[PATH_MAX];
	sprintf(fw, "%s.copy", file);
	sprintf(ctrl, "%s.ctrl", file);
/*	TestBufFiles(file, fw, 1);
	TestBufFiles(file, fw, 4);
	TestBufFiles(file, fw, 5);
	TestBufFiles(file, fw, 123);
	TestBufFiles(file, fw, 3456);
	TestBufFiles(file, fw, 0x1000);
	TestBufFiles(file, fw, 0x2000);
	TestBufFiles(file, fw, 0x2345);
	TestBufFiles(file, fw, 0x20000);
	TestBufFiles(file, fw, 0x23445);*/
	for (unsigned int i = 0; i < sizeof(bufs) / sizeof(bufs[0]); i++)
		for (unsigned int j = 0; j < sizeof(bufs) / sizeof(bufs[0]); j++)
			TestBufFiles2(file, fw, ctrl, bufs[i], bufs[j]);
	unlink(fw);
}
#endif /* TEST_CBUFFERFILE */
#endif /* USE_CSYNC_BUFFERED_FILE */
