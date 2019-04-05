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

/*  $Id: mimeconv.cpp,v 1.6 2002/05/11 15:27:27 kir Exp $
 *  Author: Kir Kolyshkin
 */

#include <unistd.h>	// for unlink
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include "logger.h"
#include "mimeconv.h"

inline int substStr(string& s, const char * var, const char * value)
{
	int p = s.find(var);
	if (p == -1)
		return -1; // not found
	int len = strlen(var);
	if (value)
	{
		do { // var can be used more than once
			s.erase(p, len);
			s.insert(p, value);
		} while ((p = s.find(var)) != -1);
		return 1; // done
	}
	return 0; // no replace
}

char* CExtConv::Convert(char* buf, int& len, int maxlen, const char* url)
{
	string cmd = m_cmd;
	char * ret = NULL;
	if (url)
		substStr(cmd, "$url", url);

	// temp files - may be needed
	int in_fd = -1, out_fd = -1;
	char in_f[]="/tmp/asiXXXXXX";
	char out_f[]="/tmp/asoXXXXXX";

	int in_p = cmd.find("$in");
	if (in_p != -1)
	{
		if ((in_fd=mkstemp(in_f)) < 0)
		{
			logger.log(CAT_FILE, L_ERROR, "Can't create temp file %s: %s\n", in_f, strerror(errno));
			return NULL; // error
		}
		// write file
		if (write(in_fd, buf, len) != len)
		{
			logger.log(CAT_FILE, L_ERROR, "Can't write to %s: %s\n", in_f, strerror(errno));
			return NULL;
		}
		// clear the output buffer
		memset((void *)buf, 0, maxlen);
		substStr(cmd, "$in", in_f);
	}

	int out_p = cmd.find("$out");
	if (out_p != -1)
	{
		if ((out_fd=mkstemp(out_f)) < 0)
		{
			logger.log(CAT_FILE, L_ERROR, "Can't create temp file %s: %s\n", out_f, strerror(errno));
			if (in_fd > 0)
			{ // clean up
				close(in_fd);
				unlink(in_f);
			}
			return NULL; // error
		}
//		fchmod(out_fd, 0x666);
		close(out_fd);
		substStr(cmd, "$out", out_f);
	}

	// do the conversion
	logger.log(CAT_ALL, L_INFO, "exec %s\n", cmd.c_str());

	if ((in_fd >= 0) && (out_fd >= 0)) // file -> file converter
	{
		system(cmd.c_str());
		out_fd = open(out_f, O_RDONLY);
		if (out_fd < 0)
		{
			logger.log(CAT_FILE, L_ERR, "Can't open file %s: %s", out_f, strerror(errno));
			// clean up
			if (in_fd > 0)
			{
				close(in_fd);
				unlink(in_f);
			}
			unlink(out_f);
			return NULL; // error
		}
		len = read(out_fd, buf, maxlen);
		if (len > 0)
			ret = (char *) m_to.c_str();
	}
	// file -> stdout converter
	else if ((in_fd >= 0) && (out_fd < 0))
	{
		// FIXME
	}
	// stdin -> file converter
	else if ((in_fd < 0) && (out_fd >= 0)) // stdin -> file converter
	{
		// FIXME
		// clear the output buffer
		memset((void *)buf, 0, maxlen);
		out_fd = open(out_f, O_RDONLY);
		if (out_fd < 0)
		{
			logger.log(CAT_FILE, L_ERR, "Can't open file %s: %s", out_f, strerror(errno));
			// clean up
			if (in_fd > 0)
			{
				close(in_fd);
				unlink(in_f);
			}
			unlink(out_f);
			return NULL; // error
		}
		len = read(out_fd, buf, maxlen);
		if (len > 0)
			ret = (char *) m_to.c_str();
	}
	else // stdin -> stdout
	{
		int wr[2];
		int rd[2];
		pid_t pid;

		// Create write and read pipes
		if (pipe(wr) == -1)
		{
			logger.log(CAT_ALL, L_ERR, "Can't make a pipe: %s\n", strerror(errno));
			return NULL;
		}
		if (pipe(rd) == -1)
		{
			logger.log(CAT_ALL, L_ERR, "Can't make a pipe: %s\n", strerror(errno));
			return NULL;
		}
		// Fork a child
		if ( (pid = fork()) == -1 )
		{
			logger.log(CAT_ALL, L_ERR, "Can't spawn a child: %s\n", strerror(errno));
			return NULL;
		}

		if (pid > 0) // We are parent
		{
			// Close other pipe ends
			close(wr[0]);
			close(wr[1]);
			close(rd[1]);
			// clear the output buffer
			memset((void *)buf, 0, maxlen);
			// read() waits till the pipe will be closed from other end
			int rem_len = maxlen;
			char * pbuf = buf;
			int br;
			while ((br = read(rd[0], pbuf, rem_len)) > 0)
			{
				rem_len -= br;
				pbuf += br;
			}
			if ((br >= 0) && (rem_len != maxlen)) // read something
				ret = (char *)m_to.c_str();
			else
				logger.log(CAT_ALL, L_ERR, "Can't read from pipe: %s\n", strerror(errno));
			close(rd[0]);
			wait(NULL);
		}
		else // We are child
		{
			// Fork a child
			if ( (pid = fork()) == -1 )
			{
				logger.log(CAT_ALL, L_ERR, "Can't spawn a child: %s\n", strerror(errno));
				return NULL;
			}
			if (pid > 0) // Parent process
			{
				// Close other pipe ends
				close(wr[0]);
				close(rd[0]);
				close(rd[1]);

				// Send data to be converted
				if (write(wr[1], buf, len) != len)
					logger.log(CAT_ALL, L_ERR, "Can't write to pipe: %s\n", strerror(errno));

				close(wr[1]);
				exit(0);
			}
			else // Child process
			{
				// Close other pipe ends
				close (wr[1]);
				close (rd[0]);
				// Connect pipes to stdin and stdout
				if (dup2(wr[0], fileno(stdin)) < 0)
					logger.log(CAT_ALL, L_ERR, "Can't dup2 stdin: %s\n", strerror(errno));
				if (dup2(rd[1], fileno(stdout)) < 0)
					logger.log(CAT_ALL, L_ERR, "Can't dup2 stdout: %s\n", strerror(errno));

				// Run the command
				exit(system(cmd.c_str()));
			}
		}

	}
	// clean up (ignore errors)
	close(in_fd);
	unlink(in_f);
	close(out_fd);
	unlink(out_f);
	return ret;
}

CConverters converters;
