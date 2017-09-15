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
#include <sys/time.h>
#include <time.h>
#include <limits.h>
#include <errno.h>


void *xemu_load_buffer_p;
char  xemu_load_filepath[PATH_MAX];



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
		if (r < 0)	// I/O error on read
			return -1;
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
		if (w < 0)	// I/O error on write
			return -1;
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
