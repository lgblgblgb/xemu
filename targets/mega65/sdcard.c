/* A work-in-progess Mega-65 (Commodore-65 clone origins) emulator
   Part of the Xemu project, please visit: https://github.com/lgblgblgb/xemu
   Copyright (C)2016-2019 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

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

static int	sdfd;			// SD-card controller emulation, UNIX file descriptor of the open image file
Uint8		sd_status;		// SD-status byte
static Uint8	sd_sector_bytes[4];
static Uint32	sd_sector;
static Uint8	sd_d81_img1_start[4];
static off_t	sd_card_size;
static int	sdcard_bytes_read = 0;
static int	sd_fill_mode = 0;
static Uint8	sd_fill_value = 0;
#ifdef COMPRESSED_SD
static int	sd_compressed = 0;
static off_t	sd_bdata_start;
static int	compressed_block;
#endif
static int	sd_is_read_only;
static int	is_sdhc_card;
int		fd_mounted;
static int	first_mount = 1;
#ifdef USE_KEEP_BUSY
static int	keep_busy = 0;
#endif
// 4K buffer space: Actually the SD buffer _IS_ inside this, also the F011 buffer should be (FIXME: that is not implemented yet right now!!)
Uint8		disk_buffers[0x1000];
static Uint8	sd_fill_buffer[512];	// Only used by the sd fill mode write command

static char	external_d81[PATH_MAX + 1];



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
	if (sdfd >= 0)
		close(sdfd);
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
	if (lseek(sdfd, 0, SEEK_SET) == (off_t)-1 || xemu_safe_read(fd, buf, 512) != 512)
		return -1;
	if (memcmp(buf, compressed_marker, sizeof compressed_marker)) {
		DEBUGPRINT("SDCARD: image is not compressed" NL);
		return 0;
	}
	if (((buf[0x1C] << 16) | (buf[0x1D] << 8) | buf[0x1E]) != 3) {
		ERROR_WINDOW("Invalid/unknown compressed image format");
		return -1;
	}
	sd_card_size = (buf[0x1F] << 16) | (buf[0x20] << 8) | buf[0x21];
	DEBUGPRINT("SDCARD: compressed image with %d blocks" NL, (int)sd_card_size);
	sd_bdata_start = 3 * sd_card_size + 0x22;
	sd_card_size <<= 9;	// convert to bytes
	sd_is_read_only = O_RDONLY;
	return 1;
}
#endif


int sdcard_init ( const char *fn, const char *extd81fn, int sdhc_flag )
{
	char fnbuf[PATH_MAX + 1];
	sdcard_set_external_d81_name(extd81fn);
	d81access_init();
	atexit(sdcard_shutdown);
	KEEP_BUSY(0);
	sd_status = 0;
	fd_mounted = 0;
	memset(sd_sector_bytes, 0, sizeof sd_sector_bytes);
	memset(sd_d81_img1_start, 0, sizeof sd_d81_img1_start);
	memset(sd_fill_buffer, sd_fill_value, 512);
retry:
	sd_is_read_only = O_RDONLY;
	sdfd = xemu_open_file(fn, O_RDWR, &sd_is_read_only, fnbuf);
	if (sdfd < 0) {
		int err = errno;
		ERROR_WINDOW("Cannot open SD-card image %s, SD-card access won't work! ERROR: %s", fnbuf, strerror(err));
		DEBUG("SDCARD: cannot open image %s" NL, fn);
		if (err == ENOENT && !strcmp(fn, SDCARD_NAME)) {
			unsigned int r = QUESTION_WINDOW("No|128M|256M|512M|1G|2G", "Default SDCARD image does not exist.\nWould you like me to create one for you?");
			if (r) {
				int r2 = xemu_create_empty_image(fnbuf, (1U << (r + 26U)));
				if (r2)
					ERROR_WINDOW("Couldn't create: %s", strerror(r2));
				else
					goto retry;
			}
		}
	} else {
		if (sd_is_read_only)
			INFO_WINDOW("Image file %s could be open only in R/O mode", fnbuf);
		else
			DEBUG("SDCARD: image file re-opened in RD/WR mode, good" NL);
		// Check size!
		DEBUG("SDCARD: cool, SD-card image %s (as %s) is open" NL, fn, fnbuf);
		sd_card_size = lseek(sdfd, 0, SEEK_END);
		if (sd_card_size == (off_t)-1) {
			ERROR_WINDOW("Cannot query the size of the SD-card image %s, SD-card access won't work! ERROR: %s", fn, strerror(errno));
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
#ifdef COMPRESSED_SD
		sd_compressed = detect_compressed_image(sdfd);
		if (sd_compressed < 0) {
			ERROR_WINDOW("Error while trying to detect compressed SD-image");
			sd_card_size = 0; // just cheating to trigger error handling later
		}
#endif
		DEBUG("SDCARD: detected size in Mbytes: %d" NL, (int)(sd_card_size >> 20));
		if (sd_card_size < 67108864) {
			ERROR_WINDOW("SD-card image is too small! Min required size is 64Mbytes!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
		if (sd_card_size & (off_t)511) {
			ERROR_WINDOW("SD-card image size is not multiple of 512 bytes!!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
		if (sd_card_size > 34359738368UL) {
			ERROR_WINDOW("SD-card image is too large! Max allowed size (in SDHC mode) is 32Gbytes!");
			close(sdfd);
			sdfd = -1;
			return sdfd;
		}
		if (sd_card_size > 2147483648UL) {
			if (!sdhc_flag) {
				INFO_WINDOW("SD card image is larger than 2Gbytes, but no SDHC mode requested.\nForcing SDHC mode now.");
				sdhc_flag = 1;
			} else
				DEBUGPRINT("SDCARD: using SDHC mode with 2-32Gbyte card, OK" NL);
		} else {
			if (sdhc_flag)
				DEBUGPRINT("SDCARD: WARNING: using SDHC mode for SD-card image smaller than 2Gbyte!" NL);
		}
	}
	is_sdhc_card = sdhc_flag;
#if 0
	if (sdhc_flag) {
		sd_status |= SD_ST_SDHC;
	}
#endif
	DEBUGPRINT("SDCARD: card init done, size=%d Mbytes, SDHC flag = %d" NL, (int)(sd_card_size >> 20), sdhc_flag);
	return sdfd;
}


static XEMU_INLINE Uint32 U8A_TO_U32 ( Uint8 *a )
{
	return ((Uint32)a[0]) | ((Uint32)a[1] << 8) | ((Uint32)a[2] << 16) | ((Uint32)a[3] << 24);
}


static int host_seek ( off_t offset )
{
	if (sdfd < 0)
		return -1;
	if (offset < 0 || offset > sd_card_size - 512) {
		DEBUGPRINT("SDCARD: SEEK: invalid offset requested: offset " PRINTF_LLD " PC=$%04X" NL, (long long)offset, cpu65.pc);
		return -1;
	}
#ifdef COMPRESSED_SD
	if (sd_compressed) {
		if (offset & 511)
			FATAL("Compressed SD got request for non-aligned access, it won't work, sorry ...");
		offset = (offset >> 9) * 3 + 0x22;
		if (lseek(sdfd, offset, SEEK_SET) != offset)
			FATAL("SDCARD: SEEK: compressed image host-OS seek failure: %s", strerror(errno));
		Uint8 buf[3];
		if (xemu_safe_read(sdfd, buf, 3) != 3)
			FATAL("SDCARD: SEEK: compressed image host-OK pre-read failure: %s", strerror(errno));
		compressed_block = (buf[0] & 0x80);
		buf[0] &= 0x7F;
		offset = ((off_t)((buf[0] << 16) | (buf[1] << 8) | buf[2]) << 9) + sd_bdata_start;
		//DEBUGPRINT("SD-COMP: got address: %d" NL, (int)offset);
	}
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
	//ret |= SD_ST_SDHC;	// ??? FIXME
	return ret;
}


// TODO: later we need to deal with buffer selection, whatever
static XEMU_INLINE Uint8 *get_buffer_memory ( int is_write )
{
	// Currently the only buffer available in Xemu is the SD buffer, UNLESS it's a write operation and "fill mode" is used
	// (sd_fill_buffer is just filled with a single byte value)
	return (is_write && sd_fill_mode) ? sd_fill_buffer : sd_buffer;
}



/* Lots of TODO's here:
 * + study M65's quite complex error handling behaviour to really match ...
 * + with external D81 mounting: have a "fake D81" on the card, and redirect accesses to that, if someone if insane enough to try to access D81 at the SD-level too ...
 * + In general: SD emulation is "too fast" done in zero emulated CPU time, which can affect the emulation badly if an I/O-rich task is running on Xemu/M65
 * */
