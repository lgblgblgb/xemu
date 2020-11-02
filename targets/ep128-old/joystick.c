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
#include "joystick.h"
#include "input.h"
#include "dave.h"

#include <SDL.h>


#define MAX_JOYSTICKS	10
#define EPJOY_DISABLED	-2
#define EPJOY_KP	-1

//#define	USE_NEW_CFG


static struct {
	SDL_Joystick 	*joy;
	SDL_Haptic	*haptic;
	int		sdl_index;
	int		rumble;
	int		num_of_buttons;
	int		num_of_axes;
	int		num_of_axes_orig;	// before abstraction of hats as axes ...
	int		num_of_hats;
	Uint32		button;
	Uint32		axisp;
	Uint32		axisn;
	const char 	*name;
	SDL_JoystickGUID guid;
	char		guid_str[40];
	int		cfg_updated;		// used by configurator
} joysticks[MAX_JOYSTICKS];
static struct {
	int	id;	// Joystick ID (in joysticks array), use -1 for numeric keypad, or -2 for disable!, for vals < 0, all other options here ARE NOT used at all!
	Uint32	button_masks[3];
	Uint32	v_axis_mask, h_axis_mask;
	int	activated;			// used by configurator
} epjoys[3];	// emulation details for the two external joysticks of Enterprise (note: we have 0,1 on EP they're named as 1,2!) index 2 is for internal joystick emu ...
static int joy_to_init = 1;
static const char unknown_joystick_name[] = "Unknown joystick";


#define BIT_TO_SET(storage,mask) (storage) |= mask
#define BIT_TO_RESET(storage,mask) (storage) &= (4294967295U - mask)

#define AXIS_LIMIT_HIGH	20000
#define AXIS_LIMIT_LOW	10000


#ifdef USE_NEW_CFG
static const char joy_cfg_file_header[] =
	"# Joystick configuration file. Updated by Xep128 every time if new joystick is detected." NL
	"# You should not change anything, just write extra options after the * and # lines for" NL
	"# the given joystick or controller you would like to use." NL
	"# A line starting with '*' marks configuration for the given joystick/controller till" NL
	"# the next '* line or to the end of the file, if the last one. New models are added" NL
	"# automatically to this file, if Xep128 detects one." NL
	"# The syntax is quite strict, you should not use extra spaces or tabs." NL
	"# Basically you may want to add '1v=8' like options (one per line) to the given place." NL
	"# In the example, the first number ('1') means the EP joystick number (1 or 2)," NL
	"# 'v' or 'h' to select axis number as v(ertical) or h(orizonal) control." NL
	"# The other options are 'a', 'b', 'c' meaning (max of three) buttons (however most EP" NL
	"# softwares use only one fire ('a'). Here, the number after the '=' sign is button" NL
	"# number on your joystick/controller to assign (not axis number as with 'v' and 'h')." NL
	"# Uppercase versions of 'v', 'h', 'a', 'b', 'c' are for selecting an alternative" NL
	"# controller if you have more than one attached, then the second one is tried to be" NL
	"# used (ie, with two X-Box controllers connected, you can have one for joy-1 and" NL
	"# one for joy-2, since the configuration marks per model, and you have two of the" NL
	"# same models of controller in this case). However, if only one controller of the" NL
	"# type is detected, upper cased options treated as lower case, possible overwriting" NL
	"# the functionality! Confusing? Indeed. But it's hard to deal with hot-pluggable system" NL
	"# having options for possible unknown mappings, with the possibility to use two identical" NL
	"# models but also different ones, etc ... Also please note, that meaning of axis and button" NL
	"# numbers varies among operating systems and even with driver versions sometimes :-(" NL NL;

static const char joy_cfg_file_name[] = "@joysticks.cfg";
static const char joy_cfg_syntax_error[] = "Syntax error (line %d) in joystick configuration file:\n%s";
#endif



static void joy_clear_assignments ( void )
{
	int a;
	for (a = 0; a < 2; a++) {
		epjoys[a].id = EPJOY_KP;
		epjoys[a].button_masks[0] = epjoys[a].button_masks[1] = epjoys[a].button_masks[2] = 0;
		epjoys[a].v_axis_mask = epjoys[a].h_axis_mask = 0;
		epjoys[a].activated = 0;
	}
}



