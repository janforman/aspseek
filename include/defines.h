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

/*  $Id: defines.h,v 1.37 2003/04/04 06:01:48 kir Exp $
    Author : Alexander F. Avdonkin
	Uses parts of UdmSearch code
*/

#ifndef _DEFINES_H_
#define _DEFINES_H_

#include <limits.h>
#include <strings.h> /* for strncasecmp() */
#include "aspseek-cfg.h"

#if (SIZEOF_LONG_INT == 4)
typedef unsigned long int ULONG;
typedef signed long int LONG;
#elif (SIZEOF_INT == 4)
typedef unsigned int ULONG;
typedef signed int LONG;
#else
#error Neither 'long int' nor 'int' are 4-byte - please contact developers
#endif

#if (SIZEOF_SHORT_INT == 2)
typedef unsigned short int WORD;
#define WORD_MAX USHRT_MAX
#else
#error Size of 'short int' is not 2 - please contact developers
#endif

#if (SIZEOF_CHAR == 1)
typedef unsigned char BYTE;
#else
#error Size of 'char' is not 1 - please contact developers
#endif

typedef int BOOL;

/* LONG_MAX should be 2147483647 */
#define TIME_T_MAX LONG_MAX

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

#define true 1
#define false 0

#define SKIP(s,set)	while((*s) && (strchr(set, *s))) s++;
#define SKIPN(s,set) while((*s) && (!strchr(set, *s))) s++;
#define STRNCMP(x,y)	(strncmp(x,y,strlen(y)))
#define STRNCASECMP(x,y)	(strncasecmp(x,y,strlen(y)))
#define STRNCPY(x,y)	{strncpy(x,y,sizeof(x));x[sizeof(x)-1]=0;}


#define MAXKEYWORDSIZE		255
#define MAXDESCSIZE			100
#define DEFAULT_REINDEX_TIME	7*24*60*60
#define DEFAULT_MAX_HOPS		256
/* MaxDocsPerServer default value, -1 skips checks */
#define DEFAULT_SERVER_MAX_DOCS         -1
#define DEFAULT_REDIR_LOOP_LIMIT	8
#define READ_TIMEOUT		90
#define MAXNETERRORS		16

#define NOTFOUND	0
#define ALLOW	1
#define DISALLOW	2
#define HEAD	3

#define LOG_DEBUG	5

#define MAXTITLESIZE		128
#define MAXTEXTSIZE		255


#define NET_ERROR			-1
#define NET_TIMEOUT			-2
#define NET_CANT_CONNECT		-3
#define NET_CANT_RESOLVE		-4
#define NET_UNKNOWN			-5


#define STRSIZ	1024*5

#define MAXDOCSIZE			1024*1024 /* 1Mb */

#define DB_UNK			0
#define DB_MSQL			1
#define DB_MYSQL		2
#define DB_PGSQL		3
#define DB_SOLID		4
#define DB_ORACLE		5
#define DB_VIRT		6
#define DB_IBASE		7
#define DB_ORACLE8		8
#define DB_ORACLE7		9
#define DB_MSSQL		10
#define DB_FILES		100


#define FLAG_REINDEX	1
/*
#define FLAG_EXP_FIRST	2
*/
#define FLAG_ADD_SERV	4
#define FLAG_MARK		8
#define FLAG_SPELL		16
#define FLAG_INSERT		32
#define FLAG_SORT_HOPS	64
#define FLAG_INIT		128
/* #define FLAG_SKIP_LOCKING	256
 */
#define FLAG_DELTAS			512
#define FLAG_SUBSETS		1024
#define FLAG_TMPURL			2048
#define FLAG_CHECKFILTERS	4096
#define FLAG_LOCAL			8192

#define MIRROR_NO			-1
#define MIRROR_YES			0
#define MIRROR_NOT_FOUND		-1
#define MIRROR_EXPIRED		-2
#define MIRROR_CANT_BUILD		-3
#define MIRROR_CANT_OPEN		-4

/* HTTP status codes */
#define HTTP_STATUS_UNKNOWN		0
#define HTTP_STATUS_OK		200
#define HTTP_STATUS_REDIRECT	301
#define HTTP_STATUS_NOT_MODIFIED	304
#define HTTP_STATUS_DELETE		400
#define HTTP_STATUS_NOT_FOUND		404
#define HTTP_STATUS_RETRY		503
#define HTTP_STATUS_BAD_REQUEST	400
#define HTTP_STATUS_UNAVAIL		503
#define HTTP_STATUS_TIMEOUT		504
#define HTTP_STATUS_NOT_SUPPORTED	505

#define IND_UNKNOWN		0
#define IND_OK			1
#define IND_ERROR		2
#define IND_NO_TARGET		3
#define IND_TERMINATED		4

#define USER_AGENT		"ASPseek"
/* lower cased name of USER_AGENT */
#define USER_AGENT_LC		"aspseek"
#define URLSIZE		127

/* URLFile actions */
#define URL_FILE_REINDEX	1
#define URL_FILE_CLEAR	2
#define URL_FILE_INSERT	3
#define URL_FILE_PARSE	4

#ifndef NDEBUG
	#define ASSERT(x) assert(x)
#else
	#define ASSERT(x)
#endif

#define SLASH		'/'

#define MAX_WORD_LEN	32
/* MAX_URL_LEN is now in aspseek-cfg.h
 * #define MAX_URL_LEN	128 
 */
#define MAX_URL_DATA	2048
/* should be the same as urlwords.etag in etc/sql/tables.sql */
#define MAX_ETAG_LEN	48

#define ER_STOPWORDS		"ER_STOPWORDS"
#define ER_EXTRASYMBOL		"ER_EXTRASYMBOL"
#define ER_EMPTYQUERY		"ER_EMPTYQUERY"
#define ER_TOOSHORT		"ER_TOOSHORT"
#define ER_NOQUOTED		"ER_NOQUOTED"
#define ER_NOPARENTHESIS	"ER_NOPARENTHESIS"
#define ER_NOSEARCHD		"ER_NOSEARCHD"

#define NUM_HREF_DELTAS 4

#ifdef UNICODE
typedef WORD CHAR;
#define STRLEN(x) strwlen(x)
#define STRCPY(x, y) strwcpy(x, y)
#define STPCPY(x, y) stpwcpy(x, y)
#define STRCMP(x, y) strwcmp(x, y)
#define STPBWCPY(x, y) stpbwcpy(x, y)
#else
typedef char CHAR;
#define STRLEN(x) strlen(x)
#define STRCPY(x, y) strcpy(x, y)
#define STPCPY(x, y) stpcpy(x, y)
#define STRCMP(x, y) strcmp(x, y)
#define STPBWCPY(x, y) stpcpy(x, y)
#endif

#define TWO_CONNS

typedef struct
{
	ULONG m_offset;
	ULONG m_siteCount;
	ULONG m_urlCount;
	ULONG m_totalCount;
} WordInd;

typedef struct
{
	ULONG m_siteID;
	ULONG m_offset;
} SiteInd;

typedef struct
{
	ULONG m_urlID;
	WORD m_count;
} UrlInd;

#define safe_strcpy(d, s) \
	{ d[0]='\0'; \
	strncat(d, s, sizeof(d)-1); }

#define MIN_ALLOWED_USER_ID	11
#define MIN_ALLOWED_GROUP_ID	11

#endif
