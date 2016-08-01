/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 emulator.
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


/* !!!!!!!!!!!!!!!!!!!
   FDC F011 emulation is still a big mess, with bugs, and unplemented features.
   It gives you only read only access currently, and important features like SWAP
   bit is not handled at all. The first goal is to be usable with "DIR" and "LOAD"
   commands on the C65, nothing too much more */


#include <stdio.h>
#include <SDL.h>

#include "c65fdc.h"
#include "emutools.h"
#include "commodore_65.h"


#define GEOS_FDC_HACK


static Uint8 head_track;		// "physical" track, ie, what is the head is positioned at currently
static int   head_side;
static Uint8 track, sector, side;	// parameters given for an operation ("find"), if track is not the same as head_track, you won't find your sector on that track probably ...
static Uint8 control, status_a, status_b;
static Uint8 cmd;
static int   curcmd;
static Uint8 clock, step;
static int   emulate_busy;
static int   drive;
static Uint8 cache[512];		// 512 bytes cache FDC will use. This is a real 512byte RAM attached to the FDC controller for buffered operations on the C65
static int   cache_p_cpu;
static int   cache_p_drive;
static FILE *disk;


static int   warn_disk = 1;




void fdc_init ( const char *dfn )
{
	disk = NULL;
	head_track = 0;
	head_side = 0;
	control = 0;
	status_a = 1;	// on track-0
	status_b = 8;	// disk is inserted
	track = 0;
	sector = 0;
	side = 0;
	cmd = 0;
	curcmd = -1;
	clock = 0xFF;
	step = 0xFF;
	cache_p_cpu = 0;
	cache_p_drive = 0;
	drive = 0;
	if (dfn) {
		disk = fopen(dfn, "rb");
		if (!disk)
			ERROR_WINDOW("Couldn't open disk image %s", dfn);
	}
}


#ifdef GEOS_FDC_HACK
// My hack for an easy disk access primarly for my tries on GEOS (later it should work with the original disk access methods!)
static int geos_hack_read_sector ()
{
	int geos_hack_track  = memory[4];
	int geos_hack_sector = memory[5];
	Uint8 *geos_hack_buffer = (memory[0xA] | (memory[0xB] << 8)) + memory;
	printf("GEOS: reading sector %d of track %d to buffer at $%04X" NL,
		geos_hack_sector, geos_hack_track, geos_hack_buffer - memory
	);
	if (!disk) {
		printf("GEOS: no disk is attached!" NL);
		return 1;
	} else {
		if (
			fseek(disk, 40 * (geos_hack_track - 0) * 256 + (geos_hack_sector - 1) * 256, SEEK_SET) ||
			fread(geos_hack_buffer, 256, 1, disk) != 1
		) {
			printf("GEOS: OK, block has been read" NL);
			return 0;
		} else {
			printf("GEOS: problem with seek or read" NL);
			return 1;
		}
	}
}
#endif


static void read_sector ( void )
{
	if (disk) {
		Uint8 buffer[512];
		if (
			fseek(disk, 40 * (track - 0) * 256 + (sector -1 ) * 512 + side * 20 * 256, SEEK_SET) ||
			fread(buffer, 512, 1, disk) != 1
		) {
			status_a |= 16; // record not found ....
			printf("FDC: READ: cannot read sector from image file!\n");
		} else {
			//memcpy(cache, buffer, 256);
			//memcpy(cache + 256, buffer, 256);
			//cache_p_drive = 256;
			int a = 0;
			// Read block to the cache
			cache_p_drive=0;
			while (a < 512) {
				//cache[cache_p_drive+256]=buffer[a];
				cache[cache_p_drive] = buffer[a++];
				cache_p_drive = (cache_p_drive + 1) & 511;
			}
			/* hacky hack! */

			//memcpy(cache, buffer, 256);
			//memcpy(cache + 256, buffer, 256);	
			printf("FDC: READ: sector has been read from image file.\n");
		}
	} else {
		status_a |= 16;	// record not found ....
		status_b &= (255- 4); // no disk inserted ...
		printf("FDC: READ: no valid image file!\n");
		if (warn_disk) {
			INFO_WINDOW("No disk image was given or can be loaded!\nStart emulator with the disk image as parameter!");
			warn_disk = 0;
		}
	}
}






