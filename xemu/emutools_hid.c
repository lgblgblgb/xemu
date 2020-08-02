/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and the MEGA65 as well.
   Copyright (C)2016-2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#ifdef HID_KBD_MAP_CFG_SUPPORT
#include "xemu/emutools_files.h"
#endif


/* Note: HID stands for "Human Input Devices" or something like that :)
   That is: keyboard, joystick, mouse.
   TODO: positional mapping stuff for keys _ONLY_
   TODO: no precise joy emu (multiple joys/axes/buttons/whatsoever)
   TODO: mouse emulation is unfinished
*/


Uint8 kbd_matrix[16];		// keyboard matrix state, 16 * 8 bits at max currently (not compulsory to use all positions!)
static int mouse_delta_x;
static int mouse_delta_y;
static unsigned int hid_state;

#define MAX_JOYSTICKS			16

static SDL_Joystick *joysticks[MAX_JOYSTICKS];

#define JOYSTATE_UP			 1
#define JOYSTATE_DOWN			 2
#define JOYSTATE_LEFT			 4
#define JOYSTATE_RIGHT			 8
#define JOYSTATE_BUTTON			16
#define MOUSESTATE_BUTTON_LEFT		32
#define MOUSESTATE_BUTTON_RIGHT		64

//static const struct KeyMappingUsed *key_map = NULL;
static Uint8 virtual_shift_pos = 0;
static struct KeyMappingUsed key_map[0x100];
static const struct KeyMappingDefault *key_map_default;
static int release_this_key_on_first_event = -1;


void hid_set_autoreleased_key ( int key )
{
	release_this_key_on_first_event = key;
}


int hid_key_event ( SDL_Scancode key, int pressed )
{
	const struct KeyMappingUsed *map = key_map;
	//OSD(-1, -1, "Key %s <%s>", pressed ? "press  " : "release", SDL_GetScancodeName(key));
	while (map->pos >= 0) {
		if (map->scan == key) {
			if (XEMU_UNLIKELY(release_this_key_on_first_event > 0)) {
				KBD_RELEASE_KEY(release_this_key_on_first_event);
				release_this_key_on_first_event = -1;
			}
			if (map->pos > 0xFF) {	// special emulator key!
				switch (map->pos) {	// handle "built-in" events, if emulator target uses them at all ...
					case XEMU_EVENT_EXIT:
						if (ARE_YOU_SURE(str_are_you_sure_to_exit, i_am_sure_override | ARE_YOU_SURE_DEFAULT_YES))
							exit(0);
						break;
					case XEMU_EVENT_FAKE_JOY_UP:
						if (pressed) hid_state |= JOYSTATE_UP;     else hid_state &= ~JOYSTATE_UP;
						break;
					case XEMU_EVENT_FAKE_JOY_DOWN:
						if (pressed) hid_state |= JOYSTATE_DOWN;   else hid_state &= ~JOYSTATE_DOWN;
						break;
					case XEMU_EVENT_FAKE_JOY_LEFT:
						if (pressed) hid_state |= JOYSTATE_LEFT;   else hid_state &= ~JOYSTATE_LEFT;
						break;
					case XEMU_EVENT_FAKE_JOY_RIGHT:
						if (pressed) hid_state |= JOYSTATE_RIGHT;  else hid_state &= ~JOYSTATE_RIGHT;
						break;
					case XEMU_EVENT_FAKE_JOY_FIRE:
						if (pressed) hid_state |= JOYSTATE_BUTTON; else hid_state &= ~JOYSTATE_BUTTON;
						break;
					case XEMU_EVENT_TOGGLE_FULLSCREEN:
						if (pressed)
							xemu_set_full_screen(-1);
						break;
					default:
						return emu_callback_key(map->pos, key, pressed, 0);
				}
				return emu_callback_key(map->pos, key, pressed, 1);
			}
			if (map->pos & 8)			// shifted key emu?
				KBD_SET_KEY(virtual_shift_pos, pressed);	// maintain the shift key
			KBD_SET_KEY(map->pos, pressed);
			return emu_callback_key(map->pos, key, pressed, 1);
		}
		map++;
	}
	return emu_callback_key(-1, key, pressed, 0);
}


