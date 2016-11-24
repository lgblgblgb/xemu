/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

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

#include "xep128.h"
#include "primoemu.h"
#include "xemu/z80.h"
#include "cpu.h"
#include "dave.h"
#include "nick.h"
#include "zxemu.h"

#include "main.h"



int primo_on = 0;
int primo_nmi_enabled = 0;
int primo_rom_seg = -1;


// translates EP keyboard matrix  positions into Primo scan keys, indexed by primo scan keys
const Uint8 primo_key_trans[] = {
	0x22,		// scan 0 Y
	0x73,		// scan 1 UP-ARROW
	0x15,		// scan 2 S
	0x07,		// scan 3 SHIFT
	0x25,		// scan 4 E
	0xFF,		// scan 5 UPPER
	0x26,		// scan 6 W
	0x17,		// scan 7 CTR
	0x13,		// scan 8 D
	0x35,		// scan 9 3 #
	0x05,		// scan 10 X
	0x36,		// scan 11 2 "
	0x21,		// scan 12 Q
	0x31,		// scan 13 1 !
	0x16,		// scan 14 A
	0x71,		// scan 15 DOWN-ARROW
	0x03,		// scan 16 C
	0xFF,		// scan 17 ----
	0x14,		// scan 18 F
	0xFF,		// scan 19 ----
	0x23,		// scan 20 R
	0xFF,		// scan 21 ----
	0x24,		// scan 22 T
	0x30,		// scan 23 7 /
	0x10,		// scan 24 H
	0x86,		// scan 25 SPACE
	0x02,		// scan 26 B
	0x32,		// scan 27 6 &
	0x12,		// scan 28 G
	0x34,		// scan 29 5 %
	0x04,		// scan 30 V
	0x33,		// scan 31 4 $
	0x00,		// scan 32 N
	0x50,		// scan 33 8 (
	0x06,		// scan 34 Z
	0xFF,		// scan 35 + ?
	0x20,		// scan 36 U
	0x54,		// scan 37 0
	0x60,		// scan 38 J
	0xFF,		// scan 39 > <
	0x64,		// scan 40 L
	0x53,		// scan 41 - i
	0x62,		// scan 42 K
	0x84,		// scan 43 . :
	0x80,		// scan 44 M
	0x52,		// scan 45 9 ;
	0x90,		// scan 46 I
	0x82,		// scan 47 ,
	0xFF,		// scan 48 U"
	0x65,		// scan 49 ' #
	0x94,		// scan 50 P
	0xFF,		// scan 51 u' u"
	0x92,		// scan 52 O
	0xFF,		// scan 53 CLS
	0xFF,		// scan 54 ----
	0x76,		// scan 55 RETURN
	0xFF,		// scan 56 ----
	0x75,		// scan 57 LEFT-ARROW
	0xFF,		// scan 58 E'
	0xFF,		// scan 59 o'
	0xFF,		// scan 60 A'
	0x72,		// scan 61 RIGHT-ARROW
	0xFF,		// scan 62 O:
	0x37		// scan 63 BRK
};


#define EP_KBDM_SCAN(sc,resval) ((kbd_matrix[(sc) >> 4] & (1 << ((sc) & 15))) ? 0 : (resval))

// F1
#define PRIMOEMU_HOTKEY_RESET 0x47


void primo_switch ( Uint8 data )
{
	if (data & 128) {
		if (data & 64)
			primo_write_io(0, 0);
		if (primo_on)
			return;
		primo_on = 0x40;	// the value is important, used by port "limit" for lower primo area!
		zxemu_switch(0);	// switch ZX Spectrum emulation off, if already enabled for some reason ...
	} else {
		if (!primo_on)
			return;
		primo_on = 0;
	}
	DEBUG("PRIMOEMU: emulation is turned %s." NL, primo_on ? "ON" : "OFF");
	nmi_pending = 0;
}


static int primo_scan_key ( int scan )
{
	scan = primo_key_trans[scan];
	if (scan == 0xFF) return 0;
	return EP_KBDM_SCAN(scan, 1);
}


Uint8 primo_read_io ( Uint8 port )
{
	// Primo does the very same for all I/O ports in the range of 0-3F!
	// EXCEPT bit 0 which is the state of the key given by the port number.
	return (vsync ? 32 : 0) | EP_KBDM_SCAN(PRIMOEMU_HOTKEY_RESET, 2) | primo_scan_key(port);
}


#define LD1HI_PV1 (((PRIMO_VID_SEG << 6) + 0x28) & 0xFF)
#define LD1HI_PV0 (((PRIMO_VID_SEG << 6) + 0x08) & 0xFF)


void primo_write_io ( Uint8 port, Uint8 data )
{
	// Primo does the  very same for all I/O ports in the range of 0-3F!
	primo_nmi_enabled = data & 128;				// bit 7: NMI enabled, this variable is "cloned" as nmi_pending by VINT in dave.c
	ports[0xA8] = ports[0xAC] = (data & 16) ? 63 : 0;	// bit 4, speaker control, pass to Dave in D/A mode
	memory[(PRIMO_LPT_SEG << 14) + 5] = (data & 8) ? LD1HI_PV1 : LD1HI_PV0;		// bit 3: display page: set LPB LD1 high (LPB should be at segment 0xFD, and 0xFC is for primo 0xC000-0xFFFF)
}


