/* Test-case for a very simple, inaccurate, work-in-progress Commodore 65 / MEGA65 emulator,
   within the Xemu project. F011 FDC core implementation.
   Copyright (C)2016,2018 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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


#include "xemu/emutools.h"
#include "xemu/f011_core.h"
#include "xemu/cpu65.h"





// #define DEBUG_FOR_PAUL
// #define SOME_DEBUG


static Uint8 head_track;		// "physical" track, ie, what is the head is positioned at currently
static int   head_side;
static Uint8 track, sector, side;	// parameters given for an operation ("find"), if track is not the same as head_track, you won't find your sector on that track probably ...
static Uint8 control, status_a, status_b;
static Uint8 cmd;
static int   curcmd;
static Uint8 dskclock, step;
static int   emulate_busy;
static int   drive;
static Uint8 *cache;			// 512 bytes cache FDC will use. This is a real 512byte RAM attached to the FDC controller for buffered operations on the C65
static int   cache_p_cpu;		// cache pointer if CPU accesses the FDC buffer cache. 0 ... 511!
static int   cache_p_fdc;		// cache pointer if FDC accesses the FDC buffer cache. 0 ... 511!
//static int   disk_fd;			// disk image file descriptor (if disk_fd < 0 ----> no disk image, ie "no disk inserted" is emulated)
static int   swap_mask = 0;

static int   warn_disk = 1;
static int   warn_swap_bit = 1;

static void  execute_command ( void );

static int   have_disk, have_write;



void fdc_init ( Uint8 *cache_set )
{
	DEBUG("FDC: init F011 emulation core" NL);
	cache = cache_set;
	head_track = 0;
	head_side = 0;
	control = 0;
	status_a = 0;
	status_b = 0;
	track = 0;
	sector = 0;
	side = 0;
	cmd = 0;
	curcmd = -1;
	dskclock = 0xFF;
	step = 0xFF;
	cache_p_cpu = 0;
	cache_p_fdc = 0;
	drive = 0;
	fdc_set_disk(0, 0);
	status_b &= 0x7F;	// at this point we don't want disk changed signal (bit 7) yet
}



void fdc_set_disk ( int in_have_disk, int in_have_write )
{
	have_disk  = in_have_disk;
	have_write = in_have_write;
	DEBUG("FDC: init: set have_disk=%d, have_write=%d" NL, have_disk, have_write);
	status_b |= 0x80;	// disk changed signal is set, since the purpose of this function is to set new disk
	if (have_disk) {
		status_a |= 1;	// on track-0
		status_b |= 8;	// disk inserted
	} else {
		status_a &= ~1;
		status_b &= ~8;
	}
	if (!have_write) {
		status_a |= 2;	// write protect flag, read-only mode
	} else {
		status_a &= ~2;
	}
}



static int calc_offset ( const char *opdesc )
{
	int offset;
	DEBUG("FDC: %s sector track=%d sector=%d side=%d @ PC=$%04X" NL, opdesc, track, sector, side, cpu65.old_pc);
	// FIXME: no checking of input parameters, can be insane values
	// FIXME: no check for desired track/side and the currently selected/seeked, what should be!
	// FIXME: whatever :)
	offset = 40 * (track - 0) * 256 + (sector - 1) * 512 + side * 20 * 256; // somewhat experimental value, it was after I figured out how that should work :-P
	if (offset < 0 || offset > D81_SIZE - 512 || (offset & 511)) {
		DEBUG("FDC: invalid D81 offset %d" NL, offset);
		return -1;
	}
	return offset;
}


/* Note: this is really not so nice, but faster and easier to emulate ;)
   That is, we read 512 bytes at once. Not byte-by-byte and allow program
   to see this with DRQ changing, pointer maintaince on each byte read/written, etc ... */


