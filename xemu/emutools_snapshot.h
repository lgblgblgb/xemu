/* Xemu - Somewhat lame emulation (running on Linux/Unix/Windows/OSX, utilizing
   SDL2) of some 8 bit machines, including the Commodore LCD and Commodore 65
   and MEGA65 as well.
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

#ifndef __XEMU_COMMON_EMUTOOLS_SNAPSHOT_H_INCLUDED
#define __XEMU_COMMON_EMUTOOLS_SNAPSHOT_H_INCLUDED

#ifdef XEMU_SNAPSHOT_SUPPORT

#define XEMUSNAP_MAX_IDENT_LENGTH	64
#define XEMUSNAP_ERROR_BUFFER_SIZE	256
#define XEMUSNAP_FIXED_HEADER_SIZE	21
#define XEMUSNAP_FRAMING_VERSION	0

#define XSNAPERR_NODATA		1
#define XSNAPERR_TRUNCATED	2
#define XSNAPERR_FORMAT		3
#define XSNAPERR_IO		4
#define XSNAPERR_CALLBACK	5

#define RETURN_XSNAPERR_USER(...) \
	do { \
		snprintf(xemusnap_user_error_buffer, XEMUSNAP_ERROR_BUFFER_SIZE, __VA_ARGS__); \
		return XSNAPERR_CALLBACK; \
	} while (0)

struct xemu_snapshot_block_st {
	Uint32	framing_version;
	Uint32	flags;
	Uint32	block_version;
	int	header_size;
	int	is_ident;
	int	idlen;
	char	idstr[XEMUSNAP_MAX_IDENT_LENGTH + 1];
	int	counter;
	int	sub_counter;
	Uint32	sub_size;
};

struct xemu_snapshot_definition_st;

typedef int (*xemu_snapshot_load_callback_t)( const struct xemu_snapshot_definition_st * , struct xemu_snapshot_block_st * );
typedef int (*xemu_snapshot_save_callback_t)( const struct xemu_snapshot_definition_st * );

struct xemu_snapshot_definition_st {
	const char *idstr;
	void *user_data;
	xemu_snapshot_load_callback_t load;
	xemu_snapshot_save_callback_t save;
};


extern char xemusnap_error_buffer[];
extern char xemusnap_user_error_buffer[];


static inline Uint64 P_AS_BE64 ( const Uint8 *p ) {
        return ((Uint64)p[0] << 56) | ((Uint64)p[1] << 48) | ((Uint64)p[2] << 40) | ((Uint64)p[3] << 32) | ((Uint64)p[4] << 24) | ((Uint64)p[5] << 16) | ((Uint64)p[6] << 8) | (Uint64)p[7];
}
static inline Uint32 P_AS_BE32 ( const Uint8 *p ) {
        return ((Uint32)p[0] << 24) | ((Uint32)p[1] << 16) | ((Uint32)p[2] <<  8) | ((Uint32)p[3]      ) ;
}
static inline Uint16 P_AS_BE16 ( const Uint8 *p ) {
        return ((Uint16)p[0] <<  8) | ((Uint16)p[1]      ) ;
}
static inline void   U64_AS_BE ( Uint8 *p, Uint64 n ) {
        p[0] = n >> 56; p[1] = n >> 48; p[2] = n >> 40; p[3] = n >> 32; p[4] = n >> 24; p[5] = n >> 16; p[6] = n >> 8; p[7] = n;
}
static inline void   U32_AS_BE ( Uint8 *p, Uint32 n ) {
        p[0] = n >> 24; p[1] = n >> 16; p[2] = n >>  8; p[3] = n;
}
static inline void   U16_AS_BE ( Uint8 *p, Uint16 n ) {
        p[0] = n >>  8; p[1] = n;
}

extern void xemusnap_init ( const struct xemu_snapshot_definition_st *def );
extern int  xemusnap_read_file ( void *buffer, size_t size );
extern int  xemusnap_skip_file_bytes ( off_t size );
extern int  xemusnap_write_file ( const void *buffer, size_t size );
extern int  xemusnap_read_block_header ( struct xemu_snapshot_block_st *block );
extern int  xemusnap_write_block_header ( const char *ident, Uint32 version );
extern int  xemusnap_read_be32 ( Uint32 *result );
extern int  xemusnap_skip_sub_blocks ( int num );
extern int  xemusnap_write_sub_block ( const Uint8 *buffer, Uint32 size );
extern int  xemusnap_load ( const char *filename );
extern int  xemusnap_save ( const char *filename );

#endif
#endif
