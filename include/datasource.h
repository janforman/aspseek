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

/*  $Id: datasource.h,v 1.21 2002/12/18 18:38:53 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _DATASOURCE_H_
#define _DATASOURCE_H_

/* This header file contains definitions of classes used for communication
 * between search script and search daemon
 */

/* Class hierarchy tree
 * CDataSource
 * +---CRemoteDataSource
 * 	+---CTcpDataSource
 * 	+---CMutliTcpDataSource

 */

#include <pthread.h>
#include <errno.h>
#include <string>
#include "my_hash_map"
#include "my_hash_set"
#include "documents.h"
#include "maps.h"
#include "sock.h"
#include "aspseek.h"
#include "templates.h"

using std::string;

class CUrlW;
class CStringSet;
//class CSrcDocument;

/**
 * Base and root class in hierarchy of classes used for communication
 * between search script and search daemon
 */
class CCgiQuery;

class CDataSource
{
public:
	/// Must return list of clones with given CRC, excluding origin
	virtual void GetCloneList(const char *crc, CUrlW* origin, CSrcDocVector &docs) = 0;
};

/// Base class for remote connection
class CRemoteDataSource : public CDataSource
{
public:
	CTemplate* m_templ;
	virtual ~CRemoteDataSource() {};
	virtual int GetResultCount(int len, const char* query, ULONG* counts) = 0;
	/// Return nFirst resluts with maximal ranks, called after GetResultCount
	virtual ULONG GetResults(ULONG cnt, CUrlW* res, int gr, ULONG nFirst) = 0;
	/// Return words to highlight in results
	virtual void GetWords(CStringSet& LightWords) = 0;
	/// Return site name by site ID
	virtual int GetSite(CUrlW* url, char* sitename) = 0;
	virtual int GetSiteCount(CCgiQuery& query, ULONG* counts) = 0;
	/// Return URL info by URL ID
	virtual void GetDocument(CUrlW* url, CSrcDocumentR& doc) = 0;
	virtual void GetDocuments(CUrlW* url, CUrlW* urle) = 0;
	virtual void GetNextDocument(CUrlW* url, CSrcDocumentR& doc) = 0;
	/// Add query statistics to the database
	virtual void AddStat(const char *pquery, const char* ul, int np, int ps, int urls, int sites, timeval &start, timeval &finish, char* spaces, int site) = 0;
	/// Process query and return site count
	virtual int GetSiteCount(int len, const char* query, ULONG* counts) = 0;
	/// Return statistical info about results for last query
	virtual int GetCounts(ULONG* counts) = 0;
	virtual void SetGroup(ULONG totalSiteCount) = 0;
	/// Finish processing query at all daemons
	virtual void Quit() = 0;
	/// Finish processing query at all daemons
	virtual void Close() = 0;
	/// Retrieve error string for query
	virtual void GetError(char* error) = 0;
	/// Print cached copy of URL
//	virtual void PrintCached(aspseek_request *r, int len, const char* query) {};
	virtual void PrintCached(aspseek_request *r, CCgiQuery& query) {};
};

/// Class used for communication with single daemon
class CTcpDataSource : public CRemoteDataSource
{
public:
	/// Buffered socket for communication with daemon
	CBufferedSocket m_socket;
	int m_connected;
public:
	CTcpDataSource(CTemplate *t)
	{
		m_templ = t;
		m_socket = -1;
		m_connected = 0;
	}
	virtual void Quit()
	{
		m_socket.SendULONG(0);
	}
	~CTcpDataSource();
	int Recv(void* buf, int bytes, int flags = 0);	///< Safe recv
	virtual void Close();			///< Close socket if opened
	int Connected();				///< Check if connected to daemon
	int Connect(int address, int port);		///< Connects to daemon
	void GetDocuments1(ULONG cnt, ULONG* urls);
	void SetGroup(ULONG thisSiteCount, ULONG totalSiteCount);

