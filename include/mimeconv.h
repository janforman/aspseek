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

/*  $Id: mimeconv.h,v 1.4 2002/01/09 11:50:14 kir Exp $
 *  Author: Kir Kolyshkin
 */

#ifndef _MIMECONV_H_
#define _MIMECONV_H_

#include <string>
#include <map>

using std::string;
using std::map;

class CMimeConverter
{
public:
	/// MIME type of source
	string m_from;
	/// MIME type of result
	string m_to;
	/// Charset of result, if set
	string m_charset;
public:
	CMimeConverter(char * from, char * to, char * charset = NULL)
	{
		if (from)
			m_from = from;
		if (to)
			m_to = to;
		if (charset)
			m_charset = charset;
	}
	/// Converts buffer "buf" that has "len" size, filling up to "maxlen"
	virtual char* Convert(char* buf, int& len, int maxlen, const char* url) = 0;
	virtual ~CMimeConverter(void){}
};

/** External MIME-type converter class
 */
class CExtConv : public CMimeConverter {
public:
	/// command name to be executed
	string m_cmd;
public:
	// Makes a new converter
	CExtConv(char * from, char * to, char * charset, char * cmd): CMimeConverter(from, to, charset)
	{
		if (cmd)
			m_cmd = cmd;
	}
	/// Converts buffer "buf" that has "len" size, filling up to "maxlen"
	virtual char* Convert(char* buf, int& len, int maxlen, const char* url);
	virtual ~CExtConv(void){}

};

class CConverters: public map<string, CMimeConverter*>
{
};

extern CConverters converters;

#endif /* _MIMECONV_H_ */
