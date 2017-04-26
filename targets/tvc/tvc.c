/* Test-case for a very simple and inaccurate Videoton TV computer
   (a Z80 based 8 bit computer) emulator using SDL2 library.

   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

   This emulator is HIGHLY inaccurate and unusable.

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
#include "xemu/emutools_hid.h"
#include "xemu/emutools_config.h"
#include "xemu/z80.h"
#include "tvc.h"
#include "sdext.h"


#define CLOCKS_PER_FRAME (CPU_CLOCK / 50)
#define VIRTUAL_SHIFT_POS 0x63

extern const struct KeyMapping tvc_key_map[];

// The Z80 emulation context itself
Z80EX_CONTEXT z80ex;

// Misc. system level stuffs
static Uint8 io_port_values[0x100];
static int   colour_mode;
static int   keyboard_row;
static int   interrupt_active;

// Memory emulation related stuffs
typedef void  (*memcbwr_type)(int, Uint8);
typedef Uint8 (*memcbrd_type)(int);
static struct {
	memcbwr_type wr_selector[4];
	memcbrd_type rd_selector[4];
	Uint8 *vmem;
	Uint8 user_ram [0x10000];
	Uint8 video_ram[0x10000];
	Uint8 sys_rom  [0x04000 + 1];
	Uint8 ext_rom  [0x04000 + 1];	// the first 8K is not used from this, currently ...
} mem;

// CRTC "internal" related stuffs
const Uint8 crtc_write_masks[18] = {
	0xFF /* R0 */, 0xFF /* R1 */, 0xFF /* R2 */, 0xFF /* R3 */, 0x7F /* R4 */, 0x1F /* R5 */,
	0x7F /* R6 */, 0x7F /* R7 */, 0xFF /* R8 */, 0x1F /* R9 */, 0x7F /* R10*/, 0x1F /*R11 */,
	0x3F /* R12 */, 0xFF /* R13 */, 0x3F /* R14 */, 0xFF /* R15 */, 0xFF /* R16 */, 0xFF /* R17 */
};
static struct {
	Uint8 *vmem;
	Uint8 registers[18];
	int   regsel;
} crtc;

// Palette related, contains "SDL-ready" values!
static Uint32 tvc_palette_rgb[16];
static Uint32 tvc_palette_bw [16];
static Uint32 palette[4];
static Uint32 palette_col16_pixel1[0x100];
static Uint32 palette_col16_pixel2[0x100];
static Uint32 border_colour;

static int frameskip = 0;



static INLINE Uint32 TVC_COLOUR_BYTE_TO_SDL ( Uint8 c )
{
	return tvc_palette_rgb[(c & 1) | ((c >> 1) & 2) | ((c >> 2) & 4) | ((c >> 3) & 8)];
}


static void crtc_write_register ( int reg, Uint8 data )
{
	if (likely(reg < 16)) {
		data &= crtc_write_masks[reg];	// this will chop unused bits off for the given register
		DEBUG("CRTC: register %02Xh is written with data %02Xh" NL, reg, data);
		crtc.registers[reg] = data;
	}
}


static Uint8 crtc_read_register ( int reg )
{
	if (likely(reg >= 12 && reg <= 17))
		return crtc.registers[reg];
	return 0xFF;
}