#ifdef USE_NEW_CFG
/* This function re-scans configuration file, and even updating it, if new device found by GUID
Emulation assignment has been re-set, and re-done according to the configuration file.
Should be called at new joystick device attached event, after joystick struct has been set properly. */
static void use_and_update_config_file ( void )
{
	char path[PATH_MAX + 1];
	int had_cfg, a;
	FILE *f = open_emu_file(joy_cfg_file_name, "r", path);
	// Reset assignments
	joy_clear_assignments();
	// Parse configuration file, if we have any ...
	if (f) {
		char line[256];
		int lineno = 0;
		int fnum = 0; // number of valid items in found[]
		int found[MAX_JOYSTICKS]; // joysticks indices found for the given controller GUID
		while (fgets(line, sizeof line, f)) {
			lineno++;
			if (line[0] == '*') {
				for (fnum = 0, a = 0; a < MAX_JOYSTICKS; a++)
					if (joysticks[a].joy && !strncmp(joysticks[a].guid_str, line + 1, strlen(joysticks[a].guid_str))) {
						joysticks[a].cfg_updated = 1;
						found[fnum++] = a;
					}
			} else if (line[0] == '+') {
				char *p = strchr(line, '=');
				if (p >= line + 3 && (line[1] == '1' || line[1] == '2')) {
					Uint32 param = atoi(p + 1) & 31; // parameter as integer after the '=' sign
					int jnum = line[1] - '1';
					if (!fnum)
						continue;
					epjoys[jnum].id = found[0];
					switch (line[2]) {
						case 'v':
							epjoys[jnum].v_axis_mask = 1U << param;
							break;
						case 'h':
							epjoys[jnum].h_axis_mask = 1U << param;
							break;
						case 'a':
						case 'b':
						case 'c':
							epjoys[jnum].button_masks[line[2] - 'a'] = 1U << param;
							break;
						default:
							ERROR_WINDOW(joy_cfg_syntax_error, lineno, path);
							break;
					}
				} else
					ERROR_WINDOW(joy_cfg_syntax_error, lineno, path);
			} else if (line[0] != '#' && line[0] > 32)
				ERROR_WINDOW(joy_cfg_syntax_error, lineno, path);
		}
		fclose(f);
		had_cfg = 1;
	} else
		had_cfg = 0;
	// Now, append joy cfg file with "new" joysticks not found in cfg file yet (or the cfg file has to be created ...)
	f = open_emu_file(joy_cfg_file_name, "a", path);
	if (f) {
		if (!had_cfg)
			fprintf(f, "%s", joy_cfg_file_header);
		for (a = 0; a < MAX_JOYSTICKS; a++)
			if (joysticks[a].joy && !joysticks[a].cfg_updated) {
				had_cfg = 2;
				fprintf(f,
					NL "*%s" NL
					"# Name: \"%s\"" NL
					"# Axes = %d, buttons = %d, hats = %d (mapped - if any - as two virtual axes from %d per hats)" NL,
					joysticks[a].guid_str, joysticks[a].name,
					joysticks[a].num_of_axes, joysticks[a].num_of_buttons, joysticks[a].num_of_hats,
					joysticks[a].num_of_axes_orig
				);
			}
		fclose(f);
		if (had_cfg == 2)
			INFO_WINDOW("Joystick configuration file has been created/appended with new joystick\n%s", path);
	} else
		ERROR_WINDOW("Cannot create/append joystick configuration file:\n%s", path);
	/* TODO: if we have joysticks found but no assignment is done, let's assign "something" at least :) */
}
#endif



/* virtual is zero for "real" axes,
   for hat->axis abstraction, axes will mean the hat number, and virtual must be 1 or 2 for the two
   "axes" of the hat */
