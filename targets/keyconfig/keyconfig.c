/* Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include <errno.h>


#define SCREEN_FORMAT           SDL_PIXELFORMAT_ARGB8888
// Do not modify this, this very program depends on it being 0.
#define USE_LOCKED_TEXTURE      0
#define RENDER_SCALE_QUALITY    1
#define SCREEN_WIDTH            800
#define SCREEN_HEIGHT           400
#define FRAME_DELAY		40
#define STATUS_COLOUR_INDEX	1
#define BACKGROUND_COLOUR_INDEX	6
// Keyboard
#define NORMAL_COLOUR_INDEX	1
#define SELECT_COLOUR_INDEX	1
#define OVER_COLOUR_INDEX	2
#define KEYTEST_COLOUR_INDEX	1
#define CONFLICT_COLOUR_INDEX	1
#define UNUSABLE_COLOUR_INDEX	1
#define LETTER_COLOUR_INDEX	0
#define STATUS(...)		do { char buffer[1024]; snprintf(buffer, sizeof buffer, __VA_ARGS__); write_status(buffer); } while(0)
#define OSD_TRAY(...)		OSD(-1,SCREEN_HEIGHT-20,__VA_ARGS__)

extern Uint32 *sdl_pixel_buffer;
static const Uint8 init_vic2_palette_rgb[16 * 3] = {    // VIC2 palette given by RGB components
	0x00, 0x00, 0x00,
	0xFF, 0xFF, 0xFF,
	0x74, 0x43, 0x35,
	0x7C, 0xAC, 0xBA,
	0x7B, 0x48, 0x90,
	0x64, 0x97, 0x4F,
	0x40, 0x32, 0x85,
	0xBF, 0xCD, 0x7A,
	0x7B, 0x5B, 0x2F,
	0x4f, 0x45, 0x00,
	0xa3, 0x72, 0x65,
	0x50, 0x50, 0x50,
	0x78, 0x78, 0x78,
	0xa4, 0xd7, 0x8e,
	0x78, 0x6a, 0xbd,
	0x9f, 0x9f, 0x9f
};
static Uint32 palette[256];



void clear_emu_events ( void )
{
	xemu_drop_events();
}

static int load_keymap ( const char *fn, void (*callback)(const char*, SDL_Scancode) )
{
	char line[256];
	FILE *fp = fopen(fn, "rb");
	if (!fp) {
		ERROR_WINDOW("Cannot open file %s: %s", fn, strerror(errno));
		return -1;
	}
	while (fgets(line, sizeof line, fp)) {
		if (strlen(line) > 250) {
			ERROR_WINDOW("Too long line in file %s", fn);
			fclose(fp);
			return -1;
		}
		if (*line == '\0' || *line == '#')
			continue;
		char *p1 = line + strlen(line) - 1;
		while (*p1 <= 0x20 && p1 >= line)
			p1--;
		p1[1] = '\0';
		printf("line=[%s]\n", line);
		if (*line == '\0')
			continue;
		p1 = line;
		while (*p1 <= 0x20 && *p1 != '\0')
			p1++;
		if (*p1 == '\0') {
			printf("bad1=[%s]\n", line);
			continue;	// BAD entry, skip it ...
		}
		// now p1 points to the first column
		char *p2 = p1;
		while (*p2 > 0x20)
			p2++;
		if (*p2 == '\0') {
			puts("bad2");
			continue;	// BAD entry, skip it ...
		}
		*p2++ = '\0';	// termination of the first field
		while (*p2 <= 0x20 && *p2 != '\0')
			p2++;
		if (*p2 == '\0') {
			puts("bad3");
			continue;	// BAD entry, skip it ...
		}
		/*char *p3 = p2 + strlen(p2) - 1;
		while (*p3 <= 0x20)
			p3--;
		p3[1] = '\0';*/
		printf("F1=[%s] F2=[%s] %d\n", p1,p2,SDL_GetScancodeFromName(p2));
		callback(p1, SDL_GetScancodeFromName(p2));
	}
	fclose(fp);
	return 0;
}

static void clear_screen ( void )
{
	for (unsigned int a = 0; a < SCREEN_WIDTH * SCREEN_HEIGHT; a++)
		sdl_pixel_buffer[a] = palette[BACKGROUND_COLOUR_INDEX];
}

