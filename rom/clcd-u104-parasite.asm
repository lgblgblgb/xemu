; This is a 6K (padded to ...) sized Commodore LCD ROM just for testing.
; Can be compiled with CA65 assembler from the CC65 suite.
; See the Makefile in this directory about more details, and
; commodore_lcd.c in the project root directory.
;
; Commodore LCD emulator.
; Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
;
;    This is an ongoing work to rewrite my old Commodore LCD emulator:
;
;       * Commodore LCD emulator, C version.
;       * (C)2013,2014 LGB Gabor Lenart
;       * Visit my site (the better, JavaScript version of the emu is here too): http://commodore-lcd.lgb.hu/
;
;    The goal is - of course - writing a primitive but still better than previous Commodore LCD emulator :)
;    Note: I would be interested in VICE adoption, but I am lame with VICE, too complex for me :)
;
;    This emulator based on my previous try (written in C), which is based on my previous JavaScript
;    based emulator, which was the world's first Commodore LCD emulator.
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

START	= $4000

.ORG START

; ROM identifier and Co.

.BYTE $00,$00,$FF,$FF,$6,$04,$12,$85
.BYTE "Commodore LCD"	; identifier string

; "Application directory" stuff ...

.BYTE 9 + 6		; length: 6 bytes + name length!
.BYTE $04, $10, $00	; I am not sure what are these :-/
.WORD myapp		; Entry point of our app
.BYTE "XCLCD.LGB"	; Name of our app ("extension" is also needed!)
.BYTE 0			; end of directory (this would be the next entry's length parameter)


; Test stuff, endless loop
myapp:
	STX	$800
	STX	$800 + 128 + 1
	INX
	JMP	myapp


.REPEAT	($8000 - $6800) - (* - START)
.BYTE $FF
.ENDREPEAT
