/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and some Mega-65 features as well.
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


#include "xemu/emutools.h"
#include "xemu/emutools_files.h"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>


void *xemu_load_buffer_p;
char  xemu_load_filepath[PATH_MAX];


#ifdef HAVE_XEMU_EXEC_API
#ifndef _WIN32
#include <sys/wait.h>

int xemuexec_run ( char *const args[] )
{
	pid_t pid = fork();
	if (pid == -1) {
		DEBUGPRINT("EXEC: fork() failed: %s" NL, strerror(errno));
		return -1;	// fork failed?
	}
	if (!pid) {	// the child's execution process after fork()
		int a;
		for(a = 3; a < 1024; a++)
			close(a);
		close(0);
		execvp(args[0], args);
		// exec won't return in case if it's OK. so if we're still here, there was a problem with the exec func ...
		printf("EXEC: execution of \"%s\" failed: %s" NL, args[0], strerror(errno));
		_exit(127);	// important to call this and not plain exit(), as we don't want to run atexit() registered stuffs and so on!!!
	}
	return pid;	// the parent's execution process after fork()
}


int xemuexec_check_status ( pid_t pid, int wait )
{
	int status;
	pid_t ret;
	if (pid <= 0)
		return 127;
	do {
		ret = waitpid(pid, &status, wait ? 0 : WNOHANG);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			DEBUGPRINT("EXEC: WAIT: waitpid(%d) returned error: %s" NL, pid, strerror(errno));
			return -1;
		}
	} while (ret < 0);
	if (ret != pid) {
		DEBUGPRINT("EXEC: still running" NL);
		return XEMUEXEC_STILL_RUNNING;	// still running?
	}
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return 127;	// we have not so much a standard status (not exited normally?!) so fake one ...
}

#else
#include <windows.h>
#include <tchar.h>

/* I'm not a Windows programmer not even a user ... By inpsecting MSDN articles, I am not sure, what needs
 * to be kept to be able to query the process later. So I keep about everything, namely this struct, malloc()'ed.
 * Though to be able to ratioanalize the prototype of functions (no need for include windows.h all the time by
 * callers too ...) the data type is a void* pointer instead of this madness "externally" ... */

struct ugly_windows_ps_t {
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	LPTSTR cmdline;
	DWORD creationstatus;
};

