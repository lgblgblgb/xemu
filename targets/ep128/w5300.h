/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015,2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

#ifndef XEP128_W5300_H_INCLUDED
#define XEP128_W5300_H_INCLUDED
#ifdef CONFIG_W5300_SUPPORT

#define W5300_IO_BASE 0x90

extern int   w5300_does_work;

extern void  w5300_reset ( void );
extern void  w5300_init ( void (*cb)(int) );
extern void  w5300_uninit ( void );
extern void  w5300_write_mr0 ( Uint8 data );
extern void  w5300_write_mr1 ( Uint8 data );
extern void  w5300_write_idm_ar0 ( Uint8 data );
extern void  w5300_write_idm_ar1 ( Uint8 data );
extern void  w5300_write_idm_dr0 ( Uint8 data );
extern void  w5300_write_idm_dr1 ( Uint8 data );
extern Uint8 w5300_read_mr0 ( void );
extern Uint8 w5300_read_mr1 ( void );
extern Uint8 w5300_read_idm_ar0 ( void );
extern Uint8 w5300_read_idm_ar1 ( void );
extern Uint8 w5300_read_idm_dr0 ( void );
extern Uint8 w5300_read_idm_dr1 ( void );

#endif
#endif
