/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include <stdlib.h>
#include <limits.h>
#include <errno.h>


void *xemu_load_buffer_p;
char  xemu_load_filepath[PATH_MAX];


#ifdef HAVE_XEMU_EXEC_API
#ifndef XEMU_ARCH_WIN
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
		int arg_padding_len = strchr(*args, ' ') ? 3 : 1;
		if (cmdlinesize <= 0)
			FATAL("exec: too long commandline");
		if (snprintf(cmdline + strlen(cmdline), cmdlinesize, arg_padding_len == 1 ? " %s" : " \"%s\"", *args) != strlen(*args) + arg_padding_len)
			FATAL("exec: too long commandline");
		cmdlinesize -= strlen(*args) + arg_padding_len;
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

// end of #ifndef XEMU_ARCH_WIN
#endif

void xemuexec_open_native_file_browser ( char *dir )
{
#ifdef HAVE_XEMU_EXEC_API
	static xemuexec_process_t fbp = XEMUEXEC_NULL_PROCESS_ID;
	char *args[] = {FILE_BROWSER, dir, NULL};
	if (fbp != XEMUEXEC_NULL_PROCESS_ID) {
		int w = xemuexec_check_status(fbp, 0);
		DEBUGPRINT("EXEC: FILEBROWSER: previous file browser process (" PRINTF_LLD ") status was: %d" NL, (unsigned long long int)(uintptr_t)fbp, w);
		if (w == XEMUEXEC_STILL_RUNNING)
			ERROR_WINDOW("A file browser is already has been opened.");
		else if (w == -1)
			ERROR_WINDOW("Process communication problem");
		else
			fbp = XEMUEXEC_NULL_PROCESS_ID;
	}
	if (XEMU_LIKELY(fbp == XEMUEXEC_NULL_PROCESS_ID))
		fbp = xemuexec_run(args);	// FIXME: process on exit will be "orphaned" (ie zombie) till exit from Xemu, because it won't be wait()'ed by the parent (us) ...
#else
	ERROR_WINDOW("Sorry, no execution API is supported by this Xemu build\nto allow to launch an OS-native file browser for you on directory:\n%s", dir);
#endif
}


#ifdef HAVE_XEMU_INSTALLER

char *xemu_installer_db = NULL;

static const char installer_marker_prefix[] = "[XEMU_DOWNLOADER]=";

static char installer_store_to[PATH_MAX];
static char installer_fetch_url[256];
static const char *downloader_utility_specifications[] = {
#ifdef XEMU_ARCH_WIN
	"powershell", "-Command", "Invoke-WebRequest", installer_fetch_url, "-OutFile", installer_store_to, "-Verbose", NULL,
	"powershell.exe", "-Command", "Invoke-WebRequest", installer_fetch_url, "-OutFile", installer_store_to, "-Verbose", NULL,
#endif
	"curl", "-o", installer_store_to, installer_fetch_url, NULL,
#ifdef XEMU_ARCH_WIN
	"curl.exe", "-o", installer_store_to, installer_fetch_url, NULL,
#endif
	"wget", "-O", installer_store_to, installer_fetch_url, NULL,
#ifdef XEMU_ARCH_WIN
	"wget.exe", "-O", installer_store_to, installer_fetch_url, NULL,
#endif
	NULL	// the final stop of listing
};

static const char **downloader_utility_spec_start_p = downloader_utility_specifications;
static int downloader_utility_selected = 0;
static int legal_warning = 1;



