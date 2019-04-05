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

/*  $Id: parse.cpp,v 1.93 2003/05/05 05:59:14 matt Exp $
 *  Author : Alexander F. Avdonkin
 *	Uses parts of UdmSearch code
 *	Implemented classes: CUrl, CMapDirToConf
 */

#include "aspseek-cfg.h"
#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <string>
#ifdef _AIX
#include <arpa/onameser_compat.h>
#else
#include <arpa/nameser.h>
#endif
#include <resolv.h>

#ifdef USE_HTTPS
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "sqldbi.h"
#include "parse.h"
#include "index.h"
#include "charsets.h"
#include "config.h"
#include "filters.h"
#include "md5.h"
#include "sock.h"
#include "misc.h"
#include "logger.h"
#include "datetime.h"
#include "timer.h"
#include "mimeconv.h"
#include "ucharset.h"
#include "content.h"
#include "rfc2396.h"

#define RFC2396_ESCAPE(ch, flag) (rfc2396_escapes[(unsigned)(ch)] & (flag))
#define MD5_DIGEST(data, size, digest) md5_buffer_str(data, size, digest)

CMapStringToServer Servers;

hash_map<string, CMapDirToConf> ServersD;

CRedirectMap rmap;

CMapStringToStringVec::iterator CMapStringToStringVec::find(const char* param)
{
	CLocker lock(&mutex);
	return hash_map<string, CStringVector>::find(param);
}

CStringVector& CMapStringToStringVec::operator[](const char* param)
{
	CLocker lock(&mutex);
	return hash_map<string, CStringVector>::operator[](param);
}

int FindRobots(char *host, char* path, char* name)
{
	CMapStringToStringVec::iterator it = Robots.find(host);
	if (it == Robots.end()) return -1;
	CStringVector& v = it->second;
	char* fpath = (char*)alloca(strlen(path) + strlen(name) + 1);
	sprintf(fpath, "%s%s", path, name);
	for (CStringVector::iterator s = v.begin(); s != v.end(); s++)
	{
		if (strstr(fpath, s->c_str()))
		{
			return 1;
		}
	}
	return -1;
}

CServer& CServer::operator=(CServer& server)
{
	m_delete_no_server = server.m_delete_no_server;
	m_gfollow = server.m_gfollow;
	m_userobots = server.m_userobots;
	m_outside = server.m_outside;
	m_maxhops = server.m_maxhops;
	//m_server_maxdocs = server.m_server_maxdocs;
	//m_server_cntdocs = server.m_server_cntdocs;
	m_max_urls_per_site = server.m_max_urls_per_site;
	m_max_urls_per_site_per_index = server.m_max_urls_per_site_per_index;
	m_increment_redir_hops = server.m_increment_redir_hops;
	m_redir_loop_limit = server.m_redir_loop_limit;
	m_fd = server.m_fd;
	m_proxy = server.m_proxy;
	m_proxy_port = server.m_proxy_port;
	m_proxy_fd = server.m_proxy_fd;
	m_charset = server.m_charset;
	m_basic_auth = server.m_basic_auth;
	m_htdb_list = server.m_htdb_list;
	m_htdb_doc = server.m_htdb_doc;
	m_url = server.m_url;
	m_period = server.m_period;
	m_hint = server.m_hint;
	m_net_errors = server.m_net_errors;
	m_max_net_errors = server.m_max_net_errors;
	m_read_timeout = server.m_read_timeout;
	m_gindex = server.m_gindex;
	m_deletebad = server.m_deletebad;
	m_use_mirror = server.m_use_mirror;
	m_use_clones = server.m_use_clones;
	m_correct_factor = server.m_correct_factor;
	m_incorrect_factor = server.m_incorrect_factor;
	m_number_factor = server.m_number_factor;
	m_alnum_factor = server.m_alnum_factor;
	m_min_word_length = server.m_min_word_length;
	if (server.m_max_word_length > MAX_WORD_LEN)
	{
		logger.log(CAT_FILE, L_ERR, "Warning [%s]: "
			"MaxWordLength can't be set to %d; "
			"limiting it to %d\n", m_url.c_str(),
			server.m_max_word_length, MAX_WORD_LEN);
		m_max_word_length = MAX_WORD_LEN;
	}
	else
	{
		m_max_word_length = server.m_max_word_length;
	}
	m_replace = server.m_replace;
	m_minDelay = server.m_minDelay;
	m_maxDocs = server.m_maxDocs;
	return *this;
}

void CRedirectMap::LimitClear(CServer* curSrv, CDocument* doc)
{
	hash_map<ULONG, int>::iterator it;
	if ((it = m_refs.find(doc->m_referrer)) != m_refs.end())
	{
		m_refs.erase(it);
	}
}

/** This method checks for an exceeded redirect loop limit by maintaining a
 * running count which is passed on to the referred to document on each
 * redirect encountered. Returns 0 if limit exceeded else 1.
 */
