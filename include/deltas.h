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

/*  $Id: deltas.h,v 1.20 2002/07/18 14:13:24 al Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _DELTAS_H_
#define _DELTAS_H_

#include <stdio.h>
#include <pthread.h>
#include "defines.h"
#include "misc.h"
#include "documents.h"

class CSQLDatabase;
class CULONG100;

/*	Delta file format																						*/

/*	Offset		Length		Description																		*/
/*	0			4			Site ID																			*/
/*	4			8			URL ID																			*/
/*	8			2			Count of different words														*/
/*	10			4			Word ID																			*/
/*	14			2			Count of words in this URL														*/
/*	16			2			First position of word in this URL												*/
/*	.....																									*/
/*	16+(N-1)*2	2			Last position of word in this URL, where N is word count in this URL			*/
/*	---Repeated for the next word in the URL starting with Word ID---										*/
/*	.....																									*/
/*	---Repeated for the subsequent URLs starting with Site ID---											*/
/*	.....																									*/


/**
 * This class represents 100 "delta" files.
 * These files are used for collecting information
 * about new or changed URLs for words.
 * Index of particular file is ("word ID" mod 100)
 */
class CDeltaFiles
{
public:
	FILE* m_files[100];		///< Array of 100 buffered files
	pthread_mutex_t m_mutex[100];	///< Array of mutexes, protecting each of 100 files
	char* m_buf[100];		///< Buffers for stdio operations
	int m_deltaFd;

public:
	int CheckDeltas();
	void CloseDeltas();
	void DeleteBuffers();
	void CheckDelta(FILE* f);
	void Flush();					///< Writes buffers to disk for all files
	void Save(CSQLDatabase *database);		///< Updates reverse index, using info from all "delta" files
	ULONG Save(int file, CSQLDatabase* database, double& readTime, double& sortTime, ULONG& memory);	///< Updates reverse index, using info from specified "delta" file
	ULONG Save(int nfile, double& readTime, double& sortTime, ULONG& memory);
	CDeltaFiles(int checkDeltas = 0);
	~CDeltaFiles();
	/// Writes information to the delta files about several words
	void SaveWords(CULONG100* words, CULONG100* ewords, ULONG urlID, ULONG siteID);
};

extern CDeltaFiles* deltaFiles;

extern ULONG NumIterations;
extern int CompactStorage;
/*
class CULONG4
{
public:
	ULONG m_value;

public:
	int operator<(const CULONG4& l) const
	{
		return ((m_value & 0x7FFFFFFF) % NUM_HREF_DELTAS) < ((l.m_value & 0x7FFFFFFF) % NUM_HREF_DELTAS);
	}
};
*/

class CBufferedFile
{
public:
	int m_file;
	int m_flags;
	int m_dirty;
	int m_eof;
	char* m_buffer;
	ULONG m_bufSize;
	ULONG m_bufOffset;
	ULONG m_offsetInBuffer;
	ULONG m_bytesInBuffer;

public:
	CBufferedFile()
	{
		m_file = -1;
		m_buffer = NULL;
		m_bufSize = 0;
		m_bufOffset = 0;
		m_offsetInBuffer = 0;
		m_bytesInBuffer = 0;
		m_dirty = 0;
		m_eof = 0;
	}
	int Open(const char* name, int flags, ULONG bufSize, mode_t mode = 0600);
	int Close();
	virtual ~CBufferedFile();
	ULONG Read(void* buf, ULONG count);
	ULONG Write(void* buf, ULONG count);
	void Flush();
	ULONG Seek(long offset, int whence);
	ULONG Tell() {return m_bufOffset + m_offsetInBuffer;};
	virtual void Sync() {};
};

#ifdef USE_CSYNC_BUFFERED_FILE

class CSyncBufferedFile : public CBufferedFile
{
public:
	CEvent m_event;
	pthread_t m_thread;
	int m_finish;

public:
	CSyncBufferedFile();
	virtual ~CSyncBufferedFile();
	void* SyncFile();
	virtual void Sync();
};

#endif /* USE_CSYNC_BUFFERED_FILE */

#define URLINDSIZE (sizeof(ULONG) + sizeof(WORD))