// Reset all HID events.
// Ie: it's usefull for initialization, and in the case when the emulator pops a window,
// in this case SDL may detect the event used to ack the window causing problems. So those
// functions should call this as well to reset events. It also uses some "burning SDL
// events" scheme to skip the possible received stuffs.
void hid_reset_events ( int burn )
{
	KBD_CLEAR_MATRIX();	// set keyboard matrix to default state (unpressed for all positions)
	mouse_delta_x = 0;
	mouse_delta_y = 0;
	hid_state = 0;
	if (burn)
		xemu_drop_events();
}


#ifdef HID_KBD_MAP_CFG_SUPPORT

#define CLEARALLMAPPING "CLEARALLMAPPING"

static const char *HID_ERR_STR_UNKNOWN_HOST_KEYNAME = "Unknown host keyname";
static const char *HID_ERR_STR_UNKNOWN_EMU_KEYNAME = "Unknown emu keyname";

static const char *scan_name_unknown = "Unknown";

const char *hid_keymap_add_mapping ( const char *emu_key_name, const char *host_key_name )
{
	SDL_Scancode scan;
	if (strcmp(host_key_name, scan_name_unknown)) {
		scan = SDL_GetScancodeFromName(host_key_name);
		if (scan == SDL_SCANCODE_UNKNOWN)
			return HID_ERR_STR_UNKNOWN_HOST_KEYNAME;
	} else
		scan = SDL_SCANCODE_UNKNOWN;
	// Look up matrix position from the default mapping given to hid_init() which also carries the "emu key names"
	int pos;
	int source;
	for (source = 0 ;; source++) {
		if (key_map_default[source].pos < 0)
			return HID_ERR_STR_UNKNOWN_EMU_KEYNAME;
		if (key_map_default[source].name && (!strcmp(key_map_default[source].name, emu_key_name))) {
			//DEBUGPRINT("FOUND KEY: %s for %s" NL, key_map_default[source].name, emu_key_name);
			pos = key_map_default[source].pos;
			break;
		}
	}
	// At this point we have the SDL scancode, and the "matrix position" ...
	// We change all mappings' scan code to the desired ones (maybe more PC keys is mapped to a single emulated key!)
	for (int a = 0; key_map[a].pos >= 0 ; a++) {
		if (key_map[a].pos == pos && key_map[a].set != source) {
			if (key_map[a].scan != scan && key_map[a].set != -2) {
				DEBUGPRINT("HID: altering keyboard mapping for \"%s\" from \"%s\" to \"%s\" at array index #%d" NL,
					emu_key_name, SDL_GetScancodeName(key_map[a].scan), host_key_name, a
				);
			}
			key_map[a].scan = scan;
			key_map[a].set = source;
			break;
		}
	}
	return NULL;
}


const char *hid_keymap_add_mapping_from_config_line ( const char *p, int *num_of_items )
{
	char emu_name[64], host_name[64];
	while (*p <= 0x20 && *p)
		p++;
	if (*p == '\0' || *p == '#')
		goto skip_rest;
	const char *emu_p = p;
	while (*p > 32)
		p++;
	if (*p == '\0' || *p == '#' || p - emu_p >= sizeof(emu_name))
		goto skip_rest;
	memcpy(emu_name, emu_p, p - emu_p);
	emu_name[p - emu_p] = 0;
	if (!strcmp(emu_name, CLEARALLMAPPING)) {
		for (int a = 0; key_map[a].pos >= 0; a++) {
			key_map[a].set = -2;
			key_map[a].scan = 0;
		}
		goto skip_rest;
	}
	while (*p <= 32 && *p)
		p++;
	if (*p == 0 || *p == '#')
		goto skip_rest;
	const char *host_p = p;
	while (*p && *p != 13 && *p != 10 && *p != '#')
		p++;
	//p--;
	while (p[-1] <= 0x20 && p > host_p)
		p--;
	if (p - host_p >= sizeof(host_name))
		goto skip_rest;
	memcpy(host_name, host_p, p - host_p);
	host_name[p - host_p] = 0;
	const char *res = hid_keymap_add_mapping(emu_name, host_name);
	if (res)
		DEBUGPRINT("HID: error on keymap user config: emu_name=[%s] host_key=<%s>: %s\n", emu_name, host_name, res);
	else
		(*num_of_items)++;
skip_rest:
	while (*p != 10 && *p != 13 && *p)
		p++;
	return p;
}