static void read_sector ( void )
{
	int error;
	if (have_disk) {
		int offset = calc_offset("reading");
		error = (offset < 0);
		if (!error) {
			Uint8 read_buffer[512];
			error = fdc_cb_rd_sec(read_buffer, offset);
			if (error)
				DEBUG("FDC: sector read-callback returned with error!" NL);
			else {
				int n;
				DEBUG("FDC: sector has been read." NL);
				for (n = 0; n < 512; n++) {
					cache[cache_p_fdc] = read_buffer[n];
					cache_p_fdc = (cache_p_fdc + 1) & 511;
				}
			}
		}
	} else {
		error = 1;
		DEBUG("FDC: no disk in drive" NL);
		if (warn_disk) {
			INFO_WINDOW("No disk image was given or can be loaded!");
			warn_disk = 0;
		}
	}
	if (error) {
		status_a |= 16; // record not found ...
		status_b &= 15; // RDREQ/WTREQ/RUN/GATE off
	} else {
		status_a |= 64; // DRQ, for buffered reads indicates that FDC accessed the buffer last (DRQ should be cleared by CPU reads, set by FDC access)
		status_b |= 128 | 32 ;  // RDREQ, RUN to set! (important: ROM waits for RDREQ to be high after issued read operation, also we must clear it SOME time later ...)
		status_a &= ~32;  // clear EQ, missing this in general freezes DOS as it usually does not expect to have EQ set even before the first data read [??]
	}
}





static void write_sector ( void )
{
	int error;
	if (have_disk && have_write) {
		int offset = calc_offset("writing");
		error = (offset < 0);
		if (!error) {
			Uint8 write_buffer[512];
			int n;
			for (n = 0; n < 512; n++) {
				write_buffer[n] = cache[cache_p_fdc];
				cache_p_fdc = (cache_p_fdc + 1) & 511;
			}
			error = fdc_cb_wr_sec(write_buffer, offset);
			if (error)
				DEBUG("FDC: sector write-callback returned with error!" NL);
			else
				DEBUG("FDC: sector has been written." NL);
		}
	} else {
		error = 1;
		DEBUG("FDC: no disk in drive or write protected" NL);
		if (warn_disk) {
			INFO_WINDOW("No disk image was given or can be loaded or write protected disk!");
			warn_disk = 0;
		}
	}
	if (error) {
		status_a |= 16; // record not found ...
		status_b &= 15; // RDREQ/WTREQ/RUN/GATE off
	} else {
		status_a |= 64; // DRQ, for buffered reads indicates that FDC accessed the buffer last (DRQ should be cleared by CPU reads, set by FDC access)
		status_b |= 64 | 32 ;  // WTREQ, RUN to set!
		status_a &= ~32;  // clear EQ, missing this in general freezes DOS as it usually does not expect to have EQ set even before the first data read [??]
	}
}