static void clear_area ( int x1, int y1, int x2, int y2, Uint32 colour )
{
	Uint32 *pix = sdl_pixel_buffer + y1 * SCREEN_WIDTH;
	while (y1 <= y2) {
		y1++;
		for (int x = x1; x <= x2; x++)
			pix[x] = colour;
		pix += SCREEN_WIDTH;
	}
}

int write_char ( int x1, int y1, char chr, Uint32 colour )
{
	Uint32 *pix = sdl_pixel_buffer + y1 * SCREEN_WIDTH + x1;
	int char_width = 0;
	int char_start = 32;
	if ((signed char)chr < 32)
		chr = '?';
	for (int y = 0; y < 16; y++)
		for (Uint32 b = font_16x16[((chr - 32) << 4) + y], x = 0; b; b <<= 1, x++)
			if ((b & 0x8000) && (x < char_start))
				char_start = x;
	for (int y = 0; y < 16; y++) {
		for (Uint32 b = font_16x16[((chr - 32) << 4) + y] << char_start, x = 0; b; b <<= 1, x++) {
			if ((b & 0x8000)) {
				pix[x] = colour;
				if (x > char_width)
					char_width = x;
			}
		}
		pix += SCREEN_WIDTH;
	}
	return chr == ' ' ? 5 : char_width + 2;
}

static int write_string_min_x = 0, write_string_min_y = 0;
static int write_string_max_x = 0, write_string_max_y = 0;
static int clear_boundary_box = 1;

static void write_string ( int x1, int y1, const char *str, Uint32 colour )
{
	int x = x1;
	if (clear_boundary_box) {
		write_string_min_x = x1;
		write_string_min_y = y1;
		write_string_max_x = 0;
		write_string_max_y = 0;
	}
	while (*str) {
		if (*str == 1 && str[1]) {
			colour = palette[str[1] & 15];
			str += 2;
		} else if (*str == '\n') {
			x = x1;
			y1 += 16;
			str++;
		} else {
			x += write_char(x, y1, *str++, colour);
			if (clear_boundary_box) {
				if (y1 > write_string_max_y)
					write_string_max_y = y1;
				if (x > write_string_max_x)
					write_string_max_x = x;
			}
		}
	}
}


static void write_status ( const char *str )
{
	int y = SCREEN_HEIGHT - 16 - 32;
	for (int a = 0; str[a]; a++)
		if (str[a] == '\n' && str[a + 1])
			y -= 16;
	if (write_string_max_x && clear_boundary_box)
		clear_area(write_string_min_x, write_string_min_y, write_string_max_x, write_string_max_y + 16, palette[BACKGROUND_COLOUR_INDEX]);
	write_string(32, y, str, palette[STATUS_COLOUR_INDEX]);
}

struct keyboard_st {
	int		x1, y1, x2, y2;
	const char	*title;
	const char 	*name;
	SDL_Scancode	code;
};
static struct keyboard_st keyboard_storage[256], *keyboard_limit = keyboard_storage;


static struct keyboard_st *search_key_by_coord ( int x, int y )
{
	struct keyboard_st *k = keyboard_storage;
	while (k < keyboard_limit)
		if (x >= k->x1 && x <= k->x2 && y >= k->y1 && y <= k->y2)
			return k;
		else
			k++;
	return NULL;
}
static struct keyboard_st *search_key_by_name ( const char *name )
{
	struct keyboard_st *k = keyboard_storage;
	while (k < keyboard_limit)
		if (!strcmp(name, k->name))
			return k;
		else
			k++;
	return NULL;
}
static struct keyboard_st *search_key_by_code ( SDL_Scancode code )
{
	struct keyboard_st *k = keyboard_storage;
	while (k < keyboard_limit)
		if (k->code == code)
			return k;
		else
			k++;
	return NULL;
}



static void construct_key ( int x1, int y1, int x2, int y2, const char *title, const char *name, SDL_Scancode code )
{
	keyboard_limit->x1	= x1;
	keyboard_limit->y1	= y1;
	keyboard_limit->x2	= x2;
	keyboard_limit->y2	= y2;
	keyboard_limit->title	= xemu_strdup(title);
	keyboard_limit->name	= xemu_strdup(name);
	keyboard_limit->code	= code;
	keyboard_limit++;
	DEBUGPRINT("Key constructed: %s %s at %d,%d-%d,%d" NL, title, name, x1, y1, x2, y2);
}

