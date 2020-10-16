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

/* General SD information:
	http://elm-chan.org/docs/mmc/mmc_e.html
	http://www.mikroe.com/downloads/get/1624/microsd_card_spec.pdf
	http://users.ece.utexas.edu/~valvano/EE345M/SD_Physical_Layer_Spec.pdf
   Flash IC used (AM29F400BT) on the cartridge:
	http://www.mouser.com/ds/2/380/spansion%20inc_am29f400b_eol_21505e8-329620.pdf
*/

#include "xemu/emutools.h"
#include "xemu/emutools_files.h"
#include "enterprise128.h"
#include "sdext.h"
#include "cpu.h"
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#ifndef CONFIG_SDEXT_SUPPORT
#warning "SDEXT support is disabled by configuration."
#else
//#define DEBUG_SDEXT
#define CONFIG_SDEXT_FLASH

#ifdef DEBUG_SDEXT
#	define SD_DEBUG	DEBUG
#else
#	define SD_DEBUG(...)
#endif


static const char *sdext_rom_signature = "SDEXT";

int sdext_cart_enabler = SDEXT_CART_ENABLER_OFF;
char sdimg_path[PATH_MAX + 1];
static int rom_page_ofs;
static int is_hs_read;
static Uint8 _spi_last_w;
static int cs0, cs1;
static Uint8 status;

static Uint8 sd_ram_ext[7 * 1024]; // 7K of accessible SRAM
/* The FIRST 64K of flash (sector 0) is structured this way:
   * first 48K is accessed directly at segment 4,5,6, so it's part of the normal EP memory emulated
   * the last 16K is CANNOT BE accessed at all from EP
   It's part of the main memory array at the given offset, see flash[0][addr] below
   The SECOND 64K of flash (sector 1) is stored in sd_rom_ext (see below), it's flash[1][addr] */
static Uint8 sd_rom_ext[0x10000];
static Uint8 *flash[2] = { memory + 0x10000, sd_rom_ext };

static int flash_wr_protect = 0;
static int flash_bus_cycle = 0;
static int flash_command = 0;

static Uint8 cmd[6], cmd_index, _read_b, _write_b, _write_specified;
static const Uint8 *ans_p;
static int ans_index, ans_size;
static int writing;
static int delay_answer;
static void (*ans_callback)(void);

//static FILE *sdf;
static int sdfd =  -1;
static Uint8 _buffer[1024];
off_t sd_card_size = 0;

#define MAX_CARD_SIZE 2147483648UL
#define MIN_CARD_SIZE    8388608UL

/* ID files:
 * C0 71 00 00 │ 00 5D 01 32 │ 13 59 80 E3 │ 76 D9 CF FF │ 16 40 00 4F │ 01 50 41 53
 * 30 31 36 42 │ 41 35 E4 39 │ 06 00 35 03 │ 80 FF 80 00 │ 00 00 00 00 │ 00 00 00 00
 * 4 bytes: size in sectors:   C0 71 00 00
 * CSD register	 00 5D 01 32 │ 13 59 80 E3 │ 76 D9 CF FF │ 16 40 00 4F
 * CID register  01 50 41 53 | 30 31 36 42 │ 41 35 E4 39 │ 06 00 35 03
 * OCR register  80 FF 80 00
 */


static const Uint8 _stop_transmission_answer[] = {
	0, 0, 0, 0, // "stuff byte" and some of it is the R1 answer anyway
	0xFF // SD card is ready again
};
#define __CSD_OFS 2
#define CSD(a) _read_csd_answer[(__CSD_OFS) + (a)]
static Uint8 _read_csd_answer[] = {
	0xFF, // waiting a bit
	0xFE, // data token
	// the CSD itself
	0x00, 0x5D, 0x01, 0x32, 0x13, 0x59, 0x80, 0xE3, 0x76, 0xD9, 0xCF, 0xFF, 0x16, 0x40, 0x00, 0x4F,
	0, 0  // CRC bytes
};
static const Uint8 _read_cid_answer[] = {
	0xFF, // waiting a bit
	0xFE, // data token
	// the CID itself
	0x01, 0x50, 0x41, 0x53, 0x30, 0x31, 0x36, 0x42, 0x41, 0x35, 0xE4, 0x39, 0x06, 0x00, 0x35, 0x03,
	0, 0  // CRC bytes
};
static const Uint8 _read_ocr_answer[] = { // no data token, nor CRC! (technically this is the R3 answer minus the R1 part at the beginning ...)
	// the OCR itself
	0x80, 0xFF, 0x80, 0x00
};

#define ADD_ANS(ans) { ans_p = (ans); ans_index = 0; ans_size = sizeof(ans); }