static void  memcbwr_ram_u0 ( int addr, Uint8 data ) { mem.user_ram[addr         ] = data; }
static void  memcbwr_ram_u1 ( int addr, Uint8 data ) { mem.user_ram[addr + 0x4000] = data; }
static void  memcbwr_ram_u2 ( int addr, Uint8 data ) { mem.user_ram[addr + 0x8000] = data; }
static void  memcbwr_ram_u3 ( int addr, Uint8 data ) { mem.user_ram[addr + 0xC000] = data; }
static Uint8 memcbrd_ram_u0 ( int addr ) { return mem.user_ram[addr         ]; }
static Uint8 memcbrd_ram_u1 ( int addr ) { return mem.user_ram[addr + 0x4000]; }
static Uint8 memcbrd_ram_u2 ( int addr ) { return mem.user_ram[addr + 0x8000]; }
static Uint8 memcbrd_ram_u3 ( int addr ) { return mem.user_ram[addr + 0xC000]; }
static void  memcbwr_ram_video ( int addr, Uint8 data ) { mem.vmem[addr] = data; }
static Uint8 memcbrd_ram_video ( int addr ) { return mem.vmem[addr]; }
static Uint8 memcbrd_rom_sys  ( int addr ) { return mem.sys_rom[addr]; }
static Uint8 memcbrd_rom_ext  ( int addr ) { return mem.ext_rom[addr]; }
static void  memcbwr_nowrite ( int addr, Uint8 data ) { }

#ifdef CONFIG_SDEXT_SUPPORT
#define memcbrd_rom_cart sdext_read_cart
#define memcbwr_rom_cart sdext_write_cart
#else
static Uint8 memcbrd_rom_cart ( int addr ) { return 0xFF; }
static void  memcbwr_rom_cart ( int addr, Uint8 data ) { }
#endif

static const memcbrd_type memory_rd_selectors_for_page3[4] = { memcbrd_rom_cart, memcbrd_rom_sys,  memcbrd_ram_u3, memcbrd_rom_ext };
static const memcbwr_type memory_wr_selectors_for_page3[4] = { memcbwr_rom_cart, memcbwr_nowrite,  memcbwr_ram_u3, memcbwr_nowrite };
static const memcbrd_type memory_rd_selectors_for_page0[4] = { memcbrd_rom_sys,  memcbrd_rom_cart, memcbrd_ram_u0, memcbrd_ram_u3  };
static const memcbwr_type memory_wr_selectors_for_page0[4] = { memcbwr_nowrite,  memcbwr_rom_cart, memcbwr_ram_u0, memcbwr_ram_u3  };



Z80EX_BYTE z80ex_mread_cb ( Z80EX_WORD addr, int m1_state )
{
	return (mem.rd_selector[addr >> 14])(addr & 0x3FFF);
}



void z80ex_mwrite_cb ( Z80EX_WORD addr, Z80EX_BYTE value )
{
	(mem.wr_selector[addr >> 14])(addr & 0x3FFF, value);
}



Z80EX_BYTE z80ex_pread_cb ( Z80EX_WORD port16 )
{
	port16 &= 0xFF;
	DEBUG("IO: reading I/O port %02Xh" NL, port16);
	switch (port16) {
		case 0x58: case 0x5C:
			DEBUG("Reading keyboard (row=%d, result=$%02X)!" NL, keyboard_row, kbd_matrix[keyboard_row]);
			return keyboard_row < 10 ? kbd_matrix[keyboard_row] : 0xFF;
		case 0x59: case 0x5D:
			return interrupt_active ? 0xEF: 0xFF;
		case 0x70:
			return crtc.regsel;
		case 0x71:
			return crtc_read_register(crtc.regsel);
	}
	return 0xFF;
}