static void sdcard_block_io ( int is_write )
{
	DEBUG("SDCARD: %s block #%d @ PC=$%04X" NL,
		is_write ? "writing" : "reading",
		sd_sector,
		cpu65.pc
	);
	if (XEMU_UNLIKELY(sdfd < 0))
		FATAL("sdcard_block_io(): no SD image open");
	if (XEMU_UNLIKELY(sd_status & SD_ST_EXT_BUS)) {
		DEBUGPRINT("SDCARD: bus #1 is empty" NL);
		// FIXME: what kind of error we should create here?????
		sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR | SD_ST_BUSY1 | SD_ST_BUSY0;
		KEEP_BUSY(1);
		return;
	}
#if 0
	if (XEMU_UNLIKELY(
		sd_multi_offset && (
			!sd_multi_mode ||
			(sd_multi_mode == 1 && !is_write) ||
			(sd_multi_mode == 2 && is_write)
		)
	)) {
		// trying to do multi-I/O without multi mode
		DEBUGPRINT("SDCARD: invalid use of multi-I/O command ofs=%d,mode=%d,is_write=%d" NL, sd_multi_offset, sd_multi_mode, is_write);
		sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR | SD_ST_BUSY1 | SD_ST_BUSY0;
		keep_busy = 1;
		return;
	}
#endif
	off_t offset = sd_sector;
	if (is_sdhc_card)
		offset <<= 9;
	if (XEMU_UNLIKELY(offset & 511UL)) {
		if (is_sdhc_card)
			FATAL("Unaligned access in sdcard_block_io() with SDHC card. This cannot happen!");
		sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR | SD_ST_BUSY1 | SD_ST_BUSY0;
		KEEP_BUSY(1);
		DEBUGPRINT("SDCARD: warning, unaligned %s access: offset = " PRINTF_LLD NL, is_write ? "write" : "read", (long long)offset);
		return;
	}
	int ret = host_seek(offset);
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
}


