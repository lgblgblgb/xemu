/* A work-in-progess MEGA65 (Commodore 65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
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

#ifndef XEMU_MEGA65_FAT32_H_INCLUDED
#define XEMU_MEGA65_FAT32_H_INCLUDED

#define MFAT_FIND_VOL	1
#define MFAT_FIND_DIR	2
#define MFAT_FIND_FILE	4

#define IS_MFAT_DIR(p) 	(!!((p)&0x10))
#define IS_MFAT_VOL(p)	(!!((p)&0x08))
#define IS_MFAT_FILE(p)	(((p)&0x18)==0)

typedef int(*mfat_io_callback_func_t)(Uint32 block, Uint8 *data);

struct mfat_part_st {
	Uint32	first_block;
	Uint32	last_block;
	Uint32	blocks;
	//Uint32	blocks_per_cluster;
	int	part_type;
	int	valid;			// partition seems to be valid by part_type and size and layout requirements
	int	fs_validated;		// FAT32 is validated already, all data below is filled
	Uint32	clusters;		// total number of clusters (don't forget, the two first clusters cannot be used, but it counts here!)
	Uint32	data_area_fake_ofs;	// the 'fake' offset (taking account the cluster<2 invalidity) which must be added to the converted cluster->block value
	Uint32	cluster_size_in_blocks;	// cluster size in blocks
	Uint32	fat1_start, fat2_start;
	Uint32	root_dir_cluster;
	Uint32	fs_info_block_number;
	Uint32	eoc_marker;
};

typedef struct {
	Uint32	cluster;
	int	in_cluster_block;
	int	in_block_pos;
	int	eof;
	int	size_constraint;
	int	file_pos;
	struct mfat_part_st *partition;
	Uint32	start_cluster;
} mfat_stream_t;

typedef struct {
	mfat_stream_t stream;
	char	name[8+3+1+1];	// 8-char-base-name + 1-char-DOT + 3 char-extension + 1-end-of-c-string-marker
	char	fat_name[8+3+1];
	Uint32	cluster;
	Uint32	size;
	Uint8	type;
	time_t  time;
} mfat_dirent_t;

extern void   mfat_init     ( mfat_io_callback_func_t reader, mfat_io_callback_func_t writer, Uint32 device_size );
extern int    mfat_init_mbr ( void     );
extern int    mfat_use_part ( int part );

extern int    mfat_flush_fat_cache ( void );
extern int    mfat_normalize_name ( char *d, const char *s );
extern int    mfat_fatize_name    ( char *d, const char *s );
extern int    mfat_read_directory ( mfat_dirent_t *p, int type_filter );
extern int    mfat_search_in_directory ( mfat_dirent_t *p, const char *name, int type_filter );
extern void   mfat_open_rootdir ( mfat_stream_t *p );
extern Uint32 mfat_get_real_size ( mfat_stream_t *p, int *fragmented );
extern Uint32 mfat_overwrite_file_with_direct_linear_device_block_write ( mfat_dirent_t *dirent, const char *name, Uint32 size );

#endif
