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

/*  $Id: aspseek.cpp,v 1.11 2002/07/19 11:52:01 kir Exp $
    Author : Ivan Gudym
    Based on original 'sc.cpp' developed by Alexander F. Avdonkin
	Uses parts of UdmSearch code
*/

#include "defines.h"
#include "aspseek.h"
#include "stdarg.h"
#include <stdio.h>
#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "datasource.h"
#include "charsets.h"
#include "templates.h"
#include "logger.h"
#include "paths.h"
#include "misc.h" // for min() macro

int aspseek_init(aspseek_client *client)
{
	if (!client->configured)
	{
		InitCharset();
		client->configured = 1;
//		logger.setcat( CAT_ALL );
//		logger.setloglevel( "debug" );
//		logger.openlogfile( DATA_DIR "/aspseek.log" );
	}
	return 0;
}

int aspseek_load_template(aspseek_site *site, char *templ)
{
	if (!site->client->configured)
		aspseek_init(site->client);
	CTemplate *t = new CTemplate;
	t->InitRand();
	t->InitErrorMes();
	site->template_name = templ;
	if (t->LoadTemplate(site->template_name, ""))
	{
		delete t;
		site->templ = NULL;
		site->configured = 0;
		return 1;
	}
	else
	{
		site->templ  = (Template *)t;
		site->configured = 1;
		return 0;
	}
}

void aspseek_free_template(aspseek_site* site)
{
	delete (CTemplate*)site->templ;
}

char buf[STRSIZ] = "";
void aspseek_printf(aspseek_request *r, const char *fmt, ...)
{
	va_list args;
	int n;
	va_start(args, fmt);
	n = vsnprintf(buf, STRSIZ-1, fmt, args);
	r->site->client->write(r, buf, n);
	va_end(args);
}

static void set_contentf(aspseek_request *r, char *fmt, ...)
{
	va_list args;
	char buf[256];
	if (r->site->client->set_content_type )
	{
		va_start(args, fmt);
		vsnprintf(buf, 256, fmt, args);
		va_end(args);
		r->site->client->set_content_type(r, buf);
	}
}

static int connectDaemons(CTemplate *templ, CRemoteDataSource * &datasource)
{
	int con;
	if (daemons.size() <= 1)
	{
		// Single daemon
		int address, port;
		if (daemons.size() == 1)
		{
			// Use configuration domain parameters
			CDaemonSet::iterator it = daemons.begin();
			address = it->m_address;
			port = it->m_port;
		}
		else
		{
			// Find out local address
			struct utsname un;
			uname(&un);
			struct hostent* phe = gethostbyname(un.nodename);
			memcpy(&address, *phe->h_addr_list, sizeof(address));
			port = templ->Port;
		}
		// Create datasource and connect
		CTcpDataSource* d = new CTcpDataSource(templ);
		con = d->Connect(address, port);
		datasource = d;
	}
	else
	{
		// Multiple daemons
		CMultiTcpDataSource* d = new CMultiTcpDataSource(templ);
		con = d->Connect(daemons);
		datasource = d;
	}
	return con;
}

typedef struct preferences_struct {
	int prps;	// Results per page ?
			// That's all as for now, something else later ?
} preferences;

static void get_preferences_from_cookie(aspseek_request *r, preferences& pref)
{
	char *env, *lasttok, *token;
	aspseek_client *client = r->site->client;

	if (client->get_request_header &&
		(env = client->get_request_header(r, "COOKIE")))
	{
		// Parse cookie to find preferences
		token = GetToken(env, "; ", &lasttok);
		while (token)
		{
			if (STRNCMP(token, "PS=") == 0)
			{
				pref.prps = atoi(token + 3);
				if (pref.prps > MAX_PS) pref.prps = MAX_PS;
			}
			token = GetToken(NULL, "; ", &lasttok);
		}
	}
}


