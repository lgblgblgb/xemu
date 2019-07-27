/* Test-case for a very simple and inaccurate Commodore VIC-20 emulator using SDL2 library
   within the Xemu project.
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This is the VIC-20 emulation. Note: the source is overcrowded with comments by intent :)
   That it can useful for other people as well, or someone wants to contribute, etc ...

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
#include "commodore_vic20.h"
#include "xemu/cpu65.h"
#include "xemu/via65c22.h"
#include "vic6561.h"

#define SCREEN_HEIGHT		(SCREEN_LAST_VISIBLE_SCANLINE - SCREEN_FIRST_VISIBLE_SCANLINE + 1)
#define SCREEN_WIDTH		(SCREEN_LAST_VISIBLE_DOTPOS   - SCREEN_FIRST_VISIBLE_DOTPOS   + 1)


static const char *rom_fatal_msg = "This is one of the selected ROMs. Without it, Xemu won't work.\nInstall it, or use -romXXX CLI switches to specify another path, see the -h output for help.";


static Uint8 memory[0x10001];	// 64K (+1 for load check) address space of the 6502 CPU (some of it is ROM, undecoded, whatsoever ... it's simply the whole address space, *NOT* only RAM!)
static const Uint8 init_vic_palette_rgb[16 * 3] = {	// VIC palette given by RGB components
	0x00, 0x00, 0x00,	// black
	0xFF, 0xFF, 0xFF,	// white
	0xF0, 0x00, 0x00,	// red
	0x00, 0xF0, 0xF0,	// cyan
	0x60, 0x00, 0x60,	// purple
	0x00, 0xA0, 0x00,	// green
	0x00, 0x00, 0xF0,	// blue
	0xD0, 0xD0, 0x00,	// yellow
	0xC0, 0xA0, 0x00,	// orange
	0xFF, 0xA0, 0x00,	// light orange
	0xF0, 0x80, 0x80,	// pink
	0x00, 0xFF, 0xFF,	// light cyan
	0xFF, 0x00, 0xFF,	// light purple
	0x00, 0xFF, 0x00,	// light green
	0x00, 0xA0, 0xFF,	// light blue
	0xFF, 0xFF, 0x00	// light yellow
};
static Uint8 dummy_vic_access[1024];			// define 1K of "nothing" for VIC-I memory regions it cannot find memory there

static int emurom_policy;
static char *emufile_p;
static int emufile_size;
static int frameskip = 0;
static int nmi_level = 0;			// level of NMI (note: 6502 is _edge_ triggered on NMI, this is only used to check edges ...)
static struct Via65c22 via1, via2;		// VIA-1 and VIA-2 emulation structures

static Uint8 is_kpage_writable[64] = {		// writable flag (for different memory expansions) for every kilobytes of the address space, this shows the default, unexpanded config!
	1,		// @ 0K     (sum 1K), RAM, built-in (VIC-I can reach it)
	0,0,0,		// @ 1K -3K (sum 3K), place for 3K expansion [will be filled with RAM on memcfg request]
	1,1,1,1,	// @ 4K- 7K (sum 4K), RAM, built-in (VIC-I can reach it)
	0,0,0,0,0,0,0,0,// @ 8K-15K (sum 8K), expansion block [will be filled with RAM on memcfg request]
	0,0,0,0,0,0,0,0,// @16K-23K (sum 8K), expansion block [will be filled with RAM on memcfg request]
	0,0,0,0,0,0,0,0,// @24K-31K (sum 8K), expansion block [will be filled with RAM on memcfg request]
	0,0,0,0,	// @32K-35K (sum 4K), character ROM (VIC-I can reach it)
	0,		// @36K     (sum 1K), I/O block 0 (VIAs, VIC-I, ...)
	1,		// @37K     (sum 1K), colour RAM (VIC-I can reach it directly), only 0.5K, but the position depends on the config ... [handled as a special case on READ - 4 bit wide only!]
	0,		// @38K     (sum 1K), I/O block 2 (not used now, gives 0xFF on read)
	0,		// @39K     (sum 1K), I/O block 3 (not used now, gives 0xFF on read)
	0,0,0,0,0,0,0,0,// @40K-47K (sum 8K), expansion block (not available for BASIC even if it's RAM) [will be filled with RAM on memcfg request]
	0,0,0,0,0,0,0,0,// @48K-55K (sum 8K), basic ROM
	0,0,0,0,0,0,0,0 // @56K-63K (sum 8K), kernal ROM
};
static Uint8 *vic_address_space_hi4[16] = {	// configure high 4 bits of VIC-I databus for 1K sized SRAM at $9400 on VIC-20
	memory + 0x9400, memory + 0x9400, memory + 0x9400, memory + 0x9400,
	memory + 0x9400, memory + 0x9400, memory + 0x9400, memory + 0x9400,
	memory + 0x9400, memory + 0x9400, memory + 0x9400, memory + 0x9400,
	memory + 0x9400, memory + 0x9400, memory + 0x9400, memory + 0x9400
};
static Uint8 *vic_address_space_lo8[16] = {	// configure low 8 bits of VIC-I databus access of the VIC-20 memory
	memory + 0x8000, memory + 0x8400, memory + 0x8800, memory + 0x8C00,	// corresponds to the character ROM, access is OK
	dummy_vic_access, dummy_vic_access, dummy_vic_access, dummy_vic_access,	// I/O and colour RAM cannot be accessed (colour RAM is only on high4 ...)
	memory,           dummy_vic_access, dummy_vic_access, dummy_vic_access, // first 1K is OK, the others are not (not even with the +3K expansion)
	memory + 0x1000, memory + 0x1400, memory + 0x1800, memory + 0x1C00	// 4K of internal RAM, OK
};


#define VIRTUAL_SHIFT_POS	0x31


static const struct KeyMappingDefault vic20_key_map[] = {
	{ SDL_SCANCODE_1,		0x00 }, // 1
	{ SDL_SCANCODE_3,		0x01 }, // 3
	{ SDL_SCANCODE_5,		0x02 }, // 5
	{ SDL_SCANCODE_7,		0x03 }, // 7
	{ SDL_SCANCODE_9,		0x04 }, // 9
	//{ SDL_SCANCODE_+		0x05 },	// PLUS
	//{ SDL_SCANCODE_font		0x06 },	// FONT
	{ SDL_SCANCODE_BACKSPACE,	0x07 },	// DEL
	//{ SDL_SCANCODE_// UNKNOWN KEY?			0x10	//
	{ SDL_SCANCODE_W,		0x11 }, // W
	{ SDL_SCANCODE_R,		0x12 }, // R
	{ SDL_SCANCODE_Y,		0x13 }, // Y
	{ SDL_SCANCODE_I,		0x14 }, // I
	{ SDL_SCANCODE_P,		0x15 }, // P
	//{ SDL_SCANCODE_STAR // *
	{ SDL_SCANCODE_RETURN,		0x17 }, // RETURN
	{ SDL_SCANCODE_LCTRL,		0x20 }, // CTRL, only the left ctrl is mapped as vic-20 ctrl ...
	{ SDL_SCANCODE_A,		0x21 }, // A
	{ SDL_SCANCODE_D,		0x22 }, // D
	{ SDL_SCANCODE_G,		0x23 }, // G
	{ SDL_SCANCODE_J,		0x24 }, // J
	{ SDL_SCANCODE_L,		0x25 }, // L
	{ SDL_SCANCODE_SEMICOLON,	0x26 }, // ;
	{ SDL_SCANCODE_RIGHT, 0x27 }, { SDL_SCANCODE_LEFT, 0x27 | 8 },	// CURSOR RIGHT, _SHIFTED_: CURSOR LEFT!
	{ SDL_SCANCODE_END,		0x30 }, // RUN/STOP !! we use PC key 'END' for this!
	{ SDL_SCANCODE_LSHIFT,		0x31 }, // LEFT SHIFT
	{ SDL_SCANCODE_X,		0x32 }, // X
	{ SDL_SCANCODE_V,		0x33 }, // V
	{ SDL_SCANCODE_N,		0x34 }, // N
	{ SDL_SCANCODE_COMMA,		0x35 }, // ,
	{ SDL_SCANCODE_SLASH,		0x36 }, // /
	{ SDL_SCANCODE_DOWN, 0x37 }, { SDL_SCANCODE_UP, 0x37 | 8 }, // CURSOR DOWN, _SHIFTED_: CURSOR UP!
	{ SDL_SCANCODE_SPACE,		0x40 }, // SPACE
	{ SDL_SCANCODE_Z,		0x41 }, // Z
	{ SDL_SCANCODE_C,		0x42 }, // C
	{ SDL_SCANCODE_B,		0x43 }, // B
	{ SDL_SCANCODE_M,		0x44 }, // M
	{ SDL_SCANCODE_PERIOD,		0x45 }, // .
	{ SDL_SCANCODE_RSHIFT,		0x46 }, // RIGHT SHIFT
	{ SDL_SCANCODE_F1, 0x47 }, { SDL_SCANCODE_F2, 0x47 | 8 }, // F1, _SHIFTED_: F2!
	{ SDL_SCANCODE_LALT, 0x50 }, { SDL_SCANCODE_RALT, 0x50 }, // COMMODORE (may fav key!), PC sucks, no C= key :) - we map left and right ALT here ...
	{ SDL_SCANCODE_S,		0x51 }, // S
	{ SDL_SCANCODE_F,		0x52 }, // F
	{ SDL_SCANCODE_H,		0x53 }, // H
	{ SDL_SCANCODE_K,		0x54 }, // K
	{ SDL_SCANCODE_APOSTROPHE,	0x55 },	// :    we map apostrophe here
	{ SDL_SCANCODE_EQUALS,		0x56 }, // =
	{ SDL_SCANCODE_F3, 0x57 }, { SDL_SCANCODE_F4, 0x57 | 8 }, // F3, _SHIFTED_: F4!
	{ SDL_SCANCODE_Q,		0x60 }, // Q
	{ SDL_SCANCODE_E,		0x61 }, // E
	{ SDL_SCANCODE_T,		0x62 }, // T
	{ SDL_SCANCODE_U,		0x63 }, // U
	{ SDL_SCANCODE_O,		0x64 }, // O
	//{ SDL_SCANCODE_// @
	// UNKNOWN KEY?!?! 0x66
	{ SDL_SCANCODE_F5, 0x67 }, { SDL_SCANCODE_F6, 0x67 | 8 }, // F5, _SHIFTED_: F6!
	{ SDL_SCANCODE_2,		0x70 }, // 2
	{ SDL_SCANCODE_4,		0x71 }, // 4
	{ SDL_SCANCODE_6,		0x72 }, // 6
	{ SDL_SCANCODE_8,		0x73 }, // 8
	{ SDL_SCANCODE_0,		0x74 }, // 0
	{ SDL_SCANCODE_MINUS,		0x75 }, // -
	{ SDL_SCANCODE_HOME,		0x76 }, // HOME
	{ SDL_SCANCODE_F7, 0x77 }, { SDL_SCANCODE_F8, 0x77 | 8 }, // F7, _SHIFTED_: F8!
	// -- the following definitions are not VIC-20 keys, but emulator related stuffs
	STD_XEMU_SPECIAL_KEYS,
	//{ SDL_SCANCODE_ESCAPE,		0x81 },	// RESTORE key
	// **** this must be the last line: end of mapping table ****
	{ 0, -1 }
};




static inline void __mark_ram ( int start_k, int size_k )
{
	printf("MEM: adding RAM $%04X-%04X" NL, start_k << 10, ((start_k + size_k) << 10) - 1);
	while (size_k--)
		is_kpage_writable[start_k++] = 1;
}


static char *vic20_get_memconfig_string ( void )
{
	static char result[40];
	sprintf(result, "%c1 %c8 %c16 %c24 %c40",
		is_kpage_writable[1] ? '+' : '-',
		is_kpage_writable[8] ? '+' : '-',
		is_kpage_writable[16] ? '+' : '-',
		is_kpage_writable[24] ? '+' : '-',
		is_kpage_writable[40] ? '+' : '-'
	);
	return result;
}


static void emuprint ( const char *str )
{
	snprintf(
		(char*)memory + (memory[0xA017] | (memory[0xA018] << 8)),
		memory[0xA019] | (memory[0xA01A] << 8),
		"%s",
		str
	);
}


#define EMUPRINTF(...) do { \
	char __buffer_for_conv__[8192];	\
	snprintf(__buffer_for_conv__, sizeof __buffer_for_conv__, __VA_ARGS__);	\
	emuprint(__buffer_for_conv__);	\
} while(0)



static void execute_monitor_command ( void )
{
	char *p;
	memory[600] = 0;	// close the input string, just in case (but it should have been anyway)
	p = (char*)memory + 512;
	while (*p <= 32)
		p++;
	if (p[0] == 'X') {
		cpu65.a = 0;	// do not continue ...
		emuprint("\r");
		return;
	}
	// unknown command
	emuprint("?\r");
}



static Uint8 inject_prg ( void )
{
	int addr;
	if (!emufile_p)
		return emurom_policy;	// No loaded program, use the pre-defined policy instead
	addr = emufile_p[0] | (emufile_p[1] << 8);
	printf("LOAD: injecting program into the memory from $%04X" NL, addr);
	memcpy(memory + addr, emufile_p + 2, emufile_size - 2);
	memory[addr - 1] = 0;
	memory[0x2b] = addr & 0xFF;
	memory[0x2c] = addr >> 8;
	addr += emufile_size - 2;
	memory[0x2d] = addr & 0xFF;
	memory[0x2e] = addr >> 8;
	return 128;
}



static int is_our_rom ( void )
{
	if (memcmp(memory + 0xA00C, "LGBXVIC20", 9))
		return -1;
	return memory[0xA015] | (memory[0xA016] << 8);
}



// Need to be defined, if CPU65_TRAP_OPCODE is defined for the CPU emulator!
int cpu65_trap_callback ( Uint8 opcode )
{
	if (cpu65.pc >= 0xA000 && opcode == CPU65_TRAP_OPCODE) {	// cpu65.pc always meant to be the position _after_ the trap opcode!
		Uint8 trap = memory[cpu65.pc];
		if (is_our_rom() < 0)
			FATAL("Unknown ROM/RAM code at $%04X caused trap!", cpu65.pc - 1);
		switch (trap) {
			case 0:
				cpu65.a = inject_prg();
				EMUPRINTF("\rMONITOR: SYS %d\r", 0xA009);
				break;
			case 1:
				EMUPRINTF("** XVIC20 MONITOR/LGB\rBM=$%04X-%04X S=$%04X\rEXP %s\r",
					memory[0x281] | (memory[0x282] << 8),
					memory[0x283] | (memory[0x284] << 8),
					memory[648] << 8,
					vic20_get_memconfig_string()
				);
				cpu65.a = 1;
				break;
			case 2:
				cpu65.a = 1;	// by default, set the continue flag ...
				execute_monitor_command();
				break;
			default:
				FATAL("Unknown CPU trap (%d) at $%04X", trap, cpu65.pc - 1);
		}
		cpu65.pc++;	// jump over the trap number byte ...
		return 1; // you must return with the CPU cycles used, but at least with value of 1!
	} else
		return 0; // ignore trap!! Return with zero means, the CPU emulator should execute the opcode anyway
}


void cpu65_illegal_opcode_callback ( void )
{
	ERROR_WINDOW("Unemulated NMOS 6502 opcode $%02X at PC=$%04X", cpu65.op, cpu65.pc - 1);
	cpu65_reset();
}



void clear_emu_events ( void )
{
	hid_reset_events(1);
}



// Called by CPU emulation code when any kind of memory byte must be written.
// Note: optimization is used, to make the *most common* type of write access easy. Even if the whole function is more complex, or longer/slower this way for other accesses!
void  cpu65_write_callback ( Uint16 addr, Uint8 data )
{
	// Write optimization, handle the most common case first: memory byte to be written is not special, ie writable RAM, not I/O, etc
	if (XEMU_LIKELY(is_kpage_writable[addr >> 10])) {	// writable flag for every Kbytes of 64K is checked (for different memory configurations, faster "decoding", etc)
		memory[addr] = data;
		return;
	}
	// ELSE: other kind of address space is tried to be written ...
	// TODO check if I/O devices are fully decoded or there can be multiple mirror ranges
	if ((addr & 0xFF00) == 0x9000) {	// VIC-I register is written (decoded for a full 256 bytes long area)
		cpu_vic_reg_write(addr & 0xF, data);
		return;
	}
	if ((addr & 0xFFF0) == 0x9110) {	// VIA-1 register is written
		via_write(&via1, addr & 0xF, data);
		return;
	}
	if ((addr & 0xFFF0) == 0x9120) {	// VIA-2 register is written
		via_write(&via2, addr & 0xF, data);
		return;
	}
	// if other memory areas tried to be written (hit this point), write will be simply ignored
}



// TODO: Use RMW write function in a proper way!
void cpu65_write_rmw_callback ( Uint16 addr, Uint8 old_data, Uint8 new_data )
{
	if (XEMU_UNLIKELY(addr & 0x8000)) {
		cpu65_write_callback(addr, old_data);
		cpu65_write_callback(addr, new_data);
	} else {
		cpu65_write_callback(addr, new_data);
	}
}


// Called by CPU emulation code when any kind of memory byte must be read.
// Note: optimization is used, to make the *most common* type of read access easy. Even if the whole function is more complex, or longer/slower this way for other accesses!
Uint8 cpu65_read_callback ( Uint16 addr )
{
	// Optimization: handle the most common case first!
	// Check if our read is NOT about the (built-in) I/O area. If it's true, let's just use the memory array
	// (even for undecoded areas, memory[] is intiailized with 0xFF values
	if (XEMU_LIKELY((addr & 0xF800) != 0x9000))
		return memory[addr];
	// ELSE: it IS the I/O area or colour SRAM ... Let's see what we want!
	// TODO check if I/O devices are fully decoded or there can be multiple mirror ranges
	if ((addr & 0xFFF0) == 0x9000)		// VIC-I register is read
		return cpu_vic_reg_read(addr & 0xF);
	if ((addr & 0xFFF0) == 0x9110)		// VIA-1 register is read
		return via_read(&via1, addr & 0xF);
	if ((addr & 0xFFF0) == 0x9120)		// VIA-2 register is read
		return via_read(&via2, addr & 0xF);
	if ((addr & 0xFC00) == 0x9400)
		return memory[addr] | 0xF0;	// colour SRAM is read, always return '1' for upper bits (only 4 bits "wide" memory chip!)
	// if non of the above worked, let's just read our emulated address space
	// which means some kind of ROM, or undecoded area (64K space is intialized with 0xFF bytes ...)
	return memory[addr];
}


// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
	return 0;
}


static void update_emulator ( void )
{
	if (!frameskip) {
		// First: update VIC-20 screen ...
		xemu_update_screen();
		// Second: we must handle SDL events waiting for us in the event queue ...
		hid_handle_all_sdl_events();
		// Third: Sleep ... Please read emutools.c source about this madness ... 40000 is (PAL) microseconds for a full frame to be produced
		xemu_timekeeping_delay(FULL_FRAME_USECS);
	}
	vic_vsync(!frameskip);	// prepare for the next frame!
}


/* VIA emulation callbacks, called by VIA core. See main() near to via_init() calls for further information */


