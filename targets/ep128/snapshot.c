/* Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2015-2016,2020 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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
#include "xemu/emutools_files.h"
#include "enterprise128.h"
#include "snapshot.h"
#include "cpu.h"
#include "roms.h"
#include <fcntl.h>



static const Uint8 ep128emu_snapshot_marker[] = {
	0x5D, 0x12, 0xE4, 0xF4, 0xC9, 0xDA, 0xB6, 0x42, 0x01, 0x33, 0xDE, 0x07, 0xD2, 0x34, 0xF2, 0x22
};

#define BLOCK_Z80	0
#define BLOCK_MEM	1
#define BLOCK_IO	2
#define BLOCK_DAVE	3
#define BLOCK_NICK	4
#define BLOCK_MACH	5
#define BLOCK_TYPES	6

#ifndef DISABLE_DEBUG
//                                                      <Z80>0      <MEM>1       <IO>2     <DAVE>3     <NICK>4  <MACHINE>5
static const char  *block_names[BLOCK_TYPES]    = {      "Z80",      "MEM",       "IO",     "DAVE",     "NICK",  "MACHINE" };
#endif
static const Uint32 block_types[BLOCK_TYPES]    = { 0x45508001, 0x45508002, 0x45508003, 0x45508004, 0x45508005, 0x45508009 };
static const int    block_versions[BLOCK_TYPES] = {  0x1000002,  0x1000000,  0x1000000,  0x1000000,  0x4000000,         -1 };

static int block_offsets[BLOCK_TYPES];
static int block_sizes[BLOCK_TYPES];
static Uint8 *snap = NULL;






static inline Uint32 getSnapDword ( const Uint8 *stream, int pos ) {
	return (stream[pos] << 24) | (stream[pos + 1] << 16) | (stream[pos + 2] << 8) | stream[pos + 3];
}


static inline Uint16 getSnapWord  ( const Uint8 *stream, int pos ) {
	return (stream[pos] << 8) | stream[pos + 1];
}


int ep128snap_load ( const char *fn )
{
	long snapsize;
	int pos;
	char pathbuffer[PATH_MAX + 1];
	int fd = xemu_open_file(fn, O_RDONLY, NULL, pathbuffer);
//	FILE *f = open_emu_file(fn, "rb", pathbuffer);
//	FIXME: move these from the f* functions ...
	if (fd < 0) {
		ERROR_WINDOW("Cannot open requestes snapshot file: %s", fn);
		return 1;
	}
	FILE *f = fdopen(fd, "rb");
	if (f == NULL) {
		ERROR_WINDOW("Cannot open requestes snapshot file: %s", fn);
		close(fd);
		return 1;
	}
	if (fseek(f, 0, SEEK_END)) {
		ERROR_WINDOW("Cannot seek snapshot file: %s", pathbuffer);
		goto error;
	}
	snapsize = ftell(f);
	DEBUGPRINT("SNAPSHOT: LOAD: %s (%ld bytes)" NL, fn, snapsize);
	if (snapsize < 16 || snapsize >= 0x500000) {
		ERROR_WINDOW("Too short or long snapshot file: %s", pathbuffer);
		goto error;
	}
	rewind(f);
	snap = malloc(snapsize);
	if (!snap) {
		ERROR_WINDOW("Not enough memory for loading snapshot");
		goto error;
	}
	if (fread(snap, snapsize, 1, f) != 1) {
		ERROR_WINDOW("Cannot read snapshot file");
		goto error;
	}
	fclose(f);
	f = NULL;
	if (memcmp(snap, ep128emu_snapshot_marker, sizeof ep128emu_snapshot_marker)) {
		ERROR_WINDOW("Invalid ep128emu snapshot (identifier not found)");
		goto error;
	}
	for (pos = 0; pos < BLOCK_TYPES; pos++)
		block_offsets[pos] = 0;
	pos = 16;
	for (;;) {
		Uint32 btype, bsize;
		int bindex;
		if (pos + 8 >= snapsize) {
			ERROR_WINDOW("Invalid snapshot file: truncated (during block header)");
			goto error;
		}
		btype = getSnapDword(snap, pos);
		if (!btype)
			break;
		bsize = getSnapDword(snap, pos + 4);
		pos += 8;
		if (pos + bsize + 4 >= snapsize) {
			ERROR_WINDOW("Invalid snapshot file: truncated (during block data)");
			goto error;
		}
		for (bindex = 0 ;; bindex++)
			if (bindex == BLOCK_TYPES) {
				ERROR_WINDOW("Invalid snapshot file: Unknown block type: %Xh", btype);
				goto error;
			} else if (btype == block_types[bindex])
				break;
		if (block_offsets[bindex]) {
			ERROR_WINDOW("Invalid snapshot file: duplicated block %Xh", block_types[bindex]);
			goto error;
		}
		if (block_versions[bindex] != -1) {
			Uint32 bver =  getSnapDword(snap, pos);
			if (bver != block_versions[bindex]) {
				ERROR_WINDOW("Invalid snapshot file: bad block version %Xh (should be %Xh) for block %Xh",
					bver, block_versions[bindex], block_types[bindex]
				);
				goto error;
			}
			pos += 4;
			bsize -= 4;
		}
		block_offsets[bindex] = pos;
		block_sizes[bindex] = bsize;
		DEBUG("SNAPSHOT: block #%d %Xh(%s) @%06Xh" NL, bindex, block_types[bindex], block_names[bindex], block_offsets[bindex]);
		pos += bsize + 4;
	}
	/* Check all needed blocks, if exists in the snapshot */
	for (pos = 0; pos < BLOCK_TYPES; pos++)
		if (block_versions[pos] != -1 && !block_offsets[pos]) {
			ERROR_WINDOW("Invalid snapshot file: missing block %Xh", block_types[pos]);
			goto error;
		}
	/* Populate memory */
	if ((block_sizes[BLOCK_MEM] - 4) % 0x4002) {
		ERROR_WINDOW("Invalid snapshot file: memory dump size is not multiple of 16Ks");
		goto error;
	}


	//printf("Memory = %Xh %f" NL, block_sizes[BLOCK_MEM], (block_sizes[BLOCK_MEM] - 4) / (float)0x4002);

	xep_rom_seg = -1;	// with snapshots, no XEP rom is possible :( [at least not with ep128emu snapshots too much ...]
	

	memset(memory, 0xFF, 0x400000);
	for (pos = 0; pos < 0x100; pos++) {
		rom_name_tab[pos] = NULL;
		memory_segment_map[pos] = (pos >= 0xFC) ? VRAM_SEGMENT : UNUSED_SEGMENT;
	}


	for (pos = block_offsets[BLOCK_MEM] + 4; pos < block_offsets[BLOCK_MEM] + block_sizes[BLOCK_MEM]; pos += 0x4002) {
		//printf("SEG %02X %02X" NL, snap[pos], snap[pos + 1]);
		if (snap[pos] < 0xFC)
			memory_segment_map[snap[pos]] = snap[pos + 1] ? ROM_SEGMENT : RAM_SEGMENT;
		//rom_name_tab[snap[pos]] = strdup(pathbuffer);
	}
	ep_init_ram();
	for (pos = block_offsets[BLOCK_MEM] + 4; pos < block_offsets[BLOCK_MEM] + block_sizes[BLOCK_MEM]; pos += 0x4002)
		memcpy(memory + (snap[pos] << 14), snap + pos + 2, 0x4000);
	return 0;
