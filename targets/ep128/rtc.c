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
#include "enterprise128.h"
#include "rtc.h"
#include <time.h>


//#define RESET_RTC_INDEX


static int _rtc_register;
static Uint8 cmos_ram[0x100];
int rtc_update_trigger;


void rtc_set_reg(Uint8 val)
{
	_rtc_register = val;
	DEBUG("RTC: register number %02X has been selected" NL, val);
}


void rtc_write_reg(Uint8 val)
{
	DEBUG("RTC: write reg %02X with data %02X" NL, _rtc_register, val);
	if (_rtc_register == 0xC || _rtc_register == 0xD) return;
	if (_rtc_register == 0xA) val &= 127;
	cmos_ram[_rtc_register] = val;
#ifdef RESET_RTC_INDEX
	_rtc_register = 0xD;
#endif
}


static int _rtc_conv(int bin, int is_hour)
{
	int b7 = 0;
	if (is_hour && (!(cmos_ram[0xB] & 2))) { // AM/PM
		if (bin == 0) {
			bin = 12;
		} else if (bin == 12) {
			b7 = 128;
		} else if (bin > 12) {
			bin -= 12;
			b7 = 128;
		}
	}
	if (!(cmos_ram[0xB] & 4)) { // do bin->bcd
		bin = ((bin / 10) << 4) | (bin % 10);
	}
	return bin | b7;
}


static void _rtc_update(void)
{
	struct tm *t = localtime(&unix_time);
	cmos_ram[   0] = _rtc_conv(t->tm_sec, 0);
	cmos_ram[   2] = _rtc_conv(t->tm_min, 0);
	cmos_ram[   4] = _rtc_conv(t->tm_hour, 1);
	cmos_ram[   6] = _rtc_conv(t->tm_wday + 1, 0);  // day, 1-7 (week)
	cmos_ram[   7] = _rtc_conv(t->tm_mday, 0); // date, 1-31
	cmos_ram[   8] = _rtc_conv(t->tm_mon + 1, 0); // month, 1 -12
	cmos_ram[   9] = _rtc_conv((t->tm_year % 100) + 20, 0); // year, 0 - 99
	cmos_ram[0x32] = _rtc_conv(21, 0); // century???
	DEBUG("RTC: time/date has been updated for \"%d-%02d-%02d %02d:%02d:%02d\" at UNIX epoch %ld" NL,
		t->tm_year + 1900,
		t->tm_mon + 1,
		t->tm_mday,
		t->tm_hour,
		t->tm_min,
		t->tm_sec,
		(long int)unix_time
	);
}


void rtc_reset(void)
{
	memset(cmos_ram, 0, 0x100);
	_rtc_register = 0xD;
	rtc_update_trigger = 0;
	cmos_ram[0xA] = 32;
	cmos_ram[0xB] = 2; // 2 | 4;
	cmos_ram[0xC] = 0;
	cmos_ram[0xD] = 128;
	DEBUG("RTC: reset" NL);
	_rtc_update();
}


Uint8 rtc_read_reg(void)
{
	int i = _rtc_register;
#ifdef RESET_RTC_INDEX
	_rtc_register = 0xD;
#endif
	if (i > 63)
		return 0xFF;
	if (rtc_update_trigger && (cmos_ram[0xB] & 128) == 0 && i < 10) {
		_rtc_update();
		rtc_update_trigger = 0;
	}
	DEBUG("RTC: reading register %02X, result will be: %02X" NL, i, cmos_ram[i]);
	return cmos_ram[i];
}