	virtual int GetResultCount(int len, const char* query, ULONG* counts);
	virtual ULONG GetResults(ULONG cnt, CUrlW* res, int gr, ULONG nFirst);
	virtual void GetWords(CStringSet& LightWords);
	virtual int GetSite(CUrlW* url, char* sitename);
	virtual void GetDocument(CUrlW* url, CSrcDocumentR& doc);
	virtual void GetDocuments(CUrlW* url, CUrlW* urle);
	virtual void GetNextDocument(CUrlW* url, CSrcDocumentR& doc);
	virtual void AddStat(const char *pquery, const char* ul, int np, int ps, int urls, int sites, timeval &start, timeval &finish, char* spaces, int site);
	virtual void GetCloneList(const char *crc, CUrlW* origin, CSrcDocVector &docs);
	virtual void GetError(char* error);
	virtual int GetSiteCount(int len, const char* query, ULONG* counts);
	virtual int GetSiteCount(CCgiQuery& query, ULONG* counts);
	virtual int GetCounts(ULONG* counts);
	virtual void SetGroup(ULONG totalSiteCount);
//	virtual void PrintCached(aspseek_request *r, int len, const char* query);
	virtual void PrintCached(aspseek_request *r, CCgiQuery& query);
};

/// Class used for passing parameter to the particular thread
class CSearchParams
{
public:
	int m_len;			///< Query length
	const char* m_query;		///< Query itself
	ULONG m_counts[5];		///< ([0]) site count, ([1]) result count, ([2]) total result count, ([3]) total URL count, ([4]) info about query complexity
	int m_result;			///< Result of particual query
	CUrlW* m_res;			///< Buffer for results
	CStringSet m_words;		///< Words to highlight
	CCgiQuery* m_cgiQuery;
};

enum
{
	QUIT = 0,
	GET_RESULT_COUNT = 1,
	GET_RESULTS = 2,
	GET_WORDS = 3,
	GET_SITE_COUNT = 4,
	GET_COUNTS = 5,
	SET_GROUP = 6,
	GET_DOCUMENTS1 = 7,
	GET_SITE_COUNTQ = 8
};

class CMultiTcpDataSource;

/// Class used for holding thread search context information
class CTcpContext
{
public:
	const CDaemonAddress* m_address;	///< IP address and port of daemon
	CEvent m_req, m_answer;			///< Event object for sending query and waiting answer
	CTcpDataSource* m_source;		///< Associated datasource for particular daemon
	int m_command;				///< Command to process
	CSearchParams m_param;			///< Command parameters
	CMultiTcpDataSource* m_parent;		///< Parent datasource
	CTemplate* m_templ;

public:
	CTcpContext(CMultiTcpDataSource* parent);
};

/// Class used for sumultaneous communication with multiple daemons
class CMultiTcpDataSource : public CRemoteDataSource
{
public:
	vector<CTcpContext*> m_sources;		///< Array of datasources for each daemon
	int m_connected;			///< Set to if connected to at least one daemon
	CEvent m_event;				///< Event object for waiting first connection to daemon

public:
	CMultiTcpDataSource(CTemplate *t)
	{
		m_templ = t;
		m_connected = 0;
	}
	virtual ~CMultiTcpDataSource();
	int Connect(CDaemonSet& daemons);	///< Connects to at least 1 daemon

	virtual int GetResultCount(int len, const char* query, ULONG* counts);
	virtual ULONG GetResults(ULONG cnt, CUrlW* res, int gr, ULONG nFirst);
	virtual void GetWords(CStringSet& LightWords);
	virtual void GetError(char* error);
	virtual void GetCloneList(const char *crc, CUrlW* origin, CSrcDocVector &docs);
	virtual int GetSite(CUrlW* url, char* sitename);
	virtual void GetDocument(CUrlW* url, CSrcDocumentR& doc);
	virtual void GetDocuments(CUrlW* url, CUrlW* urle);
	virtual void GetNextDocument(CUrlW* url, CSrcDocumentR& doc);
	virtual int GetSiteCount(int len, const char* query, ULONG* counts);
	virtual int GetSiteCount(CCgiQuery& query, ULONG* counts);
	virtual int GetCounts(ULONG* counts);
	virtual void SetGroup(ULONG totalSiteCount);
	virtual void AddStat(const char *pquery, const char* ul, int np, int ps, int urls, int sites, timeval &start, timeval &finish, char* spaces, int site);
	virtual void Quit();
	virtual void Close();
};

class CPosColor
{
public:
	ULONG m_color;
	ULONG m_pos;

public:
	int operator<(const CPosColor& pc) const
	{
		return m_color == pc.m_color ? m_pos < pc.m_pos : m_color < pc.m_color;
	}
};

#endif