#include "xemu/../rom/ep128/vhd_compressed.c"



static int decompress_vhd ( const Uint8 *p, int fd )
{
	int filelen = 0;
	for (;;) {
		Uint32 l = p[0] | (p[1] << 8) | (p[2] << 16) | ((p[3] & 0x7F) << 24);
		if (!l)
			break;
		p += 4;
		filelen += l;
		if (p[-1] & 0x80) {
			// printf("Zero seq len = %d\n", l);
			if (lseek(fd, filelen, SEEK_SET) != filelen)
				return 1;
		} else {
			// printf("Data len = %d\n", l);
			while (l) {
				int r = write(fd, p, l);
				if (r <= 0)
					return 1;
				l -= r;
				p += r;
			}
		}
	}
	return 0;
}



void sdext_clear_ram(void)
{
	memset(sd_ram_ext, 0xFF, 0x1C00);
}



static int sdext_detect_rom ( void )
{
	Uint8 *p = memory + 7 * 0x4000;
	Uint8 *p2 = p + 0x2000 - strlen(sdext_rom_signature);
	if (memcmp(p, "EXOS_ROM", 8))
		return 1;	// No EXOS_ROM header
	for (; p < p2; p++ ) {
		if (!memcmp(p, sdext_rom_signature, strlen(sdext_rom_signature)))
			return 0;	// found our extra ID
	}
	return 1;	// our ID cannot be found
}



#if 0
static inline off_t _assert_on_csd_size_mismatch ( off_t expected_size, int expected_mult, int expected_blocknr, int expected_blocklen )
{
	int mult = 2 ** (i["C_SIZE_MULT"] + 2)
	int blocknr = (i["C_SIZE"] + 1) * mult
	int blocklen = 2 ** i["READ_BL_LEN"]
	off_t size = (off_t)blocknr * (off_t)blocklen;
	if (size != expected_size || mult != expected_mult || blocknr != excepted_blocknr || blocklen != expected_blocklen)
		FATAL("Internal CSD size calculation failure!\nExpected=" PRINTF_LLD " Got=" PRINTF_LLD " (mult=%d blocknr=%d blocklen=%d)",
			(long long)expected_size, (long long)size,
			mult, blocknr, blocklen
		);
}
#endif



static int _size_calc ( off_t size )
{
	int blen_i;
	for (blen_i = 9; blen_i < 12; blen_i++) {
		int mult_i;
		int blen = 1 << blen_i;
		for (mult_i = 0; mult_i < 8; mult_i++) {
			int mult = 1 << (mult_i + 2);
			int res = size / blen;
			if (!(size % blen) && !(res % mult)) {
				res = (res / mult) - 1;
				if (res < 4096 && res > 0) {
					//printf("MAY HIT with blen=%d[%d],mult=%d[%d],result=%d\n",
					//        blen, blen_i, mult, mult_i, res
					//);
					CSD( 5) = (CSD( 5) & 0xF0) | blen_i;
					CSD( 6) = (CSD( 6) & 0xFC) | (res >> 10);
					CSD( 7) = (res >> 2) & 0xFF;
					CSD( 8) = (CSD( 8) & 0x3F) | ((res & 3) << 6);
					CSD( 9) = (CSD( 9) & 0xFC) | (mult_i >> 1);
					CSD(10) = (CSD(10) & 0x7F) | ((mult_i & 1) << 7);
					// CHECKING the result follows now!
					//_assert_on_csd_size_mismatch(size, mult, res, blen);
					return 0;
				}
			}
		}
	}
	return 1;
}