void hid_keymap_from_config_file ( const char *fn )
{
	char kbdcfg[8192];
	int a = xemu_load_file(fn, kbdcfg, 1, sizeof(kbdcfg) - 1, NULL);
	if (a == -1)
		DEBUGPRINT("HID: cannot open keymap user config file (maybe does not exist), ignoring: %s" NL, fn);
	else if (a < 1)
		DEBUGPRINT("HID: cannot read keymap user config file (maybe too large file), ignoring: %s" NL, fn);
	else {
		kbdcfg[a] = 0;
		const char *p = kbdcfg;
		for (a = 0; key_map[a].pos >= 0; a++) {
			key_map[a].set = -1;
			//key_map[a].scan = 0;
		}
		int num_of_items = 0;
		while (*p) {
			p = hid_keymap_add_mapping_from_config_line(p, &num_of_items);
		}
		DEBUGPRINT("HID: keymap configuration from file %s has been processed (%d successfull mappings)." NL, fn, num_of_items);
	}
}
#endif

void hid_init ( const struct KeyMappingDefault *key_map_in, Uint8 virtual_shift_pos_in, int joy_enable )
{
	int a;
#ifdef HID_KBD_MAP_CFG_SUPPORT
	char kbdcfg[8192];
	char *kp = kbdcfg + sprintf(kbdcfg,
		"# default settings for keyboard mapping" NL
		"# copy this file to filename '%s' in the same directory, and customize (this file is OVERWRITTEN every time, you must copy and customize that one!)" NL
		"# you can also use the -keymap option of the emulator to specify a keymap file to load (if the specific Xemu emulator supports, use -h to get help)" NL
		"# Syntax is: EMU-KEY-NAME PC-KEY-NAME" NL
		"# one assignment per line (EMU-KEY-NAME is always uppercase and one word, while PC-KEY-NAME is case/space/etc sensitive, must be put as is!)" NL
		"# EMU-KEY-NAME ends in '*' means that it's a virtual key, it is emulated by emulating pressed shift key at the same time" NL
		"# special line " CLEARALLMAPPING " can be put to clear all existing mappings, it make sense only as the first statement" NL
		"# without " CLEARALLMAPPING ", only maps are modified which are part of the keymap config file, the rest is left at their default state" NL
		"# PC-KEY-NAME Unknown means that the certain feature for the emulated keyboard is not mapped to a PC key" NL
		"# EMU-KEY-NAME strings starting with XEMU- are special Xemu related 'hot keys'" NL
		NL
		,
		KEYMAP_USER_FILENAME + 1
	);
#endif
	for (a = 0;;) {
		if (a >= 0x100)
			FATAL("Too long default keymapping table for hid_init()");
		key_map[a].pos = key_map_in[a].pos;
		key_map[a].scan = key_map_in[a].scan;
		key_map[a].set = 0;
		if (key_map[a].pos < 0) {
#ifdef HID_KBD_MAP_CFG_SUPPORT
			int fd = xemu_open_file(KEYMAP_DEFAULT_FILENAME, O_WRONLY|O_TRUNC|O_CREAT, NULL, NULL);
			if (fd >= 0) {
				xemu_safe_write(fd, kbdcfg, kp - kbdcfg);
				close(fd);
			}
#endif
			DEBUGPRINT("HID: %d key bindings has been added as the default built-in configuration" NL, a);
			break;
		}
#ifdef HID_KBD_MAP_CFG_SUPPORT
		register const char *scan_name = (key_map_in[a].scan == SDL_SCANCODE_UNKNOWN ? scan_name_unknown : SDL_GetScancodeName(key_map_in[a].scan));
		if (scan_name && *scan_name && key_map_in[a].name && key_map_in[a].name[0])
			kp += sprintf(kp, "%s %s" NL, key_map_in[a].name, scan_name);
		else
			DEBUGPRINT("HID: skipping keyboard entry on writing default map: scan <%s> name <%s>" NL, scan_name ? scan_name : "NULL", key_map_in[a].name ? key_map_in[a].name : "NULL");
#endif
		a++;
	}
	key_map_default = key_map_in;
	virtual_shift_pos = virtual_shift_pos_in;
	SDL_GameControllerEventState(SDL_DISABLE);
	SDL_JoystickEventState(joy_enable);
	hid_reset_events(0);
	for (int a = 0; a < MAX_JOYSTICKS; a++)
		joysticks[a] = NULL;
}