void fdc_write_reg ( int addr, Uint8 data )
{
        DEBUG("FDC: writing register %d with data $%02X" NL, addr, data);
#ifdef DEBUG_FOR_PAUL
	printf("PAUL: FDC register %d will be written now, data is $%02X buffer pointer is %d now" NL, addr, data, cache_p_cpu);
#endif
	switch (addr) {
		case 0:
#if 0
			if (status_a & 128) {
				DEBUG("FDC: WARN: trying to write control register ($%02X) while FDC is busy." NL, data);
				return;
			}
#endif
			control = data;
			status_a |= 128;	// writing control register also causes to set the BUSY flag for some time ...
			if (curcmd == -1)
				curcmd = 0x100;		// "virtual" command, by writing the control register
			drive = data & 7;	// drive selection
			head_side = (data >> 3) & 1;
			if ((status_b & 0x80) && drive) {
				status_b &= 0x7F;	// clearing disk change signal (not correct implementation, as it needs only if the given drive deselected!)
				DEBUG("FDC: disk change signal was cleared on drive selection (drive: %d)" NL, drive);
			}
			if (drive)
				DEBUG("FDC: WARN: not drive-0 is selected: %d!" NL, drive);
			else
				DEBUG("FDC: great, drive-0 is selected" NL);
			if (data & 16) {
				DEBUG("FDC: WARN: SWAP bit emulation is experimental!" NL);
				if (warn_swap_bit) {
					DEBUGPRINT("FDC: warning: SWAP bit emulation is experimental! There will be no further warnings on this." NL);
					warn_swap_bit = 0;
				}
				swap_mask = 0x100;
			} else
				swap_mask = 0;
			break;
		case 1:
			// FIXME: I still don't what happens if a running operation (ie BUSY) is in progress and new command is tried to be given to the F011 FDC
			DEBUG("FDC: command=$%02X (lower bits: $%X)" NL, data & 0xF8, data & 7);
			if ((status_a & 128) && ((data & 0xF8))) {	// if BUSY, and command is not the cancel command ..
				DEBUG("FDC: WARN: trying to issue another command ($%02X) while the previous ($%02X) is running." NL, data, cmd);
				return;
			}
			cmd = data;
			curcmd = data;
			status_a |= 128; 	// simulate busy status ...
			status_b &= 255 - 2;	// turn IRQ flag OFF
			status_a &= 255 - (4 + 8 + 16);	// turn RNF/CRC/LOST flags OFF
			status_b &= 255 - (128 + 64);	// also turn RDREQ and WRREQ OFF [IS THIS REALLY NEEDED?]
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
		case 7:
			// FIXME: this algorithm do not "enforce" the internals of F011, just if the software comply the rules otherwise ......
			status_a &= ~64;	// clear DRQ
			//status_b &= 127; 	// turn RDREQ off after the first access, this is somewhat incorrect :-P
			if (status_a & 32)	// if EQ was already set and another byte passed ---> LOST
				status_a |= 4;	// LOST!!!! but probably incorrect, since no new read is done by FDC since then just "wrapping" the read data, LOST remains till next command!
			cache[cache_p_cpu ^ swap_mask] = data;
			cache_p_cpu = (cache_p_cpu + 1) & 511;
			// always check EQ-situation _after_ the operation! Since initially they are same and it won't be loved by C65 DOS at all, honoured with a freeze
			if (cache_p_cpu == cache_p_fdc)
				status_a |=  32;	// turn EQ on
			else
				status_a &= ~32;	// turn EQ off
			break;
		// TODO: write DATA register (7) [only for writing it is needed anyway]
		case 8:
			dskclock = data;
			break;
		case 9:
			step = data;
			break;
	}
	if (status_a & 128) {
		emulate_busy = 10;
		execute_command();	// do it NOW!!!! With this setting now: there is no even BUSY state, everything happens within one OPC... seems to work still. Real F011 won't do this surely
	}
#ifdef DEBUG_FOR_PAUL
	printf("PAUL: FDC register %d has been written, data was $%02X buffer pointer is %d now" NL, addr, data, cache_p_cpu);
#endif
}






