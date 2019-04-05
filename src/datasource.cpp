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

/*  $Id: datasource.cpp,v 1.27 2002/08/16 11:48:58 kir Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CTcpDataSource, CMultiTcpDataSource
*/
#include "aspseek-cfg.h"

#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <algorithm>
#include "datasource.h"
#include "templates.h"
#include "logger.h"
#include "misc.h" // for min() macro

using std::sort;
using std::partial_sort;

// Main thread function for distributed search. Run for each daemon
void* Search(void* p)
{
	CTcpContext* ctx = (CTcpContext*)p;
	CTcpDataSource dataSource(ctx->m_templ);
	ctx->m_source = &dataSource;
	int con = dataSource.Connect(ctx->m_address->m_address, ctx->m_address->m_port);
	CEvent& ev = ctx->m_parent->m_event;
	ev.Lock();
	if (ctx->m_parent->m_connected == 0)
	{
		// First connection to daemon, enable to send query for main thread
		if (con == 0) ctx->m_parent->m_connected = 1;
		ev.PostN();
	}
	ev.Unlock();
	CSearchParams* param = &ctx->m_param;
	while (true)
	{
		ctx->m_req.Wait();	// Wait command from main thread
		switch (ctx->m_command)	// Process command
		{
		case QUIT:
			ctx->m_answer.Post();
			return NULL;
		case GET_RESULT_COUNT:
			param->m_result = dataSource.GetResultCount(param->m_len, param->m_query, param->m_counts);
			ctx->m_answer.Post();
			break;
		case GET_SITE_COUNT:
			param->m_result = dataSource.GetSiteCount(param->m_len, param->m_query, param->m_counts);
			ctx->m_answer.Post();
			break;
		case GET_SITE_COUNTQ:
			param->m_result = dataSource.GetSiteCount(*param->m_cgiQuery, param->m_counts);
			ctx->m_answer.Post();
			break;
		case GET_COUNTS:
			param->m_result = dataSource.GetCounts(param->m_counts + 1);
			ctx->m_answer.Post();
			break;
		case GET_RESULTS:
			dataSource.GetResults(param->m_counts[1], param->m_res, 0, 0);
			ctx->m_answer.Post();
			break;
		case SET_GROUP:
			dataSource.SetGroup(param->m_counts[0], param->m_len);
			ctx->m_answer.Post();
			break;
		case GET_WORDS:
			dataSource.GetWords(param->m_words);
			ctx->m_answer.Post();
			break;
		case GET_DOCUMENTS1:
			dataSource.GetDocuments1(param->m_len, (ULONG*)param->m_res);
			break;
		}
	}
};

CTcpDataSource::~CTcpDataSource()
{
	Close();
}

void alrmh(int x)
{
	// do nothing
}