void hid_mouse_motion_event ( int xrel, int yrel )
{
	mouse_delta_x += xrel;
	mouse_delta_y += yrel;
	DEBUG("HID: mouse motion %d:%d, collected data is now %d:%d" NL, xrel, yrel, mouse_delta_x, mouse_delta_y);
}


void hid_mouse_button_event ( int button, int pressed )
{
	int mask;
	if (button == SDL_BUTTON_LEFT)
		mask = MOUSESTATE_BUTTON_LEFT;
	else if (button == SDL_BUTTON_RIGHT)
		mask = MOUSESTATE_BUTTON_RIGHT;
	else
		return;
	if (pressed)
		hid_state |= mask;
	else
		hid_state &= ~mask;
}


void hid_joystick_device_event ( int which , int is_attach )
{
	if (which >= MAX_JOYSTICKS)
		return;
	if (is_attach) {
		if (joysticks[which])
			hid_joystick_device_event(which, 0);
		joysticks[which] = SDL_JoystickOpen(which);
		if (joysticks[which]) {
			INFO_WINDOW("HID: joystick device #%d \"%s\" has been added." NL, which, SDL_JoystickName(joysticks[which]));
		} else
			DEBUG("HID: joystick device #%d problem, cannot be opened on 'add' event: %s." NL, which, SDL_GetError());
	} else {
		if (joysticks[which]) {
			SDL_JoystickClose(joysticks[which]);
			joysticks[which] = NULL;
			DEBUG("HID: joystick device #%d has been removed." NL, which);
			// This is needed to avoid "stuck" joystick state if removed in that state ...
			hid_state &= ~(JOYSTATE_UP | JOYSTATE_DOWN | JOYSTATE_LEFT | JOYSTATE_RIGHT | JOYSTATE_BUTTON);
		}
	}
}


void hid_joystick_motion_event ( int is_vertical, int value )
{
	if (is_vertical) {
		hid_state &= ~(JOYSTATE_UP | JOYSTATE_DOWN);
		if (value < -10000)
			hid_state |= JOYSTATE_UP;
		else if (value > 10000)
			hid_state |= JOYSTATE_DOWN;
	} else {
		hid_state &= ~(JOYSTATE_LEFT | JOYSTATE_RIGHT);
		if (value < -10000)
			hid_state |= JOYSTATE_LEFT;
		else if (value > 10000)
			hid_state |= JOYSTATE_RIGHT;
	}
}


void hid_joystick_button_event ( int pressed )
{
	if (pressed)
		hid_state |=  JOYSTATE_BUTTON;
	else
		hid_state &= ~JOYSTATE_BUTTON;
}


void hid_joystick_hat_event ( int value )
{
	hid_state &= ~(JOYSTATE_UP | JOYSTATE_DOWN | JOYSTATE_LEFT | JOYSTATE_RIGHT);
	if (value & SDL_HAT_UP)
		hid_state |= JOYSTATE_UP;
	if (value & SDL_HAT_DOWN)
		hid_state |= JOYSTATE_DOWN;
	if (value & SDL_HAT_LEFT)
		hid_state |= JOYSTATE_LEFT;
	if (value & SDL_HAT_RIGHT)
		hid_state |= JOYSTATE_RIGHT;
}


int hid_read_joystick_up ( int on, int off )
{
	return (hid_state & JOYSTATE_UP) ? on : off;
}


int hid_read_joystick_down ( int on, int off )
{
	return (hid_state & JOYSTATE_DOWN) ? on : off;
}


int hid_read_joystick_left ( int on, int off )
{
	return (hid_state & JOYSTATE_LEFT) ? on : off;
}


int hid_read_joystick_right ( int on, int off )
{
	return (hid_state & JOYSTATE_RIGHT) ? on : off;
}


int hid_read_joystick_button ( int on, int off )
{
	return (hid_state & JOYSTATE_BUTTON) ? on : off;
}


int hid_read_mouse_rel_x ( int min, int max )
{
	int result = mouse_delta_x;
	mouse_delta_x = 0;
	if (result < min)
		result = min;
	else if (result > max)
		result = max;
	return result;
}


