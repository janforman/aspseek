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

/*  $Id: protocol.h,v 1.5 2002/07/15 12:11:18 kir Exp $
	Author: Alexander F. Avdonkin
	Uses parts of UdmSearch code
*/

#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#include "sock.h"
#include <string>

using std::string;

#define DEFAULT_PORT	12345
#define MAX_PS		100
#define DEFAULT_PS	20

enum
{
	GET_SITE = 1,
	GET_DOCUMENT = 2,
	GET_CLONELIST = 3,
	ADD_STAT = 4,
	GET_ERROR = 5,
	GET_DOCUMENTS = 6
};

enum
{
	SEARCH_MODE_NONE,
	SEARCH_MODE_EXACT,
	SEARCH_MODE_FORMS
};

enum
{
	ACCOUNT_DISTANCE_UNK,
	ACCOUNT_DISTANCE_YES,
	ACCOUNT_DISTANCE_NO
};

enum
{
	SORT_BY_RATE,
	SORT_BY_DATE
};

class CCgiQuery
{
public:
	string m_query_words;
	string m_results;
	string m_cached;
	string m_include_words;
	string m_exclude_words;
	string m_includeSites;
	string m_excludeSites;
	string m_ul_unescaped;
	string m_tmplp;
	string m_tmpl_url_escaped;
	string m_query_url_escaped;
	string m_query_form_escaped;
	string m_qr_form_escaped;
	string m_qr_url_escaped;
	string m_charset;
	/// What languages to use in FindForms (comma-separated list).
	/// Passed in 'fm=' parameter
	string m_wordform_langs;
	int m_format;
	int m_np;
	int m_ps;
	int m_inres;
	int m_posmask;
	int m_take;
	int m_iph;
	int m_xph;
	int m_gr;
	int m_site;
	int m_search_mode;
	int m_sort_by;
	int m_accDist;
	time_t m_t_from;	///< time filter - from time
	time_t m_t_to;		///< time filter - to time
	CULONGSet m_spaces;
	char m_cspaces[300];

public:
	CCgiQuery()
	{
		m_posmask = 0;
		m_format = 0;
		m_inres = 0;
		m_take = 0;
		m_iph = 0;
		m_xph = 0;
		m_np = 0;
		m_ps = 0;
		m_gr = 1;
		m_site = 0;
		m_cspaces[0] = 0;
		m_search_mode = SEARCH_MODE_NONE;
		m_sort_by = SORT_BY_RATE;
		m_t_from = 0;
		m_t_to = 0;
		m_accDist = ACCOUNT_DISTANCE_UNK;
	}
 	void Clear(void)
 	{
 		m_query_words = "";
 		m_results = "";
 		m_cached = "";
 		m_include_words = "";
 		m_exclude_words = "";
 		m_includeSites = "";
 		m_excludeSites = "";
 		m_ul_unescaped = "";
 		m_tmplp = "";
 		m_tmpl_url_escaped = "";
 		m_query_url_escaped = "";
 		m_query_form_escaped = "";
 		m_qr_form_escaped = "";
 		m_qr_url_escaped = "";
 		m_charset = "";
 		m_wordform_langs = "";
 		m_format = 0;
 		m_np = 0;
 		m_ps = 0;
 		m_inres = 0;
 		m_posmask = 0;
 		m_take = 0;
 		m_iph = 0;
 		m_xph = 0;
 		m_gr = 1;
 		m_site = 0;
 		m_search_mode = SEARCH_MODE_NONE;
 		m_sort_by = SORT_BY_RATE;
 		m_accDist = ACCOUNT_DISTANCE_UNK;
 		m_t_from = 0;
 		m_t_to = 0;
 		m_cspaces[0] = 0;
		m_spaces.clear();
 	}
	void Send(CBufferedSocket& sock);
	int Recv(CBufferedSocket& sock);
	ULONG MakeQueryKey(char* query);
};


#endif
