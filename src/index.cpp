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

/*  $Id: index.cpp,v 1.83 2003/04/28 19:51:09 kir Exp $
    Author : Alexander F. Avdonkin
	Uses parts of UdmSearch code
	Implemented classes: CBWSchedule
*/

#include "aspseek-cfg.h"
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <algorithm>
#include "parse.h"
#include "index.h"
#include "config.h"
#include "wcache.h"
#include "rank.h"
#include "math.h"
#include "zlib.h"
#include "misc.h"
#include "logger.h"
#include "stopwords.h"
#include "lastmod.h"
#include "geo.h"
#include "timer.h"
#include "sqldbi.h"
#include "paths.h"
#include "delmap.h"

#ifdef CWDEBUG
#include <libcw/sysd.h>
#include <libcw/debug.h>
#endif


#ifdef STATIC_DB_LINKING
#include "mysqldb.h"
#endif

// seems that index coredumps if thread stack size is short
#define NEEDED_STACK_SIZE 131072
int thread_stack_size = 0;

using std::partial_sort;
using namespace std;

CStopWords Stopwords;

extern CStringVector urlLimits;

int usage()
{
	fprintf(stderr, "index from ASPseek v." VERSION "\nCopyright (C) 2000-2010 SWsoft, janforman.com\n\n");
	fprintf(stderr, "Usage: index [options]  [configfile]\n\n");
	fprintf(stderr, "Indexing options:\n");
	fprintf(stderr, "  -a            Reindex all documents even if not expired\n");
	fprintf(stderr, "  -m            Reindex expired documents even if not modified\n");
	fprintf(stderr, "  -o            Index documents with less depth (hops value) first\n");
	fprintf(stderr, "  -n n          Index only n documents\n");
	fprintf(stderr, "  -q            Quick startup (do not add Server URLs)\n");
	fprintf(stderr, "  -i            Insert new URLs (URLs to insert must be given using -u or -f)\n");
	fprintf(stderr, "  -M            Index URLs, which were indexed by previous indexing session\n");
	fprintf(stderr, "  -T URL        Index URL to \"real-time\" database only\n");
	fprintf(stderr, "  -N n          Run N threads\n");
	fprintf(stderr, "\nSubsection control options:\n(You may give any combination of it to most other options including -a and -m)\n");
	fprintf(stderr, "  -s status     Limit index to documents matching status (HTTP Status code)\n");
	fprintf(stderr, "  -t tag        Limit index to documents matching tag\n");
	fprintf(stderr, "  -u pattern    Limit index to documents with URLs matching pattern\n");
	fprintf(stderr, "                (supports SQL LIKE wildcard '%%')\n");
	fprintf(stderr, "  -f filename   Read URLs to be indexed/inserted/cleared from file\n                (with -a or -C option, supports SQL LIKE wildcard '%%';\n");
	fprintf(stderr, "                has no effect when combined with -m option)\n");
	fprintf(stderr, "  -f -          Use STDIN instead of file as URL list\n");
#ifdef LOG_PERROR
	fprintf(stderr, "\nLogging options:\n");
	fprintf(stderr, "  -l            Do not log to stdout/stderr\n");
#endif
	fprintf(stderr, "\nDatabase repairing options:\n");
	fprintf(stderr, "  -X1           Check inverted index for deleted URLs\n");
	fprintf(stderr, "  -X2           Fix database to delete deleted URLs\n");
	fprintf(stderr, "  -H            Recreate citation indexes and ranks\n");

	fprintf(stderr, "\nMiscellanious options:\n");

	fprintf(stderr, "  -C            Clear database\n");
	fprintf(stderr, "  -w            Do not warn before clearing documents from database\n");
	fprintf(stderr, "  -S            Print database statistics\n");
	fprintf(stderr, "  -r filename   Redirect output to filename\n");
	fprintf(stderr, "  -g            Set statistics log file name (default is " DEFAULT_LOGNAME ")\n");
	fprintf(stderr, "  -E            Safely terminate already running index\n");
	fprintf(stderr, "  -R n          Set number of resolvers to n (default is NUM_THREADS/5 + 1)\n");
	fprintf(stderr, "  -P URL        Print path to specified URL\n");
	fprintf(stderr, "  -A space_id   Add/delete a site to/from web space (use with -u)\n");
	fprintf(stderr, "  -D            Only save delta files\n");
	fprintf(stderr, "  -B            Only generate subsets and spaces\n");
	fprintf(stderr, "  -K            Only calculate PageRanks\n");
//	fprintf(stderr, "  -L            Only generate lastmod file from SQL database\n");
//	fprintf(stderr, "  -c dbname     Import from UdmSearch's dbname.url table to urlword table\n");
	fprintf(stderr, "  -U            Only calculate total non-empty URLs (and put to var/total file)\n");
	fprintf(stderr, "  -x            Generate URLs per site file (for limiting indexed URLs by site)\n");
	fprintf(stderr, "  -h,-?         Print this help page\n");

	return(0);
}

// This method sets maximal value of bandwidth at specified range of time of day
void CBWSchedule::AddInterval(int start, int finish, ULONG bandwidth)
{
	if (finish < start)
	{
		AddInterval(0, finish % 86400, bandwidth);
		AddInterval(start % 86400, 86400, bandwidth);
	}
	else if (finish > start)
	{
		iterator it = upper_bound(start);
		while ((it != end()) && (finish > it->second.m_start))
		{
			if (start > it->second.m_start)
			{
				(*this)[start] = it->second;
				if (finish < it->first)
				{
					it->second.m_start = finish;
					CBW& bw = (*this)[finish];
					bw.m_start = start;
					bw.m_maxBandwidth = bandwidth;
					return;
				}
				else
				{
					iterator it1 = it++;
					erase(it1);
				}
			}
			else
			{
				if (finish >= it->first)
				{
					iterator it1 = it++;
					erase(it1);
				}
				else
				{
					it->second.m_start = finish;
					it++;
				}
			}
		}
		if ((it == end()) || (start >= it->second.m_start))
		{
			CBW& bw = (*this)[finish];
			bw.m_start = start;
			bw.m_maxBandwidth = bandwidth;
		}
	}
}

