/* Commodore LCD emulator (rewrite of my world's first working Commodore LCD emulator)
   Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   Part of the Xemu project: https://github.com/lgblgblgb/xemu

   This is an ongoing work to rewrite my old Commodore LCD emulator:

	* Commodore LCD emulator, C version.
	* (C)2013,2014 LGB Gabor Lenart
	* Visit my site (the older, JavaScript version of the emu is here too): http://commodore-lcd.lgb.hu/

   The goal is - of course - writing a primitive but still better than previous Commodore LCD emulator :)
   Note: I would be interested in VICE adoption, but I am lame with VICE, too complex for me :)

   This emulator based on my previous try (written in C), which is based on my previous JavaScript
   based emulator, which was the world's first Commodore LCD emulator. Actually this emulator turned
   out to be Xemu with many new machines since then to be emulated, including Commodore 65 and
   MEGA65 too.

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
#include "xemu/emutools_hid.h"
#include "xemu/emutools_config.h"
#include "xemu/cpu65.h"
#include "xemu/via65c22.h"
#include "xemu/emutools.h"

#include "commodore_lcd.h"

#include "acia.h"
#include "configdb.h"
#include "inject.h"
#include "ui.h"

#include <time.h>

static char emulator_addon_title[32] = "";

static int cpu_mhz, cpu_cycles_per_tv_frame;

#define MMU_RAM_MODE		0
#define MMU_APPL_MODE		1
#define MMU_KERN_MODE		2
#define MMU_TEST_MODE		3

#define MMU_KERN_WIN_OFS	mmu[MMU_KERN_MODE][1]
#define MMU_APPL_WIN_OFS(n)	mmu[MMU_APPL_MODE][n]
#define MMU_USE_CURRENT(n)	mmu[mmu_current][n]

#define MMU_RECALL		4
#define MMU_SAVE		5

static const char *mmu_mode_names[] = { "RAM", "APPL", "KERN", "TEST" };

Uint8 memory[0x40000];
static Uint8 charrom[0x2000];
static sha1_hash_str rom_sha;
extern unsigned const char roms[];
static unsigned int mmu[3][4] = {
	{0, 0, 0, 0},			// MMU_RAM_MODE (=0)
	{0, 0, 0, 0},			// MMU_APPL_MODE(=1)
	{0, 0, 0x30000, 0x30000}	// MMU_KERN_MODE(=2)
};
static unsigned int mmu_current = MMU_TEST_MODE, mmu_saved = MMU_TEST_MODE;
static Uint8 lcd_regs[4];
static struct Via65c22 via1, via2;
static Uint8 keysel;
static Uint8 rtc_regs[16];
static int rtc_sel = 0;
static unsigned int ram_size;
static Uint8 portB1 = 0, portA2 = 0;
static int keytrans = 0;

//#define MOD_KEYS_ARE_INVERTED
Uint8 powerstatus =
#ifndef MOD_KEYS_ARE_INVERTED
	0;
#else
	16|8|4|2|1;
#endif

#define IRQ_VIA1	1
#define IRQ_VIA2	2
#define	IRQ_ACIA	4

#define POWER_DOWN_WAIT_TIMEOUT	100
#define POWER_BUTTON_RELEASE_TIMEOUT 50

typedef enum { POWER_DOWN_CAUSE_NONE = 0, POWER_DOWN_CAUSE_EXIT, POWER_DOWN_CAUSE_RAMCHANGE, POWER_DOWN_CAUSE_RECYCLE, POWER_DOWN_CAUSE_SCRUB } power_down_cause_enum;
static power_down_cause_enum power_down_cause = POWER_DOWN_CAUSE_NONE;
static int power_down_wait_timeout = 0;
static int power_button_release_timeout = 0;
static int new_ram_size;

static const Uint8 init_lcd_palette_rgb[6] = {
	0xC0, 0xC0, 0xC0,
	0x00, 0x00, 0x00
};
static Uint32 lcd_palette[2];

#define VIRTUAL_SHIFT_POS 0x82
static const struct KeyMappingDefault lcd_key_map[] = {
	{ SDL_SCANCODE_BACKSPACE, 0x00 },
	{ SDL_SCANCODE_3, 0x01 },
	{ SDL_SCANCODE_5, 0x02 },
	{ SDL_SCANCODE_7, 0x03 },
	{ SDL_SCANCODE_9, 0x04 },
	{ SDL_SCANCODE_DOWN, 0x05 },
	{ SDL_SCANCODE_LEFT, 0x06 },
	{ SDL_SCANCODE_1, 0x07 },
	{ SDL_SCANCODE_RETURN, 0x10 },
	{ SDL_SCANCODE_W, 0x11 },
	{ SDL_SCANCODE_R, 0x12 },
	{ SDL_SCANCODE_Y, 0x13 },
	{ SDL_SCANCODE_I, 0x14 },
	{ SDL_SCANCODE_P, 0x15 },
	{ SDL_SCANCODE_RIGHTBRACKET, 0x16 },	// this would be '*'
	{ SDL_SCANCODE_HOME, 0x17 },
	{ SDL_SCANCODE_TAB, 0x20 },
	{ SDL_SCANCODE_A, 0x21 },
	{ SDL_SCANCODE_D, 0x22 },
	{ SDL_SCANCODE_G, 0x23 },
	{ SDL_SCANCODE_J, 0x24 },
	{ SDL_SCANCODE_L, 0x25 },
	{ SDL_SCANCODE_SEMICOLON, 0x26 },
	{ SDL_SCANCODE_F2, 0x27 },
	{ SDL_SCANCODE_F7, 0x30 },
	{ SDL_SCANCODE_4, 0x31 },
	{ SDL_SCANCODE_6, 0x32 },
	{ SDL_SCANCODE_8, 0x33 },
	{ SDL_SCANCODE_0, 0x34 },
	{ SDL_SCANCODE_UP, 0x35 },
	{ SDL_SCANCODE_RIGHT, 0x36 },
	{ SDL_SCANCODE_2, 0x37 },
	{ SDL_SCANCODE_F1, 0x40 },
	{ SDL_SCANCODE_Z, 0x41 },
	{ SDL_SCANCODE_C, 0x42 },
	{ SDL_SCANCODE_B, 0x43 },
	{ SDL_SCANCODE_M, 0x44 },
	{ SDL_SCANCODE_PERIOD, 0x45 },
	{ SDL_SCANCODE_ESCAPE, 0x46 },
	{ SDL_SCANCODE_SPACE, 0x47 },
	{ SDL_SCANCODE_F3, 0x50 },
	{ SDL_SCANCODE_S, 0x51 },
	{ SDL_SCANCODE_F, 0x52 },
	{ SDL_SCANCODE_H, 0x53 },
	{ SDL_SCANCODE_K, 0x54 },
	{ SDL_SCANCODE_APOSTROPHE, 0x55 },	// this would be ':'
	{ SDL_SCANCODE_EQUALS, 0x56 },
	{ SDL_SCANCODE_F8, 0x57 },
	{ SDL_SCANCODE_F5, 0x60 },
	{ SDL_SCANCODE_E, 0x61 },
	{ SDL_SCANCODE_T, 0x62 },
	{ SDL_SCANCODE_U, 0x63 },
	{ SDL_SCANCODE_O, 0x64 },
	{ SDL_SCANCODE_MINUS, 0x65 },
	{ SDL_SCANCODE_BACKSLASH, 0x66 }, // this would be '+'
	{ SDL_SCANCODE_Q, 0x67 },
	{ SDL_SCANCODE_LEFTBRACKET, 0x70 }, // this would be '@'
	{ SDL_SCANCODE_F4, 0x71 },
	{ SDL_SCANCODE_X, 0x72 },
	{ SDL_SCANCODE_V, 0x73 },
	{ SDL_SCANCODE_N, 0x74 },
	{ SDL_SCANCODE_COMMA, 0x75 },
	{ SDL_SCANCODE_SLASH, 0x76 },
	{ SDL_SCANCODE_F6, 0x77 },
	// extra keys, not part of main keyboard map, but separated byte (SR register can provide both in CLCD)
	{ SDL_SCANCODE_END, 0x80 },	// this would be the 'STOP' key
	{ SDL_SCANCODE_CAPSLOCK, 0x81},
	{ SDL_SCANCODE_LSHIFT, 0x82 }, { SDL_SCANCODE_RSHIFT, 0x82 },
	{ SDL_SCANCODE_LCTRL, 0x83 }, { SDL_SCANCODE_RCTRL, 0x83 },
	{ SDL_SCANCODE_LALT, 0x84 }, { SDL_SCANCODE_RALT, 0x84 },
	{ SDL_SCANCODE_F10, 0x87 },	// this is the power button. Usually we don't wanna be accessible by the user directly ...
	STD_XEMU_SPECIAL_KEYS,
	{ 0,	-1	}	// this must be the last line: end of mapping table
};





int cpu65_trap_callback ( const Uint8 opcode )
{
	return 0;	// not recognized
}


void clear_emu_events ( void )
{
	hid_reset_events(1);
}


static void update_addon_title ( void )
{
	sprintf(emulator_addon_title, "(%dMHz, %dK RAM)", cpu_mhz, ram_size >> 10);
}


static void clcd_reset ( void )
{
	clear_emu_events();
	DEBUGPRINT("RESET" NL);
	acia_reset();
	via_reset(&via1);
	via_reset(&via2);
	cpu65_reset();
	portB1 |= 4;	// Simulate "power is not yet enabled" situation by default
	// Enable waiting for enabling power by ROM, thus we can monitor, if that works on a real CLCD at all
	// ... FIXME: do that!
	// clear power-down waiting if it was in progress!
	power_down_cause = POWER_DOWN_CAUSE_NONE;
	power_down_wait_timeout = 0;
	update_addon_title();	// just in case, if memory size changed
}


// Warning: this function DOES NOT decode the fixed lo-4K and upper-1.5K ranges!!
// It also not deal with the I/O and such.
// Only call this, if those cases are handled/checked already!
static XEMU_INLINE unsigned int get_phys_addr ( const Uint16 cpu_addr )
{
	const unsigned int k16 = cpu_addr >> 14;
	if (XEMU_LIKELY(mmu_current != MMU_TEST_MODE))
		return (MMU_USE_CURRENT(k16) + cpu_addr) & 0x3FFFF;
	return ((cpu_addr < 0xE000) ? MMU_APPL_WIN_OFS(k16) : MMU_KERN_WIN_OFS) + (cpu_addr & 0x3FF);
}


Uint8 cpu65_read_callback ( Uint16 addr ) {
	if (addr <  0x1000) return memory[addr];
	if (addr <  0xF800) return memory[get_phys_addr(addr)];
	if (addr >= 0xFA00) return memory[addr + 0x30000U];
	if (addr >= 0xF980) return acia_read_reg(addr & 3);
	if (addr >= 0xF900) return 0xFF; // I/O exp area - not used by default
	if (addr >= 0xF880) return via_read(&via2, addr & 15);
	return via_read(&via1, addr & 15);
}


static void write_lcd_reg ( const Uint8 addr, const Uint8 data )
{
	static int regs[4] = { -1, -1, -1, -1 };
	int old_data = regs[addr & 3];
	regs[addr & 3] = data;
	lcd_regs[addr & 3] = data;
	if (old_data != data)
		DEBUGPRINT("LCD-CTRL: reg #%d $%02X->$%02X, regs now: $%02X $%02X $%02X $%02X" NL,
			addr & 3,
			old_data,
			data,
			regs[0], regs[1], regs[2], regs[3]
		);
}


static void mmu_set ( const unsigned int spec )
{
	if (spec == mmu_current)
		return;
	const unsigned int old = mmu_current;
	switch (spec) {
		case MMU_SAVE:
			mmu_saved = mmu_current;
			DEBUG("MMU: mode (%s) has been saved" NL, mmu_mode_names[mmu_current]);
			break;
		case MMU_RECALL:
			mmu_current = mmu_saved;
			DEBUG("MMU: mode (%s) has been recalled" NL, mmu_mode_names[mmu_saved]);
			break;
		case MMU_KERN_MODE:
		case MMU_APPL_MODE:
		case MMU_TEST_MODE:
		case MMU_RAM_MODE:
			mmu_current = spec;
			break;
		default:
			FATAL("mmu_set_mode(): invalid spec %u", spec);
			break;
	}
	if (XEMU_UNLIKELY(mmu_current > 3))
		FATAL("mmu_set_mode(): invalid mode %u", mmu_current);
	if (old != mmu_current)
		DEBUG("MMU: mode change %s -> %s" NL, mmu_mode_names[old], mmu_mode_names[mmu_current]);
	if (old == MMU_TEST_MODE && mmu_current != MMU_TEST_MODE)
		DEBUGPRINT("MMU: leaving TEST mode (to %s)" NL, mmu_mode_names[mmu_current]);
	if (old != MMU_TEST_MODE && mmu_current == MMU_TEST_MODE)
		DEBUGPRINT("MMU: entering TEST mode (from %s)" NL, mmu_mode_names[old]);
}


void cpu65_write_callback ( Uint16 addr, Uint8 data ) {
	if (addr < 0x1000) {
		if (addr == 0xAD && (data & 2)) {
			DEBUGPRINT("AD-DEBUG: writing $AD with data $%02X at PC=%04X" NL, data, cpu65.pc);
			//data &= ~2;
		}
		memory[addr] = data;
		return;
	}
	if (addr >= 0xF800) {
		switch ((addr - 0xF800) >> 7) {
			case  0: via_write(&via1, addr & 15, data); return;
			case  1: via_write(&via2, addr & 15, data); return;
			case  2: return; // I/O exp area is not handled
			case  3: acia_write_reg(addr & 3, data); return;
			case  4: mmu_set(MMU_KERN_MODE); return;
			case  5: mmu_set(MMU_APPL_MODE); return;
			case  6: mmu_set(MMU_RAM_MODE);  return;
			case  7: mmu_set(MMU_RECALL);    return;
			case  8: mmu_set(MMU_SAVE);      return;
			case  9: mmu_set(MMU_TEST_MODE); return;
			case 10: MMU_APPL_WIN_OFS(0) = data << 10; return;
			case 11: MMU_APPL_WIN_OFS(1) = data << 10; return;
			case 12: MMU_APPL_WIN_OFS(2) = data << 10; return;
			case 13: MMU_APPL_WIN_OFS(3) = data << 10; return;
			case 14: MMU_KERN_WIN_OFS    = data << 10; return;
			case 15: write_lcd_reg(addr, data); return;
		}
		FATAL("Unhandled case in cpu65_write_callback()");
	}
	const unsigned int phys_addr = get_phys_addr(addr);
	if (XEMU_LIKELY(phys_addr < ram_size)) {	// Do not allow write more RAM than we have, also avoids to overwrite ROM
		memory[phys_addr] = data;
	} else {
		DEBUG("MEM: out-of-RAM write addr=$%04X phys_addr=$%05X" NL, addr, phys_addr);
	}
}


static const char *get_ram_state_filename ( void )
{
	static char fn[64];
	sprintf(fn, "@ram-saved-%s-%u.mem", rom_sha, ram_size);
	return fn;
}


static int backup_ram_content ( void )
{
	DEBUGPRINT("RAM: backing up RAM content" NL);
	return xemu_save_file(get_ram_state_filename(), memory, ram_size, "Cannot save memory content :(") != ram_size;
}


static int restore_ram_content ( void )
{
	DEBUGPRINT("RAM: restoring RAM content" NL);
	return xemu_load_file(get_ram_state_filename(), memory, ram_size, ram_size, NULL) != ram_size;
}


static void press_power_button ( const int with_scrub )
{
	powerstatus &= 0x80|0x40|0x20;		// reset all bits but the higher three
	powerstatus |= 0x80;			// press the power button
	powerstatus &= ~0x80;			// OR NOT ....
        // lda MODKEY
        // and #MOD_CBM + MOD_SHIFT + MOD_CTRL + MOD_CAPS + MOD_STOP
        // eor #MOD_CBM + MOD_SHIFT + MOD_STOP
        // bne L85C0
        // jmp L87C5 -- we need this??? */
	if (with_scrub) {
#ifndef MOD_KEYS_ARE_INVERTED
		powerstatus |= 1|4|16;		// press the "magic key combo" as well: CBM + SHIFT + STOP
#else
		powerstatus &= ~(1|4|16);
#endif
	}
	power_button_release_timeout = POWER_BUTTON_RELEASE_TIMEOUT;
	DEBUGPRINT("POWER: virtually pressing POWER button %s scrub." NL, with_scrub ? "**WITH**" : "without");
}