static int sdext_check_and_set_size ( void )
{
	off_t new_size;
	int is_vhd;
	if (sd_card_size < MIN_CARD_SIZE) {
		ERROR_WINDOW(
			"SD card image file \"%s\" is too small, minimal size is " PRINTF_LLD " Mbytes, but this one is " PRINTF_LLD " bytes long (about " PRINTF_LLD " Mbytes). SD access has been disabled!",
			sdimg_path, (long long)(MIN_CARD_SIZE >> 20), (long long)sd_card_size, (long long)(sd_card_size >> 20)
		);
		return 1;
	}
	/* check for VHD footer (not the real part of the image, +512 bytes structure at the end) */
	is_vhd = -1;
	if (lseek(sdfd, sd_card_size - 512, SEEK_SET) == sd_card_size - 512) {
		if (read(sdfd, _buffer, 512) == 512) {
			Uint8 *p = NULL;
			if (!memcmp(_buffer + 1, "conectix", 8)) {
				sd_card_size++;		// old, buggy Microsoft tool maybe, 511 bytes footer instead of 512. Treating size as the normalized one
				p = _buffer + 1;
				DEBUG("SDEXT: warning, old buggy Microsoft VHD file, activating workaround!" NL);
			} else if (!memcmp(_buffer, "conectix", 8))
				p = _buffer;
			if (p) {
				if (p[60] || p[61] || p[62] || p[63] != 2) {
					ERROR_WINDOW("SD card image \"%s\" is an unsupported VHD file (not fixed, maybe dynamic?)", sdimg_path);
					return 1;
				}
				is_vhd = 1;
			} else
				is_vhd = 0;
		}
	}
	if (is_vhd < 0) {
		ERROR_WINDOW("SD card image \"%s\" I/O error while detecting type: %s.\nSD access has been disabled!", sdimg_path, ERRSTR());
		return 1;
	}
	if (is_vhd) {
		DEBUG("SDEXT: VHD file detected as card image." NL);
		sd_card_size -= 512;
	} else
		DEBUG("SDEXT: VHD file is not detected." NL);
	if (sd_card_size > MAX_CARD_SIZE) {	// do this check here, as VHD footer could overflow on 2G boundary at the beginning what we have support for in Xep128
		ERROR_WINDOW(
			"SD card image file \"%s\" is too large, maximal allowed size is " PRINTF_LLD " Mbytes, but this one is " PRINTF_LLD " bytes long (about " PRINTF_LLD " Mbytes). "
			"SD access has been disabled!",
			sdimg_path, (long long)(MAX_CARD_SIZE >> 20), (long long)sd_card_size, (long long)(sd_card_size >> 20)
		);
		return 1;
	}
	if ((sd_card_size & 511)) {	// do this check here, as buggy MS tool can create 511 "tail" as footer
		ERROR_WINDOW("SD card image file \"%s\" size is not multiple of 512 bytes! SD access has been disabled!", sdimg_path);
		return 1;
	}
	/* probing size, optionally extending on request */
	new_size = sd_card_size;
	while (_size_calc(new_size))
		new_size += 512;
	if (new_size == sd_card_size)
		return 0;
	if (is_vhd)
		WARNING_WINDOW("SD-card image \"%s\" is promoted for extension but it seems to be a VHD file.\nIf you allow extension it WON'T BE USED AS VHD ANY MORE BY OTHER SOFTWARE!", sdimg_path);
	INFO_WINDOW("SD-card image file \"%s\" is about to be extended with %d bytes (the next valid SD-card size), new size is: " PRINTF_LLD, sdimg_path, (int)(new_size - sd_card_size), (long long)new_size);
	if (!QUESTION_WINDOW("Not allowed|Allowed (DANGEROUS)", "Do you allow this extension? NOTE: it's a test feature, do not allow it, if you are unsure!")) {
		INFO_WINDOW("You didn't allow the extension. You can continue, but some EP128 software may fail (ie: fdisk)!");
		return 0;
	}
	if (lseek(sdfd, new_size - 1, SEEK_SET) != new_size - 1) {
		ERROR_WINDOW("SD card image file \"%s\" cannot be extended (seek error: %s).\nYou can continue but some EP128 software may fail (ie: fdisk)!", sdimg_path, ERRSTR());
		return 0;
	}
	if (write(sdfd, sd_rom_ext, 1) != 1) {	// sd_rom_ext is just used to write some *RANDOM* byte, the content is not so important here :-P It will create a file "with hole" btw.
		ERROR_WINDOW("SD card image file \"%s\" cannot be extended (write error: %s).\nYou can continue but some EP128 software may fail (ie: fdisk)!", sdimg_path, ERRSTR());
		return 0;
	}
	sd_card_size = new_size;
	INFO_WINDOW("Great, image file is successfully extended to valid SD-card size! :-)\nNext time you can enjoy the lack of these info message, as you have valid file size now :-)");
	return 0;
}



void sdext_shutdown ( void )
{
	if (sdfd >= 0) {
		close(sdfd);
		sdfd = -1;
	}
}


/* SDEXT emulation currently excepts the cartridge area (segments 4-7) to be filled
 * with the FLASH ROM content. Even segment 7, which will be copied to the second 64K "hidden"
 * and pagable flash area of the SD cartridge. Currently, there is no support for the full
 * sized SDEXT flash image */