void z80ex_pwrite_cb ( Z80EX_WORD port16, Z80EX_BYTE value )
{
	port16 &= 0xFF;
	io_port_values[port16] = value;
	if (port16 != 2)	// to avoid flooding ...
		DEBUG("IO: writing I/O port %02Xh with data %02Xh" NL, port16, value);
	switch (port16) {
		case 0x00:
			border_colour = TVC_COLOUR_BYTE_TO_SDL(value >> 1);
			break;
		case 0x02:
			// page 3
			mem.rd_selector[3] = memory_rd_selectors_for_page3[value >> 6];
			mem.wr_selector[3] = memory_wr_selectors_for_page3[value >> 6];
			// page 2
			if (value & 32) {
				mem.rd_selector[2] = memcbrd_ram_u2;
				mem.wr_selector[2] = memcbwr_ram_u2;
			} else {
				mem.rd_selector[2] = memcbrd_ram_video;
				mem.wr_selector[2] = memcbwr_ram_video;
			}
			// page 0
			mem.rd_selector[0] = memory_rd_selectors_for_page0[(value >> 3) & 3];
			mem.wr_selector[0] = memory_wr_selectors_for_page0[(value >> 3) & 3];
			// page 1
			if (!(value & 4)) {
				mem.rd_selector[1] = memcbrd_ram_u1;
				mem.wr_selector[1] = memcbwr_ram_u1;
			} else {
				mem.rd_selector[1] = memcbrd_ram_video;
				mem.wr_selector[1] = memcbwr_ram_video;
			}
			break;
		case 0x03:
			// z80ex_pwrite_cb(2, io_port_values[2]); // TODO: we need this later with expansion mem paging!
			keyboard_row = value & 0xF;	// however the lower 4 bits are for selecting row
			break;
		case 0x05:
			DEBUG("Enabled_SoundIT=%d Enabled_CursorIT=%d" NL, value & 32 ? 1 : 0, value & 16 ? 1 : 0);
			break;
		case 0x06:
			colour_mode = value & 3;
			if (colour_mode == 3)
				colour_mode = 2;
			DEBUG("VIDEO: colour mode is %d" NL, colour_mode);
			break;
		case 0x07:
			// clear cursor/sound IT. FIXME: any write would do it?!
			interrupt_active = 0;
			break;
		case 0x0C: case 0x0D: case 0x0E: case 0x0F:
			crtc.vmem = mem.video_ram + ((value & 0x30) << 10);
			mem.vmem  = mem.video_ram + ((value & 0x03) << 14);
			break;
		case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
		case 0x68: case 0x69: case 0x6A: case 0x6B: case 0x6C: case 0x6D: case 0x6E: case 0x6F:
			palette[port16 & 3] = TVC_COLOUR_BYTE_TO_SDL(value);
			break;
		case 0x70: case 0x72: case 0x74: case 0x76: case 0x78: case 0x7A: case 0x7C: case 0x7E:
			crtc.regsel = value & 31;
			break;
		case 0x71: case 0x73: case 0x75: case 0x77: case 0x79: case 0x7B: case 0x7D: case 0x7F:
			crtc_write_register(crtc.regsel, value);
			break;
	}
}



Z80EX_BYTE z80ex_intread_cb ( void )
{
	return 0xFF;
}


void z80ex_reti_cb ( void )
{
}


void clear_emu_events ( void )
{
	hid_reset_events(1);
}


