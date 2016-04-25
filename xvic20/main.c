/* Test-case for a very simple and inaccurate Commodore VIC-20 emulator using SDL2 library.
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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>

#include "xvic20.h"
#include "cpu65c02.h"
#include "via65c22.h"


// I assume that VIC-20 has exactly 1MHz CPU clock, and we need full tv frame, ie 25FPS,
// thus 1MHz = 1million cycle / 25 = 40000
#define CPU_CYCLES_PER_TV_FRAME	40000

#define SCREEN_WIDTH		176
#define SCREEN_HEIGHT		184
#define SCREEN_DEFAULT_ZOOM	4
#define SCREEN_FORMAT		SDL_PIXELFORMAT_ARGB8888



static Uint8 memory[0x10000];	// 64K address space of the 6502 CPU (some of it is ROM, undecoded, whatsoever ...)
static const Uint8 vic_palette_rgb[16][3] = {	// VIC palette given by RGB components
	{ 0x00, 0x00, 0x00 },	// black
	{ 0xFF, 0xFF, 0xFF },	// white
	{ 0xF0, 0x00, 0x00 },	// red
	{ 0x00, 0xF0, 0xF0 },	// cyan
	{ 0x60, 0x00, 0x60 },	// purple
	{ 0x00, 0xA0, 0x00 },	// green
	{ 0x00, 0x00, 0xF0 },	// blue
	{ 0xD0, 0xD0, 0x00 },	// yellow
	{ 0xC0, 0xA0, 0x00 },	// orange
	{ 0xFF, 0xA0, 0x00 },	// light orange
	{ 0xF0, 0x80, 0x80 },	// pink
	{ 0x00, 0xFF, 0xFF },	// light cyan
	{ 0xFF, 0x00, 0xFF },	// light purple
	{ 0x00, 0xFF, 0x00 },	// light green
	{ 0x00, 0xA0, 0xFF },	// light blue
	{ 0xFF, 0xFF, 0x00 }	// light yellow
};
static Uint32 vic_palette[16];			// VIC palette with native SCREEN_FORMAT aware way. It will be initialized once only from vic_palette_rgb
static Uint32 pixels[SCREEN_WIDTH * SCREEN_HEIGHT];	// we use "SDL native" 32 bit per pixel, RGBA format
static int running = 1;
static int is_fullscreen = 0;	// current state of fullscreen (0 = no)
static int win_xsize, win_ysize;	// we will use this to save window size before enterint into fullscreen mode
static SDL_Window *sdl_win = NULL;
static Uint32 sdl_winid;
static SDL_Renderer *sdl_ren;
static SDL_Texture  *sdl_tex;
static struct Via65c22 via1, via2;
static Uint8 kbd_matrix[8];		// keyboard matrix state, 8 * 8 bits
static int kb_selector = 0;		// keyboard scan selector
static struct timeval tv_old;		// timing related stuff
static int sleep_balancer;		// also a timing stuff

struct KeyMapping {
	SDL_Scancode	scan;		// SDL scancode for the given key we want to map
	Uint8		pos;		// BCD packed, high nibble / low nibble for col/row to map to.  0xFF means end of table!, high bit set on low nibble: press virtual shift as well!
};
static const struct KeyMapping key_map[] = {
	{ SDL_SCANCODE_0, 0x00 },
	{ SDL_SCANCODE_1, 0x11 },
	{ SDL_SCANCODE_2, 0x22 },
	{ SDL_SCANCODE_3, 0x33 },
	{ SDL_SCANCODE_4, 0x44 },
	{ SDL_SCANCODE_5, 0x55 },
	{ SDL_SCANCODE_6, 0x66 },
	{ SDL_SCANCODE_7, 0x77 },
	{ 0,	0xFF	}		// this must be the last line: end of mapping table
};





static int load_emu_file ( const char *fn, void *buffer, int size )
{
	int fd = open(fn, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open file %s\n", fn);
		return 1;
	}
	if (read(fd, buffer, size) != size) {
		fprintf(stderr, "Cannot read %d bytes from file %s\n", size, fn);
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}



// Called by CPU emulation code
void  cpu_write(Uint16 addr, Uint8 data)
{
	if ((addr & 0xFFF0) == 0x9110)
		via_write(&via1, addr & 0xF, data);
	else if ((addr & 0xFFF0) == 0x9120)
		via_write(&via2, addr & 0xF, data);
	// later, this should be done with a look-up table (also for expanded VIC-20 etc) eg, write enable mask for every 256 byte pages
	if (addr < 0x400 || (addr >= 0x1000 && addr < 0x2000) || (addr >= 0x9000 && addr < 0xA000))
		memory[addr] = data;
}

// Called by CPU emulation code
Uint8 cpu_read(Uint16 addr)
{
	if ((addr & 0xFFF0) == 0x9110)
		return via_read(&via1, addr & 0xF);
	if ((addr & 0xFFF0) == 0x9120)
		return via_read(&via2, addr & 0xF);
	return memory[addr];
}




static void render_screen ( void )
{
	int x, y, sc;
	Uint32 *pp;
	Uint8 *vidp, *colp;
	Uint32 bg = vic_palette[memory[0x900F] >> 4];	// background colour ...
	// Render VIC screen to "pixels"
	// Note: this is VERY incorrect, and only some std screen aware rendering,
	// with ignoring almost ALL of the VIC's possibilities!!!!!!
	// This is only a demonstration
	// Real emulation would use VIC registers, also during the main loop
	// so raster effects etc would work!!!! But this is only a quick demonstration :)
	// This should be done in sync with CPU emulation instead in steps, using the VIC-I registers to define parameters, and VIC-20 memory expansion (ie different video RAM location or so?) ...
	x = 0;
	y = 0;
	sc = 0;
	vidp = memory + 0x1E00;
	colp = memory + 0x9600;
	pp = pixels;
	while (y < 23) {
		int b;
		Uint8 shape = memory[0x8000 + ((*vidp) << 3) + sc];	// shape of current scanline of the current character
		Uint32 fg = vic_palette[*(colp) & 0x7];			// foreground colour
		//Uint8 shape = memory[0x8000 + sc];
		if ((*(colp) & 0x8))
			shape = 255 - shape; 	// inverse
		for (b = 128; b; b >>= 1)
			*(pp++) = (shape & b) ? fg : bg;
		if (x < 21) {
			vidp++;
			colp++;
			x++;
		} else {
			x = 0;
			if (sc < 7) {
				vidp -= 21;	// "rewind" video pointer
				colp -= 21;	// ... and also the colour RAM pointer
				sc++;		// next scanline of character
			} else {
				y++;
				sc = 0;
				vidp++;
				colp++;
			}
		}
	}
	//printf("WOW %d\n", pp -pixels);
	// Do the SDL stuff, update window with our "pixels" data ... This is the
	// actual SDL magic to refresh the "screen" (well, the window)
	// In theory, you can even render more textures on top of each with
	// different alpha channel, colour modulation etc, to get a composite result.
	// I use this feature in my Enterprise-128 emulator to display emulation related
	// information over the emulated screen, as some kind of "OSD" (On-Screen Display)
	SDL_UpdateTexture(sdl_tex, NULL, pixels, SCREEN_WIDTH * sizeof (Uint32));
	SDL_RenderClear(sdl_ren);
	SDL_RenderCopy(sdl_ren, sdl_tex, NULL, NULL);
	SDL_RenderPresent(sdl_ren);
}



static void toggle_full_screen ( void )
{
	if (is_fullscreen) {
		// it was in full screen mode before ...
		if (SDL_SetWindowFullscreen(sdl_win, 0)) {
			fprintf(stderr, "Cannot leave full screen mode: %s\n", SDL_GetError());
		} else {
			is_fullscreen = 0;
			SDL_SetWindowSize(sdl_win, win_xsize, win_ysize); // restore window size saved on entering fullscreen, there can be some bugs ...
		}
	} else {
		// it was in window mode before ...
		SDL_GetWindowSize(sdl_win, &win_xsize, &win_ysize); // save window size, it seems there are some problems with leaving fullscreen then
		if (SDL_SetWindowFullscreen(sdl_win, SDL_WINDOW_FULLSCREEN_DESKTOP)) {
			fprintf(stderr, "Cannot enter full screen mode: %s\n", SDL_GetError());
		} else {
			is_fullscreen = 1;
		}
	}
	SDL_RaiseWindow(sdl_win); // I have some problems with EP128 emulator that window went to the background. Let's handle that with raising it anyway :)
}



static void debug_show_kbd_matrix ( void )
{
	int a, b;
	for (a = 0; a < 8; a++) {
		for (b = 128; b; b >>= 1)
			putchar(kbd_matrix[a] & b ? '1' : '0');
		putchar('\n');
	}
}




// pressed: non zero value = key is pressed, zero value = key is released
static void emulate_keyboard ( SDL_Scancode key, int pressed )
{
	if (key == SDL_SCANCODE_F11) {	// toggle full screen mode on/off
		if (pressed)
			toggle_full_screen();
	} else if (key == SDL_SCANCODE_F9) {	// exit emulator ...
		if (pressed)
			running = 0;
	} else {
		const struct KeyMapping *map = key_map;
		while (map->pos != 0xFF) {
			if (map->scan == key) {
				if (pressed)
					kbd_matrix[map->pos >> 4] &= 255 - (1 << (map->pos & 0xF));
				else
					kbd_matrix[map->pos >> 4] |= 1 << (map->pos & 0xF);
				fprintf(stderr, "Found key, pos = %02Xh\n", map->pos);
				debug_show_kbd_matrix();
				break;	// key found, end.
			}
			map++;
		}
	}
}



static void update_emulator ( void )
{
	struct timeval tv_new;
	int t_emu, t_slept;
	SDL_Event e;
	// First: rendner VIC-20 screen ...
	render_screen();
	// Second: we must handle SDL events waiting for us in the event queue ...
	while (SDL_PollEvent(&e) != 0) {
		switch (e.type) {
			case SDL_QUIT:		// ie: someone closes the SDL window ...
				running = 0;	// set running to zero, main loop will exit then
				break;
			case SDL_KEYDOWN:	// key is pressed (down)
			case SDL_KEYUP:		// key is released (up)
				// make sure that key event is for our window, also that it's not a releated event by long key presses
				if (e.key.repeat == 0 && (e.key.windowID == sdl_winid || e.key.windowID == 0))
					emulate_keyboard(e.key.keysym.scancode, e.key.state == SDL_PRESSED);	// the last argument will be zero in case of release, other val in case of pressing
				break;
		}
	}
	// Now the timing follows! We now how much time we need for a full TV frame on a real VIC (1/25 sec)
	// We messure how much time we used on the PC, and if less, we sleep the rest
	// Note: this is not a precise emulation, as sleep functions would be not perfect in case of a multitask
	// OS, which runs our litte emulator!
	gettimeofday(&tv_new, NULL);
	t_emu = (tv_new.tv_sec - tv_old.tv_sec) * 1000000 + (tv_new.tv_usec - tv_old.tv_usec);	// microseconds we needed to emulate one frame (SDL etc stuffs included!), it's 40000 on a real VIC-20
	t_emu = 40000 - t_emu;	// if it's positive, we're faster in emulation than a real VIC-20, if negative, we're slower, and can't keep real-time emulation :(
	sleep_balancer += t_emu;
	// chop insane values, ie stopped emulator for a while, other time setting artifacts etc ...
	if (sleep_balancer < -250000 || sleep_balancer > 250000)
		sleep_balancer = 0;
	if (sleep_balancer > 1000)	// usless to sleep too short time (or even negative ...) as the OS scheduler won't sleep smaller time amounts than the sheduling frequency after all
		usleep(sleep_balancer);
	// dual purpose: this will be the start time of next frame, also we check the exact time we slept with usleep() as usleep() on a multitask OS cannot be precise ever!!
	gettimeofday(&tv_old, NULL);
	t_slept = (tv_old.tv_sec - tv_new.tv_sec) * 1000000 + (tv_old.tv_usec - tv_new.tv_usec);	// real time we slept ... (warning, old/new are exchanged here with valid reason)
	// correct sleep balancer with the real time slept, again if it's not "insane" value we got ...
	if (t_slept > -250000 && t_slept < 250000)
		sleep_balancer -= t_slept;
}



static void shutdown_emulator ( void )
{
	if (sdl_win)
		SDL_DestroyWindow(sdl_win);
	SDL_Quit();
	puts("Shutdown callback function has been called.");
}




// The SDL init stuff. Also it initiailizes the VIC-20 colour palette
static int xvic20_init_sdl ( void )
{
	int a;
	if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
		fprintf(stderr, "Cannot initialize SDL: %s\n", SDL_GetError());
		return 1;
	}
	atexit(shutdown_emulator);
	sdl_win = SDL_CreateWindow(
		"LGB's little XVic20 experiment",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		SCREEN_WIDTH * SCREEN_DEFAULT_ZOOM, SCREEN_HEIGHT * SCREEN_DEFAULT_ZOOM,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
	);
	if (!sdl_win) {
		fprintf(stderr, "Cannot create SDL window: %s\n", SDL_GetError());
		return 1;
	}
	//SDL_SetWindowMinimumSize(sdl_win, SCREEN_WIDTH, SCREEN_HEIGHT * 2);
	sdl_ren = SDL_CreateRenderer(sdl_win, -1, 0);
	if (!sdl_ren) {
		fprintf(stderr, "Cannot create SDL renderer: %s\n", SDL_GetError());
		return 1;
	}
	SDL_RenderSetLogicalSize(sdl_ren, SCREEN_WIDTH, SCREEN_HEIGHT  );	// this helps SDL to know the "logical ratio" of screen, even in full screen mode when scaling is needed!
	sdl_tex = SDL_CreateTexture(sdl_ren, SCREEN_FORMAT, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
	if (!sdl_tex) {
		fprintf(stderr, "Cannot create SDL texture: %s\n", SDL_GetError());
		return 1;
	}
	sdl_winid = SDL_GetWindowID(sdl_win);
	// Intitialize VIC palette
	for (a = 0; a < 16; a++)
		// actually this may be bad on other endian computers :-/ I forgot now how to use SDL's mapRGBA for the given pixel format :-]
		vic_palette[a] = (0xFF << 24) | (vic_palette_rgb[a][0] << 16) | (vic_palette_rgb[a][1] << 8) | (vic_palette_rgb[a][2]);
	return 0;
}


/* VIA emulation callbacks, called by VIA core. See main() near to via_init() calls for further information */