int primo_search_rom ( void )
{
	int a;
	for (a = 0; a < 0xFC; a++)
		if (memory_segment_map[a] == ROM_SEGMENT && !memcmp(memory + (a << 14) + 0x168, "PRIMO", 5)) {
			DEBUG("PRIMO: found Primo ROM in segment %02Xh, good" NL, a);
			return a;
		}
	DEBUG("PRIMO: not found Primo ROM in the loaded ROMs ..." NL);
	return -1;
}


static const Uint8 primo_lpt[] = {
	// the USEFULL area of the screen, this must be the FIRST LPB in our LPT.
	// LPIXEL 2 colour mode is used, 192 scanlines are used
	256-192,14|16,  15, 47,  0,0,0,0,     1,0xFF,0,0,0,0,0,0,
	// bottom not used area ... 47 scanlines
	256-47,  2 | 128,  6, 63,  0,0,0,0,     0,0,0,0,0,0,0,0,	// 128 = ask for VINT, which is cloned as NMI in primo mode
	// SYNC etc stuffs ...
	256-3 ,  0, 63,  0,  0,0,0,0,     0,0,0,0,0,0,0,0,
	256-2 ,  0,  6, 63,  0,0,0,0,     0,0,0,0,0,0,0,0,
	256-1 ,  0, 63, 32,  0,0,0,0,     0,0,0,0,0,0,0,0,
	256-19,  2,  6, 63,  0,0,0,0,     0,0,0,0,0,0,0,0,
	// upper not used area ... 48 scanlines, the RELOAD bit must be set!!!!
	256-48,  3,  6, 63,  0,0,0,0,     0,0,0,0,0,0,0,0
};
#define MEMORY_BACKUP_SIZE (0xC000 + sizeof primo_lpt)
static Uint8 memory_backup[MEMORY_BACKUP_SIZE];
static Uint8 ports_backup[0x100];
static Z80EX_CONTEXT z80ex_backup;



void primo_emulator_exit ( void )
{
	int a;
	ports_backup[0x83] |= 128 | 64;
	for (a = 0x80; a < 0x84; a++)
		z80ex_pwrite_cb(a, ports_backup[a]);	// restore Nick registers
	for (a = 0xA0; a < 0xB5; a++)
		z80ex_pwrite_cb(a, ports_backup[a]);	// restore Dave registers
	memcpy(memory + 0xFA * 0x4000, memory_backup, MEMORY_BACKUP_SIZE); // restore memory
	primo_switch(0);	// turn Primo mode OFF
	//z80ex.int = 1;		// enable interrupts (?)
	//memcpy(&z80ex, &z80ex_backup, sizeof(Z80EX_CONTEXT));
	//z80ex.prefix = 0;	// turn prefix off, because last opc was the ED trap for XEP ROM!
	//Z80_PC--; // nah ...
	//xep_accept_trap = 0;
	set_cpu_clock(DEFAULT_CPU_CLOCK);
}


void primo_emulator_execute ( void )
{
	int a;
	/* Save state of various things to be able to revert into EP mode later, without any data lost */
	memcpy(memory_backup, memory + 0xFA * 0x4000, MEMORY_BACKUP_SIZE);	// save memory
	memcpy(ports_backup, ports, 0x100); 	// backup ports [note: on restore, we don't want to rewrite all values, maybe only Dave/Nick!
	memcpy(&z80ex_backup, &z80ex, sizeof(Z80EX_CONTEXT));

//	memcpy();	// save Dave registers
//	memcpy();	// save Nick registers

	/* set an LPT */
	memcpy(memory + (PRIMO_LPT_SEG << 14), primo_lpt, sizeof primo_lpt);
	z80ex_pwrite_cb(0x82, (PRIMO_LPT_SEG << 10) & 0xFF);	// LPT address, low byte
	z80ex_pwrite_cb(0x83, ((PRIMO_LPT_SEG << 2) & 0xF));
	z80ex_pwrite_cb(0x83, ((PRIMO_LPT_SEG << 2) & 0xF) | 64);
	z80ex_pwrite_cb(0x83, ((PRIMO_LPT_SEG << 2) & 0xF) | 64 | 128);
	z80ex_pwrite_cb(0x81, 0);	// border color
	/* do our stuffs ... */
	z80ex_pwrite_cb(0xA7, 8 | 16);	// D/A mode for Dave audio
	for (a = 0xA8; a < 0xB0; a++)
		z80ex_pwrite_cb(a, 0);	// volumes of L/H channels
	z80ex_pwrite_cb(0xB4, 0xAA);	// disable all interrupts on Dave (z80 won't get INT but the cloned NMI directly from Nick "translated" from VINT) and reset latches
	z80ex_pwrite_cb(0xB0, PRIMO_ROM_SEG);	// first segment is the Primo ROM now
	z80ex_pwrite_cb(0xB1, PRIMO_MEM1_SEG);	// normal RAM segment
	z80ex_pwrite_cb(0xB2, PRIMO_MEM2_SEG);	// normal RAM segment
	z80ex_pwrite_cb(0xB3, PRIMO_VID_SEG);	// a video segment as the Primo video RAM
	primo_switch(128 | 64);		// turn on Primo I/O mode
	Z80_PC = 0;			// Z80 reset address to the Primo ROM
	z80ex_reset();			// reset the CPU only!!
	set_ep_cpu(CPU_Z80);		// good old Z80 NMOS CPU is selected
	set_cpu_clock(2500000);
}