void *xemuexec_run ( char *const args[] )
{
	char cmdline[1024];
	int cmdlinesize = sizeof cmdline;
	struct ugly_windows_ps_t *st = malloc(sizeof(struct ugly_windows_ps_t));
	if (!st)
		FATAL("exec: cannot allocate memory");
	ZeroMemory(st, sizeof(struct ugly_windows_ps_t));
	st->si.cb = sizeof(STARTUPINFO);
	st->cmdline = NULL;
	if (snprintf(cmdline, cmdlinesize,"\"%s\"", args[0]) != strlen(args[0]) + 2)
		FATAL("exec: too long commandline");
	cmdlinesize -= strlen(args[0]) + 2;
	while (*(++args)) {
		if (cmdlinesize <= 0)
			FATAL("exec: too long commandline");
		if (snprintf(cmdline + strlen(cmdline), cmdlinesize, " %s", *args) != strlen(*args) + 1)
			FATAL("exec: too long commandline");
		cmdlinesize -= strlen(*args) + 1;
	}
	st->cmdline = _tcsdup(TEXT(cmdline));	// really no idea about this windows madness, just copying examples ...
	if (!st->cmdline) {
		free(st);
		FATAL("exec: cannot allocate memory");
	}
	// TODO: figure out what I should have for std handles to do and inheritance for the "child" process
#if 0
	//st->si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	st->si.hStdError = (HANDLE)_open_osfhandle(_fileno(stderr), _O_TEXT);
	st->si.hStdOutput = (HANDLE)_open_osfhandle(_fileno(stdout), _O_TEXT);
		//_open_osfhandle((INT_PTR)_fileno(stdout), _O_TEXT);	
		//GetStdHandle(STD_OUTPUT_HANDLE);
	//st->si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	st->si.hStdInput = (HANDLE)_get_osfhandle(fileno(stdin));
	//st->si.dwFlags |= STARTF_USESTDHANDLES;
	SetHandleInformation(st->si.hStdError, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(st->si.hStdOutput, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(st->si.hStdInput, HANDLE_FLAG_INHERIT, 0);
#endif
	if (CreateProcess(NULL,
		st->cmdline,
		NULL,		// process handle not inheritable
		NULL,		// thread handle not inheritable
		FALSE,		// set handle inheritance
		0,		// no creation flags
		NULL,		// use parent's env. block
		NULL,		// use parent's starting directory
		&st->si,	// startup-info structure pointer
		&st->pi		// process-info structure pointer
	)) {	// Windows does this differently as well compared to others: non-zero value means OKEY ....
		st->creationstatus = 0;
		DEBUGPRINT("EXEC: (%s) seems to be OK :-)" NL, cmdline);
	} else {
		st->creationstatus = GetLastError();
		DEBUGPRINT("EXEC: (%s) failed with %d" NL, cmdline, (int)st->creationstatus);
		if (!st->creationstatus) {	// I am not sure what Windows fumbles for, even MSDN is quite lame without _exact_ specification (MS should learn from POSIX dox ...)
			st->creationstatus = 1;
		}
	}
	//CloseHandle(st->si.hStdError);
	//CloseHandle(st->si.hStdOutput);
	//CloseHandle(st->si.hStdInput);
	return st;
}


static void free_exec_struct_win32 ( struct ugly_windows_ps_t *st )
{
	if (!st)
		return;
	if (st->creationstatus) {
		CloseHandle(st->pi.hProcess);
		CloseHandle(st->pi.hThread);
	}
	if (st->cmdline)
		free(st->cmdline);
	free(st);
}


#define PID ((struct ugly_windows_ps_t *)pid)
int xemuexec_check_status ( void* pid, int wait )
{
	if (!pid)
		return 127;
	if (PID->creationstatus) {
		DEBUGPRINT("EXEC: WAIT: returning because of deferred creationstatus(%d) != 0 situation" NL, (int)PID->creationstatus);
		free_exec_struct_win32(PID);
		return 127;
	} else {
		DWORD result = 0;
		do {
			if (!result)
				usleep(10000);
			GetExitCodeProcess(PID->pi.hProcess, &result);	// TODO: check return value if GetExitCodeProcess() was OK at all!
		} while (wait && result == STILL_ACTIVE);
		if (result == STILL_ACTIVE)
			return XEMUEXEC_STILL_RUNNING;
		free_exec_struct_win32(PID);
		return result;
	}
}
#undef PID

#endif
#endif




/* Open a file, returning a file descriptor, or negative value in case of failure.
 * Input parameters:
 *	filename: name of the file
 *		- if it begins with '@' the file is meant to relative to the SDL preferences directory (ie: @thisisit.rom, no need for dirsep!)
 *		- if it begins with '#' the file is meant for 'data directory' which is probed then multiple places, depends on the OS as well
 *		- otherwise it's simply a file name, passed as-is
 *	mode: actually the flags parameter for open (O_RDONLY, etc)
 *		- O_BINARY is used automatically in case of Windows, no need to specify as input data
 *		- you can even use creating file effect with the right value here
 *	*mode2: pointer to an int, secondary open mode set
 *		- if it's NULL pointer, it won't be used ever
 *		- if it's not NULL, open() is also tried with the pointed flags for open() after trying (and failed!) open() with the 'mode' par
 *		- if mode2 pointer is not NULL, the pointed value will be zeroed by this func, if NOT *mode2 is used with successfull open
 *		- the reason for this madness: opening a disk image which neads to be read/write access, but also works for read-only, however
 *		  then the caller needs to know if the disk emulation is about r/w or ro only ...
 *	*filepath_back: if not null, actually tried path will be placed here (even in case of non-successfull call, ie retval is negative)
 *		- in case of multiple-path tries (# prefix) the first (so the most relevant, hopefully) is passed back
 *		- note: if no prefix (@ and #) the filename will be returned as is, even if didn't hold absolute path (only relative) or no path as all (both: relative to cwd)!
 *
 */
int xemu_open_file ( const char *filename, int mode, int *mode2, char *filepath_back )
{
	char paths[10][PATH_MAX];
	int a, max_paths;
	if (!filename)
		FATAL("Calling xemu_open_file() with NULL filename!");
	if (!*filename)
		FATAL("Calling xemu_open_file() with empty filename!");
	max_paths = 0;
#ifdef __EMSCRIPTEN__
	sprintf(paths[max_paths++], "%s%s", EMSCRIPTEN_SDL_BASE_DIR, (filename[0] == '@' || filename[0] == '#') ? filename + 1 : filename);
#else
	if (*filename == '@') {
		sprintf(paths[max_paths++], "%s%s", sdl_pref_dir, filename + 1);
	} else if (*filename == '#') {
		sprintf(paths[max_paths++], "%s%s", sdl_inst_dir, filename + 1);
		sprintf(paths[max_paths++], "%s%s", sdl_pref_dir, filename + 1);
		sprintf(paths[max_paths++], "%srom" DIRSEP_STR "%s", sdl_base_dir, filename + 1);
		sprintf(paths[max_paths++], "%s%s", sdl_base_dir, filename + 1);
#ifndef _WIN32
		sprintf(paths[max_paths++], "/usr/local/share/xemu/%s", filename + 1);
		sprintf(paths[max_paths++], "/usr/local/lib/xemu/%s", filename + 1);
		sprintf(paths[max_paths++], "/usr/share/xemu/%s", filename + 1);
		sprintf(paths[max_paths++], "/usr/lib/xemu/%s", filename + 1);
#endif
	} else
		strcpy(paths[max_paths++], filename);
#endif
	a = 0;
	do {
		int fd;
		// Notes:
		// 1. O_BINARY is a windows stuff. However, since we define O_BINARY as zero for non-win archs, it's OK
		// 2. 0666 mask is needed as it can be also a creat() function basically ...
		fd = open(paths[a], mode | O_BINARY, 0666);
		if (filepath_back)
			strcpy(filepath_back, paths[a]);
		if (fd >= 0) {
			if (mode2)
				*mode2 = 0;
			DEBUGPRINT("FILE: file %s opened as %s with base mode-set as fd=%d" NL, filename, paths[a], fd);
			return fd;
		}
		if (mode2) {
			fd = open(paths[a], *mode2 | O_BINARY, 0666);	// please read the comments at the previous open(), above
			if (fd >= 0) {
				DEBUGPRINT("FILE: file %s opened as %s with *second* mode-set as fd=%d" NL, filename, paths[a], fd);
				return fd;
			}
		}
	} while (++a < max_paths);
	// if not found, copy the first try so the report for user is more sane
	if (filepath_back)
		strcpy(filepath_back, paths[0]);
	DEBUGPRINT("FILE: %s cannot be open, tried path(s): ", filename);
	for (a = 0; a < max_paths; a++)
		DEBUGPRINT(" %s", paths[a]);
	DEBUGPRINT(NL);
	return -1;
}



ssize_t xemu_safe_read ( int fd, void *buffer, size_t length )
{
	ssize_t loaded = 0;
	while (length > 0) {
		ssize_t r = read(fd, buffer, length);
		if (r < 0) {	// I/O error on read
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				continue;
			return -1;
		}
		if (r == 0)	// end of file
			break;
		loaded += r;
		length -= r;
		buffer += r;
	}
	return loaded;
}


ssize_t xemu_safe_write ( int fd, const void *buffer, size_t length )
{
	ssize_t saved = 0;
	while (length > 0) {
		ssize_t w = write(fd, buffer, length);
		if (w < 0) {	// I/O error on write
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
				continue;
			return -1;
		}
		if (w == 0)	// to avoid endless loop, if no data could be written more
			break;
		saved  += w;
		length -= w;
		buffer += w;
	}
	return saved;
}



/* Loads a file, probably ROM image etc. It uses xemu_open_file() - see above - for opening it.
 * Return value:
 * 	- non-negative: given mumber of bytes loaded
 * 	- negative, error: -1 file open error, -2 file read error, -3 limit constraint violation
 * Input parameters:
 * 	* filename: see comments at xemu_open_file()
 * 	* store_to: pointer to the store buffer
 * 		- note: the buffer will be filled only in case of success, no partial modification can be
 * 		- if store_to is NULL, then the load buffer is NOT free'd and the buffer pointer is assigned to xemu_load_buffer_p
 * 	* min_size,max_size: in bytes, the minimal needed and the maximal allowed file size (can be the same)
 * 		- note: limit contraint violation, if this does not meet during the read ("load") process
 * 	* cry: character message, to 'cry' (show an error window) in case of a problem. if NULL = no dialog box
 */
int xemu_load_file ( const char *filename, void *store_to, int min_size, int max_size, const char *cry )
{
	int fd = xemu_open_file(filename, O_RDONLY, NULL, xemu_load_filepath);
	if (fd < 0) {
		if (cry) {
			ERROR_WINDOW("Cannot open file requested by %s: %s\nTried as: %s\n%s%s", filename, strerror(errno), xemu_load_filepath,
				(*filename == '#') ? "(# prefixed, multiple paths also tried)\n" : "",
				cry
			);
		}
		return -1;
	} else {
		int load_size;
		xemu_load_buffer_p = xemu_malloc(max_size + 1);	// try to load one byte more than the max allowed, to detect too large file scenario
		load_size = xemu_safe_read(fd, xemu_load_buffer_p, max_size + 1);
		if (load_size < 0) {
			ERROR_WINDOW("Cannot read file %s: %s\n%s", xemu_load_filepath, strerror(errno), cry ? cry : "");
			free(xemu_load_buffer_p);
			xemu_load_buffer_p = NULL;
			close(fd);
			return -2;
		}
		close(fd);
		if (load_size < min_size) {
			free(xemu_load_buffer_p);
			xemu_load_buffer_p = NULL;
			if (cry)
				ERROR_WINDOW("File (%s) is too small (%d bytes), %d bytes needed.\n%s", xemu_load_filepath, load_size, min_size, cry);
			else
				DEBUGPRINT("FILE: file (%s) is too small (%d bytes), %d bytes needed." NL, xemu_load_filepath, load_size, min_size);
			return -3;
		}
		if (load_size > max_size) {
			free(xemu_load_buffer_p);
			xemu_load_buffer_p = NULL;
			if (cry)
				ERROR_WINDOW("File (%s) is too large, larger than %d bytes.\n%s", xemu_load_filepath, max_size, cry);
			else
				DEBUGPRINT("FILE: file (%s) is too large, larger than %d bytes needed." NL, xemu_load_filepath, max_size);
			return -3;
		}
		if (store_to) {
			memcpy(store_to, xemu_load_buffer_p, load_size);
			free(xemu_load_buffer_p);
			xemu_load_buffer_p = NULL;
		} else
			xemu_load_buffer_p = xemu_realloc(xemu_load_buffer_p, load_size);
		DEBUGPRINT("FILE: %d bytes loaded from file: %s" NL, load_size, xemu_load_filepath);
		return load_size;
	}
}