// This method returns maximal bandwidth, which can be used now
ULONG CBWSchedule::MaxBandWidth()
{
	struct timeval tm;
	gettimeofday(&tm, NULL);
	long sec = tm.tv_sec % 86400;
	iterator it = lower_bound(sec);
	return it == end() || (it->second.m_start >= sec) ? m_defaultBandwidth : it->second.m_maxBandwidth;
}
/*
void ScheduleTest()
{
	CBWSchedule sch;
	sch.AddInterval(100, 300, 2);
	sch.AddInterval(300, 500, 1);
	sch.AddInterval(500, 600, 3);
	sch.AddInterval(600, 900, 4);
	for (CBWSchedule::iterator it = sch.begin(); it != sch.end(); it++)
	{
		printf("Start: %i, finish: %i, bandwidth: %i\n", it->second.m_start, it->first, it->second.m_maxBandwidth);
	}
}
*/
void CalcRanks(CSQLDatabaseI* database)
{
	CUrlRanks ranks;
	{
		CTimer timer("Loading took: ");
		// Load outgoing hrefs for all URLs and group it by target URL ID
		database->LoadRanks(ranks);
	}
	{
		CTimer timer("Saving links took: ");
		database->SaveLinks(ranks);	// Save reverse href index into "citation" table
	}
	// Build array of pointers to hrefs
	CUrlRankPtrR* rnk = new CUrlRankPtrR[ranks.size()];
	CUrlRankPtrR* prnk = rnk;
	for (CUrlRanks::iterator it = ranks.begin(); it != ranks.end(); it++)
	{
		prnk->m_rank = &(*it);
		prnk++;
	}
	logger.log(CAT_ALL, L_INFO, "Calculating ranks");
	if (logger.getLevel() < L_DEBUG)
		logger.log(CAT_ALL, L_INFO, "  [");
	else
		logger.log(CAT_ALL, L_INFO, "\n");
	// Perform just 10 iterations
	ranks.Sort();
	for (ULONG i = 0; i < NumIterations; i++)
	{
		if (logger.getLevel() < L_DEBUG)
			logger.log(CAT_ALL, L_INFO, ".....");
		else
			logger.log(CAT_ALL, L_DEBUG, "Iteration number %d\n", i);
		{
			CTimer timer("Iteration took: ");
			for (int ii = 0; ii < 1; ii++)
			{
				ranks.Iterate();	// Perform 1 iteration of computing PageRanks
			}
		}

		// Print URLs with highest ranks
		int c = min(prnk - rnk, 10);
		partial_sort<CUrlRankPtr*>(rnk, rnk + c, prnk);
		logger.log(CAT_ALL, L_DEBUG, "\n\t---- URLs with the highest ranks ----\n");
		logger.log(CAT_ALL, L_DEBUG, "rank      log    refN   out    urlID       URL\n");
		for (int j = 0; j < c; j++)
		{
			const CUrlRank* rank = rnk[j].m_rank;
			string url;
			database->GetUrl(rank->m_urlID, url);
			logger.log(CAT_ALL, L_DEBUG, "%7.3f %7.3f %5i %5li %8li %s\n", (double)rank->m_rank, log(rank->m_rank), rank->m_refend - rank->m_referrers, rank->m_out, rank->m_urlID, url.c_str());
		}
		// Print URLs with lowest ranks
		partial_sort<CUrlRankPtrR*>(rnk, rnk + c, prnk);
		logger.log(CAT_ALL, L_DEBUG, "\n\t---- URLs with the lowest ranks ----\n");
		logger.log(CAT_ALL, L_DEBUG, "rank      refN   out    urlID      URL\n");
		for (int j = c - 1; j >= 1; j--)
		{
			const CUrlRank* rank = rnk[j].m_rank;
			string url;
			database->GetUrl(rank->m_urlID, url);
			logger.log(CAT_ALL, L_DEBUG, "%8.6f %5i %5li %8li %s\n", (double)rank->m_rank, rank->m_refend - rank->m_referrers, rank->m_out, rank->m_urlID, url.c_str());
		}
		logger.log(CAT_ALL, L_DEBUG, "\n");
	}
	if (logger.getLevel() < L_DEBUG)
		logger.log(CAT_ALL, L_INFO, "] done.\n");
	ranks.PrintInOut();
	logger.log(CAT_ALL, L_INFO, "Urls: %u. Hrefs: %lu\n", ranks.size(), ranks.m_total);
	delete rnk;
	ranks.Save();
}

/*
void CalcRanks()
{
	CMySQLDatabaseI database;
	if (database.Connect(DBHost.c_str(), DBUser.c_str(), DBPass.c_str(), DBName.c_str(), DBPort))
	{
		database.CalcTotal();
		CalcRanks(&database);
	}
	else{
		logger.log(CAT_SQL, L_ERR, "ERROR: Can't connect to database. %s\n", database.DispError());
	}
}
*/
void CalcTotal(CSQLDatabaseI* database)
{
	database->CalcTotal();
}

void BuildSiteURLs(CSQLDatabaseI* database)
{
	database->BuildSiteURLs();
}

int URLFile(CSQLDatabaseI* database, char* fname, int action, int flags)
{
	FILE *url_file;
	char str[STRSIZ] = "";
	char str1[STRSIZ] = "";
	CUrl myurl;

	// Read lines and clear URLs
	// We've already tested in main.c to make sure it can be read

	if (!strcmp(fname, "-"))
	{
		url_file = stdin;
	}
	else
	{
		if (!(url_file=fopen(fname, "r"))){
			logger.log(CAT_FILE, L_ERR, "Can't open url file %s: %s\n", fname, strerror(errno));
			return -1;
		}
	}

	while (fgets(str1, sizeof(str1), url_file))
	{
		char *end;
		if (!str1[0]) continue;
		end = str1 + strlen(str1) - 1;
		while ((end >= str1) && (*end == '\r' || *end == '\n'))
		{
			*end = 0;
			if (end > str1) end--;
		}
		if (!str1[0]) continue;
		if (str1[0] == '#') continue;

		if(*end == '\\')
		{
			*end = 0; strcat(str, str1);
			continue;
		}
		strcat(str, str1);
		strcpy(str1, "");

		switch (action)
		{
		case URL_FILE_REINDEX:
			AddUrlLimit(str);
			break;
		case URL_FILE_INSERT:
			{
				CUrl url;
				if (url.ParseURL(str))
				{
					logger.log(CAT_ALL, L_WARNING , "Bad URL: %s\n", str);
					break;
				}
				char srv[STRSIZ];
				char robots[STRSIZ];
				sprintf(srv, "%s://%s/", url.m_schema, url.m_hostinfo);
				sprintf(robots, "%srobots.txt", srv);
				StoredHrefs.GetHref(database, NULL, robots, 0, 0, srv, flags & FLAG_SORT_HOPS ? 1 : 0, 86400);
				StoredHrefs.GetHref(database, str, 0, 0, srv, 0, 0);
			break;
			}
		case URL_FILE_PARSE:
			if (myurl.ParseURL(str))
			{
				logger.log(CAT_ALL, L_WARNING , "Bad URL: %s\n", str);
				return IND_ERROR;
			}
			break;
		}
		str[0] = 0;
	}
	if (action == URL_FILE_REINDEX)
		CalcLimit(database->m_sqlquery);
	if (url_file != stdin)
		fclose (url_file);
	return IND_OK;
}

int Startup(CSQLDatabaseI* database, char* fname, int flags)
{
	if (flags & FLAG_MARK)
	{
		if (fname)
			URLFile(database, fname, URL_FILE_REINDEX, flags);

		if (database->MarkForReindex() == IND_ERROR) return 1;
	}

	if (fname && (flags & FLAG_INSERT))
	{
		URLFile(database, fname, URL_FILE_INSERT, flags);
	}
	return database->Startup();
}

