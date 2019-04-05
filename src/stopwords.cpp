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

/*  $Id: stopwords.cpp,v 1.14 2002/01/09 11:50:15 kir Exp $
    Author : Kir Kolyshkin
	Implemented classes: CStopWords
*/

#include <stdio.h>
#include <string>
#include <errno.h>
#include <string.h>
//#include "content.h"	// for CWord
#include "config.h"	// for ConfDir
#include "logger.h"
#include "stopwords.h"
#ifdef UNICODE
#include "ucharset.h"
#else
#include "charsets.h"
#endif
#include "misc.h"

#ifdef UNICODE
int CStopWords::Load(const char* file, const char* lang, const char* charset)
{
	CCharsetBase* ucharset = GetU1Charset(charset);
	InitCharset0();
#else
int CStopWords::Load(const char* file, const char* lang)
{
#endif
	FILE *f;
	string fullname;
	int line_no=0;
	char str[256];
	char *ch_p;
	if (isAbsolutePath(file)) // absolute path
		fullname = file;
	else
		fullname = ConfDir + "/" + file;
	if (!(f = fopen(fullname.c_str(), "r")))
	{
		logger.log(CAT_FILE, L_ERR, "Can't open stopwords file %s: %s\n", fullname.c_str(), strerror(errno));
		return -1;
	}
	memset((void *)str, 0, sizeof(str));
	logger.log(CAT_FILE, L_DEBUG, "Loading stop words from %s\n", fullname.c_str());
	while (fgets(str, sizeof(str), f))
	{
		line_no++;
		ch_p = Trim(str, " \t\n\r");
		if (!*ch_p || (*ch_p == '#') || (*ch_p == 0) || (strlen(ch_p) > 16)) // empty line or comment
			continue;
		// FIXME: do we need to check for something before inserting?
#ifdef UNICODE
		WORD uw[17];
		WORD* puw = uw;
		while (*ch_p) *puw++ = ucharset->UCodeLower(*ch_p++);
		*puw = 0;
		CWordLang w(uw, lang);
#else
		CWordLang w(ch_p, lang);
#endif
		insert(w);
	}
	fclose(f);
	return 0; // ok
}
