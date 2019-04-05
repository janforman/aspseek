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

/*  $Id: config.cpp,v 1.52 2002/10/08 08:18:12 kir Exp $
    Author : Alexander F. Avdonkin
	Uses parts of UdmSearch code
*/

#include "aspseek-cfg.h"

#include <stdio.h>
#include <errno.h>
#include <string>
#include <vector>
#include "config.h"
#include "parse.h"
#include "sqldb.h"
#include "defines.h"
#include "charsets.h"
#include "index.h"
#include "filters.h"
#include "paths.h"
#include "misc.h"
#include "logger.h"
#include "stopwords.h"
#include "datetime.h"
#include "geo.h"
#ifdef UNICODE
#include "ucharset.h"
#endif
#include "mimeconv.h"
char conf_err_str[STRSIZ] = "";
static char	_user_agent[STRSIZ] = "";
static char	_extra_headers[STRSIZ] = "";
int	_max_doc_size = MAXDOCSIZE;
string MirrorRoot, MirrorHeadersRoot;
string url_file_name;
string DataDir = DATA_DIR;
string ConfDir = CONF_DIR;

vector<string> dblib_paths;

CBWSchedule bwSchedule;
CMimes Mimes;
ULONG MaxMem = 10000000;
ULONG WordCacheSize = 50000;
ULONG HrefCacheSize = 10000;
int IncrementalCitations = 1;

#define BASE64_LEN(len) (4 * (((len) + 2) / 3) +2)