static void execute_command ( void )
{
#ifdef DEBUG_FOR_PAUL
	printf("PAUL: issuing FDC command $%02X pointer was %d" NL, cmd, cache_p_cpu);
#endif
	status_a &= 127;	// turn BUSY flag OFF
	status_b |= 2;		// turn IRQ flag ON
	if (control & 128)
		INFO_WINDOW("Sorry, FDC-IRQ is not supported yet, by FDC emulation!");
	if (curcmd < 0)
		return;	// no cmd was given?!
	if (curcmd > 0xFF)
		return; // only control register was written, not the command register
#ifdef SOME_DEBUG
	printf("Command: $%02X" NL, cmd);
#endif
	switch (cmd & 0xF8) {	// high 5 bits of the command ...
		case 0x40:	// read sector
			//status_a |= 16;		// record not found for testing ...
			status_b |= 128;	// RDREQ: if it's not here, you won't get a READY. prompt!
			//status_b |= 32;		// RUN?!
			status_a |= 64;		// set DRQ
			status_a &= (255 - 32); // clear EQ
			//status_a |= 32; // set EQ?!
			//cache_p_cpu = cache_p_fdc;	// yayy .... If it's not here we can't get READY. prompt!!
			read_sector();
			//cache_p_drive = (cache_p_drive + BLOCK_SIZE) & 511;
			//////////////cache_p_cpu = 0; // yayy .... If it's not here we can't get READY. prompt!!
#ifdef SOME_DEBUG
			printf("READ: cache_p_cpu=%d / cache_p_fdc=%d drive_selected=%d" NL, cache_p_cpu, cache_p_fdc, drive);
#endif
			DEBUG("FDC: READ: head_track=%d need_track=%d head_side=%d need_side=%d need_sector=%d drive_selected=%d" NL,
				head_track, track, head_side, side, sector, drive
			);
			break;
		case 0x80:	// write sector
			if (!(status_a & 2)) {	// if not write protected ....
				status_a |= 64;         // set DRQ
				status_a &= (255 - 32); // clear EQ
				write_sector();
#ifdef SOME_DEBUG
				printf("WRITE: cache_p_cpu=%d / cache_p_fdc=%d drive_selected=%d" NL, cache_p_cpu, cache_p_fdc, drive);
#endif
				DEBUG("FDC: WRITE: head_track=%d need_track=%d head_side=%d need_side=%d need_sector=%d drive_selected=%d" NL,
                                	head_track, track, head_side, side, sector, drive
                        	);
			} else {
#ifdef SOME_DEBUG
				printf("Sorry, write protected stuff ..." NL);
#endif
			}
			break;
		case 0x10:	// head step out or no step
			if (!(cmd & 4)) {	// if only not TIME operation, which does not step!
				if (head_track)
					head_track--;
				if (!head_track)
					status_a |= 1;	// track 0 flag
				DEBUG("FDC: head position = %d" NL, head_track);
			}
			break;	
		case 0x18:	// head step in
			if (head_track < 128)
				head_track++;
			DEBUG("FDC: head position = %d" NL, head_track);
			status_a &= 0xFE;	// track 0 flag off
			break;
		case 0x20:	// motor spin up
			control |= 32;
			status_a |= 16; // according to the specification, RNF bit should be set at the end of the operation
			break;
		case 0x00:	// cancel running command?? NOTE: also if low bit is 1: clear pointer!
			// Note: there was a typo in my previous versions ... to have break HERE! So pointer reset never executed actually ...... :-@
			// It seems C65 DOS uses this, without this, the pointer maintaince is bad, and soon it will freeze on EQ check somewhere!
			// That's why I had to introduce some odd workarounds to reset pointers etc manually, which is NOT what F011 would do, I guess!
			if (cmd & 1) {
#ifdef SOME_DEBUG
				printf("BEFORE pointer reset: cache_p_cpu=%d cache_p_fdc=%d" NL, cache_p_cpu, cache_p_fdc);
#endif
				cache_p_cpu = 0;
				cache_p_fdc = 0;
				DEBUG("FDC: WARN: resetting cache pointers" NL);
				//status_a |= 32; // turn EQ on
				status_a &= 255 - 64; // turn DRQ off
				status_b &= 127;      // turn RDREQ off

			}
			break;
		default:
			DEBUG("FDC: WARN: unknown comand: $%02X" NL, cmd);
			//status_a &= 127; // well, not a valid command, revoke busy status ...
			break;
	}
	curcmd = -1;
#ifdef DEBUG_FOR_PAUL
        printf("PAUL: issued FDC command $%02X pointer is %d" NL, cmd, cache_p_cpu);
#endif
}