static void via1_setint ( int level )
{
	if (level)	cpu_irqLevel |=   1;
	else		cpu_irqLevel &= 254;
}


static void via2_setint ( int level )
{
	if (level)	cpu_irqLevel |=   2;
	else		cpu_irqLevel &= 253;
}


static void via2_kbd_set_scan ( Uint8 mask, Uint8 data )
{
	printf("SCAN set to %02X with mask %02X\n", data, mask);
	kb_selector = data;
}

static Uint8 via2_kbd_get_scan ( Uint8 mask )
{
	//printf("SCAN get with mask %02X\n", mask);
	return
		((kb_selector &   1) ? 0xFF : kbd_matrix[7]) &
		((kb_selector &   2) ? 0xFF : kbd_matrix[6]) &
		((kb_selector &   4) ? 0xFF : kbd_matrix[5]) &
		((kb_selector &   8) ? 0xFF : kbd_matrix[4]) &
		((kb_selector &  16) ? 0xFF : kbd_matrix[3]) &
		((kb_selector &  32) ? 0xFF : kbd_matrix[2]) &
		((kb_selector &  64) ? 0xFF : kbd_matrix[1]) &
		((kb_selector & 128) ? 0xFF : kbd_matrix[0])
	;
}





int main ( void )
{
	int cycles;
	/* Intialize memory and load ROMs */
	memset(memory, 0xFF, sizeof memory);
	if (
		load_emu_file("rom/chargen", memory + 0x8000, 0x1000) ||	// load chargen ROM
		load_emu_file("rom/basic", memory + 0xC000, 0x2000) ||	// load basic ROM
		load_emu_file("rom/kernal", memory + 0xE000, 0x2000)	// load kernal ROM
	) {
		fprintf(stderr, "Cannot load some of the needed ROM images (see message above)!\n");
		return 1;
	}
	/* Initialize SDL */
	if (xvic20_init_sdl())
		return 1;
	/* Start */
	memset(kbd_matrix, 0xFF, sizeof kbd_matrix);	// initialize keyboard matrix [bit 1 = unpressed, thus 0xFF for a line]
	cpu_reset();	// reset CPU
	// Initiailize VIAs.
	// Note: this is my unfinished VIA emulation skeleton, for my Commodore LCD emulator originally, ported from my JavaScript code :)
	// it uses call back functions, which must be registered here, NULL values means unused functionality
	via_init(&via1, "VIA-1",
		NULL,	// outa
		NULL,	// outb
		NULL,	// outsr
		NULL,	// ina
		NULL,	// inb
		NULL,	// insr
		via1_setint	// setint, called by via core, if interrupt level changed for whatever reason (ie: expired timer ...)
	);
	via_init(&via2, "VIA-2",
		NULL,	// outa
		via2_kbd_set_scan,	// outb, we wire port B as output to set keyboard scan
		NULL,	// outsr
		via2_kbd_get_scan,	// ina, we wire port A as input to get the scan result, which was selected with port-A
		NULL,	// inb
		NULL,	// insr
		via2_setint	// setint, same for VIA2 as with VIA1. Note: I have no idea if both VIAs can generate IRQ on VIC-20 though, maybe it's overkill to do for both and even cause problems?
	);
	cycles = 0;
	gettimeofday(&tv_old, NULL);	// update_emulator() needs a starting time for timing purposes ...
	sleep_balancer = 0;
	while (running) { // our emulation loop ...
		int opcyc;
		//printf("%04Xh\n", cpu_pc);	
		opcyc = cpu_step();	// execute one opcode (or accept IRQ, etc), return value is the used clock cycles
		via_tick(&via1, opcyc);	// run VIA-1 tasks for the same amount of cycles as the CPU
		via_tick(&via2, opcyc);	// -- "" -- the same for VIA-2
		cycles += opcyc;
		if (cycles >= CPU_CYCLES_PER_TV_FRAME) {	// if enough cycles elapsed (what would be the amount of CPU cycles for a TV frame), let's call the update function.
			update_emulator();	// this is the heart of screen update, also to handle SDL events (like key presses ...)
			cycles -= CPU_CYCLES_PER_TV_FRAME;	// not just cycle = 0, to avoid rounding errors, but it would not matter too much anyway ...
		}
	}
	puts("Goodbye!");
	return 0;
}

