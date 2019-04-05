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

/*  $Id: mysqldbi.h,v 1.5 2002/05/14 11:47:16 kir Exp $
    Author : Igor Sukhih
*/

#ifndef _MYSQLDBI_H_
#define _MYSQLDBI_H_

#include "sqldbi.h"
#include "mysqldb.h"

class CMySQLDatabaseI : public CSQLDatabaseI, public CMySQLDatabase
{
public:
#ifdef TWO_CONNS
	virtual CSQLDatabaseI* GetAnother() {return new CMySQLDatabaseI;};
#endif
};
#endif