int CTcpDataSource::Connect(int address, int port)
{
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = address;
	if ((m_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		aspseek_log(CAT_NET, L_ERR, "Can't create socket: %s\n", strerror(errno));
		return 1;
	}
	// We want to ignore SIGARLM for the time of connect() call
	struct sigaction sigalrm, old_sigalrm;
	sigalrm.sa_handler = alrmh;
	sigalrm.sa_flags = 0;
	sigemptyset(&sigalrm.sa_mask);
	sigaction(SIGALRM, &sigalrm, &old_sigalrm);
	// Set connect() timeout to 2 seconds
	alarm(2);
	int res = connect(m_socket.m_socket, (struct sockaddr *)&sin, sizeof (sin));
	alarm(0);
	// Restore original signal handler
	sigaction(SIGALRM, &old_sigalrm, NULL);

	if (res < 0)
	{
		aspseek_log(CAT_NET, L_ERR, "Can't connect(): %s\n", strerror(errno));
		Close();
		return 1;
	}
	m_connected = 1;
	return 0;
}

int CTcpDataSource::Connected()
{
	return m_connected;
}

void CTcpDataSource::Close()
{
	m_socket.Close();
	m_connected = 0;
}

int CTcpDataSource::Recv(void* buf, int bytes, int flags)
{
	return m_socket.Recv(buf, bytes);
}


int CTcpDataSource::GetResultCount(int len, const char* query, ULONG* counts)
{
	ULONG r;
	if (Connected())
	{
		m_socket.SendString(query, len);	// Send query to daemon
		Recv(&r, sizeof(r));			// Receive result code
		if (r == 0)
		{
			Recv(counts, sizeof(ULONG));	// Receive site count
			if (counts[0])
			{
				// If result is not empty, receive the rest of query statistics info
				Recv(counts + 1, sizeof(ULONG) * 4);
			}
		}
		return r;
	}
	else
	{
		memset(counts, 0, 5 * sizeof(ULONG));
		return -2;
	}
}

int CTcpDataSource::GetSiteCount(int len, const char* query, ULONG* counts)
{
	ULONG r;
	if (Connected())
	{
		m_socket.SendString(query, len);	// Send query to the daemon
		Recv(&r, sizeof(r));			// Receive result code
		if (r == 0)
		{
			Recv(counts, sizeof(ULONG));	// If no error occured, receive site count
		}
		return r;
	}
	else
	{
		counts = 0;
		return -2;
	}
}

int CTcpDataSource::GetSiteCount(CCgiQuery& query, ULONG* counts)
{
	ULONG r;
	if (Connected())
	{
		query.Send(m_socket);
		Recv(&r, sizeof(r));			// Receive result code
		if (r == 0)
		{
			Recv(counts, sizeof(ULONG));	// If no error occured, receive site count
		}
		return r;
	}
	else
	{
		counts = 0;
		return -2;
	}
}

int CTcpDataSource::GetCounts(ULONG* counts)
{
	if (Connected())
	{
		Recv(counts, 4 * sizeof(ULONG));
		return 0;
	}
	else
	{
		memset(counts, 0, 4 * sizeof(ULONG));
		return -2;
	}
}

void CTcpDataSource::SetGroup(ULONG totalSiteCount)
{
	if (totalSiteCount == 1)
	{
		m_socket.SendULONG(0);	// Turn off grouping by sites, if only one site is found
	}
}

void CTcpDataSource::SetGroup(ULONG thisSiteCount, ULONG totalSiteCount)
{
	if (thisSiteCount == 1)
	{
		// Turn off grouping by sites for daemon, which returned 1 site, if only one site is found
		m_socket.SendULONG(totalSiteCount == 1 ? 0 : 1);
	}
}

ULONG CTcpDataSource::GetResults(ULONG cnt, CUrlW* res, int gr, ULONG nFirst)
{
	Recv(res, cnt * sizeof(CUrlW));
	return cnt;
}

int CTcpDataSource::GetSite(CUrlW* url, char* sitename)
{
	m_socket.SendULONG(GET_SITE);
	m_socket.SendULONG(url->m_siteID);
	return m_socket.RecvString(sitename);
}

void CTcpDataSource::GetDocument(CUrlW* url, CSrcDocumentR& doc)
{
	m_socket.SendULONG(GET_DOCUMENT);
	m_socket.SendULONG(url->m_urlID);
	m_socket.SendULONG(m_templ->MaxExcerpts);
	m_socket.SendULONG(m_templ->MaxExcerptLen);
	m_socket.SendULONG(url->m_weight);
	doc.Recv(m_socket, m_templ->MaxExcerpts);
}

void CTcpDataSource::GetCloneList(const char *crc, CUrlW* origin,
		CSrcDocVector &docs)
{
	m_socket.SendULONG(GET_CLONELIST);
	m_socket.SendULONG(origin->m_urlID);
	m_socket.SendULONG(origin->m_siteID);
	m_socket.SendString(crc);
	ULONG cnt;
	if ((m_socket.RecvULONG(cnt) == sizeof(cnt)) && (cnt > 0))
	{
		docs.resize(cnt);
		for (ULONG i = 0; i < cnt; i++)
		{
			CSrcDocumentR* doc = new CSrcDocumentR;
			doc->Recv(m_socket, m_templ->MaxExcerpts);
			docs[i] = doc;
		}
	}
}

void CTcpDataSource::GetWords(CStringSet& LightWords)
{
	ULONG sz;
	if (m_socket.RecvULONG(sz) == sizeof(sz) && (sz > 0))
	{
		for (ULONG i = 0; i < sz; i++)
		{
#ifdef UNICODE
			CUWord str;
			m_socket.RecvWString(str);
			CWordStat& stat = LightWords[str];
			m_socket.RecvString(stat.m_output);
#else
			string str;
			m_socket.RecvString(str);
			CWordStat& stat = LightWords[str];
#endif
			m_socket.RecvULONG(stat.m_urls);
			m_socket.RecvULONG(stat.m_total);
		}
	}
}

void CTcpDataSource::GetError(char* error)
{
	m_socket.SendULONG(GET_ERROR);
	m_socket.RecvString(error);
};

void CTcpDataSource::AddStat(const char *pquery, const char* ul, int np,
		int ps, int urls, int sites, timeval &start,
		timeval &finish, char* spaces, int site)
{
	aspseek_request *r = m_templ->request;
	aspseek_client *c = r->site->client;

	m_socket.SendULONG(ADD_STAT);
	m_socket.SendString(pquery);
	m_socket.SendString(ul);
	m_socket.SendULONG(np);
	m_socket.SendULONG(ps);
	m_socket.SendULONG(urls);
	m_socket.SendULONG(sites);
	m_socket.SendULONG(start.tv_sec);
	m_socket.SendULONG(start.tv_usec);
	m_socket.SendULONG(finish.tv_sec);
	m_socket.SendULONG(finish.tv_usec);
	m_socket.SendString(spaces);
	m_socket.SendULONG(site);
	char* proxy = c->get_request_info(r, RI_REMOTE_ADDR);
	char* referer = c->get_request_header(r, "REFERER");
	char* addr = c->get_request_header(r, "FORWARDED");
	if( ! addr ){
		addr = c->get_request_header(r, "X_FORWARDED_FOR");
	}
	m_socket.SendString(proxy ? proxy : "");
	m_socket.SendString(addr ? addr : "");
	m_socket.SendString(referer ? referer : "");
}

void CTcpDataSource::GetDocuments1(ULONG cnt, ULONG* urls)
{
	m_socket.SendULONG(GET_DOCUMENTS);
	m_socket.SendULONG(cnt);
	m_socket.Send(urls, cnt * sizeof(ULONG));
}

void CTcpDataSource::GetDocuments(CUrlW* url, CUrlW* urle)
{
	int cnt = urle - url;
	ULONG* urls = (ULONG*)alloca(cnt * sizeof(ULONG));
	for (int i = 0; i < cnt; i++)
	{
		urls[i] = url[i].m_urlID;
	}
	GetDocuments1(cnt, urls);
}

void CTcpDataSource::GetNextDocument(CUrlW* url, CSrcDocumentR& doc)
{
	doc.Recv(m_socket, m_templ->MaxExcerpts);
}

//char* scolors[] = {"ffff66", "ff66ff", "66ffff", "ff6666", "6666ff", "66ff66"};

#ifdef USE_OLD_STUFF
void CTcpDataSource::PrintCached(aspseek_request *r, int qlen, const char* query)
{

	if (Connected())
	{
		char buf[0x1001];
		int len;
		ULONG code;
		m_socket.SendString(query, qlen);
		if (m_socket.RecvULONG(code) == 0) return;
		if (code == 0)
		{
			ULONG ofsz, csz;
			ULONG ofc = 0;
			int txt = 0;
			char content_type[50];
			string url;
			if (m_socket.RecvULONG(ofsz) == 0) return;
			m_templ->PrepareHighlite();
			ULONG* offsets = NULL;
			ULONG* colors = NULL;
			ULONG* npos = NULL;
			CPosColor* pcolors = NULL;
			csz = ofsz >> 1;
			if (ofsz)
			{
				offsets = new ULONG[ofsz];
				colors = new ULONG[csz];
				m_socket.Recv(offsets, ofsz << 2);
				m_socket.Recv(colors, ofsz << 1);
				pcolors = new CPosColor[csz];
				for (ULONG i = 0; i < csz; i++)
				{
					pcolors[i].m_pos = i;
					pcolors[i].m_color = colors[i];
				}
				sort<CPosColor*>(pcolors, pcolors + csz);
				npos = new ULONG[ofsz];
				for (ULONG i = 0; i < csz; )
				{
					ULONG color = pcolors[i].m_color;
					ULONG ppos = 0;
					ULONG ppi = 0;
					ULONG pi = 0;
					while ((i < csz) && (color == pcolors[i].m_color))
					{
						pi = pcolors[i].m_pos;
						npos[pi << 1] = ppos;
						ULONG pos1 = pcolors[i].m_pos + 1;
						if (ppos)
						{
							npos[(ppi << 1) + 1] = pos1;
						}
						ppos = pos1;
						ppi = pi;
						i++;
					}
					npos[(pi << 1) + 1] = 0;
				}
			}
			m_socket.RecvString(url);
			m_socket.RecvString(content_type);
			// FIXME: We assume that everything that is not text/html is plain text
			txt = (STRNCASECMP(content_type, "text/html"));
			if (txt || (STRNCASECMP(content_type, "text/html") == 0))
			{
				r->site->client->set_content_type(r, "text/html");
				r->site->client->send_headers(r);
				aspseek_printf(r, "<BASE HREF=\"%s\">\n", url.c_str());
				if (m_templ->LocalCharset.size()) aspseek_printf(r, "<META HTTP-EQUIV=\"Content-type\" CONTENT=\"%s; charset=%s\">\n", content_type, m_templ->LocalCharset.c_str()/*charsetMap.CharsetsStr(lch)*/);
				m_templ->PrintTemplate(*this, "cachetop", url.c_str(), "", "");
				if (txt) aspseek_printf(r, "<html><body><pre>");
			}
			if (m_socket.RecvULONG(len) == sizeof(len))
			{
				ULONG off = 0;
				while (len > 0)
				{
					int read = m_socket.Recv(buf, min(len, (int)(sizeof(buf) - 1)));
					if (read > 0)
					{
						buf[read] = 0;
						ULONG poff = 0;
						while ((ofc < ofsz) && (off <= offsets[ofc]) && (off + read > offsets[ofc]))
						{
							ULONG ofh = ofc >> 1;
							m_templ->icolor = colors[ofh] - 1;
//							ULONG next = npos[ofc];
							r->site->client->write(r, buf + poff, offsets[ofc] - off - poff);
							if (ofc & 1)
							{
								m_templ->PrintTemplate(*this, "hiclose", url.c_str(), "", "");
//								printf("</B>");
//								if (next) printf("<A HREF=\"#%lu\">&gt</A>", next);
							}
							else
							{
//								printf("<A NAME=\"%lu\"></A>", ofh + 1);
//								if (next) printf("<A HREF=\"#%lu\">&lt</A>", next);
								m_templ->PrintTemplate(*this, "hiopen", url.c_str(), "", "");
//								printf("<B style=\"color:black;background-color:#%s\">",
//									scolors[icolor % (sizeof(scolors) / sizeof(scolors[0]))]);
							}
							poff = offsets[ofc] - off;
							ofc++;
						}
						r->site->client->write(r, buf + poff, read - poff);

						len -= read;
						off += read;
					}
					else
					{
						break;
					}
				}
			}
			if (txt) aspseek_printf(r, "</pre></body></html>");
			if (offsets) delete offsets;
			if (colors) delete colors;
			if (pcolors) delete pcolors;
			if (npos) delete npos;
		}
		else
		{
			r->site->client->set_content_type(r, "text/plain");
			r->site->client->send_headers(r);
			aspseek_printf(r, "URL not found");
		}
	}
}
#endif /* USE_OLD_STUFF */

void CTcpDataSource::PrintCached(aspseek_request *r, CCgiQuery& query)
{
	if (Connected())
	{
		char buf[0x1001];
		int len;
		ULONG code;
		query.Send(m_socket);
		if (m_socket.RecvULONG(code) == 0) return;
		if (code == 0)
		{
			ULONG ofsz, csz;
			ULONG ofc = 0;
			int txt = 0;
			char content_type[50];
			string url;
			if (m_socket.RecvULONG(ofsz) == 0) return;
			m_templ->PrepareHighlite();
			ULONG* offsets = NULL;
			ULONG* colors = NULL;
			ULONG* npos = NULL;
			CPosColor* pcolors = NULL;
			csz = ofsz >> 1;
			if (ofsz)
			{
				offsets = new ULONG[ofsz];
				colors = new ULONG[csz];
				m_socket.Recv(offsets, ofsz << 2);
				m_socket.Recv(colors, ofsz << 1);
				pcolors = new CPosColor[csz];
				for (ULONG i = 0; i < csz; i++)
				{
					pcolors[i].m_pos = i;
					pcolors[i].m_color = colors[i];
				}
				sort<CPosColor*>(pcolors, pcolors + csz);
				npos = new ULONG[ofsz];
				for (ULONG i = 0; i < csz; )
				{
					ULONG color = pcolors[i].m_color;
					ULONG ppos = 0;
					ULONG ppi = 0;
					ULONG pi = 0;
					while ((i < csz) && (color == pcolors[i].m_color))
					{
						pi = pcolors[i].m_pos;
						npos[pi << 1] = ppos;
						ULONG pos1 = pcolors[i].m_pos + 1;
						if (ppos)
						{
							npos[(ppi << 1) + 1] = pos1;
						}
						ppos = pos1;
						ppi = pi;
						i++;
					}
					npos[(pi << 1) + 1] = 0;
				}
			}
			m_socket.RecvString(url);
			m_socket.RecvString(content_type);
//			txt = (STRNCASECMP(content_type, "text/plain") == 0) || (STRNCASECMP(content_type, "text/css") == 0) || (STRNCASECMP(content_type, "text/tab-separated-values") == 0);
			// The above line is commented out because when
			// external converters are used content-type saved
			// is original, not converted. So we assume it is
			// text/plain for now, maybe later we will introduce
			// another field in SQL table.
			txt = (STRNCASECMP(content_type, "text/html"));
			if (txt || (STRNCASECMP(content_type, "text/html") == 0))
			{
				r->site->client->set_content_type(r, "text/html");
				r->site->client->send_headers(r);
				aspseek_printf(r, "<BASE HREF=\"%s\">\n", url.c_str());
#ifdef UNICODE
				if (query.m_charset.size()) printf("<META HTTP-EQUIV=\"Content-type\" CONTENT=\"text/html; charset=%s\">\n", query.m_charset.c_str());
#else
				if (m_templ->LocalCharset.size()) aspseek_printf(r, "<META HTTP-EQUIV=\"Content-type\" CONTENT=\"%s; charset=%s\">\n", content_type, m_templ->LocalCharset.c_str()/*charsetMap.CharsetsStr(lch)*/);
#endif
				m_templ->PrintTemplate(*this, "cachetop", url.c_str(), "", "");
				if (txt) aspseek_printf(r, "<html><body><pre>");
			}
			if (m_socket.RecvULONG(len) == sizeof(len))
			{
				ULONG off = 0;
				while (len > 0)
				{
					int read = m_socket.Recv(buf, min(len, (int)(sizeof(buf) - 1)));
					if (read > 0)
					{
						buf[read] = 0;
						ULONG poff = 0;
						while ((ofc < ofsz) && (off <= offsets[ofc]) && (off + read > offsets[ofc]))
						{
							ULONG ofh = ofc >> 1;
							m_templ->icolor = colors[ofh] - 1;
//							ULONG next = npos[ofc];
							r->site->client->write(r, buf + poff, offsets[ofc] - off - poff);
							if (ofc & 1)
							{
								m_templ->PrintTemplate(*this, "hiclose", url.c_str(), "", "");
//								printf("</B>");
//								if (next) printf("<A HREF=\"#%lu\">&gt</A>", next);
							}
							else
							{
//								printf("<A NAME=\"%lu\"></A>", ofh + 1);
//								if (next) printf("<A HREF=\"#%lu\">&lt</A>", next);
								m_templ->PrintTemplate(*this, "hiopen", url.c_str(), "", "");
//								printf("<B style=\"color:black;background-color:#%s\">",
//									scolors[icolor % (sizeof(scolors) / sizeof(scolors[0]))]);
							}
							poff = offsets[ofc] - off;
							ofc++;
						}
						r->site->client->write(r, buf + poff, read - poff);

						len -= read;
						off += read;
					}
					else
					{
						if ( read < 0 ){
							if (!txt && !(STRNCASECMP(content_type, "text/html") == 0)){
								r->site->client->set_content_type(r, "text/html");
								r->site->client->send_headers(r);
							}
							aspseek_printf(r, "Can't fetch cached copy<br>\n");
							aspseek_printf(r, "URL: '%s'<br>Content-Type: '%s'<br>\n", url.c_str(), content_type);
						}
						break;
					}
				}
			}
			if (txt) aspseek_printf(r, "</pre></body></html>");
			if (offsets) delete offsets;
			if (colors) delete colors;
			if (pcolors) delete pcolors;
			if (npos) delete npos;
		}
		else
		{
			r->site->client->set_content_type(r, "text/plain");
			r->site->client->send_headers(r);
			aspseek_printf(r, "URL not found\n");
		}
	}else{
		r->site->client->set_content_type(r, "text/plain");
		r->site->client->send_headers(r);
		aspseek_printf(r, "Can't connect to daemon\n");
	}
}

CTcpContext::CTcpContext(CMultiTcpDataSource* parent)
{
	m_address = NULL;
	m_source = NULL;
	m_parent = parent;
	m_templ = parent->m_templ;
}

int CMultiTcpDataSource::GetSiteCount(int len, const char* query, ULONG* count)
{
	vector<CTcpContext *>::iterator it;
	// Send request to each thread
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		(*it)->m_command = GET_SITE_COUNT;
		(*it)->m_param.m_len = len;
		(*it)->m_param.m_query = query;
		(*it)->m_req.Post();
	}
	// Wait each thread to finish
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		(*it)->m_answer.Wait();
	}
	*count = 0;
	int res = -2;
	// Sum sites count returned by all daemons
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		CSearchParams* params = &(*it)->m_param;
		if (params->m_result == 0)
		{
			*count += params->m_counts[0];
			res = 0;
		}
	}
	return res;
}