static int download_file ( int size )
{
	int ret;
	char path_final[PATH_MAX];
	const char **execargs_p = downloader_utility_spec_start_p;
	struct stat st;
	strcpy(path_final, installer_store_to);
	strcat(installer_store_to, ".temp");
	DEBUGPRINT("INSTALLER: goal: %s -> %s -> %s (%d bytes)" NL,
		installer_fetch_url, installer_store_to, path_final, size
	);
	if (unlink(path_final) && errno != ENOENT) {
		ERROR_WINDOW("Installer: cannot delete already existing final file");
		return -1;
	}
	do {
		xemuexec_process_t proc;
		printf("Tryning: %s\n", *execargs_p);
		if (unlink(installer_store_to) && errno != ENOENT) {
			ERROR_WINDOW("Installer: cannot delete already exisiting temp file");
			return -1;
		}
		proc = xemuexec_run((char* const*)execargs_p);
		ret = xemuexec_check_status(proc, 1);
		printf("Exit status: %d\n", ret);
		if (!ret || downloader_utility_selected)
			break;
		while (*(execargs_p++))
			;
	} while (*execargs_p);
	if (ret) {
		ERROR_WINDOW("Installer: cannot download and/or no working download utility can be used");
		unlink(installer_store_to);
		return -1;
	}
	if (stat(installer_store_to, &st)) {
		ERROR_WINDOW("Installer: cannot stat file (not downloaded at all?)");
		return -1;
	}
	printf("File size = %d\n", (int)st.st_size);
	if (st.st_size != size) {
		unlink(installer_store_to);
		ERROR_WINDOW("Installer: downloaded file has wrong size (%d, wanted: %d)", (int)st.st_size, size);
		return -1;
	}
	if (rename(installer_store_to, path_final)) {
		ERROR_WINDOW("Installer: cannot rename to final");
		return -1;
	}
	if (!downloader_utility_selected) {
		downloader_utility_spec_start_p = execargs_p;
		downloader_utility_selected = 1;
		DEBUGPRINT("INSTALLER: setting \"%s\" as the default downloader utility for this session." NL, *execargs_p);
	}
	return 0;
}



static int download_file_by_db ( const char *filename, const char *storepath )
{
	char *p;
	int i;
	if (!xemu_installer_db)
		return -1;
	strcpy(installer_store_to, storepath);
	p = xemu_installer_db;
	i = strlen(filename);
	while (*p) {
		while (*p && *p <= 32)
			p++;
		if (!strncmp(filename, p, i) && (p[i] == '\t' || p[i] == ' ')) {
			p += i + 1;
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p > 32) {
				long int sizereq;
				char *q;
				if (strncasecmp(p, "http://", 7) && strncasecmp(p, "https://", 8) && strncasecmp(p, "ftp://", 6)) {
					ERROR_WINDOW("Bad download descriptor file at URL field (bar protocol) for record \"%s\"", filename);
					return -1;
				}
				q = installer_fetch_url;
				sizereq = 0;
				while (*p > 32) {
					if (sizereq == sizeof(installer_fetch_url) - 2) {
						ERROR_WINDOW("Bad download descriptor file at URL field (too long) for record \"%s\"", filename);
						return -1;
					}
					*q++ = *p++;
					sizereq++;
				}
				*q = 0;
				sizereq = strtol(p, &p, 0);
				if (*p > 32)
					sizereq = -1;
				if (sizereq > 0 && sizereq <= 4194304) {
					int ret;
					char msgbuffer[sizeof(installer_fetch_url) + 256];
					if (legal_warning) {
						INFO_WINDOW("Legal-warning blah-blah ...");
						legal_warning = 0;
					}
					sprintf(msgbuffer, "Downloading file \"%s\". Do you agree?\nSource: %s", filename, installer_fetch_url);
					if (QUESTION_WINDOW("YES|NO", msgbuffer))
						return -1;
					ret = download_file(sizereq);
					if (!ret)
						INFO_WINDOW("File %s seems to be downloaded nicely with %s", filename, *downloader_utility_spec_start_p);
					return ret;
				} else {
					ERROR_WINDOW("Bad download descriptor file at size field for record \"%s\" (or this file is not auto-installable)", filename);
					return -1;
				}
			}
		}
		while (*p && *p != '\n' && *p != '\r')
			p++;
	}
	DEBUGPRINT("INSTALLER: file-key %s cannot be found in the download description file" NL, filename);
	return -1;
}


void xemu_set_installer ( const char *filename )
{
	if (xemu_installer_db) {
		free(xemu_installer_db);
		xemu_installer_db = NULL;
	}
	if (filename) {
		int ret = xemu_load_file(filename, NULL, 32, 65535, "Specified installer-description file cannot be loaded");
		if (ret > 0) {
			xemu_installer_db = xemu_load_buffer_p;
			DEBUGPRINT("INSTALLER: description file loaded, %d bytes. Parsing will be done only on demand." NL, ret);
		}
	} else
		DEBUGPRINT("INSTALLER: not activated." NL);
}


