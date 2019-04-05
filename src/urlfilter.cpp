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

/*  $Id: urlfilter.cpp,v 1.7 2002/05/11 15:27:27 kir Exp $
 *  Authors: Alexander F. Avdonkin, Kir Kolyshkin, Igor Sukhih
 *  Implemented classes: CResultUrlFilter, CTimeFilter
 */

#include "aspseek-cfg.h"
#include "urlfilter.h"

/** Method builds buffer of URL info with weights taking filter into account.
 * Input is buffer containing URL info, which read from file or BLOB.
 * Parameters: "size" is size of input buffer in bytes,
 * "p_buffer" is pointer to original buffer,
 * "buffer" is reference to pointer to resulting buffer,
 * "form" is 0x8000 for exact word form or 0 for derived word.
 * Returns length of resulting buffer.
 */
ULONG CResultUrlFilter::GetBufferWithWeightsS(int size, BYTE* p_buffer, BYTE*& buffer, WORD form)
{
	buffer = new BYTE[size + (size >> 2)];	// Allocate maximal possible buffer
	WORD* pw = (WORD*)buffer;
	WORD* pbuf = (WORD*)p_buffer;
	WORD* pe = (WORD*)(p_buffer + size);
	// Process each URL
	while (pbuf < pe)
	{
		if ( CheckUrl(*(ULONG*)pbuf) )
		{ // UrlID is ok
			*(ULONG*)pw = *(ULONG*)pbuf;		// Copy URL ID
			pw[3] = pbuf[2] & 0x3FFF;		// Copy position count
			memcpy(pw + 4, pbuf + 3, pw[3] << 1);	// Copy positions
			pbuf += 3;
			WORD* ppos = pbuf + pw[3] - 1;
			WORD loc;
			if ((loc = (*ppos & 0xC000)) == 0)
			{
				pw[2] = form;	// Word is found only in body, weight is 0
#ifdef USE_FONTSIZE
				if (pbuf[-1] & 0xC000)
					pw[2] = form | (pbuf[-1] & 0xC000) >> 3;
#endif
			}
			else
			{
				if (m_accDist)
				{
					// Find the least position in HTML part (title/keywords/description)
					while ((--ppos >= pbuf) && ((*ppos & 0xC000) == loc));
					// Calculate weight
					pw[2] = form | (loc >> 1) | ((~ppos[1]) & 0x1FFF);
				}
				else
				{
					pw[2] = form | (loc >> 1) | 0x1FFF;
				}
			}
			pbuf += pw[3];		// Advance source pointer
			pw += pw[3] + 4;	// Advance destination pointer
		}
		else
		{ // do not include this URL in results
			pbuf += ((pbuf[2] & 0x3FFF) + 3); // Only advance source pointer
		}
	}
	return (BYTE*)pw - buffer;
}

/** This is the same as above, but you can also put a filter (mask)
 * by title/.../body
 */
ULONG CResultUrlFilter::GetBufferWithWeightsS(int size, BYTE* p_buffer, BYTE*& buffer, ULONG mask, WORD form)
{
	buffer = new BYTE[size + (size >> 2)];	// Allocate maximal possible buffer
	WORD* pw = (WORD*)buffer;
	WORD* pbuf = (WORD*)p_buffer;
	WORD* pe = (WORD*)(p_buffer + size);
	// Process each URL
	while (pbuf < pe)
	{
		if ( CheckUrl(*(ULONG*)pbuf) )
		{ // UrlID is ok
			*(ULONG*)pw = *(ULONG*)pbuf;		// Copy URL ID
			WORD* pbufe = pbuf + (pbuf[2] & 0x3FFF) + 3;
			pw += 3;
			WORD* pc = pw++;
			pbuf += 3;
			while (pbuf < pbufe)
			{
				if (mask & (1 << (*pbuf >> 14)))
				{
					*pw++ = *pbuf;
				}
				pbuf++;
			}
			if ((*pc = pw - pc - 1))
			{
				WORD loc;
				WORD* ppos = pw - 1;
				if ((loc = (*ppos & 0xC000)) == 0)
				{
					// Word is found only in body, weight is 0
					pc[-1] = form;
				}
				else
				{
					if (m_accDist)
					{
						// Find the least position in HTML part (title/keywords/description)
						while ((--ppos > pc) && ((*ppos & 0xC000) == loc));
						pc[-1] = form | (loc >> 1) | ((~ppos[1]) & 0x1FFF);		//Calculate weight
					}
					else
					{
						pc[-1] = form | (loc >> 1) | 0x1FFF;
					}
				}
			}
			else
			{
				pw -= 4;
			}
			pbuf = pbufe;
		}
		else
		{ // do not include this URL in results
			pbuf += ((pbuf[2] & 0x3FFF) + 3); // Only advance source pointer
		}
	}
	return (BYTE*)pw - buffer;
}

ULONG CResultUrlFilter::GetBufferWithWeightsSL(int size, BYTE* p_buffer, BYTE*& buffer)
{
	buffer = new BYTE[size << 1];
	ULONG* pw = (ULONG*)buffer;
	ULONG* pbuf = (ULONG*)p_buffer;
	ULONG* pe = (ULONG*)(p_buffer + size);
	while (pbuf < pe)
	{
		if ( CheckUrl(*pbuf) )
		{ // UrlID is ok - do the copy
			*pw = *pbuf;
			pw[1] = 0;
			pw += 2;
		}
		pbuf++; // Advance source pointer
	}
	return (BYTE*)pw - buffer;
}

bool CTimeFilter::CheckUrl(ULONG UrlID)
{
	if ( (m_fr) || (m_to) ) // at least one date set
	{
		WORD t = m_lm[UrlID];
		if (t==0) // no last mod. time for this UrlID
		{
#ifdef INCLUDE_URLS_WITHOUT_LASTMOD
			return true;	// accept it
#else
			return false;	// reject it
#endif
		}
		if ((t >= m_fr) && (t <= m_to))
		{
			return true;	// accept
		}
		else
		{
			return false;	// reject
		}
	}
	else
		return true; // no filter - every URL is ok
}
