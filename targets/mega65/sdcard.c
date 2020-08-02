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

#include "xemu/emutools.h"
#include "xemu/emutools_files.h"
#include "sdcard.h"
#include "xemu/f011_core.h"
#include "xemu/d81access.h"
#include "mega65.h"
#include "xemu/cpu65.h"
#include "io_mapper.h"
#include "sdcontent.h"

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

#define COMPRESSED_SD
#define USE_KEEP_BUSY

#ifdef USE_KEEP_BUSY
#	define KEEP_BUSY(n)	keep_busy=n
#else
#	define KEEP_BUSY(n)
#endif

#define SD_ST_SDHC    0x10

static int	sdfd;			// SD-card controller emulation, UNIX file descriptor of the open image file
Uint8		sd_status;		// SD-status byte
static Uint8	sd_sector_registers[4];
static Uint8	sd_d81_img1_start[4];
static int	sdhc_mode = 1;
static Uint32	sdcard_size_in_blocks;	// SD card size in term of number of 512 byte blocks
static int	sdcard_bytes_read = 0;
static int	sd_fill_mode = 0;
static Uint8	sd_fill_value = 0;
#ifdef COMPRESSED_SD
static int	sd_compressed = 0;
static off_t	sd_bdata_start;
static int	compressed_block;
#endif
static int	sd_is_read_only;
int		fd_mounted;
static int	first_mount = 1;
#ifdef USE_KEEP_BUSY
static int	keep_busy = 0;
#endif
// 4K buffer space: Actually the SD buffer _IS_ inside this, also the F011 buffer should be (FIXME: that is not implemented yet right now!!)
Uint8		disk_buffers[0x1000];
static Uint8	sd_fill_buffer[512];	// Only used by the sd fill mode write command

static char	external_d81[PATH_MAX + 1];

const char	xemu_external_d81_signature[] = "\xFF\xFE<{[(XemuExternalDiskMagic)]}>";


#ifdef VIRTUAL_DISK_IMAGE_SUPPORT

#define VIRTUAL_DISK_BLOCKS_PER_CHUNK	2048
// Simulate a 4Gbyte card for virtual disk (the number is: number of blocks). Does not matter since only the actual written blocks stored in memory from it
#define VIRTUAL_DISK_SIZE_IN_BLOCKS	8388608U

#define RANGE_MAP_SIZE 256

struct virtdisk_chunk_st {
	struct	virtdisk_chunk_st *next;	// pointer to the next chunk, or NULL, if it was the last (in that case vdisk.tail should point to this chunk)
	int	used_blocks;			// number of used blocks in this chunk (up to vidsk.blocks_per_chunk)
	Uint32	block_no_min;			// optimization: lowest numbered block inside _this_ chunk of blocks
	Uint32	block_no_max;			// optimization: highest numbered block inside _this_ chunk of blocks
	Uint8	*base;				// base pointer of the data area on this chunk, 512 bytes per block then
	Uint32	list[];				// block number list for this chunk (see: used_blocks) [C99/C11 flexible array members syntax]
};
struct virtdisk_st {
	struct	virtdisk_chunk_st *head, *tail;	// pointers to the head and tail of the linked list of chunks, or NULL for both, if there is nothing yet
	int	blocks_per_chunk;		// number of blocks handled by a chunk
	int	allocation_size;		// pre-calculated to have number of bytes allocated for a chunk, including the data area + struct itself
	int	base_offset;			// pre-calculated to have the offset to the data area inside a chunk
	int	all_chunks;			// number of all currently allocated chunks
	int	all_blocks;			// number of all currently USED blocks within all chunks
	Uint8	range_map[RANGE_MAP_SIZE];	// optimization: store here if a given range is stored in memory, eliminating the need to search all chunks over, if not
	Uint32	range_map_divisor;		// optimization: for the above, to form range number from block number
	int	mode;				// what mode is used, 0=no virtual disk
};

static struct virtdisk_st vdisk = { .head = NULL };

static void virtdisk_destroy ( void )
{
	if (vdisk.head) {
		struct virtdisk_chunk_st *v = vdisk.head;
		int ranges = 0;
		for (unsigned int a = 0; a < RANGE_MAP_SIZE; a++)
			for (Uint8 m = 0x80; m; m >>= 1)
				if ((vdisk.range_map[a] & m))
					ranges++;
		DEBUGPRINT("SDCARD: VDISK: destroying %d chunks (active data: %d blocks, %dKbytes, %d%%, ranges: %d/%d) of storage." NL,
			vdisk.all_chunks, vdisk.all_blocks, vdisk.all_blocks >> 1,
			100 * vdisk.all_blocks / (vdisk.all_chunks * vdisk.blocks_per_chunk),
			ranges, RANGE_MAP_SIZE * 8
		);
		while (v) {
			struct virtdisk_chunk_st *next = v->next;
			free(v);
			v = next;
		}
	}
	vdisk.head = NULL;
	vdisk.tail = NULL;
	vdisk.all_chunks = 0;
	vdisk.all_blocks = 0;
	memset(vdisk.range_map, 0, RANGE_MAP_SIZE);
}

