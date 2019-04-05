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

/*  $Id: sc.c,v 1.13 2002/09/16 02:42:38 matt Exp $
	Author : Ivan Gudym
    Based on original 'sc.cpp' developed by Alexander F. Avdonkin
	Uses parts of UdmSearch code
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>	/* for read() */
#include "aspseek.h"
#include "defines.h"
#include "paths.h"

char outheaders[STRSIZ] = "";
char *curhead = outheaders;
char content_type[STRSIZ] = "";
int status = 0;
int cach_ctrl = 0;
char query_string[8*1024] = ""; /* Apache allows up to 8190 bytes */
char script_name[STRSIZ] = "";
int query_cached = 0;	/* To avoid duplicated computation will set to 1 */
int script_cached = 0;	/* if the data is already in buffer. */
char *arg = NULL;	/* Command line argument, used in 'get_query',
			   should be set by 'main' before 'get_query()' call */


void sc_write(aspseek_request *r, char *buf, int size)
{
	fwrite(buf, size, 1, stdout);
}

void sc_set_content_type(aspseek_request *request, char *ct)
{
	safe_strcpy(content_type, ct);
}

void sc_set_status(aspseek_request *request, int st)
{
	status = st;
}

void sc_set_cach_ctrl(aspseek_request *request, int cach)
{
	cach_ctrl = cach;
}

void sc_add_header(aspseek_request *request, char *key, char *value)
{
	int n = STRSIZ - (curhead - outheaders) - 2;
	int k = strlen(key);
	int v = strlen(value);
	if (k+v+2 < n)
	{
		strcpy(curhead, key);
		curhead += k; *curhead++ = ':'; *curhead++ = ' ';
		strcpy(curhead, value);
		curhead += v; *curhead++ = '\r'; *curhead++ = '\n'; *curhead = '\0';
	}
}
void sc_send_headers(aspseek_request *request)
{
	char buf[32];
	if (content_type[0])
		sc_add_header(request, "Content-type", content_type);
	if (status)
	{
		sprintf(buf, "%d", status);
		sc_add_header(request, "Status", buf);
	}
	if (cach_ctrl)
		sc_add_header(request, "Cache-Control", "No-Cache");
	printf("%s\r\n", outheaders);
}

char *get_query()
{
	char *method, *env;

	if (query_cached)
	{
		return query_string;
	}
	else
	{
		if ((method = getenv("REQUEST_METHOD")))
		{
			if (!strcasecmp(method, "GET") && (env = getenv("QUERY_STRING")))
			{
				/* max. length of query, from Apache */
				if (strlen(env) > 8190)
					exit(1);	/* possible buffer overflow attack? */
				safe_strcpy(query_string, env);
			}
			else if (!strcasecmp(method, "POST"))
			{
				int n, r, i = 0;
				env = getenv("CONTENT_LENGTH");
				if (!env || !*env || (n = atoi(env)) <= 0)
				{
					/* suspect query, should really never be here */
					exit(1);
				}
				if (n > 8190)	/* keep limit consistent with GET, should never need to */
				{		/* exceed this in normal use anyway -matt */
					exit(1);
				}
				while ((i < n) && (r = read(0, query_string + i, n - i)) > 0)
				{
					i += r;
				}
				query_string[i] = '\0';
			}
			else
			{
				/* should really never be here -matt */
				exit(1);
			}
		}
		else
		{
			/* Executed from command line, or under server
			 * which do not pass an empty QUERY_STRING */
			if (arg)
				snprintf(query_string, sizeof(query_string),
					"q=%s", arg);
		}
		query_cached = 1;
		return query_string;
	}
}

char *get_script()
{
	char *env;

	if (script_cached)
	{
		return script_name;
	}
	else
	{
		if ((env = getenv("REDIRECT_STATUS")))
		{
			/* Check Apache internal redirect
			 * via "AddHandler" and "Action" */
			env = getenv("REDIRECT_URL");
			snprintf(script_name, sizeof(script_name),
				"%s", env ? env : "");
		}
		if (!script_name[0])
		{
			env = getenv("SCRIPT_NAME");
			snprintf(script_name, sizeof(script_name),
				"%s", env ? env : "/cgi-bin/s.cgi");
		}
		script_cached = 1;
		return script_name;
	}
}

char *sc_get_request_info(aspseek_request *request, request_info info)
{
	switch (info)
	{
		case RI_QUERY_STRING:
			return get_query();
			break;
		case RI_SCRIPT_NAME:
			return get_script();
			break;
		case RI_REQUEST_METHOD:
			return getenv("REQUEST_METHOD");
			break;
		case RI_REMOTE_ADDR:
			return getenv("REMOTE_ADDR");
		default:
			return NULL;
	}
}

char *sc_get_request_header(aspseek_request *request, char *header)
{
	if (!strcasecmp(header, "Cookie"))
	{
		return getenv("HTTP_COOKIE");
	}
	else if (!strcasecmp(header, "Forwarded"))
	{
		return getenv("HTTP_FORWARDED");
	}
	else if (!strcasecmp(header, "X_FORWARDED_FOR"))
	{
		return getenv("HTTP_X_FORWARDED_FOR");
	}
	else if (!strcasecmp(header, "Referer"))
	{
		return getenv("HTTP_REFERER");
	}
	else
		return NULL;
}

aspseek_client client = {
	sc_write,
	sc_set_content_type,
	sc_set_status,
	sc_set_cach_ctrl,
	sc_add_header,
	sc_send_headers,
	sc_get_request_info,
	sc_get_request_header,
	0
};

aspseek_site site = { &client };
aspseek_request request = { &site };

int main(int argc, char** argv)
{
	char templ[STRSIZ] = "";
	char *env;

	/* Don't forget to init 'arg' static var */
	arg = argc > 0 ? argv[1] : NULL;

	/* Determine template name */
	if ((env = getenv("ASPSEEK_TEMPLATE")))
	{
		safe_strcpy(templ, env);
	}
	/* for compatibility with UdmSearch */
	else if ((env = getenv("UDMSEARCH_TEMPLATE")))
	{
		safe_strcpy(templ, env);
	}
	/* also check Apache internal redirect via "AddHandler" and "Action" */
	else if (getenv("REDIRECT_STATUS") && (env = getenv("PATH_TRANSLATED")))
	{
		safe_strcpy(templ, env);
	}

	/* Try to determine script name */
	get_script();

	if (!templ[0])
	{
		/* Try to determine template name if still not known */
		/* As for template name, some.cgi should use some.htm */
		char sname[STRSIZ] = "";
		safe_strcpy(sname, get_script());
		if (sname[0])
		{
			/* we got name of CGI */
			char *s, *r;
			/* remove path */
			/* We do this FIRST since script_name
			 * may not have an extension! -matt */
			if (!(s = strrchr(sname, '/')))
				s = sname;
			if (s[0] == '/')
				s++;
			/* remove .ext */
			if ((r = strrchr(s, '.')))
				*r = '\0';
			snprintf(templ, sizeof(templ),
				"%s/%s.htm", CONF_DIR, s);
		}
		else
		{
			/* last resort */
			safe_strcpy(templ, CONF_DIR "/s.htm");
		}
	}

	aspseek_init(&client);

	aspseek_load_template(&site, templ);

	aspseek_process_query(&request);

/*	aspseek_free_template(&site); */

	return 0;
}