int CMultiTcpDataSource::GetSiteCount(CCgiQuery& query, ULONG* count)
{
	vector<CTcpContext *>::iterator it;
	// Send request to each thread
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		(*it)->m_command = GET_SITE_COUNTQ;
		(*it)->m_param.m_cgiQuery = &query;
		(*it)->m_req.Post();
	}
	// Wait each thread to finish
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		(*it)->m_answer.Wait();
	}
	*count = 0;
	int res = -2;
	// Sum sites count returned by all daemons
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		CSearchParams* params = &(*it)->m_param;
		if (params->m_result == 0)
		{
			*count += params->m_counts[0];
			res = 0;
		}
	}
	return res;
}

int CMultiTcpDataSource::GetCounts(ULONG* counts)
{
	vector<CTcpContext *>::iterator it;
	// Send request to each thread
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		if ((*it)->m_param.m_counts[0])
		{
			(*it)->m_command = GET_COUNTS;
			(*it)->m_req.Post();
		}
		else
		{
			memset((*it)->m_param.m_counts + 1, 0, 4 * sizeof(ULONG));
		}
	}
	// Wait for each thread to finish
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		if ((*it)->m_param.m_counts[0])
		{
			(*it)->m_answer.Wait();
		}
	}
	memset(counts, 0, 4 * sizeof(ULONG));
	int res = -2;
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		CSearchParams* params = &(*it)->m_param;
		if ((params->m_result == 0) && (params->m_counts[0]))
		{
			// Sum stats returned by each daemon
			for (int i = 0; i < 3; i++)
			{
				counts[i] += params->m_counts[i + 1];
			}
			counts[3] |= params->m_counts[4];	// Combine by OR query comlexity info
			res = 0;
		}
	}
	return res;
}

