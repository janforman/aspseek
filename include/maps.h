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

/*  $Id: maps.h,v 1.12 2003/05/06 12:55:16 kir Exp $
    Authors: Alexander F. Avdonkin, Kir Kolyshkin
*/

#ifndef _MAPS_INCLUDED_
#define _MAPS_INCLUDED_

#include <string>
#include "my_hash_set"
#include "my_hash_map"
#include "aspseek-cfg.h"
#include "defines.h"

namespace STL_EXT_NAMESPACE
{
	template <> struct hash<std::string>
	{
		size_t operator()(const std::string& s) const
		{
			return __stl_hash_string(s.c_str());
		}
	};
}

typedef hash_map<std::string, std::string> MapStringToString;
typedef hash_map<std::string, int> MapStringToInt;
typedef hash_set<ULONG> CULONGSet;

#endif /* _MAPS_INCLUDED_ */