int sdext_init ( const char *img_fn )
{
	sdext_shutdown();
	/* try to detect SDEXT ROM extension and only turn on emulation if it exists */
	if (sdext_detect_rom()) {
		WARNING_WINDOW("No SD-card cartridge ROM code found in loaded ROM set. SD card hardware emulation has been disabled!");
		*sdimg_path = 0;
		SD_DEBUG("SDEXT: init: REFUSE: no SD-card cartridge ROM code found in loaded ROM set." NL);
		return 0;
	}
	SD_DEBUG("SDEXT: init: cool, SD-card cartridge ROM code seems to be found in loaded ROM set, enabling SD card hardware emulation ..." NL);
	// try to open SD card image. If not found, and it's the DEFAULT config option we provide user to install an empty one (and later to download a populated one)
	for (;;) {
		int ro = O_RDONLY;
		sdfd = xemu_open_file(img_fn, O_RDWR, &ro, sdimg_path);
		if (sdfd >= 0) {
			DEBUGPRINT("SDEXT: SD-card image is open %s from file %s" NL, sdimg_path, ro ? "R/O" : "R/W");
			if (ro)
				INFO_WINDOW("Warning, SD-card image could be opened only in read-only mode!");
			break;
		} else {
			if (!strcmp(img_fn, SDCARD_IMG_FN)) {
				// if this was the default image, then we may want to give some help to the user to create it!
				int r = QUESTION_WINDOW("?Exit|!Continue without SD card|Create empty image", "Cannot open default SD card image file.");
				if (r == 1)
					break;
				if (r == 0)
					XEMUEXIT(0);
				if (r == 2) {	// create an empty image
					sdfd = xemu_open_file(img_fn, O_CREAT | O_TRUNC | O_RDWR, NULL, sdimg_path);
					if (sdfd < 0) {
						ERROR_WINDOW("Cannot create empty image: %s", ERRSTR());
						continue;
					}
					if (decompress_vhd(empty_vhd_image, sdfd)) {
						ERROR_WINDOW("Error decompressing empty image: %s",  ERRSTR());
						close(sdfd);
						sdfd = -1;
						unlink(sdimg_path);
						continue;
					}
					break;
				}
			} else {
				ERROR_WINDOW("Could not open requested SD-card image: %s\n%s", ERRSTR(), img_fn);
				XEMUEXIT(0);
			}
		}
	}
	if (sdfd < 0) {
		WARNING_WINDOW("SD card image file \"%s\" cannot be open: %s. You can use Xep128 but SD card access won't work!", sdimg_path, ERRSTR());
		*sdimg_path = 0;
	} else {
		sd_card_size = lseek(sdfd, 0, SEEK_END);
		if (sdext_check_and_set_size()) {
			close(sdfd);
			sdfd = -1;
			*sdimg_path = 0;
		} else
			DEBUG("SDEXT: SD card size is: " PRINTF_LLD " bytes" NL, (long long)sd_card_size);
	}
	memset(sd_rom_ext, 0xFF, 0x10000);
	/* Copy ROM image of 16K to the second 64K of the cartridge flash. Currently only 8K is used.
           It's possible to use 64K the ROM set image used by Xep128 can only hold 16K this way, though. */
	memcpy(sd_rom_ext, memory + 7 * 0x4000, 0x4000);
	sdext_clear_ram();
	sdext_cart_enabler = SDEXT_CART_ENABLER_ON;	// turn emulation on
	rom_page_ofs = 0;
	is_hs_read = 0;
	cmd_index = 0;
	ans_size = 0;
	delay_answer = 0;
	ans_index = 0;
	ans_callback = NULL;
	status = 0;
	_read_b = 0;
	_write_b = 0xFF;
	_spi_last_w = 0xFF;
	writing = -2;
	SD_DEBUG("SDEXT: init end" NL);
	return 0;
}


static int blocks;


// FIXME: error handling of read() !!!!
// FIXME: check excess of card size (during multiple block read) !!!!
static void _block_read ( void )
{
	int ret;
	z80ex_w_states(40);	// TODO: fake some wait states here, actully this is the WRONG method, as not the Z80 should wait but the SD card's answer ...
	blocks++;
	_buffer[0] = 0xFF; // wait a bit
	_buffer[1] = 0xFE; // data token
	//ret = fread(_buffer + 2, 1, 512, sdf);
	ret = read(sdfd, _buffer + 2, 512);
	SD_DEBUG("SDEXT: REGIO: read retval = %d" NL, ret);
	(void)ret;
	_buffer[512 + 2] = 0; // CRC
	_buffer[512 + 3] = 0; // CRC
	ans_p = _buffer;
	ans_index = 0;
	ans_size = 512 + 4;
}



/* SPI is a read/write in once stuff. We have only a single function ... 
 * _write_b is the data value to put on MOSI
 * _read_b is the data read from MISO without spending _ANY_ SPI time to do shifting!
 * This is not a real thing, but easier to code this way.
 * The implementation of the real behaviour is up to the caller of this function.
 */
