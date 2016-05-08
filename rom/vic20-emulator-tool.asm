; This is a 8K (padded to ...) sized memory image for the Commodore VIC20,
; which is tried to be loaded to $A000. It behaves like an autostart capable
; cartridge. Can be compiled with CA65 assembler from the CC65 suite.
; See the Makefile in this directory about more details.
;
; Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
;
; This program is free software; you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation; either version 2 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, write to the Free Software
; Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

.SETCPU	"6502"

START	= $A000
SIZE	= 8192

CHROUT	= $FFD2


.MACRO	TRAP	n
	.BYTE	$FC	; emulator trap
	.BYTE	n	; emulator trap number
.ENDMACRO


; *** Do not modify anything here, especially not in a way that addresses of these stuffs changes ...

.ORG	START

	.WORD	rom_entry_initial	; autostart ROM initial entry vector (at $A000)
	.WORD	rom_entry_break		; autostart ROM break entry vector   (at $A002)
	.BYTE	"A0",$C3,$C2,$CD	; AUTOSTART id string (at $A004)

	JMP	rom_entry_sys		; used in case of a SYS command (do things "manually") (at $A009)

	.BYTE	"LGBXVIC20"		; identifier string for the emulator to be sure if it's our "fake" ROM ... (at $A00C)
	.WORD	0			; ROM version for the emulator ... (at $A015)

	.WORD	EMU_BUFFER		; "export" EMU buffer address for the emulator trap handler ... (at $A017)
	.WORD	EMU_BUFFER_SIZE		; "export" EMU buffer size for the emulator trap handler (at $A019)

; *** OK, now you can modify things from this point ...

prntstr:
	LDA	#.LOBYTE(EMU_BUFFER)
	STA	251
	LDA	#.HIBYTE(EMU_BUFFER)
	STA	252
	LDY	#0
@loop:
	LDA	(251), Y
	BEQ	@eos
	JSR	CHROUT
	INY
	BNE	@loop
	INC	252
	BNE	@loop
@eos:
	RTS


getline:
	LDY	#0
@loop:
	JSR	$FFCF
	STA	512, Y
	INY
	CPY	#88
	BEQ	getline	; too long line, todo.
	CMP	#$0D
	BNE	@loop
	LDA	#0
	STA	511, Y
	RTS


rom_entry_initial:
	SEI
	; Similar to the KERNAL, intiailize the machine first
	JSR	$FD8D	; intiailize and test RAM
	JSR	$FD52	; restore I/O vectors
	JSR	$FDF9	; initialize I/O registers
	JSR	$E518	; initialize hardware
	CLI
	TRAP	0	; check if autostart functionality is allowed
	ORA	#0
	BEQ	@no_autostart
	JSR	rom_entry_sys	; we do this, to allow to return with RTS as done via the "SYS" method of monitor invocation too
@no_autostart:
	JMP	($C000)	; execute BASIC ...

rom_entry_sys:
	CLI		; enable interrupts
	JSR	$FFE7	; close possible open files, etc
	TRAP	1	; welcome msg
	JSR	prntstr
@loop:
	JSR	getline
	TRAP	2
	PHA		; store continue flag
	JSR	prntstr
	PLA
	BNE	@loop
	RTS

; If I unserstand well, it's called on NMI (?)
; Dunno about it, better to pass control to the KERNAL instead :-P
rom_entry_break:
	JMP	$FEC7	; let's pass the control to KERNAL, as we wouldn't be here at all :-)


; *** Padding ROM image size to 8K, do not put *ANYTHING* after this comment line!
; This is also used as the communication buffer, to print things given by the emulator
EMU_BUFFER:
	.REPEAT	SIZE - (* - START)
	.BYTE	$FF
	.ENDREPEAT
EMU_BUFFER_SIZE = * - EMU_BUFFER