static int show_result(aspseek_request *r, struct timeval& stime,
	int ps1, CRemoteDataSource* datasource, ULONG *counts)
{
	CTemplate *templ = (CTemplate *)r->site->templ;
	CCgiQueryC &cgiQuery = templ->cgiQuery;
	aspseek_client *client = r->site->client;
	struct timeval etime;
	char sitename[130] = "";

	gettimeofday(&etime, NULL);
	if (counts[0])
	{
		// Not empty site set
		if (cgiQuery.m_gr)
		{
			// Turn off grouping if only 1 site is found
			datasource->SetGroup(counts[0]);
		}
		// Receive the rest of query result statistics
		datasource->GetCounts(counts + 1);
		templ->ttime = timedif(etime, stime);
		ULONG nFirst = (cgiQuery.m_np + 1) * ps1;
		// Allocate buffer for results and receive results themselves
		CUrlW* urls = (CUrlW*)alloca(counts[1] * sizeof(CUrlW));
		counts[1] = datasource->GetResults(counts[1], urls, cgiQuery.m_gr, nFirst);
		// Find out words to highlight
		datasource->GetWords(LightWords);
		// Collect words with symbols '*' and '?'
		LightWords.CollectPatterns();
		// Fill string with words info statistics
		LightWords.PrintWordInfo(templ->wordinfo);
		if (counts[0] <= 1)
		{
			cgiQuery.m_gr = 0;
			if (counts[0] == 1)
			{
				// Find out site name, if only 1 site is found
				datasource->GetSite(urls, sitename);
			}
		}
		CUrlW* urle = urls + counts[1];
		// Write query statistics to the database
		datasource->AddStat(cgiQuery.m_query_words.c_str(), cgiQuery.m_ul_unescaped.c_str(), cgiQuery.m_np, ps1,
			(int)counts[3], (int)counts[0], stime, etime, cgiQuery.m_cspaces, cgiQuery.m_site);
		if (cgiQuery.m_take == 0)
		{
			templ->first = ps1 * cgiQuery.m_np + 1;
			templ->docnum = 1;
			templ->last = min(nFirst, counts[2]);
			templ->num_pages = (counts[2] + ps1 - 1) / ps1;
			templ->is_next = templ->last < counts[2];
			templ->found = counts[2];
			// Build URL string for next result pages and for "more from site pages"
			cgiQuery.FormHrefs(templ, templ->href, templ->hrefwg);
			if (counts[4] & 1)
			{
				templ->PrintTemplate(*datasource, "complexPhrase", "", "", "");
			}
			if (counts[4] & 2)
			{
				templ->PrintTemplate(*datasource, "complexExpression", "", "", "");
			}
			// Print results top
			templ->PrintTemplate(*datasource, "restop", sitename, "", "");
			templ->urlw = urls;
			// Print each result
			while (templ->urlw < urle)
			{
				ULONG site = templ->urlw->m_siteID;
				int u = 0;
				// Print each url for site, if grouping by site is on
				for ( ; (templ->urlw < urle) && (site == templ->urlw->m_siteID) && ((u == 0) || cgiQuery.m_gr); templ->urlw++, u++)
				{
					// Skip results belonging to the first pages
					if (templ->docnum >= templ->first)
					{
						CSrcDocumentR doc;
						datasource->GetDocument(templ->urlw, doc);
//								HtmlSpecialChars(doc.m_title.c_str(), title);
						safe_strcpy(templ->title, doc.m_title.c_str());
						if (templ->title[0] == 0)
						{
							safe_strcpy(templ->title, "No title");
						}
						templ->texts.insert(templ->texts.begin(), doc.m_excerpts.begin(), doc.m_excerpts.end());
#ifdef UNICODE
						templ->textH = &doc.m_excerptsH;
#endif
						templ->size = doc.m_size;
						HtmlSpecialChars(doc.m_descr.c_str(), templ->descr);
						HtmlSpecialChars(doc.m_keywords.c_str(), templ->keywords);
						templ->crc = doc.m_crc.c_str();
#ifdef UNICODE
						templ->titleH = &doc.m_titleH;
						templ->descrH = &doc.m_descrH;
						templ->keywordsH = &doc.m_keywordsH;
#endif
						// Print result
						templ->PrintTemplate(*datasource, u == 0 ? "res" : "res2", doc.m_url.c_str(), doc.m_content_type.c_str(), doc.m_last_modified.c_str());
						templ->texts.clear();
					}
				}
				templ->docnum++;
			}
			// Print result bottom
			templ->PrintTemplate(*datasource, "resbot", "", "", "");
		}
		else
		{
			CSrcDocumentR doc;
			datasource->GetDocument(urls, doc);
			if (client->set_cache_ctrl)
				client->set_cache_ctrl(r, 1);
			if (client->set_status)
				client->set_status(r, 302);
			if (client->add_header)
				client->add_header(r, "Location",
					(char *)doc.m_url.c_str());
			return 1;
		}
	}
	else
	{
		// Add empty result statistics
		datasource->AddStat(cgiQuery.m_query_words.c_str(), cgiQuery.m_ul_unescaped.c_str(), cgiQuery.m_np, ps1, 0, 0,
			stime, etime, cgiQuery.m_cspaces, cgiQuery.m_site);
		if (cgiQuery.m_take)
		{
			if (client->send_headers)
				client->send_headers(r);
			templ->PrintTemplate(*datasource, "top", "", "", "");
		}
		// Print "not found" template
		templ->PrintTemplate(*datasource, "notfound", "", "", "");
	}
	return 0;
}

