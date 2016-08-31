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

#include "sdcard.h"
#include "mega65.h"
#include "emutools.h"

#include "cpu65c02.h"


static int sdfd;		// SD-card controller emulation, UNIX file descriptor of the open image file
static Uint8 sd_buffer[512];	// SD-card controller buffer
static Uint8 sd_status;		// SD-status byte
static Uint8 sd_sector_bytes[4];
static off_t sd_card_size;
int sdcard_bytes_read = 0;


int sdcard_init ( const char *fn )
{
	sd_status = 0;
	memset(sd_sector_bytes, 0, sizeof sd_sector_bytes);
	if (mega65_capable) {
	sdfd = emu_load_file(fn, NULL, -1);    // get the file descriptor only ...
		if (sdfd < 0) {
			ERROR_WINDOW("Cannot open SD-card image %s, SD-card access won't work! ERROR: %s", fn, strerror(errno));
			DEBUG("SDCARD: cannot open image %s" NL, fn);
		} else {
			// Check size!
			DEBUG("SDCARD: cool, SD-card image %s is open" NL, fn);
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


int sdcard_read_buffer ( int addr )
{
	if (sd_status & SD_ST_MAPPED)
		return sd_buffer[addr & 511];
	else
		return -1;
}


int sdcard_write_buffer ( int addr, Uint8 data )
{
	if (sd_status & SD_ST_MAPPED) {
		sd_buffer[addr & 511] = data;
		return (int)data;
	} else
		return -1;
}


#define SEEK_SHIFT 0
//#define SEEK_SHIFT 9

static int read_sector ( void )
{
	off_t offset;
	int ret;
	if (sdfd < 0)
		return -1;
	offset = (off_t)sd_sector_bytes[0] | ((off_t)sd_sector_bytes[1] << 8) | ((off_t)sd_sector_bytes[2] << 16) | ((off_t)sd_sector_bytes[3] << 24);
	DEBUG("SDCARD: reading position %ld PC=$%04X" NL, (long)offset, cpu_pc);
	if (offset < 0 || offset >= sd_card_size) {
		DEBUG("SDCARD: invalid position value failure ..." NL);
		FATAL("SDCARD: invalid position value failure!! %lld (limit = %lld)", (long long int)offset, (long long int)sd_card_size);
		return -1;
	}
	if (lseek(sdfd, offset, SEEK_SET) != offset) {
		DEBUG("SDCARD: lseek failure ... ERROR: %s" NL, strerror(errno));
		return -1;
	}
	ret = read(sdfd, sd_buffer, 512);
	if (ret <= 0) {
		DEBUG("SDCARD: read failure ... ERROR: %s" NL, ret >=0 ? "zero byte read" : strerror(errno));
		return -1;
	}
	DEBUG("SDCARD: cool, sector read was OK (%d bytes read)!" NL, ret);
	return ret;
}



// This tries to emulate the behaviour, that at least another one status query
// is needed to BUSY flag to go away instead of with no time. Dunnu if it is needed at all.
Uint8 sdcard_read_status ( void )
{
	Uint8 ret = sd_status;
	DEBUG("SDCARD: reading SD status $D680 result is $%02X PC=$%04X" NL, ret, cpu_pc);
	sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);
	return ret;
}



void sdcard_command ( Uint8 cmd )
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
			ret = read_sector();
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
			DEBUG("SDCARD: warning, unimplemented SD-card controller command $%02X" NL, cmd);
			break;
	}
}



void sdcard_select_sector ( int secreg, Uint8 data )
{
	sd_sector_bytes[secreg] = data;
	DEBUG("SDCARD: writing sector number register $%04X with $%02X PC=$%04X" NL, secreg + 0xD681, data, cpu_pc);
}