static void set_axis_state ( int joy_id, Uint32 axis, int value, int virtual )
{
	Uint32 axis_mask, oldp, oldn;
	if (joy_id >= MAX_JOYSTICKS || !joysticks[joy_id].joy || axis >= (virtual ? joysticks[joy_id].num_of_axes : joysticks[joy_id].num_of_axes_orig))
		return;
	if (virtual)
		axis = joysticks[joy_id].num_of_axes_orig + axis * 2 + virtual - 1;
	axis_mask = 1U << axis;
	oldp = joysticks[joy_id].axisp;
	oldn = joysticks[joy_id].axisn;
	if (value > AXIS_LIMIT_HIGH) {
		BIT_TO_SET  (joysticks[joy_id].axisp, axis_mask);
		BIT_TO_RESET(joysticks[joy_id].axisn, axis_mask);
	} else if (value < AXIS_LIMIT_LOW && value > -AXIS_LIMIT_LOW) {
		BIT_TO_RESET(joysticks[joy_id].axisp, axis_mask);
		BIT_TO_RESET(joysticks[joy_id].axisn, axis_mask);
	} else if (value < -AXIS_LIMIT_HIGH) {
		BIT_TO_RESET(joysticks[joy_id].axisp, axis_mask);
		BIT_TO_SET  (joysticks[joy_id].axisn, axis_mask);
	}
	if (show_keys && (oldp != joysticks[joy_id].axisp || oldn != joysticks[joy_id].axisn))
		OSD("PC joystick #%d axis %d [%c %c]\nmask=%X value=%d virtual=%c",
			joy_id,
			axis,
			(joysticks[joy_id].axisn & axis_mask) ? '-' : ' ',
			(joysticks[joy_id].axisp & axis_mask) ? '+' : ' ',
			axis_mask,
			value,
			virtual ? 'Y' : 'N'
	);
}



static void set_button_state ( int joy_id, Uint32 button, int value )
{
	Uint32 button_mask, old;
	if (joy_id >= MAX_JOYSTICKS || !joysticks[joy_id].joy || button >= joysticks[joy_id].num_of_buttons)
		return;
	button_mask = 1U << button;
	old = joysticks[joy_id].button;
	if (value)
		BIT_TO_SET(joysticks[joy_id].button, button_mask);
	else
		BIT_TO_RESET(joysticks[joy_id].button, button_mask);
	if (show_keys && old != joysticks[joy_id].button)
		OSD("PC joystick #%d button %d\n%s", joy_id, button, value ? "pressed" : "released");
}



static void set_hat_state ( int joy_id, Uint32 hat, int value )
{
	if (joy_id >= MAX_JOYSTICKS || !joysticks[joy_id].joy || hat >= joysticks[joy_id].num_of_hats)
		return;
	// we emulate hats as two axes after placed the "normal axes", we signal set_axis_state() about this with the third parameter being non-zero
	if (value & SDL_HAT_UP)
		set_axis_state(joy_id, hat, -32000, 1);
	else if (value & SDL_HAT_DOWN)
		set_axis_state(joy_id, hat,  32000, 1);
	else
		set_axis_state(joy_id, hat,      0, 1);
	if (value & SDL_HAT_LEFT)
		set_axis_state(joy_id, hat, -32000, 2);
	else if (value & SDL_HAT_RIGHT)
		set_axis_state(joy_id, hat,  32000, 2);
	else
		set_axis_state(joy_id, hat,      0, 2);
}



static void joy_sync ( int joy_id )
{
	int a;
	if (joy_id >= MAX_JOYSTICKS || !joysticks[joy_id].joy)
		return;
	joysticks[joy_id].button = joysticks[joy_id].axisp = joysticks[joy_id].axisn = 0;
	for (a = 0; a < 32; a++) {
		if (a < joysticks[joy_id].num_of_axes_orig)
			set_axis_state(joy_id, a, SDL_JoystickGetAxis(joysticks[joy_id].joy, a), 0);
		if (a < joysticks[joy_id].num_of_buttons)
			set_button_state(joy_id, a, SDL_JoystickGetButton(joysticks[joy_id].joy, a));
		if (a < joysticks[joy_id].num_of_hats)
			set_hat_state(joy_id, a, SDL_JoystickGetHat(joysticks[joy_id].joy, a));
	}
}



