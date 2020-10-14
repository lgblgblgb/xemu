/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "enterprise128.h"

#ifdef CONFIG_EXDOS_SUPPORT

#include "exdos_wd.h"
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>

// Set the value to DEBUGPRINT to always print to the stdout as well,
// and DEBUG to use only the debug file (if it's requested at all).
// For production release, DEBUG is the normal behaviour.
#define DEBUGEXDOS DEBUG

static int   disk_fd = -1;
static Uint8 disk_buffer[512];
char wd_img_path[PATH_MAX + 1];
int wd_max_tracks, wd_max_sectors, wd_image_size;


Uint8 wd_sector, wd_track;
static Uint8 wd_status, wd_data, wd_command, wd_interrupt, wd_DRQ;
static int driveSel, diskSide, diskInserted, readOnly;
static int diskChanged = 0x40;	// 0x40 -> disk is NOT changed
static int buffer_pos = 0, buffer_size = 1;

#define WDINT_ON	0x3E
#define WDINT_OFF	0x3C
#define WDDRQ		2
// bit mask to set in WD status in case of seek error [spin-up-completed, seek-error, CRC]
#define SEEK_ERROR 	(32 | 16 | 8)
// bit mask to set WD status in case of seek OK  [spin-up-completed]
#define SEEK_OK		32


static const int disk_formats[][2] = {
	{40,  8},
	{40,  9},
	{80,  8},
	{80,  9},
	{80, 15},
	{80, 18},
	{80, 21},
	{82, 21},
	{80, 36},
	{-1, -1}
};


static int doCRC (Uint8 *buf, int nBytes, int n)	// CRC16 hmmm Just copied from ep128emu :) - thanks!
{
	int nBits = nBytes << 3;
	int bitCnt = 0;
	int bitBuf = 0;
	int bufP = 0;
	while (nBits--) {
		if (bitCnt == 0) {
			bitBuf = buf[bufP++];
			bitCnt = 8;
		}
		if ((bitBuf ^ (n >> 8)) & 0x80)
			n = (n << 1) ^ 0x1021;
		else
		n = (n << 1);
		n = n & 0xFFFF;
		bitBuf = (bitBuf << 1) & 0xFF;
		bitCnt--;
	}
	return n;
}


static int guess_geometry ( void )
{
	int a;
	off_t disk_size = lseek(disk_fd, 0, SEEK_END);
	for (a = 0; disk_formats[a][0] != -1; a++) {
		wd_max_tracks = disk_formats[a][0];
		wd_max_sectors = disk_formats[a][1];
		if ((wd_max_tracks * wd_max_sectors) << 10 == disk_size) {
			wd_image_size = disk_size;
			DEBUGPRINT("WD: disk size is %d bytes, %d tracks, %d sectors." NL, wd_image_size, wd_max_tracks, wd_max_sectors);
			return 0;
		}
	}
	return 1;
}


void wd_detach_disk_image ( void )
{
	if (disk_fd >= 0)
		close(disk_fd);
	disk_fd = -1;
	*wd_img_path = 0;
	readOnly = 0;
	diskInserted = 1; // no disk inserted by default (1 means NOT)
}



int wd_attach_disk_image ( const char *fn )
{
	wd_detach_disk_image();
	if (!fn || !*fn) {
		DEBUGPRINT("WD: no disk image was requested." NL);
		return 0;
	}
	int ro = O_RDONLY;
	disk_fd = xemu_open_file(fn, O_RDWR, &ro, wd_img_path);
	if (disk_fd <= 0) {
		ERROR_WINDOW("Cannot open EXDOS disk because %s\n%s", ERRSTR(), fn);
		return 1;
	}
	if (ro) {
		INFO_WINDOW("Disk image could be opened only in R/O mode\n%s", wd_img_path);
		DEBUGPRINT("WD: disk image opened in R/O mode only: %s" NL, wd_img_path);
		readOnly = 1;
	} else {
		DEBUGPRINT("WD: disk image opened in R/W mode: %s" NL, wd_img_path);
	}
	if (guess_geometry()) {
		ERROR_WINDOW("Cannot figure the EXDOS disk image geometry out, invalid/not supported image size?");
		wd_detach_disk_image();
		return 1;
	}
	diskInserted = 0;	// disk OK, use inserted status
	return 1;
}



void wd_exdos_reset ( void )
{
	if (disk_fd < 0)
		wd_detach_disk_image();
	readOnly = 0;
	wd_track = 0;
	wd_sector = 0;
	wd_status = 4; // track 0 flag is on at initialization
	wd_data = 0;
	wd_command = 0xD0; // fake last command as force interrupt
	wd_interrupt = WDINT_OFF; // interrupt output is OFF
	wd_DRQ = 0; // no DRQ (data request)
	driveSel = (disk_fd >= 0); // drive is selected by default if there is disk image!
	diskSide = 0;
	diskInserted = (disk_fd < 0) ? 1 : 0; // 0 means inserted disk, 1 means = not inserted
	DEBUG("WD: reset" NL);
}