// Well :) It's just an ugly hack to render one frame at once.
// There is "some CRTC emulation", though far from being all
// features are expected to work!
// Note: though the selected 16K (TVC64+ is emulated!) is used ...
static inline void render_tvc_screen ( void )
{
	int tail, y;
	Uint32 *pix = emu_start_pixel_buffer_access(&tail);
	int ma = (crtc.registers[12] << 8) | crtc.registers[13];	// CRTC MA signals, 14 bit
	int ra = 0;	// CRTC RA signals
	int start_line, limit_line, start_cpos, limit_cpos;
	//DEBUG("CRTCINFO: start addr = $%04X" NL, ma);
	//for (y = 0; y < sizeof crtc.registers; y++)
	//	DEBUG("CRTCINFO: R%02d=%d" NL, y, crtc.registers[y]);
	start_line = (SCREEN_HEIGHT - (crtc.registers[6] * (crtc.registers[9] + 1))) >> 1;
	limit_line = SCREEN_HEIGHT - start_line;
	start_cpos = ((SCREEN_WIDTH >> 3) - crtc.registers[1]) >> 1;
	limit_cpos =  (SCREEN_WIDTH >> 3) - start_cpos;
	for (y = 0; y < SCREEN_HEIGHT; y++) {
		int x;
		if (y >= start_line && y < limit_line) {	// active content!
			int addr = (ma & 63) | (ra << 6) | ((ma & 0xFC0) << 2);
			for (x = 0; x < (SCREEN_WIDTH >> 3); x++) {
				if (x >= start_cpos && x < limit_cpos) {
					Uint8 b = crtc.vmem[(addr++) & 0x3FFF];
					switch (colour_mode) {
						case 0:	// 2-colours mode
							pix[0] = palette[(b >> 7) & 1];
							pix[1] = palette[(b >> 6) & 1];
							pix[2] = palette[(b >> 5) & 1];
							pix[3] = palette[(b >> 4) & 1];
							pix[4] = palette[(b >> 3) & 1];
							pix[5] = palette[(b >> 2) & 1];
							pix[6] = palette[(b >> 1) & 1];
							pix[7] = palette[ b       & 1];
							break;
						case 1:	// 4-colours mode
							pix[0] = pix[1] = palette[((b & 0x80) >> 7) | ((b & 0x08) >> 2)];
							pix[2] = pix[3] = palette[((b & 0x40) >> 6) | ((b & 0x04) >> 1)];
							pix[4] = pix[5] = palette[((b & 0x20) >> 5) |  (b & 0x02)      ];
							pix[6] = pix[7] = palette[((b & 0x10) >> 4) | ((b & 0x01) << 1)];
							break;
						case 2: // 16-colours mode
							pix[0] = pix[1] = pix[2] = pix[3] = palette_col16_pixel1[b];
							pix[4] = pix[5] = pix[6] = pix[7] = palette_col16_pixel2[b];
							break;
					}
				} else	// sider border
					pix[0] = pix[1] = pix[2] = pix[3] = pix[4] = pix[5] = pix[6] = pix[7] = border_colour;
				pix += 8;
			}
			if (ra == crtc.registers[9]) {
				ra = 0;
				ma += crtc.registers[1];
			} else
				ra++;
		} else					// top or bottom border ...
			for (x = 0; x < SCREEN_WIDTH; x++)
				*pix++ = border_colour;
		pix += tail;
	}
	emu_update_screen();
}



// HID needs this to be defined, it's up to the emulator if it uses or not ...
int emu_callback_key ( int pos, SDL_Scancode key, int pressed, int handled )
{
        return 0;
}



static void update_emulator ( void )
{
	// FIXME: Ugly trick, we generate IRQ here, not by CRTC cursor stuff, what would do it for real on TVC!
	// This means, that TVC uses the cursor info of CRTC "abused" to be used an a periodic interrupt source. Tricky!
	// However, this implementation of mine if very wrong, and the exact time for the IRQ should depend on CRTC reg settings
	// I THINK ... :-D
	if (io_port_values[5] & 16) {	// if enabled at all!
		interrupt_active = 1;
		DEBUG("Cursor interrupt!" NL);
	}
	// Rest of the update stuff, but only at 25Hz rate ...
	if (!frameskip) {
		render_tvc_screen();
		hid_handle_all_sdl_events();
		emu_timekeeping_delay(40000);	// number: the time needed (real-time) for a "full frame"
	}
}