void fdc_write_reg ( int addr, Uint8 data )
{
        printf("FDC: writing register %d with data $%02X" NL, addr, data);
	switch (addr) {
		case 0:
#if 0
			if (status_a & 128) {
				printf("FDC: WARN: trying to write control register ($%02X) while FDC is busy." NL, data);
				return;
			}
#endif
			control = data;
			status_a |= 128;	// writing control register also causes to set the BUSY flag for some time ...
			if (curcmd == -1)
				curcmd = 0x100;		// "virtual" command, by writing the control register
			drive = data & 7;	// drive selection
			head_side = (data >> 3) & 1;
			if (drive)
				printf("FDC: WARN: not drive-0 is selected: %d!" NL, drive);
			else
				printf("FDC: great, drive-0 is selected" NL);
			if (data & 16)
				INFO_WINDOW("FDC SWAP bit is not implemented yet!");
			break;
		case 1:
#ifdef GEOS_FDC_HACK
			if (data == 0xFF && track == 0xFE && sector == 0xFD && side == 0xFC) {
				side = geos_hack_read_sector() ? 0xFF : 0;
				return;
			}
#endif
			printf("FDC: command=$%02X (lower bits: $%X)" NL, data & 0xF8, data & 7);
			if ((status_a & 128) && ((data & 0xF8))) {	// if BUSY, and command is not the cancel command ..
				printf("FDC: WARN: trying to issue another command ($%02X) while the previous ($%02X) is running." NL, data, cmd);
				return;
			}
			cmd = data;
			curcmd = data;
			status_a |= 128; 	// simulate busy status ...
			status_b &= 255 - 2;	// turn IRQ flag OFF
			status_a &= 255 - (4 + 8 + 16);	// turn RNF/CRC/LOST flags OFF
			status_b &= 255 - (128 + 64);	// also turn RDREQ and WRREQ OFF [IS THIS REALLY NEEDED?]
			if ((cmd & 0xF8) == 0x80)
				status_a |= 2; 		// write protected
			else
				status_a &= (255 - 2);
			break;
		case 4:
			track = data;
			break;
		case 5:
			sector = data;
			break;
		case 6:
			side = data;
			break;
		// TODO: write DATA register (7) [only for writing it is needed anyway]
		case 8:
			clock = data;
			break;
		case 9:
			step = data;
			break;
	}
	if (status_a & 128)
		emulate_busy = 10;
}






static void execute_command ( void )
{
	status_a &= 127;	// turn BUSY flag OFF
	status_b |= 2;		// turn IRQ flag ON
	if (control & 128)
		INFO_WINDOW("Sorry, FDC-IRQ is not supported yet, by FDC emulation!");
	if (curcmd < 0)
		return;	// no cmd was given?!
	if (curcmd > 0xFF)
		return; // only control register was written, not the command register
	switch (cmd & 0xF8) {	// high 5 bits of the command ...
		case 0x40:	// read sector
			//status_a |= 16;		// record not found for testing ...
			status_b |= 128;	// RDREQ: if it's not here, you won't get a READY. prompt!
			//status_b |= 32;		// RUN?!
			status_a |= 64;		// set DRQ
			status_a &= (255 - 32); // clear EQ
			//status_a |= 32; // set EQ?!
			read_sector();
			//cache_p_drive = (cache_p_drive + BLOCK_SIZE) & 511;
			cache_p_cpu = 0; // yayy .... If it's not here we can't get READY. prompt!!
			printf("FDC: READ: head_track=%d need_track=%d head_side=%d need_side=%d need_sector=%d drive_selected=%d" NL,
				head_track, track, head_side, side, sector, drive
			);
			break;
		case 0x80:	// write sector
			status_a |= 2;		// write protected!
			break;
		case 0x10:	// head step out or no step
			if (!(cmd & 4)) {	// if only not TIME operation, which does not step!
				if (head_track)
					head_track--;
				if (!head_track)
					status_a |= 1;	// track 0 flag
				printf("FDC: head position = %d" NL, head_track);
			}
			break;	
		case 0x18:	// head step in
			if (head_track < 128)
				head_track++;
			printf("FDC: head position = %d" NL, head_track);
			status_a &= 0xFE;	// track 0 flag off
			break;
		case 0x20:	// motor spin up
			control |= 32;
			status_a |= 16; // according to the specification, RNF bit should be set at the end of the operation
			break;
		case 0x00:	// cancel running command?? NOTE: also if low bit is 1: clear pointer!
			break;
			if (cmd & 1) {
				cache_p_cpu = 0;
				cache_p_drive = 0;
				printf("FDC: WARN: resetting cache pointers" NL);
				status_a |= 32; // turn EQ on
				status_a &= 255 - 64; // turn DRQ off
				status_b &= 127;      // turn RDREQ off

			}
			break;
		default:
			printf("FDC: WARN: unknown comand: $%02X" NL, cmd);
			//status_a &= 127; // well, not a valid command, revoke busy status ...
			break;
	}
	curcmd = -1;
}