static void sim_power_request ( power_down_cause_enum req )
{
	power_down_cause = req;
	press_power_button(req == POWER_DOWN_CAUSE_SCRUB);
	// will cause to force request if takes too long for the CLCD to react!
	power_down_wait_timeout = POWER_DOWN_WAIT_TIMEOUT;
}


void poweroff_request ( void )
{
	sim_power_request(POWER_DOWN_CAUSE_EXIT);
}


void ramsizechange_request ( const int new_ram_size_in )
{
	if (new_ram_size_in != ram_size) {
		new_ram_size = new_ram_size_in;	// "new_ram_size" will be used by on_power_down() with power_down_cause == POWER_DOWN_CAUSE_RAMCHANGE
		sim_power_request(POWER_DOWN_CAUSE_RAMCHANGE);
	}
}


void reset_request ( const int with_scrub )
{
	sim_power_request(with_scrub ? POWER_DOWN_CAUSE_SCRUB : POWER_DOWN_CAUSE_RECYCLE);
}


static void on_power_down ( const int forced )
{
	// At this point, we can say: Commodore LCD wants to shut itself down.
	// We need to decode the intent, ie is it the result of an action we did
	// (like virtually pressed the POWER button and such) and decide what to
	// do now.
	if (forced)
		DEBUGPRINT("POWER: forcing event (CLCD does not react?)" NL);
	switch (power_down_cause) {
		case POWER_DOWN_CAUSE_EXIT:
			if (!forced)
				backup_ram_content();
			DEBUGPRINT("POWER: exiting on power-down request." NL);
			XEMUEXIT(0);
			break;
		case POWER_DOWN_CAUSE_RAMCHANGE:
			if (!forced)
				backup_ram_content();	// backup RAM content from the previous amount of RAM
			memset(memory, 0xFF, ram_size > new_ram_size ? ram_size : new_ram_size);
			ram_size = new_ram_size;
			press_power_button(restore_ram_content());
			clcd_reset();
			DEBUGPRINT("POWER: RAM-size change re-powering request." NL);
			break;
		case POWER_DOWN_CAUSE_SCRUB:
			if (!forced)
				backup_ram_content();
			memset(memory, 0xFF, ram_size);
			press_power_button(1);
			clcd_reset();
			DEBUGPRINT("POWER: memory-scrub RESET re-powering request." NL);
			break;
		case POWER_DOWN_CAUSE_RECYCLE:
			if (!forced)
				backup_ram_content();
			press_power_button(restore_ram_content());
			clcd_reset();
			DEBUGPRINT("POWER: re-cycle RESET request." NL);
			break;
		case POWER_DOWN_CAUSE_NONE:
			// CLCD powering itself down without user's (via emulator) request?
			// Probably it's some built-in power-off feature of the ROM then (not using the power button).
			// FIXME: ask, what the hell to do now!
			//if (!forced)
			//	backup_ram_content();
			DEBUGPRINT("POWER: exiting on power-down request." NL);
			ERROR_WINDOW("Unknown power-down reason, ignoring!!");
			//XEMUEXIT(0);
			break;

	}
	power_down_cause = POWER_DOWN_CAUSE_NONE;
	power_down_wait_timeout = 0;
	powerstatus &= 0x7F;
}