static void init_tvc ( void )
{
	int a;
	// Initialize colours (TODO: B&W mode is not used currently!)
	for (a = 0; a < 16; a++) {
		int red   = (a & 2) ? ((a & 8) ? 0xFF : 0x80) : 0;
		int green = (a & 4) ? ((a & 8) ? 0xFF : 0x80) : 0;
		int blue  = (a & 1) ? ((a & 8) ? 0xFF : 0x80) : 0;
		int y     = 0.299 * red + 0.587 * green + 0.114 * blue;
		tvc_palette_rgb[a] = SDL_MapRGBA(sdl_pix_fmt, red, green, blue, 0xFF);
		tvc_palette_bw [a] = SDL_MapRGBA(sdl_pix_fmt, y,   y,     y,    0xFF);
	}
	// Initialize helper tables for 16 colours mode
	//  16 colour mode does not use the 4 element palette register, but layout is not "TVC colour byte" ...
	//  TVC "colour byte is":	-I-G-R-B
	//  Format in 16 col mode:
	//	* first pixel:		I-G-R-B-
	//	* second pixel:		-I-G-R-B	(this matches the colour byte layout, btw!)
	for (a = 0; a < 256; a++) {
		palette_col16_pixel1[a] = TVC_COLOUR_BYTE_TO_SDL(a >> 1);
		palette_col16_pixel2[a] = TVC_COLOUR_BYTE_TO_SDL(a);
	}
	// I/O, ROM and RAM intialization ...
	memset(&mem, 0xFF, sizeof mem);
	memset(&io_port_values, 0x00, 0x100);	// some I/O handlers re-calls itself with other values, so we zero the stuff first
	memset(crtc.registers, 0x00, sizeof crtc.registers);
	for (a = 0; a < 0x100; a++)		// ... and now use the callback which sets our variables for some initial value ...
		z80ex_pwrite_cb(a, 0);
	for (a = 0; a < sizeof crtc.registers; a++)
		crtc_write_register(a, 0);
	if (emu_load_file("tvc22_d6_64k.rom", mem.sys_rom + 0x0000, 0x2001) != 0x2000 ||
	    emu_load_file("tvc22_d4_64k.rom", mem.sys_rom + 0x2000, 0x2001) != 0x2000 ||
	    emu_load_file("tvc22_d7_64k.rom", mem.ext_rom + 0x2000, 0x2001) != 0x2000
	)
		FATAL("Cannot load ROM(s).");
#ifdef CONFIG_SDEXT_SUPPORT
	if (emucfg_get_bool("sdext"))
		sdext_init();
#endif
}




int main ( int argc, char **argv )
{
	int cycles;
	sysconsole_open();
	xemu_dump_version(stdout, "The Careless Videoton TV Computer emulator from LGB");
#ifdef CONFIG_SDEXT_SUPPORT
	emucfg_define_switch_option("sdext", "Enables SD-ext");
	emucfg_define_str_option("sdimg", SDCARD_IMG_FN, "SD-card image filename / path");
#endif
	if (emucfg_parse_commandline(argc, argv, NULL))
		return 1;
	/* Initiailize SDL - note, it must be before loading ROMs, as it depends on path info from SDL! */
	if (emu_init_sdl(
		TARGET_DESC APP_DESC_APPEND,	// window title
		APP_ORG, TARGET_NAME,		// app organization and name, used with SDL pref dir formation
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT * 2,	// logical size (width is doubled for somewhat correct aspect ratio)
		SCREEN_WIDTH, SCREEN_HEIGHT * 2,	// window size (tripled in size, original would be too small)
		SCREEN_FORMAT,		// pixel format
		0,			// custom palette init. Later, in init_tvc()
		NULL,			// -- "" --
		NULL,			// -- "" --
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		NULL			// no emulator specific shutdown function
	))
		return 1;
	hid_init(
		tvc_key_map,
		VIRTUAL_SHIFT_POS,
		SDL_DISABLE		// no joystick HID events
	);
	init_tvc();
	// Continue with initializing ...
	clear_emu_events();	// also resets the keyboard
	z80ex_init();
	cycles = 0;
	interrupt_active = 0;
	emu_timekeeping_start();	// we must call this once, right before the start of the emulation
	for (;;) { // our emulation loop ...
		if (interrupt_active) {
			int a = z80ex_int();
			if (a)
				cycles += a;
			else
				cycles += z80ex_step();
		} else
			cycles += z80ex_step();
		if (cycles >= CLOCKS_PER_FRAME) {
			update_emulator();
			frameskip = !frameskip;
			cycles -= CLOCKS_PER_FRAME;
		}
	}
	return 0;
}