void RealIndex(CSQLDatabaseI* database, const char* url, int flags, int addtospace)
{
	database->Startup();
	CWordCache wordCache(WordCacheSize);
	wordCache.m_flags = flags;
	wordCache.m_database = database;
#ifdef TWO_CONNS
	CSQLDatabaseI* dbw = database->GetAnother();
	if (dbw != database)
	{
		if (!dbw->Connect(DBHost.c_str(), DBUser.c_str(), DBPass.c_str(), DBName.c_str(), DBPort))
		{
			logger.log(CAT_SQL, L_ERR, "ERROR: Can't connect to database. %s\n", dbw->DispError());
			dbw = database;
		}
	}
	wordCache.m_databasew = dbw;
#endif
	CHrefDeltaFilesR hfiles;
	if (IncrementalCitations)
	{
		if (hfiles.Open())
		{
			wordCache.m_hfiles = &hfiles;
		}
	}
	wordCache.RealIndex(url, addtospace); // Index to the "real-time" database
	database->m_squeue.SaveIndexed();
}


int LockDatabase(CFileLocker &fl)
{
	// Open "pid" file and lock it
	char fname[1000];
	// we do mkdir first to be sure DBWordDir exists ;)
	safe_mkdir(DBWordDir.c_str(), 0775, "index:");
	snprintf(fname, sizeof(fname), "%s/pid", DBWordDir.c_str());
	int fd = open(fname, O_CREAT | O_RDWR, 0600);
	if (fd < 0)
		fd = open(fname, O_RDWR);
	if (fd > 0)
	{
		if (fl.Lock(fd) == 0)
		{
			pid_t pid = getpid();
			int wr = write(fd, &pid, sizeof(pid));
			if (wr != sizeof(pid))
				logger.log(CAT_ALL, L_ERR, "Error writing pid: %s\n", strerror(errno));
			return fd;
		}
		else
		{
			close(fd);
			return -1;
		}
	}
	else
	{
		logger.log(CAT_ALL, L_ERR, "Error opening file %s: %s\n", fname, strerror(errno));
	}
	return fd;
}

int UnlockDatabase(CFileLocker &fl)
{
	// Unlock pid file and delete it
	fl.Unlock();
	close(fl.GetFd());
	char fname[1000];
	snprintf(fname, sizeof(fname), "%s/pid", DBWordDir.c_str());
	unlink(fname);
	return 1;
}

void Terminate()
{
	// Read pid of running index and send TERM signal to it
	char fname[1000];
	snprintf(fname, sizeof(fname),  "%s/pid", DBWordDir.c_str());
	int fd = open(fname, O_RDONLY);
	CFileLocker fl;
	if (fd >= 0)
	{
		if (fl.Lock(fd))
		{ // fd is locked by another index
			pid_t pid;
			if (read(fd, &pid, sizeof(pid)) == sizeof(pid))
			{
				logger.log(CAT_ALL, L_DEBUG, "Sending SIGTERM to process %d\n", pid);
				kill(pid, SIGTERM);
				logger.log(CAT_ALL, L_DEBUG, "Waiting for other index to finish ...");
				// wait for other index to finish and unlock file
				fl.LockWait(fd);
				logger.log(CAT_ALL, L_DEBUG, " done\n");
				fl.Unlock();
			}
		}
		else
		{
			logger.log(CAT_ALL, L_WARN, "Stale pid file %s/pid found - please remove\n", fname);
			fl.Unlock();
		}
		close(fd);
	}
	else
		logger.log(CAT_ALL, L_INFO, "No process to terminate\n");
}

void Index(CSQLDatabaseI* database, int flags, int maxthreads, char* fname, char* logname)
{
		safe_mkdir(DBWordDir.c_str(), 0775, "index:");

		char dir[1000], *pdir;
		safe_strcpy(dir, DBWordDir.c_str());
		pdir = dir + strlen(dir);
		for (ULONG i = 0; i < 100; i++)
		{
			sprintf(pdir, "/%02luw", i);
			safe_mkdir(dir, 0775, "index:");
		}
		strcpy(pdir, "/deltas");
		safe_mkdir(dir, 0775, "index:");

		strcpy(pdir, "/citations");
		safe_mkdir(dir, 0775, "index:");

		database->OpenLog(logname);
		// Prevent other index to run against the same database
		CFileLocker fl;
		int fd = LockDatabase(fl);
		if (fd > 0)
		{
			{
				DelMapReuse.Open();
				Startup(database, fname, flags); //	Initialize
				DelMapReuse.Close();
			}
			if (flags & FLAG_INSERT)
			{
				DelMapReuse.Open();
				database->StoreHrefs(flags & FLAG_SORT_HOPS);	// flag -i was specified
				DelMapReuse.Close();
			}
			else
			{
				// Main indexing object for all threads
				CWordCache wordCache(WordCacheSize);
				CDeltaFiles delta((flags & (FLAG_DELTAS | FLAG_SUBSETS)) == 0);	// 100 delta files
				CHrefDeltaFiles hdelta;
				if (IncrementalCitations)
				{
					if (hdelta.Open() == 0) return;
					wordCache.m_hfiles = &hdelta;
				}
				wordCache.m_flags = flags;
				wordCache.m_database = database;
#ifdef TWO_CONNS
				CSQLDatabaseI* dbw = database->GetAnother();
				if (dbw != database)
				{
					if (!dbw->Connect(DBHost.c_str(), DBUser.c_str(), DBPass.c_str(), DBName.c_str(), DBPort))
					{
						logger.log(CAT_SQL, L_ERR, "ERROR: Can't connect to database. %s\n", dbw->DispError());
						dbw = database;
					}
				}
				wordCache.m_databasew = dbw;
#endif
				wordCache.m_files = &delta;
				DelMap.Read();
				DelMapReuse.Open();
				if ((flags & (FLAG_DELTAS | FLAG_SUBSETS)) == 0)
				{
					CTmpUrlSource tmpus;
					if (flags & FLAG_TMPURL)
					{
						// Repeat previous indexing, used for debugging
						database->GetTmpUrls(tmpus);
						wordCache.m_tmpSrc = &tmpus;
					}
					else
					{
						database->DeleteTmp();	// Delete last indexing history
					}

#ifdef CWDEBUG
// Write debug output to cout
	Debug( libcw_do.set_ostream(&cout) );
// Turn debug object on
	Debug( libcw_do.on() );
#ifdef DEBUGMARKER
// Create marker
	libcw::debug::marker_ct* marker = new libcw::debug::marker_ct("A test marker");
#endif
#endif

					if (maxthreads == 1)
					{
						wordCache.Index();	// Index by single thread
					}
					else
					{
						// Index by multiple threads
						database->m_maxSlows = (maxthreads >> 3) + 1;
						wordCache.IndexN(maxthreads);
					}
					database->m_squeue.SaveIndexed();
#ifdef CWDEBUG
#ifdef DEBUGMARKER
	delete marker;
#endif
#endif
				}
				DelMapReuse.Close();
				if ((npages < 0) && ((((flags & FLAG_SUBSETS) == 0) || (flags & FLAG_DELTAS)) && (term == 0)))
				{
					// Build reverse index only if index was not terminated
					wordCache.m_files->Save(database);
					DelMap.Erase();
					if (IncrementalCitations)
					{
						hdelta.Save();
					}
					else
					{
						CalcRanks(database);
					}
					database->GenerateWordSite();
					database->CalcTotal();

//					CalcRanks(database);
//					database->CheckHrefDeltas();
				}
				else
				{
					DelMap.Write();
				}
				if ((term == 0) || ((flags & (FLAG_DELTAS | FLAG_SUBSETS)) != 0))
				{
					database->GenerateSubsets();
					database->GenerateSpaces();
					database->ClearCache();
				}
			}
			UnlockDatabase(fl);
		}
		else
		{
			logger.log(CAT_ALL, L_ERR, "Index already working\n");
		}
}