int aspseek_process_query(aspseek_request *r)
{
	char templ[STRSIZ] = "";
	char query_string[8*1024] = ""; // Apache allows up to 8190 bytes !
	int empty = 1; // query is empty until "q=" not found !
	char *env;
	struct timeval stime, etime;
	gettimeofday(&stime, NULL);
	preferences pref; pref.prps = 0;

	/* Check if all init stuff was done */
	if (!r || !r->site || !r->site->client) return 1;
	if (!r->site->configured) return 1;

	CTemplate *tmpl = (CTemplate *)r->site->templ;
	tmpl->request = r;
	CCgiQueryC &cgiQuery = tmpl->cgiQuery;
	aspseek_client *client = r->site->client;

	if (client->get_request_info &&
		(env = client->get_request_info(r, RI_QUERY_STRING)))
	{
		safe_strcpy(query_string, env);
	}

	get_preferences_from_cookie(r, pref);

	if (client->get_request_info)
		safe_strcpy(tmpl->self, client->get_request_info(r, RI_SCRIPT_NAME));

	/* Parse Query String */
	empty = cgiQuery.ParseCgiQuery(query_string, templ);
	if (cgiQuery.m_ps == 0) // ps= is not supplied in QUERY_STRING
	{
		if (pref.prps != 0) // get it from cookies
			cgiQuery.m_ps = pref.prps;
		else // get it from template's ResultsPerPage
			cgiQuery.m_ps = tmpl->ps;
	}

	// Output Content-Type if run under web server
	// Some servers do not pass QUERY_STRING if the query was empty,
	// so we check REQUEST_METHOD as well to be safe
	if (client->get_request_info(r, RI_REQUEST_METHOD)
		&& cgiQuery.m_cached.size() == 0)
	{
		// RFC 2068 states that default charset should be iso-8859-1
#ifdef UNICODE
		set_contentf(r, "text/html; charset=%s",
			(cgiQuery.m_charset.size()) ? cgiQuery.m_charset.c_str() : "utf-8");
#else
		set_contentf(r, "text/html; charset=%s",
			(tmpl->LocalCharset.size()) ? tmpl->LocalCharset.c_str() : "utf-8");
#endif
		if (cgiQuery.m_take == 0)
			if (client->send_headers)
				client->send_headers(r);
	}

	cgiQuery.FormQuery();
	// Print TOP and if no query BOTTOM

	CRemoteDataSource* datasource = NULL;
	if ((cgiQuery.m_cached.size() == 0) && ((cgiQuery.m_take == 0) || empty))
	{
#ifdef UNICODE
//		if (cgiQuery.m_charset.size())
//			aspseek_printf(r, "<META HTTP-EQUIV=\"Content-type\" CONTENT=\"text/html; charset=%s\">\n", cgiQuery.m_charset.c_str());
#else
//		if (tmpl->LocalCharset.size())
//			aspseek_printf(r, "<META HTTP-EQUIV=\"Content-type\" CONTENT=\"text/html; charset=%s\">\n", tmpl->LocalCharset.c_str());
#endif
		// Print top template only if not processing "cached" link
		tmpl->PrintTemplate(*datasource, "top", "", "", "");
		if (empty)
		{
			// Print bottom template if query parameter is not present
			tmpl->PrintTemplate(*datasource, "bottom", "", "", "");
			tmpl->Clear();
			return 0;
		}
	}

	int con = connectDaemons(tmpl, datasource);
	if (con == 0)
	{
		if (cgiQuery.m_cached.size())
		{
			// Print "cached" copy of content
			datasource->PrintCached(r, cgiQuery);
			datasource->Close();
			delete datasource;
			tmpl->Clear();
			return 0;
		}
		struct timeval tm;
		gettimeofday(&tm, NULL);
		int ps1 = cgiQuery.m_ps ? cgiQuery.m_ps : DEFAULT_PS;
		ULONG counts[5];
		// First, find site count, matching query
		int res = datasource->GetSiteCount(cgiQuery, counts);
		if (res == 0)
		{
			// No error in query occured
			if (show_result(r, stime, ps1, datasource, counts))
			{
				// Redirect - so send respond header
				client->send_headers(r);
				datasource->Quit();
				datasource->Close();
				delete datasource;
				tmpl->Clear();
				return 0;
			}
		}
		else
		{
			// Add error statistics
			gettimeofday(&etime, NULL);
			datasource->AddStat(cgiQuery.m_query_words.c_str(),
				cgiQuery.m_ul_unescaped.c_str(),
				cgiQuery.m_np, ps1, -1, 0, stime, etime,
				cgiQuery.m_cspaces, cgiQuery.m_site);
			if (cgiQuery.m_take)
			{
				if (client->send_headers)
					client->send_headers(r);
				tmpl->PrintTemplate(*datasource, "top", "", "", "");
			}
			// Print query error template
			datasource->GetError(tmpl->ask_error);
			tmpl->PrintTemplate(*datasource, "queryerror", "", "", "");
		}
		// Finish processing query
		datasource->Quit();
	}
	else // Can't connect to searchd
	{
		safe_strcpy(tmpl->ask_error, ER_NOSEARCHD);
		// Print error template
		tmpl->PrintTemplate(*datasource, "error", "", "", "");
	}
	// Print bottom
	tmpl->PrintTemplate(*datasource, "bottom", "", "", "");

	// Clean up
	datasource->Close();
	delete datasource;
	tmpl->Clear();

	return 0;
}