// VIA-1 generates NMI on VIC-20?
static void via1_setint ( int level )
{
	if (nmi_level != level) {
		printf("VIA-1: NMI edge: %d->%d" NL, nmi_level, level);
		cpu65.nmiEdge = 1;
		nmi_level = level;
	}
}


// VIA-2 is used to generate IRQ on VIC-20
static void via2_setint ( int level )
{
	cpu65.irqLevel = level;
}




static Uint8 via2_kbd_get_scan ( Uint8 mask )
{
	return
		((via2.ORB &   1) ? 0xFF : kbd_matrix[0]) &
		((via2.ORB &   2) ? 0xFF : kbd_matrix[1]) &
		((via2.ORB &   4) ? 0xFF : kbd_matrix[2]) &
		((via2.ORB &   8) ? 0xFF : kbd_matrix[3]) &
		((via2.ORB &  16) ? 0xFF : kbd_matrix[4]) &
		((via2.ORB &  32) ? 0xFF : kbd_matrix[5]) &
		((via2.ORB &  64) ? 0xFF : kbd_matrix[6]) &
		((via2.ORB & 128) ? 0xFF : kbd_matrix[7])
	;
}


static Uint8 via1_ina ( Uint8 mask )
{
	// joystick state (RIGHT direction is not handled here though)
	return
		hid_read_joystick_left  (0, 1 << 4) |
		hid_read_joystick_up    (0, 1 << 2) |
		hid_read_joystick_down  (0, 1 << 3) |
		hid_read_joystick_button(0, 1 << 5)
	;
}