static void draw_key ( struct keyboard_st *k, int mode )
{
	clear_area(k->x1, k->y1, k->x2, k->y2, palette[mode]);
	clear_boundary_box = 0;
	write_string(k->x1 + 1, k->y1 + 1, k->title, palette[LETTER_COLOUR_INDEX]);
	clear_boundary_box = 1;
	DEBUGPRINT("Drawning key title: %s" NL, k->title);
}

static void assign_named_key ( const char *name, SDL_Scancode code )
{
	struct keyboard_st *k = search_key_by_name(name);
	if (!k)
		DEBUGPRINT("WARNING: unknown referenced entity: \"%s\"" NL, name);
	else {
		k->code = code;
		DEBUGPRINT("ASSIGN: assigned \"%s\"" NL, name);
	}
}

static void draw_full_keyboard ( void )
{
	struct keyboard_st *k = keyboard_storage;
	while (k < keyboard_limit)
		draw_key(k++, NORMAL_COLOUR_INDEX);
}

static double construct_keyboard_row ( double x, double y, double x_step, int width, int height, const char *desc )
{
	char buffer[4096], *s = buffer;
	strcpy(buffer, desc);
	for (;;) {
		char *e = strchr(s, '\1');
		if (e)
			*e = '\0';
		char *p = strchr(s, '=');
		if (p)
			*p++ = '\0';
		else
			p = s;
		construct_key(x, y, x + width, y + height, s, p, SDL_SCANCODE_UNKNOWN);
		x += x_step;
		if (!e)
			break;
		s = e + 1;
	}
	return x;
}


static void construct_mega_keyboard ( void )
{
	// top row (function keys, etc)
	construct_keyboard_row( 28          ,   4, 48.5, 26, 30, "stop");
	construct_keyboard_row( 28 +  2*48.5,   4, 48.5, 26, 30, "esc\1alt\1lock\1no.sc");
	construct_keyboard_row( 28 +  7*48.5,   4, 48.5, 26, 30, "F1\1F3\1F5\1F7");
	construct_keyboard_row( 28 + 12*48.5,   4, 48.5, 26, 30, "F9\1F11\1F13\1help");
	// kind of number row
	construct_keyboard_row( 28,  68, 48.5, 26, 30, "<-\1" "1\1" "2\1" "3\1" "4\1" "5\1" "6\1" "7\1" "8\1" "9\1" "0\1+\1-\1pnd\1clr\1DEL");
	// Q-W-E-R-T-Y
	construct_keyboard_row(100, 114, 48.5, 26, 26, "Q\1W\1E\1R\1T\1Y\1U\1I\1O\1P\1a\1b\1c");
	// A-S-D-F
	construct_keyboard_row( 15, 159, 48.5, 26, 26, "ctrl\1lck\1A\1S\1D\1F\1G\1H\1J\1K\1L\1d\1e\1f");
	// Z-X-C
	construct_keyboard_row(136, 202, 48.5, 26, 26, "Z\1X\1C\1V\1B\1N\1M\1<\n,\1>\n.\1?\n/");
}


