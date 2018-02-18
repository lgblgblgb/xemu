/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "sdcard.h"
#include "xemu/f011_core.h"
#include "mega65.h"
#include "xemu/cpu65.h"
#include "io_mapper.h"

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

static int   sdfd;		// SD-card controller emulation, UNIX file descriptor of the open image file
static int   d81fd = -1;	// special case for F011 access, allow emulator to access D81 image on the host OS, instead of "inside" the SD card image! [NOT SO MUCH USED YET]
static int   use_d81 = 0;	// the above: actually USE that!
static int   d81_is_read_only;	// access of the above, read-only or read-write
Uint8 sd_status;		// SD-status byte
static Uint8 sd_sector_bytes[4];
static Uint8 sd_d81_img1_start[4];
static off_t sd_card_size;
static int   sdcard_bytes_read = 0;
static int   sd_is_read_only;
static int   mounted;
static int   keep_busy = 0;
// 4K buffer space: Actually the SD buffer _IS_ inside this, also the F011 buffer should be (FIXME: that is not implemented yet right now!!)
Uint8 disk_buffers[0x1000];


static int open_external_d81 ( const char *fn )
{
	char fnbuf[PATH_MAX + 1];
	if (!fn)
		return -1;
	d81_is_read_only = O_RDONLY;
	d81fd = xemu_open_file(fn, O_RDWR, &d81_is_read_only, fnbuf);
	if (d81fd < 0) {
		ERROR_WINDOW("External D81 image was specified (%s) but it cannot be opened: %s", fn, strerror(errno));
		DEBUG("SDCARD: cannot open external D81 image %s" NL, fn);
	} else {
		off_t d81_size;
		if (d81_is_read_only)
			INFO_WINDOW("External D81 image file %s could be open only in R/O mode", fnbuf);
		else
			DEBUG("SDCARD: exernal D81 image file re-opened in RD/WR mode, good" NL);
		d81_size = lseek(d81fd, 0, SEEK_END);
		if (d81_size == (off_t)-1) {
			ERROR_WINDOW("Cannot query the size of external D81 image %s, using on-SD images! ERROR: %s", fn, strerror(errno));
			close(d81fd);
			d81fd = -1;
			return d81fd;
		}
		if (d81_size != D81_SIZE) {
			ERROR_WINDOW("Bad external D81 image size " PRINTF_LLD " for %s, should be %d bytes!", (long long)d81_size, fnbuf, D81_SIZE);
			close(d81fd);
			d81fd = -1;
			return d81fd;
		}
	}
	return d81fd;
}



static void sdcard_shutdown ( void )
{
	if (sdfd >= 0)
		close(sdfd);
	if (d81fd >= 0)
		close(d81fd);
}