extern int InitCharset();

char * ConfErrMsg()
{
	return conf_err_str;
}

int ClearDir(const char *dirr)
{
	int res = IND_OK;
	struct dirent* de;
	DIR* dir = opendir(dirr);
	if (dir)
	{
		// Delete all files in the subdirectory
		while ((de = readdir(dir)))
		{
			struct stat st;
			char name[2000];
			if ((strcmp(de->d_name, ".") != 0) && (strcmp(de->d_name, "..") != 0))
			{
				sprintf(name, "%s/%s", dirr, de->d_name);
				stat(name, &st);
				if (!S_ISDIR(st.st_mode))
				{
					if (unlink(name))
					{
						res = IND_ERROR;
						logger.log(CAT_FILE, L_WARN, "Couldn't delete file %s: %s\n", name, strerror(errno));
					}
				}
			}
		}
		closedir(dir);
	}
	return res;
}

int ClearDatabase(CSQLDatabaseI* database)
{
	CFileLocker fl;
	int fd = LockDatabase(fl);
	if (fd > 0)
	{
		ULONG i;
		char dirr[1000], *pdir;
		safe_strcpy(dirr, DBWordDir.c_str());
		pdir = dirr + strlen(dirr);
		int res = IND_OK;
		// Delete files in all subdirectory of reverse index
		if (logger.getLevel() < L_DEBUG)
			logger.log(CAT_ALL, L_INFO, "Clearing files [");

		strcpy(pdir, "/citations/redir");
		unlink(dirr);
		for (i = 0; i < NUM_HREF_DELTAS; i++)
		{
			sprintf(pdir, "/citations/citation%lu", i);
			unlink(dirr);
			sprintf(pdir, "/citations/ccitation%lu", i);
			unlink(dirr);
			sprintf(pdir, "/citations/citationd%lu", i);
			unlink(dirr);
			sprintf(pdir, "/citations/ccitationd%lu", i);
			unlink(dirr);
		}
		for (i = 0; i < 200; i++)
		{
			sprintf(pdir, "/%02lu%c", i >> 1, i & 1 ? 's' : 'w');
			res = ClearDir(dirr);
			if (logger.getLevel() < L_DEBUG)
			{	// every fourth time, so we have 50 dots
				if (!(i % 4))
					logger.log(CAT_ALL, L_INFO, ".");
			}
			else
				logger.log(CAT_ALL, L_DEBUG, "Clear in: %s\n", dirr);
		}
		// Delete file with PageRanks
		strcpy(pdir, "/ranks");
		unlink(dirr);
		if (logger.getLevel() >= L_DEBUG)
			logger.log(CAT_ALL, L_DEBUG, "Delete %s\n", dirr);
		strcpy(pdir, "/ranksd");
		unlink(dirr);
		if (logger.getLevel() >= L_DEBUG)
			logger.log(CAT_ALL, L_DEBUG, "Delete %s\n", dirr);
		// Delete Site URL counts
		strcpy(pdir, "/siteurls");
		unlink(dirr);
		if (logger.getLevel() >= L_DEBUG)
			logger.log(CAT_ALL, L_DEBUG, "Delete %s\n", dirr);

		// Delete files from deltas
		strcpy(pdir, "/deltas");
		res = ClearDir(dirr);
		if (logger.getLevel() >= L_DEBUG)
			logger.log(CAT_ALL, L_DEBUG, "Delete %s\n", dirr);

		// Delete files from subsets
		strcpy(pdir, "/subsets");
		res = ClearDir(dirr);
		if (logger.getLevel() >= L_DEBUG)
			logger.log(CAT_ALL, L_DEBUG, "Delete %s\n", dirr);

		// Delete file with LastMods
		safe_strcpy(dirr, DBWordDir.c_str());
		pdir = dirr + strlen(dirr);
		strcpy(pdir, "/lastmod");
		unlink(dirr);
		if (logger.getLevel() >= L_DEBUG)
			logger.log(CAT_ALL, L_DEBUG, "Delete %s\n", dirr);
		strcpy(pdir, "/lastmodd");
		unlink(dirr);
		if (logger.getLevel() >= L_DEBUG)
			logger.log(CAT_ALL, L_DEBUG, "Delete %s\n", dirr);
		strcpy(pdir, "/delmap");
		unlink(dirr);
		if (logger.getLevel() >= L_DEBUG)
			logger.log(CAT_ALL, L_DEBUG, "Delete %s\n", dirr);
		strcpy(pdir, "/delmapr");
		unlink(dirr);
		if (logger.getLevel() >= L_DEBUG)
			logger.log(CAT_ALL, L_DEBUG, "Delete %s\n", dirr);

		if (logger.getLevel() < L_DEBUG)
			logger.log(CAT_ALL, L_INFO, "] done.\n");
		if (res != IND_ERROR)
		{
			// Delete all records from the database tables
			logger.log(CAT_SQL, L_INFO, "Clearing the SQL database ... ");
			res = database->Clear();
			database->ClearCache();
			if (res == IND_OK)
			{
				logger.log(CAT_SQL, L_INFO, "done.\n");
			}
			else
			{
				logger.log(CAT_SQL, L_ERR, "\nError clearing the SQL database: %s\n", database->DispError());
			}
		}
		UnlockDatabase(fl);
		return res;
	}
	else
	{
		logger.log(CAT_ALL, L_ERR, "Index already working\n");
		return IND_ERROR;
	}
}

void ShowPath(CSQLDatabaseI* database, const char* url)
{
	database->PrintPath(url);
}

void ShowStatistics(CSQLDatabaseI* database)
{
	database->GetStatistics();
}

void AddToSpace(CSQLDatabaseI* database, int space)
{
	if (urlLimits.size())
	{
		for (CStringVector::iterator url = urlLimits.begin(); url != urlLimits.end(); url++)
		{
			if (space > 0)
			{
				database->AddUrlToSpace(url->c_str(), space);
			}
			else
			{
				database->DeleteUrlFromSpace(url->c_str(), -space);
			}
		}
	}
	else
	{
		logger.log(CAT_SQL, L_ERR, "ERROR: No URL specified\n");
	}
}