static void _spi_shifting_with_sd_card ()
{
	if (!cs0) { // Currently, we only emulate one SD card, and it must be selected for any answer
		_read_b = 0xFF;
		return;
	}
	/* begin of write support */
	if (delay_answer) {
		delay_answer = 0;
		return;
	}
	if (writing > -2) {
		_read_b = 0xFF;
		SD_DEBUG("SDEXT: write byte #%d as %02Xh for CMD %d" NL, writing, _write_b, cmd[0]);
		if (writing == -1) {
			if (_write_b == 0xFD) {	// stop token
				SD_DEBUG("SDEXT: Stop token got" NL);
				_read_b = 0;	// wait a tiny time ...
				writing = -2;	// ... but otherwise, end of write session
				return;
			}
			if (_write_b != 0xFE && _write_b != 0xFC) {
				SD_DEBUG("SDEXT: Waiting for token ..." NL);
				return;
			}
			SD_DEBUG("SDEXT: token found %02Xh" NL, _write_b);
			writing = 0;
			return;
		}
		_buffer[writing++] = _write_b;	// store written byte
		if (writing == 512 + 2) {	// if one block (+ 2byte CRC) is written by host ...
			off_t ret, _offset = (cmd[1] << 24) | (cmd[2] << 16) | (cmd[3] << 8) | cmd[4];
			_offset += 512UL * blocks;
			blocks++;
			if (_offset > sd_card_size - 512UL) {
				ret = 13;
				SD_DEBUG("SDEXT: access beyond the card size!" NL);
			} else {
				ret = (lseek(sdfd, _offset, SEEK_SET) == _offset) ? 5 : 13;
				if (ret != 5)
					SD_DEBUG("SDEXT: seek error: %s" NL, ERRSTR());
				else {
					ret = (write(sdfd, _buffer, 512) == 512) ? 5 : 13;
					if (ret != 5)
						SD_DEBUG("SDEXT: write error: %s" NL, ERRSTR());
				}
			}
			// space for the actual block write ...
			if (cmd[0] == 24 || ret != 5) {
				SD_DEBUG("SDEXT: cmd-%d end blocks=%d" NL, cmd[0], blocks);
				_read_b = ret;	// data accepted?
				writing = -2;	// turn off write mode
				delay_answer = 1;
			} else {
				SD_DEBUG("SDEXT: cmd-25 end blocks=%d" NL, blocks);
				_read_b = ret;	// data accepted?
				writing = -1;	// write mode back to the token waiting phase
				delay_answer = 1;
			}
		}
		return;
	}
	/* end of write support */
	if (cmd_index == 0 && (_write_b & 0xC0) != 0x40) {
		if (ans_index < ans_size) {
			SD_DEBUG("SDEXT: REGIO: streaming answer byte %d of %d-1 value %02X" NL, ans_index, ans_size, ans_p[ans_index]);
			_read_b = ans_p[ans_index++];
		} else {
			if (ans_callback)
				ans_callback();
			else {
				//_read_b = 0xFF;
				ans_index = 0;
				ans_size = 0;
				SD_DEBUG("SDEXT: REGIO: dummy answer 0xFF" NL);
			}
			_read_b = 0xFF;
		}
		return;
	}
	if (cmd_index < 6) {
		cmd[cmd_index++] = _write_b;
		_read_b = 0xFF;
		return;
	}
	SD_DEBUG("SDEXT: REGIO: command (CMD%d) received: %02X %02X %02X %02X %02X %02X" NL, cmd[0] & 63, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5]);
	cmd[0] &= 63;
	cmd_index = 0;
	ans_callback = NULL;
	switch (cmd[0]) {
		case 0:	// CMD 0
			_read_b = 1; // IDLE state R1 answer
			break;
		case 1:	// CMD 1 - init
			_read_b = 0; // non-IDLE now (?) R1 answer
			break;
		case 16:	// CMD16 - set blocklen (?!) : we only handles that as dummy command oh-oh ...
			_read_b = 0; // R1 answer
			break;
		case 9:  // CMD9: read CSD register
			SD_DEBUG("SDEXT: REGIO: command is read CSD register" NL);
			ADD_ANS(_read_csd_answer);
			_read_b = 0; // R1
			break;
		case 10: // CMD10: read CID register
			ADD_ANS(_read_cid_answer);
			_read_b = 0; // R1
			break;
		case 58: // CMD58: read OCR
			ADD_ANS(_read_ocr_answer);
			_read_b = 0; // R1 (R3 is sent as data in the emulation without the data token)
			break;
		case 12: // CMD12: stop transmission (reading multiple)
			ADD_ANS(_stop_transmission_answer);
			_read_b = 0;
			// actually we don't do too much, as on receiving a new command callback will be deleted before this switch-case block
			SD_DEBUG("SDEXT: REGIO: block counter before CMD12: %d" NL, blocks);
			blocks = 0;
			break;
		case 17: // CMD17: read a single block, babe
		case 18: // CMD18: read multiple blocks
			blocks = 0;
			if (sdfd < 0)
				_read_b = 32; // address error, if no SD card image ... [this is bad TODO, better error handling]
			else {
				off_t ret, _offset = (cmd[1] << 24) | (cmd[2] << 16) | (cmd[3] << 8) | cmd[4];
				SD_DEBUG("SDEXT: REGIO: seek to %ld in the image file." NL, _offset);
				z80ex_w_states(100);	// TODO: fake some wait states here, actully this is the WRONG method, as not the Z80 should wait but the SD card's answer ...
				if (_offset > sd_card_size - 512UL) {
					_read_b = 32; // address error, TODO: what is the correct answer here?
					SD_DEBUG("SDEXT: access beyond the card size!" NL);
				} else {
					//fseek(sdf, _offset, SEEK_SET);
					ret = lseek(sdfd, _offset, SEEK_SET);
					if (ret != _offset) {
						_read_b = 32; // address error, TODO: what is the correct answer here?
						SD_DEBUG("SDEXT: seek error to %ld (got: %ld)" NL, _offset, ret);
					} else {
						_block_read();
						if (cmd[0] == 18)
							ans_callback = _block_read; // in case of CMD18, continue multiple sectors, register callback for that!
						_read_b = 0; // R1
					}
				}
			}
			break;
		case 24: // CMD24: write block
		case 25: // CMD25: write multiple blocks
			blocks = 0;
			writing = -1;	// signal writing (-2 for not-write mode), also the write position into the buffer
			_read_b = 0;	// R1 answer, OK
			break;
		default: // unimplemented command, heh!
			SD_DEBUG("SDEXT: REGIO: unimplemented command %d = %02Xh" NL, cmd[0], cmd[0]);
			_read_b = 4; // illegal command :-/
			break;
	}
}



