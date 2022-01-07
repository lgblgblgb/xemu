/* Test-case for a primitive PC emulator inside the Xemu project,
   currently using Fake86's x86 CPU emulation.
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2022 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "bios.h"

#include "xemu/emutools_files.h"
#include "xemu/emutools_hid.h"

#include "cpu.h"
#include "video.h"
#include "memory.h"

#include <time.h>
#include <sys/time.h>


// All should be >= 0x100 !!!!
#define BIOS_TRAP_RESET	0x100
#define BIOS_TRAP_WAIT  0x101


#define TRAP_TABLE_SIZE 0x200
#define TRAP_TABLE_START 0x100


#define BIOS_PRINTF(...) do { \
	char _buf_[4096]; \
	snprintf(_buf_, sizeof _buf_, __VA_ARGS__); \
	video_write_string(_buf_); \
} while(0)



#if 0
#define BIOS_PRINTF(...) do { \
	char _buf[4096]; \
	snprintf(_buf, sizeof _buf, __VA_ARGS__); \
	bios_putstr(_buf); \
} while(0)


#define MAX_DRIVES 4

struct drive_st {
	int	fd;
	int	cylinders;
	int	heads;
	int	sectors;
	int	total_blocks;
	int	bios_num;
	const char *filename;
};

static struct drive_st drives[MAX_DRIVES];
#endif

static uint8_t *ram, *rom;	// will be filled by bios_init() which is called from memory_init() in memory.c

#if 0
void bios_putstr ( const char *s )
{
	printf("%s", s);
}



#define BIOS_ROM	(memory + ((BIOS_SEG) << 4))

static inline void bios_install_trap ( Uint16 ofs, Uint16 trap_no )
{
	Uint8 *t = BIOS_ROM + ofs;
	// The tick: the trap is simply an out opcode with the port number is the low byte of trap
	// then there is an extra byte, the high byte of trap.
	// the trap handler must modify CS:IP etc anyway!
	// The reason using OUT as trap: there is no real BIOS too much in Xemu. So ANY I/O opcodes in the BIOS
	// ROM segment can be treated as traps regardless of their true meaning, or lack of it.
	// Surely, the I/O callback must know about this, and check if the segment where the OUT opcode was is our BIOS ROM segment.
	t[0] = 0xE6;		// opcode of "OUT"
	t[1] = trap_no & 0xFF;
	t[2] = trap_no >> 8;	// not part of opcode!
}


void bios_install ( void )
{
	for (int a = 0; a < MAX_DRIVES; a++) {
		drives[a].bios_num = -1;
	}
	for (int a = 0; a < 0x100; a++)
		bios_install_trap(BIOS_INT_TRAPS_OFFSET + a * 3, a);
	bios_install_trap(0xFFF0, BIOS_TRAP_RESET);
}


int bios_add_drive ( const char *imgfn )
{
	static const struct {
		int c,h,s;
	} known_geos[] = {
		{},	// 5.25"  360K
		{},	// 3.5"   720K
		{},	// 5.25"  1.2M
		{},	// 3.5"  1.44M
	};
}


void bios_reset ( void )
{
	// Default interrupt vectors (just point to "dummy" trap opcodes, so we can handle them at Xemu level then!)A
	for (int a = 0; a < 0x400; a++) {
		ram[a++] = TRAP_TABLE_START;
		ram[a++] = TRAP_TABLE_START >> 8;
		ram[a++] = 0x00;	// segment, low byte
		ram[a++] = 0xF0;	// segment, high byte
	}
	BIOS_PRINTF("XEMU-BIOS: %dKbytes address space.\n", MEMORY_SIZE >> 10);
	for (int a = 0; a < MAX_DRIVES; a++) {
		BIOS_PRINTF("  drive %c: ", a + 'A');
		if (drives[a].fd < 0) {
			bios_putstr("NOT PRESENT\n");
		} else {
			BIOS_PRINTF("%s %dK CHS=%d,%d,%d\n",
				a & 2 ? "HDD" : "FDD",
				drives[a].total_blocks >> 1,
				drives[a].cylinders,
				drives[a].heads,
				drives[a].sectors
			);
		}

	}
	memory_write_word(0x40, 0x13, 640);	// memory size
}

static struct drives_st *get_drive_struct ( unsigned int bios_drive )
{
	if (bios_drive < 0)
		return NULL;
	for (int a = 0; a < MAX_DRIVES; a++)
		if (drives[a].bios_num == bios_drive)
			return drives + a;
	return NULL;
}


static int get_lba_offset ( struct drives_st *drv, unsigned int cyl, unsigned int head, unsigned int sect)
{
	if (cyl > drv->cylinders || head > drv->heads || sect > drv->sectors)
		return -1;
	return () << 9;	// in bytes, rather than sectors!
}



static int bios_readblocks ( int drive, unsigned int no, Uint16 seg, unsigned int ofs, unsigned int cyl, unsigned int head, unsigned int sect )
{
	struct drives_st *drv = get_drive_struct(drive);
	if (!drv)
		return -1;	// invalid drive
	int blk = get_lba_offset(drv, cyl, head, sect);
	if (blk < 0)
		return -1;	// bad CHS ...
	if (lseek(drv->fd, blk, SEEK_SET) != (off_t)blk)
		return -1;	// seek error ...
	int no_read = 0;
	while (no_read < no) {
		Uint8 buf[0x200];
		if (read(drv->fd, buf, 0x200) != 0x200) {
			// READ ERROR!!!
		}
		for (int a = 0; a < 0x200; a++) {
			if (ofs > 0xFFFF) {
				// segment overflow!!
			}
			memory_write_byte(seg, ofs++, buf[a]);
		}
		no_read++;
	}
	return no_read;
}


static int boot_from ( int drive )
{
	BIOS_PRINTF("Trying to boot from drive 0x%02X ... ", drive);
	if (bios_readblocks(drive, 1, 0x0000, 0x7C00, 0, 0, 0) != 1)
		BIOS_PRINTF("no-drive/no-disk or read-error\n");
	else {
		if (!(memory[0x7C00 + 0x1FE] == 0x55 && memory[0x7C00 + 0x1FF] == 0xAA)) {
			BIOS_PRINTF("not a valid boot block\n");
			return 1;
		}
		// If boot WAS OK, set registers
		X86_CS = 0x0000;
		X86_IP = 0x7C00;
		X86_DL = drive;		// drive number was boot from
		X86_DH = 0;		// must be zero, some BIOSes passes extra info here to MBR routines
		// Not so standard, but *some* BIOSes use defaults like this. Anyway, can't hurt ...
		X86_DS = 0x0000;
		X86_ES = 0x0000;
		X86_SS = 0x0000;
		X86_SP = 0x0400;
		BIOS_PRINTF("OK, bootstrap code starting @ %04X:%04X\n", X86_CS, X86_IP);
		return 0;
	}
	return 1;
}


// drive = BIOS drive number to boot from, or -1 to try all.
void bios_boot ( int drive )
{
	if (drive < 0) {
		if (!boot_from(drive))
			return;
	} else {
		static const int boot_drives[] = {0, 1, 0x80, 0x81, -1};
		for (const int *a = boot_drives; *a != -1; a++)
			if (!boot_from(*a))
				return;
	}
	// TODO
	FATAL("Cannot boot");
}


void bios_handle_traps ( int trap )
{
	switch (trap) {
		case 0x12:
			X86_AX = memory_read_word(0x40, 0x13);	// return memory size in Kbytes;
			break;
		case 0x13:	// DISK INTERRUPT
			switch (X86_AX >> 8) {	// AH = service number
				case 0x02:	// read sector(s)
					bios_readblocks(
						X86_DX & 0xFF,	// DL = drive number
						X86_AX & 0xFF,	// AL = number of sectors
						X86_ES,		// segment of buffer
						X86_BX,		// offset of buffer
						X86_CX >> 6,	// CH&CL(top 2 bits): cylinder
						X86_DX >> 8,	// DH = head
						X86_CX & 63	// CL(low 6 bits only): sector
					);
				default:


			}
			break;
		case 0x18:	// ROM BIOS
			FATAL("ROM BASIC IS CALLED");
			return;	// do not continue execution
		case BIOS_TRAP_RESET:
			bios_reset();
			bios_boot(-1);
			return;	// do not continue execution
		default:
			if (trap < 0x100) {
				DEBUGPRINT("Unhandled interrupt 0x%02X\n", trap);
				X86_CS = 0xF000;
				X86_IP = IRET;

			} else {
				DEBUGPRINT("Unknown BIOS TRAP: 0x%04X!\n", trap);
				exit(1);	// TODO !!!
			}
	}
	// Perform an IRET operation by "hand"
}

#endif

static uint8_t TO_BCD ( const uint8_t bin )
{
	return ((bin / 10) << 4) + (bin % 10);
}


static inline void write_mem_byte ( const uint32_t addr, const uint8_t data )
{
	ram[addr] = data;
}


static inline void write_mem_word ( const uint32_t addr, const uint16_t data )
{
	ram[addr] = data;
	ram[addr + 1] = data >> 8;
}


static uint8_t *floppy = NULL;


static void bios_int_13h_02h_read ( unsigned int disk, unsigned int side, unsigned int track, unsigned int sector, unsigned int putseg, unsigned int putofs, unsigned int num )
{
	X86_AL = 0;
	if (disk != 0) {
		DEBUGPRINT("INT13H-READ: no such drive: %u" NL, disk);
		X86_CF = 1;
		X86_AH = 0x80;
		return;
	}
	if (sector == 0 || sector > 18) {
		DEBUGPRINT("INT13H-READ: invalid sector: %u" NL, sector);
		X86_CF = 1;
		X86_AH = 0x04;
		return;
	}
	if (track > 79) {
		DEBUGPRINT("INT13H-READ: invalid track: %u" NL, track);
		X86_CF = 1;
		X86_AH = 0x04;
		return;
	}
	if (side > 1) {
		DEBUGPRINT("INT13H-READ: invalid side: %u" NL, side);
		X86_CF = 1;
		X86_AH = 0x04;
		return;
	}
	if (num < 1 || num > 18) {
		DEBUGPRINT("INT13H-READ: invalid number of blocks to read: %u" NL, num);
		X86_CF = 1;
		X86_AH = 0x04;
		return;
	}
	// 1.44Mb (80 cylinders, 2 heads, 18 each 512b sectors per track).
	// DIRECTORY SHOULD BE AT OFFSET $2600 (9728) which is sector offset 19 ...
	//INT13H-READ: wanna read :) track=0 sector=2 side=1 NUM=1, buffer=0000:0500
	//INT13H-READ: computed fd offset is 512
	DEBUGPRINT("INT13H-READ: wanna read :) track=%u sector=%u side=%u NUM=%u, buffer=%04X:%04X" NL, track, sector, side, num, putseg, putofs);
	unsigned int fofs = track * 18 * 2 + (sector - 1) + 18 * side;
	fofs *= 512;
	DEBUGPRINT("INT13H-READ: computed fd offset is %u" NL, fofs);
	do {
		if (fofs >= 1474560) {
			DEBUGPRINT("INT13H-READ: invalid disk offset got: %u" NL, fofs);
			X86_CF = 1;
			X86_AH = 0x04;
			return;
		}
		for (int i = 0; i < 512; i++) {
			if (putofs > 0xFFFF) {
				DEBUGPRINT("INT13H-READ: invalid memory offset encounterd" NL);
				X86_CF = 1;
				X86_AH = 0x08;	// DMA overrun?
				return;
			}
			const uint32_t lin = (putseg << 4) + putofs;
			//DEBUGPRINT("DISK-BYTE: $%02X %c from %d to %04X:%04X (%X)" NL, floppy[fofs], floppy[fofs] >= 32 && floppy[fofs] < 127 ? floppy[fofs] : '.', fofs, putseg, putofs, lin);
			write86(lin, floppy[fofs]);
			putofs++;
			fofs++;
		}
		DEBUGPRINT("INT13H-READ: read was OK, from disk offset %u" NL, fofs - 512);
		X86_AL++;
		num--;
	} while (num);
	DEBUGPRINT("INT13H-READ: FINAL OK :)" NL);
	X86_CF = 0;	// no error
	X86_AH = 0;	// error code (no error)
}


static void bios_int_13h_disk ( void )
{
	iret86();
	DEBUGPRINT("INT13H: calling func AH=$%02X" NL, X86_AH);
	switch (X86_AH) {
		case 0x00:	// DISK reset
			X86_CF = 0;	// OK
			X86_AH = 0;	// no error code
			break;
		case 0x02:	// DISK read
			bios_int_13h_02h_read(
				X86_DL,	// disk
				X86_DH,	// "side" (head)
				X86_CH,	// track?
				X86_CL,	// sector,
				X86_ES,	// buffer to read, segment
				X86_BX,	// buffer to read, offset
				X86_AL	// number of sectors to read
			);
			break;
		case 0x08:	// query format?
			if (X86_DL != 0) {
				X86_CF = 1;
				X86_AH = 1;	// non-existing drive or func number
				break;
			}
			X86_CF = 0;
			X86_BL = 4;	// some sources states, that BL should be filled, 4 means 1.44Mb floppy! TODO check this!!!
			X86_AX = 0;
			X86_CH = 80;	// low 8 bits of max cylinder number;
			X86_CL = 18;	// maximum sector number?
			X86_DH = 2;	// two heads?
			X86_DL = 1;	// number of drives?
			break;
		case 0x15:	// query drive type
			if (X86_DL != 0) {
				X86_CF = 1;
				X86_AH = 1;	// non-existing drive or func number
				break;
			}
			X86_CF = 0;
			X86_AH = 2;
			break;
		case 0x16:	// disk change query
			if (X86_DL != 0) {
				X86_CF = 1;
				X86_AH = 1;	// non-existing drive or func number
				break;
			}
			X86_CF = 0;
			X86_AH = 0;	// no disk change happened
			break;
		default:
			if (X86_DL >= 0x80) {	// assuming that in all non-implemented func, it means drive number, and >= 0x80 means HDD, which is not supported now!
				X86_CF = 1;	// error!
				X86_AH = 1;	// non-existing drive or func number
				break;
			}
			FATAL("INT13H: unimplemented function: $%02X DL was $%02X" NL, X86_AH, X86_DL);
			break;
	}
}


static void bios_int_19h_boot ( void )
{
	BIOS_PRINTF("Trying to boot: ");
	if (!floppy) {
		int ret = xemu_load_file("dos-boot.img", NULL, 1474560, 1474560, "Cannot load boot floppy");
		if (ret < 0)
			FATAL("End :-(");
		floppy = xemu_load_buffer_p;
		xemu_load_buffer_p = NULL;
		BIOS_PRINTF("FD image loaded with %d bytes.\r\n", ret);
	} else
		BIOS_PRINTF("already loaded.\r\n");
	X86_SP = 0x7C00;	// set some value to SP, 0x400 this seems to be common in BIOSes
	X86_CS = 0;
	X86_IP = 0x7C00;	// 0000:7C000 where we loaded boot record and what BIOSes do in general
	X86_SS = 0;
	X86_DS = 0;
	X86_ES = 0;
	X86_DX = 0;		// drive to boot from: DL only, but some BIOSes passes DH=0 too ...
	//decodeflagsword(0);	// clear all flags just to be safe
	memcpy(ram + X86_IP, floppy, 512);	// copy boot sector to 000:7C00
	BIOS_PRINTF("Executing MBR at %04X:%04X\r\n\r\n", X86_CS, X86_IP);
}



static void bios_reset ( void )
{
	video_clear();
	video_write_string("Xemu BIOS has been installed :-)\r\n\r\n");
	memset(ram, 0, 0x500);
	// Default interrupt vectors (just point to "dummy" trap opcodes, so we can handle them at Xemu level then!)A
	for (unsigned int a = 0, ino = 0; a < 0x400; ino++) {
		ram[a++] = (TRAP_TABLE_START + ino) & 0xFF;
		ram[a++] = (TRAP_TABLE_START + ino) >> 8;
		ram[a++] = 0x00;	// segment, low byte
		ram[a++] = 0xF0;	// segment, high byte
	}
	write_mem_word(0x413, 640);		// base memory size
	X86_CS = 0xF000;
	X86_IP = TRAP_TABLE_START + 0x19;	// jump to the boot ...
}


static void(*wait_callback)(void) = NULL;
static uint8_t da_key = 0;

static void go_wait ( void(*callback)(void) )
{
	X86_CS = 0xF000;
	X86_IP = TRAP_TABLE_START + BIOS_TRAP_WAIT;
	wait_callback = callback;
}


static int emu_callback_key_raw_sdl ( SDL_KeyboardEvent *ev )
{
	if (ev->state == SDL_PRESSED) {
		fprintf(stderr, "RAW EVENT! %d mod=%d\n", ev->keysym.sym, ev->keysym.mod);
		if (ev->keysym.sym > 0 && ev->keysym.sym < 32) {
			da_key = ev->keysym.sym;
		}
	}
	static SDL_Keymod lastmod = 0;
	if (ev->keysym.mod != lastmod) {
		DEBUGPRINT("MODIFIER CHANGE: %u -> %u" NL, (uint32_t)lastmod, (uint32_t)ev->keysym.mod);
		lastmod = ev->keysym.mod;
	}
	return 1;	// allow to run default handler
}


static int emu_callback_key_textediting_sdl ( SDL_TextEditingEvent *ev )
{
	fprintf(stderr, "TEXT EDIT EVENT: \"%s\"\n", ev->text);
	if (ev->text[0])
		fprintf(stderr, "******** WOW NOT ZERO STR ********\n");
	return 1;	// allow to run default handler
}


static int emu_callback_key_textinput_sdl ( SDL_TextInputEvent *ev )
{
	fprintf(stderr, "TEXT INPUT EVENT: \"%s\"\n", ev->text);
	const uint8_t c = ev->text[0];
	if (c >= 32 && c < 128)
		da_key = c;
	return 1;	// allow to run default handler
}


static void wait_on_int16_00 ( void )
{
	if (da_key) {
		iret86();
		X86_AX = da_key;
		da_key = 0;
		wait_callback = NULL;
		// X86_CF = 0;
	}
}

static int cursor_x, cursor_y;


// used by cpu.c !
// must return non-zero if INTO (op $CE) can be executed
int into_opcode ( void )
{
	// check if "INTO" was used as a trap opcode inside our "fake" ROM
	uint32_t addr32 = (X86_CS << 4) + ((X86_IP - 1) & 0xFFFF);
	if (addr32 < 0xF0000 || addr32 >= 0x100000)
		return 1;	// not used as trap in our "fake" ROM, return with non-zero to allow to execute the opcode
	X86_CS = 0xF000;		// normalize seg:ofs for 0xF000-based scheme
	X86_IP = (addr32 - 1) & 0xFFFF;	// ... and for ofs, though correct the IP (pointing to the opcode was executed)
 	addr32 -= 0xF0000 + TRAP_TABLE_START;
	if (addr32 >= TRAP_TABLE_SIZE)
		FATAL("Invalid TRAP location in our ROM at F000:%04X", X86_IP);	// FIXME: remove this! user code can jump into ROM to random address having CE there and causing Xemu to exit with this :(
	// Seems to be a valid trap in our trap table. Decide what to do ...
	const uint16_t old_ip = X86_IP, old_cs = X86_CS;
	switch (addr32) {
		case BIOS_TRAP_WAIT:
			if (wait_callback) {
				wait_callback();
				if (wait_callback) {	// recheck if callback itself cleared itself
					// No! So re-schedule CPU emulation for the trap
					go_wait(wait_callback);
					return 0;	// !!!!!!!
				}
				// if callback cleared itself, it's callback responsibility to do something with the current CS:IP, like calling iret86() or something!
			} else
				iret86();	// should NOT happen, unless user jumps onto the trap for whatever evil reason ...
			break;
		case BIOS_TRAP_RESET:
			bios_reset();
			break;
		default:
			if (addr32 < 0x100) {
				DEBUGPRINT("INTERRUPT: $%02X AH=$%02X" NL, addr32, X86_AH);
				switch (addr32) {
					case 0x10:
						if (X86_AH == 0) {	// set video mode
							iret86();
							break;
						} else if (X86_AH == 0x8) { // read current colour + character
							iret86();
							X86_AL = 0x20; // lie space
							X86_AH = 7; // lie colour 7
							break;
						} else if (X86_AH == 0x9) { // write char + colour in, TODO
							iret86();
							break;
						} else if (X86_AH == 0xE) {
							video_write_char(X86_AL);
							iret86();
							break;
						} else if (X86_AH == 0xF) {
							iret86();
							X86_AL = 3;	// 80*25 colour adapter
							X86_AH = 80;	// chars per row
							X86_BH = 0;	// video page number
							break;
						} else if (X86_AH == 0x1B) { // some esoteric stuff needed by QBASIC
							iret86();
							X86_CF = 1;
							X86_AX = 0;
							break;
						} else if (X86_AH == 0xFA || X86_AH == 0x10 || X86_AH == 0x12 || X86_AH == 0x11 || X86_AH == 0xEF || X86_AH == 0xFE ) { // no idea at all :(
							iret86();
							X86_CF = 1;
							X86_AH = 0xFF;
							break;
						} else if (X86_AH == 0x05) { // set video page
							iret86();
							X86_CF = 0;
							// do nothing for now ...
							break;
						} else if (X86_AH == 0x03) { // query cursor pos
							iret86();
							X86_CF = 0;
							// TODO BH is video page number as input the query refers ...
							X86_DL = cursor_x;
							X86_DH = cursor_y;
							X86_CH = 7;	// cursor begin
							X86_CL = 9;	// cursor ends
							break;
						} else if (X86_AH == 0x02) { // set cursor pos
							iret86();
							X86_CF = 0;
							// TODO BH is video page number as positioning refers
							cursor_x = X86_DL;
							cursor_y = X86_DH;
							break;
						} else if (X86_AH == 0x01) { // set cursor shape
							iret86();
							X86_CF = 0;
							// do nothing for now ...
							break;
						} else if (X86_AX == 0x1A00) { // some VGA stuff
							iret86();
							X86_CF = 0;
							X86_AL = 0x1A;
							X86_BL = 0x08;	// VGA with colour monitor
							X86_BH = 0x08;	// for the "alternate" display, whatever it is ...
							break;
						}
							FATAL("Unimplemented int10h service: $%02X" NL, X86_AH);
						break;
					case 0x11:
						iret86();
						X86_AX =
							1 +		// 1 = one or more floppy disk drives present(s)
							0 +		// 2 = FPU presents
							(2 << 4) +	// val 2 -> 80*25 colour adapter
							(0 << 6) 	// number of disk drives, 0 means 1!!
						;
						break;
					case 0x12:
						iret86();
						X86_AX = 640;	// we have 640K base RAM
						break;
					case 0x13:
						bios_int_13h_disk();
						break;
					case 0x14:
						iret86();
						if (X86_AH == 0x00) {	// init serial port
							X86_CF = 1;
							X86_AH = 0x86;	// we don't have that ;)
						} else
							FATAL("Unimplemented int14h service: $%02X" NL, X86_AH);
						break;
					case 0x15:
						iret86();
						if (X86_AH == 0xC0) {	// query bios version etc
							X86_CF = 1;
							X86_AH = 0x86;	// we don't have that ;)
						} else if (X86_AH == 0x41 || X86_AH == 0x86 || X86_AH == 0x88 || X86_AH == 0xC1) {	// wtf is this, some waiting
							X86_CF = 1;
							X86_AH = 0x86;
						} else if (X86_AH == 0x24) {	// A20 gate stuff
							if (X86_AL == 0x00) {
								X86_CF = 1, X86_AH = 0x86; // cannot be disabled
							} else if (X86_AL == 0x01) {	// enable!
								X86_CF = 0, X86_AH = 0;
							} else if (X86_AL == 0x02) {	// status
								X86_CF = 0, X86_AH = 1;
							} else if (X86_AL == 0x03) {	// query support
								X86_CF = 0, X86_AH = 0, X86_BX = 1;
							} else
								FATAL("Unknown INT15H AX=%04X A20 functionality ..." NL, X86_AX);
						} else
							FATAL("Unimplemented int15h service: $%02X" NL, X86_AH);
						break;
					case 0x16:
						//DEBUGPRINT("INT16H: function $%02X is called" NL, X86_AH);
						if (X86_AH == 0) {		// get key with waiting.
							go_wait(wait_on_int16_00);	// do NOT do iret86 when on_wait is used!!!!!!
							return 0;	// !!!!
						} else if (X86_AH == 1) {
							iret86();
							X86_CF = 0;
							if (da_key) {
								X86_ZF = 0;
								X86_AX = da_key;
							} else {
								X86_ZF = 1;	// no char in buffer if Z is set, interestingly if I do that, "Starting MS-DOS" hangs :-@
								X86_AX = 0;	// null then?!
							}
						} else if (X86_AH == 2) {
							iret86();
							X86_AL = 0;		// keyboard status (modifier keys etc)
							X86_ZF = 1;
							X86_CF = 0;
						} else if (X86_AH == 0x55) {	// some QBASIC TSR stuff ....
							iret86();
							X86_CF = 1;
						} else
							FATAL("Unimplemented int16h service: $%02X" NL, X86_AH);
						break;
					case 0x17:
						iret86();
						if (X86_AH == 0x01) {	// printer???
							X86_CF = 1;
							X86_AH = 0xFF;	// some status
						} else
							FATAL("Unimplemented int17h service: $%02X" NL, X86_AH);
						break;
					case 0x19:
						bios_int_19h_boot();
						break;
					case 0x1A:
						DEBUGPRINT("INT1AH: calling function $%02X" NL, X86_AH);
						iret86();
						if (X86_AH == 0) {	// query time counter.
							// calculate the timer IRQ frequency exactly starting the from NTSC carrier (315/88 MHz) was choosen as base even the
							// clock of the first PC (around 4.77MHz)
							static const double tick_hz = 315.0l/88.0l*4.0l/3.0l/4.0l/65536.0l*1000000.0l;
							struct timeval tv;
							gettimeofday(&tv, NULL);
							//const time_t uts = time(NULL);
							const time_t t = tv.tv_sec;	// this step is needed, since mingw drops some ugly warning if used directly :-/
							const struct tm *tim = localtime(&t);
							const uint32_t time_counter = (uint32_t)(double)((
								(double)((uint32_t)tim->tm_hour * 3600 + tim->tm_min * 60 + tim->tm_sec) + (double)tv.tv_usec / 1000000.0l
							) * tick_hz);
							X86_DX = time_counter & 0xFFFF;
							X86_CX = time_counter >> 16;
							X86_AL = 0;
							X86_CF = 0;
						} else if (X86_AH == 2) {	// real time clock read
							const time_t uts = time(NULL);
							const struct tm *tim = localtime(&uts);
							X86_CH = TO_BCD(tim->tm_hour);
							X86_CL = TO_BCD(tim->tm_min);
							X86_DH = TO_BCD(tim->tm_sec);
							X86_DL = 0;
							X86_CF = 0;	// if set -> battery is flat ;)
						} else if (X86_AH == 4) {       // real time clock read date
							const time_t uts = time(NULL);
							const struct tm *tim = localtime(&uts);
							X86_CH = TO_BCD((tim->tm_year + 1900) / 100);
							X86_CL = TO_BCD(tim->tm_year % 100);
							X86_DH = TO_BCD(tim->tm_mon + 1);
							X86_DL = TO_BCD(tim->tm_mday);
							X86_CF = 0;	// if set -> battery is flat ;)
						} else
							FATAL("Unimplemented int1Ah service: $%02X" NL, X86_AH);
						break;
					default:
						FATAL("Unimplemented interrupt by Xemu: $%02X [AH=$%02X DL=$%02X]", addr32, X86_AH, X86_DL);
				}
			} else
				FATAL("Unknown trap in Xemu: $%02X", addr32);
			break;
	}
	if (old_ip == X86_IP && old_cs == X86_CS)
		FATAL("IP was not modified after trap in Xemu: $%X!", addr32);
	return 0;	// do NOT allow to execute the original INTO opcode!
}




#define OPCODE_FAR_JMP 0xEA

#define PUT_FAR_JMP(ofs_put, ofs_jump) do { \
	rom[ofs_put + 0] = OPCODE_FAR_JMP; \
	rom[ofs_put + 1] = (ofs_jump) & 0xFF; \
	rom[ofs_put + 2] = (ofs_jump) >> 8; \
	rom[ofs_put + 3] = 0x00; \
	rom[ofs_put + 4] = 0xF0; \
} while(0)


void bios_init ( uint8_t *in_base_memory, uint8_t *in_rom_memory )
{
	static int init_done = 0;
	if (init_done)
		FATAL("%s() can be called only once!", __func__);
	init_done = 1;
	ram = in_base_memory;
	rom = in_rom_memory;
	memset(rom, 0, 0x10000);
	// Install "traps" as 'INTO' opcodes inside the ROM for default interrupt handlers, and other stuffs:
	memset(rom + TRAP_TABLE_START, 0xCE, TRAP_TABLE_SIZE);
	// Special traps
	PUT_FAR_JMP(0xFFF0, BIOS_TRAP_RESET + TRAP_TABLE_START);	// reset vector at FFFF:0000 == F000:FFF0 should jump to the RESET trap
	bios_reset();
	// register special events from the HID layer
	hid_register_sdl_keyboard_event_callback(HID_CB_LEVEL_EMU, emu_callback_key_raw_sdl);
	hid_register_sdl_textediting_event_callback(HID_CB_LEVEL_EMU, emu_callback_key_textediting_sdl);
	hid_register_sdl_textinput_event_callback(HID_CB_LEVEL_EMU, emu_callback_key_textinput_sdl);
	SDL_StartTextInput();
}