static void joy_detach ( int joy_id )
{
	if (joy_id >= MAX_JOYSTICKS || !joysticks[joy_id].joy)
		return;
	DEBUG("JOY: device removed #%d" NL,
		joy_id
	);
	if (joysticks[joy_id].haptic)
		SDL_HapticClose(joysticks[joy_id].haptic);
	SDL_JoystickClose(joysticks[joy_id].joy);
	joysticks[joy_id].joy = NULL;
	joysticks[joy_id].haptic = NULL;
	OSD("Joy detached #%d", joy_id);
	joysticks[joy_id].button = joysticks[joy_id].axisp = joysticks[joy_id].axisn = 0;	// don't leave state in a "stucked" situation!
}



static void joy_rumble ( int joy_id, Uint32 len, int always )
{
	if (joy_id >= MAX_JOYSTICKS || !joysticks[joy_id].haptic || !joysticks[joy_id].rumble)
		return;
	if (always || joysticks[joy_id].rumble == 1) {
		SDL_HapticRumblePlay(joysticks[joy_id].haptic, 1.0, len);
		joysticks[joy_id].rumble = 2;
	}
}



static void joy_attach ( int joy_id )
{
	SDL_Joystick *joy;
	if (joy_id >= MAX_JOYSTICKS)
		return;
	if (joysticks[joy_id].joy)	// already attached joystick?
		return;
	joysticks[joy_id].joy = joy = SDL_JoystickOpen(joy_id);
	if (!joy)
		return;
	joysticks[joy_id].haptic = SDL_HapticOpenFromJoystick(joy);
	if (joysticks[joy_id].haptic)
		joysticks[joy_id].rumble = SDL_HapticRumbleInit(joysticks[joy_id].haptic) ? 0 : 1;
	else
		joysticks[joy_id].rumble = 0;	// rumble is not supported
	joysticks[joy_id].sdl_index = joy_id;
	joysticks[joy_id].num_of_buttons = SDL_JoystickNumButtons(joy);
	joysticks[joy_id].num_of_axes = SDL_JoystickNumAxes(joy);
	joysticks[joy_id].num_of_hats = SDL_JoystickNumHats(joy);
	joysticks[joy_id].name = SDL_JoystickName(joy);
	if (!joysticks[joy_id].name)
		joysticks[joy_id].name = unknown_joystick_name;
	joysticks[joy_id].guid = SDL_JoystickGetGUID(joy);
	SDL_JoystickGetGUIDString(
		joysticks[joy_id].guid,
		joysticks[joy_id].guid_str,
		40
	);
	if (joysticks[joy_id].num_of_buttons > 32)
		joysticks[joy_id].num_of_buttons = 32;
	if (joysticks[joy_id].num_of_axes > 32)
		joysticks[joy_id].num_of_axes = 32;
	if (joysticks[joy_id].num_of_hats > 32)
		joysticks[joy_id].num_of_hats = 32;
	if (joysticks[joy_id].num_of_axes + (joysticks[joy_id].num_of_hats * 2) > 32) {
		if (joysticks[joy_id].num_of_hats > 4)
			joysticks[joy_id].num_of_hats = 4;
	}
	if (joysticks[joy_id].num_of_axes + (joysticks[joy_id].num_of_hats * 2) > 32) {
		joysticks[joy_id].num_of_axes = 32 - (joysticks[joy_id].num_of_hats * 2);
	}
	joy_sync(joy_id);
	DEBUGPRINT("JOY: new device added #%d \"%s\" axes=%d buttons=%d (balls=%d) hats=%d guid=%s" NL,
		joy_id,
		joysticks[joy_id].name,
		joysticks[joy_id].num_of_axes,
		joysticks[joy_id].num_of_buttons,
		SDL_JoystickNumBalls(joy),
		joysticks[joy_id].num_of_hats,
		joysticks[joy_id].guid_str
	);
	OSD("Joy attached:\n#%d %s", joy_id, joysticks[joy_id].name);
	joysticks[joy_id].num_of_axes_orig = joysticks[joy_id].num_of_axes;
	joysticks[joy_id].num_of_axes += joysticks[joy_id].num_of_hats * 2;
#ifdef USE_NEW_CFG
	use_and_update_config_file();
#endif
}