void CMultiTcpDataSource::SetGroup(ULONG totalSiteCount)
{
	vector<CTcpContext *>::iterator it;
	// Send request to each thread, which returned one site
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		if ((*it)->m_param.m_counts[0] == 1)
		{
			(*it)->m_command = SET_GROUP;
			(*it)->m_param.m_len = totalSiteCount;
			(*it)->m_req.Post();
		}
	}
	// Send each thread, which returned one site
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		if ((*it)->m_param.m_counts[0] == 1)
		{
			(*it)->m_answer.Wait();
		}
	}
}

void CMultiTcpDataSource::GetDocuments(CUrlW* url, CUrlW* urle)
{
	int cnt = urle - url;
	CPUrlWM* urlm = (CPUrlWM*)alloca(cnt * sizeof(CPUrlWM));
	CPUrlWM* urlme = urlm + cnt;
	for (int i = 0; i < cnt; i++)
	{
		urlm[i].m_purlw = url + i;
	}
	sort<CPUrlWM*>(urlm, urlme);
	ULONG* counts = (ULONG*)alloca(m_sources.size() * sizeof(ULONG));
	memset(counts, 0, m_sources.size() * sizeof(ULONG));
	ULONG* urls = (ULONG*)alloca(cnt * sizeof(ULONG));
	ULONG** purls = (ULONG**)alloca(m_sources.size() * sizeof(ULONG*));
	ULONG* urlsp = urls;
	CPUrlWM* purl = urlm;
	while (purl < urlme)
	{
		ULONG i = purl->m_purlw->m_count >> 24;
		purls[i] = urlsp;
		ULONG* urlsp1 = urlsp;
		while ((i == (purl->m_purlw->m_count >> 24)) && (purl < urlme))
		{
			*urlsp++ = purl->m_purlw->m_urlID;
			purl++;
		}
		counts[i] = urlsp - urlsp1;
	}
	int i = 0;
	vector<CTcpContext *>::iterator it;
	for (it = m_sources.begin(); it != m_sources.end(); it++, i++)
	{
		if (counts[i])
		{
			(*it)->m_command = GET_DOCUMENTS1;
			(*it)->m_param.m_len = counts[i];
			(*it)->m_param.m_res = (CUrlW*)purls[i];
			(*it)->m_req.Post();
		}
	}
}