static void flash_erase ( int sector )	// erase sectors 0 or 1, or both if -1 is given!
{
	if (sector < 1) {
		memset(flash[0], 0xFF, 0xC000);		// erase sector 0, it's only 48K accessible on SD/EP so it does not matter, real flash would be 64K here too
		SD_DEBUG("SDEXT: FLASH: erasing sector 0!" NL);
		WARNING_WINDOW("Erasing flash sector 0! You can safely ignore this warning.");
	}
	if (abs(sector) == 1) {
		memset(flash[1], 0xFF, 0x10000);	// erase sector 1
		SD_DEBUG("SDEXT: FLASH: erasing sector 1!" NL);
		WARNING_WINDOW("Erasing flash sector 1! You can safely ignore this warning.");
	}
}



static Uint8 flash_rd_bus_op ( int sector, Uint16 addr )
{
	Uint8 byte;
	if (flash_command == 0x90)
		switch (addr & 0xFF) {
			case 0x00:
				byte = 1;	// manufacturer ID
				SD_DEBUG("SDEXT: FLASH: cmd 0x90 get manufacturer ID, result = %02Xh" NL, byte);
				break;
			case 0x02:
				byte = 0x23;	// device ID, top boot block
				SD_DEBUG("SDEXT: FLASH: cmd 0x90 get device ID, result = %02Xh" NL, byte);
				break;
			case 0x04:
				byte = 0;	// sector protect status, etc?
				SD_DEBUG("SDEXT: FLASH: cmd 0x90 get sector protect status, result = %02Xh" NL, byte);
				break;
			default:
				byte = flash[sector][addr];	// not sure what to do in case of non-valid query "code"
				SD_DEBUG("SDEXT: FLASH: cmd 0x90 unknown info requested (%d), accesssing flash content instead." NL, addr & 0xFF);
				break;
		}
	else
		byte = flash[sector][addr];
	flash_command = 0;
	flash_bus_cycle = 0;
	return byte;
}


