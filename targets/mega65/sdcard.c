/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#include <stdio.h>
#include <SDL.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

#include "sdcard.h"
#include "f011_core.h"
#include "mega65.h"
#include "emutools.h"
#include "cpu65c02.h"


static int   sdfd;		// SD-card controller emulation, UNIX file descriptor of the open image file
static int   d81fd = -1;	// special case for F011 access, allow emulator to access D81 image on the host OS, instead of "inside" the SD card image! [NOT SO MUCH USED YET]
static int   d81_is_read_only;
static Uint8 sd_buffer[512];	// SD-card controller buffer
static Uint8 sd_status;		// SD-status byte
static Uint8 sd_sector_bytes[4];
static Uint8 sd_d81_img1_start[4];
static off_t sd_card_size;
static int   sdcard_bytes_read = 0;
static int   sd_is_read_only;
static int   mounted;


int sdcard_init ( const char *fn )
{
	char fnbuf[PATH_MAX + 1];
	sd_status = 0;
	sd_is_read_only = 1;
	d81_is_read_only = 1;
	mounted = 0;
	memset(sd_sector_bytes, 0, sizeof sd_sector_bytes);
	memset(sd_d81_img1_start, 0, sizeof sd_d81_img1_start);
	if (mega65_capable) {
		sdfd = emu_load_file(fn, fnbuf, -1);    // get the file descriptor only ...
		if (sdfd < 0) {
			ERROR_WINDOW("Cannot open SD-card image %s, SD-card access won't work! ERROR: %s", fn, strerror(errno));
			DEBUG("SDCARD: cannot open image %s" NL, fn);
		} else {
			// try to open in R/W mode ...
			int tryfd = open(fnbuf, O_RDWR | O_BINARY);
			if (tryfd >= 0) {
				// use R/W mode descriptor if it was OK!
				close(sdfd);
				sdfd = tryfd;
				DEBUG("SDCARD: image file re-opened in RD/WR mode, good" NL);
				sd_is_read_only = 0;
			} else
				INFO_WINDOW("Image file %s could be open only in R/O mode", fnbuf);
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
	} else {
		sdfd = -1;
		DEBUG("SDCARD: not available in case of Commodore 65 startup mode!" NL);
	}
	return sdfd;
}


// Reads a byte from buffer. Return with -1 if buffer is not I/O mapped.
int sdcard_read_buffer ( int addr )
{
	if (sd_status & SD_ST_MAPPED)
		return sd_buffer[addr & 511];
	else
		return -1;
}


// Writes a byte into buffer. Return with -1 if buffer is not I/O mapped.
int sdcard_write_buffer ( int addr, Uint8 data )
{
	if (sd_status & SD_ST_MAPPED) {
		sd_buffer[addr & 511] = data;
		return (int)data;
	} else
		return -1;
}



static int host_seek_to ( Uint8 *addr_buffer, int addressing_offset, const char *description, off_t size_limit, int fd )
{
	off_t image_offset = (addr_buffer ? (((off_t)addr_buffer[0]) | ((off_t)addr_buffer[1] << 8) | ((off_t)addr_buffer[2] << 16) | ((off_t)addr_buffer[3] << 24)) : 0) + (off_t)addressing_offset;
	DEBUG("SDCARD: %s card at position " PRINTF_LLD " (offset=%d) PC=$%04X" NL, description, (long long)image_offset, addressing_offset, cpu_pc);
	if (image_offset < 0 || image_offset > size_limit - 512) {
		FATAL("SDCARD: SEEK: invalid offset requested for %s with offset " PRINTF_LLD " PC=$%04X", description, (long long)image_offset, cpu_pc);
	}
	if (lseek(fd, image_offset, SEEK_SET) != image_offset)
		FATAL("SDCARD: SEEK: image seek host-OS failure: %s", strerror(errno));
	return 0;
}



static int diskimage_read_block ( Uint8 *io_buffer, Uint8 *addr_buffer, int addressing_offset, const char *description, off_t size_limit, int fd )
{
	int ret;
	if (sdfd < 0)
		return -1;
	host_seek_to(addr_buffer, addressing_offset, description, size_limit, fd);
	ret = read(fd, io_buffer, 512);
	if (ret != 512)
		FATAL("SDCARD: %s failure ... ERROR: %s", description, ret >=0 ? "not 512 bytes could be read" : strerror(errno));
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
	host_seek_to(addr_buffer, addressing_offset, description, size_limit, fd);
	ret = write(fd, io_buffer, 512);
	if (ret != 512)
		FATAL("SDCARD: %s failure ... ERROR: %s", description, ret >=0 ? "not 512 bytes could be written" : strerror(errno));
	DEBUG("SDCARD: cool, sector %s was OK (%d bytes read)!" NL, description, ret);
	return ret;
}



int fdc_cb_rd_sec ( Uint8 *buffer, int d81_offset )
{
	if (d81fd < 0)
		return (diskimage_read_block(buffer, sd_d81_img1_start, d81_offset, "reading[D81@SD]", sd_card_size, sdfd) != 512);
	return (diskimage_read_block(buffer, NULL, d81_offset, "reading[D81@HOST]", D81_SIZE, d81fd) != 512);
}



int fdc_cb_wr_sec ( Uint8 *buffer, int d81_offset )
{
	if (d81fd < 0)
		return (diskimage_write_block(buffer, sd_d81_img1_start, d81_offset, "writing[D81@SD]", sd_card_size, sdfd) != 512);
	return (diskimage_write_block(buffer, NULL, d81_offset, "writing[D81@HOST]", D81_SIZE, d81fd) != 512);
}



// This tries to emulate the behaviour, that at least another one status query
// is needed to BUSY flag to go away instead of with no time. DUNNO if it is needed at all.
static Uint8 sdcard_read_status ( void )
{
	Uint8 ret = sd_status;
	DEBUG("SDCARD: reading SD status $D680 result is $%02X PC=$%04X" NL, ret, cpu_pc);
	sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);
	return ret;
}



