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

/*  $Id: rank.cpp,v 1.15 2002/01/09 11:50:15 kir Exp $
    Author : Alexander F. Avdonkin
	Implemented classes: CUrlRanks
*/

#include <algorithm>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "config.h"
#include "rank.h"
#include "sqldb.h"
#include "logger.h"

using std::sort;

#define DAMPING_FACTOR 0.85

void CUrlRank::Sort()
{
	if (m_refend > m_referrers)
	{
		sort<CUrlRankPtrU*>((CUrlRankPtrU*)m_referrers, (CUrlRankPtrU*)m_refend);
	}
}

void CUrlRanks::Iterate()
{
	iterator ite = end();
	for (iterator it = begin(); it != ite; it++)
	{
		CUrlRank& rank = const_cast<CUrlRank&>(*it);
		float w = 0;
		if (rank.m_referrers)
		{
			for (CUrlRank** referrer = rank.m_referrers; referrer != rank.m_refend; referrer++)
			{
				w += (*referrer)->AvgRank();
			}
			rank.m_rank1 = DAMPING_FACTOR * w + (1 - DAMPING_FACTOR);
		}
	}
	for (iterator it = begin(); it != ite; it++)
	{
		CUrlRank& rank = const_cast<CUrlRank&>(*it);
		if (rank.m_referrers)
		{
			rank.m_rank = rank.m_rank1;
		}
	}
}

void CUrlRanks::Allocate()
{
	iterator ite = end();
	for (iterator it = begin(); it != ite; it++)
	{
		CUrlRank& rank = const_cast<CUrlRank&>(*it);
		rank.Allocate();
	}
}

void CUrlRanks::Sort()
{
	iterator ite = end();
	for (iterator it = begin(); it != ite; it++)
	{
		CUrlRank& rank = const_cast<CUrlRank&>(*it);
		rank.Sort();
	}
}

void CUrlRanks::Save()
{
	string fname(DBWordDir);
	fname.append(IncrementalCitations ? "/ranksd" : "/ranks");
	FILE* f = fopen(fname.c_str(), "r+");
	if (f == NULL)
	{
		f = fopen(fname.c_str(), "w+");
	}
	if (f)
	{
		float* ranks = new float[m_max + 1];
		memset(ranks, 0, m_max << 2);
		for (iterator it = begin(); it != end(); it++)
		{
			const CUrlRank& rank = *it;
			if (m_max < rank.m_urlID){
//				logger.log(CAT_ALL, L_ERR, "Bug in %s:%d (m_max < rank.m_urlID)\n", __FILE__, __LINE__);
			}
			else
				ranks[rank.m_urlID] = rank.m_rank;
		}
		ULONG ver;
		if (fread(&ver, sizeof(ver), 1, f) == 0)
		{
			ver = 1;
			fwrite(&ver, sizeof(ver),1, f);
		}
		else
		{
			ver++;
		}
		fwrite(ranks, sizeof(ranks[0]), m_max, f);
		if (ver > 1)
		{
			fseek(f, 0, SEEK_SET);
			fwrite(&ver, 1, sizeof(ver), f);
		}
		fclose(f);
		delete ranks;
	}
}

void CUrlRanks::Load()
{
	string fname(DBWordDir);
	fname.append(IncrementalCitations ? "/ranksd" : "/ranks");
	FILE* f = fopen(fname.c_str(), "r");
	if (f)
	{
		float w[0x4000];
		fseek(f, 4, SEEK_SET);
		ULONG urlID = 0;
		int cw;
		while ((cw = fread(&w, sizeof(w[0]), sizeof(w) / sizeof(w[0]), f)))
		{
			for (int i = 0; i < cw; i++)
			{
				if (w[i] != 0)
				{
					(*this)[urlID].m_rank = w[i];
				}
				urlID++;
			}
		}
		fclose(f);
	}
}

void CUrlRanks::PrintInOut()
{
	iterator ite = end();
	int out = 0;
	int in = 0;
	double trank = 0;
	for (iterator it = begin(); it != ite; it++)
	{
		const CUrlRank& rank = *it;
		out += rank.m_out;
		in += rank.m_refend - rank.m_referrers;
		trank += rank.m_rank;
	}
	logger.log(CAT_ALL, L_INFO, "In: %i. Out: %i. Rank: %f\n", in, out, trank);
}