void CMultiTcpDataSource::GetNextDocument(CUrlW* url, CSrcDocumentR& doc)
{
	ULONG i = url->m_count >> 24;
	if (i < m_sources.size())
	{
		m_sources[i]->m_source->GetNextDocument(url, doc);
	}
}

int CMultiTcpDataSource::Connect(CDaemonSet& daemons)
{
	int threads = 0;
	// Process all daemons
	for (CDaemonSet::iterator it = daemons.begin(); it != daemons.end(); it++)
	{
		pthread_t thread;
		CDaemonAddress* addr = const_cast<CDaemonAddress*>(&(*it));
		CTcpContext* ctx = new CTcpContext(this);	// Create thread context
		ctx->m_address = addr;
		m_sources.push_back(ctx);
		pthread_create(&thread, NULL, Search, ctx);	// Create thread for connection
		threads++;
	}
	while (threads)
	{
		m_event.Wait();
		if (m_connected) return 0;	// First connection occured
		threads--;			// Timeout occured
	}
	return -1;				// Could'n connect to any daemon
}

int CMultiTcpDataSource::GetResultCount(int len, const char* query, ULONG* counts)
{
	vector<CTcpContext *>::iterator it;
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		(*it)->m_command = GET_RESULT_COUNT;
		(*it)->m_param.m_len = len;
		(*it)->m_param.m_query = query;
		(*it)->m_req.Post();
	}
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		(*it)->m_answer.Wait();
	}
	memset(counts, 0, 5 * sizeof(ULONG));
	int res = -2;
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		CSearchParams* params = &(*it)->m_param;
		if (params->m_result == 0)
		{
			if (params->m_counts[0])
			{
				for (int i = 0; i < 5; i++)
				{
					counts[i] += params->m_counts[i];
				}
			}
			res = 0;
		}
	}
	return res;
}

