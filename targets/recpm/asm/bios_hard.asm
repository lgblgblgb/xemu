ORG ORGI

CONSOLE_IN_PORT = $00
CONSOLE_OUT_PORT = $01
TRACK_NUM_LO_PORT = $02
TRACK_NUM_HI_PORT = $03
SECTOR_NUM_LO_PORT = $04
SECTOR_NUM_HI_PORT = $05
DISK_COMMAND_PORT = $06
SECTOR_BUFFER_DATA_PORT = $07


	JP	BIOS_BOOT	;-3: Cold start routine
BVECA:	JP	BIOS_WBOOT	; 0: Warm boot - reload command processor
	JP	BIOS_CONST	; 3: Console status
	JP	BIOS_CONIN	; 6: Console input
	JP	BIOS_CONOUT	; 9: Console output
	JP	BIOS_LIST	;12: Printer output
	JP	BIOS_PUNCH	;15: Paper tape punch output
        JP	BIOS_READER	;18: Paper tape reader input
	JP	BIOS_HOME	;21: Move disc head to track 0
	JP	BIOS_SELDSK	;24: Select disc drive
	JP	BIOS_SETTRK	;27: Set track number
	JP	BIOS_SETSEC	;30: Set sector number
	JP	BIOS_SETDMA	;33: Set DMA address
	JP	BIOS_READ	;36: Read a sector
	JP	BIOS_WRITE	;39: Write a sector
	JP	BIOS_LISTST	;42: Status of list device
	JP	BIOS_SECTRAN	;45: Sector translation for skewing


write_str:
	LD	A, (HL)
	INC	HL
	OR	A
	RET	Z
	LD	C, A
	CALL	BIOS_CONOUT
	JR	write_str

msg_welcome:
	DB	"Xemu::reCPM C-BIOS",13,10,0


BIOS_BOOT:
	DI
	LD	SP, $100
	LD	HL, msg_welcome
	CALL	write_str

	; *** FALL THROUGH TO BIOS_WBOOT, DO NOT PUT ANYTHING HERE ***

BIOS_WBOOT:
	LD	SP, $100
	LD	A, $C3	; JP opcode
	LD	(1), A

BIOS_CONST:
	IN	A, (CONSOLE_IN_PORT)
	OR	A
	RET	Z
	LD	A, $FF
	RET

BIOS_CONIN:
	IN	A, (CONSOLE_IN_PORT)
	OR	A
	JR	Z, BIOS_CONIN	; wait for data
	OUT	(CONSOLE_IN_PORT), A	; remove from input queue, written data is not important, just the fact of writing itself
	RET

BIOS_CONOUT:
	IN	A, (CONSOLE_OUT_PORT)
	OR	A
	JR	Z, BIOS_CONOUT	; wait for console is ready for output
	LD	A, C
	OUT	(CONSOLE_OUT_PORT), A
	RET

BIOS_LIST:
	RET			; do nothing for now ...

BIOS_LISTST:
	XOR	A		; A=0 -> not ready
	RET

BIOS_PUNCH:
	RET			; do nothing for now ...

BIOS_READER:
	LD	A, 26		; ^Z -> not implemented
	RET

BIOS_HOME:
	XOR	A
	LD	(track), A
	LD	(track+1), A
	RET

BIOS_SELDSK:

BIOS_SETTRK:
	LD	(track), BC
	RET

BIOS_SETSEC:
	LD	(sector), BC
	RET

BIOS_SETDMA:
	LD	(dma_address), BC
	RET






BIOS_READ:
	LD	C, 0		; read command
	CALL	disk_io
	RET	NZ		; return if A was not zero (eg: 1=error, $FF = media changed)
	; Read the sector buffer
	LD	BC, $8000 + SECTOR_BUFFER_DATA_PORT	; B=$80 ($80 bytes = 1 CP/M sector, C = SECTOR_BUFFER_DATA_PORT)
	LD	HL, (dma_address)
	INIR			; transfer 128 bytes from SECTOR_BUFFER_DATA_PORT to memory at dma_address ptr
	XOR	A		; no error
	RET


BIOS_WRITE:
	; Write the sector buffer
	LD	BC, $8000 + SECTOR_BUFFER_DATA_PORT	; B=$80 ($80 bytes = 1 CP/M sector, C = SECTOR_BUFFER_DATA_PORT)
	LD	HL, (dma_address)
	OTIR			; transfer 128 bytes from memory at dma_address ptr to SECTOR_BUFFER_DATA_PORT
	; do the disk write
	LD	C, 1		; write command

	; *** FALL THROUGH TO disk_io, DO NOT PUT ANYTHING HERE ***

disk_io:
	LD	A, (track)
	OUT	(TRACK_NUM_LO_PORT), A
	LD	A, (track+1)
	OUT	(TRACK_NUM_HI_PORT), A
	LD	A, (sector)
	OUT	(SECTOR_NUM_LO_PORT), A
	LD	A, (sector+1)
	OUT	(SECTOR_NUM_HI_PORT), A
	LD	A, C
	OUT	(DISK_COMMAND_PORT), A
io_wait:
	IN	A, (DISK_COMMAND_PORT)
	CP	A, $80
	JR	Z, io_wait
	OR	A		; to have the zero flag set/reset according to value of A
	RET			; now: A = OK (+ Zero flag is set), otherwise: some kind of error (+ Zero flag is reset)




BIOS_SECTRAN:			; no sector translation, return HL from input BC, as-is
	LD	L, C
	LD	H, B
	RET


track: DS 0
sector: DS 0
dma_address: DS $80