static void virtdisk_init ( int blocks_per_chunk, Uint32 total_number_of_blocks )
{
	virtdisk_destroy();
	vdisk.blocks_per_chunk = blocks_per_chunk;
	vdisk.base_offset = sizeof(struct virtdisk_chunk_st) + (sizeof(Uint32) * blocks_per_chunk);
	vdisk.allocation_size = (blocks_per_chunk << 9) + vdisk.base_offset;
	vdisk.range_map_divisor = (total_number_of_blocks / (RANGE_MAP_SIZE * 8)) + 1;
	DEBUGPRINT("SDCARD: VDISK: %d blocks (%dKbytes) per chunk, range-divisor is %u" NL, blocks_per_chunk, blocks_per_chunk >> 1, vdisk.range_map_divisor);
}

// Note: you must take care to call this function with "block" not outside of the desired capacity or some logic (range_map) will cause crash.
// That's not a problem, as this function is intended to use as storage backend, thus boundary checking should be done BEFORE you call this!
static Uint8 *virtdisk_search_block ( Uint32 block, int do_allocate )
{
	struct virtdisk_chunk_st *v;
	Uint32 range_index = block / vdisk.range_map_divisor;
	Uint8  range_mask  = 1U << (range_index & 7U);
	range_index >>= 3U;
	if (XEMU_LIKELY(vdisk.head && (vdisk.range_map[range_index] & range_mask))) {
		v = vdisk.head;
		do {
			if (block >= v->block_no_min && block <= v->block_no_max)
				for (unsigned int a = 0; a < v->used_blocks; a++)
					if (v->list[a] == block)
						return v->base + (a << 9);
			v = v->next;
		} while (v);
	}
	// if we can't found the block, and do_allocate is false, we return with zero
	// otherwise we continue to allocate a block
	if (!do_allocate)
		return NULL;
	//DEBUGPRINT("RANGE-INDEX=%d RANGE-MASK=%d" NL, range_index, range_mask);
	// We're instructed to allocate block if cannot be found already elsewhere
	// This condition checks if we have room in "tail" or there is any chunk already at all (tail is not NULL)
	if (vdisk.tail && (vdisk.tail->used_blocks < vdisk.blocks_per_chunk)) {
		v = vdisk.tail;
		// OK, we had room in the tail, so put the block there
		vdisk.all_blocks++;
		if (block < v->block_no_min)
			v->block_no_min = block;
		if (block > v->block_no_max)
			v->block_no_max = block;
		vdisk.range_map[range_index] |= range_mask;
		v->list[v->used_blocks] = block;
		return v->base + ((v->used_blocks++) << 9);
	}
	// we don't have room in the tail, or not any chunk yet AT ALL!
	// Let's allocate a new chunk, and use the first block from it!
	v = xemu_malloc(vdisk.allocation_size);	// xemu_malloc() is safe, it malloc()s space or abort the whole program if it cannot ...
	v->next = NULL;
	v->base = (Uint8*)v + vdisk.base_offset;
	vdisk.all_chunks++;
	if (vdisk.tail)
		vdisk.tail->next = v;
	vdisk.tail = v;
	if (!vdisk.head)
		vdisk.head = v;
	v->list[0] = block;
	v->used_blocks = 1;
	v->block_no_min = block;
	v->block_no_max = block;
	vdisk.range_map[range_index] |= range_mask;
	vdisk.all_blocks++;
	return v->base;	// no need to add block offset in storage, always the first block is served in a new chunk!
}

static inline void virtdisk_write_block ( Uint32 block, Uint8 *buffer )
{
	// Check if the block is all zero. If yes, we can omit write if the block is not cached
	Uint8 *vbuf = virtdisk_search_block(block, has_block_nonzero_byte(buffer));
	if (vbuf)
		memcpy(vbuf, buffer, 512);
	// TODO: Like with the next function, this whole strategy needs to be changed when mixed operation is used!!!!!
}

static inline void virtdisk_read_block ( Uint32 block, Uint8 *buffer )
{
	Uint8 *vbuf = virtdisk_search_block(block, 0);
	if (vbuf)
		memcpy(buffer, vbuf, 512);
	else
		memset(buffer, 0, 512);	// if not found, we fake an "all zero" answer (TODO: later in mixed operation, image+vdisk, this must be modified!)
}
#endif


