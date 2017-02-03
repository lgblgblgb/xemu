/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

#ifndef __TVC_SDEXT_H_INCLUDED
#define __TVC_SDEXT_H_INCLUDED
#ifdef CONFIG_SDEXT_SUPPORT

#define SDCARD_IMG_FN		"sdcard.img"
#define SDCARD_ROM_FN		"tvc_sddos.rom"
#define SDCARD_ROM_SIZE		(40*1024)

extern void  sdext_init ( void );
extern Uint8 sdext_read_cart ( int addr );
extern void  sdext_write_cart ( int addr, Uint8 data );
extern void  sdext_clear_ram ( void );

extern int   sdext_cart_enabler;
extern char  sdimg_path[PATH_MAX + 1];
extern off_t sd_card_size;

#endif
#endif