class CRevFile
{
public:
//	FILE* m_ind;
//	FILE* m_sites;
//	FILE* m_urls;
	CBufferedFile m_ind;
	CBufferedFile m_sites;
	CBufferedFile m_urls;
	ULONG m_urlOff;
	ULONG m_siteOff;
//	char* m_indBuf;
//	char* m_sitesBuf;
//	char* m_urlsBuf;
	int m_io, m_so, m_uo;
	int m_avail;
	WordInd m_curInd;
	SiteInd m_curSite;
	UrlInd m_curUrl;
	ULONG m_curWord;
	ULONG m_curWord1;
	ULONG m_prevOffset;

public:
	CRevFile()
	{
//		m_ind = NULL;
//		m_sites = NULL;
//		m_urls = NULL;
//		m_indBuf = NULL;
//		m_sitesBuf = NULL;
//		m_urlsBuf = NULL;
		m_io = m_so = m_uo = 0;
		m_avail = 1;
		m_curInd.m_offset = 0;
		m_urlOff = 0;
		m_siteOff = 0;
		m_prevOffset = 0;
	}
	CRevFile(int index, int newFlag, char** fnames);
	void ReadNextWord();
	void ReadNextSite();
	void ReadNextUrl();
	void SkipUrlContent();
	void CopyToUrl(ULONG wordID, ULONG siteID, ULONG urlID, CRevFile& output);
	void CopyToSite(ULONG wordID, ULONG siteID, CRevFile& output);
	void CopyToWord(ULONG wordID, CRevFile& output);
	void WriteTill(ULONG wordID);
	void Write(ULONG urlID, ULONG* ppos, ULONG len);
	void WriteSite(ULONG siteID);
	void Close()
	{
//		if (m_ind) fclose(m_ind);
//		if (m_sites) fclose(m_sites);
//		if (m_urls) fclose(m_urls);
//		m_ind = NULL;
//		m_sites = NULL;
//		m_urls = NULL;
		m_ind.Close();
		m_sites.Close();
		m_urls.Close();
		m_io = m_so = m_uo = 0;
	}
	~CRevFile()
	{
		Close();
//		if (m_indBuf) delete m_indBuf;
//		if (m_sitesBuf) delete m_sitesBuf;
//		if (m_urlsBuf) delete m_urlsBuf;
	}
};

class CHrefSU
{
public:
	ULONG m_url;
	ULONG* m_site;

public:
	ULONG Site() const
	{
		return m_site[0];
	}
	ULONG URL() const
	{
		return m_site[1];
	}
	int operator<(const CHrefSU& wsu) const
	{
		int res;
		if (!(res = (m_url & 0x7FFFFFFF) - (wsu.m_url & 0x7FFFFFFF)))
		{
			if (!(res = Site() - wsu.Site()))
			{
				if (!(res = URL() - wsu.URL()))
				{
					if (!(res = (BYTE*)wsu.m_site - (BYTE*)m_site))
					{
						res = ((wsu.m_url & 0x80000000) >> 31) - ((m_url & 0x80000000) >> 31);
					}
				}
			}
		}
		return res < 0;
	}
	int operator==(const CHrefSU& wsu) const
	{
		return (Site() == wsu.Site()) && (URL() == wsu.URL()) && (m_url == wsu.m_url);
	}
	int operator!=(const CHrefSU& wsu) const
	{
		return (Site() != wsu.Site()) || (URL() != wsu.URL()) || (m_url != wsu.m_url);
	}
};

class CHref1
{
public:
	ULONG m_from;
	ULONG m_to;

public:
	ULONG To() const
	{
		return m_to & 0x7FFFFFFF;
	}
	int operator<(const CHref1& h) const
	{
		return m_from == h.m_from ? To() < h.To() : m_from < h.m_from;
	}
	int operator==(const CHref1& h) const
	{
		return (m_from == h.m_from) && (To() == h.To());
	}
	int operator!=(const CHref1& h) const
	{
		return (m_from != h.m_from) || (To() != h.To());
	}
};

class CCitationFile
{
public:
//	FILE* m_indCit;
//	FILE* m_citations;
//	char* m_indBuf;
//	char* m_citBuf;
	CBufferedFile m_indCit;
	CBufferedFile m_citations;
	ULONG m_current;
	ULONG m_curOffset;
	int m_io, m_co;

public:
	CCitationFile()
	{
//		m_indCit = NULL;
//		m_citations = NULL;
//		m_indBuf = NULL;
//		m_citBuf = NULL;
		m_current = 0;
		m_curOffset = 0;
		m_io = 0;
		m_co = 0;
	}
	CCitationFile(int index, int newFlag, char* fname, char* fnamec, const char* name = "citation");
	~CCitationFile()
	{
//		if (m_indBuf) delete m_indBuf;
//		if (m_citBuf) delete m_citBuf;
		Close();
	}
	void Close()
	{
//		if (m_indCit) fclose(m_indCit);
//		if (m_citations) fclose(m_citations);
		m_indCit.Close();
		m_citations.Close();
		m_io = 0;
		m_co = 0;
//		m_indCit = NULL;
//		m_citations = NULL;
	}
	int GetNextUrl(ULONG& urlID, ULONG& endOffset);
	void CopyTo(CCitationFile& nCit, ULONG start, ULONG end, ULONG url);
	void SaveUrl(CHrefSU*& start, CHrefSU* end);
	void SaveUrl(CHref1*& start, CHref1* end);
	void Merge(CCitationFile& nCit, ULONG start, ULONG end, CHrefSU*& pwsu, CHrefSU* ewsu);
	void Merge(CCitationFile& nCit, ULONG start, ULONG end, CHref1*& pwsu, CHref1* ewsu);
	void WriteIndTill(ULONG urlID, ULONG offset);
	void WriteInd(ULONG url);
};