static void via1_outa ( Uint8 mask, Uint8 data )
{
	keysel = data & mask;
}


static void via1_outb ( Uint8 mask, Uint8 data )
{
	// FIXME: for real, mask+data should be considered both to be correct!!!!!!!!
	//DEBUGPRINT("POWER: writing VIA1-PORTB with $%02X" NL, data);
	if (XEMU_UNLIKELY((data ^ portB1) & 4)) {
		DEBUGPRINT("POWER: supply power goes %s ($%02X->$%02X)" NL, data & 4 ? "OFF" : "ON", portB1, data);
		if (data & 4)
			on_power_down(0);	// See the comment at function power_down();
	}
	keytrans = ((!(portB1 & 1)) && (data & 1));
	portB1 = data;
}


static void via1_outsr(Uint8 data)
{
}


static Uint8 via1_ina(Uint8 mask)
{
	return 0xFF;
}


static Uint8 via1_inb ( Uint8 mask )
{
	return
		(portB1 & 4) |
		((via1.ORB & 32) << 2) |
		((via1.ORB & 16) << 2);
	return 0x00;	// LGB
}


static void via2_setint ( int level )
{
}


static void via2_outa ( Uint8 mask, Uint8 data )
{
	portA2 = data;
	// ugly stuff, but now the needed part is cut here from my other emulator :)
	if (portB1 & 2) {	// RTC RD
		if (data & 64) {
			rtc_sel = data & 15;
		}
	}
}


