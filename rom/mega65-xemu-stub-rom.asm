; --------------------------------------------------------------------
;   Part of the Xemu project.  https://github.com/lgblgblgb/xemu
;   Copyright (C)2016-2021 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
; --------------------------------------------------------------------
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
; Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
; --------------------------------------------------------------------
;
; Assemble: cl65 -t none
; (cl65 and ca65 is from the cc65 package)
;
; This is only "part" of Xemu's MEGA65 stub ROM! The rest is done by
; emulator code itself, see rom.c in targets/mega65/ to avoid the need
; for the text to be maintained here, and also to warp to line sizes
; and so on. But all the actual code is here.


.SETCPU "4510"

.ORG	$E000

scroll_table_lo_bytes = $200
scroll_table_hi_bytes = $300

	JMP	reset
	JMP	nmi
	JMP	irq

nmi:
	PHA
	LDA	$D2FF
	INA
	AND	#$F
	STA	$D2FF
	PLA
irq:
	RTI



reset:
	; Disable interrupts
	SEI
	; Z is zero!!! Till it's not, later ;)
	LDZ	#0
	; Knock for VIC-IV mode
        LDA	#$47
        STA	$D02F
        LDA	#$53
        STA	$D02F

	; Be sure about CPU speed
	LDA	#65
	STA	0

	LDA	#$80
	TSB	$D05D	; make sure hot registers are enabled
	TSB	$D06F	; NTSC; I prefer PAL, but somehow it looks better in NTSC. Well, it's an emulator, so it does make too much difference to stick with NTSC or PAL

	LDA	#3	; set legacy VIC bank, just to be sure
	TSB	$DD00

	STZ	$D015	; disables sprites (just in case)

	LDA	#$0B 	; -$10: display enable if OFF
	STA	$D011	; Screen control register #1
	LDA	#$C9
	STA	$D016	; Screen control register #2
	LDA	#39
	STA	$D018	; memory ptrs
	LDA	#4
	STA	$D030	; enable Palette-ROM
	LDA	#$80
	STA	$D031	; H640

	; Execute DMA for filling colour RAM
	LDA	#2	; we are at bank-2 with the C64 ROMs!
	STA	$D702	; very high byte
	LDA	#.HIBYTE(dma_list)
	STA	$D701	; middle byte
	LDA	#.LOBYTE(dma_list)
	STA	$D705	; trigger DMA with MEGA65 specific enhanced mode DMA (thus not $D700 is written but $D705)

	; Create our scroll stuff table.
	LDA	#3	; text bank (second 64K of the ROM, bank 3)
	STA	4
	STZ	5
	TZA
	TAX
	TAY
@make_tab:
	STA	scroll_table_lo_bytes,X
	STA	2
	STY	scroll_table_hi_bytes,X
	STY	3
	PHA
	NOP	; with the next opcode, special 32-bit fetch
	LDA	(2),Z
	CMP	#$FF
	BEQ	@end_tab
	PLA
	INX
	CLC
	ADC	#80
	BCC	@make_tab
	INY
	BRA	@make_tab
@end_tab:
	TXA
	SEC
	SBC	#24
	STA	3	; max scroll stuff

	; set background and border to colour index $FF, since we'll modify that colour for our raster effect
	LDA	#$FF
	STA	$D020
	STA	$D021
	; Set red and green component of palette index $FF
	STZ	$D1FF
	STZ	$D2FF

	; Delay the raster effect by approx 1 sec
	LDX	#128
:	LDA	$D7FA
:	CMP	$D7FA
	BEQ	:-
	LDA	pal_sine,X
	STA	$D3FF
	DEX
	DEX
	BPL	:--

	; Some "nice" raster (but not IRQ) based colour gradient
	; No need to be fancy, just to have something. Infinite loop.

	LDA	#$10
	TSB	$D011	; display is enabled at this point
	LDA	#3
	STA	$D062	; VIC-IV precise address of screen, highest byte, displaying bank3 (second 64K of the ROM)

	STZ	2
	TZA
	TAX
	TAY
@new_frame_on_scroll:
	TYA
	CLC
	ADC	2
	CMP	#$FF
	BEQ	@new_frame
	CMP	3
	BEQ	@new_frame
	STA	2
	TAY
	LDA	scroll_table_lo_bytes,Y
	STA	$D060	; VIC-IV precise address of screen, low byte
	STA	$D064	; VIC-IV colour ram base addr
	LDA	scroll_table_hi_bytes,Y
	STA	$D061
	STA	$D065