int main ( int argc, char **argv )
{
	static const char boring_warning[] = "\nThis program is not meant to be used manually.\nPlease use the menu of an Xemu emulator which supports key re-mapping.";
	xemu_pre_init(APP_ORG, TARGET_NAME, "Xemu Keyboard Configurator");
	if (argc != 4)
		FATAL("Missing specifier(s) from command line.%s", boring_warning);
	if (!strcmp(argv[3], "mega65"))
		construct_mega_keyboard();
	else
		FATAL("Bad target specifier: \"%s\"%s", argv[3], boring_warning);
	DEBUGPRINT("Loading default set: %s" NL, argv[1]);
	if (load_keymap(argv[1], assign_named_key))
		return 1;
	DEBUGPRINT("Loading used set: %s" NL, argv[2]);
	//construct_key(10,10,20,20,"t","TestKey");
	if (xemu_post_init(
		TARGET_DESC APP_DESC_APPEND,	// window title
		1,				// resizable window
		SCREEN_WIDTH, SCREEN_HEIGHT,	// texture sizes
		SCREEN_WIDTH, SCREEN_HEIGHT,	// logical size
		SCREEN_WIDTH, SCREEN_HEIGHT,	// window size
		SCREEN_FORMAT,		// pixel format
		16,			// number of colours to init
		init_vic2_palette_rgb,	// init from this
		palette,		// .. and into this!
		RENDER_SCALE_QUALITY,	// render scaling quality
		USE_LOCKED_TEXTURE,	// 1 = locked texture access
		NULL			// no emulator specific shutdown function
	))
		return 1;
	//osd_init_with_defaults();
	const Uint8 osd_palette[] = {
		0xFF, 0, 0, 0x80,	// with alpha channel 0x80
		0xFF,0xFF,0x80,0xFF	// letter colour
	};
	osd_init(
		//OSD_TEXTURE_X_SIZE, OSD_TEXTURE_Y_SIZE,
		SCREEN_WIDTH * 1, SCREEN_HEIGHT * 1,
		osd_palette,
		sizeof(osd_palette) >> 2,
		OSD_FADE_DEC_VAL,
		OSD_FADE_END_VAL
	);
	OSD_TRAY("Welcome to Xemu's Keymap Configurator!");
	clear_screen();
	SDL_Surface *surf = SDL_LoadBMP("mega65-kbd.bmp");
	if (surf) {
		printf("Colours=%d\n", surf->format->palette->ncolors);
		for (int a = 0; a < 128; a++)
			palette[128 + (a & 127)] = SDL_MapRGBA(
				sdl_pix_fmt,
				surf->format->palette->colors[a].r,
				surf->format->palette->colors[a].g,
				surf->format->palette->colors[a].b,
				0xFF
			);
		printf("BitsPerPixel=%d BytesPerPixel=%d\n",
			surf->format->BitsPerPixel,
			surf->format->BytesPerPixel
		);
		for (int a = 0; a < 800 * 300; a++)
			sdl_pixel_buffer[a] = palette[128 + (((Uint8*)surf->pixels)[a] & 127)];
	} else
		FATAL("Cannot load keyboard image: %s", SDL_GetError());
	draw_full_keyboard();
	//write_char(10,10,'A', palette[1]);
	//write_string(10, SCREEN_HEIGHT - 30, "Printout!", palette[1]);
	//write_status("Hi!\nHow are you?\nHmm?!");
	static const char help_msg[] =  "Quick help: click on a key to assign, then press a key\non your keyboard to define it\nClose window to save or cancel your mapping.";
	STATUS("%s", help_msg);
	Uint32 old_ticks;
	xemu_update_screen();
	int frames = 0;
	struct keyboard_st *wait_for_assignment = NULL, *mouse_on_key = NULL;
	SDL_Scancode result_for_assignment = SDL_SCANCODE_UNKNOWN;
	for (;;) {
		int force_render = 0;
		SDL_Event ev;
		old_ticks = SDL_GetTicks();
		while (SDL_PollEvent(&ev)) {
			switch(ev.type) {
				case SDL_QUIT:
					if (ARE_YOU_SURE(NULL, ARE_YOU_SURE_DEFAULT_YES))
						exit(1);
					break;
				case SDL_KEYUP:
					if (wait_for_assignment) {
						if (ev.key.repeat) {
							OSD_TRAY("ERROR: key repeats, too long press!");
						} else if (result_for_assignment != ev.key.keysym.scancode && ev.key.keysym.scancode != SDL_SCANCODE_UNKNOWN) {
							OSD_TRAY("ERROR: multiple keypresses are used!");
							result_for_assignment = SDL_SCANCODE_UNKNOWN;
						} else if (result_for_assignment == ev.key.keysym.scancode && result_for_assignment != SDL_SCANCODE_UNKNOWN) {
							// FIXME TODO: check is key is free! And only allow to accept then!
							//struct keyboard_st *k = search_key_by_code(ev.key.keysym.scancode);
							//if (k != NULL)
							//	QUESTION("This pressed key \"%\" already assigned to function \"%s\"");
							OSD_TRAY("OK: assigned key: %s", SDL_GetScancodeName(ev.key.keysym.scancode));
							STATUS("Last operation: assigned key: %s\nDo not give up, assign a new key today! Just here and now!", SDL_GetScancodeName(ev.key.keysym.scancode));
							wait_for_assignment->code = result_for_assignment;
							result_for_assignment = SDL_SCANCODE_UNKNOWN;
							wait_for_assignment = NULL;
						}
					} else {
						//OSD_TRAY("No key is clicked on the map");*/
					}
					break;
				case SDL_KEYDOWN:
					if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
						exit(1);
					if (wait_for_assignment) {
						if (ev.key.repeat) {
							OSD_TRAY("ERROR: key repeats, too long press!");
						} else if (result_for_assignment != SDL_SCANCODE_UNKNOWN) {
							OSD_TRAY("ERROR: multiple keypresses are used!");
							//result_for_assignment = ev.key.keysym.scancode;
						} else {
							if (ev.key.keysym.scancode == SDL_SCANCODE_UNKNOWN)
								OSD_TRAY("ERROR: This key has no proper SDL decode!");
							else
								result_for_assignment = ev.key.keysym.scancode;
						}
					} else {
						//OSD_TRAY("No key is clicked on the map");*/
						struct keyboard_st *k = search_key_by_code(ev.key.keysym.scancode);
						const char *sname = SDL_GetScancodeName(ev.key.keysym.scancode);
						if (k) {
							STATUS("KEY: %s %s %s", k->title, k->name, *sname ? sname : "UNASSIGNED");
						} else {
							STATUS("The pressed key (%s) is not assigned to any emulated key.", sname);
						}
					}
					force_render = 1;
					break;
				case SDL_MOUSEBUTTONDOWN:
					if (mouse_on_key) {
						DEBUGPRINT("X=%d, Y=%d" NL, ev.button.x, ev.button.y);
						OSD_TRAY("Waiting your keypress to assign!!");
						STATUS("Now please press a key on your keyboard to assign for the emulated key: %s", mouse_on_key->name);
						wait_for_assignment = mouse_on_key;
						force_render = 1;
						mouse_on_key = NULL;
					} else if (wait_for_assignment)
						OSD_TRAY("Not possible before assigning the selected key");
					break;
				//case SDL_MOUSEBUTTONUP:
				case SDL_MOUSEMOTION:
					if (!wait_for_assignment && ev.motion.x >= 0 && ev.motion.x < SCREEN_WIDTH && ev.motion.y >= 0 && ev.motion.y < SCREEN_HEIGHT) {
						//DEBUGPRINT("X=%d, Y=%d" NL, ev.motion.x, ev.motion.y);
						//sdl_pixel_buffer[ev.motion.x + ev.motion.y * SCREEN_WIDTH] = palette[1];
						struct keyboard_st *k = search_key_by_coord(ev.motion.x, ev.motion.y);
						if (k) {
							if (mouse_on_key != k) {
								mouse_on_key = k;
								draw_full_keyboard();
								draw_key(k, OVER_COLOUR_INDEX);
								const char *sname = SDL_GetScancodeName(k->code);
								STATUS("KEY: %s %s %s", k->title, k->name, *sname ? sname : "UNASSIGNED");
								force_render = 1;
							}
						} else if (mouse_on_key) {
							STATUS("%s", help_msg);
							draw_full_keyboard();
							mouse_on_key = NULL;
							force_render = 1;
						}
					}
					break;
				case SDL_WINDOWEVENT:
					// it's a bit cruel, but it'll do
					// basically I just want to avoid rendering texture to screen if not needed
					// any window event MAY mean some reason to do it, some of them may be not
					// but it's not fatal anyway to do so.
					force_render = 1;
					break;
				default:
					break;
			}
		}
		if (force_render || osd_status || !frames) {
			xemu_update_screen();
			frames = 15;
		} else
			frames--;
		//else
		//	DEBUGPRINT("No need to update, yeah-youh!" NL);
		Uint32 new_ticks = SDL_GetTicks();
		Uint32 delay = FRAME_DELAY - (new_ticks - old_ticks);
		if (delay > 0 && delay <= FRAME_DELAY)
			SDL_Delay(FRAME_DELAY);
	}
}
