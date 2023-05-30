/* Commodore LCD emulator (rewrite of my world's first working Commodore LCD emulator)
   Copyright (C)2016-2023 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   Part of the Xemu project: https://github.com/lgblgblgb/xemu

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
#include "inject.h"
#include "commodore_lcd.h"



struct prg_inject_st prg_inject;


static int prg_relink ( Uint8 *data, int target_addr, int source_addr, const int size_limit, int *actual_size )
{
	int i = 0, relink_counter = 0;
	for (;;) {
		if (i + 1 >= size_limit)
			return -1;
		const int ptrpos = i;
		const int nextptr = data[i] + (data[i + 1] << 8);	// read the next-addr pointer
		i += 2;	// jump over nextptr
		if (!nextptr)
			break;	// end of program
		//DEBUGPRINT("BASIC LINE: %d" NL, data[i] + data[i + 1] * 256);
		i += 2;	// jump over basic line number word
		for (int linelength = 0;;) {
			if (i >= size_limit)
				return -1;
			if (!data[i++])
				break;
			if (++linelength >= 255)	// FIXME: max line length!
				return -1;
		}
		if (source_addr >= 0) {
			source_addr += i - ptrpos;
			if (nextptr != source_addr)
				return -1;
		}
		if (target_addr >= 0) {
			target_addr += i - ptrpos;
			if (nextptr != target_addr) {
				data[ptrpos]     = target_addr  & 0xFF;
				data[ptrpos + 1] = target_addr >> 8;
				relink_counter++;
			}
		}
	}
	if (actual_size)
		*actual_size = i;
	return relink_counter;
}


int prg_load_prepare_inject ( const char *file_name, int new_address )
{
	prg_inject.phase = 0;
	if (!file_name)
		return -1;
	memset(prg_inject.data, 0, sizeof prg_inject.data);
	prg_inject.size = xemu_load_file(file_name, prg_inject.data, 4, sizeof(prg_inject.data) - 4, "Cannot load program");
	if (prg_inject.size <= 0)
		return -1;
	if (prg_inject.size <= 4) {
		ERROR_WINDOW("PRG is empty");
		return -1;
	}
	const int old_address = prg_inject.data[0] + (prg_inject.data[1] << 8);
	DEBUGPRINT("PRG: program \"%s\" load_addr=$%04X, new_load_addr=$%04X, size=%d" NL, file_name, old_address, new_address, prg_inject.size);
	int linked_size;
	const int relink_status = prg_relink(prg_inject.data + 2, new_address, -1, prg_inject.size - 2, &linked_size);
	if (relink_status < 0) {
		ERROR_WINDOW("Invalid PRG, bad BASIC link chain");
		return -1;
	}
	DEBUGPRINT("PRG: relink points=%d, size_from_linking=%d" NL, relink_status, linked_size);
	const int tail = prg_inject.size - 2 - linked_size;
	if (tail < 0) {	// technically it shouldn't happen ever, since prg_relink above should return then with relink_status < 0
		ERROR_WINDOW("Too short?!");
		return -1;
	}
	if (tail)
		DEBUGPRINT("PRG: PRG has non-BASIC tail of %d byte(s)" NL, tail);
	else
		DEBUGPRINT("PRG: PRG has no non-BASIC tail" NL);
	return 0;
}


int prg_load_do_inject ( const int address )
{
	// TODO: find out some way to detect if BASIC is unning at the moment, and if we're in screen-edit mode (not like running a program or something)
	if (memory[0x65] != (address & 0xFF) || memory[0x66] != (address >> 8)) {
		ERROR_WINDOW("Could not inject PRG into memory:\nwrong BASIC start address in ZP: $%04X (should be: $%04X)", memory[0x65] + (memory[0x66] << 8), address);
		return -1;
	}
	const int memtop = memory[0x71] + (memory[0x72] << 8);
#if 0
	int previous_size = -1;
	// NOTE: this is *NOT* relink the program. It *CHECKS* links of the program in the memory BEFORE injecting the new program which overwrites memory.
	// The only intent of this step is try to be sure that we're in BASIC mode at all hoping that "no answer" would mean, these checks fail.
	const int prev_status = prg_relink(memory + address, -1, address, memtop - address, &previous_size);
	DEBUGPRINT("PRG: previous status: %d, previous size: %d" NL, prev_status, previous_size);
#endif
	const int last_addr = address + prg_inject.size + 0x100; 	// 0x100: margin of error for now ;) TODO
	if (last_addr >= memtop) {
		ERROR_WINDOW("Program is too large to fit into the available BASIC memory\nMEMTOP = $%04X, last_load_byte = $%04X", memtop, last_addr);
		return -1;
	}
	memory[address - 1] = 0;	// I *think* that byte should be zero. I'm not absolutely sure though.
	memcpy(memory + address, prg_inject.data + 2, prg_inject.size - 2);
	memory[0x65] = address & 0xFF;
	memory[0x66] = address >> 8;
	const int end_address = address + prg_inject.size - 2;
	memory[0x67] = end_address & 0xFF;
	memory[0x68] = end_address >> 8;
	memory[0x69] = end_address & 0xFF;
	memory[0x6A] = end_address >> 8;
	memory[0x6B] = end_address & 0xFF;
	memory[0x6C] = end_address >> 8;
	memory[0x6D] = memory[0x71];
	memory[0x6E] = memory[0x72];
	return 0;
}


int prg_save_prepare_store ( const int address )
{
	prg_inject.phase = 0;
	if (memory[0x65] != (address & 0xFF) || memory[0x66] != (address >> 8)) {
		ERROR_WINDOW("SAVE error: wrong start addr ($%02X%02X) of BASIC ZP (should be: $%04X)", memory[0x66], memory[0x65], address);
		return -1;
	}
	const int memtop = memory[0x67] + (memory[0x68] << 8);
	const int size = memtop - address;	// size of the current program, without the load address
	if (size < 2 || size >= sizeof(prg_inject.data) - 2) {
		ERROR_WINDOW("SAVE error: Bad end of prg ptr in BASIC ZP");
		return -1;
	}
	if (size <= 4) {
		ERROR_WINDOW("SAVE error: no BASIC PRG in memory");
		return -1;
	}
	// use the relink functionality to check validity of memory
	int linked_size;
	const int ret = prg_relink(memory + address, -1, address, size, &linked_size);
	if (ret) {
		ERROR_WINDOW("SAVE error: in-memory program has invalid link structure, retcode: %d", ret);
		return -1;
	}
	DEBUGPRINT("SAVE: size from pointers = %d, size from link-check = %d" NL, size, linked_size);
	// hijack prg_inject.data for our purposes here ...
	prg_inject.data[0] = address & 0xFF;
	prg_inject.data[1] = address >> 8;
	memcpy(prg_inject.data + 2, memory + address, size);
	prg_inject.size = size + 2;
	return 0;
}




void prg_inject_periodic_check ( void )
{
	static const Uint8 screen_sample1[] = { 0x20, 0x03, 0x0f, 0x0d, 0x0d, 0x0f, 0x04, 0x0f, 0x12, 0x05, 0x20, 0x0c, 0x03, 0x04, 0x20 };
	static const Uint8 screen_sample2[] = { 0x12, 0x05, 0x01, 0x04, 0x19, 0x2e };
	if (
		!memcmp(memory + 0x880, screen_sample1, sizeof screen_sample1) &&
		!memcmp(memory + 0x980, screen_sample2, sizeof screen_sample2)
	) {
		prg_inject.phase = 2;
		DEBUGPRINT("BASIC: startup screen detected, injecting loaded basic program!" NL);
		prg_load_do_inject(BASIC_START);
		memory[0xA01] = 'R' - 'A' + 1;
		memory[0xA02] = 'U' - 'A' + 1;
		memory[0xA03] = 'N' - 'A' + 1;
	}
}