static void sdcard_command ( Uint8 cmd )
{
	static Uint8 sd_last_ok_cmd;
	DEBUG("SDCARD: writing command register $D680 with $%02X PC=$%04X" NL, cmd, cpu65.pc);
	sd_status &= ~(SD_ST_BUSY1 | SD_ST_BUSY0);	// ugly hack :-@
	KEEP_BUSY(0);
	switch (cmd) {
		case 0x00:	// RESET SD-card
		case 0x10:	// RESET SD-card with flags specified [FIXME: I don't know what the difference is ...]
			sd_status = SD_ST_RESET | (sd_status & SD_ST_EXT_BUS);	// clear all other flags, but not the bus selection, FIXME: bus selection should not be touched?
			memset(sd_sector_bytes, 0, sizeof sd_sector_bytes);
			break;
		case 0x01:	// END RESET
		case 0x11:	// ... [FIXME: again, I don't know what the difference is ...]
			sd_status &= ~(SD_ST_RESET | SD_ST_ERROR | SD_ST_FSM_ERROR);
			break;
		case 0x02:	// read block
			sd_sector = U8A_TO_U32(sd_sector_bytes);
			sdcard_block_io(0);
			break;
		case 0x03:	// write block
			sd_sector = U8A_TO_U32(sd_sector_bytes);
			sdcard_block_io(1);
			break;
		case 0x04:	// multi sector write - first sector
			if (sd_last_ok_cmd != 0x04) {
				sd_sector = U8A_TO_U32(sd_sector_bytes);
				sdcard_block_io(1);
			} else {
				DEBUGPRINT("SDCARD: bad multi-command sequence command $%02X after command $%02X" NL, cmd, sd_last_ok_cmd);
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR;
			}
			break;
		case 0x05:	// multi sector write - not the first, neither the last sector
			if (sd_last_ok_cmd == 0x04 || sd_last_ok_cmd == 0x05) {
				sd_sector++;
				sdcard_block_io(1);
			} else {
				DEBUGPRINT("SDCARD: bad multi-command sequence command $%02X after command $%02X" NL, cmd, sd_last_ok_cmd);
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR;
			}
			break;
		case 0x06:	// multi sector write - last sector
			if (sd_last_ok_cmd == 0x04 || sd_last_ok_cmd == 0x05) {
				sd_sector++;
				sdcard_block_io(1);
			} else {
				DEBUGPRINT("SDCARD: bad multi-command sequence command $%02X after command $%02X" NL, cmd, sd_last_ok_cmd);
				sd_status |= SD_ST_ERROR | SD_ST_FSM_ERROR;
			}
			break;
		case 0x0C:	// request flush of the SD-card [currently does nothing in Xemu ...]
			break;
		case 0x40:	// SDHC mode OFF
			sd_status &= ~SD_ST_SDHC;
			break;
		case 0x41:	// SDHC mode ON
			sd_status |= SD_ST_SDHC;
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


#ifdef COMPRESSED_SD
static int on_compressed_sd_d81_read_cb ( void *buffer, off_t offset, int sector_size )
{
	int ret = host_seek(offset);
	if (ret) {
		FATAL("Compressed SD seek error");
		return -1;
	}
	if (sector_size != 512) {
		FATAL("Compressed SD got non-512 sector size!");
		return -1;
	}
	ret = xemu_safe_read(sdfd, buffer, sector_size);
	if (ret != sector_size) {
		FATAL("Compressed SD read error");
		return -1;
	}
	//DEBUGPRINT("SDCARD: compressed-SD-D81: read ..." NL);
	return 0;
}
#endif



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
	off_t offset = U8A_TO_U32(sd_d81_img1_start);
	if (is_sdhc_card)
		offset <<= 9;
	if (offset > sd_card_size - (off_t)D81_SIZE) {
		DEBUGPRINT("SDCARD: D81: image is outside of the SD-card boundaries! Refusing to mount." NL);
		return -1;
	}
	// TODO: later, we can drop in a logic here to read SD-card image at this position for a "signature" of "fake-Xemu-D81" image,
	//       which can be used in the future to trigger external mount with native-M65 in-emulator tools, instead of emulator controls externally (like -8 option).
	// Do not use D81ACCESS_AUTOCLOSE here! It would cause to close the sdfd by d81access on umount, thus even our SD card image is closed!
	// Also, let's inherit the possible read-only status of our SD image, of course.
#ifdef COMPRESSED_SD
	if (sd_compressed) {
		d81access_attach_cb(offset, on_compressed_sd_d81_read_cb, NULL);	// we pass NULL as write callback to signal, that disk is R/O
	} else
#endif
		d81access_attach_fd(sdfd, offset, D81ACCESS_IMG | ((sd_is_read_only || force_ro) ? D81ACCESS_RO : 0));
	fd_mounted = 1;
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
			mount_internal_d81(0);
		} else {
			//fdc_set_disk(1, !d81_is_read_only);
			DEBUGPRINT("SDCARD: D81: mounting *EXTERNAL* D81 image, not from SD card (emulator feature only)!" NL);
			if (mount_external_d81(external_d81, 0)) {
				ERROR_WINDOW("Cannot mount external D81 (see previous error), mounting the internal D81");
				mount_internal_d81(0);
			}
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
			sd_sector_bytes[reg - 1] = data;
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
			data = sd_sector_bytes[reg - 1];
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
	memcpy(sd_sector_bytes, buffer, 4);
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
	memcpy(buffer, sd_sector_bytes, 4);
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