static Uint8 via2_inb ( Uint8 mask )
{
	// Port-B in VIA2 is used (temporary with DDR-B set to input) to scan joystick direction 'RIGHT'
	return hid_read_joystick_right(0x7F, 0xFF);
}



static int cycles;


static void emulation_loop ( void )
{
	for (;;) { // our emulation loop ...
		int opcyc;
		opcyc = cpu65_step();	// execute one opcode (or accept IRQ, etc), return value is the used clock cycles
		via_tick(&via1, opcyc);	// run VIA-1 tasks for the same amount of cycles as the CPU
		via_tick(&via2, opcyc);	// -- "" -- the same for VIA-2
		cycles += opcyc;
		if (cycles >= CYCLES_PER_SCANLINE) {	// if [at least!] 71 (on PAL) CPU cycles passed then render a VIC-I scanline, and maintain scanline value + texture/SDL update (at the end of a frame)
			// render one (scan)line. Note: this is INACCURATE, we should do rendering per dot clock/cycle or something,
			// but for a simple emulator like this, it's already acceptable solultion, I think!
			// Note about frameskip: we render only every second (half) frame, no interlace (PAL VIC), not so correct, but we also save some resources this way
			if (!frameskip)
				vic_render_line();
			if (scanline == LAST_SCANLINE) {
				update_emulator();
				frameskip = !frameskip;
				return;
			} else
				scanline++;
			cycles -= CYCLES_PER_SCANLINE;
		}
	}
}