int sdcard_init ( const char *fn, const char *extd81fn )
{
	char fnbuf[PATH_MAX + 1];
	atexit(sdcard_shutdown);
	keep_busy = 0;
	sd_status = 0;
	d81_is_read_only = 1;
	mounted = 0;
	memset(sd_sector_bytes, 0, sizeof sd_sector_bytes);
	memset(sd_d81_img1_start, 0, sizeof sd_d81_img1_start);
retry:
	sd_is_read_only = O_RDONLY;
	sdfd = xemu_open_file(fn, O_RDWR, &sd_is_read_only, fnbuf);
	if (sdfd < 0) {
		int err = errno;
		ERROR_WINDOW("Cannot open SD-card image %s, SD-card access won't work! ERROR: %s", fnbuf, strerror(err));
		DEBUG("SDCARD: cannot open image %s" NL, fn);
		if (err == ENOENT && !strcmp(fn, SDCARD_NAME)) {
			unsigned int r = QUESTION_WINDOW("No|128M|256M|512M|1G|2G", "Default SDCARD image does not exist.\nWould you like me to create one for you?");
			if (r) {
				int r2 = xemu_create_empty_image(fnbuf, (1U << (r + 26U)));
				if (r2)
					ERROR_WINDOW("Couldn't create: %s", strerror(r2));
				else
					goto retry;
			}
		}
	} else {
		if (sd_is_read_only)
			INFO_WINDOW("Image file %s could be open only in R/O mode", fnbuf);
		else
			DEBUG("SDCARD: image file re-opened in RD/WR mode, good" NL);
		// Check size!
		DEBUG("SDCARD: cool, SD-card image %s (as %s) is open" NL, fn, fnbuf);
		sd_card_size = lseek(sdfd, 0, SEEK_END);
		if (sd_card_size == (off_t)-1) {
			ERROR_WINDOW("Cannot query the size of the SD-card image %s, SD-card access won't work! ERROR: %s", fn, strerror(errno));
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
		if (sd_card_size > 2147483648UL) {
			ERROR_WINDOW("SD-card image is too large! Max allowed size is 2Gbytes!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
		if (sd_card_size < 67108864) {
			ERROR_WINDOW("SD-card image is too small! Min required size is 64Mbytes!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
		DEBUG("SDCARD: detected size in Mbytes: %d" NL, (int)(sd_card_size >> 20));
		if (sd_card_size & (off_t)511) {
			ERROR_WINDOW("SD-card image size is not multiple of 512 bytes!!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
	}
	if (sdfd >= 0)
		open_external_d81(extd81fn);
	return sdfd;
}


static off_t host_seek_to ( Uint8 *addr_buffer, int addressing_offset, const char *description, off_t size_limit, int fd )
{
	off_t image_offset = (addr_buffer ? (((off_t)addr_buffer[0]) | ((off_t)addr_buffer[1] << 8) | ((off_t)addr_buffer[2] << 16) | ((off_t)addr_buffer[3] << 24)) : 0) + (off_t)addressing_offset;
	DEBUG("SDCARD: %s card at position " PRINTF_LLD " (offset=%d) PC=$%04X" NL, description, (long long)image_offset, addressing_offset, cpu65.pc);
	if (image_offset < 0 || image_offset > size_limit - 512) {
		DEBUG("SDCARD: SEEK: invalid offset requested for %s with offset " PRINTF_LLD " PC=$%04X" NL, description, (long long)image_offset, cpu65.pc);
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



int fdc_cb_rd_sec ( Uint8 *buffer, int d81_offset )
{
	if (use_d81)
		return (diskimage_read_block(buffer, NULL, d81_offset, "reading[D81@HOST]", D81_SIZE, d81fd) != 512);
	else
		return (diskimage_read_block(buffer, sd_d81_img1_start, d81_offset, "reading[D81@SD]", sd_card_size, sdfd) != 512);
}



int fdc_cb_wr_sec ( Uint8 *buffer, int d81_offset )
{
	if (use_d81)
		return (diskimage_write_block(buffer, NULL, d81_offset, "writing[D81@HOST]", D81_SIZE, d81fd) != 512);
	else
		return (diskimage_write_block(buffer, sd_d81_img1_start, d81_offset, "writing[D81@SD]", sd_card_size, sdfd) != 512);
}



// This tries to emulate the behaviour, that at least another one status query
// is needed to BUSY flag to go away instead of with no time. DUNNO if it is needed at all.
static Uint8 sdcard_read_status ( void )
{
	Uint8 ret = sd_status;
	DEBUG("SDCARD: reading SD status $D680 result is $%02X PC=$%04X" NL, ret, cpu65.pc);
	if (!keep_busy)
		sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);
	return ret;
}



static void sdcard_command ( Uint8 cmd )
{
	int ret;
	DEBUG("SDCARD: writing command register $D680 with $%02X PC=$%04X" NL, cmd, cpu65.pc);
	sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);	// ugly hack :-@
	keep_busy = 0;
	switch (cmd) {
		case 0x00:	// RESET SD-card
			sd_status = SD_ST_RESET;	// clear all other flags
			memset(sd_sector_bytes, 0, sizeof sd_sector_bytes);
			break;
		case 0x01:	// END RESET
			sd_status &= ~(SD_ST_RESET | SD_ST_ERROR | SD_ST_FSM_ERROR);
			break;
		case 0x02:	// read block
			if (sd_sector_bytes[0] || (sd_sector_bytes[1] & 1)) {
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR | SD_ST_BUSY1 | SD_ST_BUSY0;
				keep_busy = 1;
				DEBUGPRINT("SDCARD: warning, unaligned read access!" NL);
			} else {
				ret = diskimage_read_block(sd_buffer, sd_sector_bytes, 0, "reading[SD]", sd_card_size, sdfd);
				if (ret < 0) {
					sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR; // | SD_ST_BUSY1 | SD_ST_BUSY0;
					sdcard_bytes_read = 0;
				} else {
					sd_status &= ~(SD_ST_ERROR | SD_ST_FSM_ERROR);
					sdcard_bytes_read = ret;
				}
			}
			break;
		case 0x03:	// write block
			if (sd_sector_bytes[0] || (sd_sector_bytes[1] & 1)) {
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR | SD_ST_BUSY1 | SD_ST_BUSY0;
				keep_busy = 1;
				DEBUGPRINT("SDCARD: warning, unaligned write access!" NL);
			} else {
				ret = diskimage_write_block(sd_buffer, sd_sector_bytes, 0, "writing[SD]", sd_card_size, sdfd);
				if (ret < 0) {
					sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR; // | SD_ST_BUSY1 | SD_ST_BUSY0;
					sdcard_bytes_read = 0;
				} else {
					sd_status &= ~(SD_ST_ERROR | SD_ST_FSM_ERROR);
					sdcard_bytes_read = ret;
				}
			}
			break;
		case 0x40:	// SDHC mode OFF
			sd_status &= ~SD_ST_SDHC;
			break;
		case 0x41:	// SDHC mode ON
			DEBUGPRINT("SDCARD: warning, SDHC mode is turned ON with SD command $41, though Xemu does not support SDHC! PC=$%02X" NL, cpu65.pc);
			sd_status |= SD_ST_SDHC;
			break;
		case 0x42:	// half-speed OFF
			sd_status &= ~SD_ST_HALFSPEED;
			break;
		case 0x43:	// half-speed ON
			sd_status |= SD_ST_HALFSPEED;
			break;
		case 0x81:	// map SD-buffer
			sd_status |= SD_ST_MAPPED;
			sd_status &= ~(SD_ST_ERROR | SD_ST_FSM_ERROR);
			break;
		case 0x82:	// unmap SD-buffer
			sd_status &= ~(SD_ST_MAPPED | SD_ST_ERROR | SD_ST_FSM_ERROR);
			break;
		default:
			// FIXME: how to signal this to the user/sys app? error flags, etc?
			DEBUGPRINT("SDCARD: warning, unimplemented SD-card controller command $%02X" NL, cmd);
			break;
	}
}


// data = D68B write
static void sdcard_mount_d81 ( Uint8 data )
{
	DEBUGPRINT("SDCARD: SD/FDC mount register request @ $D68B val=$%02X at PC=$%04X" NL, data, cpu65.pc);
	if ((data & 3) == 3) {
		if (d81fd >= 0)
			use_d81 = QUESTION_WINDOW("Use D81 from SD-card|Use external D81 image file", "Hypervisor mount request, and you have defined external D81 image.");
		else
			use_d81 = 0;
		if (!use_d81) {
			fdc_set_disk(1, sd_is_read_only ? 0 : QUESTION_WINDOW("Use read-only access|Use R/W access (can be dangerous, can corrupt the image!)", "Hypervisor seems to be about mounting a D81 image. You can override the access mode now."));
			DEBUGPRINT("SDCARD: SD/FDC: (re-?)mounted D81 for starting sector $%02X%02X%02X%02X" NL,
				sd_d81_img1_start[3], sd_d81_img1_start[2], sd_d81_img1_start[1], sd_d81_img1_start[0]
			);
		} else {
			fdc_set_disk(1, !d81_is_read_only);
			DEBUGPRINT("SDCARD: SD/FDC: mount *EXTERNAL* D81 image, not from SD card (emulator feature only)!" NL);
		}
		mounted = 1;
	} else {
		if (mounted)
			DEBUGPRINT("SDCARD: SD/FDC: unmounted D81" NL);
		fdc_set_disk(0, 0);
		mounted = 0;
	}
}




void sdcard_write_register ( int reg, Uint8 data )
{
	D6XX_registers[reg + 0x80] = data;
	switch (reg) {
		case 0:		// command/status register
			sdcard_command(data);
			break;
		case 1:		// sector address
		case 2:		// sector address
		case 3:		// sector address
		case 4:		// sector address
			sd_sector_bytes[reg - 1] = data;
			DEBUG("SDCARD: writing sector number register $%04X with $%02X PC=$%04X" NL, reg + 0xD680, data, cpu65.pc);
			break;
		case 0xB:
			sdcard_mount_d81(data);
			break;
		case 0xC:
		case 0xD:
		case 0xE:
		case 0xF:
			sd_d81_img1_start[reg - 0xC] = data;
			DEBUG("SDCARD: writing D81 #1 sector register $%04X with $%02X PC=$%04X" NL, reg + 0xD680, data, cpu65.pc);
			break;
	}
}



Uint8 sdcard_read_register ( int reg )
{
	Uint8 data;
	switch (reg) {
		case 0:
			data = sdcard_read_status();
			break;
		case 8:	// SDcard read bytes low byte
			data = sdcard_bytes_read & 0xFF;
			break;
		case 9:	// SDcard read bytes hi byte
			data = sdcard_bytes_read >> 8;
			break;
		default:
			data = D6XX_registers[reg + 0x80];
			break;
	}
	return data;
}


/* --- SNAPSHOT RELATED --- */


#ifdef XEMU_SNAPSHOT_SUPPORT

#include <string.h>

#define SNAPSHOT_SDCARD_BLOCK_VERSION	0
#define SNAPSHOT_SDCARD_BLOCK_SIZE	(0x100 + sizeof(disk_buffers))

int sdcard_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	Uint8 buffer[SNAPSHOT_SDCARD_BLOCK_SIZE];
	int a;
	if (block->block_version != SNAPSHOT_SDCARD_BLOCK_VERSION || block->sub_counter || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad SD-Card block syntax");
	a = xemusnap_read_file(buffer, sizeof buffer);
	if (a) return a;
	/* loading state ... */
	memcpy(sd_sector_bytes, buffer, 4);
	memcpy(sd_d81_img1_start, buffer + 4, 4);
	mounted = (int)P_AS_BE32(buffer + 8);
	sdcard_bytes_read = (int)P_AS_BE32(buffer + 12);
	sd_is_read_only = (int)P_AS_BE32(buffer + 16);
	d81_is_read_only = (int)P_AS_BE32(buffer + 20);
	use_d81 = (int)P_AS_BE32(buffer + 24);
	sd_status = buffer[0xFF];
	memcpy(disk_buffers, buffer + 0x100, sizeof disk_buffers);
	return 0;
}


int sdcard_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_SDCARD_BLOCK_SIZE];
	int a = xemusnap_write_block_header(def->idstr, SNAPSHOT_SDCARD_BLOCK_VERSION);
	if (a) return a;
	memset(buffer, 0xFF, sizeof buffer);
	/* saving state ... */
	memcpy(buffer, sd_sector_bytes, 4);
	memcpy(buffer + 4,sd_d81_img1_start, 4);
	U32_AS_BE(buffer + 8, mounted);
	U32_AS_BE(buffer + 12, sdcard_bytes_read);
	U32_AS_BE(buffer + 16, sd_is_read_only);
	U32_AS_BE(buffer + 20, d81_is_read_only);
	U32_AS_BE(buffer + 24, use_d81);
	buffer[0xFF] = sd_status;
	memcpy(buffer + 0x100, sd_buffer, sizeof disk_buffers);
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}

#endif
