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

/*  $Id: config.h,v 1.13 2002/12/18 18:38:53 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <vector>
#include <sys/types.h>
#include <regex.h>
#include <string>
#include "my_hash_map"
#include "defines.h"
#include "maps.h"

extern std::string DataDir;
extern std::string ConfDir;
extern std::vector<std::string> dblib_paths;
extern int MaxThreads;
extern int IncrementalCitations;

extern char conf_err_str[STRSIZ];

class CBWSchedule;

int LoadConfig(char *conf_name, int config_level, int load_flags);
char* GetToken(char *s, const char *delim, char **last);
char* ExtraHeaders();
char* UserAgent();

extern std::string DBHost, DBName, DBUser, DBPass;
extern ULONG MaxMem, WordCacheSize, HrefCacheSize;
extern int _max_doc_size;
extern CBWSchedule bwSchedule;

#endif