void CheckDeleted(CSQLDatabase* db, int fix_deleted)
{
	CFileLocker fl;
	int fd = LockDatabase(fl);
	if (fd <= 0)
	{
		logger.log(CAT_ALL, L_ERR, "Index already working\n");
		return;
	}
	ULONG max_url = 0;
	CSQLAnswer* answ = db->sql_query(db->m_sqlquery->SetQuery("select max(url_id) from urlword", NULL));
	if (answ->FetchRow())
	{
		answ->GetColumn(0, max_url);
	}
	delete answ;
	logger.log(CAT_SQL, L_INFO, "Retrieving URLs\n");
	ULONG dmapsize;

	if (fix_deleted <= 2)
	{
#ifdef OPTIMIZE_CLONE
		answ = db->sql_query(db->m_sqlquery->SetQuery("select url_id from urlword where deleted=0 and status=200 and origin=1", NULL));
#else
		answ = db->sql_query(db->m_sqlquery->SetQuery("select url_id from urlword where deleted=0 and status=200", NULL));
#endif
		dmapsize = (max_url + 1) >> 3;
	}
	else
	{
		answ = db->sql_query(db->m_sqlquery->SetQuery("select url_id from urlword where deleted=0", NULL));
		dmapsize = ((max_url | ((DEL_CHUNK_SIZE << 3) - 1)) + 1) >> 3;
	}
	ULONG* dmap = new ULONG[dmapsize >> 2];
	memset(dmap, 0, dmapsize);
	while (answ->FetchRow())
	{
		ULONG url_id;
		answ->GetColumn(0, url_id);
		dmap[url_id >> 5] |= 1 << (url_id & 0x1F);
	}
	delete answ;
	CDeltaFiles* deltas = NULL;
	ULONG wc = 0;
	if (fix_deleted > 2)
	{
		ULONG mi = ((max_url + 1) >> 5);
		for (ULONG i = 0; i <= mi; i++)
			dmap[i] = ~(dmap[i]);
		char fname[PATH_MAX];
		sprintf(fname, "%s/delmap", DBWordDir.c_str());
		int f = open(fname, O_WRONLY | O_CREAT, 0664);
		if (f >= 0)
		{
			write(f, dmap, dmapsize);
			close(f);
		}
		else
		{
			logger.log(CAT_ALL, L_ERROR, "Can't open file %s for writing\n", fname);
		}
	}
	else
	{
		if (fix_deleted > 1)
		{
			deltas = new CDeltaFiles(1);
		}
		for (int i = 0; i < 100; i++)
		{
			logger.log(CAT_ALL, L_INFO, "Processing inverted index file: %i\n", i);
			char sites[PATH_MAX], urls[PATH_MAX], ind[PATH_MAX];
			char* fnames[] = {ind, sites, urls};
			CRevFile rev(i, 0, fnames);
			while (rev.m_avail)
			{
				while (rev.m_avail && (rev.m_siteOff <= rev.m_curInd.m_offset))
				{
					while (rev.m_avail && (rev.m_urlOff < rev.m_curSite.m_offset))
					{
						if ((rev.m_curUrl.m_urlID > max_url) || ((dmap[rev.m_curUrl.m_urlID >> 5] & (1 << (rev.m_curUrl.m_urlID & 0x1F))) == 0))
						{
							if (deltas)
							{
								CULONG100 w;
								WORD ww[3];
								*(ULONG*)ww = rev.m_curWord;
								ww[2] = 0;
								w.m_id = (ULONG*)ww;
								deltas->SaveWords(&w, &w + 1, rev.m_curUrl.m_urlID, rev.m_curSite.m_siteID);
								wc++;
							}
							else
							{
								logger.log(CAT_ALL, L_ERR, "Url: %lu, site: %lu, word: %lu\n", rev.m_curUrl.m_urlID, rev.m_curSite.m_siteID, rev.m_curWord);
							}
						}
						rev.SkipUrlContent();
						rev.ReadNextUrl();
					}
					rev.ReadNextSite();
				}
				rev.ReadNextWord();
			}
		}
	}
	delete dmap;
	if (deltas) delete deltas;
	UnlockDatabase(fl);
	if (wc)
	{
		logger.log(CAT_ALL, L_INFO, "%lu words deleted\n", wc);
	}
	logger.log(CAT_ALL, L_INFO, "Check finished\n");
}

void FixCitations(CSQLDatabase* db)
{
	CFileLocker fl;
	int fd = LockDatabase(fl);
	if (fd <= 0)
	{
		logger.log(CAT_ALL, L_ERR, "Index already working\n");
		return;
	}
	CHrefDeltaFiles hd;
	if (hd.Open("w+"))
	{
		ULONG max_url = 0;
		CSQLAnswer* answ = db->sql_query(db->m_sqlquery->SetQuery("select max(url_id) from urlword", NULL));
		if (answ->FetchRow())
		{
			answ->GetColumn(0, max_url);
		}
		delete answ;
		ULONG* usite = new ULONG[max_url + 1];
		memset(usite, 0, (max_url + 1) * sizeof(ULONG));
		logger.log(CAT_SQL, L_INFO, "Processing redirects\n");
		answ = db->sql_query(db->m_sqlquery->SetQuery("select url_id, redir, last_modified, site_id from urlword where status!=0", NULL));
		while (answ->FetchRow())
		{
			ULONG url_id, redir;
			answ->GetColumn(0, url_id);
			answ->GetColumn(1, redir);
			if (redir) hd.AddRedir(url_id, redir);
			char* lastmod = answ->GetColumn(2);
			if (lastmod && *lastmod) hd.AddLastmod(url_id, httpDate2time_t(lastmod));
			answ->GetColumn(3, usite[url_id]);
		}
		delete answ;
		for (ULONG u = 0; u < 16; u++)
		{
			char id[3];
			sprintf(id, "%02lu", u);
			CSQLParam p;
			p.AddParam(id);
			logger.log(CAT_SQL, L_INFO, "Processing hrefs from urlwords%02lu\n", u);
			CSQLAnswer* answ = db->sql_query(db->m_sqlquery->SetQuery("select uw.url_id,uw.hrefs from urlwords%s uw where deleted=0", &p));
			while (answ->FetchRow())
			{
				ULONG url_id, site_id;
				ULONG* hrefs;
				ULONG* ehrefs;
				answ->GetColumn(0, url_id);
				site_id = usite[url_id];
				hrefs = (ULONG*)answ->GetColumn(1);
				ehrefs = (ULONG*)((char*)hrefs + answ->GetLength(1));
				for (ULONG* h = hrefs; h < ehrefs; h++)
				{
					if (*h > max_url)
					{
						logger.log(CAT_ALL, L_WARN, "Outgoing href ID=%lu from URL=%lu is greater then max URL ID\n", *h, url_id);
					}
				}
				hd.SaveHrefs(hrefs, ehrefs, url_id, site_id);
			}
			delete answ;
		}
		delete usite;
		char fname[PATH_MAX];
		sprintf(fname, "%s/citations/redir", DBWordDir.c_str());
		unlink(fname);
		sprintf(fname, "%s/citations/lm", DBWordDir.c_str());
		unlink(fname);
		for (int i = 0; i < NUM_HREF_DELTAS; i++)
		{
			sprintf(fname, "%s/citations/citation%i", DBWordDir.c_str(), i);
			unlink(fname);
			sprintf(fname, "%s/citations/ccitation%i", DBWordDir.c_str(), i);
			unlink(fname);
			sprintf(fname, "%s/citations/citationd%i", DBWordDir.c_str(), i);
			unlink(fname);
			sprintf(fname, "%s/citations/ccitationd%i", DBWordDir.c_str(), i);
			unlink(fname);
		}
		hd.Save();
	}
	UnlockDatabase(fl);
}