static int flash_warn_programming = 1;
static void flash_wr_bus_op ( int sector, Uint16 addr, Uint8 data )
{
	int idaddr = addr & 0x3FFF;
	if (flash_command == 0x90)
		flash_bus_cycle = 0;    // autoselect mode does not have wr cycles more (only rd)
	SD_DEBUG("SDEXT: FLASH: WR OP: sector %d addr %04Xh data %02Xh flash-bus-cycle %d flash-command %02Xh" NL, sector, addr, data, flash_bus_cycle, flash_command);
	if (flash_wr_protect)
		return;	// write protection on flash, do not accept any write bus op
	switch (flash_bus_cycle) {
		case 0:
			flash_command = 0;	// invalidate command
			if (data == 0xB0 || data == 0x30) {
				//WARNING_WINDOW("SDEXT FLASH erase suspend/resume (cmd %02Xh) is not emulated yet :-(", data);
				SD_DEBUG("SDEXT: FLASH: erase suspend/resume is not yet supported, ignoring ..." NL);
				return; // well, erase suspend/resume is currently not supported :-(
			}
			if (data == 0xF0) {
				SD_DEBUG("SDEXT: FLASH: reset command" NL);
				return; // reset command
			}
			if (idaddr != 0xAAA || data != 0xAA) {
				SD_DEBUG("SDEXT: FLASH: invalid command sequence at the beginning [bus_cycle=0]" NL);
				return; // invalid cmd seq
			}
			flash_bus_cycle = 1;
			return;
		case 1:
			if (idaddr != 0x555 || data != 0x55) { // invalid cmd seq
				SD_DEBUG("SDEXT: FLASH: invalid command sequence [bus_cycle=1]" NL);
				flash_bus_cycle = 0;
				return;
			}
			flash_bus_cycle = 2;
			return;
		case 2:
			if (idaddr != 0xAAA) {
				SD_DEBUG("SDEXT: FLASH: invalid command sequence [bus_cycle=2]" NL);
				flash_bus_cycle = 0; // invalid cmd seq
				return;
			}
			if (data != 0x90 && data != 0x80 && data != 0xA0) {
				SD_DEBUG("SDEXT: FLASH: unknown command [bus_cycle=2]" NL);
				flash_bus_cycle = 0; // invalid cmd seq
				return;
			}
			flash_command = data;
			flash_bus_cycle = 3;
			return;
		case 3:
			if (flash_command == 0xA0) {	// program command!!!!
				// flash programming allows only 1->0 on data bits, erase must be executed for 0->1
				Uint8 oldbyte = flash[sector][addr];
				Uint8 newbyte = oldbyte & data;
				flash[sector][addr] = newbyte;
				SD_DEBUG("SDEXT: FLASH: programming: sector %d address %04Xh data-req %02Xh, result %02Xh->%02Xh" NL, sector, addr, data, oldbyte, newbyte);
				if (flash_warn_programming) {
					WARNING_WINDOW("Flash programming detected! There will be no further warnings on more bytes.\nYou can safely ignore this warning.");
					flash_warn_programming = 0;
				}
				flash_command = 0; // end of command
				flash_bus_cycle = 0;
				return;
			}
			// only flash command 0x80 can be left, 0x90 handled before "switch", 0xA0 just before ...
			if (idaddr != 0xAAA || data != 0xAA) { // invalid cmd seq
				SD_DEBUG("SDEXT: FLASH: invalid command sequence [bus_cycle=3]" NL);
				flash_command = 0;
				flash_bus_cycle = 0;
				return;
			}
			flash_bus_cycle = 4;
			return;
		case 4:	// only flash command 0x80 can get this far ...
			if (idaddr != 0x555 || data != 0x55) { // invalid cmd seq
				SD_DEBUG("SDEXT: FLASH: invalid command sequence [bus_cycle=4]" NL);
				flash_command = 0;
				flash_bus_cycle = 0;
				return;
			}
			flash_bus_cycle = 5;
			return;
		case 5:	// only flash command 0x80 can get this far ...
			if (idaddr == 0xAAA && data == 0x10) {	// CHIP ERASE!!!!
				flash_erase(-1);
			} else if (data == 0x30) {
				flash_erase(sector);
			}
			flash_bus_cycle = 0; // end of erase command?
			flash_command = 0;
			return;
		default:
			FATAL("Invalid SDEXT FLASH bus cycle #%d on WR", flash_bus_cycle);
			break;
	}
}



/* We expects all 4-7 seg reads/writes to be handled, as for re-flashing emu etc will need it!
   Otherwise only segment 7 would be enough if flash is not emulated other than only "some kind of ROM". */