int hid_read_mouse_rel_y ( int min, int max )
{
	int result = mouse_delta_y;
	mouse_delta_y = 0;
	if (result < min)
		result = min;
	else if (result > max)
		result = max;
	return result;
}


int hid_read_mouse_button_left ( int on, int off )
{
	return (hid_state & MOUSESTATE_BUTTON_LEFT) ? on : off;
}


int hid_read_mouse_button_right ( int on, int off )
{
	return (hid_state & MOUSESTATE_BUTTON_RIGHT) ? on : off;
}


int hid_handle_one_sdl_event ( SDL_Event *event )
{
	int handled = 1;
	switch (event->type) {
#ifdef CONFIG_DROPFILE_CALLBACK
		case SDL_DROPFILE:
			if (event->drop.file && event->drop.file[0]) {
				emu_dropfile_callback(event->drop.file);
				SDL_free(event->drop.file);
			}
			break;
#endif
		case SDL_QUIT:
			if (ARE_YOU_SURE(str_are_you_sure_to_exit, i_am_sure_override | ARE_YOU_SURE_DEFAULT_YES)) {
#ifdef CONFIG_QUIT_CALLBACK
				emu_quit_callback();
#endif
				exit(0);
			}
			break;
		case SDL_KEYUP:
		case SDL_KEYDOWN:
			if (
#ifndef CONFIG_KBD_ALSO_REPEATS
				event->key.repeat == 0 &&
#endif
				event->key.keysym.scancode != SDL_SCANCODE_UNKNOWN
#ifdef CONFIG_KBD_SELECT_FOCUS
				&& (event->key.windowID == sdl_winid || event->key.windowID == 0)
#endif
#ifdef CONFIG_KBD_AVOID_LALTTAB
				/* ALT-TAB is usually used by the OS, and it can be ignored to "leak" this event into Xemu
				 * so if it's requested, we filter out. NOTE: it's only for LALT (left ALT), right ALT maybe
				 * used	for other emulation purposes ... */
				&& !(event->key.keysym.scancode == SDL_SCANCODE_TAB && (event->key.keysym.mod & KMOD_LALT))
#endif
			) {
#ifdef CONFIG_KBD_ALSO_RAW_SDL_CALLBACK
				emu_callback_key_raw_sdl(&event->key);
#endif
				hid_key_event(event->key.keysym.scancode, event->key.state == SDL_PRESSED);
			}
			break;
		case SDL_JOYDEVICEADDED:
		case SDL_JOYDEVICEREMOVED:
			hid_joystick_device_event(event->jdevice.which, event->type == SDL_JOYDEVICEADDED);
			break;
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			hid_joystick_button_event(event->type == SDL_JOYBUTTONDOWN);
			break;
		case SDL_JOYHATMOTION:
			hid_joystick_hat_event(event->jhat.value);
			break;
		case SDL_JOYAXISMOTION:
			if (event->jaxis.axis < 2)
				hid_joystick_motion_event(event->jaxis.axis, event->jaxis.value);
			break;
		case SDL_MOUSEMOTION:
			if (is_mouse_grab())
				hid_mouse_motion_event(event->motion.xrel, event->motion.yrel);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			if (is_mouse_grab())
				hid_mouse_button_event(event->button.button, event->type == SDL_MOUSEBUTTONDOWN);
			else
				emu_callback_key(-2, 0, event->type == SDL_MOUSEBUTTONDOWN, event->button.button);
			break;
#ifdef CONFIG_KBD_ALSO_TEXTEDITING_SDL_CALLBACK
		case SDL_TEXTEDITING:
			emu_callback_key_texteditng_sdl(&event->edit);
			break;
#endif
#ifdef CONFIG_KBD_ALSO_TEXTINPUT_SDL_CALLBACK
		case SDL_TEXTINPUT:
			emu_callback_key_textinput_sdl(&event->text);
			break;
#endif
		default:
			handled = 0;
			break;
	}
	return handled;
}


// For simple emulators it's even enough to call regularly this function for all HID stuffs!
void hid_handle_all_sdl_events ( void )
{
	SDL_Event event;
	while (SDL_PollEvent(&event) != 0)
		hid_handle_one_sdl_event(&event);

}