// end of #ifdef HAVE_XEMU_INSTALLER
#endif

// end of #ifdef HAVE_XEMU_EXEC_API - else ...
#else

void xemuexec_open_native_file_browser ( char *dir )
{
	ERROR_WINDOW("Sorry, no execution API is supported by this Xemu build\nto allow to launch an OS-native file browser for you on directory:\n%s", dir);
}

// end of #ifdef HAVE_XEMU_EXEC_API - all
#endif




/** Open a file (probably with special search paths, see below), returning a file descriptor, or negative value in case of failure.
 *
 * Central part of file handling in Xemu.
 *
 * @param *filename
 *	name of the file
 *		- if it begins with '@' the file is meant to relative to the SDL preferences directory (ie: @thisisit.rom, no need for dirsep!)
 *		- if it begins with '#' the file is meant for 'data directory' which is probed then multiple places, depends on the OS as well
 *		  - Note: in this case, if installer is enabled and file not found, Xemu can try to download the file. For this, see above the "installer" part of this source
 *		- otherwise it's simply a file name, passed as-is
 * @param mode
 *	actually the flags parameter for open (O_RDONLY, etc)
 *		- O_BINARY is used automatically in case of Windows, no need to specify as input data
 *		- you can even use creating file effect with the right value here
 * @param *mode2
 *	pointer to an int, secondary open mode set
 *		- if it's NULL pointer, it won't be used ever
 *		- if it's not NULL, open() is also tried with the pointed flags for open() after trying (and failed!) open() with the 'mode' par
 *		- if mode2 pointer is not NULL, the pointed value will be zeroed by this func, if NOT *mode2 is used with successfull open
 *		- the reason for this madness: opening a disk image which neads to be read/write access, but also works for read-only, however
 *		  then the caller needs to know if the disk emulation is about r/w or ro only ...
 * @param *filepath_back
 *	if not null, actually tried path will be placed here (even in case of non-successfull call, ie retval is negative)
 *		- in case of multiple-path tries (# prefix) the first (so the most relevant, hopefully) is passed back
 *		- note: if no prefix (@ and #) the filename will be returned as is, even if didn't hold absolute path (only relative) or no path as all (both: relative to cwd)!
 *
 */
