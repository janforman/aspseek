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

/*  $Id: logger.cpp,v 1.19 2002/01/09 11:50:15 kir Exp $
    Author : Igor Sukhih
	Implemented classes: CLogger
*/

#include <stdio.h>
#include <string>
#include <pthread.h>
#include <stdarg.h>
#include <pthread.h>

#include "logger.h"
#include "daemon.h"

CLogger logger(L_INFO);

void aspseek_log(unsigned int category, int loglevel, const char *format, ...){
char buf[4096];
	va_list args;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf)-1, format, args);
	va_end(args);
	logger.log(category, loglevel, "%s", buf);
}

void aspseek_setloglevel(const char *levelstr){
	logger.setloglevel(levelstr);
}

void CLogger::Init()
{
	m_catmask = CAT_ALL;
	m_logfile = stderr;
	pthread_mutex_init(&m_mutex, NULL);
}


void CLogger::log(unsigned int category, int loglevel, const char *format, ...)
{
	if (!(m_catmask & category) || (loglevel > m_loglevel))
	{
		return;
	}

	va_list args;

	CLocker lock(&m_mutex);

	va_start(args, format);
	vsnprintf(m_buf, MAX_LOGBUFSIZE, format, args);
	fprintf(m_logfile, "%s", m_buf);
	va_end(args);
}

CLogger::~CLogger(void)
{
	if (m_logfile)
		fclose(m_logfile);
	pthread_mutex_destroy(&m_mutex);
}

int CLogger::openlogfile(const char* logname)
{
	FILE *logfile;

	if ((logfile = fopen(logname, "a+")))
	{
		m_logfile = logfile;
		setvbuf(m_logfile, NULL, _IOLBF, 0);
	}
/*	else
	{
		m_logfile=stderr;
	}
*/
	return logfile != NULL;
}

void CLogger::setloglevel(const char *levelstr)
{
	if (!STRNCASECMP(levelstr, "none"))
		m_loglevel = L_NONE;
	else if (!STRNCASECMP(levelstr, "error"))
		m_loglevel = L_ERR;
	else if (!STRNCASECMP(levelstr, "warning"))
		m_loglevel = L_WARNING;
	else if (!STRNCASECMP(levelstr, "info"))
		m_loglevel = L_INFO;
	else if (!STRNCASECMP(levelstr, "debug"))
		m_loglevel = L_DEBUG;
	else
		m_loglevel = L_NONE;
}
