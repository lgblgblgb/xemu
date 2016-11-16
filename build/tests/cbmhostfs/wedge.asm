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

;.SETCPU "4510"

DEVICE		= 7	; Our device number we would like to use
RELOCATED_TO	= $C000

.ORG  $7FF
.SCOPE
	.WORD first
first:
	.WORD next, 0
	.BYTE $9E
	.BYTE $30 + .LOBYTE( start_loader             /1000)
	.BYTE $30 + .LOBYTE((start_loader .MOD 1000 ) / 100)
	.BYTE $30 + .LOBYTE((start_loader .MOD 100  ) /  10)
	.BYTE $30 + .LOBYTE( start_loader .MOD 10          )
	.BYTE 0
next:
	.WORD 0
.ENDSCOPE



; ****************************************************************************
;                               T H E   S T U F F
; ****************************************************************************

; Cheap'n'dirty way to try to not use a decent linker config file for this purpose ...
RELOCATED_FROM:
.ORG RELOCATED_TO

; LOAD. Load or verify file. (Must call SETLFS and SETNAM beforehands.)
; Input: A: 0 = Load, 1-255 = Verify; X/Y = Load address (if secondary address = 0).
; Output: Carry: 0 = No errors, 1 = Error; A = KERNAL error code (if Carry = 1); X/Y = Address of last byte loaded/verified (if Carry = 0).
; Used registers: A, X, Y.
; Real address: $F49E (this uses vector $330 to jump to $F4A5 by default after storing X/Y to $C3/$C4 to ZP locs)
; It seems the actual code at $F4A5 does not use X/Y but from $C3/C4 already. However register A is still important LOAD/VERIFY flag)
;
; SETNAM stores: $B7 = filename length, $BB = low byte of file name pointer, $BC = high byte of filename pointer
; SETLFS stores: $B8 = logical number, $BA = device number, $B9 = secondary address

; !!! TODO FIXME TODO FIXME TODO FIXME TODO FIXME !!!!
; This is a naive implementation! Only LOAD is implemented in "once".
; Buggy :( I couldn't load programs (at least not always ...) via HostFS to really work for some reason ...
; There is not even the usual SEARCHING FOR and LOADING messages that is (maybe this is a problem), since I don't call such a things.
; The code is not even optimal, and should use 65CE02 opcodes as well.
; Also, using $C000 memory area maybe is not the best idea.
; VERIFY is not implemented at all.
; !!! TODO FIXME TODO FIXME TODO FIXME TODO FIXME !!!!



.PROC xemu_load_routine
	STA	$93		; store LOAD/VERIFY flag
	CMP	#0
	BNE	old_load	; Ugly enough, but now I handle LOAD only and *NOT* verify ...
	LDA	$BA		; device number set
	CMP	#DEVICE		; compare with our "custom" device number
	BEQ	new_load
old_load:
	LDA	$93		; continue with the good old ROM LOAD
old_routine_address = * + 1
	JMP	$FFFF
the_error:
io_error:
	LDA	#4		; file not found
some_error:
	STA	$D02F		; disable VIC-III I/O mode (any byte will do)
	SEC
	RTS
new_load:
	LDA	#0
	STA	$90		; I/O status byte zeroed
	; Enable VIC-III I/O mode
	LDA	#$A5
	STA	$D02F
	LDA	#$96
	STA	$D02F
	; Send file name was set by SETLFS before LOAD
	LDY	#0		; Sets Xemu hostFS to name specification mode
	STY	$D0FE
send_file_name:
	CPY	$B7		; filename length
	BEQ	end_of_file_name
	LDA	($BB), Y
	STA	$D0FF
	INY
	BNE	send_file_name
end_of_file_name:
	LDA	$B9		; SA
	AND	#15
	ORA	#$10
	STA	$D0FE		; Send Xemu open request (high nibble = 1, low nibble = channel aka SA, FIXME: I have no idea what happend if SA > 15 is used, as CBM DOS has only those ... TODO)
	LDX	$D0FE		; check Xemu status
	BNE	the_error
	AND	#$0F
	ORA	#$40
	STA	$D0FE		; Sets Xemu to "use channel" request mode (it should be Okey)
	; Okey, we have now open channel, all we need is to read $D0FF (and/or check $D0FE for status)
	LDX	$D0FF		; read load address low byte from file
	LDY	$D0FF		; read load address high byte from file
	LDA	$D0FE		; check Xemu status
	BNE	the_error
	LDA	$B9		; check SA
	BNE	use_file_load_spec
	LDX	$C3		; override values just read from file
	LDY	$C4		; -- "" --
use_file_load_spec:
	STX	$AE		; store our pointer finally ...
	STY	$AF
	; Now the loader loop is here!!!!
	; Since now we only support LOAD (not VERIFY), I even not care
	; about ROM/RAM switching, as writing memory would "write through
	; ROM to RAM".
	LDY	#0
loader_loop:
	LDA	$D0FF		; read file
	LDX	$D0FE		; check status (currently in Xemu, you MUST check this *AFTER* trying to read, if EOF, the read byte is invalid!)
	BNE	end_of_loop	; break loop if status is not zero (let's check the status outside of the loop, EOF or error)
	STA	($AE), Y	; write byte into memory
	INC	$AE		; using "Y" and then adding after the loop would be faster, but who cares now
	BNE	loader_loop
	INC	$AF
	BNE	loader_loop
end_of_loop:
	; Close Xemu HostFS channel
	LDA	$B9
	AND	#15
	ORA	#$30
	STA	$D0FE		; Xemu to close its channel
	; Check if status was an error
	TXA
	BMI	io_error
	; Everything seems to be OK, it's time to return soon ...
	STA	$D02F		; turn VIC-III mode off (any byte will do it)
	; In my braindead implementation I have the load pointer AFTER the last byte
	; Kernal DOCS says, it must point to the last, so I have to subtract one??
	; I guess ...
	; Note: later Xemu HostFS can become problematic, that checking EOF situation
	; can only be done _AFTER_ a byte is tried to be read, but we cannot tell
	; BEFORE a read if it's EOF or not ...
	LDY	$AF
	LDX	$AE
	BNE	nodechi
	DEY
	STY	$AF
nodechi:
	DEX
	STX	$AE
	CLC			; no error
	RTS
.ENDPROC


; ****************************************************************************
;                                 L O A D E R
; ****************************************************************************


.ORG * - RELOCATED_TO + RELOCATED_FROM


.PROC start_loader
	SEI
	LDX	#0
	; 1K should be enough for everyone.
copy_sys:
	LDA	RELOCATED_FROM + $000, X
	STA	RELOCATED_TO   + $000, X
	LDA	RELOCATED_FROM + $100, X
	STA	RELOCATED_TO   + $100, X
	LDA	RELOCATED_FROM + $200, X
	STA	RELOCATED_TO   + $200, X
	LDA	RELOCATED_FROM + $300, X
	STA	RELOCATED_TO   + $300, X
	INX
	BNE	copy_sys
	LDA	$330
	STA	xemu_load_routine::old_routine_address
	LDA	$331
	STA	xemu_load_routine::old_routine_address + 1
	LDA	#.LOBYTE(xemu_load_routine)
	STA	$330
	LDA	#.HIBYTE(xemu_load_routine)
	STA	$331
	CLI
	STX	53281
eol:
	LDA	msg, X
	BEQ	eos
	JSR	$FFD2
	INX
	BNE	eol
eos:
	RTS
msg:
	.BYTE	"WEDGE FOR LOAD INSTALLED",0
.ENDPROC
