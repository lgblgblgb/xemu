; Test program for Xemu/CBMhostFS, easily can be compiled with cl65 -t none
; This porgram only hooks on the LOAD kernal vector!
;
;   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
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

DEVICE = 7	; Our device number we would like to use

.ORG  $7FF
.SCOPE
	.WORD first
first:
	.WORD next, 0
	.BYTE $9E
	.BYTE $30 + .LOBYTE( start_basic_loader             /1000)
	.BYTE $30 + .LOBYTE((start_basic_loader .MOD 1000 ) / 100)
	.BYTE $30 + .LOBYTE((start_basic_loader .MOD 100  ) /  10)
	.BYTE $30 + .LOBYTE( start_basic_loader .MOD 10          )
	.BYTE 0
next:
	.WORD 0
.ENDSCOPE


vector_tab:
	.BYTE	$30
	.WORD	xemu_load
	.BYTE	0


start_basic_loader:
	SEI
	LDX	#0
@copy_sys:
	LDA	relocate, X
	STA	$C000, X
	LDA	relocate + $100, X
	STA	$C100, X
	LDA	relocate + $200, X
	STA	$C200, X
	LDA	relocate + $300, X
	STA	$C300, X
	INX
	BNE	@copy_sys
@copy_vec:
	LDY	vector_tab, X
	BEQ	@end_vec
	INX
	LDA	vector_tab, X
	INX
	STA	$300, Y
	LDA	vector_tab, X
	INX
	STA	$301, Y
	BNE	@copy_vec
@end_vec:
	LDA	#0
	STA	53281
	CLI
	RTS


; ****************************************************************************
;                               T H E   S T U F F
; ****************************************************************************


relocate:
.ORG $C000	

; LOAD. Load or verify file. (Must call SETLFS and SETNAM beforehands.)
; Input: A: 0 = Load, 1-255 = Verify; X/Y = Load address (if secondary address = 0).
; Output: Carry: 0 = No errors, 1 = Error; A = KERNAL error code (if Carry = 1); X/Y = Address of last byte loaded/verified (if Carry = 0).
; Used registers: A, X, Y.
; Real address: $F49E.
;
; SETNAM stores: $B7 = filename length, $BB = low byte of file name pointer, $BC = high byte of filename pointer
; SETLFS stores: $B8 = logical number, $BA = device number, $B9 = secondary address


.PROC xemu_load
	CMP	#0
	BNE	@old_load_jump	; Ugly enough, but now I handle LOAD only and *NOT* verify ...
	PHA
	LDA	$BA		; device number set
	CMP	#DEVICE		; compare with our "custom" device number
	BEQ	@new_load
@old_load:
	PLA			; continue with the good old ROM LOAD, if it's not our device number
@old_load_jump:
	JMP	$F4A5
@the_error:
@io_error:
	LDA	#4		; file not found
@some_error:
	STA	$D02F		; disable VIC-III I/O mode (any byte will do)
	SEC
	RTS
@new_load:
	PLA			; forget "A", we only handle LOAD, not VERIFY now :(
	; Be sure that secondary address is 0..15 (is this needed?)
	LDA	#15
	AND	$B9
	STA	$B9
	; Store X/Y (address to load if SA = 0)
	STX	$C3
	STY	$C4
	; Enable VIC-III I/O mode
	LDA	#$A5
	STA	$D02F
	LDA	#$96
	STA	$D02F
	; Send file name was set by SETLFS before LOAD
	LDY	#0		; Sets Xemu hostFS to name specification mode
	STY	$D0FE
@send_file_name:
	CPY	$B7		; filename length
	BEQ	@end_of_file_name
	LDA	($BB), Y
	STA	$D0FF
	INY
	JMP	@send_file_name
@end_of_file_name:
	LDA	$B9		; SA
	ORA	#$10
	STA	$D0FE		; Send Xemu open request!
	LDX	$D0FE		; check Xemu status
	BNE	@the_error
	AND	#$F
	ORA	#$40
	STA	$D0FE		; Sets Xemu to "use channel" request mode (it should be Okey)
	; Okey, we have now open channel, all we need is to read $D0FF (and/or check $D0FE for status)
	LDX	$D0FF		; read load address low byte from file
	LDY	$D0FF		; read load address high byte from file
	LDA	$D0FE		; check Xemu status
	BNE	@the_error
	LDA	$B9		; check SA
	BEQ	@use_fixed_load_spec
	STX	$C3		; Note: I am totally unsure if it's OK to use $C3/$C4 and even modify it!!!!!!
	STY	$C4
@use_fixed_load_spec:
	; Now the loader loop is here!!!!
	LDY	#0
@loader_loop:
	LDA	$D0FF		; read file
	BIT	$D0FE		; check status
	BVS	@end_of_file
	BMI	@io_error
	STA	($C3), Y	; write byte into memory
	INC	$C3		; using "Y" and then adding after the loop would be faster, but who cares now
	BNE	@loader_loop
	INC	$C4
	BNE	@loader_loop
@end_of_file:
	; Close channel
	LDA	$B9
	ORA	#$30
	STA	$D0FE		; Xemu to close its channel
	STA	$D02F		; turn VIC-III mode off (any byte will do it)
	CLC			; no error
	LDX	$C3
	LDY	$C4
	RTS
.ENDPROC
