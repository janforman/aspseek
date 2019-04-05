/* Copyright (C) 2000, 2001  SWsoft, Singapore
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

/*  $Id: templates.h,v 1.30 2003/05/06 12:56:20 kir Exp $
	Author: Alexander F. Avdonkin
	Uses parts of UdmSearch code
*/

#ifndef _TEMPLATE_H_
#define _TEMPLATE_H_

#include <stdio.h>
#include <string>
#include <vector>
// #include "my_hash_set"
#include "maps.h"
#include "defines.h"
#include "documents.h"
#include "url.h"
#include "protocol.h"
#include "aspseek.h"
#ifdef UNICODE
#include "ucharset.h"
#endif

class CStringSet;

typedef std::vector<std::string> CStringVector;
class CStringStringVector : public std::vector<CStringVector*>
{
public:
	~CStringStringVector()
	{
		for (iterator v = begin(); v != end(); v++)
		{
			if (*v) delete *v;
		}
	}
};

class CBufferedSocket;

class CTemplate;

class CSrcDocumentR : public CSrcDocument
{
public:
	void Recv(CBufferedSocket& socket, int MaxExcerpts);
};

class CCgiQueryC : public CCgiQuery
{
public:
	char m_r_str[STRSIZ];
	char m_ul_str[STRSIZ];

public:
	CCgiQueryC()
	{
		m_r_str[0] = 0;
		m_ul_str[0] = 0;
	}
	void FormQuery();
	void FormHrefs(CTemplate *tmpl, char* href, char* hrefwg);
	int ParseCgiQuery(char* query, char* templ);
};
/*
extern char self [STRSIZ];
extern char wordinfo[STRSIZ];
extern char href[STRSIZ];
extern char ask_error[BUFSIZ];
extern char hrefwg[STRSIZ];
//extern std::string query_form_escaped, qr_form_escaped, qr_url_escaped;
//extern std::string tmpl_url_escaped;
extern ULONG icolor;
*/
extern char defaultw[];

class CDataSource;

//int LoadTemplate(char *name, const char *query, char* default_where = defaultw);
//int PrintTemplate(CDataSource& database, char* Target, const char* where, const char *url, const char *content_type, const char *last_modified);
//int PrintTemplate(CDataSource& database, const char* where, const char *url, const char *content_type, const char *last_modified);
//int PrintOneTemplate(CDataSource& database, char* Target, const char *s, const char* where, const char *url, const char *content_type, const char *last_modified);
//int PrintOption(CDataSource& database, char* Target, const char* where, const char* option);
//void ClearTemplates();
char * UnescapeCGIQuery(char *d, char *s);
char* EscapeHtml(char *d, const char *s, char what);
char* EscapeURL(char *d, const char *s);
void HtmlSpecialChars(const char *str, char* p);
int InitRand();
int Match(const char* str, const char* pattern);
void PrepareHighlite();

void SendString(int socket, const char* str, int len);
void SendString(int socket, const char* str);
int RecvString(int socket, char* str);
int RecvString(int socket, std::string& str);
int RecvULONG(int socket, ULONG& val);
int RecvULONG(int socket, long& val);
void SendULONG(int socket, ULONG val);
void InitErrorMes();

void AddIE(std::string& qwords, std::string& is, std::string& xs);
std::string CombineIE(std::string& include, std::string& exclude,
		int iphrase, int xphrase);

/* All of this below is moved into CTemplate class
extern int pps, use_clones;
extern int found;
extern ULONG docnum, first, last;
//extern unsigned int format;
extern char *mode;
//extern CULONGSet spaces;
//extern int gr;
extern double ttime;
extern CUrlW* urlw;
extern int  size;
extern char title[MAXTITLESIZE * 5];
extern CStringVector texts;
extern char descr[MAXDESCSIZE * 5];
extern char keywords[MAXKEYWORDSIZE * 5];
extern int num_pages, navpage, is_next;
//extern std::string ul_unescaped;
extern const char *crc;

extern int Port;
//extern unsigned long DaemonAddress;
extern int MaxExcerpts;
extern ULONG MaxExcerptLen;

extern std::string LocalCharset;
*/


/* This too ;)
extern CCgiQueryC cgiQuery;

#ifdef UNICODE
extern CULONGVector* titleH;
extern CULONGVector* textH;
extern CULONGVector* descrH;
extern CULONGVector* keywordsH;
#endif
*/

typedef hash_map<std::string, CStringStringVector> CMapStringToStringVec;
typedef hash_map<std::string, std::string> CMapStringToString;

typedef class CTemplate {
	public:

	aspseek_request *request;
	int Port;
	CCgiQueryC cgiQuery;
	CMapStringToStringVec Templates;
	CMapStringToString Errormes;
	int LCharset;
	int ps;		///< Value from ResultsPerPage
	int pps;	///< Value from PagesPerScreen
	int use_clones;	///< Set to 0 if "Clones" is set to "no"
	int found;
	ULONG docnum;
	ULONG first;
	ULONG last;
	//unsigned int format = 0;
	double ttime;
	char *mode;
	//int gr = 1;
	int  size;
	//static int  rating=0;
	char title[MAXTITLESIZE * 5];
	CStringVector texts;
	char descr[MAXDESCSIZE * 5];
	char keywords[MAXKEYWORDSIZE * 5];

#ifdef UNICODE
	CULONGVector* titleH;
	CULONGVector* textH;
	CULONGVector* descrH;
	CULONGVector* keywordsH;
#endif

	int num_pages, navpage, is_next;
	//int np = 0;
	//std::string ul_unescaped;
	const char *crc;
	int MaxExcerpts;
	ULONG MaxExcerptLen;
	ULONG icolor;
	CStringStringVector* hcolors;

	/* Template variables */
	//std::string query_form_escaped;
	//std::string qr_form_escaped, qr_url_escaped;
	//std::string tmpl_url_escaped;
	char self [STRSIZ];
	char ask_error[BUFSIZ];
	char wordinfo[STRSIZ];
	char href[STRSIZ];
	char hrefwg[STRSIZ];
	char fullhref[STRSIZ];
	CUrlW* urlw;

	std::string LocalCharset;

#define MAXRANDOM 128
	int Randoms[MAXRANDOM];

// Class methods
	CTemplate();
	~CTemplate() {};
	void Clear();
	void ClearTemplates();
	void PrepareHighlite();
	void InitErrorMes();
	int InitRand();
	int LoadTemplate(char *name, const char *query, char* default_where = defaultw);
//	void GetHilightPos(const char *src, CStringSet& w_list, CCharsetB* charset, CULONGVector& positions);
	char* hilightcpy(CDataSource& database, char *dst, const char *src, CULONGVector& positions);
	char* hilightcpy(CDataSource& database, char *dst, char *src, CStringSet& w_list);
	int PrintTemplate(CDataSource& database, char* Target, const char* where, const char *url, const char *content_type, const char *last_modified);
	int PrintTemplate(CDataSource& database, const char* where, const char *url, const char *content_type, const char *last_modified);
	int PrintOneTemplate(CDataSource& database, char* Target, const char *s, const char* where, const char *url, const char *content_type, const char *last_modified);
	int PrintOption(CDataSource& database, char* Target, const char* where, const char* option);
	int PrintRadio(CDataSource& database, char* Target, const char* where, const char* option, const char* name);
};


#endif