static void sdcard_command ( Uint8 cmd )
{
	int ret;
	DEBUG("SDCARD: writing command register $D680 with $%02X PC=$%04X" NL, cmd, cpu_pc);
	sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);	// ugly hack :-@
	switch (cmd) {
		case 0x00:	// RESET SD-card
			sd_status = SD_ST_RESET;	// clear all other flags
			memset(sd_sector_bytes, 0, sizeof sd_sector_bytes);
			break;
		case 0x01:	// END RESET
			sd_status &= ~(SD_ST_RESET | SD_ST_ERROR | SD_ST_FSM_ERROR);
			break;
		case 0x02:	// read sector
			ret = diskimage_read_block(sd_buffer, sd_sector_bytes, 0, "reading[SD]", sd_card_size, sdfd);
			if (ret < 0) {
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR; // | SD_ST_BUSY1 | SD_ST_BUSY0;
				sdcard_bytes_read = 0;
			} else {
				sd_status &= ~(SD_ST_ERROR | SD_ST_FSM_ERROR);
				sdcard_bytes_read = ret;
			}
			break;
		case 0x40:	// SDHC mode OFF
			sd_status &= ~SD_ST_SDHC;
			break;
		case 0x41:	// SDHC mode ON
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
			DEBUG("SDCARD: warning, unimplemented SD-card controller command $%02X" NL, cmd);
			printf("MEGA65: SD: unimplemented command $%02X" NL, cmd);
			break;
	}
}


// data = D68B write
static void sdcard_mount_d81 ( Uint8 data )
{
	printf("SD/FDC mount register request @ $D68B val=$%02X at PC=$%04X" NL, data, cpu_pc);
	if ((data & 3) == 3) {
		fdc_set_disk(1, sd_is_read_only ? 0 : QUESTION_WINDOW("Use read-only access|Use R/W access (can be dangerous, can corrupt the image!)", "Hypervisor seems to be about mounting a D81 image. You can override the access mode now."));
		mounted = 1;
		printf("SD/FDC: (re-?)mounted D81 for starting sector $%02X%02X%02X%02X" NL,
			sd_d81_img1_start[3], sd_d81_img1_start[2], sd_d81_img1_start[1], sd_d81_img1_start[0]
		);
	} else {
		if (mounted)
			printf("SD/FDC: unmounted D81" NL);
		fdc_set_disk(0, 0);
		mounted = 0;
	}
}




void sdcard_write_register ( int reg, Uint8 data )
{
	gs_regs[reg + 0x680] = data;
	switch (reg) {
		case 0:		// command/status register
			sdcard_command(data);
			break;
		case 1:		// sector address
		case 2:		// sector address
		case 3:		// sector address
		case 4:		// sector address
			sd_sector_bytes[reg - 1] = data;
			DEBUG("SDCARD: writing sector number register $%04X with $%02X PC=$%04X" NL, reg + 0xD680, data, cpu_pc);
			break;
		case 0xB:
			sdcard_mount_d81(data);
			break;
		case 0xC:
		case 0xD:
		case 0xE:
		case 0xF:
			sd_d81_img1_start[reg - 0xC] = data;
			DEBUG("SDCARD: writing D81 #1 sector register $%04X with $%02X PC=$%04X" NL, reg + 0xD680, data, cpu_pc);
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
			data = gs_regs[reg + 0x680];
			break;
	}
	return data;
}