static void via2_outb ( Uint8 mask, Uint8 data )
{
}


static void via2_outsr ( Uint8 data )
{
}


static Uint8 via2_ina ( Uint8 mask )
{
	if (portB1 & 2) {
		if (portA2 & 16) {
			return rtc_regs[rtc_sel] | (portA2 & 0x70);
		}
		return portA2;
	}
	return 0;
}


static Uint8 via2_inb ( Uint8 mask )
{
	return 0xFF;
}


static Uint8 via2_insr ( void )
{
	return 0xFF;
}


static Uint8 via1_insr ( void )
{
	if (keytrans) {
		int data = 0;
		keytrans = 0;
		if (!(keysel &   1)) data |= ~kbd_matrix[0];
		if (!(keysel &   2)) data |= ~kbd_matrix[1];
		if (!(keysel &   4)) data |= ~kbd_matrix[2];
		if (!(keysel &   8)) data |= ~kbd_matrix[3];
		if (!(keysel &  16)) data |= ~kbd_matrix[4];
		if (!(keysel &  32)) data |= ~kbd_matrix[5];
		if (!(keysel &  64)) data |= ~kbd_matrix[6];
		if (!(keysel & 128)) data |= ~kbd_matrix[7];
		return data;
	} else {
		// FIXME: for some reason capslock is stuck in emulation!!! so we want to unmask it!
		static Uint8 old_data = 0xEE;
		//const Uint8 data = ((~kbd_matrix[8]) | powerstatus) & (~2);
		const Uint8 data = ((~kbd_matrix[8]) | powerstatus);
		if (data != old_data) {
			DEBUGPRINT("Reading keytrans/1 result=$%02X" NL, data);
			old_data = data;
		}
		return data;
	}
}