int xemu_open_file ( const char *filename, int mode, int *mode2, char *filepath_back )
{
	char paths[16][PATH_MAX];
	int a, max_paths;
	if (!filename)
		FATAL("Calling xemu_open_file() with NULL filename!");
	if (!*filename)
		FATAL("Calling xemu_open_file() with empty filename!");
	max_paths = 0;
#ifdef XEMU_ARCH_HTML
	sprintf(paths[max_paths++], "%s%s", EMSCRIPTEN_SDL_BASE_DIR, (filename[0] == '@' || filename[0] == '#') ? filename + 1 : filename);
#else
	if (*filename == '@') {
		sprintf(paths[max_paths++], "%s%s", sdl_pref_dir, filename + 1);
	} else if (*filename == '#') {
		sprintf(paths[max_paths++], "%s%s", sdl_inst_dir, filename + 1);
		sprintf(paths[max_paths++], "%s%s", sdl_pref_dir, filename + 1);
		sprintf(paths[max_paths++], "%srom" DIRSEP_STR "%s", sdl_base_dir, filename + 1);
		sprintf(paths[max_paths++], "%s%s", sdl_base_dir, filename + 1);
#ifndef XEMU_ARCH_WIN
		sprintf(paths[max_paths++], "/usr/local/share/xemu/%s", filename + 1);
		sprintf(paths[max_paths++], "/usr/local/lib/xemu/%s", filename + 1);
		sprintf(paths[max_paths++], "/usr/share/xemu/%s", filename + 1);
		sprintf(paths[max_paths++], "/usr/lib/xemu/%s", filename + 1);
#endif
#ifdef HAVE_XEMU_INSTALLER
		sprintf(paths[max_paths++], "%s%s%s", installer_marker_prefix, sdl_inst_dir, filename + 1);
#endif
	} else
		strcpy(paths[max_paths++], filename);
// End of #ifdef XEMU_ARCH_HTML
#endif
	a = 0;
	do {
		int fd;
		const char *filepath = paths[a];
#ifdef HAVE_XEMU_INSTALLER
		if (!strncmp(paths[a], installer_marker_prefix, strlen(installer_marker_prefix))) {
			filepath += strlen(installer_marker_prefix);
			download_file_by_db(filename + 1, filepath);
		}
#endif
		// Notes:
		//   1. O_BINARY is a windows stuff. However, since we define O_BINARY as zero for non-win archs, it's OK
		//   2. 0666 mask is needed as it can be also a creat() function basically ...
		fd = open(filepath, mode | O_BINARY, 0666);
		if (filepath_back)
			strcpy(filepath_back, filepath);
		if (fd >= 0) {
			if (mode2)
				*mode2 = 0;
			DEBUGPRINT("FILE: file %s opened as %s with base mode-set as fd=%d" NL, filename, paths[a], fd);
			return fd;
		}
		if (mode2) {
			fd = open(filepath, *mode2 | O_BINARY, 0666);	// please read the comments at the previous open(), above
			if (fd >= 0) {
				DEBUGPRINT("FILE: file %s opened as %s with *second* mode-set as fd=%d" NL, filename, paths[a], fd);
				return fd;
			}
		}
	} while (++a < max_paths);
	// if not found, copy the first try so the report to user is more sane
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


off_t xemu_safe_file_size_by_fd ( int fd )
{
	struct stat st;
	if (fstat(fd, &st))
		return OFF_T_ERROR;
	return st.st_size;
}


off_t xemu_safe_file_size_by_name ( const char *name )
{
	struct stat st;
	if (stat(name, &st))
		return OFF_T_ERROR;
	return st.st_size;
}

int xemu_safe_close ( int fd )
{
	return close(fd);
}


#ifdef XEMU_ARCH_WIN
#endif


int xemu_safe_open ( const char *fn, int flags )
{
	return open(fn, flags | O_BINARY);
}


int xemu_safe_open_with_mode ( const char *fn, int flags, int mode )
{
	return open(fn, flags | O_BINARY, mode);
}


int xemu_save_file ( const char *filename_in, void *data, int size, const char *cry )
{
	static const char temp_end[] = ".TMP";
	char filename[PATH_MAX];
	char filename_real[PATH_MAX];
	strcpy(filename, filename_in);
	strcat(filename, temp_end);
	int fd = xemu_open_file(filename, O_WRONLY | O_TRUNC | O_CREAT, NULL, filename_real);
	if (fd < 0) {
		if (cry)
			ERROR_WINDOW("%s\nCannot create file: %s\n%s", cry, filename, strerror(errno));
		return -1;
	}
	if (xemu_safe_write(fd, data, size) != size) {
		if (cry)
			ERROR_WINDOW("%s\nCannot write %d bytes into file: %s\n%s", cry, size, filename_real, strerror(errno));
		close(fd);
		unlink(filename_real);
		return -1;
	}
	close(fd);
	char filename_real2[PATH_MAX];
	strcpy(filename_real2, filename_real);
	filename_real2[strlen(filename_real2) - strlen(temp_end)] = 0;
	if (rename(filename_real, filename_real2)) {
		if (cry)
			ERROR_WINDOW("%s\nCannot rename file %s to %s\n%s", cry, filename_real, filename_real2, strerror(errno));
		unlink(filename_real);
		return -1;
	}
	return 0;
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


int xemu_create_sparse_file ( const char *os_path, Uint64 size )
{
	int err = 0, fd;
	unsigned char zero = 0;
	fd = open(os_path, O_BINARY | O_RDWR | O_CREAT, 0600);
	if (fd < 0)
		goto error;
	if (size > 0) {
		size--;
		if (lseek(fd, size, SEEK_SET) != size)
			goto error;
		if (write(fd, &zero, 1) != 1)
			goto error;
		if (lseek(fd, -1, SEEK_CUR) != size)
			goto error;
	}
	if (close(fd))
		goto error;
	return 0;
error:
	err = errno;
	if (fd >= 0)
		close(fd);
	unlink(os_path);
	return err ? err : -1;
}