static int last_addr = 0;
static int mad_hack = 0;


Uint8 fdc_read_reg  ( int addr )
{
	Uint8 result;
	/* Emulate BUSY timing, with a very bad manner: ie, decrement a counter on each register read to give some time to wait.
	   Won't work if DOS is IRQ driven ... */
	if (status_a & 128) {	// check the BUSY flag
		if (emulate_busy > 0)
			emulate_busy--;
		if (emulate_busy <= 0)
			execute_command();	// execute the command only now for real ... (it will also turn BUSY flag - bit 7 - OFF in status_a)
	}
	switch (addr) {
		case 0:
			result = control;
			break;
		case 1:
			result = cmd;
			break;
		case 2:	// STATUS register A
			if (last_addr == addr) {
				if (mad_hack > 1000) {
					mad_hack = 0;
					//status_a &= 255 - 32;
				} else
					mad_hack++;
			} else
				mad_hack = 0;
			result = drive ? 0 : status_a;
			result = status_a;
			break;
		case 3: // STATUS register B
			result = drive ? 0 : status_b;
			result = status_b;
			break;
		case 4:
			result = track;
			break;
		case 5:
			result = sector;
			break;
		case 6:
			result = side;
			break;
		case 7:
			// TODO: if BUSY, do not provide anything!
			status_a &= (255 - 64);	// clear DRQ
			status_b &= 127; 	// turn RDREQ off after the first access, this is somewhat incorrect :-P
			result = cache[cache_p_cpu];
			cache_p_cpu = (cache_p_cpu + 1) & 511;
				printf("FDC: read_pointer is now %d, drive pointer %d" NL, cache_p_cpu, cache_p_drive);
#if 0
				if (read_pointer == 256) {
					//status_a &= 255 - 64;	// turn DRQ off
					//status_b &= 127;	// turn RDREQ off
					//status_a |= 32;		// turn EQ on (???)
				} else {
					//status_a &= (255-32);	// EQ->off
					//status_b |= 128;	// RDREQ->on
					//status_a |= 64;		// DRQ->on
				}
#endif
#if 0
			if (cache_p_cpu == cache_p_drive) {
				//status_a |= 32;               // turn EQ on
			} else {
				status_a &= 255 - 32;		// turn EQ off
			}
#endif
			if (!cache_p_cpu)
				 status_a |= 32;
			else
				status_a &= 255 - 32;
			break;
		case 8:
			result = clock;
			break;
		case 9:
			result = step;
			break;
		case 10:
			result = 0; // No "protection code" ...
			break;
		default:
			result = 0xFF;
			break;
	}
	last_addr = addr;
        printf("FDC: reading register %d result is $%02X" NL, addr, result);
	return result;
}

