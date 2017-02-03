/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2015 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

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


; This ROM is *INCLUDED* in Xep128 by default and have strong relation
; with the _current_ version of the emulator. it would crash on a real EP,
; also with any other version of Xep128 what is part of. That's the reason
; it's included inside the Xep128 executable itself.
;
; Following functionalities is done by this ROM:
;
; * provides processing of :XEP commands
; * sets EXOS clock on restart
; * handles the FILE: functionality to load programs from host OS FS
; * provides information (ie: EXOS version) for the emulator


; Note, symbols having name starting with "xepsym_" are extracted
; into a .h file can can be used by Xep128 itself!

; Choose an unused ED XX opcode for our trap, which is also not used on Z180, just in case of Z180 mode selected for Xep128 :)
; This symbol is also exported for the C code, so the trap handler will recognize it
xepsym_ed_trap_opcode	= 0xBC

xepsym_default_device_name_string = $FFE0
xepsym_exos_info_struct	= $FFF0

; Great help: http://ep.homeserver.hu/Dokumentacio/Konyvek/EXOS_2.1_technikal_information/exos/kernel/Ch6.html
;             http://ep.homeserver.hu/Dokumentacio/Konyvek/EXOS_2.1_technikal_information/exos/kernel/Ch7.html

DEFINE PAGE1_ADDR(a) (a & 0x3FFF) | 0x4000

	ORG	0xC000
	DB	"EXOS_ROM"
	DW	PAGE1_ADDR(fileio_device_pseudo_descriptor.rom_ref)	; device chain (0 = no), otherwise address, "seen as" in the 0x4000 - 0x7FFF Z80 page (page 1)
	JP	rom_main_entry_point
	DB	"[XEProm]"	; Please leave this here, in this form! It may be used to detect XEP ROM in the future!

; Standard EXOS call macro
MACRO	EXOS n
	RST	0x30
	DB	n
ENDMACRO
; This macro is used to place a trap, also creating a symbol (after the trap yes, since that PC will be seen by the trap handler)
; Value of "sym" should be start with xepsym_ so it's exported as a symbol for the C code! And it must be handled there too ...
MACRO	TRAP sym
	DB 0xED, xepsym_ed_trap_opcode
sym = $
ENDMACRO



fileio_device_pseudo_descriptor:
	DW	0	; link to the next descriptor (0 = none)
	DW	$FFFE	; needed RAM amount (-2 = no RAM needed)
.type
	DB	0	; type
	DB	0	; IRQ flag
	DB	0	; flags
	DW	PAGE1_ADDR(.jump_table)
	DB	0	; segment of jump table, will be filled by EXOS when descriptor is copied to RAM!
	DB	0	; unit count for this device
.device_fn:
	DB	4, "FILE"	; name, with its size
.rom_ref:
	DB	.rom_ref - .type	; size!
.jump_table:
	DW	fileio_not_used_call	; interrupt, not valid, since IRQ flag is 0 in the descriptor
	DW	fileio_open_channel
	DW	fileio_create_channel
	DW	fileio_close_channel
	DW	fileio_destroy_channel
	DW	fileio_read_character
	DW	fileio_read_block
	DW	fileio_write_character
	DW	fileio_write_block
	DW	fileio_channel_read_status
	DW	fileio_set_channel_status
	DW	fileio_special_function
	DW	fileio_init
	DW	fileio_buffer_moved
	DW	fileio_not_used_call
	DW	fileio_not_used_call

xepsym_device_fn = fileio_device_pseudo_descriptor.device_fn

fileio_not_used_call:
	TRAP	xepsym_fileio_no_used_call
	RET

allocate_channel_buffer:
	TRAP	xepsym_fileio_open_channel_remember	; this will store A register as channel number in fileio.c
	PUSH	DE
	PUSH	IX
	LD	DE, 1
	EXOS	27		; allocate channel buffer RAM
	POP	IX
	POP	DE
	RET			; A (error code of EXOS 27) will be checked by the caller

fileio_open_channel:
	CALL	allocate_channel_buffer	; A (error code) will be checked by the next trap!
	TRAP	xepsym_fileio_open_channel
	RET

fileio_create_channel:
	CALL	allocate_channel_buffer	; A (error code) will be checked by the next trap!
	TRAP	xepsym_fileio_create_channel
	RET

fileio_close_channel:
	TRAP	xepsym_fileio_close_channel
	RET

fileio_destroy_channel:
	TRAP	xepsym_fileio_destroy_channel
	RET

fileio_read_character:
	TRAP	xepsym_fileio_read_character
	RET

fileio_read_block:
	TRAP	xepsym_fileio_read_block
	RET

fileio_write_character:
	TRAP	xepsym_fileio_write_character
	RET

