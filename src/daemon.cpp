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

/*  $Id: daemon.cpp,v 1.97 2004/03/22 17:36:20 kir Exp $
    Author : Alexander F. Avdonkin
	Uses parts of UdmSearch code
	Implemented classes: CSearchContext, CWorkerThreadList
*/
#include <signal.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dlfcn.h>
#include <string.h>
#include <string>
#include <vector>
#include "sqldb.h"
#include "defines.h"
#include "daemon.h"
#include "affix.h"
#include "charsets.h"
#include "search.h"
#include "excerpts.h"
#include "qparser.h"
#include "paths.h"
#include "logger.h"
#include "parse.h"
#include "stopwords.h"
#include "lastmod.h"
#include "url.h"
#include "urlfilter.h"
#include "acl.h"
#ifdef UNICODE
#include "ucharset.h"
#include "misc.h"

using std::sort;


CCharsetB* LocalCharsetU = &baseCharset;

#endif

using std::string;
using std::vector;

#define doit_forever 1
#include "paths.h"
#include "cache.h"

int MaxThreads = 10;
string ConfDir = CONF_DIR;
string DataDir = DATA_DIR;

CSiteWeights siteWeights;
MapStringToInt hostWeights;
CStopWords Stopwords;
CLastMods last_mods;
CCache cache;
bool use_lastmod = 1;	// Yes, use last_mods.
CIpAccess ipaccess;
int WordForms = SEARCH_MODE_EXACT;
string WordFormLangs;
int AccDist = ACCOUNT_DISTANCE_YES;
int IncrementalCitations = 1;
int CompactStorage = 1;
extern int MinPatLen;	// in qparser.cpp
int MultiDBConnections = 1;

// daemon log file (in DataDir)
#define DEFAULT_SEARCHD_LOGFILE "dlog.log"
char * logfile = DEFAULT_SEARCHD_LOGFILE;

PSQLDatabase (*newDB)(void);
/* search modes */
#define ORD_RATE	1
#define ORD_DATE	2
#define MOD_ALL		1
#define MOD_ANY		2

typedef struct
{
	char m_lang[20];
	char m_name[STRSIZ];
#ifdef UNICODE
	char m_charset[STRSIZ];
#endif
} LangAff;

// seems that searchd coredumps then thread stack size is less than 128K
#define NEEDED_STACK_SIZE 131072
int thread_stack_size = 0;

int Port = DEFAULT_PORT;

/// Paths to DB backend library
vector<string> dblib_paths;

void init_dblib_paths(void)
{
	string p;
	dblib_paths.clear();
	p = LIB_DIR;
	dblib_paths.push_back(p);
}


#ifdef UNICODE
void GetHilightPos(const char *src, CStringSet& w_list, CCharsetB* charset, CULONGVector& positions)
{
	char* lt = (char*)src;
	char* token;
	WORD uword[MAX_WORD_LEN + 1];
	while (true)
	{
		int r = GetUWord((unsigned char**)&lt, charset, uword, &token);
		if (r == 1) break;
		if (r == 0)
		{
			if (w_list.IsHighlight(uword))
			{
				positions.push_back(token - src);
				positions.push_back(lt - src);
			}
		}
	}
	positions.push_back(0);
	positions.push_back(0);
}

void CSrcDocumentS::RecodeTo(CCharsetB* charset)
{
	CCharsetB* srcCharset = GetUCharset(m_charset.c_str());
	if (charset != srcCharset)
	{
		for (ULONG i = 0; i < m_excerpts.size(); i++)
		{
			m_excerpts[i] = charset->RecodeFrom(m_excerpts[i], srcCharset);
		}
		m_text = charset->RecodeFrom(m_text, srcCharset);
		m_title = charset->RecodeFrom(m_title, srcCharset);
		m_keywords = charset->RecodeFrom(m_keywords, srcCharset);
		m_descr = charset->RecodeFrom(m_descr, srcCharset);
	}
}

void CSrcDocumentS::GetHilightPos(CCharsetB* charset, CStringSet& lightWords)
{
	if (!m_excerpts.size())
	{
		m_excerpts.push_back(m_text);
	}

	for (ULONG i = 0; i < m_excerpts.size(); i++)
	{
		::GetHilightPos(m_excerpts[i].c_str(), lightWords, charset, m_excerptsH);
	}
	::GetHilightPos(m_title.c_str(), lightWords, charset, m_titleH);
	::GetHilightPos(m_keywords.c_str(), lightWords, charset, m_keywordsH);
	::GetHilightPos(m_descr.c_str(), lightWords, charset, m_descrH);
}
#endif

void CSrcDocumentS::Send(CBufferedSocket& socket)
{
	socket.SendULONG(m_urlID);
	socket.SendULONG(m_siteID);
	socket.SendULONG(m_last_index_time);
	socket.SendULONG(m_next_index_time);
	socket.SendULONG(m_hops);
	socket.SendULONG(m_referrer);
	socket.SendULONG(m_status);
	socket.SendULONG(m_slow);
	socket.SendString(m_crc.c_str());
	socket.SendString(m_last_modified.c_str());
	socket.SendString(m_url.c_str());
	socket.SendString(m_descr.c_str(), m_descr.size());
	socket.SendString(m_keywords.c_str(), m_keywords.size());
	socket.SendString(m_title.c_str(), m_title.size());
	socket.SendString(m_content_type.c_str(), m_content_type.size());

	socket.SendULONG(m_excerpts.size());
	for (ULONG i = 0; i < m_excerpts.size(); i++)
	{
		socket.SendString(m_excerpts[i].c_str(), m_excerpts[i].size());
	}
	socket.SendULONG(m_size);
#ifdef UNICODE
//	socket.SendULONGVector(m_textH);
	socket.SendULONGVector(m_descrH);
	socket.SendULONGVector(m_titleH);
	socket.SendULONGVector(m_keywordsH);
	socket.SendULONGVector(m_excerptsH);
#endif
}

