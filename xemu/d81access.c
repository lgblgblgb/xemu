/* Various D81 access method for F011 core, for Xemu / C65 and M65 emulators.
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
	int	image_size;
	//d81access_rd_cb_t read_cb;
	//d81access_wr_cb_t write_cb;
	// Only valid for PRG mode currently:
	int	prg_size;
	int	prg_blk_size;
	int	prg_blk_last_size;
} d81[8];
static int enable_mode_transient_callback = -1;

#define IS_RO(p)	(!!((p) & D81ACCESS_RO))
#define IS_RW(p)	(!((p) & D81ACCESS_RO))
#define HAS_DISK(p)	(((p)&& 0xFF) != D81ACCESS_EMPTY)
#define IS_AUTOCLOSE(p)	(!!((p) & D81ACCESS_AUTOCLOSE))


void d81access_init ( void )
{
	DEBUGPRINT("D81: initial subsystem reset" NL);
	if (enable_mode_transient_callback != -1)
		FATAL("d81access_init(): trying to re-run d81access_init()?!");
	enable_mode_transient_callback = 1;
	for (int i = 0; i < 8; i++) {
		d81[i].fd = -1;
		d81[i].dir = NULL;
		d81[i].start_at = 0;
		d81[i].mode = D81ACCESS_EMPTY;
		d81access_cb_chgmode(i, d81[i].mode);
	}
}


int d81access_get_mode ( const int which )
{
	return d81[which].mode;
}


int d81access_get_size ( const int which )
{
	return d81[which].image_size;
}


void d81access_close ( const int which )
{
	if (d81[which].fd >= 0) {
		if (IS_AUTOCLOSE(d81[which].mode)) {
			close(d81[which].fd);
			DEBUGPRINT("D81: previous file descriptor (%d) closed because of auto-close policy" NL, d81[which].fd);
		} else
			DEBUGPRINT("D81: previous file descriptor (%d) is NOT closed, because marked as non-autoclose!" NL, d81[which].fd);
		d81[which].fd = -1;
	}
	if (d81[which].dir) {
		closedir(d81[which].dir);
		DEBUGPRINT("D81: previous directory access closed" NL);
		d81[which].dir = NULL;
	}
	d81[which].mode = D81ACCESS_EMPTY;
	d81[which].start_at = 0;
	if (enable_mode_transient_callback)
		d81access_cb_chgmode(which, d81[which].mode);
}


void d81access_close_all ( void )
{
	for (int i = 0; i < 8; i++)
		d81access_close(i);
}


static void d81access_close_internal ( int which )
{
	enable_mode_transient_callback = 0;
	d81access_close(which);
	enable_mode_transient_callback = 1;
}


static void d81access_attach_fd_internal ( int which, int fd, off_t offset, int mode )
{
	if (fd < 0)
		FATAL("d81access_attach_fd_internal() tries to attach invalid fd");
	d81access_close_internal(which);
	if (HAS_DISK(mode)) {
		d81[which].fd = fd;
		d81[which].mode = mode;
		d81[which].image_size = D81_SIZE;	// the default size of the image ...
		// ... override based on possible options, if image size is different:
		if ((mode & D81ACCESS_D64))
			d81[which].image_size = D64_SIZE;
		if ((mode & D81ACCESS_D71))
			d81[which].image_size = D71_SIZE;
		if ((mode & D81ACCESS_D65))
			d81[which].image_size = D65_SIZE;
		DEBUGPRINT("D81: fd %d has been attached to #%d with " PRINTF_LLD " offset, read_only = %d, autoclose = %d, size = %d" NL, fd, which, (long long)offset, IS_RO(mode), IS_AUTOCLOSE(mode), d81[which].image_size);
	} else {
		DEBUGPRINT("D81: using empty access (no disk in drive) by request" NL);
		d81[which].mode = D81ACCESS_EMPTY;
	}
	d81[which].start_at = offset;
	d81access_cb_chgmode(which, d81[which].mode);
}


// this is used to attach external (to the d81access ...) file descriptor, not handled here to be opened as D81
// it's the caller's responsibility that it's really an FD for a D81 image in size etc enough for that!
// One example for this function to be used for: MEGA65, on-SDCARD "mounted" D81, where the "master" fd is used
// to access the D81 inside, managed by the caller!
void d81access_attach_fd ( int which, int fd, off_t offset, int mode )
{
	int check_mode = mode & 0xFF;
	if (check_mode != D81ACCESS_IMG && check_mode != D81ACCESS_EMPTY)
		FATAL("d81access_attach_fd() mode low bits must have D81ACCESS_IMG or D81ACCESS_EMPTY");
	d81access_attach_fd_internal(which, fd, offset, mode);
}


// FIXME: this API function is removed, since does not provide size information. Since currently it's not used,
// either remove it in the future, or refactor it!
#if 0
// Attach callbacks instead of handling requests in this source
void d81access_attach_cb ( int which, off_t offset, d81access_rd_cb_t rd_callback, d81access_wr_cb_t wr_callback )
{
	d81access_close_internal(which);
	d81[which].mode = D81ACCESS_CALLBACKS;
	if (!wr_callback)
		d81[which].mode |= D81ACCESS_RO;
	d81[which].read_cb = rd_callback;
	d81[which].write_cb = wr_callback;
	d81[which].start_at = offset;
	DEBUGPRINT("D81: attaching D81 via provided callbacks, read=%p, write=%p" NL, rd_callback, wr_callback);
	d81access_cb_chgmode(which, d81[which].mode);
}
#endif


int d81access_attach_fsobj ( int which, const char *fn, int mode )
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
			d81access_close_internal(which);
			d81[which].dir = dir;
			d81[which].mode = D81ACCESS_DIR | D81ACCESS_RO | D81ACCESS_AUTOCLOSE;	// TODO? directory access is always read only currently ...
			d81[which].image_size = D81_SIZE;
			DEBUGPRINT("D81: file system object \"%s\" opened as a directory." NL, fn);
			d81access_cb_chgmode(which, d81[which].mode);
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
	int ro = O_RDONLY;
	int fd = xemu_open_file(fn, IS_RO(mode) ? O_RDONLY : O_RDWR, IS_RO(mode) ? NULL : &ro, fnbuf);
	ro = (IS_RO(mode) || ro != XEMU_OPEN_FILE_FIRST_MODE_USED) ? D81ACCESS_RO : 0;
	if (fd < 0) {
		ERROR_WINDOW("D81: image/program file was specified (%s) but it cannot be opened: %s", fn, strerror(errno));
		return 1;
	}
	off_t size = xemu_safe_file_size_by_fd(fd);
	if (size == OFF_T_ERROR) {
		ERROR_WINDOW("D81: Cannot query the size of external D81 image/program file %s ERROR: %s", fn, strerror(errno));
		close(fd);
		return 1;
	}
	// Check if it's a PRG-file mode, based on the "sane" size of such a file ...
	if (size >= PRG_MIN_SIZE && size <= PRG_MAX_SIZE) {
		if (mode & D81ACCESS_PRG) {
			d81access_attach_fd_internal(which, fd, 0, D81ACCESS_PRG | D81ACCESS_AUTOCLOSE | D81ACCESS_RO);
			d81[which].prg_size = size;	// store real size of the object
			d81[which].prg_blk_size = size / 254;
			d81[which].prg_blk_last_size = size % 254;
			if (d81[which].prg_blk_last_size)
				d81[which].prg_blk_size++;
			else
				d81[which].prg_blk_last_size = 254;
			d81[which].image_size = D81_SIZE;
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
	// Fake-D64 ... If requested + allowed at all.
	if (size == D64_SIZE && (mode & D81ACCESS_FAKE64)) {
		// Set D81ACCESS_RO! As this is only a read-only hack mode!!
		d81access_attach_fd_internal(which, fd, 0, D81ACCESS_IMG | D81ACCESS_AUTOCLOSE | D81ACCESS_FAKE64 | D81ACCESS_RO);
		return 0;
	}
	// D64 mounted as a D81 - via ROM support
	if (size == D64_SIZE && (mode & D81ACCESS_D64)) {
		d81access_attach_fd_internal(which, fd, 0, D81ACCESS_IMG | D81ACCESS_AUTOCLOSE | D81ACCESS_D64 | ro);
		return 0;
	}
	// D71 mounted as a D81 - via ROM support
	if (size == D71_SIZE && (mode & D81ACCESS_D71)) {
		d81access_attach_fd_internal(which, fd, 0, D81ACCESS_IMG | D81ACCESS_AUTOCLOSE | D81ACCESS_D71 | ro);
		return 0;
	}
	// D65 mounted as a D81
	if (size == D65_SIZE && (mode & D81ACCESS_D65)) {
		d81access_attach_fd_internal(which, fd, 0, D81ACCESS_IMG | D81ACCESS_AUTOCLOSE | D81ACCESS_D65 | ro);
		return 0;
	}
	// Only the possibility left that it's a D81 image
	if (size == D81_SIZE) {
		// candidate for the "normal" D81 as being used
		d81access_attach_fd_internal(which, fd, 0, D81ACCESS_IMG | D81ACCESS_AUTOCLOSE | ro);
		return 0;
	}
	close(fd);
	ERROR_WINDOW("Cannot guess the type of object (from its size) wanted to use for floppy emulation");
	return 1;
}


static int file_io_op ( const int which, const int is_write, const int image_offset, Uint8 *buffer, const int size )
{
	off_t offset = d81[which].start_at + (off_t)image_offset;
	if (lseek(d81[which].fd, offset, SEEK_SET) != offset)
		FATAL("D81: SEEK: seek host-OS failure: %s", strerror(errno));
	const int ret = is_write ? xemu_safe_write(d81[which].fd, buffer, size) : xemu_safe_read(d81[which].fd, buffer, size);
	if (ret >= 0)
		return ret;
	FATAL("D81: %s: host-OS error: %s", is_write ? "WRITE" : "READ", strerror(errno));
	return -1;
}


static int read_fake64 ( const int which, Uint8 *buffer, int d81_offset, int number_of_logical_sectors )
{
	for (; number_of_logical_sectors; number_of_logical_sectors--, d81_offset += 0x100, buffer += 0x100) {
		int track  =  d81_offset / 0x2800 + 1;	// Calculate D81 requested track number (starting from 1)
		int sector = (d81_offset % 0x2800) >> 8;// Calculate D81 requested sector number, in _256_ bytes long sectors though! (starting from 0)
		if (track == 18) {
			memset(buffer, 0, 0x100);
			DEBUGPRINT("D81: FAKE64: D81 track 18 tried to be read, which would be the D64 dir/sys track. Ignoring!" NL);
			continue;
		}
		if (track == 40)	// track 40 on D81 is the directory. We replace that with track 18 on the D64 (some workarounds still needed to be applied later, possibly)
			track = 18;
		if (!track || track > 35) {	// not existing track on D64
			memset(buffer, 0, 0x100);
			DEBUGPRINT("D81: FAKE64: invalid track for D64 %d" NL, track);
			continue;
		}
		// Resolve number of sectors on D64 given by the track (unlike D81, it's not a constant!)
		// Also work out the base byte offset of the track in the D64 image ("sector * 256" should be added later)
		int d64_max_sectors, d64_track_ofs;
		if (track <= 17) {		// D64 tracks  1-17
			d64_track_ofs   = 21 * 256 * (track -  1) + 0x00000;
			d64_max_sectors = 21;
		} else if (track <= 24) {	// D64 tracks 18-24
			d64_track_ofs   = 19 * 256 * (track - 18) + 0x16500;
			d64_max_sectors = 19;
		} else if (track <= 30) {	// D64 tracks 25-30
			d64_track_ofs   = 18 * 256 * (track - 25) + 0x1EA00;
			d64_max_sectors = 18;
		} else {			// D64 tracks 31-35
			d64_track_ofs   = 17 * 256 * (track - 31) + 0x25600;
			d64_max_sectors = 17;
		}
		if (sector >= d64_max_sectors) {	// requested sector does not exist on D64 on the given track
			memset(buffer, 0, 0x100);
			DEBUGPRINT("D81: FAKE64: invalid sector for D64 %d on track %d" NL, sector, track);
			continue;
		}
		// This is now checks for 18, as we already translated our 40 to 18 as the directory track!!
		int sector_to_read;
		if (track == 18) {
			if (sector >= 3)
				sector_to_read = sector - 2;
			else
				sector_to_read = 0;	// for D81 sectors 0,1,2 we always want to read sector 0 of D64 to extract the needed information from there!
		} else
			sector_to_read = sector;
		if (track == 18 || sector_to_read != sector)
			DEBUGPRINT("D81: FAKE64: translated to D64 track:sector %d:%d from the orginal requested %d:%d" NL, track, sector_to_read, track == 18 ? 40 : track, sector);
		if (file_io_op(which, 0, d64_track_ofs + sector_to_read * 256, buffer, 0x100) != 0x100) {
			DEBUGPRINT("D81: FAKE64: read failed!" NL);
			return -1;
		}
		// This is now checks for 18, as we already translated our 40 to 18 as the directory track!!
		if (track == 18) {
			if (sector == 0) {
				buffer[0] = 40;		// next track
				buffer[1] = 3;		// next sector
				buffer[2] = 0x44;	// 'D': DOS version, this is for 1581
				buffer[3] = 0;
				memcpy(buffer + 4, buffer + 0x90, 16);	// 0x4 - 0x13: disk name, on D64 that is from offset $90
				buffer[0x14] = 0xA0;
				buffer[0x15] = 0xA0;
				buffer[0x16] = buffer[0xA2];	// disk ID
				buffer[0x17] = buffer[0xA3];	// disk ID
				buffer[0x18] = 0xA0;
				buffer[0x19] = 0x33;	// DOS version: '3'
				buffer[0x1A] = 0x44;	// DOS version: 'D'
				buffer[0x1B] = 0xA0;
				buffer[0x1C] = 0xA0;
				memset(buffer + 0x1D, 0, 0x100 - 0x1D);
			} else if (sector == 1 || sector == 2) {
				// these two sectors are the BAM on D81 among some other information (partly the same as with sector 0)
				Uint8 obuffer[0x100];
				memcpy(obuffer, buffer, 0x100);	// save these, as we overwrite the original values ...
				memset(buffer, 0, 0x100);
				if (sector == 1) {	// sector 1, first BAM
					buffer[0] = 40;
					buffer[1] =  2;
					for (int tf0 = 0; tf0 < 35; tf0++) {
						Uint8 *obam = obuffer + 4 + (4 * tf0);
						Uint8 *nbam = buffer + 0x10 + (6 * tf0);
						if (tf0 + 1 != 18)	// tf0 -> "track from zero" (non std method), skip the directory/system track (memset above already did the trick)
							memcpy(nbam, obam, 4);
					}
				} else {		// sector 2, second BAM (not used, as all the D64 tracks fits into the first part of the BAM ...)
					buffer[0] = 0x00;
					buffer[1] = 0xFF;
				}
				buffer[2] = 0x44;	// 'D': DOS version
				buffer[3] = 0xBB;	// one's complement of the version ...
				buffer[4] = obuffer[0xA2];	// disk ID, same as in sector 0
				buffer[5] = obuffer[0xA3];	// disk ID, same as in sector 0
				buffer[6] = 0xC0;	// flags [it seems it's usually $C0?]
				buffer[7] = 0;		// autoboot?
			} else if (buffer[0] == 18) {	// fix chain track/sector in the buffer to have some meaningful values for D81 context on track 40 (vs track 18 on D64)
				buffer[0] = 40;
				if (buffer[1] > 0 && buffer[1] < 19)
					buffer[1] += 2;
			}
		}
	}
	return 0;
}


static int read_prg ( const int which, Uint8 *buffer, int d81_offset, int number_of_logical_sectors )
{
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
			buffer[0x1E] = d81[which].prg_blk_size & 0xFF;
			buffer[0x1F] = d81[which].prg_blk_size >> 8;
			memcpy(buffer + 0x22, buffer + 2, 0x20 - 2);	// the next dir entry is the same, BUT:
			buffer[0x22] = 0x81;				// ... SEQ prg type
			buffer[0x29] = 'S';				// ... "SEQ" after name "FILE", so we have "FILE" (PRG) and "FILESEQ" (SEQ)
			buffer[0x2A] = 'E';
			buffer[0x2B] = 'Q';
		} else {		// what we want to handle at all yet, is the file itself, which starts at the very beginning at our 'virtual' disk
			int block = d81_offset >> 8;	// calculate the block from offset
			if (block < d81[which].prg_blk_size) {	// so it seems, we need to do something here at last, disk area belongs to our file!
				int reqsize, ret;
				if (block == d81[which].prg_blk_size -1) {   // last block of file
					reqsize = d81[which].prg_blk_last_size;
					buffer[1] = 0xFF;	// offs 0 is already 0
				} else {				// not the last block, we must resolve the track/sector info of the next block
					reqsize = 254;
					buffer[0] = ((block + 1) / 40) + 1;
					buffer[1] = (block + 1) % 40;
				}
				ret = file_io_op(which, 0, block * 254, buffer + 2, reqsize);
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


// Notes:
// * all calculations are based on 512 bytes sector, "sector_size" parameter is merely is a read size not the actual size of the sector
// * sectors per track meant as on one side only!

static int check_io_req_params ( const int which, const Uint8 side, const Uint8 track, const Uint8 sector, const int sector_size, int *io_size_p )
{
	if (XEMU_UNLIKELY(sector_size != 0x100 && sector_size != 0x200))
		FATAL("D81ACCESS: check_io_req_params(): invalid sector size %d", sector_size);
	if (XEMU_UNLIKELY(sector == 0)) {
		DEBUGPRINT("D81ACCESS: warning, trying file op on sector-0 within track %d" NL, track);
		return -1;
	}
	int offset; //, num_of_sides, num_of_tracks, num_of_sectors;
	// NOTE: MEGA65 D71 and D64 access works as leaving D81 calculation as-is, and from software you need to do the geometry conversion, so not the scope of the emulator
	//       (this also means that a D64/D71 can be addressed beyond the end of the disk image, it needs a final check on offset not only geometry info limitations!)
	//       However, D65 has its own geometry calulcation at hardware level!
	if (XEMU_UNLIKELY(d81[which].mode & D81ACCESS_D65)) {
		// FIXME: no idea about the exact values for D65, or the formula :(
		offset = (((const int)track << 7) + (sector - 1)) * 512;
		//num_of_sides = 2;
		//num_of_tracks = 85;
		//num_of_sectors = 64;	// (sectors by SIDE ...)
	} else {
		offset = 40 * (track - 0) * 256 + (sector - 1) * 512 + side * 20 * 256;	// somewhat experimental value, it was after I figured out how that should work :-P
		//num_of_sides = 2;
		//num_of_tracks = 80;
		//num_of_sectors = 10;	// (sectors by SIDE ...)
	}
	// FIXME: I have no idea why it does not work. It seems, some software wants to access sector 17 and such,
	// which does not exist (only 10 sectors per side). By deactivating these checks, everything seems to work again :-O
#if 0
	// Check disk geometry limit (note about the D64/D71 comment above, those are handled internally as D81 disk geometry!)
	if (XEMU_UNLIKELY(num_of_sides != -1 && side >= num_of_sides)) {	// num_of_sides = -1 --> do not check side info
		DEBUGPRINT("D81ACCESS: trying to access non-existing side (%d)" NL, side);
		return -1;
	}
	if (XEMU_UNLIKELY(track >= num_of_tracks)) {
		DEBUGPRINT("D81ACCESS: trying to access non-existing track (%d)" NL, track);
		return -1;
	}
	if (XEMU_UNLIKELY(sector > num_of_sectors || sector == 0)) {		// sector numbering starts with ONE, not with zero!
		DEBUGPRINT("D81ACCESS: trying to access non-exisiting sector (%d) on track %d" NL, sector, track);
		return -1;
	}
#endif
	// Check offset limit
	if (XEMU_UNLIKELY(offset < 0 || offset >= d81[which].image_size)) {
		DEBUGPRINT("D81ACCESS: trying to R/W beyond the end of disk image (offset %d, size %d)" NL, offset, d81[which].image_size);
		return -1;
	}
	// Check "partial" I/O, can happen with a disk image like D64 is accessed as a D81 and D64 image size is not divisable by 512, only by 256
	const int io_size = d81[which].image_size - offset;
	if (XEMU_UNLIKELY(io_size < sector_size)) {
		DEBUGPRINT("D81ACCESS: partial R/W with %d bytes" NL, io_size);
		if (io_size != 256) {
			// this should not happen though ...
			ERROR_WINDOW("Partial R/W on D81 access which is not 256 byte in length but %d (at offset %d, image size is %d bytes)!", io_size, offset, d81[which].image_size);
			return -1;
		}
		*io_size_p = io_size;
	} else
		*io_size_p = sector_size;
	return offset;
}


int d81access_read_sect_raw ( const int which, Uint8 *buffer, const int offset, const int sector_size, const int io_size )
{
	switch (d81[which].mode & 0xFF) {
		case D81ACCESS_EMPTY:
			return -1;
		case D81ACCESS_IMG:
			if (XEMU_UNLIKELY(d81[which].mode & D81ACCESS_FAKE64)) {
				return read_fake64(which, buffer, offset, sector_size >> 8);
			} else {
				// we fill the buffer if partial read is done to have consistent "tail"
				if (XEMU_UNLIKELY(io_size != sector_size))
					memset(buffer, 0xFF, sector_size);
				return file_io_op(which, 0, offset, buffer, io_size) == io_size ? 0 : -1;
			}
		case D81ACCESS_PRG:
			return read_prg(which, buffer, offset, sector_size >> 8);
		case D81ACCESS_DIR:
			FATAL("D81ACCESS: DIR access method is not yet implemented in Xemu, sorry :-(");
		case D81ACCESS_CALLBACKS:
			FATAL("D81: D81ACCESS_CALLBACKS is not implemented!");
			//return d81[which].read_cb(which, buffer, d81[which].start_at + offset, sector_size);
		default:
			FATAL("D81ACCESS: d81access_read_sect(): invalid value for 'd81[%d].mode & 0xFF' = %d", which, d81[which].mode & 0xFF);
	}
	FATAL("D81ACCESS: d81access_read_sect() unhandled case" NL);
	return -1;
}


int d81access_read_sect  ( const int which, Uint8 *buffer, const Uint8 side, const Uint8 track, const Uint8 sector, const int sector_size )
{
	int io_size;
	const int offset = check_io_req_params(which, side, track, sector, sector_size, &io_size);
	if (XEMU_UNLIKELY(offset < 0))
		return offset;	// return negative number as error
	return d81access_read_sect_raw(which, buffer, offset, sector_size, io_size);
}


int d81access_write_sect_raw ( const int which, Uint8 *buffer, const int offset, const int sector_size, const int io_size )
{
	if (IS_RO(d81[which].mode))
		return -1;
	switch (d81[which].mode & 0xFF) {
		case D81ACCESS_EMPTY:
			return -1;
		case D81ACCESS_IMG:
			return file_io_op(which, 1, offset, buffer, io_size) == io_size ? 0 : -1;
		case D81ACCESS_PRG:
		case D81ACCESS_DIR:
			return -1;	// currently, these are all read-only, even if caller forgets that and try :-O
		case D81ACCESS_CALLBACKS:
			FATAL("D81: D81ACCESS_CALLBACKS is not implemented!");
			//return (d81[which].write_cb ? d81[which].write_cb(which, buffer, d81[which].start_at + offset, sector_size) : -1);
		default:
			FATAL("D81ACCESS: d81access_write_sect(): invalid d81[%d].mode & 0xFF", which);
	}
	FATAL("D81ACCESS: d81access_write_sect() unhandled case" NL);
	return -1;
}


int d81access_write_sect ( const int which, Uint8 *buffer, const Uint8 side, const Uint8 track, const Uint8 sector, const int sector_size )
{
	int io_size;
	const int offset = check_io_req_params(which, side, track, sector, sector_size, &io_size);
	if (XEMU_UNLIKELY(offset < 0))
		return offset;	// return negative number as error
	return d81access_write_sect_raw(which, buffer, offset, sector_size, io_size);
}


// Notes:
// * if img is NULL, disk image is malloc()'ed and the caller should free() the space
// * return value is the actual image, either the same as img as input, or the malloc()'ed address if img was NULL
// * if name_from_fn is non-zero, then this function tries some heuristics to form a "sane" disk name from a win/unix/etc file path given
//   otherwise diskname is used as-is (other than converting case, etc)
Uint8 *d81access_create_image ( Uint8 *img, const char *diskname, const int name_from_fn )
{
	if (!img)
		img = xemu_malloc(D81_SIZE);
	static const char diskname_default[] = "XEMU-NAMELESS";
	if (!diskname)
		diskname = diskname_default;
	static const Uint8 mods_at_61800[] = {
#if 0
		/* 61800 */ 0x28,0x03,0x44,0x00,0x44,0x45,0x4d,0x4f,0x45,0x4d,0x50,0x54,0x59,0xa0,0xa0,0xa0,	/* | (.D.DEMOEMPTY...| */
