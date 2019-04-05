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

/*  $Id: logger.h,v 1.17 2002/05/14 11:47:15 kir Exp $
    Author : Igor Sukhih
*/

#ifndef _LOGGER_H_
#define _LOGGER_H_

#include <stdio.h>	// for FILE
#include <pthread.h>

#define MAX_LOGBUFSIZE	2048
// Debug level
#define L_NONE		0
#define L_ERR		1
#define L_ERROR		L_ERR
#define L_WARNING	2
#define L_WARN		L_WARNING
#define L_INFO		3
#define L_DEBUG		4

// Debug category
#define CAT_SQL		2
#define CAT_NET		4
#define CAT_MEM 	8
#define CAT_FILE	16 // file-related operations
#define CAT_TIME	32 // for CTimer
#define CAT_ALL 0xffff

class CLogger
{
public:
	unsigned int m_catmask;
	int m_loglevel;
	FILE *m_logfile;
	pthread_mutex_t	m_mutex;
	char m_buf[MAX_LOGBUFSIZE];
public:
	CLogger()
	{
		m_loglevel = L_INFO;
		Init();
	}
	CLogger(int level)
	{
		m_loglevel = level;
		Init();
	}
	int getLevel(void)
	{
		return m_loglevel;
	}
	int getfd(void)
	{
		return fileno(m_logfile);
	}
	void Init();
	void setcat(unsigned int catmask)
	{
		m_catmask |= catmask;
	}
	virtual void log(unsigned int category, int loglevel, const char *format, ...);
	int openlogfile(const char *logname);
	void setloglevel(const char *levelstr);
	virtual ~CLogger(void);
};
extern CLogger logger;

void aspseek_log(unsigned int category, int loglevel, const char *format, ...);
void aspseek_setloglevel(const char *levelstr);

#endif /* _LOGGER_H_ */
