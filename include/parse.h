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

/*  $Id: parse.h,v 1.23 2002/12/18 18:38:53 kir Exp $
	Author: Alexander F. Avdonkin
	Uses parts of UdmSearch code
*/

#ifndef _PARSE_H_
#define _PARSE_H_

#include <pthread.h>
#include <string>
#include <unistd.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <stdio.h>
#include "defines.h"
#include "maps.h"
#include "documents.h"

class CReplaceVec;
class CCharsetB;
class CUrlContent;
class CWordCache;

int OpenHost(const char *hostname, int port, int timeout);

class CServer
{
public:
	CServer& operator=(CServer& p_server);
	CServer()
	{
		m_outside = 0;
		m_fd = -1;
		m_proxy_fd = -1;
		m_proxy_port = 0;
		m_period = DEFAULT_REINDEX_TIME;
		m_hint = 0;
		m_net_errors = 0;
		m_max_net_errors = MAXNETERRORS;
		m_read_timeout = READ_TIMEOUT;
		m_delete_no_server = 1;
		m_maxhops = DEFAULT_MAX_HOPS;
		/// used for MaxDocsPerServer option
		//m_server_maxdocs = DEFAULT_SERVER_MAX_DOCS;
		//m_server_cntdocs = 0;
		m_max_urls_per_site = (ULONG) -1;
		m_max_urls_per_site_per_index = (ULONG) -1;
		m_increment_redir_hops = 1;
		m_redir_loop_limit = DEFAULT_REDIR_LOOP_LIMIT;
		m_gindex = 1;
		m_gfollow = 1;
		m_deletebad = 0;
		m_userobots = 1;

		m_correct_factor = 1;
		m_incorrect_factor = 1;

		m_number_factor = 1;
		m_alnum_factor = 1;
		m_min_word_length = 1;
		m_max_word_length = 32;
		m_use_mirror = MIRROR_NO;
		m_use_clones = 1;
		m_replace = NULL;
		m_minDelay = 0;
		m_maxDocs = 1;
	}
	void Close()
	{
		if (m_fd >= 0)
		{
			close(m_fd);
			m_fd = -1;
		}
		if (m_proxy_fd >= 0)
		{
			close(m_proxy_fd);
			m_proxy_fd = -1;
		}
	}

	~CServer()
	{
		Close();
	}

	int OpenIfNot(char *hostname, int port, int timeout)
	{
		if (m_fd == -1) m_fd = OpenHost(hostname, port, timeout);
		return m_fd;
	}
	int OpenProxyIfNot(int timeout)
	{
		if (m_proxy_fd == -1) m_proxy_fd = OpenHost(m_proxy.c_str(), m_proxy_port, timeout);
		return m_proxy_fd;
	}
	void AddReplacement(char* str);

public:
	int  m_delete_no_server;
	int  m_gfollow;
	int  m_userobots;
	int  m_outside;
	int  m_maxhops;
	/// used for MaxDocsPerServer option
	//int m_server_maxdocs;
	//int m_server_cntdocs;
        ULONG m_max_urls_per_site;
        ULONG m_max_urls_per_site_per_index;
	int m_increment_redir_hops;
	int m_redir_loop_limit;
	int  m_fd;
	std::string m_proxy;
	int  m_proxy_port;
	int	 m_proxy_fd;
	std::string m_charset;
	std::string m_basic_auth;
	std::string m_htdb_list;
	std::string m_htdb_doc;
	std::string m_url;
	int  m_period;
	int  m_hint;
	int  m_net_errors;
	int  m_max_net_errors;
	int  m_read_timeout;
	int  m_gindex;
	int  m_deletebad;
	int  m_use_mirror;
	int  m_use_clones;
	int  m_minDelay;
	int  m_maxDocs;

	int  m_correct_factor;
	int  m_incorrect_factor;
	int  m_number_factor;
	int  m_alnum_factor;
	int  m_min_word_length;
	int  m_max_word_length;
	CReplaceVec* m_replace;
};

class CWordCache;
class CByteCounter;