ULONG CMultiTcpDataSource::GetResults(ULONG count, CUrlW* res, int gr, ULONG nFirst)
{
	vector<CTcpContext *>::iterator it;
	// Compute total result count for all daemons
	int cnt = 0;
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		cnt += (*it)->m_param.m_counts[1];
	}
	// Allocete result buffers
	CUrlW* tres = (CUrlW*)alloca(cnt * sizeof(CUrlW));
	CPUrlW* pr = (CPUrlW*)alloca(cnt * sizeof(CPUrlW));
	CUrlW* pres = tres;
	// Send request for threads, which returned not empty results
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		ULONG resc = (*it)->m_param.m_counts[1];
		if (resc)
		{
			(*it)->m_command = GET_RESULTS;
			(*it)->m_param.m_res = pres;
			pres += resc;
			(*it)->m_req.Post();
		}
	}
	CUrlW* eres = pres;
	pres = tres;
	CPUrlW* ppr = pr;
	ULONG i = 0;
	// Wait each thread to finish request
	for (it = m_sources.begin(); it != m_sources.end(); it++, i++)
	{
		ULONG resc = (*it)->m_param.m_counts[1];
		if (resc)
		{
			(*it)->m_answer.Wait();
			ULONG s = 0;
			for (ULONG u = resc; u > 0; u--, pres++)
			{
				pres->m_count |= (i << 24);	// Set daemon ordinal number for subsequent calls
				if ((pres->m_siteID != s) || (gr == 0))
				{
					// Collect only first result for each site if grouping by site is on
					ppr->m_purlw = pres;
					ppr++;
					s = pres->m_siteID;
				}
			}
		}
	}
	if ((int)nFirst > ppr - pr) nFirst = ppr - pr;
	partial_sort<CPUrlW*>(pr, pr + nFirst, ppr);	// Partially sort to find results with highest ranks
	ppr = pr;
	pres = res;
	// Fill final result buffer
	for (i = 0; i < nFirst; i++)
	{
		CUrlW* r = ppr->m_purlw;
		*pres++ = *r;
		ULONG site = r->m_siteID;
		ppr++;
		r++;
		if (gr)
		{
			// Copy results from the same site
			while ((r < eres) && (r->m_siteID == site))
			{
				*pres++ = *r;
				r++;
			}
		}
	}
	return pres - res;
}