int CRedirectMap::LimitNotExceeded(CServer* curSrv, CDocument* doc)
{
	hash_map<ULONG, int>::iterator it;
	// Find redirect count for document that redirected us here.
	if ((it = m_refs.find(doc->m_referrer)) != m_refs.end())
	{
		int refcount = it->second;
		m_refs.erase(it);
		if (refcount < curSrv->m_redir_loop_limit)
		{
			// Increment counter and insert as redirect count for
			// urlID (document that was referred to) into map.
			m_refs[doc->m_urlID] = ++refcount;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		m_refs[doc->m_urlID] = 1;
	}
	return 1;
}

char* Remove2Dot(char *path)
{
	char *ptr;
	char *tail;

	if (!(ptr = strstr(path, "../"))) return path;
	if (ptr == path) return path; /* How could it be? */
	tail = ptr + 2;
	ptr--;
	*ptr = 0;
	if (!(ptr = strrchr(path, '/')))
	{
		*path = 0;
	}
	else
	{
		if ((ptr == path) || (ptr[-1] != '/'))
		{
			*ptr = 0;
		}
	}
	path = strcat (path,tail);
	return Remove2Dot(path);
}

/** This method splits the string src into dst1 and dst2 on boundary c.
 * c is included as the first char of dst2 if incl is true otherwise
 * it is excuded from dst2.  The return value is the number of parts src
 * was split into.  The behaviour should be identical to:
 *   if incl  -> sscanf(src, "%[^c]%s", dst1, dst2)
 *   if !incl -> sscanf(src, "%[^c]c%s", dst1, dst2)
 * with the exception that, unlike sscanf(), whitespace is allowed
 * in dst2 (see 'man sscanf' notes on %s).  Assumes that dst1 and dst2
 * are large enough to hold all of src.
 */
static int splitstr(const char *src, char *dst1, char *dst2, int c, int incl)
{
	int parts = 0;
	char *start = (char *) src;
	char *ss = start;
	while (*ss && *ss != c) ss++;
	if ((ss - start) > 0)
	{
		memcpy(dst1, start, ss - start);
		dst1[ss - start] = 0;
		parts++;
		if (*ss == c && !incl) ss++;
		if (*ss)
		{
			strcpy(dst2, ss);
			parts++;
		}
	}
	return parts;
}

int CUrl::ParseURL(const char *s, int base)
{
	char *ss;
	int len;

	Free();
	len = strlen(s) + 1;
	if (len > MAX_URL_DATA)
	{
		logger.log(CAT_ALL, L_WARN, "URL is too long: %s\n", s);
		return 1;
	}
	m_url = strdup(s);

	m_schema = new char[len]; m_schema[0] = 0;
	m_specific = new char[len]; m_specific[0] = 0;
	m_hostinfo = new char[len]; m_hostinfo[0] = 0;
	m_hostname = new char[len]; m_hostname[0] = 0;
	m_anchor = new char[len]; m_anchor[0] = 0;
	m_port = 0;
	m_path = new char[len]; m_path[0] = 0;
	m_filename = new char[len]; m_filename[0] = 0;

	if (splitstr(s, m_schema, m_specific, ':', 0) != 2)
	{
		if (base)
		{
			switch (splitstr(s, m_hostinfo, m_path, '/', 1))
			{
				case 1: strcpy(m_path,"/"); break;
				case 2: break;
				default: return 1;
			}
			sscanf(m_hostinfo, "%[^:]:%d", m_hostname, &m_port);
		}
		else
		{
			strcpy(m_path, s);
		}
		strcpy(m_schema,"");
		strcpy(m_specific,"");
	}
	else
	{
		// Scheme is case-insensitive, according to RFC 1738, 2.1
		char *p = m_schema;
		while (*p)
		{
			*p = tolower(*p); p++;
		}

		if (!strcmp(m_schema, "ftp") || (!strcmp(m_schema, "http")) ||
			(!strcmp(m_schema, "https")) || (!strcmp(m_schema, "news")) ||
			(!strcmp(m_schema, "nntp")))
		{
			int parts = 0;
			if (!strncmp(m_specific, "//", 2))
			{
				parts = splitstr(m_specific + 2, m_hostinfo, m_path, '/', 1);
			}
			switch (parts)
			{
				case 1: strcpy(m_path,"/"); break;
				case 2: break;
				default:
					logger.log(CAT_ALL, L_WARN, "Bad URL format: %s\n", m_specific);
					return 1;
			}

			char *auth = new char[len]; auth[0] = 0;
			char *host = new char[len]; host[0] = 0;

			switch (splitstr(m_hostinfo, auth, m_hostname, '@', 0))
			{
				case 1:
					strcpy(host, auth);
					break;
				case 2:
					m_user = new char[len]; m_user[0] = 0;
					m_passwd = new char[len]; m_passwd[0] = 0;

					strcpy(host, m_hostname);
					splitstr(auth, m_user, m_passwd, ':', 0);
					break;
				default: break;
			}

			if (sscanf(host, "%[^:]:%d", m_hostname, &m_port) == 1)
			{
				strcpy(m_hostname, host);
			}

			delete auth;
			delete host;
		}
		else if (!strcmp(m_schema, "file"))
		{
			strcpy(m_path, m_specific);
		}
		else if (!strcmp(m_schema, "htdb"))
		{
			strcpy(m_path, m_specific);
		}
		else
		{
			logger.log(CAT_ALL, L_DEBUG, "Unsupported protocol in %s\n", s);
			return 1;
		}
	}

	if (strchr(m_hostname, '\'') || (strchr(m_hostname, '\\'))) return 1;
	if ((ss = strstr(m_path, "#"))) *ss = 0;
	m_path = Remove2Dot(m_path);
	if (m_path[0] != '/')
	{
		if (!strncmp(m_path, "./", 2))
			strcpy(m_filename, m_path + 2);
		else
			strcpy(m_filename, m_path);
		strcpy(m_path, "");
	}
	char* q;
	if ((q = strchr(m_path, '?'))) *q = 0;
	if ((ss = strrchr(m_path,'/')) && ((strcmp(ss, "/") || q)))
	{
		if (q) *q = '?';
		strcpy(m_filename, ss + 1);
		ss[1] = 0;
		return 0;
	}
	if (q) *q = '?';
	return 0;
}

/** Convert a character to it's escaped equivalent.
 */
static const char char2hex_table[] = "0123456789ABCDEF";
static inline BYTE *char2hex(unsigned ch, BYTE *hex)
{
	*hex++ = '%';
	*hex++ = char2hex_table[ch >> 4];
	*hex++ = char2hex_table[ch & 0xF];
	return hex;
}

/** This method translates all SGML entities in URI into equivalent character codes
 * Ref: <URL:http://www.w3.org/TR/html4/sgml/dtd.html#URI>
 * Ref: <URL:http://www.w3.org/TR/html4/types.html#type-cdata>
 * Additionally we should also follow the recommendations regarding non ASCII
 * characters [those not already escaped]
 * Ref: <URL:http://www.w3.org/TR/html4/appendix/notes.html#non-ascii-chars>
 */
static void URIUnescapeSGML(const char *uri, string& unescaped, int charset)
{
	char *copy = (char *) alloca(2 * strlen(uri) + 1);
	BYTE *text = (BYTE *) uri;
	BYTE *q = (BYTE *) copy;
	if (!text) return;
	while (text && *text)
	{
		if (*text == '&')
		{
			WORD n;
			BYTE *s = text;
			if ((n = SgmlToChar((const char*&)++text, charset)))
			{
				// Make sure we don't mistake unterminated entities ...
				// e.g. without this check "&amplifier=on" would become "&lifier=on"
				if (*(text-1) != ';' && isalnum(*text))
				{
					*q++ = *s++;
					text = s;
				}
				// Store converted byte
				else if (n <= 255)
				{
					*q++ = n;
				}
				// Convert to utf-8 and store %escape sequence
				else
				{
					// Utf8Code() encodes at most 3 bytes although UTF-8 can use
					// up to 4 bytes per character.  Play it safe in case this
					// changes at some latter date.
					ULONG utf8 = 0;
					BYTE *x, *y = (BYTE *) &utf8;
					x = y;
					Utf8Code(x, n);
					for (int i = x - y; i > 0; i--)
					{
						q = char2hex(*y, q);
						y++;
					}
				}
			}
			else
			{
				*q++ = *s++;
				text = s;
			}
		}
		else
		{
			*q++ = *text++;
		}
	}
	*q = 0;
	unescaped = copy;
}

int HTTPResponseType(int status)
{

	switch (status)
	{
	case 1:
		return 1;

	case 200:
		return HTTP_STATUS_OK;

	case 301: // Moved Permanently
	case 302: // Moved Temporarily
	case 303: // See Other
		return HTTP_STATUS_REDIRECT;

	case 304: // Not Modified
		return HTTP_STATUS_NOT_MODIFIED;

	case 300: // Multiple Choices
	case 305: // Use Proxy (proxy redirect)
	case 400: // Bad Request ??? We tried ...
	case 401: // Unauthorized
	case 402: // Payment Required
	case 403: // Forbidden
	case 404: // Not found
	case 405: // Method Not Allowed
	case 406: // Not Acceptable
	case 407: // Proxy Authentication Required
	case 408: // Request Timeout
	case 409: // Conflict
	case 410: // Gone
	case 411: // Length Required
	case 412: // Precondition Failed
	case 413: // Request Entity Too Large
	case 414: // Request-URI Too Long
	case 415: // Unsupported Media Type
	case 500: // Internal Server Error
	case 501: // Not Implemented
	case 502: // Bad Gateway
	case 505: // Protocol Version Not Supported
		return HTTP_STATUS_DELETE;

	case 503: // Service Unavailable
	case 504: // Gateway Timeout
		return HTTP_STATUS_RETRY;

	default:
		return HTTP_STATUS_UNKNOWN;
	}
}

static int NET_BUF_SIZE = 4096;

char* Find2CRs(char* buf, int size)
{
	char* f = buf;
	char* e = buf + size;
	while (f < e)
	{
		f = (char*)memchr(f, '\n', e - f);
		if (f == NULL) return NULL;
		if ((f + 1 < e) && (f[1] == '\n'))
		{
			return f;
		}
		if ((f > buf) && (f[-1] == '\r') && (f + 2 < e) && (f[1] == '\r') && (f[2] == '\n'))
		{
			return f - 1;
		}
		f++;
	}
	return NULL;
}

int CUrl::HTTPGetUrlAndStore(CWordCache& wordCache, char* buf, int maxsize, CDocument& doc)
{
	char reason[STRSIZ] = "";
	int new_url = (doc.m_status == 0);
	CUrlContent ucontent;
	ucontent.m_cache = &wordCache;
	ucontent.m_url.m_urlID = doc.m_urlID;
	ucontent.m_url.m_siteID = doc.m_siteID;
	ucontent.m_url.m_oldRedir = doc.m_redir;
	ucontent.m_url.m_new = new_url;
	ucontent.m_url.m_lmod = (*doc.m_last_modified.c_str() != 0);

	int flags = wordCache.m_flags;
	CServer* CurSrv = FindServer();
	if (CurSrv == NULL)
	{
		// Don't delete robots.txt
		if (strcmp(m_filename,"robots.txt"))
		{
			wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
			wordCache.MarkDeleted(doc.m_urlID, doc.m_siteID, m_url, new_url);
			logger.log(CAT_ALL, L_INFO, "No \"Server\" command for URL %s - deleted.\n", m_url);
		}
		return IND_OK;
	}
	// Check that hops is less than MaxHops
	if (doc.m_hops > CurSrv->m_maxhops)
	{
		wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
		wordCache.MarkDeleted(doc.m_urlID, doc.m_siteID, m_url, new_url);
		return IND_OK;
	}
	int Method = FilterType(m_url, reason, &wordCache);
	if (Method == DISALLOW)
	{
		if (!strcmp(m_filename,"robots.txt"))
		{
			wordCache.m_database->DeleteRobotsFromHost(m_hostinfo);
			wordCache.m_database->LoadRobotsOfHost(m_hostinfo);
		}
		else
		{
			wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
			wordCache.MarkDeleted(doc.m_urlID, doc.m_siteID, m_url, new_url);
		}
		return IND_OK;
	}

	// Check whether URL is disallowed by robots.txt
	if (CurSrv->m_userobots && (FindRobots(m_hostinfo, m_path, m_filename) >= 0))
	{
		wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
		wordCache.MarkDeleted(doc.m_urlID, doc.m_siteID, m_url, new_url);
		return IND_OK;
	}
	else if (!strcmp(m_schema, "ftp") && !CurSrv->m_proxy.size())
	{
		logger.log(CAT_ALL, L_WARN, "The ftp protocol is not supported without proxy\n");
		wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
		wordCache.MarkDeleted(doc.m_urlID, doc.m_siteID, m_url, new_url);
		return IND_OK;
	}
#ifndef USE_HTTPS
	else if (!strcmp(m_schema, "https"))
	{
		logger.log(CAT_ALL, L_WARN, "The https protocol is not supported in this build\n");
		wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
		wordCache.MarkDeleted(doc.m_urlID, doc.m_siteID, m_url, new_url);
		return IND_OK;
	}
#endif
	// This implementation is broken when not using "Server" command.  For example, if there
	// are no "Server" commands in aspseek.conf and we insert initial URLs into database using
	// "index -i -f somefile" instead, then MaxDocsPerServer here becomes limit of ALL sites
	// rather than a per site limit.  This is due to the fact that there is only one CServer
	// (hash key = "") and therefore only one counter.  MaxDocsPerServer has been replaced
	// with MaxURLsPerServerPerIndex.  -matt
	//
	// MaxDocsPerServer
	// We only want to get max docs, -1 for no limit
	//if ((CurSrv->m_server_maxdocs != -1) && (CurSrv->m_server_cntdocs > CurSrv->m_server_maxdocs))
	//{
	//	wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
	//	wordCache.m_database->MarkDeleted(doc.m_urlID);
	//	logger.log(CAT_ALL, L_INFO, "MaxDocsPerServer reached for URL %s - deleted.\n", m_url);
	//	return IND_OK;
	//}
	switch (wordCache.LimitReached(doc.m_siteID, m_url, new_url))
	{
		// Don't delete the URL unless incrementing would exceed MaxURLsPerServer ...
		case 1: // We overshot MaxURLsPerServerPerIndex ...
			return IND_OK;
		case 2: // We overshot MaxURLsPerServer ...
			wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
			wordCache.MarkDeleted(doc.m_urlID, doc.m_siteID, m_url, new_url);
			logger.log(CAT_ALL, L_INFO, "MaxURLsPerServer reached for URL %s - deleted.\n", m_url);
			return IND_OK;
		default:
			break;
	}

	ucontent.m_follow = CurSrv->m_gfollow;
	ucontent.m_curSrv = CurSrv;
	char extra[1000] = "";
	int reindex = flags & FLAG_REINDEX;
	if (doc.m_status && !reindex){
	// According to RFC 2616, we should use both If-Modified-Since
	// and If-None-Match if we have both  --kir.
		if (doc.m_last_modified.c_str()[0])
			sprintf(extra,"If-Modified-Since: %s\r\n", doc.m_last_modified.c_str());
		if (doc.m_etag.c_str()[0])
			sprintf(extra+strlen(extra),"If-None-Match: %s\r\n", doc.m_etag.c_str());
	}

	if ((!strcmp(m_schema,"http")) || (!strcmp(m_schema,"https")) || (!strcmp(m_schema, "ftp")))
	{

		// User agent
		sprintf(extra + strlen(extra), "User-Agent: %s\r\n", UserAgent());

		// If LocalCharset specified
		if (wordCache.local_charset)
		{
			sprintf(extra + strlen(extra), "Accept-charset: %s\r\n",
				charsetMap.CharsetsStr(wordCache.local_charset));
		}

		// if Basic Auth is needed
		if (!(CurSrv->m_basic_auth.empty()))
		{
			sprintf(extra + strlen(extra), "Authorization: Basic %s\r\n",
				CurSrv->m_basic_auth.c_str());
		}
	}

	strcat(extra, ExtraHeaders());

	int size = HTTPGetUrl(wordCache, CurSrv, buf, maxsize, Method, extra);
	if (size > 0) wordCache.AddSize(size);

	int result = IND_UNKNOWN;
	int status;
	char* content=NULL;
	switch(size)
	{
	case NET_UNKNOWN:
		status = HTTP_STATUS_NOT_SUPPORTED;
		result = IND_OK;
		break;
	case NET_TIMEOUT:
		status = HTTP_STATUS_TIMEOUT;
		result = IND_OK;
		break;
	case NET_CANT_CONNECT:
		status = HTTP_STATUS_UNAVAIL;
		result = IND_OK;
		break;
	case NET_CANT_RESOLVE:
		status = HTTP_STATUS_UNAVAIL;
		result = IND_OK;
		break;
	default:
		if (size < 0)
		{	/* No connection */
			status = HTTP_STATUS_UNAVAIL;
			result = IND_OK;
		}
		else
		{
			// Document has been successfully loaded
			// Cut HTTP response header first
			content = Find2CRs(buf, size);
			if (content)
			{
				char cr = *content;
				*content = 0;
				content += cr == '\r' ? 4 : 2;
				ucontent.m_url.m_content = content;
			}
			else
			{
				status = HTTP_STATUS_UNAVAIL;
				result =IND_OK;
			}
		}
	}
	if (result)
	{
		CurSrv->m_net_errors++;
		return size;
	}

	// count pages that are indexed for MaxDocsPerServer
	//CurSrv->m_server_cntdocs++;
	wordCache.IncrementIndexed(doc.m_siteID, m_url, new_url);

	/* Let's start parsing */
	char* header = buf;
	int realsize = size;
	int changed;
	status = changed = 1;
	ULONG origin = 0;
	char *content_type, *location, *statusline;
	char *real_content_type;
	std::string content_type_nocs;
	content_type = location = statusline = NULL;
	real_content_type = NULL;
	char etag[MAX_ETAG_LEN] = "";
	time_t exp_time = 0;
	char title[MAXTITLESIZE] = "";
	char keywords[MAXKEYWORDSIZE] = "";
	char subj[MAXTITLESIZE] = "";
	char from[MAXKEYWORDSIZE] = "";
	char last_modified[32] = "";
	char descript[MAXDESCSIZE] = "";
	char digest[33];
	char text[2 * MAXTEXTSIZE] = "";
	char charset[16] = "";
	title[0] = subj[0] = from[0] = keywords[0] = digest[0] = descript[0] = charset[0] = 0;

	int index	= CurSrv->m_gindex;
	int follow	= CurSrv->m_gfollow;

	size -= (content - buf);	// Could be modified by Content-Length
	realsize -= (content - buf);	// Will be safe for md5sum, converters etc.
	ucontent.m_url.m_size = size;

	// Now let's parse response header lines
	char* lt;
	int headerlen = strlen(header);
	if (headerlen > 10000)
	{
		if (wordCache.m_database->m_logFile)
		{
			fprintf(wordCache.m_database->m_logFile, "Warning, header size > 10000 bytes. URL: %s\nContent:\n%s\n", m_url, header);
			fflush(wordCache.m_database->m_logFile);
		}
	}
	char* hcopy = (char*)alloca(headerlen + 1);
	strcpy(hcopy, header);
	char* tok = GetToken(header, "\r\n", &lt);
	while (tok)
	{
		if (!STRNCASECMP(tok,"HTTP/"))
		{
			status = atoi(tok + 8);
			statusline = tok;
		}
		else if (!STRNCASECMP(tok, "Content-Type: "))
		{
			char *p;
			content_type = tok + 14;
			real_content_type = content_type;
			if ((p = strstr(content_type, "charset=")))
			{
				strncpy(charset, p + 8, sizeof(charset) - 1);
				charset[sizeof(charset) - 1] = '\0';
				Trim(charset, " ");
				char * sc = strchr(content_type, ';');
				if (sc != NULL)
					content_type_nocs.assign(content_type,
							sc - content_type);
			}
			else
				content_type_nocs = content_type;
		}
		else if (!STRNCASECMP(tok, "Location: "))
		{
			location = tok + 10;
		}
		else if (!STRNCASECMP(tok, "Content-Length: "))
		{
			size = atoi(tok + 16);
		}
		else if (!STRNCASECMP(tok, "ETag: "))
		{
			strncpy(etag, tok + 6, MAX_ETAG_LEN);
		}
		else if (!STRNCASECMP(tok, "Expires: "))
		{
			exp_time = httpDate2time_t(tok + 9);
		}
		else if (!STRNCASECMP(tok, "Subject: "))
		{
			STRNCPY(title, tok + 9);
			strcpy(subj, title);
		}
		else if (!STRNCASECMP(tok, "From: "))
		{
			STRNCPY(from, tok + 6);
		}
		else if (!STRNCASECMP(tok, "Newsgroups: "))
		{
			STRNCPY(keywords, tok + 12);
		}
		else
		if (!STRNCASECMP(tok, "Last-Modified: "))
		{
			STRNCPY(last_modified, tok + 15);
		}
		tok = GetToken(NULL, "\r\n", &lt);
	}

	if (!CurSrv->m_increment_redir_hops)
	{
		if (HTTPResponseType(status) != HTTP_STATUS_REDIRECT)
		{
			CLocker lock(&rmap.m_mutex);

			rmap.LimitClear(CurSrv, &doc);
		}
	}

	switch (HTTPResponseType(status))
	{
	case 1: /* No HTTP code */
		CurSrv->m_net_errors++;
		ucontent.UpdateUrl(status, CurSrv->m_period);
		result = IND_OK;
		break;

	case HTTP_STATUS_OK:
		if (!content_type)
		{
			CurSrv->m_net_errors++;
			ucontent.UpdateUrl(status, CurSrv->m_period);
			result = IND_OK;
		}
		break;

	case HTTP_STATUS_REDIRECT: // We'll try to use Location: xxx instead
		if (!strcmp(m_filename,"robots.txt"))
		{	// Special case: we pretend we got a 404
			// since robots.txt should never redirect.
			status = HTTP_STATUS_NOT_FOUND;
			wordCache.m_database->DeleteRobotsFromHost(m_hostinfo);
			wordCache.m_database->LoadRobotsOfHost(m_hostinfo);
			ucontent.UpdateUrl(status, CurSrv->m_period);
		}
		else
		{
			ULONG hrID = 0;
			int follow_redirect = 1;
			if (!CurSrv->m_increment_redir_hops)
			{
				{
					CLocker lock(&rmap.m_mutex);

					follow_redirect = rmap.LimitNotExceeded(CurSrv, &doc);
				}
				if (!follow_redirect)
					logger.log(CAT_ALL, L_INFO, "RedirectLoopLimit reached for URL %s\n", m_url);
			}
			if (follow_redirect && (doc.m_hops < CurSrv->m_maxhops) && location)
			{
				// According to RFC 2616 rhs is absoluteURI,
				// however we need to be a little flexible
				// here all the same. -matt
				CUrl newURL;
				string location_unescaped;
				char *location_trim = str_trim(location);
				URIUnescapeSGML(location_trim, location_unescaped, ucontent.m_charset);
				if (!newURL.ParseURL(location_unescaped.c_str()))
				{
					int newMethod;
					char srv[STRSIZ], str[STRSIZ];
					char *newschema, *host, *path;
					if (newURL.m_schema[0])
						newschema = newURL.m_schema;
					else
						newschema = m_schema;
					if (!strcmp(newschema, "file") || !strcmp(newschema, "htdb"))
					{
						host = NULL;
						path = newURL.m_path[0] ? newURL.m_path : m_path;
						sprintf(str, "%s:%s%s", newschema, path, newURL.m_filename);
						sprintf(srv, "%s:%s/", newschema, path);
					}
					else
					{
						host = newURL.m_hostinfo[0] ? newURL.m_hostinfo : m_hostinfo;
						path = newURL.m_path[0] ? newURL.m_path : m_path;
						sprintf(str, "%s://%s%s%s", newschema, host, path, newURL.m_filename);
						sprintf(srv, "%s://%s/", newschema, host);
					}
					Remove2Dot(str);
					if(!STRNCMP(str, "ftp://") && (strstr(str,";type=")))
							*(strstr(str, ";type=")) = '\0';
						newMethod = FilterType(str, reason, &wordCache);

					if ((newMethod != DISALLOW))
					{
						if (host && (FindRobots(host, path, newURL.m_filename) >= 0) && CurSrv->m_userobots)
						{
						}
						else
						{
							int add=1;
							/* compare hostinfo in some cases */
							if (((!CurSrv->m_delete_no_server) && (!CurSrv->m_outside)) && (Sites.find(srv) == Sites.end()))
							{
								add = !strcmp(m_schema, newschema);
								if (add && host)
								{
									add = !strcmp(m_hostinfo, host);
								}
							}
							if (add)
							{
								CLocker lock(&wordCache);
								hrID = wordCache.GetHref(str, CurSrv, doc.m_urlID,
									(CurSrv->m_increment_redir_hops) ? doc.m_hops + 1 : doc.m_hops , srv);
								if (hrID == doc.m_urlID)
								{
									hrID = 0;
								}
								// Add robots.txt for HTTP schema
								// When FollowOutside or DeleteNoServer no
								if ((!strcmp(newschema, "http")) && CurSrv->m_userobots &&
									(CurSrv->m_outside || (!CurSrv->m_delete_no_server)))
								{
									char str1[STRSIZ];
									snprintf(str1, STRSIZ, "%s://%s/%s", newschema, host, "robots.txt");
									wordCache.GetHref(str1, NULL, 0, 0, srv, 86400);
								}
							}
						}
					}
				}
			}
			ucontent.UpdateUrl(status, CurSrv->m_period, hrID);
		}
		result = IND_OK;
		break;

	case HTTP_STATUS_NOT_MODIFIED: /* Not Modified, nothing to do */
		ucontent.UpdateUrl(status, CurSrv->m_period);
		result = IND_OK;
		break;

	case HTTP_STATUS_DELETE:
		// Delete it if is not robots.txt
		if (!strcmp(m_filename, "robots.txt"))
		{
			wordCache.m_database->DeleteRobotsFromHost(m_hostinfo);
			wordCache.m_database->LoadRobotsOfHost(m_hostinfo);
			ucontent.UpdateUrl(status, CurSrv->m_period);
		}
		else
		{
			if (result != IND_ERROR)
			{
				if (CurSrv->m_deletebad)
				{
#ifdef OPTIMIZE_CLONE
					if (doc.m_origin == 1)
					{
						SetNewOrigin(doc.m_crc.c_str(), &ucontent, CurSrv, &wordCache);
					}
#endif
					wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
					wordCache.MarkDeleted(doc.m_urlID, doc.m_siteID, m_url, new_url);
				}
				else
				{
					ucontent.UpdateUrl(status, CurSrv->m_period);
				}
			}
		}
		result = IND_OK;
		break;

	case HTTP_STATUS_RETRY: // We'll retry later, maybe host is down
		CurSrv->m_net_errors++;
		ucontent.UpdateUrl(status, CurSrv->m_period);
		result = IND_OK;
		break;

	default: // Unknown status, retry later
		ucontent.UpdateUrl(status, CurSrv->m_period);
		result = IND_OK;
	}

	// Override next index time if server sent "Expires:" header
	// and it is later than after our Period  --kir.
	if (exp_time > ucontent.m_url.m_period)
		ucontent.m_url.m_period = exp_time;

	// Return if Content parsing is not required
	if (result)
	{
		return result;
	}

#ifdef USE_EXT_CONV
	// Try to run converter
	CConverters::iterator it = converters.find(content_type_nocs.c_str());
	if (it != converters.end()) // conv. found
	{
		char * res = it->second->Convert(content, realsize, maxsize, doc.m_url.c_str());
		if (res != NULL)
		{
			content_type = res;
			strncpy(charset, it->second->m_charset.c_str(),
				sizeof(charset) - 1);
			charset[sizeof(charset) - 1] = '\0';
		}
	}
#endif /* USE_EXT_CONV */

	//if (!STRNCASECMP(content_type, "text/plain") && (!strcmp(m_filename,"robots.txt")))

	// Hmmmh, don't look at content type! If it's robots.txt we always (!) want to
	// drop in here... Why? Because it is common for sites to have default Error
	// Document handlers that output (with status 200) their main website page.
	// The commented out test above would result in erronious "robots.txt" pages
	// being indexed (and subsequently appearing in search results), which, really,
	// we don't want! :) No one should ever serve out anything other that robot info
	// under this filename and it should be safe to assume this to always be the
	// case ...
	if (!strcmp(m_filename,"robots.txt"))
	{
		// Ok, now we're here, if we didn't get content_type "text/plain" assume
		// site has a ErrorDocument handler that outputs their main page or such
		// which of course we do not want to do a ParseRobots on...
		if (!STRNCASECMP(content_type, "text/plain"))
		{
			result = wordCache.ParseRobots(content, m_hostinfo);
			if (result != IND_ERROR) wordCache.m_database->LoadRobotsOfHost(m_hostinfo);
		}
		else
		{	// We pretend we got a 404 otherwise.
			status = HTTP_STATUS_NOT_FOUND;
			wordCache.m_database->DeleteRobotsFromHost(m_hostinfo);
			wordCache.m_database->LoadRobotsOfHost(m_hostinfo);
			ucontent.UpdateUrl(status, CurSrv->m_period);
			return IND_OK;
		}
	}
	else
	// plain text or something like that
	if (!STRNCASECMP(content_type, "text/plain") ||
		!STRNCASECMP(content_type, "text/tab-separated-values") ||
		!STRNCASECMP(content_type, "text/css"))
	{
		if (charset[0])
		{
			ucontent.RecodeContent(content, GetCharset(charset), wordCache.local_charset);
			ucontent.SetOriginalCharset(charset);
		}
		else
		{
			int DCindex;
			const char* charset;
#ifdef USE_CHARSET_GUESSER
			if (!(DCindex = langs.GetLang(content)) && CurSrv->m_charset.size())
			{
				charset = CurSrv->m_charset.c_str();
				DCindex = GetCharset(charset);
			}
			else
			{
#ifdef UNICODE
				charset = DCindex > 0 ? GetCharsetName(DCindex) : (const char*)NULL;
#else
				charset = DCindex > 0 ? charsetMap.CharsetsStr(DCindex) : (const char*)NULL;
#endif
			}
#else
			charset = CurSrv->m_charset.c_str();
			DCindex = GetCharset(charset);
#endif
			ucontent.RecodeContent(content, DCindex, wordCache.local_charset);
			if (charset && charset[0]) ucontent.SetOriginalCharset(charset);
		}

		if (Method != HEAD)
		{
			MD5_DIGEST(content, realsize, digest);
			changed = strcmp(digest, doc.m_crc.c_str());
			if (CurSrv->m_use_clones)
			{
//				CLocker lock(&wordCache);
				{
					CTimerAdd timer(wordCache.m_origin);
					origin = wordCache.m_database->FindOrigin(digest, doc.m_urlID, doc.m_siteID);
				}

#ifdef OPTIMIZE_CLONE
				if (changed && doc.m_origin == 1 && doc.m_status )
				{
					SetNewOrigin(doc.m_crc.c_str(), &ucontent, CurSrv, &wordCache);
				}
				if (origin)
				{
					ucontent.m_url.m_origin = 0;
					wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
					if (doc.m_origin == 1)
					{
						wordCache.m_database->UpdateOrigin(doc.m_urlID);
					}
					if (doc.m_status != 0) ucontent.ClearF(); else ucontent.Clear();
				}
				else
				{
					ucontent.m_url.m_origin = 1;
				}
#else
				if (origin)
				{
					wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
					ucontent.Clear();
				}
#endif

			}
		}
		if (index && (!origin) && (changed || reindex))
		{
			strncpy(text, content, MAXTEXTSIZE - 2);
			text[MAXTEXTSIZE-1] = 0;
			WORD pos = 1;
			ucontent.ParseText(content, pos, CurSrv);
		}
	}
	else if (!STRNCASECMP(content_type, "text/html"))
	{
		if (charset[0])
		{
			ucontent.RecodeContent(content, GetCharset(charset), wordCache.local_charset);
			ucontent.SetOriginalCharset(charset);
		}
		else
		{
			int DCindex;
			const char* charset;
#ifdef USE_CHARSET_GUESSER
			if (!(DCindex = langs.GetLang(content)) && CurSrv->m_charset.size())
			{
				charset = CurSrv->m_charset.c_str();
				DCindex = GetCharset(charset);
			}
			else
			{
#ifdef UNICODE
				charset = DCindex > 0 ? GetCharsetName(DCindex) : (const char*)NULL;
#else
				charset = DCindex > 0 ? charsetMap.CharsetsStr(DCindex) : (const char*)NULL;
#endif
			}
#else
			charset = CurSrv->m_charset.c_str();
			DCindex = GetCharset(charset);
#endif
			ucontent.RecodeContent(content, DCindex, wordCache.local_charset);
			if (charset && charset[0]) ucontent.SetOriginalCharset(charset);
		}
		if (Method != HEAD)
		{
			MD5_DIGEST(content, realsize, digest);
			changed = strcmp(digest,doc.m_crc.c_str());
			if (CurSrv->m_use_clones)
			{
//				CLocker lock(&wordCache);
				{
					CTimerAdd timer(wordCache.m_origin);
					origin = wordCache.m_database->FindOrigin(digest, doc.m_urlID, doc.m_siteID);
				}
#ifdef OPTIMIZE_CLONE
				if (changed && doc.m_origin == 1 && doc.m_status)
				{
					SetNewOrigin(doc.m_crc.c_str(), &ucontent, CurSrv, &wordCache);
				}
				if (origin)
				{
					wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
					if (doc.m_origin == 1)
					{
						wordCache.m_database->UpdateOrigin(doc.m_urlID);
					}
					if (doc.m_status != 0) ucontent.ClearF(); else ucontent.Clear();
				}
				else
				{
					ucontent.m_url.m_origin = 1;
				}
#else
				if (origin)
				{
					wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
					ucontent.Clear();
				}
#endif
			}
		}
		if ((index || follow) && (!origin) && (changed || reindex))
		{
			int rsz = ParseHtml(&ucontent, this, CurSrv, &doc, content, index, follow, text, keywords, descript, title);
			// Get Last-Modified META, overriding
			// the one from HTTP headers
			if (ucontent.m_lastmod.size() > 0)
				STRNCPY(last_modified, ucontent.m_lastmod.c_str());
			if (rsz > realsize)
			{
				fprintf(stderr, "Error in HTML parser, URL: %s, real length: %i, processed: %i\n", m_url, realsize, rsz);
			}
		}
	}
	else
	{
		/* Unknown Content-Type */
		index = 0; // What this line is for?
		if (Method != HEAD)
		{
			MD5_DIGEST(content, realsize, digest);
			changed = strcmp(digest, doc.m_crc.c_str());
			if (CurSrv->m_use_clones)
			{
//				CLocker lock(&wordCache);
				{
					CTimerAdd timer(wordCache.m_origin);
					origin = wordCache.m_database->FindOrigin(digest, doc.m_urlID, doc.m_siteID);
				}
				if (origin)
				{
					wordCache.DeleteWordsFromURL(doc.m_urlID, doc.m_siteID, CurSrv);
					ucontent.Clear();
				}
			}
		}
	}
	if ((!changed) && (!reindex))
	{
		ucontent.UpdateUrl(status, CurSrv->m_period);
		ucontent.Clear();
		ucontent.m_url.m_changed &= ~8;
	}
	else
	{
		if (subj[0])
		{
			char str[STRSIZ];
			strcpy(str,subj);
			WORD pos = 0xC000;
			ucontent.ParseText(str, pos, CurSrv);
		}

		char lang[3]="";
		const char *p;
		if ((p = ucontent.GetLang()))
			strncpy(lang, p, 3);

		char *s;

		if (!strcmp(m_schema,"news")) strcpy(keywords, from);

		s = text;
			text[MAXTEXTSIZE - 1] = 0;

			s = keywords;
			while (*s)
			{
				if (strchr("\t\n\r", *s)) *s=' ';
				s++;
			}
			keywords[MAXKEYWORDSIZE - 1] = 0;

			s = descript;
			while (*s)
			{
				if (strchr("\t\n\r", *s)) *s=' ';
				s++;
			}
			descript[MAXDESCSIZE - 1] = 0;

			s = title;
			while (*s)
			{
				if (strchr("\t\n\r", *s)) *s=' ';
				s++;
			}
			title[MAXTITLESIZE - 1] = 0;

			ucontent.UpdateLongUrl(status, CurSrv->m_period,
				changed, size, CurSrv->m_hint, last_modified,
				etag, text, title, real_content_type, charset,
				keywords, descript, digest, lang);
	}
	return size;
}

void CUrl::SetNewOrigin(const char *crc, CUrlContent *ucont, CServer *srv,
	CWordCache *wordCache)
{
#ifdef OPTIMIZE_CLONE
	char text[2 * MAXTEXTSIZE] = "";
	CDocument *docnew;
	CSrcDocVector vdocs;
	wordCache->m_database->GetCloneList(crc, ucont->m_url.m_urlID,
		vdocs, ucont->m_url.m_siteID);
	if (vdocs.size())
	{
		CSrcDocument* d = vdocs[0];
		BYTE* uncompr = NULL;
		if ((docnew = wordCache->m_database->GetDocument(d->m_urlID)))
		{
			CUrlContent ucontent;
			CServer *CurSrv = FindServer();
			ucontent.m_cache = wordCache;
			ucontent.m_url.m_urlID = docnew->m_urlID;
			ucontent.m_url.m_siteID = docnew->m_siteID;
			ucontent.m_url.m_oldRedir = docnew->m_redir;
			ucontent.m_follow = CurSrv->m_gfollow;
			ucontent.m_curSrv = CurSrv;
			ucontent.m_url.m_origin = 1;

			ucontent.m_url.m_crc = ucont->m_url.m_crc;
			//ucontent.m_url.m_charset = ucont->m_url.m_charset;
			ucontent.m_url.m_changed = 4;

			BYTE* oldc = NULL;
			ULONG oldwCount;
			ULONG oldLen;
			ULONG* oldHrefs = NULL;
			ULONG oldHsz;
			unsigned long int uLen;
			string charset;
			char content_type[50];

			CWordContent wold;
			{
//				CTimer1 timer(sqlRead);
				oldLen = wordCache->m_database->GetContents(
					ucont->m_url.m_urlID, (BYTE**)&oldc,
					oldwCount, &oldHrefs, &oldHsz,
					content_type, charset);
				oldHsz >>= 2;
			}
			uncompr = Uncompress(oldc, oldLen, uLen);
			ucontent.m_url.m_content = (char*) uncompr;
			ucontent.m_url.m_size = uLen;
			ucontent.m_url.m_content_type = content_type;
			ucontent.m_url.m_charset = charset;

			ucontent.m_url.m_docsize = uLen;

			if (!STRNCASECMP(content_type, "text/html"))
			{
				char title[MAXTITLESIZE] = "";
				char keywords[MAXKEYWORDSIZE] = "";
				char descript[MAXDESCSIZE] = "";

				ParseHtml(&ucontent, this, CurSrv, docnew,
					(char*) uncompr, CurSrv->m_gindex,
					CurSrv->m_gfollow, text, keywords,
					descript, title);
				ucontent.m_url.m_txt = text;
				ucontent.m_url.m_keywords = keywords;
				ucontent.m_url.m_title = title;
				ucontent.m_url.m_description = descript;
			}
			else if (!STRNCASECMP(content_type, "text/plain") ||
				!STRNCASECMP(content_type,
					"text/tab-separated-values") ||
				!STRNCASECMP(content_type, "text/css"))
			{
//				char *content = new char[uLen + 1];
//				memcpy(content, (char*)uncompr, uLen);
//				text[uLen] = 0;
				ULONG ult = uLen > (MAXTEXTSIZE - 1) ?
					MAXTEXTSIZE - 1 : uLen;
				WORD pos = 1;
				ucontent.ParseText((char*)uncompr, pos, CurSrv);
				strncpy(text, (char*)uncompr, ult);
				text[ult] = 0;
				ucontent.m_url.m_txt = text;
//				delete content;
			}

			delete docnew;
			if (oldc) delete oldc;
			if (oldHrefs) delete oldHrefs;
		}
		if (uncompr) delete uncompr;
	}
#endif
}

static void rfc2396_escape_part(const char *part, string& escaped, BYTE flag)
{
	char *copy = (char *) alloca(3 * strlen(part) + 1);
	const BYTE *s = (const BYTE *) part;
	BYTE *d = (BYTE *) copy;
	unsigned c;

	while ((c = *s))
	{
		if (RFC2396_ESCAPE(c, flag))
		{
			// Existing escapes are a special case
			if (c == (BYTE) '%')
			{
				// We don't escape valid sequences
				if (isxdigit(s[1]) && isxdigit(s[2]))
				{
					*d++ = c;
					*d++ = *++s;
					*d++ = *++s;
				}
				else
				{
					d = char2hex(c, d);
				}
			}
			// Spaces can be escaped with '+' only in the query portion
			else if ((flag == RFC2396_ESCAPE_QUERY) && (c == (BYTE) ' '))
			{
				*d++ = (BYTE) '+';
			}
			else
			{
				d = char2hex(c, d);
			}
		}
		else
		{
			*d++ = c;
		}
		++s;
	}
	*d = '\0';
	escaped = copy;
}

int CUrl::HTTPGetUrl(CWordCache& wordCache, CServer* CurSrv, char* buf,
	int maxsize, int Method, char* extra)
{
	int fd;
	char *q, request[STRSIZ]="";
	string path, filename, query;

	// Make sure our request URI is escaped; path, filename and query
	// are the escaped version of m_path, m_filename and m_query.
	// We'll use these when constructing the request.

	rfc2396_escape_part(m_path, path, RFC2396_ESCAPE_PATH);

	if ((q = strchr(m_filename, '?')))
	{
		char *p = q++;
		int flen = p - m_filename;
		int qlen = strlen(q);
		if (flen > 0)
		{
			char *fstr = new char[flen + 1];
			memcpy(fstr, m_filename, flen); fstr[flen] = 0;
			rfc2396_escape_part(fstr, filename, RFC2396_ESCAPE_PATH);
			delete fstr;
		}
		else
		{
			filename = "";
		}
		if (qlen > 0)
		{
			char *qstr = new char[qlen + 1];
			memcpy(qstr, q, qlen); qstr[qlen] = 0;
			rfc2396_escape_part(qstr, query, RFC2396_ESCAPE_QUERY);
			delete qstr;
		}
		else
		{
			query = "";
		}
	}
	else
	{
		rfc2396_escape_part(m_filename, filename, RFC2396_ESCAPE_PATH);
		query = "";
	}

	if (CurSrv->m_proxy.size() > 0)
	{
		fd = OpenHost(wordCache, CurSrv->m_proxy.c_str(),
			CurSrv->m_proxy_port, CurSrv->m_read_timeout);
		if (fd < 0) return fd;
		char port_str[7] = "";
		if (m_port)
		{
			snprintf(port_str, 6, ":%i" , m_port);
		}
		sprintf(request,"%s %s://%s%s%s%s%s%s HTTP/1.0\r\n%s",
			Method == HEAD ? "HEAD" : "GET", m_schema, m_hostname, port_str,
			path.c_str(), filename.c_str(), query.size() ? "?" : "",
			query.size() ? query.c_str() : "", extra);
		if (m_port)
		{
			sprintf(request + strlen(request), ":%i", m_port);
		}
	}
	else
	{
		int def_port = 80;
#ifdef USE_HTTPS
		// Default port for https is 443
		if (!strcmp(m_schema, "https"))
			def_port = 443;
#endif
		fd = OpenHost(wordCache, m_hostname, m_port ?
			m_port : def_port, CurSrv->m_read_timeout);
		if (fd < 0) return fd;
		sprintf(request,"%s %s%s%s%s HTTP/1.0\r\n%s",
			Method == HEAD ? "HEAD" : "GET", path.c_str(), filename.c_str(),
			query.size() ? "?" : "", query.size() ? query.c_str() : "", extra);
	}
	wordCache.IncConnections(fd);
	if ((!strcmp(m_schema, "http")) || (!strcmp(m_schema, "https")) ||
		(!strcmp(m_schema,"ftp")))
	{
		sprintf(request + strlen(request), "Host: %s\r\n", m_hostname);
	}
	strcat(request,"\r\n");

	gettimeofday(&m_time, NULL);
	int size;
#ifdef USE_HTTPS
	if (!strcmp(m_schema, "https"))
		size = HTTPSGet(request, buf, fd, maxsize,
			CurSrv->m_read_timeout, &wordCache.m_byteCounter);
	else
#endif
		size = HTTPGet(request, buf, fd, maxsize,
			CurSrv->m_read_timeout, &wordCache.m_byteCounter);

	if (size >= 0)
	{
		buf[size] = 0;
		buf[size + 1] = 0;
		buf[size + 2] = 0;
	}
	close(fd);
	gettimeofday(&m_etime, NULL);
	wordCache.DecConnections();
	return size;
}

int CUrl::HTTPGet(char* header, char* buf, int fd, int maxsize,
	int read_timeout, CByteCounter* byteCounter)
{
	fd_set sfds;
	int status, nread=0;

	struct timeval tv, time, etime;

	/* Send HTTP request */
	if (Send(fd, header, strlen(header), 0) < 0)
		return -1;

	/* Retrieve response */
	tv.tv_sec = (long)read_timeout;
	tv.tv_usec = 0;

	double sel = 0, readt = 0;
	while (tv.tv_sec >= 0)
	{
		FD_ZERO( &sfds );
		FD_SET( fd, &sfds );
		double sel1;
		struct timeval tv1 = tv;
		gettimeofday(&time, NULL);
		status = select(FD_SETSIZE, &sfds, NULL, NULL, &tv);
		gettimeofday(&etime, NULL);
		sel += (sel1 = etime.tv_sec - time.tv_sec + (etime.tv_usec - time.tv_usec) / 1000000.0);
		if (sel1 > read_timeout + 10)
		{
			printf("Actual select timeout: %7.3f. status: %i\n", sel1, status);
		}
		tv.tv_sec = tv1.tv_sec - (etime.tv_sec - time.tv_sec);
		tv.tv_usec = tv1.tv_usec - (etime.tv_usec - time.tv_usec);
		if (tv.tv_usec < 0)
		{
			tv.tv_usec += 1000000;
			tv.tv_sec--;
		}
		else if (tv.tv_usec >= 1000000)
		{
			tv.tv_usec -= 1000000;
			tv.tv_sec++;
		}
		if (status == -1)
		{
			nread = NET_ERROR;
			break;
		}
		else if (status == 0)
		{
			nread = NET_TIMEOUT;
			break;
		}
		else
		{
			if (FD_ISSET(fd,&sfds))
			{
				byteCounter->WaitBW();
				gettimeofday(&time, NULL);
				status = recv(fd, buf + nread, NET_BUF_SIZE, 0);
				gettimeofday(&etime, NULL);
				readt += etime.tv_sec - time.tv_sec + (etime.tv_usec - time.tv_usec) / 1000000.0;
				if (status < 0)
				{
					nread = status;
					break;
				}
				else if (status == 0)
				{
					nread += status;
					break;
				}
				else
				{
					nread += status;
					byteCounter->AddBytes(status);
					if ((nread + NET_BUF_SIZE) >= maxsize)
						break;
				}
			}
			else
				break;
		}
	}
	return nread;
}

#ifdef USE_HTTPS

#define sslcleanup close(fd); SSL_free(ssl); SSL_CTX_free(ctx)

int CUrl::HTTPSGet(char* header, char* buf, int fd, int maxsize, int read_timeout, CByteCounter* byteCounter)
{
	fd_set sfds;
	int status, nread=0;
	SSL_CTX* ctx;
	SSL* ssl;
	SSL_METHOD *meth;

	struct timeval tv, time, etime;

	SSLeay_add_ssl_algorithms();
	meth = SSLv23_client_method(); //  SSLv3 but can rollback to v2
	SSL_load_error_strings();
	ctx = SSL_CTX_new(meth);

	if ((ctx = SSL_CTX_new(meth)) == NULL){
		sslcleanup;
		return -1;
	}
	// Now we have TCP connection. Start SSL negotiation.
	if ((ssl = SSL_new(ctx)) == NULL){
		sslcleanup;
		return -1;
	}

	SSL_set_fd (ssl, fd);

	if (SSL_connect(ssl) == -1){
		sslcleanup;
		return -1;
	}

	// Send HTTP request
	if ((SSL_write(ssl, header,strlen(header))) == -1){
		sslcleanup;
		return -1;
	}

	// Retrieve response
	tv.tv_sec = (long)read_timeout;
	tv.tv_usec = 0;

	double sel = 0, readt = 0;
	while (tv.tv_sec >= 0)
	{
		FD_ZERO( &sfds );
		FD_SET( fd, &sfds );
		double sel1;
		struct timeval tv1 = tv;
		gettimeofday(&time, NULL);
		status = select(FD_SETSIZE, &sfds, NULL, NULL, &tv);
		gettimeofday(&etime, NULL);
		sel += (sel1 = etime.tv_sec - time.tv_sec + (etime.tv_usec - time.tv_usec) / 1000000.0);
		if (sel1 > read_timeout + 10)
		{
			printf("Actual select timeout: %7.3f. status: %i\n", sel1, status);
		}
		tv.tv_sec = tv1.tv_sec - (etime.tv_sec - time.tv_sec);
		tv.tv_usec = tv1.tv_usec - (etime.tv_usec - time.tv_usec);
		if (tv.tv_usec < 0)
		{
			tv.tv_usec += 1000000;
			tv.tv_sec--;
		}
		else if (tv.tv_usec >= 1000000)
		{
			tv.tv_usec -= 1000000;
			tv.tv_sec++;
		}
		if (status == -1)
		{
			nread = NET_ERROR;
			break;
		}
		else if (status == 0)
		{
			nread = NET_TIMEOUT;
			break;
		}
		else
		{
			if (FD_ISSET(fd, &sfds))
			{
				byteCounter->WaitBW();
				gettimeofday(&time, NULL);
				status = SSL_read(ssl, buf + nread, NET_BUF_SIZE);
				gettimeofday(&etime, NULL);
				readt += etime.tv_sec - time.tv_sec + (etime.tv_usec - time.tv_usec) / 1000000.0;
				if (status < 0)
				{
					nread = status;
					break;
				}
				else if (status == 0)
				{
					nread += status;
					break;
				}
				else
				{
					nread += status;
					byteCounter->AddBytes(status);
					if ((nread + NET_BUF_SIZE) >= maxsize)
						break;
				}
			}
			else
				break;
		}
	}
	return nread;
}

#endif /* USE_HTTPS */


/* HTML parser states */
#define HTML_UNK	0
#define HTML_TAG	1
#define HTML_TXT	2
#define HTML_COM	3

void ParseText1(CParsedContent* ucontent, CServer* CurSrv, WORD* position, int fontsize, int inbody,
				int inscript, int intitle, int index, int& state,
				char* e, char*& s, char* title, char*& ptitle, char* text, char*& ptext)
{
	int len = e ? e - s : strlen(s);
	int len6 = 2 * len + 6;
	char* tmp = Alloca(len6);
#ifdef UNICODE
	WORD* tmpu = new WORD[len6];
	WORD* eu = tmpu;
	*eu = 0;
#endif
	int j = 0;
	e = tmp; *e = 0;
	WORD sch;
#ifdef UNICODE
	const BYTE* ps = (const BYTE*)s;
	const BYTE* pe = ps + len;
	WORD code;
	CCharsetB* charset = ucontent->m_ucharset;
	while ((ps < pe) && (code = charset->NextChar(ps)))
	{
		switch (code)
#else
	char* ps;
	char* pe = s + len;
	for (ps = s; ps < pe; ps++)
	{
		switch (*ps)
#endif
		{
		case ' ' : case '\t':
		case '\n': case '\r':
			j++; break;

		default:
			if (j)
			{
				*e = ' '; e++; j = 0;
			}
#ifdef UNICODE
			charset->PutChar((BYTE*&)e, (code == '\'') ? '`' : code);
#else
			*e = (*ps == '\'') ? '`' : *ps; e++;
#endif
			*e = 0;
			break;
		}
	}
	int tlen;
	int l = e - tmp;
	if (index && (inbody && !inscript) && (((tlen = ptext - text) + 1) < MAXTEXTSIZE))
	{
		if (*text)
		{
			tlen++;
			*ptext++ = ' ';
			*ptext = 0;
		}
		if (l > MAXTEXTSIZE - 1 - tlen) l = MAXTEXTSIZE - 1 - tlen;
		memcpy(ptext, tmp, l);
		ptext += l;
		*ptext = 0;
	}
	if (intitle && (((tlen = ptitle - title) + 1) < MAXTITLESIZE))
	{
		if (*title)
		{
			*ptitle++ = ' ';
			*ptitle = 0;
			tlen++;
		}
		if (l > MAXTITLESIZE - 1 - tlen) l = MAXTITLESIZE - 1 - tlen;
		memcpy(ptitle, tmp, l);
		ptitle += l;
		*ptitle = 0;
	}
	if (index && !inscript)
	{

#ifdef UNICODE
	BYTE* etmp = (BYTE*)e;
	e = tmp;
	j = 0;
	const BYTE* ps = (const BYTE*)tmp;
	while ((ps < etmp) && (code = charset->NextChar(ps)))
	{
		switch (code)
#else
	char* etmp = e;
	e = tmp;
	j = 0;
	for (ps = tmp; ps < etmp; ps++)
	{
		switch (*ps)
#endif
		{
		case '&': /* parse specials */
			if (j)
			{
#ifdef UNICODE
				*eu++ = ' ';
#else
				*e++ = ' ';
#endif
				j = 0;
			}
#ifndef UNICODE
			ps++;
#endif
			if ((sch = SgmlToChar((const char*&)ps, ucontent->m_charset)))
			{
#ifdef UNICODE
				*eu++ = TolowerU(sch);
#else
				*e++ = sch;
#endif
				if (sch == ' ') j++;
			}
			else
			{
#ifdef UNICODE
				*eu++ = '?';
#else
				*e++ = '?';
#endif
			}
#ifndef UNICODE
			ps--;
#endif
			break;
		default:
			if (j)
			{
#ifdef UNICODE
				*eu++ = ' ';
#else
				*e++ = ' ';
#endif
				j = 0;
			}
#ifdef UNICODE
//			*eu = ucontent->m_ucharset->UCodeLower((*ps == '\'') ? '`' : *ps);
			*eu = charset->UCodeL((code == '\'') ? '`' : code);
			if (*eu == 0) *eu = ' ';
			eu++;
#else
			*e++ = (*ps == '\'') ? '`' : *ps;
#endif
			break;
		}
	}
#ifdef UNICODE
	*eu = 0;
#else
	*e = 0;
#endif

#ifdef UNICODE
		ucontent->ParseText(tmpu, position[intitle ? 3 : 0], CurSrv, fontsize);
#else
		ucontent->ParseText(tmp, position[intitle ? 3 : 0], CurSrv, fontsize);
#endif
	}
	s += len;
	skip_junk(s);
	state = HTML_UNK;
	Freea(tmp, len6);
#ifdef UNICODE
	delete tmpu;
#endif
}

#define STRSIZ	1024*5
#define DEFAULTFONTSIZE	3
#define MAXFONTSIZE	7

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
	char *title)
{
	ucontent->SetParsed();
	ucontent->ParseHead(content);
	int len, inbody, inscript, intitle, instyle, state, Method;
	int noindex = 0;
	char *e = 0, *href = 0, *newschema = 0, *lt;
	char str[STRSIZ] = "";
	char str1[STRSIZ] = "";
	char* ptext = text;
	char* ptitle = title;
	int base_font = DEFAULTFONTSIZE;
	int fontsize = DEFAULTFONTSIZE;
	inbody = inscript = intitle = instyle = 0;
	state = HTML_UNK;
	string curhost;
	string cursch;
	if (curURL)
	{
		curhost = curURL->m_hostinfo;
		cursch = curURL->m_schema;
	}
	WORD position[4] = {0, 0x4000, 0x8000, 0xC000};
	WORD code;
	char* s = content;
#ifdef UNICODE
	const BYTE* s1 = (const BYTE*)content;
	const BYTE* e1;
	CCharsetB* charset = ucontent->m_ucharset;
	while ((s = (char*)s1, code = charset->NextChar(s1)))
	{
#else
	while (*s)
	{
		code = *(BYTE*)s;
#endif
		CUrl newURL;
		CTag tag;
		switch(state)
		{
		case HTML_UNK: /* yet unknown */
#ifdef UNICODE
			if (!USTRNCMP(charset, s, "<!--"))
#else
			if (!STRNCMP(s, "<!--"))
#endif
			{
				state = HTML_COM;
			}
			else
			{
				state = code == '<' ? HTML_TAG : HTML_TXT;
			}
#ifdef UNICODE
			s1 = (BYTE*)s;
#endif
			break;

		case HTML_TAG: /* tag */
#ifdef UNICODE
			ParseTag(charset, tag, len, s, e);
#else
			ParseTag(tag, len, s, e);
#endif
			if (!strcasecmp(tag.m_name, "body")) inbody = 1;
			else if (!strcasecmp(tag.m_name, "/body")) inbody = 0;
			else if (!strcasecmp(tag.m_name, "script")) inscript = 1;
			else if (!strcasecmp(tag.m_name, "/script")) inscript = 0;
			else if (!strcasecmp(tag.m_name, "title")) intitle = 1;
			else if (!strcasecmp(tag.m_name, "/title")) intitle = 0;
			else if (!strcasecmp(tag.m_name, "style")) instyle = 1;
			else if (!strcasecmp(tag.m_name, "/style")) instyle = 0;
			else if ((!strcasecmp(tag.m_name, "a")) && (tag.m_href)) href = strdup(tag.m_href);
			else if ((!strcasecmp(tag.m_name, "area")) && (tag.m_href))	href = strdup(tag.m_href);
			else if ((!strcasecmp(tag.m_name, "link")) && (tag.m_href))	href = strdup(tag.m_href);
			else if ((!strcasecmp(tag.m_name, "frame")) && (tag.m_src))	href = strdup(tag.m_src);
			else if ((!strcasecmp(tag.m_name, "img")) && (tag.m_src)) href = strdup(tag.m_src);
			else if (!strcasecmp(tag.m_name, "h1")) fontsize = 6;
			else if (!strcasecmp(tag.m_name, "/h1")) fontsize = base_font;
			else if (!strcasecmp(tag.m_name, "h2")) fontsize = 5;
			else if (!strcasecmp(tag.m_name, "/h2")) fontsize = base_font;
			else if (!strcasecmp(tag.m_name, "h3")) fontsize = 4;
			else if (!strcasecmp(tag.m_name, "/h3")) fontsize = base_font;
			else if (!strcasecmp(tag.m_name, "h4")) fontsize = 3;
			else if (!strcasecmp(tag.m_name, "/h4")) fontsize = base_font;
			else if (!strcasecmp(tag.m_name, "h5")) fontsize = 2;
			else if (!strcasecmp(tag.m_name, "/h5")) fontsize = base_font;
			else if (!strcasecmp(tag.m_name, "h6")) fontsize = 1;
			else if (!strcasecmp(tag.m_name, "/h6")) fontsize = base_font;
			else if (!strcasecmp(tag.m_name, "font") && tag.m_size)
			{
				int sz = atoi(tag.m_size);

				if (tag.m_size[0] == '+' || tag.m_size[0] == '-')
				{
					fontsize = (sz += base_font) > MAXFONTSIZE ? MAXFONTSIZE : sz;
				}
				else
					fontsize = sz > MAXFONTSIZE ? MAXFONTSIZE : sz;

				if (fontsize < 0)
					fontsize = 1;

			}
			else if (!strcasecmp(tag.m_name, "/font")) fontsize = base_font;
			else if (!strcasecmp(tag.m_name, "basefont"))
			{
				if (tag.m_size)
				{
					int sz;
					base_font = (sz = atoi(tag.m_size)) > MAXFONTSIZE ? MAXFONTSIZE : sz < 0 ? 1 : sz;
				}
			}
			else if (!strcasecmp(tag.m_name, "big")) fontsize = base_font+1;
			else if (!strcasecmp(tag.m_name, "/big")) fontsize = base_font;
			else if (!strcasecmp(tag.m_name, "small")) fontsize = base_font-1;
			else if (!strcasecmp(tag.m_name, "/small")) fontsize = base_font;
			else if ((!strcasecmp(tag.m_name, "base")) && (tag.m_href))
			{
				/* <BASE HREF="xxx"> stuff
				 * Check that URL is properly formed.
				 * baseURL is just temporary variable.
				 * If parsing fails we will use old
				 * base href, passed via curURL
				 */
				if (curURL)
				{
					CUrl baseURL;
					if(!baseURL.ParseURL(tag.m_href))
					{
						char* oldSchema = curURL->m_schema;
						curURL->m_schema = 0;
						curURL->Free();
						curURL->ParseURL(tag.m_href, 1);
						if (curURL->m_schema[0] == 0)
						{
							delete curURL->m_schema;
							curURL->m_schema = oldSchema;
						}
						else
						{
							delete oldSchema;
						}
					}
				}
			}
			else if ((!strcasecmp(tag.m_name, "meta")) && tag.m_equiv && tag.m_content)
			{
				if (!strcasecmp(tag.m_equiv, "refresh"))
				{
					char *u;
					if ((u = strcasestr(tag.m_content, "url")))
					{
						if ((u = str_ltrim(u + 3)))
						{
							if (*u == '=')
							{
								href = strdup(u + 1);
							}
						}
					}
				}
				else if (!strcasecmp(tag.m_equiv, "keywords"))
				{
					strncpy(keywords, tag.m_content, MAXKEYWORDSIZE - 1);
					keywords[MAXKEYWORDSIZE - 1] = 0;
					if (index)
					{
						strcpy(str, keywords);
						ucontent->ParseText(str, position[2], CurSrv);
					}
				}
				else if (!strcasecmp(tag.m_equiv, "description"))
				{
					strncpy(descript, tag.m_content,MAXDESCSIZE-1);
					descript[MAXDESCSIZE-1]=0;
					if (index)
					{
						strcpy(str, descript);
						ucontent->ParseText(str, position[1], CurSrv);
					}
				}
				else if (!strcasecmp(tag.m_equiv, "robots"))
				{
					if ((CurSrv == NULL) || CurSrv->m_userobots)
					{
						lt = tag.m_content;
						while (true)
						{
							CGetWord gw;
							e = gw.GetWord((unsigned char**)&lt, ucontent->m_cache->local_charset);
							if (e == NULL) break;
							if (!strcasecmp(e, "ALL"))
							{
								follow = 1; index = 1;
							}
							else if (!strcasecmp(e, "NONE"))
							{
								ucontent->ClearW();
								follow = 0; index = 0;
							}
							else if (!strcasecmp(e, "NOINDEX"))
							{
								ucontent->ClearW();
								index=0;
							}
							else if (!strcasecmp(e, "NOFOLLOW"))
							{
								follow=0;
							}
							else if (!strcasecmp(e, "INDEX"))
							{
								index=1;
							}
							else if (!strcasecmp(e, "FOLLOW"))
							{
								follow=1;
							}
						}
					}
				}
			}
			if (href && follow && doc && CurSrv->m_gfollow)
			{
				string href_unescaped;
				char *href_trim = str_trim(href);
				URIUnescapeSGML(href_trim, href_unescaped, ucontent->m_charset);
				if (doc->m_hops >= CurSrv->m_maxhops)
				{
				}
				else if (!newURL.ParseURL(href_unescaped.c_str()))
				{
					char srv[STRSIZ];
					char* host;
					char* path;
					char reason[STRSIZ] = "";
					if (newURL.m_schema[0])
						newschema = newURL.m_schema;
					else
						newschema = curURL->m_schema;
					if (!strcmp(newschema, "file") || !strcmp(newschema, "htdb"))
					{
						host = NULL;
						path = newURL.m_path[0] ? newURL.m_path : curURL->m_path;
						sprintf(str, "%s:%s%s", newschema, path, newURL.m_filename);
						sprintf(srv, "%s:%s/", newschema, path);
					}
					else
					{
						host = newURL.m_hostinfo[0] ? newURL.m_hostinfo : curURL->m_hostinfo;
						path = newURL.m_path[0] ? newURL.m_path : curURL->m_path;
						sprintf(str, "%s://%s%s%s", newschema, host, path, newURL.m_filename);
						sprintf(srv, "%s://%s/", newschema, host);
					}
					Remove2Dot(str);
					if(!STRNCMP(str, "ftp://") && (strstr(str,";type=")))
						*(strstr(str, ";type"))=0;

					Method = FilterType(str, reason, ucontent->m_cache);

					if ((Method != DISALLOW))
					{
						if (host && (FindRobots(host, path, newURL.m_filename) >= 0) && CurSrv->m_userobots)
						{
						}
						else
						{
							int add=1;
							/* compare hostinfo in some cases */
							if (((!CurSrv->m_delete_no_server) && (!CurSrv->m_outside)) && (Sites.find(srv) == Sites.end()))
							{
								add = !strcmp(cursch.c_str(), newschema);
								if (add && host)
								{
									add = !strcmp(curhost.c_str(), host);
								}
							}
							if (add)
							{
								CLocker lock(ucontent->m_cache);

								/* Add URL itself */
								ULONG hrID = ucontent->m_cache->GetHref(str, CurSrv, doc->m_urlID, doc->m_hops + 1, srv);
								if ((hrID != 0) && (hrID != doc->m_urlID))
								{
									ucontent->InsertHref(hrID);
								}

								/* Add robots.txt for HTTP schema */
								/* When FollowOutside or DeleteNoServer no */
								if ((!strcmp(newschema,"http")) && CurSrv->m_userobots &&
									(CurSrv->m_outside || (!CurSrv->m_delete_no_server)))
								{
									sprintf(str1, "%s://%s/%s",newschema, host, "robots.txt");
									ucontent->m_cache->GetHref(str1, NULL, 0, 0, srv, 86400);
								}
							}
						}
					}
				}
			}
			if (href)
			{
				free(href);
				href = NULL;
			}
#ifdef UNICODE
			s1 = (BYTE*)s + len;
#else
			s += len;
#endif
			state = HTML_UNK;
			break;

		case HTML_COM: /* comment */
			// check for ending noindex tags
			if (noindex) // check for ending noindex tags
			{
				if (!STRNCASECMP(s, "<!--/noindex-->"))
					noindex &= ~1;
				else if (!STRNCASECMP(s, "<!--/htdig_noindex-->"))
					noindex &= ~2;
				else if (!STRNCASECMP(s, "<!--/UdmComment-->"))
					noindex &= ~4;
			}
			// check for starting noindex tags
			if (!STRNCASECMP(s, "<!--noindex-->"))
				noindex |= 1;
			else if (!STRNCASECMP(s, "<!--htdig_noindex-->"))
				noindex |= 2;
			else if (!STRNCASECMP(s, "<!--UdmComment-->"))
				noindex |= 4;

#ifdef UNICODE
			s1 = UStrStrE(charset, s1, (const BYTE*)"-->");
#else
			e = strstr(s, "-->");
			len = e ? e - s + 3 :strlen(s);

			/* This is for debug pusposes
			tmp=(char*)malloc(len+1);
			strncpy(tmp,s,len);tmp[len]=0;
			printf("com: '%s'\n",tmp);
			*/

			s += len;
#endif
			state = HTML_UNK;
			break;

		case HTML_TXT: /* text */
#ifdef UNICODE
			e1 = (BYTE*)s;
			while ((e = (char*)e1, code = charset->NextChar(e1)) && code != '<');
			while (inscript && code && (STRNCASECMP(e, "</script")))
			{
				while ((e = (char*)e1, code = charset->NextChar(e1)) && (code != '<'));
			}
#else
			e = strchr(s, '<');

			/* Special case when script body is not commented: */
			/* <script> x="<"; </script> */

			/* We should find </script> in this case: */
			while (inscript && e && (STRNCASECMP(e, "</script")))
				e = strchr(e + 1, '<');
#endif
			if (noindex == 0)
			{
				ParseText1(ucontent, CurSrv, position, (fontsize > base_font) ? 1 : 0,
					inbody, inscript || instyle, intitle, index, state,
					e, s, title, ptitle, text, ptext);
			}
			else
			{
#ifndef UNICODE
				len = e ? e - s : strlen(s);
				s += len;
#endif
				state = HTML_UNK;
			}
#ifdef UNICODE
			s1 = (BYTE*)e;
#endif
			break;
		}
	}
	return s - content;
}

CServer* CUrl::FindServer()
{
	char serv[1000];
	sprintf(serv, "%s://%s/", m_schema, m_hostname);
	hash_map<string, CMapDirToConf>::iterator it = ServersD.find(serv);
	if (it != ServersD.end())
	{
		CServer* server = it->second.GetServer(m_path + 1);
		if (server) return server;
	}
	it = ServersD.find("");
	if (it != ServersD.end())
	{
		return it->second.m_server;
	}
	return NULL;
}

void CUrl::Save(char *buf, int size)
{
	char dir[1000];
	sprintf(dir, "%s/%s", DBName.c_str(), m_hostname);
	safe_mkdir(dir, 0774);
	char* path = m_path;
	char* pd = dir + strlen(dir);
	do
	{
		char* np = strchr(path, '/');
		if ((np == NULL) && (path[0] == 0)) break;
		memcpy(pd, path, np ? np - path : strlen(path));
		pd += strlen(pd);
		*pd = 0;
		struct stat st;
		if ((stat(dir, &st) != 0) && (errno == ENOENT))
		{
			if (mkdir(dir, 0774))
			{
				return;
			}
		}
		else
		{
			if (!S_ISDIR(st.st_mode))
			{
				unlink(dir);
				if (mkdir(dir, 0774))
				{
					return;
				}
			}
		}
		*pd++ = '/';
		*pd = 0;
		path = np;
		if (path) path++;
	}
	while (path);
	strcpy(pd, m_filename[0] ? m_filename : "index");
	FILE* f = fopen(dir, "w");
	if (f)
	{
		fwrite(buf, size, 1, f);
		fclose(f);
	}
}

int CUrl::GetFromFile(char *buf, int maxsize)
{
	char dir[1000];
	sprintf(dir, "%s/%s/%s/%s", DBName.c_str(), m_hostname, m_path, m_filename[0] ? m_filename : "index");
	FILE* f = fopen(dir, "r");
	if (f)
	{
		printf("Reading from file: %s\n", m_url);
		int size = fread(buf, 1, maxsize, f);
		fclose(f);
		return size;
	}
	else
	{
		return -1;
	}
}


CServer* CMapDirToConf::GetServer(const char* dir)
{
	const char* sl = strchr(dir, '/');
	int len = sl ? sl - dir : strlen(dir);
	sl = sl ? sl + 1 : dir + len;
	char* dir1 = (char*)alloca(len + 1);
	memcpy(dir1, dir, len);
	dir1[len] = 0;
	iterator it = find(dir1);
	if (it == end())
	{
		return m_server;
	}
	if (it->second == NULL) return NULL;
	CServer* server = it->second->GetServer(sl);
	if (server) return server;
	return m_server;
}

void CMapDirToConf::AddServer(const char* dir, CServer& server)
{
	const char* sl = strchr(dir, '/');
	int len = sl ? sl - dir : strlen(dir);
	sl = sl ? sl + 1 : dir + len;
	char* dir1 = (char*)alloca(len + 1);
	memcpy(dir1, dir, len);
	dir1[len] = 0;
	if (*dir1)
	{
		CMapDirToConf*& map = (*this)[dir1];
		if (map == NULL) map = new CMapDirToConf;
		map->AddServer(sl, server);
	}
	else
	{
		if (m_server) delete m_server;
		m_server = new CServer;
		*m_server = server;
	}
}

void AddServer(CServer& server)
{
	CUrl url;
	if (server.m_url.size())
	{
		url.ParseURL(server.m_url.c_str());
		char serv[1000];
		sprintf(serv, "%s://%s/", url.m_schema, url.m_hostname);
		ServersD[serv].AddServer(url.m_path + 1, server);
	}
	else
	{
		ServersD[""].AddServer("", server);
	}
}

CServer* FindServer(const char *url)
{
	CUrl cururl;
	cururl.ParseURL(url);
	return cururl.FindServer();
}

void AddServer(char* url)
{
	CServer srv;
	srv.m_url = url;
	AddServer(srv);
}

void FindServerP(char* srv)
{
	CServer* server = FindServer(srv);
	printf("%s: %s\n", srv, server ? server->m_url.c_str() : "Not found");
}

void ServerTest()
{
	AddServer("");
	AddServer("http://sg/");
	AddServer("http://www/pub/");
	AddServer("http://www/pub/2");
	AddServer("http://www/doc/1/");
	AddServer("http://www/doc/3/");
	FindServerP("http://www/");
	FindServerP("http://www/1/");
	FindServerP("http://www/doc/2/");
	FindServerP("http://www/doc/1/");
	FindServerP("http://www/doc/1/4/");
	FindServerP("http://www/pub/2/3/");
	FindServerP("http://sg/doc/2/");
}
