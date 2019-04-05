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

/*  $Id: misc.h,v 1.18 2002/07/15 12:11:18 kir Exp $
    Author : Kir Kolyshkin
*/

#ifndef _MISC_H_
#define _MISC_H_

#include "aspseek-cfg.h"
#include <sys/types.h>
#include <sys/file.h>	/* for flock */
#ifndef HAVE_FLOCK
#include <unistd.h>
#include <fcntl.h>
#endif

/// "Safe" mkdir that prints error message and do optional exit(1) on fail
int safe_mkdir(const char *dir, mode_t mode, const char *errstr_prefix="", int exit_on_fail=1);

/// Returns file size in bytes or -1 on error
size_t fsize(int fd);

/// Case-insensitive variant of strstr function
//char *strcasestr(const char *haystack, const char *needle);

/// Removes extra spaces from the end of src
char *str_rtrim(register char *src);
/// Removes extra spaces from the beginning of src by moving the pointer
char *str_ltrim(register char *src);
/// Removes extra spaces from the both sides of src
char *str_trim(register char *src);

/// Returns true if path is absolute or relative to current directory
inline int isAbsolutePath(const char * s)
{
	return ((s[0] == '/') || ((s[0] == '.') && (s[1] == '/')) ||
		((s[0] == '.') && (s[1] == '.') && (s[2] == '/')));
}

// Some useful macros (shamelessly taken from /usr/include/pbmplus.h)
#undef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#undef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#undef abs
#define abs(a) ((a) >= 0 ? (a) : -(a))


// This is to skip \n \r \t and spaces in HTML parser
#define skip_junk(s) while ((*s == '\r') || (*s == '\n') || (*s == ' ') || (*s == '\t')) s++;

#ifdef HAVE_FLOCK
/// This is for file locking
class CFileLocker
{
public:
	int m_locked_fd;	///< file descriptor
public:
	CFileLocker(void): m_locked_fd(-1) {}
	int GetFd(void) { return m_locked_fd; }
	/// Try to get exclusive non-blocking lock
	int Lock(int fd)
	{
		return FLock(fd, LOCK_EX | LOCK_NB);
	}
	/// Try to get exclusive lock, waiting
	int LockWait(int fd)
	{
		return FLock(fd, LOCK_EX);
	}
	/// Try to get non-exclusive lock
	int LockRead(int fd)
	{
		return FLock(fd, LOCK_SH | LOCK_NB);
	}
	/// Try to get non-exclusive lock, waiting
	int LockReadWait(int fd)
	{
		return FLock(fd, LOCK_SH);
	}
	void Unlock(void)
	{
		if (m_locked_fd >= 0)
			flock(m_locked_fd, LOCK_UN);
	}
	~CFileLocker(void)
	{
		Unlock();
	}
private:
	inline int FLock(int fd, int op)
	{
		if (fd < 0)
			return -1; // bad fd
		m_locked_fd = fd;
		return flock(fd, op);
	}
};

#elif HAVE_FCNTL
/// This is for file locking
class CFileLocker
{
public:
	int m_locked_fd;	///< file descriptor
	int m_flags;		///< flags
	struct flock* m_flock;	///< pointer to flock struct
public:
	CFileLocker(struct flock* fl = NULL)
	{
		if (fl)
		{
			m_flock = fl;
			m_flags = 1; // not delete m_flock in destructor
		}
		else
		{
			m_flock = new struct flock;
		}
		InitFlockStruct();
	}
	~CFileLocker()
	{
		Unlock();
		if (!(m_flags & 1))
			delete m_flock;
	}
	int GetFd(void) { return m_locked_fd; }
	/// Try to get exclusive non-blocking lock
	int Lock(int fd)
	{
		return FCntl(fd, F_WRLCK);
	}
	/// Try to get exclusive lock, waiting
	int LockWait(int fd)
	{
		return FCntlWait(fd, F_WRLCK);
	}
	/// Try to get non-exclusive lock
	int LockRead(int fd)
	{
		return FCntl(fd, F_RDLCK);
	}
	/// Try to get non-exclusive lock, waiting
	int LockReadWait(int fd)
	{
		return FCntlWait(fd, F_RDLCK);
	}
	/// Unlocks the file
	void Unlock(void)
	{
		FCntl(m_locked_fd, F_UNLCK);
	}
private:
	void InitFlockStruct(void){
		// lock the whole file
		m_flock->l_whence = SEEK_SET;
		m_flock->l_start = 0;
		m_flock->l_len = 0;
		m_flock->l_pid = getpid();
	}
	inline int FCntl(int fd, int op){
		if (fd < 0)
		{
//			logger.log(CAT_ALL, L_WARN, "CFileLocker: bad fd given: %d\n", fd);
			return -1; // bad fd
		}
		m_locked_fd = fd;
		m_flock->l_type = op;
		return fcntl(m_locked_fd, F_SETLK, m_flock);
	}
	inline int FCntlWait(int fd, int op){
		if (fd < 0)
		{
//			logger.log(CAT_ALL, L_WARN, "CFileLocker: bad fd given: %d\n", fd);
			return -1; // bad fd
		}
		m_locked_fd = fd;
		m_flock->l_type = op;
		return fcntl(m_locked_fd, F_SETLKW, m_flock);
	}
};
#else /* HAVE_FCNTL */
#error You do neither have flock nor fcnlt on your system
#endif

#endif /* _MISC_H_ */