Uint8 sdext_read_cart ( Uint16 addr )
{
	SD_DEBUG("SDEXT: read cart @ %04X [CPU: seg=%02X, pc=%04X]" NL, addr, ports[0xB0 | (Z80_PC >> 14)], Z80_PC);
	if (addr < 0xC000) {
		Uint8 byte = flash_rd_bus_op(0, addr);
		SD_DEBUG("SDEXT: reading base ROM, ROM offset = %04X, result = %02X" NL, addr, byte);
		return byte;
	}
	if (addr < 0xE000) {
		Uint8 byte;
		addr = rom_page_ofs + (addr & 0x1FFF);
		byte = flash_rd_bus_op(1, addr);
		SD_DEBUG("SDEXT: reading paged ROM, ROM offset = %04X, result = %02X" NL, addr, byte);
		return byte;
	}
	if (addr < 0xFC00) {
		addr -= 0xE000;
		SD_DEBUG("SDEXT: reading RAM at offset %04X, result = %02X" NL, addr, sd_ram_ext[addr]);
		return sd_ram_ext[addr];
	}
	if (is_hs_read) {
		// in HS-read (high speed read) mode, all the 0x3C00-0x3FFF acts as data _read_ register (but not for _write_!!!)
		// also, there is a fundamental difference compared to "normal" read: each reads triggers SPI shifting in HS mode, but not in regular mode, there only write does that!
		Uint8 old = _read_b; // HS-read initiates an SPI shift, but the result (AFAIK) is the previous state, as shifting needs time!
		_spi_shifting_with_sd_card();
		SD_DEBUG("SDEXT: REGIO: R: DATA: SPI data register HIGH SPEED read %02X [future byte %02X] [shited out was: %02X]" NL, old, _read_b, _write_b);
		return old;
	} else
		switch (addr & 3) {
			case 0: 
				// regular read (not HS) only gives the last shifted-in data, that's all!
				SD_DEBUG("SDEXT: REGIO: R: DATA: SPI data register regular read %02X" NL, _read_b);
				return _read_b;
			case 1: // status reg: bit7=wp1, bit6=insert, bit5=changed (insert/changed=1: some of the cards not inserted or changed)
				SD_DEBUG("SDEXT: REGIO: R: status" NL);
				return status;
				//return 0xFF - 32 + changed;
				//return changed | 64;
			case 2: // ROM pager [hmm not readble?!]
				SD_DEBUG("SDEXT: REGIO: R: rom pager" NL);
				return 0xFF;
				return rom_page_ofs >> 8;
			case 3: // HS read config is not readable?!]
				SD_DEBUG("SDEXT: REGIO: R: HS config" NL);
				return 0xFF;
				return is_hs_read;
			default:
				FATAL("SDEXT: FATAL, unhandled (RD) case");
				break;
		}
	FATAL("SDEXT: FATAL, control should not get here");
	return 0; // make GCC happy :)
}


void sdext_write_cart ( Uint16 addr, Uint8 data )
{
	SD_DEBUG("SDEXT: write cart @ %04X with %02X [CPU: seg=%02X, pc=%04X]" NL, addr, data, ports[0xB0 | (Z80_PC >> 14)], Z80_PC);
	if (addr < 0xC000) {		// segments 4-6, call flash WR emulation (sector 0, last 16K cannot be accessed by the EP ever!)
		flash_wr_bus_op(0, addr, data);
		return;
	}
	if (addr < 0xE000) {		// pageable ROM (8K), call flash WR emulation with the right flash sector (1) and address offset in flash within the sector
		flash_wr_bus_op(1, (addr & 0x1FFF) + rom_page_ofs, data);
		return;
	}
	if (addr < 0xFC00) {		// SDEXT's RAM (7K), writable
		addr -= 0xE000;
		SD_DEBUG("SDEXT: writing RAM at offset %04X" NL, addr);
		sd_ram_ext[addr] = data;
		return;
	}
	// rest 1K is the (memory mapped) I/O area
	switch (addr & 3) {
		case 0:	// data register
			SD_DEBUG("SDEXT: REGIO: W: DATA: SPI data register to %02X" NL, data);
			if (!is_hs_read) _write_b = data;
			_write_specified = data;
			_spi_shifting_with_sd_card();
			break;
		case 1: // control register (bit7=CS0, bit6=CS1, bit5=clear change card signal
			if (data & 32) // clear change signal
				status &= 255 - 32;
			cs0 = data & 128;
			cs1 = data & 64;
			SD_DEBUG("SDEXT: REGIO: W: control register to %02X CS0=%d CS1=%d" NL, data, cs0, cs1);
			break;
		case 2: // ROM pager register
			rom_page_ofs = (data & 0xE0) << 8;	// only high 3 bits count
			SD_DEBUG("SDEXT: REGIO: W: paging ROM to %02X" NL, data);
			break;
		case 3: // HS (high speed) read mode to set: bit7=1
			is_hs_read = data & 128;
			_write_b = is_hs_read ? 0xFF : _write_specified;
			SD_DEBUG("SDEXT: REGIO: W: HS read mode is %s" NL, is_hs_read ? "set" : "reset");
			break;
		default:
			FATAL("SDEXT: FATAL, unhandled (WR) case");
			break;
	}
}

#endif