void CMultiTcpDataSource::GetDocument(CUrlW* url, CSrcDocumentR& doc)
{
	ULONG i = url->m_count >> 24;	// Ordinal number of daemon, set in GetResults
	if (i < m_sources.size())
	{
		m_sources[i]->m_source->GetDocument(url, doc);
	}
}

void CMultiTcpDataSource::AddStat(const char *pquery, const char* ul, int np, int ps, int urls, int sites, timeval &start, timeval &finish, char* spaces, int site)
{
	if (m_sources.size() > 0)	// Store statistics at first database
	{
		m_sources[0]->m_source->AddStat(pquery, ul, np, ps, urls, sites, start, finish, spaces, site);
	}
}

int CMultiTcpDataSource::GetSite(CUrlW* url, char* sitename)
{
	ULONG i = url->m_count >> 24;	// Ordinal number of daemon, set in GetResults
	if (i < m_sources.size())
	{
		m_sources[i]->m_source->GetSite(url, sitename);
	}
	return 1;
}

void CMultiTcpDataSource::GetCloneList(const char *crc, CUrlW* origin, CSrcDocVector &docs)
{
	ULONG i = origin->m_count >> 24;	// Ordinal number of daemon, set in GetResults
	if (i < m_sources.size())
	{
		m_sources[i]->m_source->GetCloneList(crc, origin, docs);
	}
}

