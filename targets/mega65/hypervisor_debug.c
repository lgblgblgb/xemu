/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
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

#include <stdio.h>
#include <SDL.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "mega65.h"
#include "emutools.h"
#include "hypervisor_debug.h"
#include "cpu65c02.h"

#define INFO_MAX_SIZE	32
#define BUFFER_SIZE	256


static const char empty_string[] = "";
static char debug_lines[0x4000][2][INFO_MAX_SIZE];		// I know. UGLY! and wasting memory. But this is only a HACK :)
static char megadebug_buffer[BUFFER_SIZE];
static int resolver_ok = 0;



int megadebug_init ( const char *fn )
{
	FILE *fp;
	int fd;
	if (!fn || !*fn) {
		DEBUG("MEGADEBUG: feature is not enabled, null file name for list file" NL);
		return 1;
	}
	for (fd = 0; fd < 0x4000; fd++) {
		debug_lines[fd][0][0] = 0;
	}
	fd = emu_load_file(fn, NULL, -1);
	if (fd < 0) {
		INFO_WINDOW("Cannot open %s, no resolved symbols will be used.", fn);
		return 1;
	}
	fp = fdopen(fd, "rb");
	while (fgets(megadebug_buffer, BUFFER_SIZE, fp)) {
		int addr = 0xFFFF;
		char *p, *p1, *p2;
		if (sscanf(megadebug_buffer, "%04X", &addr) != 1) continue;
		if (addr < 0x8000 || addr >= 0xC000) continue;
		p2  = strchr(megadebug_buffer, '|');
		if (!p2) continue;			// no '|' pipe in the line, skip
		if (strchr(p2 + 1, '|')) continue;	// more than one '|' pipes, it's a hex dump, skip
		*(p2++) = 0;
		p = strrchr(p2, '/');
		if (p)
			p2 = p + 1;
		else {
			p = strrchr(p2, '\\');
			if (p)
				p2 = p + 1;
		}
		while (*p2 <= 32 && *p2)
			p2++;
		p = p2 + strlen(p2) - 1;
		while (p > p2 && *p <= 32)
			*(p--) = 0;
		if (strlen(p2) >= 32)
			FATAL("Bad list file, too long file reference part");
		// that was awful. Now the first part
		p1 = megadebug_buffer + 5;
		while (*p1 && *p1 <= 32)
			p1++;
		p = p1 + strlen(p1) - 1;
		while (p > p1 && *p <= 32)
			*(p--) = 0;
		if (strlen(p2) >= 32)
			FATAL("Bad list file, too long assembly part");
		// And finally, copy line to its well deserved place(s)
		snprintf(debug_lines[addr - 0x8000][0], INFO_MAX_SIZE, "%s", p1);
		snprintf(debug_lines[addr - 0x8000][1], INFO_MAX_SIZE, "%s", p2);
	}
	fclose(fp);
	close(fd);
	resolver_ok = 1;
	return 0;
}


const char *megadebug_resolve ( Uint16 addr )
{
	if (!in_hypervisor) {
		sprintf(megadebug_buffer, "[not in hypervisor mode]");
		return megadebug_buffer;
	}
	// TODO: check upgraded bit, if not upgraded hypervisor does this, we accept this, as it's needed for the upgrade itself.
	if (addr < 0x8000 || addr >= 0xC000) {
		DEBUG("MEGADEBUG: code outside of hypervisor memory @ $%04X!" NL, addr);
		return NULL;
	}
	if (!resolver_ok) {
		sprintf(megadebug_buffer, "[no resolver map]");
		return megadebug_buffer;
	}
	if (!debug_lines[addr - 0x8000][0]) {
		DEBUG("MEGADEBUG: address $%04X cannot be found in asm list!" NL, addr);
		return NULL;	// empty "code point!"
	}
	// WARNING: as it turned out, using snprintf() is EXTREMLY slow to call at the frequency of the emulated CPU clock (~3.5MHz)
	// even on a "modern" PC. TODO: this must be rewritten to use custom "rendering" of debug information, instead of stdio stuffs!
	snprintf(megadebug_buffer, BUFFER_SIZE, "%-32s PC=%04X SP=%04X B=%02X A=%02X X=%02X Y=%02X Z=%02X P=%c%c%c%c%c%c%c%c @ %s",
		debug_lines[addr - 0x8000][0],
		cpu_pc, cpu_sphi | cpu_sp, cpu_bphi >> 8, cpu_a, cpu_x, cpu_y, cpu_z,
		cpu_pfn ? 'N' : 'n',
		cpu_pfv ? 'V' : 'v',
		cpu_pfe ? 'E' : 'e',
		'-',
		cpu_pfd ? 'D' : 'd',
		cpu_pfi ? 'I' : 'i',
		cpu_pfz ? 'Z' : 'z',
		cpu_pfc ? 'C' : 'c',
		debug_lines[addr - 0x8000][1]
	);
	return megadebug_buffer;
}