static void via1_setint ( int level )
{
	//DEBUG("IRQ level: %d" NL, level);
	if (level)
		cpu65.irqLevel |= IRQ_VIA1;
	else
		cpu65.irqLevel &= ~IRQ_VIA1;
}


static void acia_setint( const int level )
{
	if (level)
		cpu65.irqLevel |= IRQ_ACIA;
	else
		cpu65.irqLevel &= ~IRQ_ACIA;
}


#define BG lcd_palette[0]
#define FG lcd_palette[1]


static void render_screen ( void )
{
	int ps = lcd_regs[1] << 7;
	//int x, y, ch;
	int tail;
	Uint32 *pix = xemu_start_pixel_buffer_access(&tail);
	if (lcd_regs[2] & 2) { // graphic mode
		for (int y = 0; y < 128; y++) {
			for (int x = 0; x < 60; x++) {
				const Uint8 ch = memory[ps++];
				*(pix++) = (ch & 0x80) ? FG : BG;
				*(pix++) = (ch & 0x40) ? FG : BG;
				*(pix++) = (ch & 0x20) ? FG : BG;
				*(pix++) = (ch & 0x10) ? FG : BG;
				*(pix++) = (ch & 0x08) ? FG : BG;
				*(pix++) = (ch & 0x04) ? FG : BG;
				*(pix++) = (ch & 0x02) ? FG : BG;
				*(pix++) = (ch & 0x01) ? FG : BG;
			}
			ps = (ps + 4) & 0x7FFF;
			pix += tail;
		}
	} else { // text mode
		int cof  = (lcd_regs[2] & 1) << 10;
		int maxx = (lcd_regs[3] & 4) ? 60 : 80;
		ps += lcd_regs[0] & 0x7F; // X-Scroll register, only the lower 7 bits are used
		for (int y = 0; y < 128; y++) {
			for (int x = 0; x < maxx; x++) {
				ps &= 0x7FFF;
				Uint8 ch = memory[ps++];
				ch = charrom[cof + ((ch & 0x7F) << 3) + (y & 7)] ^ ((ch & 0x80) ? 0xFF : 0x00);
				pix[0] = (ch & 0x80) ? FG : BG;
				pix[1] = (ch & 0x40) ? FG : BG;
				pix[2] = (ch & 0x20) ? FG : BG;
				pix[3] = (ch & 0x10) ? FG : BG;
				pix[4] = (ch & 0x08) ? FG : BG;
				pix[5] = (ch & 0x04) ? FG : BG;
				if (lcd_regs[3] & 4) {
					pix[6] = (ch & 0x02) ? FG : BG;
					pix[7] = (ch & 0x01) ? FG : BG;
					pix += 8;
				} else
					pix += 6;
			}
			if ((y & 7) == 7)
				ps += 128 - maxx;
			else
				ps -= maxx;
			pix += tail;
		}
	}
	if (XEMU_UNLIKELY(register_screenshot_request)) {
		register_screenshot_request = 0;
		if (!xemu_screenshot_png(
			NULL, NULL,
			SCREEN_DEFAULT_ZOOM,
			SCREEN_DEFAULT_ZOOM,
			NULL,	// Allow function to figure it out ;)
			SCREEN_WIDTH,
			SCREEN_HEIGHT,
			SCREEN_WIDTH
		))
			OSD(-1, -1, "Screenshot has been taken");
	}
	xemu_update_screen();
}


// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	if (!pressed && pos == -2 && key == 0 && handled == SDL_BUTTON_RIGHT)
		ui_enter_menu();
	else
		powerstatus &= 128|64|32;	// resets other bit than the upper 3 on any key event
        return 0;
}


static void update_rtc ( void )
{
	struct tm *t = xemu_get_localtime();
	rtc_regs[ 0] = t->tm_sec % 10;
	rtc_regs[ 1] = t->tm_sec / 10;
	rtc_regs[ 2] = t->tm_min % 10;
	rtc_regs[ 3] = t->tm_min / 10;
	rtc_regs[ 4] = t->tm_hour % 10;
	rtc_regs[ 5] = (t->tm_hour / 10) | 8; // TODO: AM/PM, 24h/12h time format
	rtc_regs[ 6] = t->tm_wday; // day of week
	rtc_regs[ 9] = t->tm_mday % 10;
	rtc_regs[10] = t->tm_mday / 10;
	rtc_regs[ 7] = (t->tm_mon + 1) % 10;
	rtc_regs[ 8] = (t->tm_mon + 1) / 10;
	rtc_regs[11] = (t->tm_year - 84) % 10; // beware of 2084, Commodore LCDs will have "Y2K-like" problem ... :)
	rtc_regs[12] = (t->tm_year - 84) / 10;
}


static void shutdown_emu ( void )
{
	//if (configdb.keep_ram)
	//	backup_ram_content();
	if (configdb.dumpmem)
		xemu_save_file(configdb.dumpmem, memory, ram_size, "Cannot dump RAM content into file");
	DEBUGPRINT("Shutting down ..." NL);
}


static int memory_init ( void )
{
	static const char *rom_fatal_msg = "This is one of the selected ROMs. Without it, Xemu won't work.\nInstall it, or use -romXXX CLI switches to specify another path, see the -h output for help.";
	memset(memory, 0xFF, sizeof memory);
	memset(charrom, 0xFF, sizeof charrom);
	// --- Load the character ROM
	const int charrom_load_size = xemu_load_file(configdb.romchr_fn, charrom, 0x1000, 0x2000, "Cannot load character ROM (must exist with size exactly 4K or 8K");
	if (charrom_load_size < 0) {
		return 1;
	} else if (charrom_load_size == 0x1000) {
		memcpy(charrom + 0x1000, charrom, 0x1000);
		DEBUGPRINT("ROM: character ROM is 4096 bytes, duplicated to be 8192 bytes long." NL);
	} else if (charrom_load_size != 0x2000) {
		ERROR_WINDOW("Bad character ROM, must be 4096 or 8192 bytes in length (got %d bytes): %s", charrom_load_size, configdb.romchr_fn);
		return 1;
	} else {
		DEBUGPRINT("ROM: character ROM is 8192 bytes, low/high 4K %s" NL, !memcmp(charrom, charrom + 0x1000, 0x1000) ? "matches" : "mismatches");
	}
	// --- Load the KERNAL
	if (xemu_load_file(configdb.rom102_fn, memory + 0x38000, 0x8000, 0x8000, rom_fatal_msg) < 0)
		return 1;
	sha1_checksum_as_string(rom_sha, memory + 0x38000, 0x8000);
	const int rom_good_sha = !strcmp(rom_sha, "e49c20b237a78b54c2cb26b133d5903bb60bd8ef");
	DEBUGPRINT("ROM: KERNAL SHA1 checksum: %s [%s]" NL, rom_sha, rom_good_sha ? "OK" : "UNKNOWN_ROM");
	// --- Load other ROMs
	static char **rom_fnps[] = { &configdb.rom103_fn, &configdb.rom104_fn, &configdb.rom105_fn };
	static const int rom_addrs[] = { 0x30000, 0x28000, 0x20000, 0 };
	for (int i = 0; rom_addrs[i]; i++) {
		const char *fn = *rom_fnps[i];
		const char *on = xemucfg_get_optname(rom_fnps[i]);
		if (!fn || !fn[0] || (fn[0] == '-' && !fn[1])) {
			DEBUGPRINT("ROM: skipping loading ROM to $%X (optname %s): not requested." NL, rom_addrs[i], on);
			continue;
		}
		if (xemu_load_file(fn, memory + rom_addrs[i], 0x8000, 0x8000, "Cannot load ROM") < 0)
			return 1;
		DEBUGPRINT("ROM: loaded to $%X (option %s): %s" NL, rom_addrs[i], on, xemu_load_filepath);
	}
	// --- Restore RAM content, if possible & press power button
	press_power_button(restore_ram_content() | configdb.scrub);
	powerstatus &= 0x7F;	// FIXME: remove this
	//restore_ram_content();
	// --- Checking/patching KERNAL ROM
#ifdef	ROM_HACK_COLD_START
#undef		ROM_HACK_COLD_START
#warning	"ROM_HACK_COLD_START is obsolete for now, FIXME?"
#endif
#ifdef	ROM_HACK_NEW_ROM_SEARCHING
#undef		ROM_HACK_NEW_ROM_SEARCHING
#warning	"ROM_HACK_NEW_ROM_SEARCHING is obsolete for now, FIXME?"
#endif
	// Ugly hacks :-( <patching ROM>
#ifdef	ROM_HACK_COLD_START
#error	"DO not enable this for now!"
	if (!configdb.keep_ram) {
		if (rom_good_sha) {
			// this ROM patching is needed, as Commodore LCD seems not to work to well with "not intact" SRAM content (ie: it has battery powered SRAM even when "switched off")
			DEBUGPRINT("ROM-HACK: forcing cold start condition with ROM patching!" NL);
			memory[0x385BB] = 0xEA;
			memory[0x385BC] = 0xEA;
		} else {
			DEBUGPRINT("ROM-HACK: Warning! Cannot apply 'force cold start condition' ROM patch because of unknown ROM" NL);
		}
	} else {
		DEBUGPRINT("ROM-HACK: SKIP cold start condition forcing!" NL);
		restore_ram_content();
	}
#endif
#ifdef	ROM_HACK_NEW_ROM_SEARCHING
#error	"DO not enable this for now!"
	if (rom_good_sha) {
		// this ROM hack modifies the ROM signature searching bytes so we can squeeze extra menu points of the main screen!
		// this hack SHOULD NOT be used, if the ROM 32K ROM images from 0x20000 and 0x28000 are not empty after offset 0x6800
		// WARNING: Commodore LCDs are known to have different ROM versions, be careful with different ROMs, if you find any!
		// [note: if you find other ROM versions, please tell me!!!! - that's the other message ...]
		DEBUG("ROM-HACK: modifying ROM searching MMU table" NL);
		// overwrite MMU table positions for ROM scanner in KERNAL
		memory[0x382CC] = 0x8A;	// offset 0x6800 in the ROM image of clcd-u105.rom [phys memory address: 0x26800]
		memory[0x382CE] = 0xAA;	// offset 0x6800 in the ROM image of clcd-u104.rom [phys memory address: 0x2E800]
		// try to load "parasite" ROMs (it's not fatal if we cannot ...)
		// these loads to an unused part of the original ROM images
		xemu_load_file("#clcd-u105-parasite.rom", memory + 0x26800, 32, 0x8000 - 0x6800, NULL);
		xemu_load_file("#clcd-u104-parasite.rom", memory + 0x2E800, 32, 0x8000 - 0x6800, NULL);
	} else {
		DEBUGPRINT("ROM-HACK: Warning! Cannot apply 'MMU-search-table' ROM patch because of unknown ROM" NL);
	}
#endif
	return 0;
}