error:
	if (f)
		fclose(f);
	if (snap) {
		free(snap);
		snap = NULL;
	}
	return 1;
}



void ep128snap_set_cpu_and_io ( void )
{
	int pos;
	/* Z80 */
	pos = block_offsets[BLOCK_Z80];
        Z80_PC = getSnapWord(snap, pos);
	DEBUG("SNAPSHOT: Z80: PC is %04Xh" NL, Z80_PC);
        Z80_A = snap[pos + 2]; Z80_F = snap[pos + 3];
        Z80_B = snap[pos + 4]; Z80_C = snap[pos + 5];
        Z80_D = snap[pos + 6]; Z80_E = snap[pos + 7];
        Z80_H = snap[pos + 8]; Z80_L = snap[pos + 9];
        Z80_SP = getSnapWord(snap, pos + 10);
        Z80_IXH = snap[pos + 12]; Z80_IXL = snap[pos + 13];
        Z80_IYH = snap[pos + 14]; Z80_IYL = snap[pos + 15];
        Z80_A_ = snap[pos + 16]; Z80_F_ = snap[pos + 17];
        Z80_B_ = snap[pos + 18]; Z80_C_ = snap[pos + 19];
        Z80_D_ = snap[pos + 20]; Z80_E_ = snap[pos + 21];
        Z80_H_ = snap[pos + 22]; Z80_L_ = snap[pos + 23];
        Z80_I = snap[pos + 24];
        Z80_R = snap[pos + 25] & 127;
        // 26 27 28 29 -> internal reg
        Z80_IFF1 = snap[pos + 30] & 1;
        Z80_IFF2 = snap[pos + 31] & 1;
        Z80_R7 = snap[pos + 32] & 128;
        Z80_IM = snap[pos + 33];
	/* I/O ports */
	for (pos = 0; pos < 256; pos++)
		ports[pos] = snap[block_offsets[BLOCK_IO] + pos];
	for (pos = 0xA0; pos <= 0xBF; pos++)
		z80ex_pwrite_cb(pos, snap[block_offsets[BLOCK_IO] + pos]);
	for (pos = 0; pos < 4; pos++) {
		z80ex_pwrite_cb(0xB0 | pos, snap[block_offsets[BLOCK_MEM] + pos]);
		z80ex_pwrite_cb(0x80 | pos, snap[block_offsets[BLOCK_IO] + 0x80 + pos]);
	}
	z80ex_pwrite_cb(0x83, snap[block_offsets[BLOCK_IO] + 0x83] & 127 );
	z80ex_pwrite_cb(0x83, snap[block_offsets[BLOCK_IO] + 0x83] | 128 | 64);
	/* END */
	free(snap);
	snap = NULL;
}
