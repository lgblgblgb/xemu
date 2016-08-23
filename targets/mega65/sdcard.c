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


static int sdfd;		// SD-card controller emulation, UNIX file descriptor of the open image file
static Uint8 sd_buffer[512];	// SD-card controller buffer
static Uint8 sd_status;		// SD-status byte
static Uint8 sd_sector_bytes[4];
static int   sd_card_size_in_sectors;
int sdcard_bytes_read = 0;


int sdcard_init ( const char *fn )
{
	sd_status = 0;
	memset(sd_sector_bytes, 0, sizeof sd_sector_bytes);
	if (mega65_capable) {
	sdfd = emu_load_file(fn, NULL, -1);    // get the file descriptor only ...
		if (sdfd < 0) {
			ERROR_WINDOW("Cannot open SD-card image %s, SD-card access won't work! ERROR: %s", fn, strerror(errno));
			printf("SDCARD: cannot open image %s" NL, fn);
		} else {
			// Check size!
			printf("SDCARD: cool, SD-card image %s is open" NL, fn);
			off_t s = lseek(sdfd, 0, SEEK_END);
			if (s == (off_t)-1) {
				ERROR_WINDOW("Cannot query the size of the SD-card image %s, SD-card access won't work! ERROR: %s", fn, strerror(errno));
				close(sdfd);
				sdfd = -1;
				return sdfd;
			}
			if (s > 2147483648L) {
				ERROR_WINDOW("SD-card image is too large! Max allowed size is 2Gbytes!");
				close(sdfd);
				sdfd = -1;
				return sdfd;
			}
			if (s < 67108864) {
				ERROR_WINDOW("SD-card image is too small! Min required size is 64Mbytes!");
				close(sdfd);
				sdfd = -1;
				return sdfd;
			}
			printf("SDCARD: detected size in bytes: %ld" NL, (long)s);
			if (s & 511) {
				ERROR_WINDOW("SD-card image size is not multiple of 512 bytes!!");
				close(sdfd);
				sdfd = -1;
				return sdfd;
			}
			sd_card_size_in_sectors = s >> 9;
			printf("SDCARD: detected size in sectors: %d" NL, sd_card_size_in_sectors);
		}
	} else {
		sdfd = -1;
		printf("SDCARD: not available in case of Commodore 65 startup mode!" NL);
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


static int read_sector ( void )
{
	int secno;
	if (sdfd < 0)
		return 1;
	secno = sd_sector_bytes[0] | (sd_sector_bytes[1] << 8) | (sd_sector_bytes[2] << 16) | (sd_sector_bytes[3] << 24);
	printf("SDCARD: reading sector %d" NL, secno);
	if (secno < 0 || secno >= sd_card_size_in_sectors) {
		printf("SDCARD: invalid sector number failure ..." NL);
		return 1;
	}
	if (lseek(sdfd, (off_t)secno << 9, SEEK_SET) != (off_t)secno << 9) {
		printf("SDCARD: lseek failure ... ERROR: %s" NL, strerror(errno));
		return 1;
	}
	secno = read(sdfd, sd_buffer, 512);
	if (secno != 512) {
		printf("SDCARD: read failure ... ERROR: %s" NL, secno >=0 ? "not 512 bytes are read" : strerror(errno));
		return 1;
	}
	printf("SDCARD: cool, sector read was OK!" NL);
	return 0;
}



// This tries to emulate the behaviour, that at least another one status query
// is needed to BUSY flag to go away instead of with no time. Dunnu if it is needed at all.
Uint8 sdcard_read_status ( void )
{
	Uint8 ret = sd_status;
	printf("SDCARD: reading SD status $D680 result is $%02X" NL, ret);
	sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);
	return ret;
}



void sdcard_command ( Uint8 cmd )
{
	printf("SDCARD: writing command register $D680 with $%02X" NL, cmd);
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
			if (read_sector()) {
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR; // | SD_ST_BUSY1 | SD_ST_BUSY0;
				sdcard_bytes_read = 0;
			} else {
				sd_status &= ~(SD_ST_ERROR | SD_ST_FSM_ERROR);
				sdcard_bytes_read = 512;
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
			printf("SDCARD: warning, unimplemented SD-card controller command $%02X" NL, cmd);
			break;
	}
}



void sdcard_select_sector ( int secreg, Uint8 data )
{
	sd_sector_bytes[secreg] = data;
	printf("SDCARD: writing sector number register $%04X with $%02X" NL, secreg + 0xD681, data);
}
