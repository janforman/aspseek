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

/*  $Id: aspseek.h,v 1.5 2002/05/14 11:47:15 kir Exp $
    Author : Ivan Gudym
*/


#ifndef _ASPSEEK_H
#define _ASPSEEK_H

#ifdef __cplusplus
extern "C" {
#endif

struct aspseek_client_struct;
/* Declare template as void* now, actually it is class, but who cares here ;)*/
typedef void *Template;

/* 'Site' can mean anything - from per directory config in Apache to main
	server config - something, that can have their own template
*/
typedef struct aspseek_site_struct{
	struct aspseek_client_struct *client;
	char *template_name;
	Template *templ;
	int configured; /* =1 if all config and init staff was successully done */
	void *sitedata; /* Pointer to user defined data, specific to site */
} aspseek_site;

/* Structure represents particular request */
typedef struct aspseek_request_struct{
	aspseek_site *site;
	void *requestdata;	/* Pointer to user defined data, specific to request */
} aspseek_request;

typedef enum {
	RI_QUERY_STRING,
	RI_SCRIPT_NAME,
	RI_REQUEST_METHOD,
	RI_REMOTE_ADDR
} request_info;

typedef void aspseek_write(aspseek_request *request, char *buf, int size);
typedef void aspseek_set_content_type(aspseek_request *request, char *content_type);
typedef void aspseek_set_status(aspseek_request *request, int status);
typedef void aspseek_set_cache_ctrl(aspseek_request *request, int cache);
typedef void aspseek_add_header(aspseek_request *request, char *key, char *value);
typedef void aspseek_send_headers(aspseek_request *request);
typedef char *aspseek_get_request_info(aspseek_request *request, request_info info);
typedef char *aspseek_get_request_header(aspseek_request *request, char *header);

/* Client should allocate and fill this structure with functions references */
typedef struct aspseek_client_struct {
	aspseek_write *write;
	aspseek_set_content_type *set_content_type;
	aspseek_set_status *set_status;
	aspseek_set_cache_ctrl *set_cache_ctrl;	/* =1 if 'Cache-control: No-cache' needed */
	aspseek_add_header *add_header;
	aspseek_send_headers *send_headers;
	aspseek_get_request_info *get_request_info;
	aspseek_get_request_header *get_request_header;
	int configured; /* Set to 1 by aspseek_init, checked by other API func */
	void *clientdata; /* Pointer to user defined data, specific to client */
} aspseek_client;

/* Helper function, works through 'aspseek_write' function */
void aspseek_printf(aspseek_request *r, const char *fmt, ...);

/*************************************************************************
  Base aspseek API functions. All returns non-zero on error condition
*************************************************************************/

/* init should be called prior to any aspseek API using */
int aspseek_init(aspseek_client *client);

/*  load_template can be called ones at startup,
    then can be used in all subsequent query processing */
int aspseek_load_template(aspseek_site *site, char *templ);

void aspseek_free_template(aspseek_site *site);

/* process_query produces actual search result output */
int aspseek_process_query(aspseek_request *request);

#ifdef __cplusplus
}
#endif

#endif