int main ( int argc, char **argv )
{
	xemu_pre_init(APP_ORG, TARGET_NAME, "The Inaccurate Commodore VIC-20 emulator from LGB");
	xemucfg_define_switch_option("bootmon", "Boot into monitor");
	xemucfg_define_switch_option("fullscreen", "Start in fullscreen mode");
	xemucfg_define_str_option("prg", NULL, "Load a PRG file");
	xemucfg_define_str_option("ramexp", NULL, "Comma separated list of installed RAM expansions at Kbyte(s)");
	xemucfg_define_str_option("romchr",    CHR_ROM_NAME, "Sets character ROM to use");
	xemucfg_define_str_option("rombasic",  BASIC_ROM_NAME, "Sets BASIC ROM to use");
	xemucfg_define_str_option("romkernal", KERNAL_ROM_NAME, "Sets KERNAL ROM to use");
	xemucfg_define_str_option("romemu",    EMU_ROM_NAME, "Sets EMU ROM to use");
	xemucfg_define_switch_option("syscon", "Keep system console open (Windows-specific effect only)");
	if (xemucfg_parse_all(argc, argv))
		return 1;
	emurom_policy = xemucfg_get_bool("bootmon");	// normally: "boot" into BASIC, but to monitor of -bootmon was used
	emufile_p = NULL;
	emufile_size = 0;
	printf(
		"INFO: CPU clock frequency (calculated) %d Hz (wanted: %d Hz)" NL
		"INFO: Texture resolution is %dx%d" NL
		"INFO: Defined visible area is (%d,%d)-(%d,%d)" NL NL,
		(int)((LAST_SCANLINE + 1) * CYCLES_PER_SCANLINE * (1000000.0 / (double)FULL_FRAME_USECS) * 2),
		REAL_CPU_SPEED,
		SCREEN_WIDTH, SCREEN_HEIGHT,
		SCREEN_FIRST_VISIBLE_DOTPOS, SCREEN_FIRST_VISIBLE_SCANLINE,
		SCREEN_LAST_VISIBLE_DOTPOS,  SCREEN_LAST_VISIBLE_SCANLINE
	);
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH * 2, SCREEN_HEIGHT,	// logical size (width is doubled for somewhat correct aspect ratio)
		SCREEN_WIDTH * 2 * 2, SCREEN_HEIGHT * 2,	// window size (doubled size, original would be too small)
		SCREEN_FORMAT,		// pixel format
		16,			// we have 16 colours
		init_vic_palette_rgb,	// initialize palette from this constant array
		vic_palette,		// initialize palette into this stuff
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		NULL			// no emulator specific shutdown function
	))
		return 1;
	hid_init(
		vic20_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_ENABLE		// enable HID joy events
	);
	// Program to load?
	if (xemucfg_get_str("prg")) {
		emufile_size = xemu_load_file(xemucfg_get_str("prg"), NULL, 3, 0x8000, "Cannot load user specified PRG with -prg");
		if (emufile_size < 0) {
			emufile_p = NULL;
			emufile_size = 0;
		} else
			emufile_p = xemu_load_buffer_p;
	}
	// RAM expansion
	if (xemucfg_get_str("ramexp")) {
		int explist[5], r = xemucfg_integer_list_from_string(xemucfg_get_str("ramexp"), explist, 5, ",");
		if (r < 0)
			FATAL("Invalid memory expansion list (not comma separated list, more than 5 elements, etc) syntax given with -ramexp");
		else
			while (r--)
				switch (explist[r]) {
					case  1:
						__mark_ram( 1, 3);
						break;
					case  8:
						__mark_ram( 8, 8);
						break;
					case 16:
						__mark_ram(16, 8);
						break;
					case 24:
						__mark_ram(24, 8);
						break;
					case 40:
						__mark_ram(40, 8);
						INFO_WINDOW("Warning, RAM from 40K (at $A000) is defined.\nThis may collide with the loaded EMU ROM there!");
						break;
					default:
						FATAL("Unknown memory expansion element %d in -ramexp %s", explist[r], xemucfg_get_str("ramexp"));
						break;
				}
	}
	/* Intialize memory and load ROMs */
	memset(memory, 0xFF, sizeof memory);
	memset(dummy_vic_access, 0xFF, sizeof dummy_vic_access);	// define 1K of "nothing" for VIC-I memory regions what it can't access by hardware constraints
	if (
		xemu_load_file(xemucfg_get_str("romchr"),    memory + 0x8000, 0x1000, 0x1000, rom_fatal_msg) < 0 ||	// load chargen ROM
		xemu_load_file(xemucfg_get_str("rombasic"),  memory + 0xC000, 0x2000, 0x2000, rom_fatal_msg) < 0 ||	// load basic ROM
		xemu_load_file(xemucfg_get_str("romkernal"), memory + 0xE000, 0x2000, 0x2000, rom_fatal_msg) < 0 ||	// load kernal ROM
		xemu_load_file(xemucfg_get_str("romemu"),    memory + 0xA000, 0x2000, 0x2000, rom_fatal_msg) < 0		// load our "emulator monitor" ROM
	)
		return 1;
	// Check our "emulator monitor" ROM ...
	if (is_our_rom() < -1) {
		ERROR_WINDOW("Unknown emulator ROM: %s", xemucfg_get_str("romemu"));
		return 1;
	}
	if (is_our_rom() != EMU_ROM_VERSION) {
		ERROR_WINDOW("Bad emulator ROM %s version, we need v%d\nPlease upgrade the ROM image!", xemucfg_get_str("romemu"), EMU_ROM_VERSION);
		return 1;
	}
	// Continue with initializing ...
	clear_emu_events();	// also resets the keyboard
	cpu65_reset();	// reset CPU: it must be AFTER kernal is loaded at least, as reset also fetches the reset vector into PC ...
	// Initiailize VIAs.
	// Note: this is my unfinished VIA emulation skeleton, for my Commodore LCD emulator originally, ported from my JavaScript code :)
	// it uses callback functions, which must be registered here, NULL values means unused functionality
	via_init(&via1, "VIA-1",	// from $9110 on VIC-20
		NULL,	// outa
		NULL,	// outb
		NULL,	// outsr
		via1_ina, // ina
		NULL,	// inb
		NULL,	// insr
		via1_setint	// setint, called by via core, if interrupt level changed for whatever reason (ie: expired timer ...). It is wired to NMI on VIC20.
	);
	via_init(&via2, "VIA-2",	// from $9120 on VIC-20
		NULL,			// outa [reg 1]
		NULL, //via2_kbd_set_scan,	// outb [reg 0], we wire port B as output to set keyboard scan, HOWEVER, we use ORB directly in get scan!
		NULL,	// outsr
		via2_kbd_get_scan,	// ina  [reg 1], we wire port A as input to get the scan result, which was selected with port-A
		via2_inb,		// inb  [reg 0], used with DDR set to input for joystick direction 'right' in VIC20
		NULL,	// insr
		via2_setint	// setint, same for VIA2 as with VIA1, but this is wired to IRQ on VIC20.
	);
	vic_init(vic_address_space_lo8, vic_address_space_hi4);
	cycles = 0;
	xemu_set_full_screen(xemucfg_get_bool("fullscreen"));
	if (!xemucfg_get_bool("syscon"))
		sysconsole_close(NULL);
	xemu_timekeeping_start();	// we must call this once, right before the start of the emulation
	XEMU_MAIN_LOOP(emulation_loop, 25, 1);
	return 0;
}