// define the callback, d81access call this, we can dispatch the change in FDC config to the F011 core emulation this way, automatically
void d81access_cb_chgmode ( int mode ) {
	int have_disk = ((mode & 0xFF) != D81ACCESS_EMPTY);
	int can_write = (!(mode & D81ACCESS_RO));
	DEBUGPRINT("SDCARD: configuring F011 FDC with have_disk=%d, can_write=%d" NL, have_disk, can_write);
	fdc_set_disk(have_disk, can_write);
}
// Here we implement F011 core's callbacks using d81access (and yes, F011 uses 512 bytes long sectors for real)
int fdc_cb_rd_sec ( Uint8 *buffer, int d81_offset ) {
	int ret = d81access_read_sect(buffer, d81_offset, 512);
	DEBUG("SDCARD: D81: reading sector at d81_offset=%d, return value=%d" NL, d81_offset, ret);
	return ret;
}
int fdc_cb_wr_sec ( Uint8 *buffer, int d81_offset ) {
	int ret = d81access_write_sect(buffer, d81_offset, 512);
	DEBUG("SDCARD: D81: writing sector at d81_offset=%d, return value=%d" NL, d81_offset, ret);
	return ret;
}



static void sdcard_shutdown ( void )
{
	d81access_close();
	if (sdfd >= 0) {
		close(sdfd);
		sdfd = -1;
	}
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	virtdisk_destroy();
#endif
}


static void sdcard_set_external_d81_name ( const char *name )
{
	if (!name || !*name)
		*external_d81 = 0;
	else
		strncpy(external_d81, name, sizeof external_d81);
}


#ifdef COMPRESSED_SD
static int detect_compressed_image ( int fd )
{
	static const char compressed_marker[] = "XemuBlockCompressedImage000";
	Uint8 buf[512];
	if (lseek(sdfd, 0, SEEK_SET) == OFF_T_ERROR || xemu_safe_read(fd, buf, 512) != 512)
		return -1;
	if (memcmp(buf, compressed_marker, sizeof compressed_marker)) {
		DEBUGPRINT("SDCARD: image is not compressed" NL);
		return 0;
	}
	if (((buf[0x1C] << 16) | (buf[0x1D] << 8) | buf[0x1E]) != 3) {
		ERROR_WINDOW("Invalid/unknown compressed image format");
		return -1;
	}
	sdcard_size_in_blocks = (buf[0x1F] << 16) | (buf[0x20] << 8) | buf[0x21];
	DEBUGPRINT("SDCARD: compressed image with %u blocks" NL, sdcard_size_in_blocks);
	sd_bdata_start = 3 * sdcard_size_in_blocks + 0x22;
	sd_is_read_only = O_RDONLY;
	return 1;
}
#endif


Uint32 sdcard_get_size ( void )
{
	return sdcard_size_in_blocks;
}