#endif
		/* 61800 */ 0x28,0x03,0x44,0x00,0xa0,0xa0,0xa0,0xa0,0xa0,0xa0,0xa0,0xa0,0xa0,0xa0,0xa0,0xa0,	/* | (.D.............| */
		/* 61810 */ 0xa0,0xa0,0xa0,0xa0,0xa0,0xa0,0x30,0x30,0xa0,0x33,0x44,0xa0,0xa0			/* | ......00.3D.....| */
	};
	static const Uint8 mods_at_61900[] = {
		/* 61900 */ 0x28,0x02,0x44,0xbb,0x30,0x30,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	/* | (.D.00..........| */
		/* 61910 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
		/* 61920 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
		/* 61930 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
		/* 61940 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
		/* 61950 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
		/* 61960 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
		/* 61970 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
		/* 61980 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
		/* 61990 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
		/* 619a0 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
		/* 619b0 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
		/* 619c0 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
		/* 619d0 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
		/* 619e0 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
		/* 619f0 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x24,0xf0,0xff,0xff,0xff,0xff,	/* | ....(.....$.....| */
		/* 61a00 */ 0x00,0xff,0x44,0xbb,0x30,0x30,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	/* | ..D.00..........| */
		/* 61a10 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
		/* 61a20 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
		/* 61a30 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
		/* 61a40 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
		/* 61a50 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
		/* 61a60 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
		/* 61a70 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
		/* 61a80 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
		/* 61a90 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
		/* 61aa0 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
		/* 61ab0 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
		/* 61ac0 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
		/* 61ad0 */ 0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,	/* | (.....(.....(...| */
		/* 61ae0 */ 0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,	/* | ..(.....(.....(.| */
		/* 61af0 */ 0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,0x28,0xff,0xff,0xff,0xff,0xff,	/* | ....(.....(.....| */
		/* 61b00 */ 0x00,0xff										/* | ................| */
	};
	memset(img, 0, D81_SIZE);
	memcpy(img + 0x61800, mods_at_61800, sizeof mods_at_61800);
	memcpy(img + 0x61900, mods_at_61900, sizeof mods_at_61900);
	int namelen;
	if (name_from_fn) {
		// in this mode, we want to generate diskname from a filename, so additional rules should be applied
		if (diskname[0] == '@' || diskname[0] == '#')
			diskname++;		// skip first char if '@' or '#' since it's used by Xemu to signal pref/base dir expansion
		const char *r = strrchr(diskname, DIRSEP_CHR);
		if (r)
			diskname = r + 1;	// skip the path part, and use the filename part only, if dirsep character is found at least
		namelen = strlen(diskname);
		if (namelen > 4 && !strcasecmp(diskname + namelen - 4, ".D81"))
			namelen -= 4;		// do not use the ".D81" part at the end in the disk name
	} else
		namelen = strlen(diskname);
	if (namelen > 16) {
		namelen = 16;
	} else if (!namelen) {
		diskname = diskname_default;
		namelen = strlen(diskname_default);
	}
	unsigned int diskid = namelen + ((namelen + 1) << 8);
	DEBUGPRINT("D81ACCESS: creating memory image of a new D81 disk \"");
	for (unsigned int i = 0; i < namelen; i++) {
		Uint8 c = (Uint8)diskname[i];
		diskid += (unsigned int)c + (i << 5);
		if (c >= 'a' && c <= 'z')
			c -= 32;
		else if (c < 32 || c >= 0x7F)
			c = '?';
		img[0x61804 + i] = c;
		DEBUGPRINT("%c", c);
	}
	diskid = (diskid ^ (diskid >> 5));
	for (unsigned int i = 0; i < 2; i++, diskid /= 36) {
		Uint8 c = diskid % 36;
		c = c < 26 ? c + 'A' : c - 26 + '0';
		img[0x61816 + i] = c;
		img[0x61904 + i] = c;
		img[0x61a04 + i] = c;
	}
	DEBUGPRINT("\",\"%c%c\"" NL, img[0x61816], img[0x61817]);
	return img;
}


