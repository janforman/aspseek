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

/*  $Id: unaligned.h,v 1.1 2002/05/31 12:19:27 kir Exp $
    Author: Kir Kolyshkin
*/

/* Below macros were shamelessly taken from linux-2.4 source code
 * (file include/asm-sparc/unaligned.h under linux source tree).
 * They are used if hardware do not support unaligned access, and
 * operating system kernel does not emulate it either. Seems that
 * at least Linux kernel does that emulation, while other OSes
 * like SunOS 5.8 does not. Script configure will check it and
 * set EMULATE_UNALIGNED if emulation is needed. ASPseek code
 * that use unaligned access is supposed to include this file and
 * use get_unaligned and put_unaligned macros.
 *
 * These are generic case macros suitable for any arch; probably
 * some architectures will benefit from more sophisticated code.
 *   --kir.
 */

#ifndef __UNALIGNED_H_
#define __UNALIGNED_H_

#include "aspseek-cfg.h"

#ifdef EMULATE_UNALIGNED

#include <string.h>

#define get_unaligned(ptr) \
 ({ __typeof__(*(ptr)) __tmp; memmove(&__tmp, (ptr), sizeof(*(ptr))); __tmp; })

#define put_unaligned(val, ptr)				\
 ({ __typeof__(*(ptr)) __tmp = (val);			\
     memmove((ptr), &__tmp, sizeof(*(ptr)));		\
     (void)0; })

#else

#define get_unaligned(ptr) (*(ptr))
#define put_unaligned(val, ptr) ((void)( *(ptr) = (val) ))

#endif /* EMULATE_UNALIGNED */

#endif /* __UNALIGNED_H_ */