int sdcard_init ( const char *fn, const char *extd81fn, int virtsd_flag )
{
	char fnbuf[PATH_MAX + 1];
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	if (virtsd_flag) {
		virtdisk_init(VIRTUAL_DISK_BLOCKS_PER_CHUNK, VIRTUAL_DISK_SIZE_IN_BLOCKS);
		vdisk.mode = 1;
	} else
		vdisk.mode = 0;
#endif
	sdcard_set_external_d81_name(extd81fn);
	d81access_init();
	atexit(sdcard_shutdown);
	KEEP_BUSY(0);
	sd_status = 0;
	fd_mounted = 0;
	memset(sd_sector_registers, 0, sizeof sd_sector_registers);
	memset(sd_d81_img1_start, 0, sizeof sd_d81_img1_start);
	memset(sd_fill_buffer, sd_fill_value, 512);
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	if (vdisk.mode) {
		sdfd = -1;
		sd_is_read_only = 0;
		sdcard_size_in_blocks = VIRTUAL_DISK_SIZE_IN_BLOCKS;
#ifdef COMPRESSED_SD
		sd_compressed = 0;
#endif
		DEBUGPRINT("SDCARD: card init done (VDISK!), size=%u Mbytes, virtsd_flag=%d" NL, sdcard_size_in_blocks >> 11, virtsd_flag);
#ifdef SD_CONTENT_SUPPORT
		sdcontent_handle(sdcard_size_in_blocks, fn, SDCONTENT_FORCE_FDISK);
#endif
		return 0;
	}
#endif
retry:
	sd_is_read_only = O_RDONLY;
	sdfd = xemu_open_file(fn, O_RDWR, &sd_is_read_only, fnbuf);
	if (sdfd < 0) {
		int r = errno;
		ERROR_WINDOW("Cannot open SD-card image %s, SD-card access won't work! ERROR: %s", fnbuf, strerror(r));
		DEBUG("SDCARD: cannot open image %s" NL, fn);
		if (r == ENOENT && !strcmp(fn, SDCARD_NAME)) {
			r = QUESTION_WINDOW(
				"No, thank you, I give up :(|Yes, create it for me :)"
				,
				"Default SDCARD image does not exist. Would you like me to create one for you?\n"
				"Note: it will be a 4Gbytes long file, since this is the minimal size for an SDHC card,\n"
				"what MEGA65 needs. Do not worry, it's a 'sparse' file on most modern OSes which does\n"
				"not takes as much disk space as its displayed size suggests.\n"
				"This is unavoidable to emulate something uses an SDHC-card."
			);
			if (r) {
				r = xemu_create_sparse_file(fnbuf, 4294967296UL);
				if (r) {
					ERROR_WINDOW("Couldn't create: %s", strerror(r));
				} else {
					goto retry;
				}
			}
		}
	} else {
		if (sd_is_read_only)
			INFO_WINDOW("Image file %s could be open only in R/O mode", fnbuf);
		else
			DEBUG("SDCARD: image file re-opened in RD/WR mode, good" NL);
		// Check size!
		DEBUG("SDCARD: cool, SD-card image %s (as %s) is open" NL, fn, fnbuf);
		off_t size_in_bytes = xemu_safe_file_size_by_fd(sdfd);
		if (size_in_bytes == OFF_T_ERROR) {
			ERROR_WINDOW("Cannot query the size of the SD-card image %s, SD-card access won't work! ERROR: %s", fn, strerror(errno));
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
#ifdef COMPRESSED_SD
		sd_compressed = detect_compressed_image(sdfd);
		if (sd_compressed < 0) {
			ERROR_WINDOW("Error while trying to detect compressed SD-image");
			sdcard_size_in_blocks = 0; // just cheating to trigger error handling later
		}
#endif
		sdcard_size_in_blocks = size_in_bytes >> 9;
		DEBUG("SDCARD: detected size in Mbytes: %d" NL, (int)(size_in_bytes >> 20));
		if (size_in_bytes < 67108864UL) {
			ERROR_WINDOW("SD-card image is too small! Min required size is 64Mbytes!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
		if (size_in_bytes & (off_t)511) {
			ERROR_WINDOW("SD-card image size is not multiple of 512 bytes!!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
		if (size_in_bytes > 34359738368UL) {
			ERROR_WINDOW("SD-card image is too large! Max allowed size is 32Gbytes!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
	}
	if (sdfd >= 0) {
		DEBUGPRINT("SDCARD: card init done, size=%u Mbytes, virtsd_flag=%d" NL, sdcard_size_in_blocks >> 11, virtsd_flag);
		//sdcontent_handle(sdcard_size_in_blocks, NULL, SDCONTENT_ASK_FDISK | SDCONTENT_ASK_FILES);
	}
	return sdfd;
}


static XEMU_INLINE Uint32 U8A_TO_U32 ( Uint8 *a )
{
	return ((Uint32)a[0]) | ((Uint32)a[1] << 8) | ((Uint32)a[2] << 16) | ((Uint32)a[3] << 24);
}


static int host_seek ( Uint32 block )
{
	if (sdfd < 0)
		FATAL("host_seek is called with invalid sdfd!");	// FIXME: this check can go away, once we're sure it does not apply!
	off_t offset;
#ifdef COMPRESSED_SD
	if (sd_compressed) {
		offset = block * 3 + 0x22;
		if (lseek(sdfd, offset, SEEK_SET) != offset)
			FATAL("SDCARD: SEEK: compressed image host-OS seek failure: %s", strerror(errno));
		Uint8 buf[3];
		if (xemu_safe_read(sdfd, buf, 3) != 3)
			FATAL("SDCARD: SEEK: compressed image host-OK pre-read failure: %s", strerror(errno));
		compressed_block = (buf[0] & 0x80);
		buf[0] &= 0x7F;
		offset = ((off_t)((buf[0] << 16) | (buf[1] << 8) | buf[2]) << 9) + sd_bdata_start;
		//DEBUGPRINT("SD-COMP: got address: %d" NL, (int)offset);
	} else {
		offset = (off_t)block << 9;
	}
#else
	offset = (off_t)block << 9;
#endif
	if (lseek(sdfd, offset, SEEK_SET) != offset)
		FATAL("SDCARD: SEEK: image seek host-OS failure: %s", strerror(errno));
	return 0;
}



// This tries to emulate the behaviour, that at least another one status query
// is needed to BUSY flag to go away instead of with no time. DUNNO if it is needed at all.
static Uint8 sdcard_read_status ( void )
{
	Uint8 ret = sd_status;
	DEBUG("SDCARD: reading SD status $D680 result is $%02X PC=$%04X" NL, ret, cpu65.pc);
#ifdef USE_KEEP_BUSY
	if (!keep_busy)
		sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);
#endif
	//ret |= 0x10;	// FIXME? according to Paul, the old "SDHC" flag stuck to one always from now
	return ret;
}


// TODO: later we need to deal with buffer selection, whatever
static XEMU_INLINE Uint8 *get_buffer_memory ( int is_write )
{
	// Currently the only buffer available in Xemu is the SD buffer, UNLESS it's a write operation and "fill mode" is used
	// (sd_fill_buffer is just filled with a single byte value)
	return (is_write && sd_fill_mode) ? sd_fill_buffer : sd_buffer;
}


int sdcard_read_block ( Uint32 block, Uint8 *buffer )
{
	if (block >= sdcard_size_in_blocks) {
		DEBUGPRINT("SDCARD: SEEK: invalid block was requested to READ: block=%u (max_block=%u) @ PC=$%04X" NL, block, sdcard_size_in_blocks, cpu65.pc);
		return -1;
	}
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	if (vdisk.mode) {
		virtdisk_read_block(block, buffer);
		return 0;
	}
#endif
	if (host_seek(block))
		return -1;
	if (xemu_safe_read(sdfd, buffer, 512) == 512)
		return 0;
	else
		return -1;
}


int sdcard_write_block ( Uint32 block, Uint8 *buffer )
{
	if (block >= sdcard_size_in_blocks) {
		DEBUGPRINT("SDCARD: SEEK: invalid block was requested to WRITE: block=%u (max_block=%u) @ PC=$%04X" NL, block, sdcard_size_in_blocks, cpu65.pc);
		return -1;
	}
	if (sd_is_read_only)	// on compressed SD image, it's also set btw
		return -1;	// read-only SD-card
#ifdef VIRTUAL_DISK_IMAGE_SUPPORT
	if (vdisk.mode) {
		virtdisk_write_block(block, buffer);
		return 0;
	}
#endif
	if (host_seek(block))
		return -1;
	if (xemu_safe_write(sdfd, buffer, 512) == 512)
		return 0;
	else
		return -1;
}



/* Lots of TODO's here:
 * + study M65's quite complex error handling behaviour to really match ...
 * + with external D81 mounting: have a "fake D81" on the card, and redirect accesses to that, if someone if insane enough to try to access D81 at the SD-level too ...
 * + In general: SD emulation is "too fast" done in zero emulated CPU time, which can affect the emulation badly if an I/O-rich task is running on Xemu/M65
 * */
static void sdcard_block_io ( Uint32 block, int is_write )
{
	DEBUG("SDCARD: %s block #%u @ PC=$%04X" NL,
		is_write ? "writing" : "reading",
		block, cpu65.pc
	);
	if (XEMU_UNLIKELY(sd_status & SD_ST_EXT_BUS)) {
		DEBUGPRINT("SDCARD: bus #1 is empty" NL);
		// FIXME: what kind of error we should create here?????
		sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR | SD_ST_BUSY1 | SD_ST_BUSY0;
		KEEP_BUSY(1);
		return;
	}
	Uint8 *buffer = get_buffer_memory(is_write);
	int ret = is_write ? sdcard_write_block(block, buffer) : sdcard_read_block(block, buffer);
	if (ret || !sdhc_mode) {
		sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR; // | SD_ST_BUSY1 | SD_ST_BUSY0;
			sd_status |= SD_ST_BUSY1 | SD_ST_BUSY0;
			//KEEP_BUSY(1);
		sdcard_bytes_read = 0;
		return;
	}
	sd_status &= ~(SD_ST_ERROR | SD_ST_FSM_ERROR);
	sdcard_bytes_read = 512;
#if 0
	off_t offset = sd_sector;
	offset <<= 9;	// make byte offset from sector (always SDHC card!)
	int ret = host__seek(offset);
	if (XEMU_UNLIKELY(!ret && is_write && sd_is_read_only)) {
		ret = 1;	// write protected SD image?
	}
	if (XEMU_LIKELY(!ret)) {
		Uint8 *wp = get_buffer_memory(is_write);
		if (
#ifdef COMPRESSED_SD
			(is_write && compressed_block) ||
#endif
			(is_write ? xemu_safe_write(sdfd, wp, 512) : xemu_safe_read(sdfd, wp, 512)) != 512
		)
			ret = -1;
	}
	if (XEMU_UNLIKELY(ret < 0)) {
		sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR; // | SD_ST_BUSY1 | SD_ST_BUSY0;
			sd_status |= SD_ST_BUSY1 | SD_ST_BUSY0;
			//KEEP_BUSY(1);
		sdcard_bytes_read = 0;
		return;
	}
	sd_status &= ~(SD_ST_ERROR | SD_ST_FSM_ERROR);
	sdcard_bytes_read = 512;
#endif
}


static void sdcard_command ( Uint8 cmd )
{
	static Uint32 multi_io_block;
	static Uint8 sd_last_ok_cmd;
	DEBUG("SDCARD: writing command register $D680 with $%02X PC=$%04X" NL, cmd, cpu65.pc);
	sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);	// ugly hack :-@
	KEEP_BUSY(0);
	switch (cmd) {
		case 0x00:	// RESET SD-card
		case 0x10:	// RESET SD-card with flags specified [FIXME: I don't know what the difference is ...]
			sd_status = SD_ST_RESET | (sd_status & SD_ST_EXT_BUS);	// clear all other flags, but not the bus selection, FIXME: bus selection should not be touched?
			memset(sd_sector_registers, 0, sizeof sd_sector_registers);
			sdhc_mode = 1;
			break;
		case 0x01:	// END RESET
		case 0x11:	// ... [FIXME: again, I don't know what the difference is ...]
			sd_status &= ~(SD_ST_RESET | SD_ST_ERROR | SD_ST_FSM_ERROR);
			break;
		case 0x02:	// read block
			sdcard_block_io(U8A_TO_U32(sd_sector_registers), 0);
			break;
		case 0x03:	// write block
			sdcard_block_io(U8A_TO_U32(sd_sector_registers), 1);
			break;
		case 0x04:	// multi sector write - first sector
			if (sd_last_ok_cmd != 0x04) {
				multi_io_block = U8A_TO_U32(sd_sector_registers);
				sdcard_block_io(multi_io_block, 1);
			} else {
				DEBUGPRINT("SDCARD: bad multi-command sequence command $%02X after command $%02X" NL, cmd, sd_last_ok_cmd);
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR;
			}
			break;
		case 0x05:	// multi sector write - not the first, neither the last sector
			if (sd_last_ok_cmd == 0x04 || sd_last_ok_cmd == 0x05) {
				multi_io_block++;
				sdcard_block_io(multi_io_block, 1);
			} else {
				DEBUGPRINT("SDCARD: bad multi-command sequence command $%02X after command $%02X" NL, cmd, sd_last_ok_cmd);
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR;
			}
			break;
		case 0x06:	// multi sector write - last sector
			if (sd_last_ok_cmd == 0x04 || sd_last_ok_cmd == 0x05) {
				multi_io_block++;
				sdcard_block_io(multi_io_block, 1);
			} else {
				DEBUGPRINT("SDCARD: bad multi-command sequence command $%02X after command $%02X" NL, cmd, sd_last_ok_cmd);
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR;
			}
			break;
		case 0x0C:	// request flush of the SD-card [currently does nothing in Xemu ...]
			break;
		case 0x40:	// SDHC mode OFF - Not supported on newer M65s!
			sd_status &= ~SD_ST_SDHC;
			sdhc_mode = 0;
			break;
		case 0x41:	// SDHC mode ON - Not supported on newer M65s!
			sd_status |= SD_ST_SDHC;
			sdhc_mode = 1;
			break;
		case 0x44:	// sd_clear_error <= '0'	FIXME: what is this?
			break;
		case 0x45:	// sd_clear_error <= '1'	FIXME: what is this?
			break;
		case 0x81:	// map SD-buffer
			sd_status |= SD_ST_MAPPED;
			sd_status &= ~(SD_ST_ERROR | SD_ST_FSM_ERROR);
			break;
		case 0x82:	// unmap SD-buffer
			sd_status &= ~(SD_ST_MAPPED | SD_ST_ERROR | SD_ST_FSM_ERROR);
			break;
		case 0x83:	// SD write fill mode set
			sd_fill_mode = 1;
			break;
		case 0x84:	// SD write fill mode clear
			sd_fill_mode = 0;
			break;
		case 0xC0:	// select internal SD-card bus
			sd_status &= ~SD_ST_EXT_BUS;
			break;
		case 0xC1:	// select external SD-card bus
			sd_status |= SD_ST_EXT_BUS;
			break;
		default:
			sd_status |= SD_ST_ERROR;
			DEBUGPRINT("SDCARD: warning, unimplemented SD-card controller command $%02X" NL, cmd);
			break;
	}
	if (XEMU_UNLIKELY(sd_status & (SD_ST_ERROR | SD_ST_FSM_ERROR))) {
		sd_last_ok_cmd = 0xFF;
	} else {
		sd_last_ok_cmd = cmd;
	}
}


// Note: off_t for "block" is requirement of the FDC core framework, not so much sdcard.c, where it's used as Uint32
static int on_sd_fdc_read_block_cb ( void *buffer, off_t offset, int sector_size )
{
	if (XEMU_UNLIKELY(sector_size != 512))
		FATAL("Invalid sector size in fdc read CB: %d" NL, sector_size);
	if (XEMU_UNLIKELY(offset & 511))
		FATAL("Invalid offset in fdc read CB" NL);
	return sdcard_read_block((Uint32)(offset >> 9), buffer);
}

static int on_sd_fdc_write_block_cb ( void *buffer, off_t offset, int sector_size )
{
	if (XEMU_UNLIKELY(sector_size != 512))
		FATAL("Invalid sector size in fdc write CB: %d" NL, sector_size);
	if (XEMU_UNLIKELY(offset & 511))
		FATAL("Invalid offset in fdc read CB" NL);
	return sdcard_write_block((Uint32)(offset >> 9), buffer);
}


int mount_external_d81 ( const char *name, int force_ro )
{
	// Let fsobj func guess the "name" being image, a program file, or an FS directory
	// In addition, pass AUTOCLOSE parameter, as it will be managed by d81access subsys, not sdcard level!
	// This is the opposite situation compared to mount_internal_d81() where an sdcard.c managed FD is passed only.
	int ret = d81access_attach_fsobj(name, D81ACCESS_IMG | D81ACCESS_PRG | D81ACCESS_DIR | D81ACCESS_AUTOCLOSE | (force_ro ? D81ACCESS_RO : 0));
	if (!ret)
		fd_mounted = 1;
	else
		DEBUGPRINT("SDCARD: D81: couldn't mount external D81 image" NL);
	return ret;
}


static int mount_internal_d81 ( int force_ro )
{
	int block = U8A_TO_U32(sd_d81_img1_start);
	if (XEMU_UNLIKELY(block + (D81_SIZE >> 9) >= sdcard_size_in_blocks)) {
		DEBUGPRINT("SDCARD: D81: image is outside of the SD-card boundaries! Refusing to mount." NL);
		return -1;
	}
	// TODO: later, we can drop in a logic here to read SD-card image at this position for a "signature" of "fake-Xemu-D81" image,
	//       which can be used in the future to trigger external mount with native-M65 in-emulator tools, instead of emulator controls externally (like -8 option).
	// Do not use D81ACCESS_AUTOCLOSE here! It would cause to close the sdfd by d81access on umount, thus even our SD card image is closed!
	// Also, let's inherit the possible read-only status of our SD image, of course.
	d81access_attach_cb((off_t)block << 9, on_sd_fdc_read_block_cb, (sd_is_read_only || force_ro) ? NULL : on_sd_fdc_write_block_cb);
	return 0;
}


// data = D68B write
static void sdcard_mount_d81 ( Uint8 data )
{
	DEBUGPRINT("SDCARD: D81: mount register request @ $D68B val=$%02X at PC=$%04X" NL, data, cpu65.pc);
	if ((data & 3) == 3) {
		int use_d81;
		fd_mounted = 0;
		if (*external_d81) {	// request for external mounting
			if (first_mount) {
				first_mount = 0;
				use_d81 = 1;
			} else
				use_d81 = QUESTION_WINDOW("Use D81 from SD-card|Use external D81 image/prg file", "Hypervisor mount request, and you have defined external D81 image.");
		} else
			use_d81 = 0;
		if (!use_d81) {
			// fdc_set_disk(1, sd_is_read_only ? 0 : QUESTION_WINDOW("Use read-only access|Use R/W access (can be dangerous, can corrupt the image!)", "Hypervisor seems to be about mounting a D81 image. You can override the access mode now."));
			DEBUGPRINT("SDCARD: D81: (re-?)mounting D81 for starting sector $%02X%02X%02X%02X on the SD-card" NL,
				sd_d81_img1_start[3], sd_d81_img1_start[2], sd_d81_img1_start[1], sd_d81_img1_start[0]
			);
			//mount_internal_d81(!QUESTION_WINDOW("Use read-only access|Use R/W access (can be dangerous, can corrupt the image!)", "Hypervisor seems to be about mounting a D81 image. You can override the access mode now."));
			fd_mounted = !mount_internal_d81(0);
		} else {
			//fdc_set_disk(1, !d81_is_read_only);
			DEBUGPRINT("SDCARD: D81: mounting *EXTERNAL* D81 image, not from SD card (emulator feature only)!" NL);
			if (mount_external_d81(external_d81, 0)) {
				ERROR_WINDOW("Cannot mount external D81 (see previous error), mounting the internal D81");
				fd_mounted = !mount_internal_d81(0);
			} else
				fd_mounted = 1;
		}
		DEBUGPRINT("SDCARD: D81: mounting %s" NL, fd_mounted ? "OK" : "*FAILED*");
	} else {
		if (fd_mounted)
			DEBUGPRINT("SDCARD: D81: unmounting." NL);
		//fdc_set_disk(0, 0);
		d81access_close();
		fd_mounted = 0;
	}
}




void sdcard_write_register ( int reg, Uint8 data )
{
	D6XX_registers[reg + 0x80] = data;
	switch (reg) {
		case 0:		// command/status register
			sdcard_command(data);
			break;
		case 1:		// sector address
		case 2:		// sector address
		case 3:		// sector address
		case 4:		// sector address
			sd_sector_registers[reg - 1] = data;
			DEBUG("SDCARD: writing sector number register $%04X with $%02X PC=$%04X" NL, reg + 0xD680, data, cpu65.pc);
			break;
		case 6:		// write-only register: byte value for SD-fill mode on SD write command
			sd_fill_value = data;
			if (sd_fill_value != sd_fill_buffer[0])
				memset(sd_fill_buffer, sd_fill_value, 512);
			break;
		// FIXME: bit7 of reg9 is buffer select?! WHAT is THAT?! [f011sd_buffer_select]  btw, bit2 seems to be some "handshake" stuff ...
		case 0xB:
			sdcard_mount_d81(data);
			break;
		case 0xC:
		case 0xD:
		case 0xE:
		case 0xF:
			sd_d81_img1_start[reg - 0xC] = data;
			DEBUG("SDCARD: writing D81 #1 sector register $%04X with $%02X PC=$%04X" NL, reg + 0xD680, data, cpu65.pc);
			break;
		default:
			DEBUGPRINT("SDCARD: unimplemented register: $%02X tried to be written with data $%02X" NL, reg, data);
			break;
	}
}



Uint8 sdcard_read_register ( int reg )
{
	Uint8 data;
	switch (reg) {
		case 0:
			data = sdcard_read_status();
			break;
		case 1:
		case 2:
		case 3:
		case 4:
			data = sd_sector_registers[reg - 1];
			break;
		case 8:	// SDcard read bytes low byte
			data = sdcard_bytes_read & 0xFF;
			break;
		case 9:	// SDcard read bytes hi byte
			data = sdcard_bytes_read >> 8;
			break;
		default:
			data = D6XX_registers[reg + 0x80];
			DEBUGPRINT("SDCARD: unimplemented register: $%02X tried to be read, defaulting to the back storage with data $%02X" NL, reg, data);
			break;
	}
	return data;
}


/* --- SNAPSHOT RELATED --- */


#ifdef XEMU_SNAPSHOT_SUPPORT

#include <string.h>

#define SNAPSHOT_SDCARD_BLOCK_VERSION	0
#define SNAPSHOT_SDCARD_BLOCK_SIZE	(0x100 + sizeof(disk_buffers))

int sdcard_snapshot_load_state ( const struct xemu_snapshot_definition_st *def, struct xemu_snapshot_block_st *block )
{
	Uint8 buffer[SNAPSHOT_SDCARD_BLOCK_SIZE];
	int a;
	if (block->block_version != SNAPSHOT_SDCARD_BLOCK_VERSION || block->sub_counter || block->sub_size != sizeof buffer)
		RETURN_XSNAPERR_USER("Bad SD-Card block syntax");
	a = xemusnap_read_file(buffer, sizeof buffer);
	if (a) return a;
	/* loading state ... */
	memcpy(sd_sector_registers, buffer, 4);
	memcpy(sd_d81_img1_start, buffer + 4, 4);
	fd_mounted = (int)P_AS_BE32(buffer + 8);
	sdcard_bytes_read = (int)P_AS_BE32(buffer + 12);
	sd_is_read_only = (int)P_AS_BE32(buffer + 16);
	//d81_is_read_only = (int)P_AS_BE32(buffer + 20);
	//use_d81 = (int)P_AS_BE32(buffer + 24);
	sd_status = buffer[0xFF];
	memcpy(disk_buffers, buffer + 0x100, sizeof disk_buffers);
	return 0;
}


int sdcard_snapshot_save_state ( const struct xemu_snapshot_definition_st *def )
{
	Uint8 buffer[SNAPSHOT_SDCARD_BLOCK_SIZE];
	int a = xemusnap_write_block_header(def->idstr, SNAPSHOT_SDCARD_BLOCK_VERSION);
	if (a) return a;
	memset(buffer, 0xFF, sizeof buffer);
	/* saving state ... */
	memcpy(buffer, sd_sector_registers, 4);
	memcpy(buffer + 4,sd_d81_img1_start, 4);
	U32_AS_BE(buffer + 8, fd_mounted);
	U32_AS_BE(buffer + 12, sdcard_bytes_read);
	U32_AS_BE(buffer + 16, sd_is_read_only);
	//U32_AS_BE(buffer + 20, d81_is_read_only);
	//U32_AS_BE(buffer + 24, use_d81);
	buffer[0xFF] = sd_status;
	memcpy(buffer + 0x100, disk_buffers, sizeof disk_buffers);
	return xemusnap_write_sub_block(buffer, sizeof buffer);
}

#endif