static int read_sector ( void )
{
	off_t ofs;
	int ret;
	buffer_pos = 0;
	buffer_size = 512;
	if (!driveSel) {
		// ugly hack! to avoid delay on boot trying to read B:, we provide here an empty sector ... :-/
		DEBUGEXDOS("WD: read sector refused: driveSel=0" NL);
		memset(disk_buffer, 0, 512); // fake empty
		return 0;
	}
	if (wd_sector < 1 || wd_sector > wd_max_sectors) {
		DEBUGEXDOS("WD: read sector refused: invalid sector number %d (valid = %d...%d)" NL, wd_sector, 1, wd_max_sectors);
		return 1;
	}
	if (wd_track < 0 || wd_track >= wd_max_tracks) {
		DEBUGEXDOS("WD: read sector refused: invalid track number %d (valid = %d...%d)" NL, wd_track, 0, wd_max_tracks - 1);
		return 1;
	}
	ofs = (wd_track * wd_max_sectors * 2 + wd_sector - 1 + wd_max_sectors * diskSide) << 9;
	DEBUGEXDOS("WD: Seeking to: offset %d (track=%d, sector=%d, side=%d)" NL, (int)ofs, wd_track, wd_sector, diskSide);
	if (lseek(disk_fd, ofs, SEEK_SET) != ofs) {
		ERROR_WINDOW("WD/EXDOS disk image seek error: %s", ERRSTR());
		return 1;
	}
	ret = read(disk_fd, disk_buffer, 512);
	DEBUGEXDOS("WD: read() for 512 bytes data to read, retval=%d" NL, ret);
	switch (ret) {
		case   0:
			ERROR_WINDOW("WD/EXDOS disk image read error: NO DATA READ");
			return 1;
		case 512:
			break;
		default:
			ERROR_WINDOW("WD/EXDOS disk image read error: %s", ERRSTR());
			return 1;
	}
	return 0;
}



Uint8 wd_read_status ( void )
{
	wd_interrupt = WDINT_OFF; // on reading WD status, interrupt is reset!
	return 128 | wd_status | wd_DRQ; // motor is always on, the given wdStatus bits, DRQ handled separately (as we need it for exdos status too!)
}



Uint8 wd_read_data ( void )
{
	if (wd_DRQ) {
		wd_data = disk_buffer[buffer_pos++];
		if (buffer_pos >= buffer_size)
			wd_DRQ = 0; // end of data, switch DRQ off!
	}
	return wd_data;
}



Uint8 wd_read_exdos_status ( void )
{
	return wd_interrupt | (wd_DRQ << 6) | diskInserted | diskChanged;
}



void wd_send_command ( Uint8 value )
{
	wd_command = value;
	wd_DRQ = 0;	// reset DRQ
	wd_interrupt = WDINT_OFF;	// reset INTERRUPT
	DEBUGEXDOS("WD: command received: 0x%02X driveSel=%d distInserted=%d hasImage=%d" NL, value, driveSel, diskInserted, disk_fd >= 0);
	switch (value >> 4) {
		case  0: // restore (type I), seeks to track zero
			if (driveSel) {
				wd_status = 4 | SEEK_OK; // 4->set track0
				wd_track = 0;
			} else
				wd_status = SEEK_ERROR | 4; // set track0 flag (4) is needed here not to be mapped A: as B: by EXDOS, or such?!
			wd_interrupt = WDINT_ON;
			break;
		case  1: // seek (type I)
			if (wd_data < wd_max_tracks && driveSel) {
				wd_track = wd_data;
				wd_status = SEEK_OK;
			} else
				wd_status = SEEK_ERROR;
			wd_interrupt = WDINT_ON;
			if (!wd_track) wd_status |= 4;
			break;
		case  8: // read sector, single (type II)
			if (read_sector())
				wd_status = 16; // record not found
			else {
				wd_status = 0;
				wd_DRQ = WDDRQ;
			}
			wd_interrupt = WDINT_ON;
			break;
		case 12: // read address (type III)
			if (driveSel) {
				int i;
				disk_buffer[0] = wd_track;
				wd_sector = wd_track; // why?! it seems WD puts track number into sector register. Sounds odd ...
				disk_buffer[1] = diskSide;
				disk_buffer[2] = 1; // first sector!
				disk_buffer[3] = 2; // sector size???
				i = doCRC(disk_buffer, 4, 0xB230);
				disk_buffer[4] = i >> 8; // CRC1
				disk_buffer[5] = i & 0xFF; // CRC2
				wd_DRQ = WDDRQ;
				buffer_size = 6;
				buffer_pos = 0;
				wd_status = 0;
			} else
				wd_status = 16;	// record not found
			wd_interrupt = WDINT_ON;
			break;
		case 13: // force interrupt (type IV)
			if (value & 15) wd_interrupt = WDINT_ON;
			wd_status = (wd_track == 0) ? 4 : 0;
                        break;
		default:
			DEBUGEXDOS("WD: unknown command: 0x%02X" NL, value);
			wd_status = 4 | 8 | 16 | 64; // unimplemented command results in large set of problems reported :)
			wd_interrupt = WDINT_ON;
			break;
	}
}



void wd_write_data ( Uint8 value )
{
	wd_data = value;
}



void wd_set_exdos_control ( Uint8 value )
{
	driveSel = (disk_fd >= 0) && ((value & 15) == 1);
	diskSide = (value >> 4) & 1;
	diskInserted = driveSel ? 0 : 1;
}


#else
#warning "EXDOS/WD support is not compiled in / not ready"
#endif