@new_frame:
	LDY	$D7FA	; frame counter
@raster_loop:
	LDA	pal_sine,X
	STA	$D3FF
	INX
	; wait for next raster
	LDA	$D012
:	CMP	$D012
	BEQ	:-
	; Still the same frame?
	CPY	$D7FA
	BEQ	@raster_loop
	INZ
	TZA
	TAX
	LDA	$D610		; any key?
	BEQ	@new_frame	; no key is pressed
	STA	$D610		; write something to pull that from the "queue"
	CMP	#'s'
	BEQ	easter_egg
	LDY	#$FF
	CMP	#145		; cursor-up key
	BEQ	@new_frame_on_scroll
	LDY	#$01
	CMP	#17		; cursor-down key
	BEQ	@new_frame_on_scroll
	BRA	@new_frame


easter_egg:
	LDZ	#0
	STZ	$D020
	STZ	$D021
:	BRA	:-



pal_sine:
	.BYTE $00,$30,$60,$90,$C0,$F0,$21,$51,$81,$C1,$F1,$22,$52,$82,$B2,$E2
	.BYTE $13,$43,$73,$A3,$D3,$04,$44,$74,$A4,$D4,$F4,$25,$55,$85,$B5,$E5
	.BYTE $16,$46,$76,$A6,$D6,$F6,$27,$57,$87,$A7,$D7,$08,$38,$58,$88,$B8
	.BYTE $D8,$09,$29,$59,$79,$A9,$C9,$F9,$1A,$4A,$6A,$8A,$BA,$DA,$FA,$2B
	.BYTE $4B,$6B,$8B,$AB,$CB,$FB,$1C,$3C,$5C,$7C,$9C,$AC,$CC,$EC,$0D,$2D
	.BYTE $4D,$5D,$7D,$9D,$AD,$CD,$DD,$FD,$0E,$2E,$3E,$5E,$6E,$7E,$9E,$AE
	.BYTE $BE,$CE,$DE,$FE,$0F,$1F,$2F,$3F,$4F,$4F,$5F,$6F,$7F,$8F,$8F,$9F
	.BYTE $AF,$AF,$BF,$BF,$CF,$CF,$DF,$DF,$DF,$EF,$EF,$EF,$EF,$EF,$EF,$EF
	.BYTE $FF,$EF,$EF,$EF,$EF,$EF,$EF,$EF,$DF,$DF,$DF,$CF,$CF,$BF,$BF,$AF
	.BYTE $AF,$9F,$8F,$8F,$7F,$6F,$5F,$4F,$4F,$3F,$2F,$1F,$0F,$FE,$DE,$CE
	.BYTE $BE,$AE,$9E,$7E,$6E,$5E,$3E,$2E,$0E,$FD,$DD,$CD,$AD,$9D,$7D,$5D
	.BYTE $4D,$2D,$0D,$EC,$CC,$AC,$9C,$7C,$5C,$3C,$1C,$FB,$CB,$AB,$8B,$6B
	.BYTE $4B,$2B,$FA,$DA,$BA,$8A,$6A,$4A,$1A,$F9,$C9,$A9,$79,$59,$29,$09
	.BYTE $D8,$B8,$88,$58,$38,$08,$D7,$A7,$87,$57,$27,$F6,$D6,$A6,$76,$46
	.BYTE $16,$E5,$B5,$85,$55,$25,$F4,$D4,$A4,$74,$44,$04,$D3,$A3,$73,$43
	.BYTE $13,$E2,$B2,$82,$52,$22,$F1,$C1,$81,$51,$21,$F0,$C0,$90,$60,$30

dma_list:
	; --- copy colour data into the colour RAM
	.BYTE	$0A	; MEGA65 enhanced mode list: F018A style DMA list (shorter)
	.BYTE	$81,$FF	; target megabyte slice option to $FF
	.BYTE	$00	; end of enhanced option list.
	.BYTE	0	; command byte: 0 = copy ( no chain)
	.WORD	$8000	; DMA length, 32K
	.WORD	$8000	; DMA source addr 16 bits
	.BYTE	3	; DMA source addr high bits+others (bank3, second 64K of the ROM)
	.WORD	0	; DMA target addr 16 bits
	.BYTE	8	; DMA target addr high bits+others
	; MISSING MODULO WORD! Do not do this ever, only maybe at the LAST entry (ie, not chained!), if modulo was NOT used!!