int LoadDaemonConfig(const char *name, int level)
{
	FILE *file;
	vector<LangAff*> affixes;
	vector<LangAff*> dictionaries;
//	int variables = 0;
	char *s, *lasttok = NULL;
	char str[BUFSIZ];
	char str1[STRSIZ];
#ifdef UNICODE
	char str2[STRSIZ];
#endif
//	char first_letters[64]="";
	string localcharset;

	if(!name)
		return 1;

	file = fopen(name,"r");
	if (!file)
	{
		logger.log(CAT_ALL, L_ERR, "Can't open config file %s\n", name, strerror(errno));
		return 1;
	}

	/* Init for the case when there is no  */
	/* LocalCharset definition in template */
	while ((fgets(str, sizeof(str), file)))
	{
		if ((s = GetToken(str, "\r\n", &lasttok)))
		{
			if (s[0] == '#')
			{
				continue;
			}
			else if (!STRNCASECMP(str,"Include"))
			{
				string name, dir;

				name = Trim(str + 7, "= \t\r\n");

				if (!isAbsolutePath(name.c_str()))
				{
					dir = ConfDir + "/";
				}
				dir += name;
				LoadDaemonConfig(dir.c_str(), level + 1);
			}
			else if (!STRNCASECMP(str,"DBHost"))
			{
					DBHost = Trim(str + 6, "= \t\r\n");
			}
			else if (!STRNCASECMP(str, "DBType"))
			{
					DBType = Trim(str + 6, "= \r\n\t");
			}
			else if (!STRNCASECMP(str, "DBName"))
			{
					DBName = Trim(str + 6,"= \t\r\n");
			}
			else if (!STRNCASECMP(str, "DBWordDir"))
			{
					DBWordDir = Trim(str + 9,"= \t\r\n");
			}
			else if (!STRNCASECMP(str, "DataDir"))
			{
					DataDir = Trim(str + 7,"= \t\r\n");
			}
			else if (!STRNCASECMP(str, "DBUser"))
			{
					DBUser = Trim(str + 6, "= \t\r\n");
			}
			else if (!STRNCASECMP(str, "DBPass"))
			{
					DBPass = Trim(str + 6, "= \t\r\n");
			}
			else if (!STRNCASECMP(str, "DBPort"))
			{
					DBPort = atoi(Trim(str + 6, "= \t\r\n"));
			}
			else if (!STRNCASECMP(str, "Port"))
			{
					Port = atoi(Trim(str + 4, "= \t\r\n"));
			}
//			else if (!STRNCASECMP(str, "MaxExcerptLen"))
//			{
//				MaxExcerptLen = atoi(Trim(str + 13, "= \t\r\n"));
//			}
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
				if (sscanf(str+12, "%2s %s", lang, file) == 2)
				{
					Stopwords.Load(file, lang);
				}
#endif
				else
				{
					logger.log(CAT_FILE, L_ERR,
						"Bad format: %s in %s\n",
						str, name);
				}
				delete file;
#ifdef UNICODE
				delete encoding;
#endif
			}
			else if (!STRNCASECMP(str, "LocalCharset"))
			{
					localcharset = Trim(str + 12, "= \r\t\n");
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
					logger.log(CAT_ALL, L_ERR, "Bad CharsetAlias format: %s\n", str );

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
				int param, charset_id=0;

				param = sscanf(str + 14, "%s%s%s", name, lang, dir);
				if (param == 3)
				{
					if ((charset_id = LoadCharsetU1(lang, name, dir)) == -1)
					{
						logger.log(CAT_FILE, L_WARN, "Charset %s has not been loaded\n", name);
					}
				}
				if (param <3)
				{
					logger.log(CAT_ALL, L_ERR, "Bad CharsetTableU1 format: %s", str);
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
				int param, charset_id=0;

				param = sscanf(str + 14, "%s%s%s", name, lang, dir);
				if (param == 3)
				{
					if ((charset_id = LoadCharsetU2V(lang, name, dir)) == -1)
					{
						logger.log(CAT_FILE, L_WARN, "Charset %s has not been loaded\n", name);
					}
				}
				if (param <3)
				{
					logger.log(CAT_ALL, L_ERR, "Bad CharsetTableU2 format: %s", str);
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
#endif
			else if (!STRNCASECMP(str, "CharsetTable"))
			{
				int len = strlen(str);
				char *name = new char[len];
				char *dir = new char[len];
				char *lang = new char[len];

				if (sscanf(str+12, "%s%s%s", name, lang, dir) == 3)
				{
					if (LoadCharset(lang, name, dir)==-1)
					{
						logger.log(CAT_ALL, L_ERR, "Charset %s has not been loaded\n", name);
					}
				}
				else
				{
					logger.log(CAT_ALL, L_ERR, "Bad CharsetTable format: %s\n", str );
				}
				delete name;
				delete dir;
				delete lang;
			}
			else if (!STRNCASECMP(str, "MaxThreads"))
			{
				MaxThreads = atoi(Trim(str + 10, "= \t\r\n"));
			}
			else if (!STRNCASECMP(str, "DebugLevel"))
			{
				logger.setloglevel(Trim(str + 10, "= \t\r\n"));
			}
			else if (!STRNCASECMP(str, "DBAddr"))
			{
				ParseDBAddr(Trim(str + 6, "= \t\r\n"));
			}
			else if (!STRNCASECMP(str, "SiteWeight"))
			{
				char site[MAX_URL_LEN + 1];
				int weight;
				if (sscanf(str + 10, "%s %i", site, &weight) == 2)
				{
					hostWeights[site] = weight;
				}
			}
			else if (!STRNCASECMP(str, "WordForms"))
			{
				if (strcmp(Trim(str + 9, "= \r\t\n"), "on") == 0)
				{
					WordForms = SEARCH_MODE_FORMS;
				}
				else if (strcmp(Trim(str + 9, "= \r\t\n"), "off") == 0)
				{
					WordForms = SEARCH_MODE_EXACT;
				}
				else // we have a comma-separated list of languages here
				{
					WordForms = SEARCH_MODE_FORMS;
					// Surrond the list with spaces
					WordFormLangs = ",";
					WordFormLangs.append(Trim(str + 9, "= \r\t\n"));
					WordFormLangs.append(",");
				}
			}
			else if (!STRNCASECMP(str, "MinFixedPatternLength"))
			{
				MinPatLen = atoi(Trim(str + 21, "= \t\r\n"));
			}
			else if (!STRNCASECMP(str, "IncrementalCitations"))
			{
				if (strcmp(Trim(str + 20, "= \r\t\n"), "no") == 0)
				{
					IncrementalCitations = 0;
				}
			}
			else if (!STRNCASECMP(str, "CompactStorage"))
			{
				if (strcmp(Trim(str + 14, "= \r\t\n"), "no") == 0)
				{
					CompactStorage = 0;
				}
			}
			else if (!STRNCASECMP(str, "HiByteFirst"))
			{
				if (strcmp(Trim(str + 11, "= \r\t\n"), "yes") == 0)
				{
					HiLo = 1;
				}
			}
			else if (!STRNCASECMP(str, "MultipleDBConnections"))
			{
				if (strcmp(Trim(str + 21, "= \r\t\n"), "yes") == 0)
				{
					MultiDBConnections = 1;
				}
			}
			else if (!STRNCASECMP(str, "UtfStorage"))
			{
				if (strcmp(Trim(str + 10, "= \r\t\n"), "yes") == 0)
				{
					UtfStorage = 1;
				}
			}
			else if (!STRNCASECMP(str, "Affix"))
			{
				LangAff* la = new LangAff;
#ifdef UNICODE
				int n;
				if ((n = sscanf(str + 5,"%s%s%s", la->m_lang, str1, str2)) >= 2)
				{
					strcpy(la->m_charset, n > 2 ? str2 : "");
#else
				if (sscanf(str + 5,"%s%s", la->m_lang, str1) == 2)
				{
#endif
					if (isAbsolutePath(str1))
						strcpy(la->m_name, str1);
					else
						sprintf(la->m_name, "%s/%s", CONF_DIR, str1);
					affixes.push_back(la);
				}
				else
				{
					delete la;
					logger.log(CAT_ALL, L_ERR, "Error in config file '%s' %s", name, str);
				}
			}
			else if (!STRNCASECMP(str, "Spell"))
			{
				LangAff* la = new LangAff;
#ifdef UNICODE
				int n;
				if ((n = sscanf(str + 5,"%s%s%s", la->m_lang, str1, str2)) >= 2)
				{
					strcpy(la->m_charset, n > 2 ? str2 : "");
#else
				if (sscanf(str + 5, "%s%s", la->m_lang, str1) == 2)
				{
#endif
					if (isAbsolutePath(str1))
						strcpy(la->m_name, str1);
					else
						sprintf(la->m_name, "%s/%s", CONF_DIR, str1);
					dictionaries.push_back(la);
				}
				else
				{
					delete la;
					logger.log(CAT_ALL, L_ERR, "Error in config file '%s' %s", name, str);
				}
			}
			else if (!STRNCASECMP(str, "AllowFrom"))
			{
				ipaccess.AddElem(str + 9);
			}
			else if (!STRNCASECMP(str, "AccountDistance"))
			{
				if (strcmp(Trim(str + 15, "= \r\t\n"), "off") == 0)
				{
					AccDist = ACCOUNT_DISTANCE_NO;
				}
			}
			else if (!STRNCASECMP(str, "CacheLocalSize"))
			{
				cache.SetLocalSize(atoi(Trim(str + 14, "= \t\r\n")));
			}
			else if (!STRNCASECMP(str, "CachedUrls"))
			{
				cache.SetUrlsSize(atoi(Trim(str + 10, "= \t\r\n")));
			}
			else if (!STRNCASECMP(str, "Cache"))
			{
				if (strcmp(Trim(str + 5, "= \r\t\n"), "on") == 0)
				{
					cache.CacheOn();
				}
			}
		}
	}
	fclose(file);

	if (!GetDefaultCharset())
	{
#ifdef UNICODE
		LocalCharsetU = GetUCharset(localcharset.c_str());
#else
		SetDefaultCharset(GetCharset(localcharset.c_str()));
		InitCharset();
		logger.log(CAT_ALL, L_DEBUG, "Set localcharset to [%s]\n", localcharset.c_str());
#endif /* UNICODE */
	}
	if (DBWordDir.empty())
		DBWordDir = DataDir;

	vector<LangAff*>::iterator pla;
	for (pla = affixes.begin(); pla != affixes.end(); pla++)
	{
#ifdef UNICODE
		if (ImportAffixes((*pla)->m_lang, (*pla)->m_name, (*pla)->m_charset[0] ? (*pla)->m_charset : localcharset.c_str()))
#else
		if (ImportAffixes((*pla)->m_lang, (*pla)->m_name))
#endif
		{
			logger.log(CAT_ALL, L_ERR, "Can't load affix :%s", (*pla)->m_name);
		}
		delete *pla;
	}
	for (pla = dictionaries.begin(); pla != dictionaries.end(); pla++)
	{
#ifdef UNICODE
		if (ImportDictionary((*pla)->m_lang, (*pla)->m_name, (*pla)->m_charset[0] ? (*pla)->m_charset : localcharset.c_str()))
#else
		if (ImportDictionary((*pla)->m_lang, (*pla)->m_name))
#endif
		{
			logger.log(CAT_ALL, L_ERR, "Can't load affix :%s", (*pla)->m_name);
		}
		delete *pla;
	}
#ifdef UNICODE
	if (level == 0) FixLangs();
#endif
	return 0;
}

void BuildSiteWeights(CSQLDatabase* db)
{
	for (MapStringToInt::iterator it = hostWeights.begin(); it != hostWeights.end(); it++)
	{
		ULONG siteID;
		if (db->GetSite(it->first.c_str(), siteID))
		{
			siteWeights.push_back(CSiteWeight(siteID, it->second));
		}
		else
		{
			logger.log(CAT_ALL, L_WARN, "Unknown site name "
				"in SiteWeight: %s\n", it->first.c_str());
		}
	}
	sort(siteWeights.begin(), siteWeights.end());
}

// This function reads query statistics info from socket and inserts it to the database
void AddStat(CBufferedSocket& socket, CSQLDatabase* database)
{
	string p_query, ul, spaces, proxy, addr, referer;
	ULONG np, ps, urls, sites, site;
	struct timeval start, finish;
	if (socket.RecvString(p_query) < 0) return;
	if (socket.RecvString(ul) < 0) return;
	if (socket.RecvULONG(np) <= 0) return;
	if (socket.RecvULONG(ps) <= 0) return;
	if (socket.RecvULONG(urls) <= 0) return;
	if (socket.RecvULONG(sites) <= 0) return;
	if (socket.RecvULONG(start.tv_sec) <= 0) return;
	if (socket.RecvULONG(start.tv_usec) <= 0) return;
	if (socket.RecvULONG(finish.tv_sec) <= 0) return;
	if (socket.RecvULONG(finish.tv_usec) <= 0) return;
	if (socket.RecvString(spaces) < 0) return;
	if (socket.RecvULONG(site) <= 0) return;
	if (socket.RecvString(proxy) < 0) return;
	if (socket.RecvString(addr) < 0) return;
	if (socket.RecvString(referer) < 0) return;
	database->AddStat(proxy.c_str(), addr.size() == 0 ? NULL : addr.c_str(), p_query.c_str(), ul.c_str(), np, ps, urls, sites, start, finish, spaces.c_str(), site, referer.c_str());
}

// This function is not used currently
void CSearchContext::GetDocuments()
{
	ULONG len;
	if (m_socket.RecvULONG(len))
	// FIXME: check that len is sane
	{
		int lenb = len * sizeof(ULONG);
		ULONG* urlID = (ULONG*)alloca(lenb);
		if (m_socket.Recv(urlID, lenb) == lenb)
		{
			for (unsigned int i = 0; i < len; i++)
			{
				CSrcDocumentS doc;
				m_database->GetDocument(urlID[i], doc);
				doc.Send(m_socket);
			}
		}
	}
}

void CSearchContext::ProcessCommands(CSearchExpression* src)
{
	while (doit_forever)
	{
		int cmd;
		int r = m_socket.Recv(&cmd, sizeof(cmd));
		if (r != sizeof(cmd)) break;
		if (cmd == 0) break;
		switch (cmd)
		{
		case GET_SITE:
			{
				// Get site name by site ID
				int site;
				if (m_socket.Recv(&site, sizeof(site)) == sizeof(site))
				{
					char sitename[130] = "";
					m_database->GetSite(site, sitename);
					m_socket.SendString(sitename);
				}
				else
				{
					int err = -1;
					m_socket.Send(&err, sizeof(err));
				}
				break;
			}
		case GET_DOCUMENT:
			{
				// Get URL info by URL ID
				ULONG urlID;
				int exc = 0;	// Maximal excerpts count to retrieve
				ULONG weight, excl;
				if (m_socket.RecvULONG(urlID) && m_socket.RecvULONG(exc) && m_socket.RecvULONG(excl) && m_socket.RecvULONG(weight))
				{
					CSrcDocumentS doc;
					m_database->GetDocument(urlID, doc, exc);
					if (exc && doc.m_content)
					{
						// Retrieve excerpts
						CMatchFinder mf(src->MaxSize(), excl);
						mf.m_search = src;
						mf.m_excerpts = exc;
						mf.m_total_excerpts = exc;
						mf.m_match = ((weight >> 27) & 3) | ((weight >> 29) & 4);	// Phrase matching desired
						mf.ParseDoc(&doc);
						if (mf.m_lastExcerpt)
						{
							// At least one except matching query is found in the original document
							// Build excerpts string
							mf.FormExcerpts(doc.m_excerpts);
						}
					}
#ifdef UNICODE
					doc.RecodeTo(m_outCharset);
					doc.GetHilightPos(m_outCharset, *m_lightWords);
#endif
					doc.Send(m_socket);
				}
				break;
			}
		case GET_DOCUMENTS:
			GetDocuments();
			break;
		case GET_CLONELIST:
			{
				// Get clone list by crc, exclude URL ID, specified by origin
				ULONG origin, siteID;
				char crc[33];
				CSrcDocVector vec;
				m_socket.RecvULONG(origin);
				m_socket.RecvULONG(siteID);
				m_socket.RecvString(crc);
				m_database->GetCloneList(crc, origin, vec, siteID);
				m_socket.SendULONG(vec.size());
				for (unsigned int i = 0; i < vec.size(); i++)
				{
					CSrcDocumentS* doc = (CSrcDocumentS*)(vec[i]);
					doc->Send(m_socket);
				}
				break;
			}
		case ADD_STAT:
			// Store query statistics
			AddStat(m_socket, m_database);
			break;
		case GET_ERROR:
			// Get error message
			m_socket.SendString(m_error.c_str(), m_error.size());
			break;
		}
	}
}

void CSearchContext::PrintCached(const char* url, CSearchExpression* expr)
{
	char* content;
	char content_type[50];
	// Retrieve unzipped content from database
#ifdef UNICODE
	char charset[100];
	if (m_database->GetContent(url, content, content_type, charset))
#else
	if (m_database->GetContent(url, content, content_type))
#endif
	{
		ULONG corder = 1;
		int esize = expr ? expr->MaxSize() : 0;
		CWordFinder wfinder(esize);
		if (esize)
		{
			expr->SetColorOrder(corder);
			wfinder.m_content = content;
			wfinder.m_search = expr;
#ifdef UNICODE
			wfinder.m_ucharset = GetUCharset(charset);
#endif
			wfinder.ParseHtml();
		}
		int contlen = content ? strlen(content) : 0;
		ULONG ofsz = wfinder.m_offsets.size();
		m_socket.SendULONG(0);
		m_socket.SendULONG(ofsz);
		m_socket.Send(&(*wfinder.m_offsets.begin()), ofsz << 2);
		m_socket.Send(&(*wfinder.m_colors.begin()), ofsz << 1);
		m_socket.SendString(url);
		m_socket.SendString(content_type);
		m_socket.SendULONG(contlen);
		m_socket.Send(content ? content : "", contlen);
	}
	else
	{
		m_socket.SendULONG(1);
	}
	if (content) delete content;
}

/// Process one query for search context
void* processReq1(CSearchContext* ctx)
{
	CCgiQuery cgiQ;
	// Receive length of full query parameters string
	if (cgiQ.Recv(ctx->m_socket) >= 0) // received ok
	{
		// Set URL filter
		CResultUrlFilter* filter;
		if (((cgiQ.m_t_from) || (cgiQ.m_t_to)) && use_lastmod )
		{
			filter = new CTimeFilter(last_mods, cgiQ.m_t_from, cgiQ.m_t_to);
		}
		else
		{
			filter = new CStubFilter();
		}
		filter->m_accDist = (cgiQ.m_accDist == ACCOUNT_DISTANCE_UNK ? AccDist : cgiQ.m_accDist) == ACCOUNT_DISTANCE_YES ? 1 : 0;
		CQueryParser parser(filter);
#ifdef UNICODE
		parser.m_ucharset = cgiQ.m_charset.size() > 0 ? GetUCharset(cgiQ.m_charset.c_str()) : LocalCharsetU;
		ctx->m_outCharset = parser.m_ucharset;	//TO DO optionally set different output charset
#endif
		parser.m_database = ctx->m_database;
		parser.m_search_mode = cgiQ.m_search_mode == SEARCH_MODE_NONE ? WordForms : cgiQ.m_search_mode;
		if (!cgiQ.m_wordform_langs.empty())
			parser.m_wordform_langs = cgiQ.m_wordform_langs;
		else // set to default
			parser.m_wordform_langs = WordFormLangs;
//		gettimeofday(&tm, NULL);
		CStringSet LightWords;
#ifdef UNICODE
		ctx->m_lightWords = &LightWords;
#endif
//		char wordinfo[STRSIZ] = "";
		// Build parse tree http://ben.aspads.net/ex/c/1664/1664680565031364287
		CSearchExpression* src = parser.ParseQuery(cgiQ.m_query_words.c_str());
		if (cgiQ.m_cached.size())
		{
			ctx->PrintCached(cgiQ.m_cached.c_str(), src);
			if (src) src->Release();
			return NULL;
		}
		if (src)
		{
			ULONG key;
			char *urlsp;
			int gr = cgiQ.m_gr;
			char *nrquery = NULL;
			if (cache.m_status)
			{
				nrquery = (char*)src->NormalizedQuery();
			}
			key = cgiQ.MakeQueryKey(nrquery);
			ULONG ps = cgiQ.m_ps ? cgiQ.m_ps : DEFAULT_PS;
			ULONG n = cgiQ.m_take ? 1 : (cgiQ.m_np + 1) * ps;
			/* Do not allow too high result number to prevent
			 * DoS attack or coredump due to failed malloc.
			 */
			if (n > 1000000) n = 1000000;

			if (!(urlsp = cache.GetCached(key, n)))
			{
				CResult* res[2];	// Results for main and "real-time" database
				ULONG urlCount;
				// Compute results for query
				urlCount = src->GetResult(cgiQ.m_ul_unescaped.c_str(), cgiQ.m_site, cgiQ.m_gr, cgiQ.m_sort_by, cgiQ.m_spaces, res);
				parser.AddWords(LightWords);
				LightWords.CollectPatterns();
				if ((res[0] && res[0]->NotEmpty()) || (res[1] && res[1]->NotEmpty()))
				{
					// At least 1 site is found
					ULONG nFirst, rescount;
					if (cache.m_status)
					{
						nFirst = cgiQ.m_take ? 1 : (cache.m_urlssize < n) ? n : cache.m_urlssize;
					}
					else
					{
						nFirst = n;
					}
					ULONG nTotal;
					// Prepare buffer for results with size twice more than last result number,
					// because max 2 URLs can be returned for each site
					CUrlW* urls = new CUrlW[nFirst << 1];
//					ULONG totalSiteCount = (res[0] ? res[0]->m_infoCount : 0) + (res[1] ? res[1]->m_infoCount : 0);
					ULONG totalSiteCount = TotalSiteCount(res);
					ctx->m_socket.SendULONG(0);
					ctx->m_socket.SendULONG(totalSiteCount);
					// Don't group by sites if only 1 site found and other daemons has not found pages for query
					if (cgiQ.m_gr && (totalSiteCount == 1))
					{
						ctx->m_socket.RecvULONG(cgiQ.m_gr);
					}
					CUrlW* urle = urls;
					// if last_mods are disabled than SORT_BY_DATE is unavailable
					if ((!use_lastmod) && (cgiQ.m_sort_by == SORT_BY_DATE))
						cgiQ.m_sort_by = SORT_BY_RATE;
					// Get results with highest ranks
					GetUrls(res, nFirst, nTotal, urle, cgiQ.m_gr, cgiQ.m_sort_by);
					// Get url count for first N records
					if (cgiQ.m_gr)
					{
						rescount = cache.GetUrls(n , urls, (urle - urls));
					}
					else
					{
						rescount = n < (ULONG)(urle - urls) ? n : (urle - urls);
					}
					// Send results to the client
					ctx->m_socket.SendULONG(rescount);
					ctx->m_socket.SendULONG(nTotal);
					ctx->m_socket.SendULONG(urlCount);
					ctx->m_socket.SendULONG(parser.m_complexPhrase);
					ctx->m_socket.Send(urls, rescount*sizeof(CUrlW));
					// Send words to highlight in the results
					ctx->m_socket.SendULONG(LightWords.size());
					for (CStringSet::iterator it = LightWords.begin(); it != LightWords.end(); it++)
					{
						CWordStat& s = it->second;
#ifdef UNICODE
						const CUWord& w = it->first;
						ctx->m_socket.SendWString(w.Word());
						ctx->m_socket.SendString(s.m_output.c_str());
#else
						const string& w = it->first;
						ctx->m_socket.SendString(w.c_str(), w.size());
#endif
						ctx->m_socket.SendULONG(s.m_urls);
						ctx->m_socket.SendULONG(s.m_total);
					}
					cache.AddToCache(key, totalSiteCount, (urle - urls), nTotal, urlCount, parser.m_complexPhrase, (char*)urls, cgiQ.m_gr, &LightWords);
					if (cgiQ.m_gr != gr && (totalSiteCount == 1))
					{
						key = cgiQ.MakeQueryKey(nrquery);
						cache.AddToCache(key, totalSiteCount, (urle - urls), nTotal, urlCount, parser.m_complexPhrase, (char*)urls, cgiQ.m_gr, &LightWords);
					}
					delete urls;
				}
				else
				{
					// No results found
	//				int s = 0;
					ctx->m_socket.SendULONG(0);
					ctx->m_socket.SendULONG(0);
				}
			}
			else
			{
				ULONG cnt, totalSiteCount, rescount;
				ULONG *p = (ULONG *) urlsp;

				parser.AddWords(LightWords);
				LightWords.CollectPatterns();

				ctx->m_socket.SendULONG(0); p++;
				totalSiteCount = *p++;
				rescount = *p++;
				ctx->m_socket.SendULONG(totalSiteCount);
				if (cgiQ.m_gr && (totalSiteCount == 1))
				{
					ctx->m_socket.RecvULONG(cgiQ.m_gr);
					cnt = n < rescount ? n : rescount;
				}
				else if (cgiQ.m_gr)
				{
					cnt = cache.GetUrls(n, (CUrlW*)(urlsp + 6 * sizeof(ULONG)), rescount);
				}
				else
				{
					cnt = n < rescount ? n : rescount;
				}
				ctx->m_socket.SendULONG(cnt);
				ctx->m_socket.SendULONG(*p++);
				ctx->m_socket.SendULONG(*p++);
				ctx->m_socket.SendULONG(*p++);
				ctx->m_socket.Send((char*)p, cnt * sizeof(CUrlW));
				p = (ULONG*)((char*)p + rescount * sizeof(CUrlW)); p++;
				ctx->m_socket.SendULONG(LightWords.size());
				for (CStringSet::iterator it = LightWords.begin(); it != LightWords.end(); it++)
				{
#ifdef UNICODE
					CWordStat& s = it->second;
					const CUWord& w = it->first;
					ctx->m_socket.SendWString(w.Word());
					ctx->m_socket.SendString(s.m_output.c_str());
#else
					const string& w = it->first;
					ctx->m_socket.SendString(w.c_str(), w.size());
#endif
					ctx->m_socket.SendULONG(*p++);
					ctx->m_socket.SendULONG(*p++);
				}
			}
			if (nrquery) delete nrquery;
		}
		else
		{
			// Error in query
//			int code = 1;
			ctx->m_error = parser.m_error;
			ctx->m_socket.SendULONG(1);

		}
		parser.Clear();
		delete filter;
		// Process subsequent requests from the client (retrieve URL, text, excerpts, clones...)
		ctx->ProcessCommands(src);
		if (src) src->Release();

	}
	else
	{
		logger.log(CAT_ALL, L_ERR, "Error in CCgiQuery::Recv (misformed query?)\n");
	}
	return NULL;
}

/// Main worker thread function, processes incoming requests
void* processReqs(void* p)
{
	CWorkerThread* thread = (CWorkerThread*)p;
	while (doit_forever)
	{
		// Wait for event, posted in CWorkerThreadList::processReq
		thread->m_req.Wait();
		// Create search context
		CSearchContext ctx(thread->m_database, thread->m_socket);
		while (doit_forever)
		{
			// Process one query
			processReq1(&ctx);
			ctx.m_socket.Close();
			{
				CLocker lock(&thread->m_parent->m_mutex);
				if (thread->m_parent->IsWaiting())
				{
					// Process other queries in the loop buffer
					ctx.m_socket = thread->m_parent->PopFirstWaiting();
				}
				else
				{
					// Make thread available for other requests
					thread->Insert(&thread->m_parent->m_first);
					break;
				}
			}
		}
	}
	delete thread;
	return NULL; // to make g++ happy
}

/// This method starts processing query, or puts socket "csock" into
/// loop buffer if number of running worker threads have reached max value
void CWorkerThreadList::processReq(CSQLDatabase* database, int csock)
{
	CLocker lock(&m_mutex);
	CWorkerThread* thread = NULL;
	if (m_first == NULL)
	{
		// No threads are available in the linked list
		if (m_threads < m_maxThreads)
		{
			// Thread count has not reached maximal value, create new one
			thread = new CWorkerThread;
			if (MultiDBConnections)
			{
				thread->m_database = newDB();
				if (!thread->m_database->Connect(DBHost.c_str(), DBUser.c_str(), DBPass.c_str(), DBName.c_str(), DBPort))
				{
					thread->m_database = database;
				}
			}
			else
			{
				thread->m_database = database;
			}
			thread->m_parent = this;
			pthread_attr_t attr;
			pthread_attr_init(&attr);
			if (thread_stack_size)
				pthread_attr_setstacksize(&attr, thread_stack_size);
			pthread_create(&thread->m_thread, &attr, processReqs, thread);
			m_threads++;
		}
		else
		{
			// Otherwise put socket into loop buffer and return
			InsertSocket(csock);
			return;
		}
	}
	else
	{
		// Remove first worker thread from the linked list
		thread = m_first->Remove();
	}
	thread->m_socket = csock;
	// Start processing the query
	thread->m_req.Post();
}

int daemon_child(const char* config)
{
	if (LoadDaemonConfig(config, 0))
	{
		return 1;
	}
	CalcWordDir();

	struct sockaddr_in addr;
	memset((void *)&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(Port);
	if (ipaccess.IsLocalhostOnly())
	{
		addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
	}
	else
	{
		addr.sin_addr.s_addr=htonl(INADDR_ANY);
	}
	int sock;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0))==-1)
	{
		logger.log(CAT_NET, L_ERR, "Error %d in socket(): %s\n", errno, strerror(errno));
		return 4;
	}
	int rc = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
	if (rc < 0)
	{
		logger.log(CAT_NET, L_ERR, "Error %d in bind(): %s\n", errno, strerror(errno));
		return 5;
	}
	rc = listen(sock, 10);
	if (rc < 0)
	{
		logger.log(CAT_NET, L_ERR, "Error %d in listen(): %s\n", errno, strerror(errno));
		return 6;
	}
	// Loading db driver
	init_dblib_paths();
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
		return 2;
	}
	dblib_paths.clear();
	// Searching for newDataBase()
	newDB = (PSQLDatabase (*)(void))dlsym(libh, "newDataBase");
	CLogger ** plog = (CLogger **) dlsym(libh, "plogger");
	*plog = &logger;

	// Creating CSQLDataBase object
	CSQLDatabase * database = newDB();
	if (database->Connect(DBHost.c_str(), DBUser.c_str(), DBPass.c_str(), DBName.c_str(), DBPort))
	{
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
	}
	else
	{
		logger.log(CAT_SQL, L_ERR, "ERROR: Can't connect to database. %s\n", database->DispError());
		return 3;
	}

	// Set Names BINARY to deal with MySQL 4.1+
        database->sql_query(database->m_sqlquery->SetQuery("SET NAMES 'binary'", NULL));
	
#ifndef UNICODE
	// Allow to use characters from local charset in query words
	InitTypeTable(Charsets[GetDefaultCharset()].wordch);
#endif
	cache.SetDb(database);
	// load LastMods
	if (last_mods.Init(DBWordDir.c_str()) <= 0)
	{
		logger.log(CAT_FILE, L_WARN, "No lastmod file - disabling lastmod-related functionality\n");
		use_lastmod = 0;
	}
	if (use_lastmod)
	{
		if (last_mods.Load() < 0)
		{
			logger.log(CAT_FILE, L_ERR, "Can't load lastmod - exiting\n");
			exit(1);
		}
	}

	BuildSiteWeights(database);
	// Initialize listening socket
	ranks.LoadRanks();
	logger.log(CAT_NET, L_INFO, "Started accepting queries on port %d\n", Port);
	CWorkerThreadList threads;
	threads.m_maxThreads = MaxThreads;
	// Process incoming requests
	while (doit_forever)
	{
		struct sockaddr_in cad;
		socklen_t cadlen=sizeof(cad);
		int csock = accept(sock, (struct sockaddr*)&cad, &cadlen);
		if (csock >= 0)
		{
			if (ipaccess.IsAllowed(cad.sin_addr))
			{
/*				int arg = 1;
				setsockopt(csock, SOL_SOCKET, SO_KEEPALIVE, (char *)&arg, sizeof(arg));
				// Set linger with zero timeout option on client socket
				struct linger sld = {0};	// socket linger definition
				sld.l_onoff = 1;
				sld.l_linger = 0;
				setsockopt(csock, SOL_SOCKET, SO_LINGER, (char *)&sld, sizeof(sld));
*/				// Start processing request
				threads.processReq(database, csock);
			}
			else
			{
				close(csock);
				logger.log(CAT_NET, L_INFO, "Access denied for client from %s\n", inet_ntoa(cad.sin_addr));
			}
		}
		else
		{
			logger.log(CAT_NET, L_ERR, "Error %d in accept(): %s\n", errno, strerror(errno));
		}
	} // while
	// Free memory
	delete database;
	dlclose(libh);
	return 0;
}


// Main function of daemon
void daemon(const char* config, int respawn)
{
	if (respawn)
	{
		while (true)
		{
			pid_t child = fork();
			if (child < 0)
			{
				logger.log(CAT_ALL, L_ERR, "Error %d in fork(): %s\n", errno, strerror(errno));
				return;
			}
			if (child)
			{
				int status;
				waitpid(child, &status, 0);
				if (WTERMSIG(status) == SIGSEGV)
				{
					time_t now;
					time(&now);
					logger.log(CAT_ALL, L_ERR, "SEGV occured in daemon at %s\n", ctime(&now));
				}
				else if (WTERMSIG(status) == 0)
					break;
			}
			else
			{
				int res = daemon_child(config);
				exit(res);
			}
		}
	}
	else
	{
		daemon_child(config);
	}
}

void start_daemon(const char* config, int respawn)
{
	string dir;
	if (!isAbsolutePath(logfile))
	{
		dir = DataDir + "/";
	}
	dir += logfile;
	logger.setcat( CAT_ALL );
	if (!(logger.openlogfile(dir.c_str())))
		fprintf(stderr, "WARNING: Can't open logfile %s: %s\n", dir.c_str(), strerror(errno));

	logger.log(CAT_ALL, L_INFO, "\nStarting search daemon from ASPseek v." VERSION "\n");
	close(0);
	close(1);
	close(2);
	pid_t p = fork();
	if (p == 0)
	{
		setsid();
		daemon(config, respawn);
	}
}

void PrintUrl(CSQLDatabase *db, ULONG urlID)
{
	CSrcDocument doc;
	if (db->GetDocument(urlID, doc, 1) && doc.m_content)
	{
		printf(doc.m_content);
	}
	else
	{
		logger.log(CAT_ALL, L_ERR, "Error: content not found\n");
	}
}

void usage()
{
	fprintf(stderr, "searchd from ASPseek v." VERSION "\nCopyright (C) 2000-2010 SWsoft, janforman.com\n\n");
	fprintf(stderr, "Usage: searchd [options]  [configfile]\n\n");
	fprintf(stderr, "Available options are:\n");
	fprintf(stderr, "  -D           Run as daemon\n");
	fprintf(stderr, "  -R           Respawn daemon in case of errors\n");
	fprintf(stderr, "  -F           Run in foreground, to use with process monitors\n");
	fprintf(stderr, "  -l logfile   Output log to logfile (default %s/" DEFAULT_SEARCHD_LOGFILE ")\n", DataDir.c_str());
	fprintf(stderr, "  -h           Print this help page\n");
}

int main(int argc, char** argv)
{
	int daemon = 0, opt;
	int foreground = 0;
	int respawn = 0;
	extern char *optarg;
	extern int optind;
	string config_name;
	const char* spellf = NULL;

	if ( (getuid() < MIN_ALLOWED_USER_ID) && (getgid() < MIN_ALLOWED_GROUP_ID) )
	{
		fprintf(stderr, "Don't run %s with privileged user/group ID (UID < %d, GID < %d)\n", argv[0], MIN_ALLOWED_USER_ID, MIN_ALLOWED_GROUP_ID);
		exit(1);
	}

	// Dirty hack to avoid non-threadsafeness of string class
	// We set ref to big value here so it will not reach 0
	// Works only with GNU libstdc++ STL!
	static long* ref = (long*)config_name.data() - 2;
	*ref = 0x40000000;

	while ((opt = getopt(argc, argv, "DRFhs:l:")) != -1)
	{
		switch (opt)
		{
		case 'D':
			daemon = 1;
			break;
		case 'R':
			respawn = 1;
			break;
		case 'F':
			foreground = 1;
			break;
		case 's':
			spellf = strdup(optarg);
			break;
		case 'l':
			logfile = strdup(optarg);
			break;
		case 'h':
		default:
			usage();
			return 1;
		}
	}
	if (daemon && foreground) {
		fprintf(stderr, "Options -D and -F are mutually exclusive; "
				"You should not use both.\n");
		exit(1);
	}

	if (daemon || foreground || spellf)
	{
		// Run daemon
		string dir;
		dir = argv[0];
		char* sl = strrchr(dir.c_str(), '/');
		if (sl)
		{
			*sl = 0;
			chdir(dir.c_str());
		}
		argc -= optind; argv += optind;
		if (argc == 1)
		{
			config_name = Trim(argv[0], " \t");
			if (isAbsolutePath(config_name.c_str()))
			{
				dir = config_name;
			}
			else
			{
				dir = ConfDir + "/" + config_name;
			}
		}
		else
		{
			dir = ConfDir + "/searchd.conf";
		}

		if (spellf)
		{
#ifdef UNICODE
			TestSpell(spellf, "utf-8");
#else
			TestSpell(spellf);
#endif
			return 0;
		}
		if (daemon) {
			start_daemon(dir.c_str(), respawn);
		}

		if (foreground) {
			daemon_child(dir.c_str());
		}
	}
	else // no parameters were specified
	{
		usage();
	}
	return 0;
}
