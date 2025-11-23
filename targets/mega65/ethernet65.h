/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2018,2025 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

#ifndef XEMU_MEGA65_ETHERNET65_H_INCLUDED
#define XEMU_MEGA65_ETHERNET65_H_INCLUDED

extern Uint8 eth_rx_buf[0x800];
extern Uint8 eth_tx_buf[0x800];
#ifdef HAVE_ETHERTAP
extern char *eth65_options_used;
#endif

extern int   eth65_init			( const char *options );
#ifdef HAVE_ETHERTAP
extern unsigned int eth65_get_stat ( char *buf, const unsigned int buf_size, unsigned int *rxcnt, unsigned int *txcnt );
#endif
extern void  eth65_shutdown		( void );
extern void  eth65_reset		( void );
extern Uint8 eth65_read_reg		( const unsigned int addr );
extern void  eth65_write_reg		( const unsigned int addr, const Uint8 data );

#endif
