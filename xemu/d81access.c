/* Various D81 access method for F011 core, for Xemu / C65 and M65 emulators.
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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
#include "xemu/d81access.h"
#include "xemu/emutools_files.h"

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

static struct {
	int	fd;
	DIR	*dir;
	off_t	start_at;
	int	mode;
	// Only valid for PRG mode currently:
	int	prg_size;
	int	prg_blk_size;
	int	prg_blk_last_size;
	d81access_rd_cb_t read_cb;
	d81access_wr_cb_t write_cb;
} d81;
static int enable_mode_transient_callback = -1;


#define IS_RO(p)	(!!((p) & D81ACCESS_RO))
#define IS_RW(p)	(!((p) & D81ACCESS_RO))
#define HAS_DISK(p)	(((p)&& 0xFF) != D81ACCESS_EMPTY)
#define IS_AUTOCLOSE(p)	(!!((p) & D81ACCESS_AUTOCLOSE))


static const Uint8 vdsk_head_sect[] = {
	0x28, 0x03,
	0x44, 0x00,
	'X', 'E', 'M', 'U', ' ', 'V', 'R', '-', 'D', 'I', 'S', 'K', ' ', 'R', '/', 'O',
	0xA0, 0xA0,
	'6', '5',
	0xA0,
	0x33, 0x44, 0xA0, 0xA0
};
static const Uint8 vdsk_file_name[16] = {
	'F', 'I', 'L', 'E', 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0
};


void d81access_init ( void )
{
	DEBUGPRINT("D81: initial subsystem reset" NL);
	if (enable_mode_transient_callback != -1)
		FATAL("d81access_init(): trying to re-run d81access_init()?!");
	enable_mode_transient_callback = 1;
	d81.fd = -1;
	d81.dir = NULL;
	d81.start_at = 0;
	d81.mode = D81ACCESS_EMPTY;
	d81access_cb_chgmode(d81.mode);
}


int d81access_get_mode ( void )
{
	return d81.mode;
}


void d81access_close ( void )
{
	if (d81.fd >= 0) {
		if (IS_AUTOCLOSE(d81.mode)) {
			close(d81.fd);
			DEBUGPRINT("D81: previous file descriptor (%d) closed because of auto-close policy" NL, d81.fd);
		} else
			DEBUGPRINT("D81: previous file descriptor (%d) is NOT closed, because marked as non-autoclose!" NL, d81.fd);
		d81.fd = -1;
	}
	if (d81.dir) {
		closedir(d81.dir);
		DEBUGPRINT("D81: previous directory access closed" NL);
		d81.dir = NULL;
	}
	d81.mode = D81ACCESS_EMPTY;
	d81.start_at = 0;
	if (enable_mode_transient_callback)
		d81access_cb_chgmode(d81.mode);
}


static void d81access_close_internal ( void )
{
	enable_mode_transient_callback = 0;
	d81access_close();
	enable_mode_transient_callback = 1;
}


static void d81access_attach_fd_internal ( int fd, off_t offset, int mode )
{
	if (fd < 0)
		FATAL("d81access_attach_fd_internal() tries to attach invalid fd");
	d81access_close_internal();
	if (HAS_DISK(mode)) {
		d81.fd = fd;
		d81.mode = mode;
		DEBUGPRINT("D81: fd %d has been attached with " PRINTF_LLD " offset, read_only = %d, autoclose = %d" NL, fd, (long long)offset, IS_RO(mode), IS_AUTOCLOSE(mode));
	} else {
		DEBUGPRINT("D81: using empty access (no disk in drive) by request" NL);
		d81.mode = D81ACCESS_EMPTY;
	}
	d81.start_at = offset;
	d81access_cb_chgmode(d81.mode);
}


// this is used to attach external (to the d81access ...) file descriptor, not handled here to be opened as D81
// it's the caller's responsibility that it's really an FD for a D81 image in size etc enough for that!
// One example for this function to be used for: Mega65, on-SDCARD "mounted" D81, where the "master" fd is used
// to access the D81 inside, managed by the caller!
void d81access_attach_fd ( int fd, off_t offset, int mode )
{
	int check_mode = mode & 0xFF;
	if (check_mode != D81ACCESS_IMG && check_mode != D81ACCESS_EMPTY)
		FATAL("d81access_attach_fd() mode low bits must have D81ACCESS_IMG or D81ACCESS_EMPTY");
	d81access_attach_fd_internal(fd, offset, mode);
}


// Attach callbacks instead of handling requests in this source
void d81access_attach_cb ( off_t offset, d81access_rd_cb_t rd_callback, d81access_wr_cb_t wr_callback )
{
	d81access_close_internal();
	d81.mode = D81ACCESS_CALLBACKS;
	if (!wr_callback)
		d81.mode |= D81ACCESS_RO;
	d81.read_cb = rd_callback;
	d81.write_cb = wr_callback;
	d81.start_at = offset;
	DEBUGPRINT("D81: attaching D81 via provided callbacks, read=%p, write=%p" NL, rd_callback, wr_callback);
	d81access_cb_chgmode(d81.mode);
}


int d81access_attach_fsobj ( const char *fn, int mode )
{
	if (!fn || !*fn) {
		DEBUGPRINT("D81: attach file request with empty file name, not using FS based disk attachment." NL);
		return -1;
	}
	if (mode & D81ACCESS_DIR) {
		// if we passed D81ACCESS_DIR, we try to open the named object as a directory first.
		// if it was OK, let's containue with that
		// if not OK and error was ENOTDIR or ENOENT then simply assume to continue with other methods (not as directory).
		// this is because, the "fn" parameter can be a relative path too, later can be used with relative-to-preferences directory or so.
		// though directory opening is always absolute path what we're assuming.
		DIR *dir = opendir(fn);
		if (dir) {
			// It seems we could open the "raw" object as directory
			d81access_close_internal();
			d81.dir = dir;
			d81.mode = D81ACCESS_DIR | D81ACCESS_RO | D81ACCESS_AUTOCLOSE;	// TODO? directory access is always read only currently ...
			DEBUGPRINT("D81: file system object \"%s\" opened as a directory." NL, fn);
			d81access_cb_chgmode(d81.mode);
			return 0;
		} else if (errno != ENOTDIR && errno != ENOENT) {
			ERROR_WINDOW("D81: cannot open directory %s for virtual D81 mode: %s", fn, strerror(errno));
			return 1;
		}
	}
	// So, we can assume that the object should be a file ...
	if (!(mode & (D81ACCESS_IMG | D81ACCESS_PRG))) {
		if (mode & D81ACCESS_DIR)
			DEBUGPRINT("D81: could not open file system object \"%s\" as a directory.", fn);
		else
			FATAL("d81access_attach_fsobj(): insane mode argument, no DIR,IMG,PRG given");
		return 1;	// but if no success with directory open, and no request for file based attach, then we give up here
	}
	// OK, so some file based open request can happen, let's continue
	char fnbuf[PATH_MAX + 1];
	int ro, fd = xemu_open_file(fn, IS_RO(mode) ? O_RDONLY : O_RDWR, &ro, fnbuf);
	if (fd < 0) {
		ERROR_WINDOW("D81: image/program file was specified (%s) but it cannot be opened: %s", fn, strerror(errno));
		return 1;
	}
	ro = (IS_RO(mode) || ro) ? D81ACCESS_RO : 0;
	off_t size = xemu_safe_file_size_by_fd(fd);
	if (size == OFF_T_ERROR) {
		ERROR_WINDOW("D81: Cannot query the size of external D81 image/program file %s ERROR: %s", fn, strerror(errno));
		close(fd);
		return 1;
	}
	// Check if it's a PRG-file mode, based on the "sane" size of such a file ...
	if (size >= PRG_MIN_SIZE && size <= PRG_MAX_SIZE) {
		if (mode & D81ACCESS_PRG) {
			d81access_attach_fd_internal(fd, 0, D81ACCESS_PRG | D81ACCESS_AUTOCLOSE | D81ACCESS_RO);
			d81.prg_size = size;	// store real size of the object
			d81.prg_blk_size = size / 254;
			d81.prg_blk_last_size = size % 254;
			if (d81.prg_blk_last_size)
				d81.prg_blk_size++;
			else
				d81.prg_blk_last_size = 254;
			return 0;
		} else {
			close(fd);
			ERROR_WINDOW("Specified size for D81 seems to be a program file (too small for real D81), but PRG mode virtual disk feature was not requested");
			return 1;
		}
	}
	if (!(mode & D81ACCESS_IMG)) {
		close(fd);
		ERROR_WINDOW("D81 image mode was not requested ...");
		return 1;
	}
	// Only the possibility left that it's a D81 image
	if (size == D81_SIZE) {
		// candidate for the "normal" D81 as being used
		d81access_attach_fd_internal(fd, 0, D81ACCESS_IMG | D81ACCESS_AUTOCLOSE | ro);
		return 0;
	}
	close(fd);
	ERROR_WINDOW("Cannot guess the type of object (from its size) wanted to use for floppy emulation, sorry");
	return 1;
#if 0
	return 1;
		if (d81_size < PRG_MIN_SIZE) {	// the minimal size which is not treated as valid program file, and for sure, not D81 image either!
			ERROR_WINDOW("External PRG file tried to open as virtual-D81 but it's too short (" PRINTF_LLD " bytes) for %s, should be at least %d bytes!", (long long)d81_size, fnbuf, PRG_MIN_SIZE);
			close(d81fd);
			d81fd = -1;
			return d81fd;
		} else if (d81_size <= PRG_MAX_SIZE) {	// some random size at max which is treated as valid program file
			d81_is_prg = d81_size;	// we use the "d81_is_prg" flag to carry the file size as well
			// However we need the size in 254 bytes unit as well
			prg_blk_size = d81_size / 254;
			prg_blk_last_size = d81_size % 254;
			if (!prg_blk_last_size)
				prg_blk_last_size = 254;
			else
				prg_blk_size++;
			INFO_WINDOW("External file opened as a program file, guessed on its size (smaller than a D81). Size = %d, blocks = %d", d81_is_prg, prg_blk_size);
		} else if (d81_size != D81_SIZE) {
			ERROR_WINDOW("Bad external D81 image size " PRINTF_LLD " for %s, should be %d bytes!", (long long)d81_size, fnbuf, D81_SIZE);
			close(d81fd);
			d81fd = -1;
			return d81fd;
		}
		if (!d81_is_prg) {
			if (d81_is_read_only)
				INFO_WINDOW("External D81 image file %s could be open only in R/O mode", fnbuf);
			else
				DEBUG("SDCARD: exernal D81 image file re-opened in RD/WR mode, good" NL);
		}
	}
	return d81fd;
#endif
}



static int file_io_op ( int is_write, int d81_offset, Uint8 *buffer, int size )
{
	off_t offset = d81.start_at + (off_t)d81_offset;
	if (lseek(d81.fd, offset, SEEK_SET) != offset)
		FATAL("D81: SEEK: seek host-OS failure: %s", strerror(errno));
	int ret = is_write ? xemu_safe_write(d81.fd, buffer, size) : xemu_safe_read(d81.fd, buffer, size);
	if (ret >= 0)
		return ret;
	FATAL("D81: %s: host-OS error: %s", is_write ? "WRITE" : "READ", strerror(errno));
	return -1;
}


#if 0
static off_t host_seek_to ( Uint8 *addr_buffer, int addressing_offset, const char *description, off_t size_limit, int fd )
{
	off_t image_offset = (addr_buffer ? (((off_t)addr_buffer[0]) | ((off_t)addr_buffer[1] << 8) | ((off_t)addr_buffer[2] << 16) | ((off_t)addr_buffer[3] << 24)) : 0) + (off_t)addressing_offset;
	DEBUG("SDCARD: %s card at position " PRINTF_LLD " (offset=%d) PC=$%04X" NL, description, (long long)image_offset, addressing_offset, cpu65.pc);
	if (image_offset < 0 || image_offset > size_limit - 512) {
		DEBUGPRINT("SDCARD: SEEK: invalid offset requested for %s with offset " PRINTF_LLD " PC=$%04X" NL, description, (long long)image_offset, cpu65.pc);
		return -1;
	}
	if (lseek(fd, image_offset, SEEK_SET) != image_offset)
		FATAL("SDCARD: SEEK: image seek host-OS failure: %s", strerror(errno));
	return image_offset;
}



static int diskimage_read_block ( Uint8 *io_buffer, Uint8 *addr_buffer, int addressing_offset, const char *description, off_t size_limit, int fd )
{
	int ret;
	if (sdfd < 0)
		return -1;
	if (host_seek_to(addr_buffer, addressing_offset, description, size_limit, fd) < 0)
		return -1;
	ret = xemu_safe_read(fd, io_buffer, 512);
	if (ret != 512)
		FATAL("SDCARD: %s failure ... ERROR: %s", description, ret >= 0 ? "not 512 bytes could be read" : strerror(errno));
	DEBUG("SDCARD: cool, sector %s was OK (%d bytes read)!" NL, description, ret);
	return ret;
}



static int diskimage_write_block ( Uint8 *io_buffer, Uint8 *addr_buffer, int addressing_offset, const char *description, off_t size_limit, int fd )
{
	int ret;
	if (sdfd < 0)
		return -1;
	if (sd_is_read_only)
		return -1;
	if (host_seek_to(addr_buffer, addressing_offset, description, size_limit, fd) < 0)
		return -1;
	ret = xemu_safe_write(fd, io_buffer, 512);
	if (ret != 512)
		FATAL("SDCARD: %s failure ... ERROR: %s", description, ret >= 0 ? "not 512 bytes could be written" : strerror(errno));
	DEBUG("SDCARD: cool, sector %s was OK (%d bytes read)!" NL, description, ret);
	return ret;
}
#endif


static int read_prg ( Uint8 *buffer, int d81_offset, int number_of_logical_sectors )
{
	// just pre-zero buffer, so we don't need to take care on this at various code points with possible partly filled output
	memset(buffer, 0, 512);
	// disk organization at CBM-DOS level is 256 byte sector based, though FDC F011 itself is 512 bytes sectored stuff
	// so we always need to check to 256 bytes "DOS-evel" sectors even if F011 itself handled 512 bytes long sectors
	for (; number_of_logical_sectors; number_of_logical_sectors--, d81_offset += 0x100, buffer += 0x100) {
		DEBUGPRINT("D81VIRTUAL: reading sub-sector @ $%X" NL, d81_offset);
		if (d81_offset == 0x61800) {		// the header sector
			memcpy(buffer, vdsk_head_sect, sizeof vdsk_head_sect);
		} else if (d81_offset == 0x61900 || d81_offset == 0x61A00) {	// BAM sectors (we don't handle BAM entries at all, so it will be a filled disk ...)
			if (d81_offset == 0x61900) {
				buffer[0] = 0x28;
				buffer[1] = 0x02;
			} else
				buffer[1] = 0xFF;	// chain, byte #0 is already 0
			buffer[2] = 0x44;
			buffer[3] = 0xBB;
			buffer[4] = vdsk_head_sect[0x16];
			buffer[5] = vdsk_head_sect[0x17];
			buffer[6] = 0xC0;
		} else if (d81_offset == 0x61B00) {	// directory sector, the only one we want to handle here
			buffer[2] = 0x82;	// PRG
			buffer[3] = 0x01;	// starts on track-1
			// starts on sector-0 of track-1, 0 is already set
			memcpy(buffer + 5, vdsk_file_name, 16);
			buffer[0x1E] = d81.prg_blk_size & 0xFF;
			buffer[0x1F] = d81.prg_blk_size >> 8;
		} else {		// what we want to handle at all yet, is the file itself, which starts at the very beginning at our 'virtual' disk
			int block = d81_offset >> 8;	// calculate the block from offset
			if (block < d81.prg_blk_size) {	// so it seems, we need to do something here at last, disk area belongs to our file!
				int reqsize, ret;
				if (block == d81.prg_blk_size -1) {   // last block of file
					reqsize = d81.prg_blk_last_size;
					buffer[1] = 0xFF;	// offs 0 is already 0
				} else {				// not the last block, we must resolve the track/sector info of the next block
					reqsize = 254;
					buffer[0] = ((block + 1) / 40) + 1;
					buffer[1] = (block + 1) % 40;
				}
				ret = file_io_op(0, block * 254, buffer + 2, reqsize);
				DEBUGPRINT("D81VIRTUAL: ... data block, block number %d, next_track = $%02X next_sector = $%02X" NL, block, buffer[0], buffer[1]);
#if 0
				if (host_seek_to(NULL, block * 254, "reading[PRG81VIRT@HOST]", d81_is_prg + 512, d81fd) < 0)
					return -1;
				block = xemu_safe_read(d81fd, buffer + 2, reqsize);
#endif
				DEBUGPRINT("D81VIRTUAL: ... reading result: expected %d retval %d" NL, reqsize, ret);
				if (ret != reqsize)
					return -1;
			} // if it's not our block of the file, not BAMs, header block or directory, the default zeroed area is returned, what we memset()'ed to zero
		}
	}
	return 0;
}


static void check_io_req_params ( int d81_offset, int sector_size )
{
	if (XEMU_UNLIKELY(sector_size != 0x100 && sector_size != 0x200))
		FATAL("d81access: check_io_req_params(): invalid sector size %d", sector_size);
	if (XEMU_UNLIKELY(d81_offset < 0 || (d81_offset % sector_size) || d81_offset > D81_SIZE - sector_size))
		FATAL("d81access: check_io_req_params(): invalid offset %d", d81_offset);
}


int d81access_read_sect  ( Uint8 *buffer, int d81_offset, int sector_size )
{
	check_io_req_params(d81_offset, sector_size);
	switch (d81.mode & 0xFF) {
		case D81ACCESS_EMPTY:
			return -1;
		case D81ACCESS_IMG:
			if (file_io_op(0, d81_offset, buffer, sector_size) == sector_size)
				return 0;
			else
				return -1;
		case D81ACCESS_PRG:
			return read_prg(buffer, d81_offset, sector_size >> 8);
		case D81ACCESS_DIR:
			FATAL("DIR access method is not yet implemented in Xemu, sorry :-(");
		case D81ACCESS_CALLBACKS:
			return d81.read_cb(buffer, d81.start_at + d81_offset, sector_size);
		default:
			FATAL("d81access_read_sect(): invalid d81.mode & 0xFF");
	}
	return -1;
}


int d81access_write_sect ( Uint8 *buffer, int d81_offset, int sector_size )
{
	check_io_req_params(d81_offset, sector_size);
	if (IS_RO(d81.mode))
		return -1;
	switch (d81.mode & 0xFF) {
		case D81ACCESS_EMPTY:
			return -1;
		case D81ACCESS_IMG:
			if (file_io_op(1, d81_offset, buffer, sector_size) == sector_size)
				return 0;
			else
				return -1;
		case D81ACCESS_PRG:
		case D81ACCESS_DIR:
			return -1;	// currently, these are all read-only, even if caller forgets that and try :-O
		case D81ACCESS_CALLBACKS:
			return (d81.write_cb ? d81.write_cb(buffer, d81.start_at + d81_offset, sector_size) : -1);
		default:
			FATAL("d81access_write_sect(): invalid d81.mode & 0xFF");
	}
	return -1;
}