static char base64[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode (const char *s, char *store, int length)
{
	int i;
	unsigned char *p = (unsigned char *)store;

	for (i = 0; i < length; i += 3)
	{
		*p++ = base64[s[0] >> 2];
		*p++ = base64[((s[0] & 3) << 4) + (s[1] >> 4)];
		*p++ = base64[((s[1] & 0xf) << 2) + (s[2] >> 6)];
		*p++ = base64[s[2] & 0x3f];
		s += 3;
	}
	// Pad the result
	if (i == length + 1) *(p - 1) = '=';
	else if (i == length + 2) *(p - 1) = *(p - 2) = '=';
	*p = '\0';
}

char* UserAgent()
{
	return _user_agent;
}

char* ExtraHeaders()
{
	return _extra_headers;
}
int AddType(char *mime_type, char *reg, char *errstr)
{
	CMime* m = new CMime;
	m->SetType(reg, mime_type);
	if (m->m_mime_type.size() > 0)
	{
		Mimes.push_back(m);
		return 0;
	}
	else
	{
		delete m;
		return 1;
	}
}

void CServer::AddReplacement(char* str)
{
	char find[MAX_URL_LEN + 1] = "", replace[MAX_URL_LEN + 1] = "";
	int n = sscanf(str, "%s %s", find, replace);
	switch (n)
	{
	case 0 : case -1:
		m_replace = NULL;
		break;
	case 1: case 2:
		{
			CReplacement* repl = new CReplacement;
			if (repl->SetFindReplace(find, replace))
			{
				delete repl;
			}
			else
			{
				if (m_replace == NULL) m_replace = new CReplaceVec;
				m_replace->push_back(repl);
			}
			break;
		}
	}
}

#define IF_NUM_PARAM(src, name, dst, def) if (!STRNCASECMP(src, name)) {if (sscanf(src + strlen(name), "%d", (int*)&(dst)) != 1) dst = def;}
#define IF_BOOL_PARAM(src, name, dst, def) if (!STRNCASECMP(src, name)) {char xxx[1024]; if (sscanf(src + strlen(name), "%s", xxx) == 1) {if (!STRNCASECMP(xxx, "yes")) dst = 1; else dst = 0;} else dst = def;}

#define IF_TIME_PARAM(src,name,dst,def) \
if (!STRNCASECMP(src,name)){ \
	if ((dst=tstr2time_t(src+strlen(name)))==BAD_DATE) \
		dst=def; \
}


CServer csrv;
static string localcharset;

int LoadConfig(char *conf_name, int config_level, int load_flags)
{
	int i = 0; // For line number
	FILE *config;
	char str[BUFSIZ];
	char str1[STRSIZ];
	char str2[STRSIZ];

	if (config_level == 0)
	{ /* Do some initialization */
		sprintf(_user_agent,"%s/%s", USER_AGENT, VERSION);
		_extra_headers[0] = 0;
		_max_doc_size = MAXDOCSIZE;

		DBPort = 0;
		SetDefaultCharset(CHARSET_USASCII);

	}

	string config_file_name;
	// check if the path is absolute
	if (isAbsolutePath(conf_name))
		config_file_name = conf_name;
	else
		config_file_name = ConfDir + "/" + conf_name;
	// Open config
	if (!(config = fopen(config_file_name.c_str(), "r")))
	{
		sprintf(conf_err_str, "Error: can't open config file '%s': %s",config_file_name.c_str(), strerror(errno));
		return 1;
	}
	// Read lines and parse
	while (fgets(str1, sizeof(str1), config))
	{
		char *end;
		i++;
		if (!str1[0]) continue;
		end = str1 + strlen(str1) - 1;
		while ((end >= str1) && (*end == '\r' || *end == '\n' || *end == ' ' || *end == '\t'))
		{
			*end = 0;
			if (end > str1) end--;
		}
		if (!str1[0]) continue;
		if (str1[0] == '#') continue;

		if (*end == '\\')
		{
			*end = 0;
			strcat(str, str1);
			continue;
		}
		strcat(str, str1);
		strcpy(str1, "");

                if (!STRNCASECMP(str, "DBHost"))
                {
                    if (sscanf(str+6, "%s", str1))
                    DBHost = str1;
                }
                else if (!STRNCASECMP(str, "DBWordDir"))
                {
                     DBWordDir = Trim(str + 9," \r\n\t");
                }
                else if (!STRNCASECMP(str, "DataDir"))
                {
                     DataDir = Trim(str + 7," \r\n\t");
                }
                else if (!STRNCASECMP(str, "DBName"))
                {
                     DBName = Trim(str + 6," \r\n\t");
	        }
	        else if (!STRNCASECMP(str, "DBUser"))
	        {
	            if (sscanf(str + 6, "%s", str1) && strcmp(str1, ""))
	    		DBUser = str1;
	        }
	        else if (!STRNCASECMP(str, "DBPass"))
	             {
	             if (sscanf(str + 6, "%s", str1) && strcmp(str1, ""))
	             DBPass = str1;
	             else
	             DBPass = "";
	        }
		else if (!STRNCASECMP(str, "DBAddr"))
		{
			ParseDBAddr(Trim(str + 6, "= \t\r\n"));
		}
		else if (!STRNCASECMP(str, "DBLibDir"))
		{
			dblib_paths.push_back(string(Trim(str + 9, "= \t\r\n")));
		}
		else if (!STRNCASECMP(str, "DebugLevel"))
		{
			logger.setloglevel(Trim(str + 10, "= \t\r\n"));
		}
		else if (!STRNCASECMP(str, "LocalCharset"))
		{
			if (sscanf(str + 13, "%s", str1) && strcmp(str1, ""))
			{
				localcharset = str1;
			}
		}
		else if (!STRNCASECMP(str, "DBType"))
		{
				DBType = Trim(str + 6, "= \r\n\t");
		}
		else if (!STRNCASECMP(str, "AllowCountries"))
		{
			AddCountries(str);
		}
		else if (!STRNCASECMP(str, "DisallowNoMatch"))
		{
			char *s,*lt;
			s = GetToken(str, " \t\r\n", &lt);
			while ((s = GetToken(NULL, " \t\r\n",&lt)))
			{
				if (AddFilter(s, DISALLOW, 1))
					return 1;
			}
		}
		else if (!STRNCASECMP(str,"Disallow"))
		{
			char *s,*lt;
			s = GetToken(str, " \t\r\n", &lt);
			while ((s = GetToken(NULL, " \t\r\n", &lt)))
			{
				if (AddFilter(s, DISALLOW, 0))
					return 1;
			}
		}
		else if (!STRNCASECMP(str, "AllowNoMatch"))
		{
			char *s,*lt;
			s = GetToken(str, " \t\r\n", &lt);
			while ((s = GetToken(NULL, " \t\r\n", &lt)))
			{
				if (AddFilter(s, ALLOW, 1))
					return 1;
			}
		}
		else if (!STRNCASECMP(str, "Allow"))
		{
			char *s,*lt;
			s = GetToken(str, " \t\r\n", &lt);
			while ((s = GetToken(NULL, " \t\r\n", &lt)))
			{
				if (AddFilter(s, ALLOW, 0))
					return 1;
			}
		}
		else if (!STRNCASECMP(str, "CheckOnlyNoMatch"))
		{
			char *s,*lt;
			s = GetToken(str, " \t\r\n", &lt);
			while ((s = GetToken(NULL, " \t\r\n", &lt)))
			{
				if (AddFilter(s, HEAD, 1))
					return 1;
			}
		}
		else if (!STRNCASECMP(str,"CheckOnly"))
		{
			char *s,*lt;
			s = GetToken(str, " \t\r\n", &lt);
			while ((s = GetToken(NULL, " \t\r\n", &lt)))
			{
				if (AddFilter(s, HEAD, 0))
					return 1;
			}
		}
		else if (!STRNCASECMP(str, "AddType"))
		{
			char *s, *s1, *lt;
			s = GetToken(str, " \t\r\n", &lt);
			if ((s1 = GetToken(NULL, " \t\r\n", &lt)))
			{
				while ((s = GetToken(NULL, " \t\r\n", &lt)))
					AddType(s1, s, conf_err_str);
			}
		}
		else if (!STRNCASECMP(str, "StopwordFile"))
		{
			int len = strlen(str);
			char lang[3];
			char *file = new char[len];
#ifdef UNICODE
			char* encoding = new char[len];
			int n = sscanf(str+12, "%2s %s %s", lang, file, encoding);
			if (n >= 2)
			{
				Stopwords.Load(file, lang, n == 3 ? encoding : localcharset.c_str());
			}
#else
			if(sscanf(str+12, "%2s %s", lang, file)==2)
			{
				Stopwords.Load(file, lang);
			}
#endif
			else
			{
				sprintf(conf_err_str, "Error in config file line %d: %s", i, str);
			}
			delete file;
#ifdef UNICODE
			delete encoding;
#endif
		}
		else if (!STRNCASECMP(str, "CharsetAlias"))
		{
			int len = strlen(str);
			char *name = new char[len];
			char *aliases = new char[len];
			if(sscanf(str+12, "%s%[^\n\r]s", name, aliases)==2)
			{
				AddAlias(name, aliases);
			}
			else
			{
				sprintf(conf_err_str, "Error in config file line %d: %s", i, str);
			}
			delete name;
			delete aliases;
		}
#ifdef UNICODE
		else if (!STRNCASECMP(str, "CharsetTableU1"))
		{
			int len = strlen(str);
			char *name = new char[len];
			char *dir = new char[len];
			char *lang = new char[len];
			char *lmdir = new char[len];
			int param, charset_id=0;

			param = sscanf(str + 14, "%s%s%s%s", name, lang, dir, lmdir);
			if (param >= 3)
			{
				if ((charset_id = LoadCharsetU1(lang, name, dir)) == -1)
				{
					logger.log(CAT_FILE, L_WARN, "Charset %s has not been loaded\n", name);
				}
#ifdef USE_CHARSET_GUESSER
				if ((param == 4) && (charset_id > 0))
				{
					langs.AddLang(charset_id, lmdir);
				}
#endif
			}
			if (param < 3)
			{
				sprintf(conf_err_str, "Error in config file line %d: %s", i, str);
			}
			delete name;
			delete dir;
			delete lang;
		}
		else if (!STRNCASECMP(str, "CharsetTableU2"))
		{
			int len = strlen(str);
			char *name = new char[len];
			char *dir = new char[len];
			char *lang = new char[len];
			char *lmdir = new char[len];
			int param, charset_id=0;

			param = sscanf(str + 14, "%s%s%s%s", name, lang, dir, lmdir);
			if (param >= 3)
			{
				if ((charset_id = LoadCharsetU2V(lang, name, dir)) == -1)
				{
					logger.log(CAT_FILE, L_WARN, "Charset %s has not been loaded\n", name);
				}
#ifdef USE_CHARSET_GUESSER
				if ((param == 4) && (charset_id > 0))
				{
					langs.AddLang(charset_id, lmdir);
				}
#endif
			}
			if (param <3)
			{
				sprintf(conf_err_str, "Error in config file line %d: %s", i, str);
			}
			delete name;
			delete dir;
			delete lang;
		}
		else if (!STRNCASECMP(str, "Dictionary2"))
		{
			int len = strlen(str);
			char *dir = new char[len];
			char *lang = new char[len];
			char *charset = new char[len];
			int param;
			if ((param = sscanf(str + 11, "%s%s%s", lang, dir, charset)) >= 2)
			{
				LoadDictionary2(lang, dir, param > 2 ? charset : NULL);
			}
			delete dir;
			delete lang;
		}
#endif //UNICODE
		else if (!STRNCASECMP(str, "CharsetTable"))
		{
			int len = strlen(str);
			char *name = new char[len];
			char *dir = new char[len];
			char *lang = new char[len];
			char *lmdir = new char[len];
			int param, charset_id=0;

			param = sscanf(str+12, "%s%s%s%s", name, lang, dir, lmdir);
			if (param >= 3)
			{
				if ((charset_id = LoadCharset(lang, name, dir)) == -1)
				{
					logger.log(CAT_FILE, L_WARN, "Charset %s has not been loaded\n", name);
				}
			}
#ifdef USE_CHARSET_GUESSER
			if (param == 4 && charset_id > 0)
			{
				langs.AddLang(charset_id, lmdir);
			}
#endif
			if (param < 3)
			{
				sprintf(conf_err_str, "Error in config file line %d: %s", i, str);
			}
			delete name;
			delete dir;
			delete lang;
			delete lmdir;
		}
		else if (!STRNCASECMP(str, "CharSet"))
		{
			if (sscanf(str + 7, "%s", str1))
				csrv.m_charset = str1;
		}
		else if (!STRNCASECMP(str, "Proxy"))
		{
			if (sscanf(str + 5, "%s", str1) && strcmp(str1, ""))
			{
				int port = 0;
				char proxy[STRSIZ];
				sscanf(str1, "%[^:]:%d", proxy, &port);
				csrv.m_proxy = proxy;
				csrv.m_proxy_port = port == 0 ? 3128 : port;
			}
		}
		else if (!STRNCASECMP(str, "HTTPHeader"))
		{
			if (sscanf(str + 11, "%[^\n\r]s", str1) && strcmp(str1, ""))
			{
				if (!STRNCMP(str1, "User-Agent: "))
				{
					safe_strcpy(_user_agent, str1 + 12);
				}
				else
				{
					strcat(_extra_headers, str1);
					strcat(_extra_headers, "\r\n");
				}
			}
		}
		else
		if (!STRNCASECMP(str, "AuthBasic"))
		{
			if (sscanf(str + 9, "%s", str1))
			{
				char* s = (char*)alloca(BASE64_LEN(strlen(str1)));
				base64_encode(str1, s, strlen(str1));
				csrv.m_basic_auth = s;
			}
		}
		else if (!STRNCASECMP(str, "HTDBList"))
		{
			csrv.m_htdb_list = Trim(str + 8, " \t\r\n");
		}
		else if (!STRNCASECMP(str, "HTDBDoc"))
		{
			csrv.m_htdb_doc = Trim(str + 8, " \t\r\n");
		}
#ifdef USE_EXT_CONV
		else if (!STRNCASECMP(str, "Converter"))
		{
			// Arguments are: from/type to/type[;charset=some] command
			// Example: application/msword text/plain;charset=windows-1251 catdoc -a $in
			char * from;
			char * to;
			char * charset;
			char * cmd;
			char * lt;
			// parse the args
			from = GetToken(str + 9, " \t", &lt);
			to = GetToken(NULL, "; \t", &lt);
			cmd = GetToken(NULL, "\r\n", &lt);
			if (!cmd)
			{
				sprintf(conf_err_str, "Error in config file line %d: Too few arguments for Converter\n", i);
				break;
			}
			else
			{
				if ((charset = strstr(cmd, "charset=")) != NULL)
				{
					charset += 8;
					cmd = strchr(charset + 1, ' ');
					if (*cmd != '\0')
					{
						*cmd = '\0'; cmd++;
						while ((*cmd == ' ') ||
							(*cmd == '\t')) cmd++;
					}
					else
						cmd = NULL;
				}
				else
				{
					charset = NULL;
				}
				if (!cmd)
				{
					sprintf(conf_err_str, "Error in config file line %d: Too few arguments for Converter\n", i);
				}
				else
				{
					// Add a new converter
					converters[from] = new CExtConv(from, to, charset, cmd);
				}
			}
		}
#endif /* USE_EXT_CONV */
		else if (!STRNCASECMP(str, "Alias"))
		{
			if (sscanf(str + 5, "%s%s", str1, str2) == 2)
			{
//				AddAlias(str1,str2);
			}
			else
			{
				sprintf(conf_err_str,"Error in config file '%s' line %d:%s",conf_name,i,str);
				return 1;
			}
		}
		else
		if (!STRNCASECMP(str, "Server"))
		{
			CUrl from;
			char srv[2000];
			if (sscanf(str + 6, "%s", str1) != 1)
				continue;
			if (from.ParseURL(str1))
			{
				sprintf(conf_err_str,"File '%s' line %d: Invalid URL: %s",conf_name,i,str1);
				return 1;
			}
			csrv.m_url = str1;
			AddServer(csrv);
			if (load_flags & FLAG_ADD_SERV)
			{
				sprintf(srv, "%s://%s/", from.m_schema, from.m_hostinfo);
				if ((!strcasecmp(from.m_schema, "http")) && (load_flags & FLAG_ADD_SERV) && (csrv.m_userobots))
				{
					sprintf(str,"%s://%s/%s", from.m_schema, from.m_hostinfo, "robots.txt");
					AddHref(str, 0, 0, srv, 1);
				}
				if (load_flags & FLAG_ADD_SERV)
					AddHref(str1, 0, 0, srv, 0);
			}
		}
		else if (!STRNCASECMP(str, "MaxBandwidth"))
		{
			long bandwidth;
			int start, finish = 0;
			switch (sscanf(str + 12, "%li %i %i", &bandwidth, &start, &finish))
			{
			case 1:
				bwSchedule.m_defaultBandwidth = bandwidth;
				break;
			case 2: case 3:
				bwSchedule.AddInterval(start, finish, bandwidth);
				break;
			}
		}
		else	IF_BOOL_PARAM(str,"FollowOutside",csrv.m_outside,0)
		else	IF_BOOL_PARAM(str,"Index",csrv.m_gindex,1)
		else	IF_BOOL_PARAM(str,"Follow",csrv.m_gfollow,1)
		else	IF_BOOL_PARAM(str,"Robots",csrv.m_userobots,1)
		else	IF_BOOL_PARAM(str,"DeleteBad",csrv.m_deletebad,0)
		else	IF_BOOL_PARAM(str,"DeleteNoServer",csrv.m_delete_no_server,1)
		else	IF_BOOL_PARAM(str,"Clones",csrv.m_use_clones,1)

		else	IF_TIME_PARAM(str, "AddressExpiry", AddressExpiry, 3600)
		else	IF_NUM_PARAM(str, "NextDocLimit", NextDocLimit, 1000)
		else	IF_NUM_PARAM(str, "WordCacheSize", WordCacheSize, 50000)
		else	IF_NUM_PARAM(str, "HrefCacheSize", StoredHrefs.m_maxSize, 10000)
		else	IF_NUM_PARAM(str, "DeltaBufferSize", DeltaBufferSize, 0x200)
		else	IF_NUM_PARAM(str, "UrlBufferSize", UrlBufferSize, 0x1000)
		else	IF_NUM_PARAM(str,"Tag",csrv.m_hint,0)
		else	IF_NUM_PARAM(str,"MaxNetErrors",csrv.m_max_net_errors,MAXNETERRORS)
		else	IF_TIME_PARAM(str,"ReadTimeOut",csrv.m_read_timeout,READ_TIMEOUT)
		else	IF_TIME_PARAM(str,"Period",csrv.m_period,DEFAULT_REINDEX_TIME)
		else	IF_NUM_PARAM(str,"MaxHops",csrv.m_maxhops, 0)
		else if (!STRNCASECMP(str, "MaxDocsPerServer"))
		{
			IF_NUM_PARAM(str,"MaxDocsPerServer", csrv.m_max_urls_per_site_per_index,
				(ULONG) -1);
			logger.log(CAT_ALL, L_WARN,
				"WARNING: MaxDocsPerServer is deprecated, please use MaxURLsPerServerPerIndex instead\n");
		}
		// Be careful of ordering here, since we use strncasecmp MaxURLsPerServerPerIndex must come first
		else    IF_NUM_PARAM(str,"MaxURLsPerServerPerIndex", csrv.m_max_urls_per_site_per_index, (ULONG) -1)
		else    IF_NUM_PARAM(str,"MaxURLsPerServer", csrv.m_max_urls_per_site, (ULONG) -1)
		else	IF_BOOL_PARAM(str,"IncrementHopsOnRedirect", csrv.m_increment_redir_hops, 1)
		else    IF_NUM_PARAM(str,"RedirectLoopLimit", csrv.m_redir_loop_limit, DEFAULT_REDIR_LOOP_LIMIT)
		else	IF_TIME_PARAM(str,"MinDelay", csrv.m_minDelay, 0)
		else	IF_NUM_PARAM(str,"IspellCorrectFactor",csrv.m_correct_factor,1)
		else	IF_NUM_PARAM(str,"IspellIncorrectFactor",csrv.m_incorrect_factor,1)
		else	IF_NUM_PARAM(str,"NumberFactor",csrv.m_number_factor,1)
		else	IF_NUM_PARAM(str,"AlnumFactor",csrv.m_alnum_factor,1)
		else	IF_NUM_PARAM(str,"MinWordLength",csrv.m_min_word_length,1)
		else	IF_NUM_PARAM(str,"MaxWordLength",csrv.m_max_word_length,32)
		else	IF_NUM_PARAM(str,"MaxDocSize",_max_doc_size,MAXDOCSIZE)
		else	IF_NUM_PARAM(str, "MaxDocsAtOnce", csrv.m_maxDocs, 1)
		else	if (!STRNCASECMP(str, "NoIndex")) csrv.m_gindex=0;
		else	if (!STRNCASECMP(str, "NoFollow")) csrv.m_gfollow=0;
		else	IF_BOOL_PARAM(str,"OnlineGeo", OnlineGeo, 0)
		else	IF_BOOL_PARAM(str,"IncrementalCitations", IncrementalCitations, 1)
		else	IF_BOOL_PARAM(str,"CompactStorage", CompactStorage, 1)
		else	IF_BOOL_PARAM(str,"HiByteFirst", HiLo, 0)
		else	IF_BOOL_PARAM(str,"UtfStorage", UtfStorage, 0)
		else if (!STRNCASECMP(str, "Include"))
		{
			if ((sscanf(str + 7, "%s", str1)) && (config_level < 5))
			{
				if (LoadConfig(str1, config_level + 1, load_flags))
					return 1;
			}
		}
		else if (!STRNCASECMP(str, "Countries"))
		{
			if (sscanf(str + 9, "%s", str1))
			{
				if (isAbsolutePath(str1))
					strcpy(str, str1);
				else
					sprintf(str, "%s/%s", CONF_DIR, str1);
				ImportCountries(str);
			}
		}
		else if (!STRNCASECMP(str, "MirrorRoot"))
		{
			if ((sscanf(str + 10, "%s", str1)))
			{
				if (isAbsolutePath(str1))
					strcpy(str, str1);
				else
					sprintf(str, "%s/%s", CONF_DIR, str1);
			}
			else
			{
				sprintf(str, "%s/mirrors", CONF_DIR);
			}
			MirrorRoot = strdup(str);
			csrv.m_use_mirror = csrv.m_use_mirror > 0 ? csrv.m_use_mirror : 0;
		}
		else if (!STRNCASECMP(str, "MirrorHeadersRoot"))
		{
			if ((sscanf(str + 17, "%s", str1)))
			{
				if (isAbsolutePath(str1))
					strcpy(str,str1);
				else
					sprintf(str, "%s/%s", CONF_DIR, str1);
			}
			else
			{
				sprintf(str, "%s/headers", CONF_DIR);
			}
			MirrorHeadersRoot = strdup(str);
			csrv.m_use_mirror = csrv.m_use_mirror > 0 ? csrv.m_use_mirror : 0;
		}
		else IF_TIME_PARAM(str, "MirrorPeriod", csrv.m_use_mirror, MIRROR_NO)
		else if (!STRNCASECMP(str, "Replace"))
		{
			csrv.AddReplacement(str + 7);
		}
		else
		{
			sprintf(conf_err_str, "Error in config file line %d: %s", i, str);
			return 1;
		}
		str[0] = 0;
	}
	fclose(config);

	// Ensure per index max doesn't exceed per site max ...
	if (csrv.m_max_urls_per_site_per_index > csrv.m_max_urls_per_site)
	{
		csrv.m_max_urls_per_site_per_index = csrv.m_max_urls_per_site;
	}

#ifndef UNICODE
	if (!GetDefaultCharset())
	{
		SetDefaultCharset(GetCharset(localcharset.c_str()));
		logger.log(CAT_ALL, L_DEBUG, "Set localcharset to [%s]\n", localcharset.c_str());
	}
#endif /* UNICODE */

	if (DBWordDir.empty())
		DBWordDir = DataDir;

	/* On level0 : Free some variables, prepare others, etc */
	if (config_level == 0)
	{
		/* Add one virtual server if we want FollowOutside */
		/* or DeleteNoServer no	*/

		if ((csrv.m_outside) || (!csrv.m_delete_no_server))
		{
			csrv.m_url = "";
			AddServer(csrv);
		}
		else
		{
			csrv.m_url = "";
		}
		if (UrlBufferSize == 0) UrlBufferSize = DeltaBufferSize << 3;
#ifdef UNICODE
		FixLangs();
#endif
	}
	logger.log(CAT_FILE, L_INFO, "Loading configuration from %s\n", config_file_name.c_str());
	return 0;
}