#ifdef UNICODE
void ConvertToUTF(CSQLDatabase* db)
{
	CFileLocker fl;
	int fd = LockDatabase(fl);
	if (fd <= 0)
	{
		logger.log(CAT_ALL, L_ERR, "Index already working\n");
		return;
	}
	ULONG total = 0, maxid = 0, id = 0;
	db->sql_query(db->m_sqlquery->SetQuery("drop table wordurlu", NULL));
	db->sql_query(db->m_sqlquery->SetQuery("create table wordurlu(word varbinary(96) not null, word_id integer auto_increment primary key, urls blob, urlcount integer, totalcount integer)", NULL));
	CSQLAnswer* answ = db->sql_query(db->m_sqlquery->SetQuery("select count(*),max(word_id) from wordurl", NULL));
	if (answ && answ->FetchRow())
	{
		answ->GetColumn(0, total);
		answ->GetColumn(1, maxid);
	}
	ULONG c = 0;
	while (id <= maxid)
	{
		CSQLParam p;
		ULONG idt = id + 999;
		p.AddParam(&id);
		p.AddParam(&idt);
		answ = db->sql_query(db->m_sqlquery->SetQuery("select word, word_id, urls, urlcount, totalcount FROM wordurl WHERE word_id between :1 and :2", &p));
		while (answ->FetchRow())
		{
			ULONG word_id, u, t;
			CSQLParam p;
			BYTE uword[97];
			BYTE* pu = uword;
			BYTE* word = (BYTE*)answ->GetColumn(0);
			int len = answ->GetLength(0) >> 1;
			for (int i = 0; i < len; i++)
			{
				WORD w = HiLo ? ((word[0] << 8) + word[1]) : ((word[1] << 8) + word[0]);
				Utf8Code(pu, w);
				word += 2;
			}
			*pu = 0;
			p.AddParam((char*)uword);
			answ->GetColumn(1, word_id);
			p.AddParam(&word_id);
			p.AddParamEsc(answ->GetColumn(2), answ->GetLength(2));
			p.AddParam(answ->GetColumn(3, u) ? &u : (ULONG*)NULL);
			p.AddParam(answ->GetColumn(4, t) ? &t : (ULONG*)NULL);
			db->sql_real_query(db->m_sqlquery->SetQuery("INSERT INTO wordurlu(word,word_id,urls,urlcount,totalcount) VALUES (:1, :2, :3, :4, :5)", &p));
			if ((++c % 1000) == 0)
			{
				printf("\r%10lu records of %10lu total inserted", c, total);
				fflush(stdout);
			}
		}
		delete answ;
		id += 1000;
	}
	printf("\nCreating index\n");
	db->sql_query(db->m_sqlquery->SetQuery("create unique index wu on wordurlu(word)"));
	printf("\nTable \"wordurlu\" with UTF8 encoded field \"word\" has been created\n");
	printf("Rename this table to \"wordurl\" and put command \"UtfStorage yes\"\n");
	printf("to \"searchd.conf\" and \"aspseek.conf\"\n");
	UnlockDatabase(fl);
}
#endif

/* Fixme
void Import(const char* udb)
{
	CSQLDatabaseI database;
	if (database.Connect(DBHost.c_str(), DBUser.c_str(), DBPass.c_str(), DBName.c_str(), DBPort))
	{
		MYSQL_RES* res;
		string url;
		MYSQL_ROW row;
		do
		{
			char query[1000];
			sprintf(query, "SELECT url FROM %s.url WHERE url > '", udb);
			char* pq = strcpyq(query + strlen(query), url.c_str());
			strcpy(pq, "' LIMIT 1");
			res = database.sql_query(query);
			if (res == NULL)
			{
				printf("Can not execute query: %s\n", query);
				break;
			}
			row = mysql_fetch_row(res);
			if (row)
			{
				CUrl ur;
				ur.ParseURL(row[0]);
				if (((strcmp(ur.m_schema, "http") == 0) || (strcmp(ur.m_schema, "ftp") == 0) || (strcmp(ur.m_schema, "https") == 0)) && (*ur.m_hostinfo))
				{
					char srv[STRSIZ];
					char srv1[STRSIZ];
					sprintf(srv, "%s://%s/", ur.m_schema, ur.m_hostinfo);
					StoredHrefs.GetHref(&database, srv, 0, 0, srv, 0, 0);
					sprintf(srv1, "%srobots.txt", srv);
					StoredHrefs.GetHref(&database, srv1, 0, 0, srv, 0, 1);
					url = srv;
				}
				else
				{
					url = row[0];
				}
			}
			mysql_free_result(res);
		}
		while (row);
	}
	else{
		logger.log(CAT_SQL, L_ERR, "ERROR: Can't connect to database. %s\n", database.DispError());
	}
}

void MoveBlobs(int maxblob)
{
	CSQLDatabase database;
	if (database.Connect(DBHost.c_str(), DBUser.c_str(), DBPass.c_str(), DBName.c_str(), DBPort))
	{
		char query[200];
		sprintf(query, "SELECT word_id FROM wordurl WHERE octet_length(urls) > %i", maxblob);
		MYSQL_RES* res = database.sql_query(query);
		MYSQL_ROW row;
		while ((row = mysql_fetch_row(res)))
		{
			char fname[1000];
			sprintf(fname, "%s/%s", DBWordDir.c_str(), row[0]);
			char queryb[200];
			sprintf(queryb, "SELECT urls FROM wordurl WHERE word_id = %s", row[0]);
			MYSQL_RES* resb = database.sql_query(queryb);
			MYSQL_ROW rowb = mysql_fetch_row(resb);
			ULONG len = *mysql_fetch_lengths(resb);
			FILE* f = fopen(fname, "w");
			if (f)
			{
				fwrite(rowb[0], len, 1, f);
				fclose(f);
				sprintf(queryb, "UPDATE wordurl SET urls = '' WHERE word_id = %s", row[0]);
				database.sql_query2(queryb);
			}
			mysql_free_result(resb);
		}
		mysql_free_result(res);
	}
	else{
		logger.log(CAT_SQL, L_ERR, "ERROR: Can't connect to database. %s\n", database.DispError());
	}

}
*/
void alrmh(int x)
{
}

struct sigaction oldsegv;

void segv(int x)
{
	if (deltaFiles)
	{
		deltaFiles->Flush();
	}
	if (hdeltaFiles)
	{
		hdeltaFiles->Flush();
	}
	DelMapReuse.Close();
	logger.log(CAT_ALL, L_ERROR, "Address of param: %08x\n", (unsigned int)&x);
}