fileio_write_block:
	TRAP	xepsym_fileio_write_block
	RET

fileio_channel_read_status:
	TRAP	xepsym_fileio_channel_read_status
	RET

fileio_set_channel_status:
	TRAP	xepsym_fileio_set_channel_status
	RET

fileio_special_function:
	TRAP	xepsym_fileio_special_function
	RET

fileio_init:
	TRAP	xepsym_fileio_init
	RET

fileio_buffer_moved:
	TRAP	xepsym_fileio_buffer_moved
	RET




rom_main_entry_point:
	; The following invalid ED opcode is handled by the "ED trap" of the CPU emulator in Xep128
	; The trap handler itself will check EXOS action code, register values, and may modify
	; registers C and/or A as well. Also, it can pass data through the ROm area :) from 0xF8000
	TRAP	xepsym_trap_exos_command
	; Argument of "JP" (the word at xepsym_jump) is filled/modified by Xep128 on the ED trap above!
	; Usually it's set to xepsym_print_xep_buffer if there is something to print at least :)
xepsym_jump_on_rom_entry = $ + 1
	JP	0

xepsym_print_xep_buffer:
	; Let save registers may be used by the check to print anything and/or print stuff
	; This also includes the possibly already modified A/C by the ED trap above!
	PUSH	AF
	PUSH	BC
	PUSH	DE
.nopush:
.store_length = $ + 1
	LD	BC, 0		; will be modified by Xep128
	LD	A, C
	OR	B
	JR	Z, xepsym_pop_and_ret
	LD	DE, xepsym_cobuf	; the ED trap modifies memory from here xepsym_cobuf
	LD	A, 0xFF		; default channel
	EXOS	8		; write block EXOS function
xepsym_pop_and_ret:
	POP	DE
	POP	BC
	POP	AF
xepsym_ret:
	RET
xepsym_print_size = xepsym_print_xep_buffer.store_length


; Enable write of the ROM.
; Note: on next TRAP R/O access will be restored!
enable_write:
	TRAP	xepsym_trap_enable_rom_write
	RET


; Set EXOS time/date.
; Caller should write the register fill values first ...
set_exos_time:
xepsym_settime_hour = $ + 1
	LD	C, 0x88
xepsym_settime_minsec = $ + 1
	LD	DE, 0x8888
	EXOS	31		; EXOS set time
xepsym_setdate_year = $ + 1
	LD	C, 0x88
xepsym_setdate_monday = $ + 1
	LD	DE, 0x8888
	EXOS	33		; EXOS set date
	RET


xepsym_set_time:
	PUSH	AF
	PUSH	BC
	PUSH	DE
	CALL	set_exos_time
	JP	xepsym_print_xep_buffer.nopush


set_default_device_name:
.is_file_handler = $ + 1
	LD	C, 0
	LD	DE, xepsym_default_device_name_string
	LD	A, (DE)
	OR	A
	RET	Z	; if EXOS string is zero in its length, skip its setting
	EXOS	19
	PUSH	AF
	LD	DE, xepsym_error_message_buffer
	CALL	enable_write
	EXOS	28
	POP	AF
	TRAP	xepsym_trap_set_default_device_name_feedback
	RET

xepsym_set_default_device_name_is_file_handler = set_default_device_name.is_file_handler

xepsym_set_default_device_name:
	PUSH	AF
	PUSH	BC
	PUSH	DE
	CALL	set_default_device_name
	JP	xepsym_print_xep_buffer.nopush


; Called on system initialization (EXOS action code 8)
; Currently it just sets date/time as xepsym_set_time would do as well ...
xepsym_system_init:
	PUSH	AF
	PUSH	BC
	PUSH	DE
	CALL	set_exos_time
	LD	DE, xepsym_exos_info_struct
	CALL	enable_write
	EXOS	20
	TRAP	xepsym_trap_on_system_init ; also stores the version number, receives the info struct from EXOS
	CALL	set_default_device_name
	JP	xepsym_pop_and_ret

; Called on EXOS action code 1
xepsym_cold_reset:
	PUSH	AF
	PUSH	BC
	PUSH	DE
	CALL	set_default_device_name
	JP	xepsym_pop_and_ret


; **** !! YOU MUST NOT PUT ANYTHING EXTRA AFTER THIS LINE, EMULATOR OVERWRITES THE AREA !! ****
xepsym_error_message_buffer:
	DB	.def_msg_size
.def_msg:
	DB	"Unknown XEP ROM error"
.def_msg_size = $ - .def_msg
	ORG	xepsym_error_message_buffer + 64
xepsym_error_message_buffer_size = $ - xepsym_error_message_buffer
xepsym_cobuf:
xepsym_cobuf_size = 0xFFE0 - xepsym_cobuf