// Return values:
//	0 = OK, image created
//	-1 = some error
//	-2 = file existed before, and do_overwrite as not specified
int d81access_create_image_file ( const char *fn, const char *diskname, const int do_overwrite, const char *cry )
{
	char fullpath[PATH_MAX + 1];
	const int fd = xemu_open_file(fn, O_WRONLY | O_CREAT | O_TRUNC | (!do_overwrite ? O_EXCL : 0), NULL, fullpath);
	if (fd < 0) {
		const int ret = (errno == EEXIST) ? -2 : -1;
		if (cry)
			ERROR_WINDOW("%s [D81-CREATE]\n%s\n%s", cry, fn, strerror(errno));
		if (ret == -2)
			DEBUGPRINT("D81ACCESS: D81 image \"%s\" existed before." NL, fn);
		return ret;
	}
	Uint8 *img = d81access_create_image(NULL, diskname ? diskname : fn, !diskname);
	const int written = xemu_safe_write(fd, img, D81_SIZE);
	xemu_os_close(fd);
	free(img);
	if (written != D81_SIZE) {
		if (cry)
			ERROR_WINDOW("%s [D81-WRITE]\n%s\n%s", cry, fullpath, strerror(errno));
		xemu_os_unlink(fullpath);
		return -1;
	}
	DEBUGPRINT("D81ACCESS: new disk image file \"%s\" has been successfully created (overwrite policy %d)." NL, fullpath, do_overwrite);
	return 0;
}