void termh(int x)
{
	if (term != 1)
		logger.log(CAT_ALL, L_INFO, "Terminating in progress...\n");
	term = 1;
}
/*
struct sigaction oldchld;

void chldh(int x)
{
	int status;
	pid_t child = waitpid(-1, &status, WNOHANG);
	if (child < 0)
	{
		logger.log(CAT_ALL, L_ERROR, "Error getting exited child pid\n");
	}
	else if (child == 0)
	{
		logger.log(CAT_ALL, L_ERROR, "Can't get exited child pid\n");
	}
	else
	{
		if (WIFSIGNALED(status))
		{
			logger.log(CAT_ALL, L_INFO, "(%i)Child process %i exited with status %i and signal %i\n",
				getpid(), child, WEXITSTATUS(status), WTERMSIG(status));
		}
		else
		{
			logger.log(CAT_ALL, L_INFO, "(%i)Child process %i exited with status %i\n",
				getpid(), child, WEXITSTATUS(status));
		}
	}
}

struct sigaction sigchld;
*/
struct sigaction sigsegv;
struct sigaction sigterm;
struct sigaction sigint;
struct sigaction sigalrm;

static char *config_name= "aspseek.conf";

void init_dblib_paths(void)
{
	string p;
	dblib_paths.clear();
	p = LIB_DIR;
	dblib_paths.push_back(p);
}