Uint8 fdc_read_reg  ( int addr )
{
	Uint8 result;
#ifdef DEBUG_FOR_PAUL
	printf("PAUL: FDC register %d will be read, buffer pointer is %d now" NL, addr, cache_p_cpu);
#endif
	/* Emulate BUSY timing, with a very bad manner: ie, decrement a counter on each register read to give some time to wait.
	   FIXME: not sure if it's needed and what happen if C65 DOS gots "instant" operations done without BUSY ever set ... Not a real happening, but it can be with my primitive emulation :)
	   Won't work if DOS is IRQ driven ... */
	if (status_a & 128) {	// check the BUSY flag
		// Note: this is may not used at all, check the end of write reg func!
		if (emulate_busy > 0)
			emulate_busy--;
		if (emulate_busy <= 0) {
#ifdef SOME_DEBUG
			printf("Delayed command execution!!!" NL);
#endif
			execute_command();	// execute the command only now for real ... (it will also turn BUSY flag - bit 7 - OFF in status_a)
		}
	}
	switch (addr) {
		case 0:
			result = control;
			break;
		case 1:
			result = cmd;
			break;
		case 2:	// STATUS register A
			result = drive ? 0 : status_a;	// FIXME: Not sure: if no drive 0 is selected, other status is shown, ie every drives should have their own statuses?
			//result = status_a;
			break;
		case 3: // STATUS register B
			result = drive ? 0 : status_b;	// FIXME: Not sure: if no drive 0 is selected, other status is shown, ie every drives should have their own statuses?
			//result = status_b;
			status_b &= ~64;	// turn WTREQ off, as it seems CPU noticed with reading this register, that is was the case for a while. Somewhat incorrect implementation ... :-/
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
			// FIXME: this algorithm do not "enforce" the internals of F011, just if the software comply the rules otherwise ......
			status_a &= ~64;	// clear DRQ
			status_b &= 127; 	// turn RDREQ off after the first access, this is somewhat incorrect :-P
			if (status_a & 32)	// if EQ was already set and another byte passed ---> LOST
				status_a |= 4;	// LOST!!!! but probably incorrect, since no new read is done by FDC since then just "wrapping" the read data, LOST remains till next command!
			result = cache[cache_p_cpu ^ swap_mask];
			cache_p_cpu = (cache_p_cpu + 1) & 511;
			// always check EQ-situation _after_ the operation! Since initially they are same and it won't be loved by C65 DOS at all, honoured with a freeze
			if (cache_p_cpu == cache_p_fdc)
				status_a |=  32;	// turn EQ on
			else
				status_a &= ~32;	// turn EQ off
			break;
		case 8:
			result = dskclock;
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
        DEBUG("FDC: reading register %d result is $%02X" NL, addr, result);
#ifdef DEBUG_FOR_PAUL
	printf("PAUL: FDC register %d has been read, result was $%02X buffer pointer is %d now" NL, addr, result, cache_p_cpu);
#endif
	return result;
}


/* --- SNAPSHOT RELATED --- */


#ifdef XEMU_SNAPSHOT_SUPPORT

#include <string.h>

#define SNAPSHOT_FDC_BLOCK_VERSION	0

#ifdef MEGA65
#define SNAPSHOT_FDC_BLOCK_SIZE		0x100
#else
#define CACHE_SIZE 512
#define SNAPSHOT_FDC_BLOCK_SIZE		(0x100 + CACHE_SIZE)
#endif

int fdc_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	Uint8 buffer[SNAPSHOT_FDC_BLOCK_SIZE];
	int a;
	if (block->block_version != SNAPSHOT_FDC_BLOCK_VERSION || block->sub_counter || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad FDC-F011 block syntax");
	a = xemusnap_read_file(buffer, sizeof buffer);
	if (a) return a;
	/* loading state ... */
	head_side = P_AS_BE32(buffer + 0);
	curcmd = P_AS_BE32(buffer + 4);
	emulate_busy = P_AS_BE32(buffer + 8);
	drive = P_AS_BE32(buffer + 12);
	cache_p_cpu = P_AS_BE32(buffer + 16);
	cache_p_fdc = P_AS_BE32(buffer + 20);
	swap_mask = P_AS_BE32(buffer + 24);
	have_disk = P_AS_BE32(buffer + 28);
	have_write = P_AS_BE32(buffer + 32);
	head_track = buffer[128];
	track = buffer[129];
	sector = buffer[130];
	side = buffer[131];
	control = buffer[132];
	status_a = buffer[133];
	status_b = buffer[134];
	cmd = buffer[135];
	dskclock = buffer[136];
	step = buffer[137];
#ifndef MEGA65
	memcpy(cache, buffer + 0x100, CACHE_SIZE);
#endif
	return 0;
}


int fdc_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_FDC_BLOCK_SIZE];
	int a = xemusnap_write_block_header(def->idstr, SNAPSHOT_FDC_BLOCK_VERSION);
	if (a) return a;
	memset(buffer, 0xFF, sizeof buffer);
	/* saving state ... */
	U32_AS_BE(buffer + 0, head_side);
	U32_AS_BE(buffer + 4, curcmd);
	U32_AS_BE(buffer + 8, emulate_busy);
	U32_AS_BE(buffer + 12, drive);
	U32_AS_BE(buffer + 16, cache_p_cpu);
	U32_AS_BE(buffer + 20, cache_p_fdc);
	U32_AS_BE(buffer + 24, swap_mask);
	U32_AS_BE(buffer + 28, have_disk);
	U32_AS_BE(buffer + 32, have_write);
	buffer[128] = head_track;
	buffer[129] = track;
	buffer[130] = sector;
	buffer[131] = side;
	buffer[132] = control;
	buffer[133] = status_a;
	buffer[134] = status_b;
	buffer[135] = cmd;
	buffer[136] = dskclock;
	buffer[137] = step;
#ifndef MEGA65
	memcpy(buffer + 0x100, cache, CACHE_SIZE);
#endif
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}

#endif