static void rom_list ( void )
{
	//const char *defprg = xemucfg_get_str("defprg");
	for (int addr = 0x20000; addr < 0x40000; addr += 0x4000) {
		if (!memcmp(memory + addr + 8, "Commodore LCD", 13)) {
			DEBUGPRINT("ROM directory entry point @ $%05X" NL, addr);
			DEBUGPRINT("  ROM header bytes: $%02X $%02X $%02X $%02X $%02X $%02X $%02X $%02X" NL,
				memory[addr + 0], memory[addr + 1], memory[addr + 2], memory[addr + 3],
				memory[addr + 4], memory[addr + 5], memory[addr + 6], memory[addr + 7]
			);
			DEBUGPRINT("  ROM checksum Kbytes: %dK (@4)" NL, memory[addr + 4]);
			if (memory[addr + 5] != 0xDD && memory[addr + 6] != 0xDD && memory[addr + 7] != 0xDD)
				DEBUGPRINT("  ROM date: %02X-%02X-%02X" NL,
					memory[addr + 5], memory[addr + 6], memory[addr + 7]
				);
			int pos = addr + 13 + 8;
			while (memory[pos]) {
				char name[256];
				memcpy(name, memory + pos + 6, memory[pos] - 6);
				name[memory[pos] - 6] = 0;
				DEBUGPRINT("\t($%02X $%02X $%02X) START=$%04X : \"%s\"" NL,
					memory[pos + 1], memory[pos + 2], memory[pos + 3],
					memory[pos + 4] | (memory[pos + 5] <<8),
					name
				);
				//if (defprg && !strcasecmp(name, defprg)) {
				//	DEBUGPRINT("\tFOUND!!!!!" NL);
				//	memory[pos + 1] |= 0x20;
				//	memory[pos + 1] = 0x20;
				//} else if (defprg) {
				//	//memory[pos + 1] &= ~0x20;
				//}
				pos += memory[pos];
			}
		}
	}
}


void set_cpu_mhz ( const int mhz )
{
	if (mhz == cpu_mhz)
		return;
	cpu_mhz = mhz;
	cpu_cycles_per_tv_frame = mhz * 1000000 / 25;
	DEBUGPRINT("CPU: setting CPU to %dMHz, %d CPU cycles per full 1/25sec frame." NL, mhz, cpu_cycles_per_tv_frame);
	update_addon_title();
}


int get_cpu_mhz ( void )
{
	return cpu_mhz;
}


int get_ram_size ( void )
{
	return ram_size;
}


static void update_emulator ( void )
{
	if (XEMU_UNLIKELY(power_button_release_timeout)) {
		power_button_release_timeout--;
		if (power_button_release_timeout <= 0) {
			power_button_release_timeout = 0;
			//powerstatus &= 0x40;
			DEBUGPRINT("POWER: virtually releasing POWER button (and maybe others)" NL);
			KBD_CLEAR_MATRIX();
#ifndef MOD_KEYS_ARE_INVERTED
			powerstatus &= ~(1|4|16);
#else
			powerstatus |= 1|4|16;
#endif
		}
	}
	if (XEMU_UNLIKELY(power_down_wait_timeout)) {
		power_down_wait_timeout--;
		if (power_down_wait_timeout <= 0) {
			power_down_wait_timeout = 0;
			on_power_down(1);
		}
	}
	if (XEMU_UNLIKELY(prg_inject.phase == 1))
		prg_inject_periodic_check();
	ui_iteration();
	render_screen();
	hid_handle_all_sdl_events();
	xemu_timekeeping_delay(40000);	// 40000 microseconds would be the real time for a full TV frame (see main() for more info: CLCD is not TV based for real ...)
	if (seconds_timer_trigger)
		update_rtc();
}