int main(int argc, char **argv)
{
	int flags = 0;
//	char *language = NULL;
	int clear = 0,stat = 0;
//	int integrity = 0;
//	int log2stderr = 1;
//	int dump = 0;
//	int makelastmod = 0;
	int add_servers = 1;
	extern char *optarg;
	extern int optind;
	int ch;
	int maxthreads = 1;
	int warnings = 1;
	int test_guesser = 0;
	char * url_fname = NULL;
	string DBName1;
//	int import = 0;
	int blobs = 0;
	int resolverCount = -1;
	string exe = argv[0];
	char* redir = NULL;
	char* showpath = NULL;
	char* realtime = NULL;
	char* logname = DEFAULT_LOGNAME;
	int ranks = 0;
	int trmn = 0;
	int total = 0;
	int addtospace = 0;
	int cutf = 0;
	int makesiteurls = 0;
	int fix_hrefs = 0;
	int fix_deleted = 0;

	// Sanity check - index should not be run as root or some other priv. user
	if ( (getuid() < MIN_ALLOWED_USER_ID) && (getgid() < MIN_ALLOWED_GROUP_ID) )
	{
		fprintf(stderr, "Don't run %s with privileged user/group ID (UID < %d, GID < %d)\n", argv[0], MIN_ALLOWED_USER_ID, MIN_ALLOWED_GROUP_ID);
		exit(1);
	}


	// This is just to set reference counter of static member
	// of STL string class to big value
	// so it will never reach 0 in SMP system
	long* ref = (long*)DBName1.data() - 2;
	*ref = 0x40000000;

	// Set SEGV handler
	sigsegv.sa_handler = segv;
	sigsegv.sa_flags = SA_RESETHAND; // the same as SA_ONESHOT
	sigemptyset(&sigsegv.sa_mask);
	sigaction(SIGSEGV, &sigsegv, &oldsegv);

	// Parse command line
	while ((ch = getopt(argc, argv, "X:HFUMVEKLCSahomdqiwlDxBb?t:u:s:n:L:D:N:R:f:c:r:P:T:g:A:G")) != -1)
	{
		switch (ch)
		{
		case 'X':
			fix_deleted = atoi(optarg);
			break;
		case 'H':
			fix_hrefs = 1;
			break;
		case 'b':
			cutf = 1;
			break;
		case 'l':
			flags |= FLAG_LOCAL;
			break;
		case 'A':
			addtospace = atoi(optarg);
			break;
		case 'F': flags |= FLAG_CHECKFILTERS; break;
		case 'M': flags |= FLAG_TMPURL; break;
		case 'U': total = 1; break;
		case 'g': logname = optarg; break;
		case 'E': trmn = 1; break;
		case 'K': ranks = 1; break;
//		case 'L': makelastmod = 1; break;
		case 'T': realtime = optarg; add_servers = 0; break;
		case 'P': showpath = optarg; break;
		case 'D': flags |= FLAG_DELTAS; break;
		case 'x': makesiteurls = 1; break;
		case 'B': flags |= FLAG_SUBSETS; break;
		case 'C': clear++; add_servers = 0; break;
		case 'S': stat++; add_servers = 0; break;
//		case 'I': integrity++; add_servers = 0; break;
//		case 'L': language = optarg; break;
		case 'q': add_servers = 0; break;
//		case 'l': log2stderr = 0; break;
		case 'a': flags |= FLAG_MARK; break;
//		case 'e': flags |= FLAG_EXP_FIRST; break;
		case 'o': flags |= FLAG_SORT_HOPS; break;
		case 'm': flags |= FLAG_REINDEX; break;
//		case 'k': flags |= FLAG_SKIP_LOCKING; break;
		case 'n': npages = atoi(optarg); break;
//		case 'd': dump = 1; break;
		case 'r': redir = optarg; break;
		case 't': AddTagLimit(optarg); break;
		case 's': AddStatusLimit(optarg); break;
		case 'u':
			AddUrlLimit(optarg);
			if (flags & FLAG_INSERT)
			{
				AddHref(optarg,0,0,0);
			}
			break;
/*
		case 'c':
			import = 1;
			DBName1 = optarg;
			break;
*/
		case 'N': maxthreads = atoi(optarg); break;
		case 'R': resolverCount = atoi(optarg); break;
		case 'f': url_fname = optarg; break;
		case 'i': flags |= FLAG_INSERT; break;
		case 'w': warnings = 0; break;
		case 'G': test_guesser = 1; break;
		case '?':
		case 'h':
		default:
			usage();
			return 1;
		}
	}
	argc -= optind; argv += optind;

	if(argc > 1)
	{
		// Wrong command-line, print help screen
		usage();
		return 1;
	}
	sigalrm.sa_handler = alrmh;
	sigalrm.sa_flags = 0;
	sigemptyset(&sigalrm.sa_mask);
	if (maxthreads > 1)
	{
		// Multiple threads are used, initialize multiple resolver processes
		if (resolverCount > maxthreads) resolverCount = maxthreads;
		if (resolverCount < 0) resolverCount = maxthreads / 5 + 1;
		if (resolverCount > 0)
		{
			resolverList.Init(resolverCount); // Spawn processes for IP address resolving
			logger.log(CAT_ALL, L_INFO, "Intializing resolver list, total count: %i, free count: %i\n", resolverList.m_count, resolverList.m_freeCount);
		}
	}
	if (argc == 1) config_name = argv[0];

#ifndef STATIC_DB_LINKING
	init_dblib_paths();
	// Loading db driver
	void* libh = NULL;
	for (int i = dblib_paths.size() - 1; i >= 0; i--)
	{
		string libfile = dblib_paths[i] + "/lib" + DBType + "db-" VERSION ".so";
		if ((libh = dlopen(libfile.c_str(), RTLD_NOW)) != NULL)
		{
			logger.log(CAT_FILE, L_DEBUG, "Loaded DB backend "
					"from %s\n", libfile.c_str());
			break;
		}
	}
	if (!libh) {
		logger.log(CAT_FILE, L_ERROR, "Can't load DB driver: %s\n", dlerror());
		exit(1);
	}
	dblib_paths.clear();

	CalcWordDir(); // Calculate location of reverse index files

	// Searching for newDataBaseI()
	PSQLDatabaseI (*newDataBaseI)(void) = (PSQLDatabaseI (*)(void))dlsym(libh, "newDataBaseI");
	CLogger ** plog = (CLogger **) dlsym(libh, "plogger");
	*plog = &logger;
	// Creating CSQLDataBaseI object
	CSQLDatabaseI * db = newDataBaseI();
#else
	CSQLDatabaseI * db = new CMySQLDatabaseI;
#endif
	if (LoadConfig(config_name, 0, (add_servers ? FLAG_ADD_SERV : 0))) // Load config
	{
		logger.log(CAT_ALL, L_ERR, "Error loading config: %s\n", ConfErrMsg());
		return 1;
	}

	if (!db->Connect(DBHost.c_str(), DBUser.c_str(), DBPass.c_str(), DBName.c_str(), DBPort))
	{
		logger.log(CAT_SQL, L_ERR, "ERROR: Can't connect to database. %s\n", db->DispError());
		return 1;
	}

// Set Names BINARY to deal with MySQL 4.1+
	db->sql_query(db->m_sqlquery->SetQuery("SET NAMES 'binary'", NULL));
	CalcLimit(db->m_sqlquery); // Calculate URL limit SQL WHERE condition
/*
	if (import)
	{
		Import(DBName1.c_str()); // Import of URLs from ASPseek database
	}
*/
	if (addtospace && (realtime == NULL))
	{
		AddToSpace(db, addtospace);
	}
	else if (stat)
	{
		ShowStatistics(db);
	}
	else if (ranks)
	{
		db->CalcTotal();
		CalcRanks(db);	// Calculate PageRanks for all URLs
	}
/*
	else if (makelastmod)
	{
		MakeLastMods();	// Make lastmod file from SQL database
	}
*/
	else if (total)
	{
		CalcTotal(db);
	}
	else if (showpath)
	{
		ShowPath(db, showpath);	// Show URLs, by which index found specified urls
	}
	else if (blobs)
	{
//		MoveBlobs(blobs);	// Obsolete
	}
	else if (clear)
	{
		// Clear entire database
		int clear_confirmed=0;
		if (warnings)
		{
			char str[5]="";
			printf("You are going to delete database '%s' content with limit: %s\n",
			       DBName.size() > 0 ? DBName.c_str() : "",
			       Limit.size() > 0 ? Limit.c_str() : "none");
			printf("Are you sure?(YES/no)");
			if (fgets(str, sizeof(str), stdin))
				if (!strncmp(str, "YES", 3))
					clear_confirmed = 1;
		}
		else
		{
			clear_confirmed = 1;
		}

		if (clear_confirmed)
		{
			InitCharset();

			if (Limit.size())
			{
				// Prevent other index to run against the same database
				CFileLocker fl;
				int fd = LockDatabase(fl);
				if (fd > 0)
				{

					CWordCache wordCache(WordCacheSize);
					CDeltaFiles delta((flags & (FLAG_DELTAS | FLAG_SUBSETS)) == 0);	//
					CHrefDeltaFiles hdelta;
					if (IncrementalCitations)
					{
						if (hdelta.Open() == 0) return 1;
						wordCache.m_hfiles = &hdelta;
					}
					wordCache.m_flags = flags;
					wordCache.m_database = db;
#ifdef TWO_CONNS
					CSQLDatabaseI* dbw = db->GetAnother();
					if (dbw != db)
					{
						if (!dbw->Connect(DBHost.c_str(), DBUser.c_str(), DBPass.c_str(), DBName.c_str(), DBPort))
						{
							logger.log(CAT_SQL, L_ERR, "ERROR: Can't connect to database. %s\n", dbw->DispError());
							dbw = db;
						}
					}
					wordCache.m_databasew = dbw;
#endif
					wordCache.m_files = &delta;
					DelMap.Read();
					ULONG nurls = db->DeleteUrls(&wordCache, Limit.c_str());
					db->m_squeue.SaveIndexed();

					if (nurls > 0 && term == 0)
					{
						// Build reverse index only if index was not terminated
						wordCache.m_files->Save(db);
						DelMap.Erase();
						if (IncrementalCitations)
						{
							hdelta.Save();
						}
						else
						{
							CalcRanks(db);
						}
						db->GenerateWordSite();
						db->CalcTotal();
						db->ClearCache();
					}
					else
					{
						DelMap.Write();
					}
					logger.log(CAT_SQL, L_ERR, "Deleted %d urls\n", nurls);
					UnlockDatabase(fl);
				}
			}
			else
			{
				if (url_fname == NULL)
				{
					// logger.log("Deleting...");
					// flush(stdout);
					if (ClearDatabase(db) != IND_OK)
					{
						logger.log(CAT_ALL, L_ERR, "Error clearing database\n");
					}
					// printf("Done\n");
				}
			}
		}
	}
	else if (trmn)
	{
		// Safely terminate running index, which used the same database
		Terminate();
	}
	else if (test_guesser)
	{
		db->CheckCharsetGuesser();
	}
	else if (cutf)
	{
#ifdef UNICODE
		ConvertToUTF(db);
#endif
	}
	else if (makesiteurls)
	{
		BuildSiteURLs(db);	// flag -x was specified
	}
	else if (fix_hrefs)
	{
		FixCitations(db);
	}
	else if (fix_deleted)
	{
		CheckDeleted(db, fix_deleted);
	}
	else
	{
		// Indexing
		InitCharset();
//		sigchld.sa_handler = chldh;
		sigint.sa_handler = termh;
		sigterm.sa_handler = termh;

//		sigchld.sa_flags = 0;
		sigint.sa_flags = 0;
		sigterm.sa_flags = 0;

//		sigemptyset(&sigchld.sa_mask);
		sigemptyset(&sigint.sa_mask);
		sigemptyset(&sigterm.sa_mask);

//		sigaction(SIGCHLD, &sigchld, &oldchld);
		sigaction(SIGALRM, &sigalrm, NULL);
		sigaction(SIGINT, &sigint, NULL);
		sigaction(SIGTERM, &sigterm, NULL);

		if (redir)
		{
			if (!logger.openlogfile(redir))
				logger.log(CAT_FILE, L_WARNING, "Can't open file %s for logging\n", redir);
		}

		if (realtime)
		{
			// Index to the "real-time" database
			RealIndex(db, realtime, flags, addtospace);
		}
		else
		{
			// Index to the main database
			max_failed_current = (maxthreads >> 2) + 1;
			// check thread stack size
			pthread_attr_t attr;
			size_t ss;
			pthread_attr_init(&attr);
			pthread_attr_getstacksize(&attr, &ss);
			if (ss < NEEDED_STACK_SIZE)
			{
				thread_stack_size = NEEDED_STACK_SIZE;
			}
			pthread_attr_destroy(&attr);
			Index(db, flags, maxthreads, url_fname, logname);
//			TestCacheN(db);
//			TestHrefCacheN(db);
		}
		delete db;
		logger.log(CAT_ALL, L_INFO, "index process finished.\n");
//		sigaction(SIGCHLD, &oldchld, NULL);
		return curUrl < npages ? term ? 2 : 0 : 1;
	}
	delete db;
}