void joy_sdl_event ( SDL_Event *e )
{
	if (joy_to_init) {
		int a;
		for (a = 0; a < MAX_JOYSTICKS; a++) {
			joysticks[a].joy    = NULL;
			joysticks[a].haptic = NULL;
			joysticks[a].button = joysticks[a].axisp = joysticks[a].axisn = 0;
			joysticks[a].cfg_updated = 0;
		}
		joy_clear_assignments();
#ifndef USE_NEW_CFG
		for (a = 0; a < 2; a++) {
			epjoys[a].id = 0;		// assigned to SDL joystick #0
			epjoys[a].button_masks[0] = 1;
			epjoys[a].button_masks[1] = 0;
			epjoys[a].button_masks[2] = 0;
			epjoys[a].v_axis_mask = 2;
			epjoys[a].h_axis_mask = 1;
		}
#endif
		SDL_GameControllerEventState(SDL_DISABLE);
		SDL_JoystickEventState(SDL_ENABLE);
		joy_to_init = 0;
	}
	if (e) switch (e->type) {
		case SDL_JOYAXISMOTION:	// joystick axis motion
			set_axis_state(e->jaxis.which, e->jaxis.axis, e->jaxis.value, 0);
			break;
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			set_button_state(e->jbutton.which, e->jbutton.button, e->jbutton.state);
			break;
		case SDL_JOYDEVICEADDED:
			joy_attach(e->jdevice.which);
			break;
		case SDL_JOYDEVICEREMOVED:
			joy_detach(e->jdevice.which);
			break;
		case SDL_JOYHATMOTION:
			set_hat_state(e->jhat.which, e->jhat.hat, e->jhat.value);
			break;
	}
}



/* The SCAN function called from input.c
   num is the Enterprise (!) external joystick number, 0 or 1 (beware, on EP it's called 1 or 2)
   dir is the direction / button to scan, see JOY_SCAN_* defines in the header file.
   The return value is '0' for inactive and '1' for active (well, or any non-zero value) for a given scan, warning, it's the
   opposite behaviour as you can see on EP HW level!
   DO NOT call this function with other num than 0, 1! (2 is reserved for internal joy emu, but it's not handled by this func!)
*/
int joystick_scan ( int num, int dir )
{
	if (num != 0 && num != 1)
		return 0;
	switch (epjoys[num].id) {
		case EPJOY_KP:
			return !(kbd_matrix[10] & (1 << dir));  // keyboard-matrix row #10 (not a real EP one!) is used to maintain status of numeric keypad joy emu keys ...
		case EPJOY_DISABLED:
			return 0;		// disabled, always zero answer
		default:
			// hack: give priority to the numeric keypad ;-)
			if (!(kbd_matrix[10] & (1 << dir)))
				return 1;
			joy_rumble(epjoys[num].id, 200, 0);
			switch (dir) {
				case JOY_SCAN_FIRE1:
					return joysticks[epjoys[num].id].button & epjoys[num].button_masks[0];
				case JOY_SCAN_UP:
					return joysticks[epjoys[num].id].axisn  & epjoys[num].v_axis_mask;
				case JOY_SCAN_DOWN:
					return joysticks[epjoys[num].id].axisp  & epjoys[num].v_axis_mask;
				case JOY_SCAN_LEFT:
					return joysticks[epjoys[num].id].axisn  & epjoys[num].h_axis_mask;
				case JOY_SCAN_RIGHT:
					return joysticks[epjoys[num].id].axisp  & epjoys[num].h_axis_mask;
				case JOY_SCAN_FIRE2:
					return joysticks[epjoys[num].id].button & epjoys[num].button_masks[1];
				case JOY_SCAN_FIRE3:
					return joysticks[epjoys[num].id].button & epjoys[num].button_masks[2];
			}
	}
	return 0;	// unknown?!
}