#pragma pack(4)
class CUrlLm
{
public:
	ULONG m_urlID;
	time_t m_lm;

public:
	int operator<(const CUrlLm& u) const
	{
		return m_urlID < u.m_urlID;
	}
};

class CRedir
{
public:
	ULONG m_from;
	ULONG m_to;

public:
	ULONG From() const
	{
		return m_from & 0x7FFFFFFF;
	}
	int operator<(const CRedir& r) const
	{
		return From() < r.From();
	}
};
#pragma pack()

class CRedir4 : public CRedir
{
public:
	ULONG DeltaNum() const
	{
		return From() % NUM_HREF_DELTAS;
	}
	int operator<(const CRedir4& r) const
	{
		int res = DeltaNum() - r.DeltaNum();
		if (res == 0)
		{
			return CRedir::operator<(r);
		}
		else
		{
			return res < 0;
		}
	}
};

class CRedirMap
{
public:
	ULONG* m_redir;
	ULONG m_size;

public:
	~CRedirMap(void)
	{
		if (m_redir)
			delete m_redir;
	}
	ULONG* Find(ULONG url);
	ULONG GetRedirTo(ULONG url);
};

class CHrefDeltaFilesR
{
public:
	pthread_mutex_t m_mutex;	///< Mutex, protecting m_file
	pthread_mutex_t m_rmutex;	///< Mutex, protecting file
	pthread_mutex_t m_lmutex;	///< Mutex, protecting file
	FILE* m_redir;
	FILE* m_files;	///< Delta file;
	FILE* m_lmDelta;
	CFileLocker m_l_redir;		///< File lock for m_redir
	CFileLocker m_l_files;		///< File lock for m_files
	CFileLocker m_l_lmDelta;	///< File lock for m_lmDelta

public:
	CHrefDeltaFilesR();
	virtual ~CHrefDeltaFilesR();
	virtual void SetBuffers();
	virtual const char* GetNameP() {return ".r";};
	void Close();
	virtual void LockFiles();
	virtual void UnlockFiles();
	int Open(const char* flags = "a+");
	void Flush();					///< Writes buffers to disk for all files
	void Split(WORD count, FILE** files, FILE** dfiles, char** bufs, char** dbufs, CRedirMap& redirMap, long eoff);
	void SaveHrefs(ULONG* hrefs, ULONG* ehrefs, ULONG urlID, ULONG siteID);
	void AddRedir(ULONG from, ULONG to);
	void AddLastmod(ULONG url, time_t lm);
	void SaveLastmods();
	CRedir* SaveRedir(CRedirMap& redirMap, ULONG& redirLen);
	void AddRedirs(CRedir4* start, CRedir4* end);
	void Save();
	int Save(int index, FILE* file);
	ULONG SaveDirect(int index, FILE* file);
	void SaveReal();
};

class CHrefDeltaFiles : public CHrefDeltaFilesR
{
public:
	char* m_redirBuf;
	char* m_deltaBuf;
	char* m_lmDeltaBuf;

public:
	CHrefDeltaFiles();
	virtual ~CHrefDeltaFiles();
	virtual void SetBuffers();
	virtual void LockFiles() {};
	virtual void UnlockFiles() {};
	virtual const char* GetNameP() {return "";};
};

extern CHrefDeltaFilesR* hdeltaFiles;

/**
 * Auxiliary class used during updating of reverse index.
 * Used as template parameter of STL sort function. Instantiated in array,
 * used for sorting of particular "delta" file.
 */
class CWordSU
{
public:
	ULONG* m_site;	///< Pointer to site ID for word
	ULONG* m_word;	///< Pointer to word ID

public:
	ULONG Site() const
	{
		return m_site[0];
	}
	ULONG URL() const
	{
		return m_site[1];
	}
	int operator<(const CWordSU& wsu) const
	{
		int res;
		if (!(res = *m_word - *wsu.m_word))
		{
			if (!(res = Site() - wsu.Site()))
			{
				if (!(res = URL() - wsu.URL()))
				{
					res = (BYTE*)wsu.m_site - (BYTE*)m_site;
				}
			}
		}
		return res < 0;
	}
	int operator==(const CWordSU& wsu) const
	{
		return (Site() == wsu.Site()) && (URL() == wsu.URL()) && (*m_word == *wsu.m_word);
	}
	int operator!=(const CWordSU& wsu) const
	{
		return (Site() != wsu.Site()) || (URL() != wsu.URL()) || (*m_word != *wsu.m_word);
	}
};


#endif