static int cycles, viacyc;


static void emulation_loop ( void )
{
	for (;;) {
		if (cpu65.pc >= 0x85B5 && cpu65.pc <= 0x85C0) {
			DEBUGPRINT("CPU-DEBUG **before** PC=$%04X A=%02X" NL, cpu65.pc, cpu65.a);
		}
		const int opcyc = cpu65_step();	// execute one opcode (or accept IRQ, etc), return value is the used clock cycles
		if (cpu65.pc >= 0x85B5 && cpu65.pc <= 0x85C0) {
			DEBUGPRINT("CPU-DEBUG **after**  PC=$%04X A=%02X" NL, cpu65.pc, cpu65.a);
		}
		viacyc += opcyc;
		cycles += opcyc;
		if (viacyc >= cpu_mhz) {
			const int steps = viacyc / cpu_mhz;
			viacyc = viacyc % cpu_mhz;
			via_tick(&via1, steps);	// run VIA-1 tasks for the same amount of cycles as the CPU would do @ 1MHz
			via_tick(&via2, steps);	// -- "" -- the same for VIA-2
		}
		/* Note, Commodore LCD is not TV standard based ... Since I have no idea about the update etc, I still assume some kind of TV-related stuff, who cares :) */
		if (XEMU_UNLIKELY(cycles >= cpu_cycles_per_tv_frame)) {	// if enough cycles elapsed (what would be the amount of CPU cycles for a TV frame), let's call the update function.
			update_emulator();			// this is the heart of screen update, also to handle SDL events (like key presses ...)
			cycles -= cpu_cycles_per_tv_frame;	// not just cycle = 0, to avoid rounding errors, but it would not matter too much anyway ...
			return;
		}
	}
}


static int ok_to_exit ( void )
{
	DEBUGPRINT("POWER: exit request received, pressing the POWER button!" NL);
	//exit_requested = 1;
	//powerstatus = 0x80;
	return 0;	// do NOT exit (yet!)
}




int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "The world's first Commodore LCD emulator from LGB", argc, argv);
	configdb_define_emulator_options();
	if (xemucfg_parse_all())
		return 1;
	ram_size = configdb.ram_size_kbytes << 10;
	DEBUGPRINT("MEM: RAM size is set to %dKbytes." NL, configdb.ram_size_kbytes);
	set_cpu_mhz(configdb.clock_mhz);	// will update the "addon" display as well: update_addon_title() will be called
	window_title_info_addon = emulator_addon_title;
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size (same as texture for now ...)
		SCREEN_WIDTH * SCREEN_DEFAULT_ZOOM * configdb.zoom, SCREEN_HEIGHT * SCREEN_DEFAULT_ZOOM * configdb.zoom,	// window size
		SCREEN_FORMAT,		// pixel format
		2,			// we have 2 colours :)
		init_lcd_palette_rgb,	// initialize palette from this constant array
		lcd_palette,		// initialize palette into this stuff
#ifdef SDL_HINT_RENDER_SCALE_QUALITY
		configdb.sdlrenderquality,// render scaling quality
#else
		RENDER_SCALE_QUALITY,	// render scaling quality
#endif
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		shutdown_emu		// shutdown function
	))
		return 1;
	osd_init_with_defaults();
	ui_init(configdb.gui_selection);	// allow to fail (do not exit if it fails). Some targets may not have X running, etc.
	hid_init(
		lcd_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_DISABLE	// no joystick HID events enabled
	);
	hid_ok_to_exit_cb = ok_to_exit;
	/* init memory & ROM content */
	if (memory_init())
		return 1;
	rom_list();
	/* PRG injection CLI request */
	prg_inject.phase = 0;
	if (!prg_load_prepare_inject(configdb.prg_inject_fn, BASIC_START))
		prg_inject.phase = 1;
	/* init CPU */
	cpu65_reset();	// we must do this after loading KERNAL at least, since PC is fetched from reset vector here!
	/* init VIAs */
	via_init(&via1, "VIA#1", via1_outa, via1_outb, via1_outsr, via1_ina, via1_inb, via1_insr, via1_setint);
	via_init(&via2, "VIA#2", via2_outa, via2_outb, via2_outsr, via2_ina, via2_inb, via2_insr, via2_setint);
	portB1 = 4;
	/* init ACIA */
	acia_init(acia_setint);
	/* keyboard */
	clear_emu_events();	// also resets the keyboard
	keysel = 0;
	/* --- START EMULATION --- */
	cycles = 0;
	xemu_set_full_screen(configdb.fullscreen_requested);
	if (!configdb.syscon)
		sysconsole_close(NULL);
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	update_rtc();			// this will use time-keeping stuff as well, so initially let's do after the function call above
	viacyc = 0;
	// FIXME: add here the "OK to save ROM state" ...
	XEMU_MAIN_LOOP(emulation_loop, 25, 1);
	return 0;
}
