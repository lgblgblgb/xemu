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

#ifndef __XEP128_EXDOS_WD_H_INCLUDED
#define __XEP128_EXDOS_WD_H_INCLUDED

#ifdef CONFIG_EXDOS_SUPPORT

extern char  wd_img_path[PATH_MAX + 1];
extern int   wd_max_tracks, wd_max_sectors, wd_image_size;
extern Uint8 wd_sector,     wd_track;


extern Uint8 wd_read_status       ( void );
extern Uint8 wd_read_data         ( void );
extern Uint8 wd_read_exdos_status ( void );
extern void  wd_send_command      ( Uint8 value );
extern void  wd_write_data        ( Uint8 value );
extern void  wd_set_exdos_control ( Uint8 value );
extern void  wd_exdos_reset       ( void );
extern int   wd_attach_disk_image ( const char *fn );
extern void  wd_detach_disk_image ( void );

#endif
#endif