void CMultiTcpDataSource::GetWords(CStringSet& LightWords)
{
	vector<CTcpContext *>::iterator it;
	// Send request for threads, which returned not empty results
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		ULONG resc = (*it)->m_param.m_counts[0];
		if (resc)
		{
			(*it)->m_command = GET_WORDS;
			(*it)->m_req.Post();
		}
	}
	// Wait each thread to finish request
	for (it = m_sources.begin(); it != m_sources.end(); it++)
	{
		ULONG resc = (*it)->m_param.m_counts[0];
		if (resc)
		{
			// Combine word information statistics for each daemon
			(*it)->m_answer.Wait();
			CStringSet& w = (*it)->m_param.m_words;
			for (CStringSet::iterator i = w.begin(); i != w.end(); i++)
			{
				CWordStat& sd = LightWords[i->first];
				CWordStat& sr = i->second;
				sd.m_total += sr.m_total;
				sd.m_urls += sr.m_urls;
			}
		}
	}
}

void CMultiTcpDataSource::GetError(char* error)
{
	if (m_sources.size() > 0)	// Get error message from first daemon
	{
		m_sources[0]->m_source->GetError(error);
	}
}

void CMultiTcpDataSource::Quit()
{
	// Quit all threads
	for (vector<CTcpContext *>::iterator it = m_sources.begin(); it != m_sources.end(); it++)
	{
		(*it)->m_source->Quit();
	}
}

void CMultiTcpDataSource::Close()
{
	// Close all sockets
	for (vector<CTcpContext *>::iterator it = m_sources.begin(); it != m_sources.end(); it++)
	{
		(*it)->m_source->Close();
	}
}

CMultiTcpDataSource::~CMultiTcpDataSource()
{
	for (vector<CTcpContext *>::iterator it = m_sources.begin(); it != m_sources.end(); it++)
	{
		if (*it)
		{
			(*it)->m_command = QUIT;
			(*it)->m_req.Post();
			(*it)->m_answer.Wait();
			delete *it;
		}
	}
}

