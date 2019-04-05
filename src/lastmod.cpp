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

/*  $Id: lastmod.cpp,v 1.19 2002/05/11 15:27:27 kir Exp $
 *  Author: Kir Kolyshkin
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>	// for ftruncate
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "sqldb.h"
#include "lastmod.h"
#include "logger.h"
#include "misc.h"	// for fsize
#include "daemon.h"

/* Format of lastmod file:
 * Offset  Size  Value     Comment
 * ------  ----  --------  -------
 * 0       4     version   Increments each time lastmod file is saved
 * 4       4     our_epoch Offset from UNIX epoch in seconds
 * 8       2     time0     Last-Modified time for URL with id==0 (should be 0)
 * 10      2     time1          --//--            URL with id==0
 * ....................... Till the end of file
 * FIXME: should we add some CRC checksum or so?
 */

int CLastMods::Init(const char *path)
{
	string fn(path);
	fn.append(IncrementalCitations ? "/lastmodd" : "/lastmod");
	m_f = open(fn.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP);
	if (m_f < 0)
	{
		logger.log(CAT_FILE, L_WARN, "Can't open file %s: %s\n", fn.c_str(), strerror(errno));
		return -1; // error
	}
	// read version
	// if we can't read that means it's a new file, so m_ver will be 0
	read(m_f, &m_ver, sizeof(m_ver));
	if (m_ver == 0)
	{
//		logger.log(CAT_FILE, L_WARN, "No lastmod data found in %s\n", fn.c_str());
		return 0; // error
	}
	return 1; // all ok
}

int CLastMods::Load(void){
	if (m_f < 0)
		return -1;
	// just in case
	clear();
	// locks the file, if it's locked then just fail - searchd can't wait
	CFileLocker fl;
	if (fl.LockRead(m_f) < 0)
	{
		logger.log(CAT_FILE, L_DEBUG, "Lastmod file is locked - not loading\n");
		return -1; // we'll load it next time ;)
	}
	size_t tnum = 1024*4; // 4K values, so 8Kbytes
	size_t trnum;
	WORD* t = new WORD[tnum];
	size_t header_size = sizeof(m_ver) + sizeof(m_our_epoch);
	size_t s = fsize(m_f);
	if (s==0)
		return -1; // error
	// skip the version - it has been read in Init() or ReLoad()
	lseek(m_f, sizeof(m_ver), SEEK_SET);
	// read m_our_epoch
	read(m_f, &m_our_epoch, sizeof(m_our_epoch));
	// reserve memory for data
	reserve( (s - header_size) / sizeof(WORD) );
	// read data
	while ( (trnum = read(m_f, t, sizeof(WORD) * tnum)) > 0 )
	{
		trnum /= sizeof(WORD);
		for (size_t j = 0; j < trnum; j++)
		{
			push_back(t[j]);
		}
	}
	delete t;
	logger.log(CAT_ALL, L_INFO, "Loaded %lu values from lastmod file v.%d\n", size(), m_ver);
	return 1;
}

int CLastMods::Save(void)
{
	if (m_f < 0)
		return -1; // error
	ULONG max=maxID()+1;
	if (!max)
		return 0; // nothing to save
	// Lock file, if it's locked (by searchd) we can wait
	CFileLocker fl;
	fl.LockWait(m_f);
	lseek(m_f, 0, SEEK_SET);
	// truncate the file just in case
	ftruncate(m_f, 0);
	m_ver++;
//	logger.log(CAT_FILE, L_INFO, "Saving lastmod file v.%lu ...", m_ver);
	if (write(m_f, &m_ver, sizeof(m_ver)) != sizeof(m_ver))
	{
		logger.log(CAT_FILE, L_ERR, "Can't write to lastmod file: %s\n", strerror(errno));
		return -1; // error
	}
	write(m_f, &m_our_epoch, sizeof(m_our_epoch));
	WORD* t = new WORD[max];
	ssize_t data_size = sizeof(WORD) * max;
	memset((void *)t, 0, data_size);
	for (ULONG i = 0; i < max; i++)
		t[i]=(*this)[i];
	if (write(m_f, t, data_size) != data_size)
	{
		delete t;
		logger.log(CAT_ALL, L_ERROR, "Can't save lastmod file: %s\n", strerror(errno));
		return -1; // error
	}
	delete t;
//	logger.log(CAT_FILE, L_INFO, " done (%lu values)\n", ++max);
	return 1; // saved
}

int CLastMods::ReLoad(void)
{
	if (m_f >= 0)
	{
		CLocker lock(&m_mutex);
		ULONG ver;
		// position to file beginning
		if (lseek(m_f, 0, SEEK_SET) != 0)
			return -1; // error
		// read version
		if ( read(m_f, &ver, sizeof(ver)) == sizeof(ver) )
		{
			if (ver != m_ver)
			{
				logger.log(CAT_FILE, L_DEBUG, "Reloading lastmod (version was %d, now %d)\n", m_ver, ver);
				m_ver = ver;
				return Load();
			}
			else
				return 0; // no need to reload
		}
		else
			return -1; // no file? :-0
	}
	else
		return -1;
}