/// Simple class which maps URL ids to running count of redirects
class CRedirectMap
{
public:
	pthread_mutex_t m_mutex;
	hash_map <ULONG, int> m_refs;	///< Map of URL ids to running redirect count
public:
	CRedirectMap()
	{
		pthread_mutex_init(&m_mutex, NULL);
	}
	~CRedirectMap()
	{
		pthread_mutex_destroy(&m_mutex);
	}
	void LimitClear(CServer* curSrv, CDocument* doc);	///< Clear count for this doc if exists in map
	int LimitNotExceeded(CServer* curSrv, CDocument* doc);	///< Check for exceeded limit or replace URL id and increment
};

class CUrl
{
public:
	char *m_schema;
	char *m_specific;
	char *m_hostinfo;
	char *m_hostname;
	char *m_path;
	char *m_filename;
	char *m_anchor;
	char *m_user;
	char *m_passwd;
	int  m_port;
	char* m_url;
	struct timeval m_time, m_etime;

public:
	int GetFromFile(char* buf, int maxsize);
	void Save(char* buf, int size);
	CServer* FindServer();
	CUrl()
	{
		Zero();
		m_time.tv_sec = m_etime.tv_sec = 0;
		m_time.tv_usec = m_etime.tv_usec = 0;
	}

	void Zero()
	{
		m_schema = m_specific = m_hostinfo = m_hostname = m_path = m_filename = m_anchor = m_url = m_user = m_passwd = NULL;
	}

	void Free()
	{
		if (m_schema) delete m_schema;
		if (m_specific) delete m_specific;
		if (m_hostinfo) delete m_hostinfo;
		if (m_hostname) delete m_hostname;
		if (m_path) delete m_path;
		if (m_filename) delete m_filename;
		if (m_anchor) delete m_anchor;
		if (m_url) delete m_url;
		if (m_user) delete m_user;
		if (m_passwd) delete m_passwd;
		Zero();
	}

	~CUrl()
	{
		Free();
	}
	int ParseURL(const char *s, int base = 0);
	int HTTPGet(char* header, char* buf, int fd, int maxsize, int read_timeout, CByteCounter* byteCounter);
	int HTTPSGet(char* header, char* buf, int fd, int maxsize, int read_timeout, CByteCounter* byteCounter);
	int HTTPGetUrl(CWordCache& wordCache, CServer* CurSrv, char* buf, int maxsize, int Method, char* extra);
	int HTTPGetUrlAndStore(CWordCache& wordCache, char* buf, int maxsize, CDocument& doc);
	void SetNewOrigin(const char *crc, CUrlContent *ucont, CServer *srv, CWordCache *wordCache);
};

class CMapDirToConf : public hash_map<std::string, CMapDirToConf*>
{
public:
	CServer* m_server;

public:
	CMapDirToConf()
	{
		m_server = NULL;
	}
	void AddServer(const char* dir, CServer& server);
	CServer* GetServer(const char* dir);
};

extern char conf_err_str[STRSIZ];

class CParsedContent;

int ParseHtml(
	CParsedContent* ucontent,
	CUrl* curURL,
	CServer* CurSrv,
	CDocument* doc,
	char* content,
	int index,int follow,
	char *text,
	char *keywords,
	char *descript,
	char *title);

typedef hash_map<std::string, CServer> CMapStringToServer;
extern CMapStringToServer Servers;

typedef std::vector<std::string> CStringVector;

class CMapStringToStringVec : public hash_map<std::string, CStringVector>
{
public:
	pthread_mutex_t mutex;

public:
	CMapStringToStringVec()
	{
		pthread_mutex_init(&mutex, NULL);
	}
	~CMapStringToStringVec()
	{
		pthread_mutex_destroy(&mutex);
	}
	iterator find(const char* param);
	CStringVector& operator[](const char* param);
};

extern CMapStringToStringVec Robots;
extern time_t AddressExpiry;


int SetDefaultCharset(int id);
int GetCharset(const char *alias);

CServer* FindServer(const char *url);
void AddServer(CServer& server);
char* Find2CRs(char* buf, int size);

#endif /* _PARSE_H_ */
