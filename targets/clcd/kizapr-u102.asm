;============================================================================
;                  Commodore LCD Kernal ROM Disassembly
;============================================================================
; Taken from an EPROM labelled "kizapr-u102.bin" on Prototype CLCD
;
; Based on this disassembly by Gábor Lénárt:
; https://web.archive.org/web/20170419205827/http://commodore-lcd.lgb.hu/sk/

;=============================================================================
; Memory Map
;=============================================================================
; The machine has an 18-bit address space from $00000 to $3FFFF.
; The CPU only sees $00000 to $0FFFF.
; The MMU can map memory from the upper address space into the CPU space.
; The MMU always maps in the TOP of the KERNAL ROM and the bottom of RAM so
; that the normal reset vectors and KERNAL Jump table is available to handle
; Interupts and KERNAL calls, and the Zero Page and system RAM are always
; available to KERNAL and applications.
; The KERNAL calls map in any resources that it needs and restores the
; state so that the applications can function. The MMU manages READ/WRITE to
; the address space allowing the LCD and MMU registers to hide under the
; fixed KERNAL space. The IO space is always mapped in. The CPU address space
; is divided into KERNAL, four Application Windows, and the System RAM.
; The four Application Windows can contain ROM or RAM from any location in
; the extended address space.

; ADDRESS       SIZE    TYPE                    CONTENTS
; -------       ----    ----                    --------
; 30000-3FFFF   64K     ROM                     KERNAL, Character Set, Monitor
; 20000-2FFFF   64K     ROM                     Applications
; 10000-1FFFF   64K     Expansion RAM
; 00000-0FFFF   64K     Built-in RAM

; The CPU Address Space:
;                               /------------------------- MODES ------------------------\
; ADDRESS       SIZE    TYPE    RAM             APPL            KERN            TEST           NOTES
; -------       ----    ----    ---             ----            ----            ----           -----
; 0FA00-0FFFF   1.5K    Fixed   3FA00-3FFFF     3FA00-3FFFF     3FA00-3FFFF     3FA00-3FFFF    Top of KERNAL. Read from ROM, Write to LCD or MMU
; 0F800-0F9FF   0.5K    I/O     0F800-0F9FF     0F800-0F9FF     0F800-0F9FF     0F800-0F9FF    Fixed I/O (VIAs, ACIA, EXP)
; 0C000-0F7FF   14K     Banked  0C000-0F7FF     APPL Window 4   3C000-3F7FF     Offset 4/5     Configured via MMU
; 08000-0BFFF   16K     Banked  08000-0BFFF     APPL Window 3   38000-3BFFF     Offset 3       Configured via MMU
; 04000-07FFF   16K     Banked  04000-07FFF     APPL Window 2   KERN Window     Offset 2       Configured via MMU
; 01000-03FFF   12K     Banked  01000-03FFF     APPL Window 1   01000-03FFF     Offset 1       Configured via MMU
; 00000-00FFF   4K      Fixed   00000-00FFF     00000-00FFF     00000-00FFF     00000-00FFF    Always fixed

; LCD, MMU, IO Address:

; ADDRESS RANGE TYPE    ADDRESS DESCRIPTION                             NOTES
; ------------- ----    ------- -----------                             -----
; 0FF80-0FFFF   LCD     Custom Gate Array
;                       FF80    [0-6] X-Scroll
;                       FF81    [0-7] Y-Scroll
;                       FF82    [1] Graphics Enable, [0] Chararcter Select
;                       FF83    [5] Test Mode, [4] SS40, [3] CS80, [2] Chr Width

; 0FA00-0FFFF   MMU     Custom Gate Array
;                       FF00    KERN Window offset      (write only)    * Sets a pointer to any 1K boundary in the extended address range.
;                       FE80    APPL Window 4 offset    (write only)      The top 8 bits (A10-A17) of the extended address are written to
;                       FE00    APPL Window 3 offset    (write only)      any of these offset registers.
;                       FD80    APPL Window 2 offset    (write only)
;                       FD00    APPL Window 1 offset    (write only)

;                       FC80    Select TEST mode        (dummy write)   * Any write to these registers triggers the selected mode
;                       FC00    Save current mode       (dummy write)     (see above) or does a Save or Recall operation.
;                       FB80    Recall saved mode       (dummy write)
;                       FB00    Select RAM mode         (dummy write)
;                       FA80    Select APPL mode        (dummy write)
;                       FA00    Select KERN mode        (dummy write)

; 0F800-0F9FF   I/O     IO Chips and Expansion via rear connector
;                       F980    I/O#4   ACIA            RS-232, Modem
;                       F900    I/O#3                   External Expansion
;                       F880    I/O#2   VIA#2           Centronics, RTC, RS-232, Modem, Beeper, Barcode Reader
;                       F800    I/O#1   VIA#1           Keyboard, Battery Level, Alarm, RTC Enable, Power, IEC

; ----------------------------------------------------------------------------

        .setcpu "65C02"

        .org $8000

; ----------------------------------------------------------------------------
MEM_0015        := $0015
MEM_0041        := $0041
MEM_0081        := $0081
VidMemHi        := $00A0
CursorX         := $00A1
CursorY         := $00A2
WIN_TOP_LEFT_X  := $00A3
WIN_BTM_RGHT_X  := $00A4
WIN_TOP_LEFT_Y  := $00A5
WIN_BTM_RGHT_Y  := $00A6
QTSW            := $00A7  ;Quote mode flag (0=quote mode off, nonzero=on)
INSRT           := $00A8  ;Number of chars to insert (1 for each time SHIFT-INS/DEL is pressed)
INSFLG          := $00A9  ;Auto-insert mode flag (0=auto-insert off, nonzero=on)
MEM_00AA        := $00AA  ;Screen editor or maybe keyboard related
MEM_00AB        := $00AB  ;Keyboard scan related
MEM_00AC        := $00AC  ;Keyboard scan related
MODKEY          := $00AD  ;"Modifier" key byte read directly from keyboard shift register
FNADR           := $00AE
EAL             := $00B2
EAH             := $00B3
STAL            := $00B6
STAH            := $00B7
SAL             := $00B8
SAH             := $00B9
SATUS           := $00BA
VidPtrLo        := $00C1
VidPtrHi        := $00C2
SA              := $00C4
FA              := $00C5
LA              := $00C6
T0              := $00C7  ;2 bytes
T1              := $00C9  ;2 bytes
T2              := $00CB  ;2 bytes
CHRPTR          := $00CD
BUFEND          := $00CE
LENGTH          := $00CF
WRAP            := $00D0
TMPC            := $00D1
MSAL            := $00D2
V1541_FNADR     := $00E2  ;2 bytes
V1541_DEFAULT_CHAN := $00E6
V1541_ACTIV_FLAGS := $00E7  ; \
V1541_ACTIV_E8    := $00E8  ;  Active Channel
V1541_ACTIV_E9    := $00E9  ;  4 bytes
V1541_ACTIV_EA    := $00EA  ; /
BLNCT             := $00EF  ;Counter for cursor blink
CHAR_UNDER_CURSOR := $00F0  ;Character under the cursor; used with blinking
MEM_00F4          := $00F4  ;Keyboard scan related
MEM_00F5          := $00F5  ;Keyboard scan related
stack             := $0100
ROM_ENV_A         := $0204
ROM_ENV_X         := $0205
ROM_ENV_Y         := $0206
V1541_DATA_BUF    := $0218 ;basic line for dir listing, other unknown uses
V1541_CHAN_BUF    := $024D ;71 bytes, all data for all channels, see V1541_SELECT_CHANNEL_A
V1541_CMD_BUF     := $0295 ;command sent to command channel
V1541_CMD_LEN     := $02d5
V1541_02D6      := $02d6
V1541_02D7      := $02d7
LAT             := $02DB
SAT             := $02F3
FAT             := $02E7
MEM_0300        := $0300
RAMVEC_IRQ      := $0314   ;KERNAL RAM vectors, 36 bytes: $0314-0337
RAMVEC_BRK      := $0316
RAMVEC_NMI      := $0318
RAMVEC_OPEN     := $031A
RAMVEC_CLOSE    := $031C
RAMVEC_CHKIN    := $031E
RAMVEC_CHKOUT   := $0320
RAMVEC_CLRCHN   := $0322
RAMVEC_CHRIN    := $0324
RAMVEC_CHROUT   := $0326
RAMVEC_STOP     := $0328
RAMVEC_GETIN    := $032A
RAMVEC_CLALL    := $032C
RAMVEC_WTF      := $032E
RAMVEC_LOAD     := $0330
RAMVEC_SAVE     := $0332
RAMVEC_MEM_0334 := $0334
RAMVEC_MEM_0336 := $0336
GO_RAM_LOAD_GO_APPL       := $0338  ;
GO_RAM_STORE_GO_APPL      := $0341  ; RAM-resident code loaded from:
GO_RAM_LOAD_GO_KERN       := $034A  ; MMU_HELPER_ROUTINES
GO_NOWHERE_LOAD_GO_KERN   := $034D  ;
SINNER                    := $034E  ; "SINNER" name is from TED-series KERNAL,
GO_APPL_LOAD_GO_KERN      := $0353  ; where similar RAM-resident code is
GO_RAM_STORE_GO_KERN      := $035C  ; modified at runtime.
GO_NOWHERE_STORE_GO_KERN  := $035F  ;
MEM_0365        := $0365  ;Keyboard related
MEM_0366        := $0366  ;Keyboard related
MEM_0367        := $0367  ;Keyboard related
LSTCHR          := $036E  ;Last char typed; used to test for ESC sequence
REVERSE         := $036C  ;0=Reverse Off, 0x80=Reverse On
BLNOFF          := $036F  ;0=Cursor Blink On, 0x80=Cursor Blink Off
TABMAP          := $0370
SETUP_LCD_A     := $037A
SETUP_LCD_X     := $037B
SETUP_LCD_Y     := $037C
CurMaxY         := $037E
MEM_0380        := $0380
CurMaxX         := $0381
MSGFLG          := $0383
DFLTN           := $0385
DFLTO           := $0386
FNLEN           := $0387
MEM_038E        := $038E  ;Keyboard related
JIFFIES         := $038F
TOD_SECS        := $0390
TOD_MINS        := $0391
TOD_HOURS       := $0392
ALARM_SECS      := $0393
ALARM_MINS      := $0394
ALARM_HOURS     := $0395
UNKNOWN_SECS    := $0396
UNKNOWN_MINS    := $0397
MemBotLoByte    := $0398
MemBotHiByte    := $0399
MemTopLoByte    := $039A
MemTopHiByte    := $039B
V1541_BYTE_TO_WRITE := $039E
V1541_FNLEN     := $039F
BAD             := $03A0
MON_MMU_MODE    := $03A1  ;0=MMU_MODE_RAM, 1=MMU_MODE_APPL, 2=MMU_MODE_KERN
V1541_FILE_MODE := $03A3
V1541_FILE_TYPE := $03A4
MEM_03AC        := $03AC
SXREG           := $039D
FORMAT          := $03B4
MEM_03B7        := $03B7
MEM_03C0        := $03C0
RAMVEC_BACKUP   := $03C3  ;Backs up KERNAL RAM vectors, 36 bytes: $03C3-$03E6
LSTP            := $03E8
LSXP            := $03E9
SavedCursorX    := $03EA
SavedCursorY    := $03EB
KEYD            := $03EC
MEM_03F6        := $03F6  ;Keyboard related
MEM_03F7        := $03F7  ;Keyboard related
MEM_03F8        := $03F8  ;Keyboard related
MEM_03F9        := $03F9  ;Keyboard related
MEM_03FA        := $03FA  ;Possibly Virtual 1541 or Keyboard related
SWITCH_COUNT    := $03FB  ;Counts down to debounce switching upper/lowercase on Shift-Commodore
CAPS_FLAGS      := $03FC
LDTND           := $0405
VERCHK          := $0406
WRBASE          := $0407  ;Temp storage (was low byte of tape write pointer in other CBMs)
BSOUR           := $0408
BSOUR1          := $0409
R2D2            := $040A
C3P0            := $040B
IECCNT          := $040C
RTC_IDX         := $0411
RTC_DATA        := $0412  ;8 bytes (see RTC_ constants below)
HULP            := $0450
LINE_INPUT_BUF  := $0470  ;Buffer used for a line of input in the monitor and menu
MEM_04C0        := $04C0

;VIA #1 Registers
VIA1_PORTB    := $F800
VIA1_PORTA    := $F801
VIA1_DDRB     := $F802
VIA1_DDRA     := $F803
VIA1_T1CL     := $F804
VIA1_T1CH     := $F805
VIA1_T1LL     := $F806
VIA1_T1LH     := $F807
VIA1_T2CL     := $F808
VIA1_T2CH     := $F809
VIA1_SR       := $F80A
VIA1_ACR      := $F80B
VIA1_PCR      := $F80C
VIA1_IFR      := $F80D
VIA1_IER      := $F80E
VIA1_PORTANHS := $F80F

;VIA #2 Registers
VIA2_PORTB    := $F880
VIA2_PORTA    := $F881
VIA2_DDRB     := $F882
VIA2_DDRA     := $F883
VIA2_T1CL     := $F884
VIA2_T1CH     := $F885
VIA2_T1LL     := $F886
VIA2_T1LH     := $F887
VIA2_T2CL     := $F888
VIA2_T2CH     := $F889
VIA2_SR       := $F88A
VIA2_ACR      := $F88B
VIA2_PCR      := $F88C
VIA2_IFR      := $F88D
VIA2_IER      := $F88E
VIA2_PORTANHS := $F88F

;ACIA Registers
ACIA_DATA     := $F980
ACIA_ST       := $F981
ACIA_CMD      := $F982
ACIA_CTRL     := $F983

;MMU Registers
MMU_MODE_KERN    := $FA00   ;Any write here switches to the "KERN" MMU mode.
MMU_MODE_APPL    := $FA80   ;Any write here switches to the "APPL" MMU mode.
MMU_MODE_RAM     := $FB00   ;Any write here switches to the "RAM" MMU mode.
MMU_RECALL_MODE  := $FB80   ;Any write here recalls the previously saved mode.
MMU_SAVE_MODE    := $FC00   ;Any write here saves the current mode so it can be recalled.
MMU_MODE_TEST    := $FC80   ;Any write here switches to the "TEST" MMU mode. (Unused)
MMU_OFFS_APPL_W1 := $FD00   ;Sets offset for $1000-3FFF "APPL Window 1" in the "APPL" MMU mode.
MMU_OFFS_APPL_W2 := $FD80   ;Sets offset for $4000-7FFF "APPL Window 2" in the "APPL" MMU mode.
MMU_OFFS_APPL_W3 := $FE00   ;Sets offset for $8000-BFFF "APPL Window 3" in the "APPL" MMU mode.
MMU_OFFS_APPL_W4 := $FE80   ;Sets offset for $C000-F7FF "APPL Window 4" in the "APPL" MMU mode.
MMU_OFFS_KERN_W  := $FF00   ;Sets offset for $4000-7FFF "KERN Window" in the "KERN" MMU mode.

;LCD Controller Registers $FF80-$FF83
LCDCTRL_REG0 := $FF80
LCDCTRL_REG1 := $FF81
LCDCTRL_REG2 := $FF82
LCDCTRL_REG3 := $FF83

;Equates

;Used to test MODKEY
MOD_BIT_7  = 128 ;Unknown
MOD_BIT_6  = 64  ;Unknown
MOD_BIT_5  = 32  ;Unknown
MOD_CBM    = 16
MOD_CTRL   = 8
MOD_SHIFT  = 4
MOD_CAPS   = 2
MOD_STOP   = 1

;CBM DOS error codes
doserr_20_read_err        = $14 ;20 read error (block header not found)
doserr_25_write_err       = $19 ;25 write error (write-verify error)
doserr_26_write_prot_on   = $1a ;26 write protect on
doserr_27_read_error      = $1b ;27 read error (checksum error in header)
doserr_31_invalid_cmd     = $1f ;31 invalid command
doserr_32_syntax_err      = $20 ;32 syntax error (long line)
doserr_33_syntax_err      = $21 ;33 syntax error (invalid filename)
doserr_34_syntax_err      = $22 ;34 syntax error (no file given)
doserr_60_write_file_open = $3c ;60 write file open
doserr_61_file_not_open   = $3d ;61 file not open
doserr_62_file_not_found  = $3e ;62 file not found
doserr_63_file_exists     = $3f ;63 file exists
doserr_64_file_type_mism  = $40 ;64 file type mismatch
doserr_67_illegal_sys_ts  = $43 ;67 illegal system t or s
doserr_70_no_channel      = $46 ;70 no channel
doserr_71_dir_error       = $47 ;71 directory error
doserr_72_disk_full       = $48 ;72 disk full
doserr_73_dos_mismatch    = $49 ;73 power-on message

doschan_14_cmd_app   = $0e ;14 unknown channel, seems to be used by "command.cmd" app
doschan_15_command   = $0f ;15 normal cbm dos command channel
doschan_16_directory = $10 ;16 directory channel
doschan_17_unknown   = $11 ;17 unknown channel

;Virtual 1541 file types and modes
ftype_p_prg     = 'P'   ;Program
ftype_s_seq     = 'S'   ;Sequential
fmode_r_read    = 'R'   ;Read
fmode_w_write   = 'W'   ;Write
fmode_a_append  = 'A'   ;Append
fmode_m_modify  = 'M'   ;Modify

;RTC_DATA offsets
RTC_HOURS = 0
RTC_MINUTES = 1
RTC_SECONDS = 2
RTC_24H_AMPM = 3
RTC_DOW = 4
RTC_DAY = 5
RTC_MONTH = 6
RTC_YEAR = 7

; ----------------------------------------------------------------------------
ROM_HEADER:
;Every ROM starts with an 8-byte header followed by the magic string
        .byte   $00 ;unknown
        .byte   $00 ;unknown
        .byte   $FF ;unknown
        .byte   $FF ;unknown
        .byte   16  ;number of kilobytes to be checked by RomCheckSum
        .byte   $DD ;unknown
        .byte   $DD ;unknown
        .byte   $DD ;unknown

ROM_MAGIC:
;SCAN_ROMS routine looks for this magic string
        .byte   "Commodore LCD"
ROM_MAGIC_SIZE = * - ROM_MAGIC

; ----------------------------------------------------------------------------
; Every ROM contains a "directory" with the "applications" to be found.
;
;  - Apps can be displayed on the menu or hidden from it.  An app that is
;    hidden can still be run by typing its name.
;
;  - An app can optionally have a file extension associated with it.  If a
;    period follows the name, the characters that follow are the extension.
;    The menu will then use the app to open files with that extension.  All
;    extensions are 3 characters in the LCD ROMs but this is not required.
;    The extension is part of a regular 16-character CBM filename and can be
;    longer or shorter than 3 characters.
ROM_DIR_START := *

ROM_DIR_ENTRY_MONITOR:
        .byte ROM_DIR_ENTRY_MONITOR_SIZE
        .byte $10               ;$01=show on menu, $10=hidden
        .byte $20               ;unknown
        .byte $00               ;unknown
        .word ROM_ENTRY_MONITOR ;entry point
        .byte "MONITOR"         ;menu name
        .byte ".MON"            ;associated file extension
        ROM_DIR_ENTRY_MONITOR_SIZE = * - ROM_DIR_ENTRY_MONITOR

ROM_DIR_ENTRY_COMMAND:
        .byte ROM_DIR_ENTRY_COMMAND_SIZE
        .byte $01               ;$01=show on menu, $10=hidden
        .byte $20               ;unknown
        .byte $00               ;unknown
        .word ROM_ENTRY_COMMAND ;entry point
        .byte "COMMAND"         ;menu name
        .byte ".CMD"            ;associated file extension
        ROM_DIR_ENTRY_COMMAND_SIZE = * - ROM_DIR_ENTRY_COMMAND

ROM_DIR_END:
        .byte 0
; ----------------------------------------------------------------------------
ROM_ENTRY_MONITOR:
        cpx     #$0E
        bne     L8040
        clc
        jmp     L84FA_MAYBE_SHUTDOWN
L8040:  cpx     #$06
        beq     JMP_MON_START
        cpx     #$04
        beq     JMP_MON_START
        rts
JMP_MON_START:
        jmp     MON_START
; ----------------------------------------------------------------------------
ROM_ENTRY_COMMAND:
        cpx     #$08
        bne     L8066

        lda     #$7E                  ;A = Logical file number (126)
L8052:  ldx     #$01                  ;X = Device 1 (Virtual 1541)
        ldy     #doschan_14_cmd_app   ;Y = Channel 14
        jsr     SETLFS_

        lda     $0423       ;A = Filename length
        ldx     #<$0424     ;XY = Filename
        ldy     #>$0424
        jsr     SETNAM_
        jsr     Open_
L8066:  rts
; ----------------------------------------------------------------------------
L8067:  txa
        tay
        lda     #$FF
L806B:  phy
        ldx     #$00
        phx
        pha
        phy
        cld
        ldx     #$08
L8074:  stx     MEM_03C0
L8077:  dec     MEM_03C0
        ldx     MEM_03C0
        bpl     L8082
        sec
        bra     L80BB
L8082:  lda     ROM_ENV_A
        and     PowersOfTwo,x
        beq     L8077
        lda     ROM_START_KERN_W_OFFSETS,x
        sta     MMU_OFFS_KERN_W
        lda     #$40
        sta     $DC
        stz     $DB
        lda     #$15
        .byte   $2C
L8099:  lda     ($DB)
        clc
        adc     $DB
        sta     $DB
        bcc     L80A4
        inc     $DC
L80A4:  lda     ($DB)
        beq     L8077
        tsx
L80A9:  inc     stack+3,x
        ldy     #$01
        lda     ($DB),y
        and     stack+2,x
        beq     L8099
        dec     stack+1,x
        bne     L8099
        clc
L80BB:  ply
        ply
        plx
        ply
        bcc     L80C3
        ldx     #$00
L80C3:  cpx     #$00
        rts
; ----------------------------------------------------------------------------
L80C6:  pha
        ldy     #$01
        phy
L80CA:  ply
        lda     #$07
        jsr     L806B
        beq     L80DC
        pla
        pha
        phy
        ldy     #$03
        eor     ($DB),y
        bne     L80CA
        ply
L80DC:  ply
        cpx     #$00
        rts
; ----------------------------------------------------------------------------
L80E0_DRAW_FKEY_BAR_AND_WAIT_FOR_FKEY_OR_RETURN:
        and     #$3F
        sta     $03BC
        ldx     #$04
        jsr     LD230_JMP_LD233_PLUS_X  ;-> LD255_X_04
        stz     $03BD
        lda     $03BC
        ldy     #$01
        jsr     L806B
        beq     L8148
        lda     #$05
        sta     $03BF
        ldx     #$07
        lda     $03BC
L8101:  cmp     PowersOfTwo,x
        beq     L810B
        dex
        bpl     L8101
        bra     L8115
L810B:  ldy     #$08
        jsr     L806B
        bne     L8115
        inc     $03BF
L8115:  jsr     L815E_DRAW_FKEY_BAR

L8118_WAIT_FOR_FKEY_OR_RETURN_LOOP:
        jsr     LB6DF_GET_KEY_BLOCKING

        ldx     #$09
L811D_FIND_KEY_LOOP:
        cmp     L8154_KEYCODES_FKEYS_AND_RETURNS,x
        beq     L8127_FOUND_KEY
        dex
        bpl     L811D_FIND_KEY_LOOP

        bra     L8118_WAIT_FOR_FKEY_OR_RETURN_LOOP

L8127_FOUND_KEY:
        txa             ;A = 0=F1,1=F2,2=F3,3=F4,4=F5,5=F6,6=F7,7=F8,8=RETURN,9=SHIFT-RETURN
        cmp     #$07
        bcs     L8148
        cmp     #$06    ;F7
        bne     L813A_SKIP_DRAW_AND_WAIT
        cmp     $03BF
        beq     L813A_SKIP_DRAW_AND_WAIT

        jsr     L815E_DRAW_FKEY_BAR
        bra     L8118_WAIT_FOR_FKEY_OR_RETURN_LOOP

L813A_SKIP_DRAW_AND_WAIT:
        clc
        adc     $03BE
        tay
        lda     $03BD
        jsr     L806B
        beq     L8118_WAIT_FOR_FKEY_OR_RETURN_LOOP
        .byte   $2C

L8148:  ldx     #$00
        phx
        pha
        ldx     #$06
        jsr     LD230_JMP_LD233_PLUS_X  ;-> LD297_X_06
        pla
        plx
        rts

L8154_KEYCODES_FKEYS_AND_RETURNS:
        .byte   $85 ;F1
        .byte   $89 ;F2
        .byte   $86 ;F3
        .byte   $8A ;F4
        .byte   $87 ;F5
        .byte   $8B ;F6
        .byte   $88 ;F7
        .byte   $8C ;F8
        .byte   $0D ;RETURN
        .byte   $8D ;SHIFT-RETURN
; ----------------------------------------------------------------------------
L815E_DRAW_FKEY_BAR:
        sec
        cld
        lda     $03BF
        adc     $03BE
        sta     $03BE
L8169:  ldy     $03BE
        lda     $03BD
        jsr     L806B
        bne     L818A
        ldy     #$01
        sty     $03BE
L8179:  lda     $03BD
        asl     a
        bne     L8180
        inc     a
L8180:  sta     $03BD
        bit     $03BC
        beq     L8179
        bra     L8169

L818A:
        ldx     #$00
L818C_OUTER_LOOP:
        phx
        phy
        jsr     L81E0_PUT_CHAR_IN_FKEY_BAR
        lda     #<MORE_EXIT
        sta     $DB
        lda     #>MORE_EXIT
        sta     $DC
        tsx
        lda     stack+2,x
        ldy     #$07 ;0-7 for F1-F8
        cmp     #$07
        beq     L81B8_INNER_LOOP
        ldy     #$00
        dec     a
        cmp     $03BF
        beq     L81B8_INNER_LOOP
        ldy     stack+1,x
        lda     $03BD
        jsr     L806B
        beq     L81C5_PERIOD

        ldy     #$06
L81B8_INNER_LOOP:
        lda     ($DB),y
        cmp     #'.'
        beq     L81C5_PERIOD
        clc
        jsr     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
        iny
        bra     L81B8_INNER_LOOP

L81C5_PERIOD:
        lda     #$0D
        clc
        jsr     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
        ply
        plx
        iny
        inx
        cpx     #$08 ;0-7 for F1-F8
        bne     L818C_OUTER_LOOP
        rts
MORE_EXIT:
        .byte   "<MORE>."
        .byte   "EXIT."
; ----------------------------------------------------------------------------
L81E0_PUT_CHAR_IN_FKEY_BAR:
        ldy     L81F3_FKEY_COLUMNS,x
        ldx     $039C
        lda     #$89
        sec
        jsr     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
        lda     #$67 ;TODO graphics character
        ldy     #$09
        sta     ($BD),y
        rts

L81F3_FKEY_COLUMNS:
        ;      F1,F2,F3,F4,F5,F6,F7,F8
        .byte   0,10,20,30,40,50,60,70  ;Starting column on bottom screen line
; ----------------------------------------------------------------------------
L81FB:  stz     HULP
        jsr     L806B
        beq     L821C
        phx
        pha
        phy
        ldx     #$00
        ldy     #$06
L820A:  lda     ($DB),y
        sta     HULP,x
        inx
        iny
        tya
        cmp     ($DB)
        bne     L820A
        stz     HULP,x
        ply
        pla
        plx
L821C:  rts
; ----------------------------------------------------------------------------
L821D:  lda     #FNADR
        sta     SINNER
        ldx     FNLEN
        beq     L826E
        ldy     #$01
L8229:  lda     #$7F
        jsr     L806B
        beq     L826E
        pha
        phx
        phy
        ldx     FNLEN
        beq     L826E
        lda     ($DB)
        tay
L823B:  dey
        dex
        bmi     L824D
        jsr     L826F
        cmp     ($DB),y
        bne     L824D
        cmp     #$2E
        bne     L823B
        clc
        bra     L826B
L824D:  ldy     #$06
        ldx     #$00
L8251:  jsr     L826F
        cmp     ($DB),y
        bne     L8265
        inx
        iny
        cpx     FNLEN
        bne     L8251
        lda     ($DB),y
        cmp     #'.'
        beq     L826B
L8265:  ply
        plx
        pla
        iny
        bra     L8229
L826B:  ply
        plx
        pla
L826E:  rts
; ----------------------------------------------------------------------------
L826F:  phy
        txa
        tay
        jsr     GO_RAM_LOAD_GO_KERN
        ply
        rts
; ----------------------------------------------------------------------------
; Interesting, though I don't know the purpose of the given ZP locations. It
; seems, $FD00, $FD80, $FE00, $FE80 are used some kind of MMU purpose, based
; on value CMP'd with constants which suggests memory is divided into parts
; (high byte only): $00-$3F, $40-$7F, $80-$BF, $C0-$F7, $F8-$FF.
L8277:  sei
        phx
        ldx     MEM_03C0
        lda     ROM_START_KERN_W_OFFSETS,x
        clc
        adc     #$10
        ldy     #$02
        sec
        sbc     ($DB),y
        tax
        ldy     #$05
        lda     ($DB),y
        cmp     #$F8
        bcs     L82B1
        stz     MMU_OFFS_APPL_W1
        stz     MMU_OFFS_APPL_W2
        stz     MMU_OFFS_APPL_W3
        cmp     #$C0
        bcs     L82AE_APPL_W4
        cmp     #$80
        bcs     L82AB_APPL_W3
        cmp     #$40
        bcs     L82A8_APPL_W2
        stx     MMU_OFFS_APPL_W1
L82A8_APPL_W2:
        stx     MMU_OFFS_APPL_W2
L82AB_APPL_W3:
        stx     MMU_OFFS_APPL_W3
L82AE_APPL_W4:
        stx     MMU_OFFS_APPL_W4
L82B1:  pha
        dey
        lda     ($DB),y
        ply
        cmp     #$00
        bne     L82BB
        dey
L82BB:  dec     a
        plx
        rts
; ----------------------------------------------------------------------------
L82BE_CHECK_ROM_ENV:
        jsr     SCAN_ROMS
        sta     ROM_ENV_A
        stx     ROM_ENV_X
        sty     ROM_ENV_Y
        rts
; ----------------------------------------------------------------------------
ROM_START_KERN_W_OFFSETS:
;SCAN_ROMS uses this table to write offsets to MMU_OFFS_KERN_W ($FF00)
;to check for ROMs.  There are 8 offsets in the table, each corresponding
;to a 16K area.  SCAN_ROMS looks for a header (see the ROM_HEADER area)
;at the start of each 16K area.
;
;Although eight areas of 16K are scanned, the EPROMs found in Bil Herd's
;prototype are 32K each.  So, the two 16K halves of each 32K EPROM are scanned.
;The code in the EPROMs really is 32K, though.  Offset 0 in each EPROM contains
;a magic header while offset $4000 is just normal code.
;
;MMU_OFFS_KERN_W = (Physical address - KERN Window base address $4000) >> 10
;
        .byte ($20000-$4000)>>10  ;Physical address $20000 (ss-calc13apr-u105.bin: $0000)
        .byte ($24000-$4000)>>10  ;Physical address $24000 (ss-calc13apr-u105.bin: $4000)
        .byte ($28000-$4000)>>10  ;Physical address $28000 (sept-m-13apr-u104.bin: $0000)
        .byte ($2C000-$4000)>>10  ;Physical address $2C000 (sept-m-13apr-u104.bin: $4000)
        .byte ($30000-$4000)>>10  ;Physical address $30000 (sizapr-u103.bin: $0000)
        .byte ($34000-$4000)>>10  ;Physical address $34000 (sizapr-u103.bin: $4000)
        .byte ($38000-$4000)>>10  ;Physical address $38000 (kizapr-u102.bin: $0000)
        .byte ($3C000-$4000)>>10  ;Physical address $3C000 (kizapr-u102.bin: $4000)

ROM_START_KERN_W_OFFSETS_SIZE = * - ROM_START_KERN_W_OFFSETS
; ----------------------------------------------------------------------------
SCAN_ROMS:
; This routine scans ROMs, searching for the "Commodore LCD" string.
; This is done by using register at $FF00 which seems to tell the memory
; mapping at CPU address $4000.
        lda     #0
        pha
        pha
        pha
        ldy     #ROM_START_KERN_W_OFFSETS_SIZE-1
L82DA:  lda     ROM_START_KERN_W_OFFSETS,y
        sta     MMU_OFFS_KERN_W
        phy
        ldx     #ROM_MAGIC_SIZE-1
L82E3:  lda     ROM_MAGIC-$4000,x
        cmp     ROM_MAGIC,x
        bne     L8315
        dex
        bpl     L82E3
        ply
        phy
        lda     ROM_START_KERN_W_OFFSETS,y
; $4004 is the paged-in ROM, where the id string would be ($FF00 controls
; what can you see from $4000), it's compared with the kernal's image's id
; string ("Commodore LCD").
        ldx     ROM_HEADER-$4000+4
        pha
        jsr     RomCheckSum
        ply
        sty     MMU_OFFS_KERN_W
L82FE:  phx
        tsx
        clc
        adc     stack+4,x
        sta     stack+4,x
; Hmm, it seems to be a bug for me, it should be 'pla', otherwise X is messed
; up to be used to address byte on the stack.
        plx
        adc     stack+5,x
        sta     stack+5,x
        ply
        phy
        jsr     PrintRomSumChkByPassed
        sec
        .byte   $24 ;skip 1 byte
L8315:  clc
        ply
        tsx
        rol     stack+1,x
        dey
        bpl     L82DA
        pla
        ply
        plx
        cmp     ROM_ENV_A
        bne     L832E_RTS
        cpx     ROM_ENV_X
        bne     L832E_RTS
        cpy     ROM_ENV_Y
L832E_RTS:
        rts
; ----------------------------------------------------------------------------
PrintRomSumChkByPassed:
; Push Y onto the stack. Write "ROMSUM ...." text, then take the value from
; the stack, "covert" into an ASCII number (ORA), and print it, followed by
; the " INSTALLED" text.
        phy
        jsr     PRIMM
        .byte   "ROMSUM CHECK BYPASSED, ROM #",0
        pla
        ora     #'0'          ;convert ROM number to PETSCII
        jsr     KR_ShowChar_  ;print it
        jsr     PRIMM
        .byte   "  INSTALLED",$0d,0
        rts
; ----------------------------------------------------------------------------
RomCheckSum:
; Creates checksum on ROMs.
; Input:
;        A = value of $FF00 reg to start at
;        X = number of Kbytes to check
; Output:
;        X/A = 16 bit checksum (simple addition, X is the high byte)
        sta     $03C2
        stx     $03C1
        lda     #$00
        tax
        cld
L8371:  ldy     $03C2
        sty     MMU_OFFS_KERN_W
        ldy     #$00
        clc
L837A:  adc     $4000,y
        bcc     L8381
        clc
        inx
L8381:  adc     $4100,y
        bcc     L8388
        clc
        inx
L8388:  adc     $4200,y
        bcc     L838F
        clc
        inx
L838F:  adc     $4300,y
        bcc     L8396
        clc
        inx
L8396:  iny
        bne     L837A
        inc     $03C2
        dec     $03C1
        bne     L8371
        rts
; ----------------------------------------------------------------------------
L83A2:  lda     $DD                             ; 83A2 A5 DD                    ..
        ldx     $DE                             ; 83A4 A6 DE                    ..
        ldy     $DF                             ; 83A6 A4 DF                    ..
        pha                                     ; 83A8 48                       H
        phx                                     ; 83A9 DA                       .
        phy                                     ; 83AA 5A                       Z
        lda     #$0F                            ; 83AB A9 0F                    ..
        sta     $DE                             ; 83AD 85 DE                    ..
        lda     #$00                            ; 83AF A9 00                    ..
        sta     $DD                             ; 83B1 85 DD                    ..
        sta     $DF                             ; 83B3 85 DF                    ..
        pha                                     ; 83B5 48                       H
        pha                                     ; 83B6 48                       H
        tay                                     ; 83B7 A8                       .
        tsx                                     ; 83B8 BA                       .
        cld                                     ; 83B9 D8                       .
L83BA:  clc                                     ; 83BA 18                       .
        adc     ($DD),y                         ; 83BB 71 DD                    q.
        bcc     L83C7                           ; 83BD 90 08                    ..
        inc     stack+1,x                       ; 83BF FE 01 01                 ...
        bne     L83C7                           ; 83C2 D0 03                    ..
        inc     stack+2,x                       ; 83C4 FE 02 01                 ...
L83C7:  iny                                     ; 83C7 C8                       .
        bne     L83BA                           ; 83C8 D0 F0                    ..
        dec     $DE                             ; 83CA C6 DE                    ..
        dec     $DE                             ; 83CC C6 DE                    ..
        beq     L83BA                           ; 83CE F0 EA                    ..
        inc     $DE                             ; 83D0 E6 DE                    ..
        bpl     L83BA                           ; 83D2 10 E6                    ..
        plx                                     ; 83D4 FA                       .
        ply                                     ; 83D5 7A                       z
        sta     $DD                             ; 83D6 85 DD                    ..
        stx     $DE                             ; 83D8 86 DE                    ..
        sty     $DF                             ; 83DA 84 DF                    ..
        ply                                     ; 83DC 7A                       z
        plx                                     ; 83DD FA                       .
        pla                                     ; 83DE 68                       h
        cmp     $DD                             ; 83DF C5 DD                    ..
        bne     L83EB                           ; 83E1 D0 08                    ..
        cpx     $DE                             ; 83E3 E4 DE                    ..
        bne     L83EB                           ; 83E5 D0 04                    ..
        cpy     $DF                             ; 83E7 C4 DF                    ..
        beq     L83EC                           ; 83E9 F0 01                    ..
L83EB:  clc                                     ; 83EB 18                       .
L83EC:  rts                                     ; 83EC 60                       `
; ----------------------------------------------------------------------------
L83ED:  lda     #$02                            ; 83ED A9 02                    ..
        .byte   $2C                             ; 83EF 2C                       ,
L83F0:  lda     #$00                            ; 83F0 A9 00                    ..
        bit     $10A9                           ; 83F2 2C A9 10                 ,..
        ldx     #$01                            ; 83F5 A2 01                    ..
L83F7:  phx                                     ; 83F7 DA                       .
        pha                                     ; 83F8 48                       H
        jsr     L840F                           ; 83F9 20 0F 84                  ..
        bcs     L8407                           ; 83FC B0 09                    ..
        jsr     KL_RESTOR                       ; 83FE 20 96 C6                  ..
        plx                                     ; 8401 FA                       .
        phx                                     ; 8402 DA                       .
        jsr     L8420_JSR_L8277_JMP_LFA67       ; 8403 20 20 84                   .
        clc                                     ; 8406 18                       .
L8407:  pla                                     ; 8407 68                       h
        plx                                     ; 8408 FA                       .
        inx                                     ; 8409 E8                       .
        bcc     L83F7                           ; 840A 90 EB                    ..
        jmp     KL_RESTOR                       ; 840C 4C 96 C6                 L..
; ----------------------------------------------------------------------------
L840F:  jsr     L8067                           ; 840F 20 67 80                  g.
        beq     L841C                           ; 8412 F0 08                    ..
        bit     #$C0                            ; 8414 89 C0                    ..
        bne     L841C                           ; 8416 D0 04                    ..
        ldy     #$01                            ; 8418 A0 01                    ..
        clc                                     ; 841A 18                       .
        rts                                     ; 841B 60                       `
; ----------------------------------------------------------------------------
L841C:  sec                                     ; 841C 38                       8
        bit     #$00                            ; 841D 89 00                    ..
        rts                                     ; 841F 60                       `
; ----------------------------------------------------------------------------
L8420_JSR_L8277_JMP_LFA67:
        jsr     L8277                           ; 8420 20 77 82                  w.
        jmp     LFA67                           ; 8423 4C 67 FA                 Lg.
; ----------------------------------------------------------------------------
MON_CMD_EXIT:
L8426:  stz     $0202                           ; 8426 9C 02 02                 ...
        ldx     $0203                           ; 8429 AE 03 02                 ...
        stx     $0200                           ; 842C 8E 00 02                 ...
        stz     $0203                           ; 842F 9C 03 02                 ...
        jsr     L840F                           ; 8432 20 0F 84                  ..
        bcc     L843A                           ; 8435 90 03                    ..
        jmp     L843F                           ; 8437 4C 3F 84                 L?.
; ----------------------------------------------------------------------------
L843A:  ldx     #$0A                            ; 843A A2 0A                    ..
        jsr     L8420_JSR_L8277_JMP_LFA67       ; 843C 20 20 84                   .
L843F:  jsr     L8685                           ; 843F 20 85 86                  ..
        lda     #$20                            ; 8442 A9 20                    .
        ldy     #$01                            ; 8444 A0 01                    ..
        jsr     L806B                           ; 8446 20 6B 80                  k.
        clc                                     ; 8449 18                       .
        jsr     L8459                           ; 844A 20 59 84                  Y.
        ldy     #$01                            ; 844D A0 01                    ..
        lda     #$10                            ; 844F A9 10                    ..
        jsr     L806B                           ; 8451 20 6B 80                  k.
        clc                                     ; 8454 18                       .
        jsr     L8459                           ; 8455 20 59 84                  Y.
        brk                                     ; 8458 00                       .
L8459:  ldy     $0202                           ; 8459 AC 02 02                 ...
        bne     L843F                           ; 845C D0 E1                    ..
        bcc     L8472                           ; 845E 90 12                    ..
        jsr     L840F                           ; 8460 20 0F 84                  ..
        beq     L84C3_CLC_RTS                           ; 8463 F0 5E                    .^
        bit     #$12                            ; 8465 89 12                    ..
        beq     L84C3_CLC_RTS                           ; 8467 F0 5A                    .Z
        ldy     $0200                           ; 8469 AC 00 02                 ...
        sty     $0203                           ; 846C 8C 03 02                 ...
        stz     $0200                           ; 846F 9C 00 02                 ...
L8472:  jsr     L840F                           ; 8472 20 0F 84                  ..
        beq     L84C3_CLC_RTS                           ; 8475 F0 4C                    .L
        bit     #$01                            ; 8477 89 01                    ..
        bne     L849B                           ; 8479 D0 20                    .
        bit     #$12                            ; 847B 89 12                    ..
        bne     L8482                           ; 847D D0 03                    ..
        stz     $0203                           ; 847F 9C 03 02                 ...
L8482:  sta     $0201                           ; 8482 8D 01 02                 ...
        stx     $0200                           ; 8485 8E 00 02                 ...
        sei                                     ; 8488 78                       x
        jsr     KL_RESTOR                       ; 8489 20 96 C6                  ..
        ldx     #$04                            ; 848C A2 04                    ..
        lda     $0203                           ; 848E AD 03 02                 ...
        beq     L8495                           ; 8491 F0 02                    ..
        ldx     #$06                            ; 8493 A2 06                    ..
L8495:  jsr     L8420_JSR_L8277_JMP_LFA67       ; 8495 20 20 84                   .
        jmp     L8426                           ; 8498 4C 26 84                 L&.
; ----------------------------------------------------------------------------
L849B:  stx     $0202
        php
        sei
        jsr     SWAP_RAMVEC     ;Swap out the current vectors
        jsr     KL_RESTOR       ;Restore the KERNAL default ones
        ldx     #$08
        jsr     L8420_JSR_L8277_JMP_LFA67
        sei
        jsr     SWAP_RAMVEC     ;Swap the other vectors back in
        stz     $0202
        ldx     $0200
        jsr     L840F
        beq     L84C0_JMP_L8426
        jsr     L8277
        plp
        sec
        rts
; ----------------------------------------------------------------------------
L84C0_JMP_L8426:
        jmp     L8426
; ----------------------------------------------------------------------------
L84C3_CLC_RTS:
        clc
        rts
; ----------------------------------------------------------------------------
L84C5:  php
        sei
        ldx     $0202
        beq     L84DA
        jsr     SWAP_RAMVEC
        jsr     L84ED
        jsr     SWAP_RAMVEC
        ldx     $0202
        bra     L84E0
L84DA:  jsr     L84ED
        ldx     $0200
L84E0:  jsr     L840F
        beq     L84EA
        jsr     L8277
        plp
        rts
; ----------------------------------------------------------------------------
L84EA:  jmp     L843F
; ----------------------------------------------------------------------------
L84ED:  ldx     $0200
        jsr     L840F
        beq     L84FA_MAYBE_SHUTDOWN
        ldx     #$0E
        jmp     L8420_JSR_L8277_JMP_LFA67
; ----------------------------------------------------------------------------
; This seems to be the "shutdown" function or part of it: "state" should be
; saved (which is checked on next reset to see it was a clean shutdown) and
; then it used /POWEROFF line to actually switch the power off (the RAM is
; still powered at least on CLCD!)
L84FA_MAYBE_SHUTDOWN:
        sec
L84FB:  php
        sei
        php
        ldx     #$00
L8500:  phx
        jsr     LFCF1_APPL_CLOSE
        plx
        dex
        bpl     L8500
        plp
        bcs     L8510
        tsx
        cpx     #$20
        bcs     L8516
L8510:  ldx     #$FF
        tsx
        jsr     L8685
L8516:  jsr     L889A
        jsr     L83ED
        jsr     L8644_CHECK_BUTTON
        jsr     L86E9_MAYBE_V1541_SHUTDOWN
        sei
        tsx
        stx     $0207
        jsr     L83A2
; Release /POWERON signal, machine will switch off. Run the endless BRA if it
; needs some cycle to happen or some kind of odd problem makes it impossible
; to power off actually.
        lda     #$04
        tsb     VIA1_PORTB
        trb     VIA1_DDRB
L8532:  bra     L8532

KL_RESET:
; *************************************
; Start of the real RESET routine after
; MMU set up.
; *************************************
        sei
; As soon as possible set /POWERON signal to low (low-active signal)
; configure DDR bit as well.
        lda     #$04
        tsb     VIA1_DDRB
        trb     VIA1_PORTB
        ldx     $0207
        txs
        cpx     #$20
        bcc     L8582_COULD_NOT_RESTORE_STATE
        jsr     L83A2
        bne     L8582_COULD_NOT_RESTORE_STATE
        sec
        jsr     LCDsetupGetOrSet
        jsr     L870F_CHECK_V1541_DISK_INTACT
        bcs     L8582_COULD_NOT_RESTORE_STATE ;Branch if not intact
        jsr     SCAN_ROMS
        bne     L8582_COULD_NOT_RESTORE_STATE
        ldx     $0200
        jsr     L840F
        beq     L8582_COULD_NOT_RESTORE_STATE
        jsr     InitIOhw
        jsr     KBD_TRIGGER_AND_READ_NORMAL_KEYS
        jsr     KBD_READ_MODIFIER_KEYS_DO_SWITCH_AND_CAPS
        lsr     a ;Bit 0 = MOD_STOP
        bcs     L8582_COULD_NOT_RESTORE_STATE ;Branch if STOP is pressed
        jsr     L83F0
        jsr     L8644_CHECK_BUTTON
        jsr     L887F
        ldx     $0200
        jsr     L840F
        beq     L8582_COULD_NOT_RESTORE_STATE
        jsr     L8277
        plp
        rts
; ----------------------------------------------------------------------------
L8582_COULD_NOT_RESTORE_STATE:
        ldx     #$FF
        txs
        jsr     L8685
        cli
        jsr     PRIMM
        .byte   " COULD NOT RESTORE PREVIOUS STATE",$0d,$07,0
        ldx     #$02
        jsr     WaitXticks_

;MOD_BIT_7  = 128 ;Unknown
;MOD_BIT_6  = 64  ;Unknown
;MOD_BIT_5  = 32  ;Unknown
;MOD_CBM    = 16
;MOD_CTRL   = 8
;MOD_SHIFT  = 4
;MOD_CAPS   = 2
;MOD_STOP   = 1

        lda     MODKEY							; 85B5
        and     #MOD_CBM + MOD_SHIFT + MOD_CTRL + MOD_CAPS + MOD_STOP	; 85B7
        eor     #MOD_CBM + MOD_SHIFT + MOD_STOP   ; 16 + 4 + 1		; 85B9
        bne     L85C0							; 85BB
        jmp     L87C5	; full re-initialize ... - LGB			; 85BD
; ----------------------------------------------------------------------------
L85C0:  jsr     L870F_CHECK_V1541_DISK_INTACT
        bcc     L85E2 ;branch if intact
        jsr     PRIMM
        .byte   "YOUR DISK IS NOT INTACT",$0d,$07,0
; ----------------------------------------------------------------------------
L85E2:  jsr     L82BE_CHECK_ROM_ENV
        beq     L8607 ;branch if no change
        jsr     PRIMM
        .byte   "ROM ENVIROMENT HAS CHANGED",$0d,$07,0
; ----------------------------------------------------------------------------
L8607:  jsr     L889A
        jsr     L83F0
        jsr     L8644_CHECK_BUTTON
        stz     $0384
        lda     #$0E
        sta     CursorY
        jsr     PRIMM
        .byte   "PRESS ANY KEY TO CONTINUE",0
        cli
        jsr     LB2D6_SHOW_CURSOR
        jsr     LB6DF_GET_KEY_BLOCKING
        jsr     LB2E4_HIDE_CURSOR
        jsr     CRLF
        jmp     L843F
; ----------------------------------------------------------------------------
L8644_CHECK_BUTTON:
        cli
        ldy     #$00
L8647:  ldx     #$02
        jsr     WaitXticks_
        lda     MODKEY
        bit     #MOD_BIT_5
        bne     L8653
        rts
L8653:  iny
        bne     L8647
        jsr     PRIMM80
        .byte   "HEY, LEAVE OFF THE BUTTON, WILL YA ??",$0D,0
        jsr     BELL
        bra     L8644_CHECK_BUTTON
; ----------------------------------------------------------------------------
L8685:  stz     $0200
        stz     $0203
        stz     $0202
        jsr     KL_IOINIT
        jsr     L87BA_INIT_KEYB_AND_EDITOR
        jsr     KL_RESTOR
        jsr     LFDDF_JSR_LFFE7_CLALL
        jsr     L8C6F_V1541_I_INITIALIZE
        stz     $0384
; Set MEMBOT vector to $0FFF
        ldy     #>$0FFF
        ldx     #<$0FFF
        clc
        jmp     MEMBOT__
; ----------------------------------------------------------------------------
KL_RAMTAS:
        php
; D9/DA shows here the tested amount of RAM to be found OK, starts from zero
        sei
        stz     $D9
        stz     $DA
; This seems to test the zero page memory.
        ldx     #$00
L86B0_LOOP:
        lda     $00,x
        ldy     #$01
L86B4:  eor     $FF
        sta     $00,x
        cmp     $00,x
        bne     L86E3_NOT_EQUAL
        dey
        bpl     L86B4
        dex
        bne     L86B0_LOOP

; Test rest of the RAM, using the KERN Window to page in the testable area.
L86C2:  lda     $D9
        ldx     $DA
        inc     a
        bne     L86CA
        inx
L86CA:  jsr     L8A87
        ldy     #$00
L86CF:  lda     ($E4),y
        ldx     #$01
L86D3:  eor     #$FF
        sta     ($E4),y
        cmp     ($E4),y
        bne     L86E3_NOT_EQUAL
        dex
        bpl     L86D3
        iny
L86DF:  bne     L86CF
        bra     L86C2

L86E3_NOT_EQUAL:
        lda     $D9
        ldx     $DA
        plp
        rts
; ----------------------------------------------------------------------------
;Called only from L84FA_MAYBE_SHUTDOWN
L86E9_MAYBE_V1541_SHUTDOWN:
        jsr     L8C6F_V1541_I_INITIALIZE
        jsr     L86F6_V1541_UNKNOWN
        sta     $02D9
        sty     $02DA
        rts

;Called only from routine directly above (L86E9_MAYBE_V1541_SHUTDOWN)
L86F6_V1541_UNKNOWN:
        cld
        lda     #$00
        tay
        ldx     #$D1
L86FC:  clc
        adc     $0208,x
        bcc     L8703
        iny
L8703:  dex
        bpl     L86FC
        cmp     $02D9
        bne     L870E
        cpy     $02DA
L870E:  rts
; ----------------------------------------------------------------------------
;carry clear = intact, set = not intact
L870F_CHECK_V1541_DISK_INTACT:
        jsr     L8E46
        bcc     L8745
        lda     $020A
        ldx     $020B
        bne     L8720
        cmp     #$10
        bcc     L8745
L8720:  jsr     KL_RAMTAS
        cmp     $0208
        bne     L8745
        cpx     $0209
        bne     L8745
        cpx     $020B
        bcc     L8745
        bne     L8739
        cmp     $020A
        bcc     L8745
L8739:  cpx     #$02
        bcc     L8743
        bne     L8745
        cmp     #$00
        bne     L8745
L8743:  clc
        rts
; ----------------------------------------------------------------------------
L8745:  sec
        rts
; ----------------------------------------------------------------------------
KL_IOINIT:
        jsr     InitIOhw
        jsr     KEYB_INIT

        ;Clear alarm seconds, minutes, hours
        ldx     #$02
L874F:  stz     ALARM_SECS,x
        dex
        bpl     L874F

        stz     DFLTN ;Default input = 0 Keyboard

        lda     #$03
        sta     DFLTO ;Default output = 3 Screen

        lda     #$FF
        sta     MSGFLG
        rts
; ----------------------------------------------------------------------------
InitIOhw:
; Inits VIAs, ACIA and possible other stuffs with JSRing routines.
        php
        sei
        lda     #$FF
        sta     VIA1_DDRA

        lda     #%00111111  ;PB7 = Input    IEC DAT In
                            ;PB6 = Input    IEC CLK In
                            ;PB5 = Output   IEC DAT Out
                            ;PB4 = Output   IEC CLK Out
                            ;PB3 = Output   IEC ATN Out
                            ;PB2 = Output   ?
                            ;PB1 = Output   ?
                            ;PB0 = Output   ?
        sta     VIA1_DDRB

        lda     #$00
        sta     VIA1_PORTB

        lda     #%01001000  ;ACR7=0 Timer 1 PB7 Output = Disabled
                            ;ACR6=1 Timer 1 = Continuous (Jiffy clock)
                            ;ACR5=0 Timer 2 = One-shot (IEC)
                            ;ACR4=0 \
                            ;ACR3=1  Shift in under control of Phi2
                            ;ACR2=0 /
                            ;ACR1=0 Port B Latch = Disabled
                            ;ACR0=0 Port A Latch = Disabled
        sta     VIA1_ACR

        lda     #%10100000  ;PCR7=1 \
                            ;PCR6=0  CB2 Control = Pulse Output (Beeper)
                            ;PCR5=1 /
                            ;PCR4=0 CB1 Interrupt Control = Negative Active Edge
                            ;PCR3=0 \
                            ;PCR2=0  CA2 Control = Input-negative active edge
                            ;PCR1=0 /
                            ;PCR0=0 CA1 Interrupt Control = Negative Active Edge
        sta     VIA1_PCR

                            ;Timer 1 Count (Jiffy clock)
        lda     #<16666     ;TOD clock code in IRQ handler expects to be called at 60 Hz.
        sta     VIA1_T1LL   ;60 Hz has a period of 16666 microseconds.
        lda     #>16666     ;Timer 1 fires every 16666 microseconds by counting phi2.
        sta     VIA1_T1CH   ;Phi2 must be 1 MHz, since 1 MHz has a period of 1 microsecond.

        lda     #%11000000  ;IER7=1 Set/Clear=Set interrupts
                            ;IER6=1 Timer 1 interrupt enabled (Jiffy clock)
                            ;All other interrupts disabled
        sta     VIA1_IER

        stz     VIA2_PORTA
        lda     #$FF
        sta     VIA2_DDRA
        lda     #%10101111
        sta     VIA2_DDRB
        lda     #$82
        sta     VIA2_PORTB
        lda     #$00
        sta     VIA2_ACR
        lda     #$0C
        sta     VIA2_PCR
        lda     #$80
        sta     VIA2_IFR
        stz     ACIA_ST
        jsr     LBFBE ;UNKNOWN_SECS/MINS
        sec
        jsr     LCDsetupGetOrSet
        plp
        rts
; ----------------------------------------------------------------------------
L87BA_INIT_KEYB_AND_EDITOR:
        jsr     KEYB_INIT
        ldx     #$00
        jsr     LD230_JMP_LD233_PLUS_X ;-> LD247_X_00
        jmp     SCINIT_
; ----------------------------------------------------------------------------
L87C5:  sei
        ldx     #$FF
        txs
        inx
L87CA:  stz     $00,x
        stz     stack,x
        stz     $0200,x
        stz     MEM_0300,x
        stz     $0400,x
        inx
        bne     L87CA
        jsr     L8685
        cli
        jsr     PRIMM
        .byte   "ESTABLISHING SYSTEM PARAMETERS ",$07,$0D,0
        jsr     L82BE_CHECK_ROM_ENV
        lda     #$0F
        sta     $020C
        jsr     KL_RAMTAS
        sta     $0208
        stx     $0209
        sta     $020A
        stx     $020B
        stx     $00
        lsr     $00
        ror     a
        lsr     $00
        ror     a
        jsr     PRINT_BCD_NIBS ;Print the "128" in "128 KBYTE SYSTEM ESTABLISHED"
        jsr     L8E5C
        jsr PRIMM
        .byte   " KBYTE SYSTEM ESTABLISHED",$0d,0
        jsr     LD411
        jsr     L8644_CHECK_BUTTON
        jmp     L843F
; ----------------------------------------------------------------------------
;Print BCD nibbles in YXA in PETSCII
;Y=$00, X=$03, A=$02 -> Prints "32"
;Y=$00, X=$06, A=$04 -> Prints "64"
;Y=$01, X=$02, A=$08 -> Prints "128"
PRINT_BCD_NIBS:
        jsr     BIN_TO_BCD_NIBS
        pha
        phx
        tya
        bne     L885D
        pla
        bne     L8861
        beq     L8864
L885D:  jsr     L8865
        pla
L8861:  jsr     L8865
L8864:  pla
L8865:  ora     #'0'
        jmp     KR_ShowChar_
; ----------------------------------------------------------------------------
;Convert binary number A to BCD nibbles in YXA:
;A=0   -> Y=$00, X=$00, A=$00
;A=32  -> Y=$00, X=$03, A=$02
;A=64  -> Y=$00, X=$06, A=$04
;A=128 -> Y=$01, X=$02, A=$08
;A=255 -> Y=$02, X=$05, A=$05
BIN_TO_BCD_NIBS:
        ldy     #$FF
        cld
        sec
L886E:  iny
        sbc     #100
        bcs     L886E
        adc     #100
        ldx     #$FF
L8877:  inx
        sbc     #10
        bcs     L8877
        adc     #10
        rts
; ----------------------------------------------------------------------------
L887F:  clc
        jsr     L88C2
        bit     $0384
        bvc     L8896
        ldy     #$02
L888A:  lda     ($E4),y
        sta     SETUP_LCD_A,y
        dey
        bpl     L888A
        sec
        jsr     LCDsetupGetOrSet
L8896:  stz     $0384
        rts
; ----------------------------------------------------------------------------
L889A:  jsr     LBE69
        sec
        jsr     L88C2
        bit     $0384
        bvc     L88BF
        lda     #$93 ;CHR$(147) Clear Screen
        jsr     KR_ShowChar_
        ldy     #$02
L88AD:  lda     SETUP_LCD_A,y
        sta     ($E4),y
        dey
        bpl     L88AD
        and     #$01
        ldy     VidMemHi
        ldx     #$00
        clc
        jsr     LCDsetupGetOrSet
L88BF:  jmp     KL_RESTOR
; ----------------------------------------------------------------------------
L88C2:  ldx     #<MEM_04C0
        ldy     #>MEM_04C0
        jsr     KL_VECTOR
        lda     $020C
        ldx     $020D
        inc     a
        bne     L88D3
        inx
L88D3:  cpx     $020B
        bcc     L88E5
        bne     L88DF
        cmp     $020A
        bcc     L88E5
L88DF:  lda     #$80
        sta     $0384
        rts
; ----------------------------------------------------------------------------
L88E5:  jsr     L8A87
        lda     #$FF
        sta     $0384
        lda     #$05
        sta     $03E7
        ldy     #$FF
L88F4:  phy
        clc
        cld
        lda     $03E7
        adc     #$04
        tax
        ldy     #$13
        sec
        jsr     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
        ply
        ldx     #$29
L8906:  phy
        lda     ($E4),y
        pha
        phy
        txa
        tay
        lda     ($BD),y
        ply
        sta     ($E4),y
        txa
        tay
        pla
        sta     ($BD),y
        ply
        dey
        dex
        bpl     L8906
        dec     $03E7
        bpl     L88F4
        rts
; ----------------------------------------------------------------------------
L8922:  lda     #$05
        sta     $03E7
        ldx     #$A4
L8929:  jsr     L893C
        ldx     #$0D
        dec     $03E7
        bne     L8929
        ldx     #$A3
        jsr     L893C
        lda     #$0D
        bra     L8948
; ----------------------------------------------------------------------------
L893C:  phx
        jsr     L8964
        plx
L8941:  txa
        jsr     L897C
        bcc     L8941
        rts
; ----------------------------------------------------------------------------
L8948:  cmp     #$07 ;CHR$(7) Bell
        bne     L894F
        jmp     BELL
; ----------------------------------------------------------------------------
L894F:  cmp     #$93 ;CHR$(147) Clear Screen
        beq     L8922
        cmp     #$0D ;CHR$(13) Carriage Return
        bne     L897C
        jsr     L897C
        lda     $03e7
        cmp     #$04
        bcs     L8980
        inc     $03E7
L8964:  clc
        cld
        lda     $03E7
        adc     #$04
        tax
        ldy     #$13
        lda     #$29
        pha
        sec
        jsr     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
        lda     #$65
        ply
        sta     ($BD),y
        lda     #$A7
L897C:  clc
        jmp     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
; ----------------------------------------------------------------------------
L8980:  stz     $03E7
L8983:  inc     $03E7
        jsr     L8964
        lda     $03E7
        cmp     #$04
        beq     L89A1
        ldy     #$AA
L8992:  lda     ($BD),y
        jsr     L89A8
        sta     ($BD),y
        jsr     L89A8
        dey
        bmi     L8992
        bra     L8983
L89A1:  lda     #$0D
        jsr     L897C
        bra     L8964
L89A8:  tax
        tya
        eor     #$80
        tay
        txa
        rts
; ----------------------------------------------------------------------------
L89AF:  jsr     L8A39_V1541_DOESNT_WRITE_BUT_CONDITIONALLY_RETURNS_WRITE_ERROR
        bcs     L89B5 ;branch if no error
        rts
; ----------------------------------------------------------------------------
L89B5:  lda     $020A
L89B8:  bne     L89BD
        dec     $020B
L89BD:  dec     $020A
        jsr     LD3F6
        jmp     L8A81
; ----------------------------------------------------------------------------
L89C6:  jsr     L89AF
        bcc     L89E1
        lda     V1541_ACTIV_E8
        sta     $020E
        sta     ($E4)
        lda     V1541_ACTIV_E9
        sta     $020F
        ldy     #$01
        sta     ($E4),y
        iny
        lda     #$02
        sta     ($E4),y
        sec
L89E1:  rts
; ----------------------------------------------------------------------------
L89E2:  jsr     L8A81
L89E5:  lda     $020E
        cmp     V1541_DATA_BUF+1
        bne     L89F2
        jsr     L89FF
        bra     L89E2
L89F2:  jsr     L8A61
        bcs     L89E5
        sec
        rts
; ----------------------------------------------------------------------------
L89F9:  jsr     L8AD5_MAYBE_READS_BLOCK_HEADER
        bcs     L89FF ;branch if no error
        rts
; ----------------------------------------------------------------------------
L89FF:  lda     $0216
        pha
        lda     $E5
        pha
        jsr     L8A81
        stz     $D9
        pla
        sta     $DA
        ldy     #$00
L8A10:  lda     ($E4),y
        tax
        pla
L8A14:  pha
        sta     MMU_OFFS_KERN_W
        lda     ($D9),y
L8A1A:  pha
        txa
        sta     ($D9),y
        lda     $0216
        sta     MMU_OFFS_KERN_W
        pla
        sta     ($e4),y
        iny
        bne     L8A10
        pla
        inc     $020A
        bne     L8A33
        inc     $020B
L8A33:  jsr     LD3F6
        jmp     L8A81
; ----------------------------------------------------------------------------
L8A39_V1541_DOESNT_WRITE_BUT_CONDITIONALLY_RETURNS_WRITE_ERROR:
        ldx     $020B
        lda     $020A
        bne     L8A42
        dex
L8A42:  dec     a
        cpx     $020D
        bne     L8A4E_25_WRITE_ERROR
        cmp     $020C
        bne     L8A4E_25_WRITE_ERROR
        clc
L8A4E_25_WRITE_ERROR:
        lda     #doserr_25_write_err ;25 write error (write-verify error)
        rts
; ----------------------------------------------------------------------------
        cld                                     ; 8A51 D8                       .
        sec                                     ; 8A52 38                       8
        lda     $0208                           ; 8A53 AD 08 02                 ...
        sbc     $020A                           ; 8A56 ED 0A 02                 ...
        tax                                     ; 8A59 AA                       .
        lda     $0209                           ; 8A5A AD 09 02                 ...
        sbc     $020B                           ; 8A5D ED 0B 02                 ...
        rts                                     ; 8A60 60                       `
; ----------------------------------------------------------------------------
L8A61:  ldx     $DA                             ; 8A61 A6 DA                    ..
        lda     $D9                             ; 8A63 A5 D9                    ..
        inc     a                               ; 8A65 1A                       .
        bne     L8A69                           ; 8A66 D0 01                    ..
        inx                                     ; 8A68 E8                       .
L8A69:  cpx     $0209                           ; 8A69 EC 09 02                 ...
        bcc     L8A77                           ; 8A6C 90 09                    ..
        bne     L8A75                           ; 8A6E D0 05                    ..
        cmp     $0208                           ; 8A70 CD 08 02                 ...
        bcc     L8A77                           ; 8A73 90 02                    ..
L8A75:  clc                                     ; 8A75 18                       .
        rts                                     ; 8A76 60                       `
; ----------------------------------------------------------------------------
L8A77:  stx     $DA                             ; 8A77 86 DA                    ..
        sta     $D9                             ; 8A79 85 D9                    ..
        inc     $E5                             ; 8A7B E6 E5                    ..
        bmi     L8A87                           ; 8A7D 30 08                    0.
        bra     L8AA9                           ; 8A7F 80 28                    .(

L8A81:  ldx     $020B                           ; 8A81 AE 0B 02                 ...

L8A84:  lda     $020A                           ; 8A84 AD 0A 02                 ...

L8A87:  sta     $D9                             ; 8A87 85 D9                    ..
        stx     $DA                             ; 8A89 86 DA                    ..
        sec                                     ; 8A8B 38                       8
        cld                                     ; 8A8C D8                       .
        sbc     #$40                            ; 8A8D E9 40                    .@
        bcs     L8A92                           ; 8A8F B0 01                    ..
        dex                                     ; 8A91 CA                       .
L8A92:  sta     $E5                             ; 8A92 85 E5                    ..
        txa                                     ; 8A94 8A                       .
        asl     $E5                             ; 8A95 06 E5                    ..
        rol     a                               ; 8A97 2A                       *
        asl     $E5                             ; 8A98 06 E5                    ..
        rol     a                               ; 8A9A 2A                       *
        asl     a                               ; 8A9B 0A                       .
        asl     a                               ; 8A9C 0A                       .
        asl     a                               ; 8A9D 0A                       .
        asl     a                               ; 8A9E 0A                       .
        sta     $0216                           ; 8A9F 8D 16 02                 ...
        sec                                     ; 8AA2 38                       8
        ror     $E5                             ; 8AA3 66 E5                    f.
        lsr     $E5                             ; 8AA5 46 E5                    F.
        stz     $E4                             ; 8AA7 64 E4                    d.
L8AA9:  lda     $0216                           ; 8AA9 AD 16 02                 ...
        sta     MMU_OFFS_KERN_W                 ; 8AAC 8D 00 FF                 ...
L8AAF:  ldy     #$01                            ; 8AAF A0 01                    ..
        lda     ($E4)                           ; 8AB1 B2 E4                    ..
        tax                                     ; 8AB3 AA                       .
        sta     $020E                           ; 8AB4 8D 0E 02                 ...
        lda     ($E4),y                         ; 8AB7 B1 E4                    ..
        tay                                     ; 8AB9 A8                       .
        lda     $020A                           ; 8ABA AD 0A 02                 ...
        eor     $0208                           ; 8ABD 4D 08 02                 M..
        bne     L8ACD                           ; 8AC0 D0 0B                    ..
        lda     $020B                           ; 8AC2 AD 0B 02                 ...
        eor     $0209                           ; 8AC5 4D 09 02                 M..
        bne     L8ACD                           ; 8AC8 D0 03                    ..
        ldy     #$FF                            ; 8ACA A0 FF                    ..
        tax                                     ; 8ACC AA                       .
L8ACD:  stx     $020E                           ; 8ACD 8E 0E 02                 ...
        sty     $020F                           ; 8AD0 8C 0F 02                 ...
        sec                                     ; 8AD3 38                       8
        rts                                     ; 8AD4 60                       `
; ----------------------------------------------------------------------------
;maybe returns a cbm dos error code
L8AD5_MAYBE_READS_BLOCK_HEADER:
        jsr     L8AA9
        jsr     L8AEE
        beq     L8AFA
        jsr     L8A81
L8AE0:  jsr     L8AEE
        beq     L8AFA
        jsr     L8A61
        bcs     L8AE0
        clc
        lda     #doserr_20_read_err ;20 read error (block header not found)
        rts
; ----------------------------------------------------------------------------
L8AEE:  lda     V1541_ACTIV_E8
        cmp     $020E
        bne     L8AFA
        lda     V1541_ACTIV_E9
        cmp     $020F
L8AFA:  rts
; ----------------------------------------------------------------------------
        cpx     $DA                             ; 8AFB E4 DA                    ..
        bne     L8B08                           ; 8AFD D0 09                    ..
        cmp     $D9                             ; 8AFF C5 D9                    ..
        bne     L8B08                           ; 8B01 D0 05                    ..
        jsr     L8AA9                           ; 8B03 20 A9 8A                  ..
        bra     L8B0B                           ; 8B06 80 03                    ..
L8B08:  jsr     L8A87                           ; 8B08 20 87 8A                  ..
L8B0B:  stz     $020E                           ; 8B0B 9C 0E 02                 ...
        stz     $020F                           ; 8B0E 9C 0F 02                 ...
        sec                                     ; 8B11 38                       8
        rts                                     ; 8B12 60                       `
; ----------------------------------------------------------------------------
;maybe returns cbm dos error code in a
L8B13_MAYBE_ALLOCATES_SPACE_OR_CHECKS_DISK_FULL:
        lda     $0207
        bne     L8B1D_LOOP
        inc     $0207
        bra     L8B13_MAYBE_ALLOCATES_SPACE_OR_CHECKS_DISK_FULL

L8B1D_LOOP:
        pha
        jsr     L8DBE_UNKNOWN_CALLS_DOES_62_FILE_NOT_FOUND_ON_ERROR
        pla
        bcs     L8B27 ;branch if no error
        sec
        bra     L8B31_STORE_0207_0219_THEN_72_DISK_FULL

L8B27:  inc     a
        bne     L8B2B
        inc     a
L8B2B:  cmp     $0207
        bne     L8B1D_LOOP
        clc
L8B31_STORE_0207_0219_THEN_72_DISK_FULL:
        sta     $0207
        sta     V1541_DATA_BUF+1
        lda     #doserr_72_disk_full ;72 disk full
        rts
; ----------------------------------------------------------------------------
;CHRIN to Virtual 1541
V1541_CHRIN:
        jsr     L8B40_V1541_INTERNAL_CHRIN
        jmp     V1541_KERNAL_CALL_DONE

L8B40_V1541_INTERNAL_CHRIN:
        jsr     V1541_SELECT_CHANNEL_GIVEN_SA
        bcs     L8B46 ;branch if no error
        rts
; ----------------------------------------------------------------------------
L8B46:  lda     V1541_ACTIV_FLAGS
        bit     #$10
        bne     L8B50
        lda     #doserr_61_file_not_open
        clc
        rts
; ----------------------------------------------------------------------------
L8B50:  lda     V1541_DEFAULT_CHAN
        cmp     #doschan_15_command ;command channel?
        bne     L8B59
        jmp     L9AA5_V1541_CHRIN_CMD_CHAN
; ----------------------------------------------------------------------------
L8B59:  lda     V1541_ACTIV_FLAGS
        bit     #$80 ;eof?
        bne     L8BA0_CHRIN_EOF  ;branch if eof
        ;not eof
        lda     V1541_ACTIV_E8
        bne     L8B66_CHRIN_V1541_ACTIV_E8_NONZERO
        jmp     L939A
; ----------------------------------------------------------------------------
L8B66_CHRIN_V1541_ACTIV_E8_NONZERO:
        stz     SXREG
        lda     V1541_ACTIV_EA
        bne     L8B7F
        jsr     L8AD5_MAYBE_READS_BLOCK_HEADER
        bcs     L8B7B_CHRIN_NO_ERROR ;branch if no error
        dec     SXREG
        lda     #$0D      ;carriage return if error or eof
        sec
        rts
; ----------------------------------------------------------------------------
L8B79:  inc     V1541_ACTIV_E9
L8B7B_CHRIN_NO_ERROR:
        lda     #$02
        sta     V1541_ACTIV_EA
L8B7F:  jsr     L8AD5_MAYBE_READS_BLOCK_HEADER
        bcc     L8B9F_RTS ;branch if error
        ldy     #$02
        lda     ($E4),y
        beq     L8B8E
        cmp     V1541_ACTIV_EA
        beq     L8B92
L8B8E:  inc     V1541_ACTIV_EA
        beq     L8B79
L8B92:  lda     V1541_ACTIV_EA
        cmp     ($E4),y
        bne     L8B9B
        ror     SXREG
L8B9B:  tay
        lda     ($E4),y
        sec
L8B9F_RTS:  rts
; ----------------------------------------------------------------------------
L8BA0_CHRIN_EOF:
        lda     #$0D    ;CR is returned when reading past EOF
        stz     SXREG
        dec     SXREG
        sec
        rts
; ----------------------------------------------------------------------------
;CHROUT to Virtual 1541
V1541_CHROUT:
        jsr     L8BB0_V1541_INTERNAL_CHROUT
        jmp     V1541_KERNAL_CALL_DONE
; ----------------------------------------------------------------------------
L8BB0_V1541_INTERNAL_CHROUT:
        sta     V1541_BYTE_TO_WRITE
        jsr     V1541_SELECT_CHANNEL_GIVEN_SA
        bcs     L8BB9 ;branch if no error
        rts

L8BB9:  lda     V1541_ACTIV_FLAGS
        bit     #$20
        bne     L8BC3
        lda     #doserr_61_file_not_open
L8BC1_CLC_RTS:
        clc
        rts

L8BC3:  bit     #$80
        beq     L8BCB
L8BC7_73_DOS_MISMATCH:
        lda     #doserr_73_dos_mismatch
        bra     L8BC1_CLC_RTS

L8BCB:  lda     V1541_DEFAULT_CHAN
        cmp     #doschan_15_command ;command channel?
        bne     L8BD7_V1541_CHROUT_NOT_CMD_CHAN
        jmp     L975D_V1541_CHROUT_CMD_CHAN
; ----------------------------------------------------------------------------
L8BD4:  sta     V1541_BYTE_TO_WRITE
        ;Fall through

L8BD7_V1541_CHROUT_NOT_CMD_CHAN:
        lda     V1541_ACTIV_EA
        bne     L8BE1_WRITE_BYTE
        jsr     L8A39_V1541_DOESNT_WRITE_BUT_CONDITIONALLY_RETURNS_WRITE_ERROR
        bcs     L8BF7 ;branch if no error
        rts
; ----------------------------------------------------------------------------
L8BE1_WRITE_BYTE:
        inc     V1541_ACTIV_EA
        bne     L8BFE
        jsr     L8A39_V1541_DOESNT_WRITE_BUT_CONDITIONALLY_RETURNS_WRITE_ERROR
        bcc     L8C0E_RTS ;branch if error
        jsr     L8AD5_MAYBE_READS_BLOCK_HEADER
        bcc     L8C0E_RTS ;branch if error
        ldy     #$02
        lda     #$00
L8BF3:  sta     ($E4),y
        inc     V1541_ACTIV_E9
L8BF7:  ldy     #$03
        sty     V1541_ACTIV_EA
        jsr     L89C6
L8BFE:  jsr     L8AD5_MAYBE_READS_BLOCK_HEADER
        ldy     #$02
        lda     V1541_ACTIV_EA
        sta     ($E4),y
        tay
        lda     V1541_BYTE_TO_WRITE
        sta     ($E4),y
        sec
L8C0E_RTS:
        rts
; ----------------------------------------------------------------------------
L8C0F_DIR_RELATED:
        pha
        lda     V1541_ACTIV_EA
L8C12:  inc     a
        bne     L8C17
        inc     V1541_ACTIV_E9
L8C17:  cmp     #$03
        bcc     L8C12
L8C1B:  sta     V1541_ACTIV_EA
        jsr     L8AD5_MAYBE_READS_BLOCK_HEADER
        pla
        bcc     L8C27_71_DIR_ERROR ;branch if error
        ldy     V1541_ACTIV_EA
        sta     ($E4),y
L8C27_71_DIR_ERROR:
        lda     #doserr_71_dir_error ;71 directory error
        rts

; ----------------------------------------------------------------------------
L8C2A_JSR_V1541_SELECT_CHAN_17_JMP_L8C8B_CLEAR_ACTIVE_CHANNEL:
        jsr     V1541_SELECT_CHAN_17
        jmp     L8C8B_CLEAR_ACTIVE_CHANNEL

V1541_SELECT_DIR_CHANNEL_AND_CLEAR_IT:
        jsr     V1541_SELECT_DIR_CHANNEL
        jmp     L8C8B_CLEAR_ACTIVE_CHANNEL
; ----------------------------------------------------------------------------

;Get a channel's 4 bytes of data from the all-channels area
;into the active area.  Returns carry clear on failure, set on success.
V1541_SELECT_CHANNEL_GIVEN_SA:
        ;SA high nib is command, low nib is channel
        lda     SA
        and     #$0F
L8C3A:  .byte   $2C ;skip next 2 bytes

V1541_SELECT_CHAN_17:
        lda     #doschan_17_unknown
        .byte   $2C ;skip next 2 bytes

V1541_SELECT_DIR_CHANNEL:
        lda     #doschan_16_directory

V1541_SELECT_CHANNEL_A:
        cmp     V1541_DEFAULT_CHAN
        beq     L8C66_70_NO_CHANNEL

        pha                               ;Save the requested channel number
        lda     V1541_DEFAULT_CHAN                ;Get the current channel number
        jsr     L8C4D_SWAP_ACTIV_AND_BUF  ;Save the active channel in its slot in all-channels buf

L8C4A:  pla                               ;Get the requested channel number back
        sta     V1541_DEFAULT_CHAN                ;Set it as the active channel number
                                          ;Fall through to get data from all-channels buf into active

;Get buffer index from channel number
L8C4D_SWAP_ACTIV_AND_BUF:
        ;X = ((A+1)*4) - 1       Examples:
        inc     a               ;A=0 -> X=3
        asl     a               ;A=1 -> X=7
        asl     a               ;A=2 -> X=11
        dec     a               ;A=3 -> X=15
        tax                     ;A=4 -> X=19
                                ;A=5 -> X=23

        ldy     #$03
L8C54_LOOP:
        lda     V1541_ACTIV_FLAGS,y
        pha
        lda     V1541_CHAN_BUF,x
        sta     V1541_ACTIV_FLAGS,y
        pla
        sta     V1541_CHAN_BUF,x
        dex
        dey
        bpl     L8C54_LOOP

L8C66_70_NO_CHANNEL:
        lda     #doserr_70_no_channel
L8C68:  clc
        ldx     V1541_ACTIV_FLAGS
        beq     L8C6E_RTS
        sec
L8C6E_RTS:
        rts

; ----------------------------------------------------------------------------
L8C6F_V1541_I_INITIALIZE:
        lda     #doschan_14_cmd_app
        jsr     V1541_SELECT_CHANNEL_A
        ldx     #$47
L8C76:  stz     V1541_CHAN_BUF,X
        dex
L8C7A:  bpl     L8C76
        stz     V1541_CMD_LEN
        inx     ;A=0
        txa     ;X=0
        tay     ;Y=0
        sec
        jmp     L9964_STORE_XAY_CLEAR_0217
; ----------------------------------------------------------------------------
V1541_SELECT_CHANNEL_AND_CLEAR_IT:
        jsr     V1541_SELECT_CHANNEL_GIVEN_SA

L8C8B_CLEAR_ACTIVE_CHANNEL:
        ldx     #$03
L8C8B_LOOP:
        stz     V1541_ACTIV_FLAGS,x
        dex
        bpl     L8C8B_LOOP
        sec
        rts
; ----------------------------------------------------------------------------
L8C92:  lda     #$00
        jsr     L8C9F
        bcc     L8C9E_RTS ;branch on error
        jsr     L8C8B_CLEAR_ACTIVE_CHANNEL
        bra     L8C92

L8C9E_RTS:  rts
; ----------------------------------------------------------------------------
L8C9F:  tay
        ldx     #doschan_15_command
L8CA2:  phy
        phx
        txa
        jsr     V1541_SELECT_CHANNEL_A
        plx
        ply
        lda     V1541_ACTIV_FLAGS
        beq     L8CB6
        bit     #$80
        bne     L8CB6
        cpy     V1541_ACTIV_E8
        beq     L8CBA
L8CB6:  dex
        bpl     L8CA2
        clc
L8CBA:  rts
; ----------------------------------------------------------------------------
L8CBB_CLEAR_ACTIVE_CHANNEL_EXCEPT_FLAGS_THEN_BRA_L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6:
        stz     V1541_ACTIV_E9
        stz     V1541_ACTIV_EA
        stz     V1541_ACTIV_E8
        bra     L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6
; ----------------------------------------------------------------------------
;returns cbm dos error code in A
L8CC3:  jsr     L8CE6
        bcc     L8CD1 ;branch if error
        clc
        bit     SXREG
        bmi     L8CD1

L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6:
        jsr     L8CE6
L8CD1:  ldx     V1541_ACTIV_EA
        ldy     $03A7
        stx     $03A7
        sty     V1541_ACTIV_EA
        ldx     V1541_ACTIV_E9
        ldy     $03A6
        stx     $03A6
        sty     V1541_ACTIV_E9
        rts
; ----------------------------------------------------------------------------
;returns cbm dos error code in A
L8CE6:  ldx     V1541_ACTIV_EA
        ldy     V1541_ACTIV_E9
        stx     $03A7
        sty     $03a6
        stz     $02D8
        ldx     #$FF
L8CF5_LOOP:
        inx
        cpx     #$19
        beq     L8D13_67_ILLEGAL_SYS_TS
        phx
        jsr     L8B66_CHRIN_V1541_ACTIV_E8_NONZERO
        plx
        bcc     L8D15_CLC_RTS ;branch if error
        sta     V1541_DATA_BUF,x
        cpx     #$05
        bit     SXREG
        bmi     L8D12_RTS
        bcc     L8CF5_LOOP
        cmp     #$00
        bne     L8CF5_LOOP
        sec
L8D12_RTS:
        rts

L8D13_67_ILLEGAL_SYS_TS:
        lda     #doserr_67_illegal_sys_ts ;67 illegal system t or s
L8D15_CLC_RTS:
        clc
        rts
; ----------------------------------------------------------------------------
;maybe returns cbm dos error in a
L8D17:  jsr     L8C92
        jsr     V1541_SELECT_DIR_CHANNEL
        jsr     L8CE6
        bcc     L8D3C ;branch if error
L8D22:  bit     SXREG
        bmi     L8D3C
L8D27:  jsr     L8B66_CHRIN_V1541_ACTIV_E8_NONZERO ;maybe returns cbm dos error in a
        bcc     L8D5A_RTS ;branch if error
        jsr     L8CD1
        jsr     L8C0F_DIR_RELATED ;maybe returns cbm dos error in a
        bcc     L8D5A_RTS
        jsr     L8CD1
        bit     SXREG
        bpl     L8D27
L8D3C:  jsr     L8CD1
        jsr     L8AD5_MAYBE_READS_BLOCK_HEADER
        lda     #doserr_71_dir_error ;71 directory error
        bcc     L8D5A_RTS ;branch if error
        lda     V1541_ACTIV_EA
        beq     L8D50
        ldy     #$02
        sta     ($E4),y
        inc     V1541_ACTIV_E9
L8D50:  jsr     L89F9
        jsr     V1541_SELECT_DIR_CHANNEL_AND_CLEAR_IT
        jsr     L8E39
        sec
L8D5A_RTS:
        rts
; ----------------------------------------------------------------------------
L8D5B_UNKNOWN_DIR_RELATED:
        jsr     L8E10
        lda     #$30
        trb     V1541_DATA_BUF
L8D63:  jsr     L8E91
        bcc     L8D9E_ERROR_OR_DONE ;branch if error
        jsr     L8C92
        jsr     V1541_SELECT_DIR_CHANNEL_AND_CLEAR_IT
        lda     #$10 ;file is open for reading?
        sta     V1541_ACTIV_FLAGS
L8D72:  jsr     L8B66_CHRIN_V1541_ACTIV_E8_NONZERO
        bcs     L8D7A_NO_ERROR ;branch if no error
        lda     #doserr_71_dir_error ;maybe: 71 directory error
        rts

L8D7A_NO_ERROR:
        lda     SXREG
        bpl     L8D72
        lda     #$20 ;file open for writing?
        tsb     V1541_ACTIV_FLAGS
        ldx     #$FF
L8D85_LOOP:
        inx
        phx
        lda     V1541_DATA_BUF,x
        jsr     L8BD4
        plx
        cpx     #$05
        bcc     L8D85_LOOP
        lda     V1541_DATA_BUF,x
        bne     L8D85_LOOP
        jsr     V1541_SELECT_DIR_CHANNEL_AND_CLEAR_IT
        jsr     L8E39
        sec
L8D9E_ERROR_OR_DONE:
        rts
; ----------------------------------------------------------------------------
;returns cbm dos error code in a
;carry clear = file exists, carry set = not found
L8D9F_SELECT_DIR_CHANNEL_AND_CLEAR_IT_THEN_UNKNOWN_THEN_FILENAME_COMPARE:
        jsr     V1541_SELECT_DIR_CHANNEL_AND_CLEAR_IT
        jsr     L8CBB_CLEAR_ACTIVE_CHANNEL_EXCEPT_FLAGS_THEN_BRA_L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6
        bra     L8DAA

L8DA7:  jsr     L8CC3

L8DAA:  bcc     L8DBA_62_FILE_NOT_FOUND ;branch if error from L8CBB_CLEAR_ACTIVE_CHANNEL_EXCEPT_FLAGS_THEN_BRA_L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6 or L8CC3
        jsr     L8FC3_COMPARE_FILENAME_INCL_WILDCARDS
        bcc     L8DB5_NO_MATCH ;filename does not match
        lda     V1541_DATA_BUF
        rts

L8DB5_NO_MATCH:
        bit     SXREG
        bpl     L8DA7
L8DBA_62_FILE_NOT_FOUND:
        clc
        lda     #doserr_62_file_not_found
        rts
; ----------------------------------------------------------------------------
;returns cbm dos error code in a
L8DBE_UNKNOWN_CALLS_DOES_62_FILE_NOT_FOUND_ON_ERROR:
        pha
        jsr     V1541_SELECT_DIR_CHANNEL_AND_CLEAR_IT
        jsr     L8CBB_CLEAR_ACTIVE_CHANNEL_EXCEPT_FLAGS_THEN_BRA_L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6
        bra     L8DCA_SKIP_LD8C7

L8DC7:  jsr     L8CC3

L8DCA_SKIP_LD8C7:
        bcc     L8DDC_62_FILE_NOT_FOUND ;branch if error
        lda     V1541_DATA_BUF
        bit     #$80
        bne     L8DC7
        tsx
        lda     V1541_DATA_BUF+1
        cmp     stack+1,x
        bne     L8DC7

L8DDC_62_FILE_NOT_FOUND:
        pla
        lda     #doserr_62_file_not_found
        rts
; ----------------------------------------------------------------------------
;Returns number of blocks used in A
L8DE0_SOMEHOW_GETS_FILE_BLOCKS_USED_1:
        lda     V1541_DATA_BUF+1
        .byte   $2C
L8DE4_SOMEHOW_GETS_FILE_BLOCKS_USED_2:
        lda     #$00
        ldx     #$00
        phx
        phx
        pha
        jsr     L8A81
        beq     L8E0A
L8DF0:  tsx
        lda     $020E
        cmp     stack+1,x
        bne     L8E05
        inc     stack+2,x
        ldy     #$02
        lda     ($E4),y
        beq     L8E05
        sta     stack+3,x
L8E05:  jsr     L8A61
        bcs     L8DF0
L8E0A:  pla
        pla
        ply
        cmp     #$00
        rts
; ----------------------------------------------------------------------------
L8E10:  lda     V1541_DATA_BUF+1
        jsr     L8E5E
        sta     V1541_DATA_BUF+2
        stx     V1541_DATA_BUF+3
        sty     V1541_DATA_BUF+4
        rts
; ----------------------------------------------------------------------------
L8E20_MAYBE_CHECKS_HEADER:
        lda     V1541_DATA_BUF+1
        jsr     L8E5E
        cmp     V1541_DATA_BUF+2
        bne     L8E35_27_CHECKSUM_ERROR_IN_HEADER
        cpx     V1541_DATA_BUF+3
        bne     L8E35_27_CHECKSUM_ERROR_IN_HEADER
        cpy     V1541_DATA_BUF+4
        beq     L8E36
L8E35_27_CHECKSUM_ERROR_IN_HEADER:
        clc
L8E36:  lda     #doserr_27_read_error ;27 read error (checksum error in header)
        rts
; ----------------------------------------------------------------------------
L8E39:  jsr     L8E5C
        sta     $0213
        stx     $0214
        sty     $0215
        rts
; ----------------------------------------------------------------------------
L8E46:  jsr     L8E5C
        cmp     $0213
        bne     L8E58
        cpx     $0214
        bne     L8E58
        cpy     $0215
        beq     L8E59
L8E58:  clc
L8E59:  lda     #$1B
        rts
; ----------------------------------------------------------------------------
L8E5C:  lda     #$00                            ; 8E5C A9 00                    ..
L8E5E:  ldx     #$00                            ; 8E5E A2 00                    ..
        phx                                     ; 8E60 DA                       .
        phx                                     ; 8E61 DA                       .
        pha                                     ; 8E62 48                       H
        jsr     L8A81                           ; 8E63 20 81 8A                  ..
        beq     L8E8D                           ; 8E66 F0 25                    .%
        lda     #$00                            ; 8E68 A9 00                    ..
L8E6A:  tsx                                     ; 8E6A BA                       .
        ldy     stack+1,x                       ; 8E6B BC 01 01                 ...
        cpy     $020E                           ; 8E6E CC 0E 02                 ...
        bne     L8E86                           ; 8E71 D0 13                    ..
        ldy     #$00                            ; 8E73 A0 00                    ..
        clc                                     ; 8E75 18                       .
L8E76:  adc     ($E4),y                         ; 8E76 71 E4                    q.
        bcc     L8E83                           ; 8E78 90 09                    ..
        clc                                     ; 8E7A 18                       .
        inc     stack+2,x                       ; 8E7B FE 02 01                 ...
        bne     L8E83                           ; 8E7E D0 03                    ..
        inc     stack+3,x                       ; 8E80 FE 03 01                 ...
L8E83:  iny                                     ; 8E83 C8                       .
        bne     L8E76                           ; 8E84 D0 F0                    ..
L8E86:  pha                                     ; 8E86 48                       H
        jsr     L8A61                           ; 8E87 20 61 8A                  a.
        pla                                     ; 8E8A 68                       h
        bcs     L8E6A                           ; 8E8B B0 DD                    ..
L8E8D:  plx                                     ; 8E8D FA                       .
        plx                                     ; 8E8E FA                       .
        ply                                     ; 8E8F 7A                       z
        rts                                     ; 8E90 60                       `
; ----------------------------------------------------------------------------
L8E91:  jsr     L8DE4_SOMEHOW_GETS_FILE_BLOCKS_USED_2
        beq     L8EA7_BLOCKS_USED_0 ;branch if blocks used = 0
        tya
        ldx     #$FF
L8E99_LOOP:
        inc     a
        beq     L8EA7_BLOCKS_USED_0 ;branch if just-incremented blocks used = 0
        inx
        cpx     #$05
        bcc     L8E99_LOOP
        lda     V1541_DATA_BUF+5,x
        bne     L8E99_LOOP
        rts

L8EA7_BLOCKS_USED_0:
        jsr     L8A39_V1541_DOESNT_WRITE_BUT_CONDITIONALLY_RETURNS_WRITE_ERROR
        bcc     L8EAE_RTS ;branch if error
        lda     #$01
L8EAE_RTS:
        rts
; ----------------------------------------------------------------------------
L8EAF_COPY_FNADR_FNLEN_THEN_SETUP_FOR_FILE_ACCESS:
        lda     FNADR
        ldx     FNADR+1
        ldy     FNLEN
L8EB6:  sta     V1541_FNADR
        stx     V1541_FNADR+1
        sty     V1541_FNLEN
        ;Fall through
; ----------------------------------------------------------------------------
L8EBD_SETUP_FOR_FILE_ACCESS_AND_DO_DIR_SEARCH_STUFF:
        stz     $03A5
        stz     V1541_FILE_MODE
        stz     V1541_FILE_TYPE
        stz     BAD
        lda     #V1541_FNADR ;ZP-address
        sta     SINNER ;Y-index for (ZP),Y
        lda     V1541_FNLEN
        bne     L8ED7

L8ED3_33_SYNTAX_ERROR:
        lda     #doserr_33_syntax_err ;33 invalid filename
        clc
        rts

L8ED7:  ldy     #$00
        jsr     L8FAD_GET_AND_CHECK_NEXT_CHAR_OF_FILENAME
        dey
        bcc     L8ED3_33_SYNTAX_ERROR
        cmp     #'$'
        beq     L8EE7_GOT_DOLLAR
        cmp     #'@'
        bne     L8EEB_GOT_AT
L8EE7_GOT_DOLLAR:
        iny
        sta     BAD
L8EEB_GOT_AT:
        sty     MON_MMU_MODE

L8EEE_NEXT_CHAR:
        sty     $03A2
        cpy     V1541_FNLEN
        bne     L8EF9
        jmp     L8F86

;Looks like filename parsing for directory listing LOAD"$0:*=P"
L8EF9:  jsr     L8FAD_GET_AND_CHECK_NEXT_CHAR_OF_FILENAME
        bcc     L8ED3_33_SYNTAX_ERROR
        tax
        cpx     #' '
        beq     L8EEE_NEXT_CHAR
        cpx     #'0'
        beq     L8EEE_NEXT_CHAR
        cpx     #'9'+1
        bne     L8F14
        lda     #$03
        tsb     $03A5
        bne     L8ED3_33_SYNTAX_ERROR
        bra     L8EEB_GOT_AT
L8F14:  lda     #$02
        tsb     $03A5
        cpx     #'='
        beq     L8F81_GOT_EQUALS
        cpx     #'?'
        beq     L8F25_GOT_QUESTION_OR_STAR
        cpx     #'*'
        bne     L8F2A
L8F25_GOT_QUESTION_OR_STAR:
        lda     #$40
        tsb     $03A5
L8F2A:  cpx     #','
        bne     L8EEE_NEXT_CHAR
        dey
L8F2F_NEXT_CHAR:
        cpy     V1541_FNLEN
        beq     L8F86
        jsr     L8FAD_GET_AND_CHECK_NEXT_CHAR_OF_FILENAME
        bcc     L8F5F_33_SYNTAX_ERROR
        cmp     #'='
        beq     L8F81_GOT_EQUALS
        cmp     #' '
        beq     L8F2F_NEXT_CHAR
        cmp     #','
        bne     L8F5F_33_SYNTAX_ERROR

L8F45_LOOP:
        cpy     V1541_FNLEN
        bcs     L8F5F_33_SYNTAX_ERROR
        jsr     L8FAD_GET_AND_CHECK_NEXT_CHAR_OF_FILENAME
        bcc     L8F5F_33_SYNTAX_ERROR
        cmp     #' '
        beq     L8F45_LOOP
        and     #$DF

        ldx     #$05
L8F57_SPRWAM_SEARCH_LOOP:
        cmp     L8F7B_SPRWAM,x
        beq     L8F63_FOUND_IN_SPRWAM
        dex
        bpl     L8F57_SPRWAM_SEARCH_LOOP

L8F5F_33_SYNTAX_ERROR:
        lda     #doserr_33_syntax_err ;Invalid filename
        clc
        rts
; ----------------------------------------------------------------------------
L8F63_FOUND_IN_SPRWAM:
        cpx     #$02
        bcs     L8F71_RWAM
        ;PR
        ldx     V1541_FILE_TYPE
        bne     L8F5F_33_SYNTAX_ERROR
        sta     V1541_FILE_TYPE
        bra     L8F2F_NEXT_CHAR
L8F71_RWAM:
        ;RWAM
        ldx     V1541_FILE_MODE
        bne     L8F5F_33_SYNTAX_ERROR
        sta     V1541_FILE_MODE
        bra     L8F2F_NEXT_CHAR

L8F7B_SPRWAM:
        .byte ftype_s_seq, ftype_p_prg
        .byte fmode_r_read, fmode_w_write, fmode_a_append, fmode_m_modify

L8F81_GOT_EQUALS:
        lda     #$20
        tsb     $03A5
L8F86:  lda     MON_MMU_MODE
        cmp     $03A2
        bcc     L8F96
        stz     $03A2
        stz     MON_MMU_MODE
        bcs     L8F9B
L8F96:  lda     #$80
        tsb     $03A5
L8F9B:  cld
        clc
        lda     #$10
        adc     MON_MMU_MODE
        cmp     $03A2
        lda     #doserr_33_syntax_err ;33 syntax error
        bcc     L8FAC_RTS
        lda     $03a5
L8FAC_RTS:  rts
; ----------------------------------------------------------------------------
;Get the next character from the filename and check
;if it contains a disallowed character.
;
;Returns A=char, carry=clear on error
L8FAD_GET_AND_CHECK_NEXT_CHAR_OF_FILENAME:
        jsr     GO_RAM_LOAD_GO_KERN  ;get the char
        iny
L8FB1:  ldx     #$03
L8FB3_LOOP:
        cmp L8FBF_DIASLLOWED_FNAME_CHARS,X
        bne L8FBA_NOT_EQU
        clc ;Found a bad character
        rts
L8FBA_NOT_EQU:
        dex
        bpl     L8FB3_LOOP
        sec
        rts

L8FBF_DIASLLOWED_FNAME_CHARS:
       .byte $00 ;null
       .byte $0d ;return
       .byte $22 ;quote
       .byte $8d ;shift-return
; ----------------------------------------------------------------------------
;Compare filename at V1541_DATA_BUF+5 with indirect filename
;carry set = filename matches
L8FC3_COMPARE_FILENAME_INCL_WILDCARDS:
        ldx     #$00
        ldy     MON_MMU_MODE
        lda     #V1541_FNADR ;ZP-address
        sta     SINNER
L8FCD_LOOP:
        jsr     GO_RAM_LOAD_GO_KERN ;get char from filename
L8FD0:  cmp     #'*'
        beq     L8FE9_SUCCESS_FILENAME_MATCHES
        cmp     #'?'
        beq     L8FDD_ANY_ONE_CHAR
        cmp     V1541_DATA_BUF+5,x
        bne     L8FF1_FAIL_FILENAME_DOES_NOT_MATCH
L8FDD_ANY_ONE_CHAR:
        iny
        cpy     $03A2
        bne     L8FEB
        inx
        lda     V1541_DATA_BUF+5,x
        bne     L8FF1_FAIL_FILENAME_DOES_NOT_MATCH
L8FE9_SUCCESS_FILENAME_MATCHES:
        sec
        rts
L8FEB:  inx
        lda     V1541_DATA_BUF+5,X
        bne     L8FCD_LOOP
L8FF1_FAIL_FILENAME_DOES_NOT_MATCH:
        clc
        rts
; ----------------------------------------------------------------------------
L8FF3:  stz     $02D8
        lda     #V1541_FNADR ;ZP-address
        sta     SINNER
        ldx     #$00
        ldy     MON_MMU_MODE
L9000_LOOP:
        jsr     GO_RAM_LOAD_GO_KERN
        sta     V1541_DATA_BUF+5,x
        inx
        iny
        cpy     $03A2
        bne     L9000_LOOP
        stz     V1541_DATA_BUF+5,x
        rts
; ----------------------------------------------------------------------------
;maybe returns cbm dos error code in A
L9011_TEST_0218_AND_STORE_FILE_TYPE:
        ldx     #ftype_s_seq
        lda     V1541_DATA_BUF
        bit     #$40
        beq     L901C_GOT_SEQ
        ldx     #ftype_p_prg
L901C_GOT_SEQ:
        lda     #'@'
        cpx     V1541_FILE_TYPE
        beq     L9029_STORE_TYPE_AND_RTS
        ldy     V1541_FILE_TYPE
L9026:
        beq     L9029_STORE_TYPE_AND_RTS
        clc
L9029_STORE_TYPE_AND_RTS:
        stx     V1541_FILE_TYPE
        rts
; ----------------------------------------------------------------------------
L902D:  ldx     #$04
        lda     V1541_DATA_BUF
        bit     #$80
        bne     L9038
        ldx     #$01
L9038:  lda     V1541_DATA_BUF,x
        sta     V1541_ACTIV_FLAGS,x
        dex
        bpl     L9038
        rts
; ----------------------------------------------------------------------------
L9041:  jsr     L8C92
        stz     V1541_02D6
        lda     #'*'
        sta     $0238
        stz     $0239
        stz     $024C
        lda     $03A5
L9055:  bit     #$80
        beq     L907D_SEC_RTS
        ldx     #$00
        ldy     MON_MMU_MODE
L905E:  lda     #V1541_FNADR ;ZP-address
        sta     SINNER
        jsr     GO_RAM_LOAD_GO_KERN
        sta     $0238,x
        iny
        inx
        cpy     $03A2
        bne     L905E
        cpx     #$14
        bcs     L9077
        stz     $0238,x
L9077:  LDA     V1541_FILE_TYPE
        sta     $024C
L907D_SEC_RTS:
        sec
        rts
; ----------------------------------------------------------------------------
        ldx     EAL
        ldy     EAH
        sec
        rts
; ----------------------------------------------------------------------------
L9085_V1541_SAVE:
        jsr     L908B_V1541_INTERNAL_SAVE
        jmp     V1541_KERNAL_CALL_DONE
; ----------------------------------------------------------------------------
L908B_V1541_INTERNAL_SAVE:
        ldx     STAH
        lda     STAL
        sta     SAL
        stx     SAH
        cpx     #>$08F8
        bcc     L90DD_25_WRITE_ERROR
        cpx     #<$08F8
        bcs     L90DD_25_WRITE_ERROR
        lda     EAH
        cmp     #>$F800
        bcc     L90A7
        bne     L90DD_25_WRITE_ERROR
        lda     EAL
        bne     L90DD_25_WRITE_ERROR
L90A7:  jsr     L8EAF_COPY_FNADR_FNLEN_THEN_SETUP_FOR_FILE_ACCESS
        bcc     L90DA_33_SYNTAX_ERROR
        bit     #$80
        beq     L90DA_33_SYNTAX_ERROR
        bit     #$60
        bne     L90DA_33_SYNTAX_ERROR
        lda     V1541_FILE_MODE
        bne     L90DA_33_SYNTAX_ERROR
        lda     BAD
        beq     L90C2
        cmp     #$40 ;'@'
        bne     L90DA_33_SYNTAX_ERROR

L90C2:  jsr     L8D9F_SELECT_DIR_CHANNEL_AND_CLEAR_IT_THEN_UNKNOWN_THEN_FILENAME_COMPARE
        bcc     L90EC_ERROR ;branch if error (file not found)

        lda     BAD
        bne     L90D0_03A0_NOT_ZERO
        lda     #doserr_63_file_exists ;63 file exists
        bra     L90DF_ERROR

L90D0_03A0_NOT_ZERO:
        lda     V1541_DATA_BUF
        and     #$80
        beq     L90E1
        lda     #doserr_26_write_prot_on ;#26 write protect on
        .byte   $2C
L90DA_33_SYNTAX_ERROR:
        lda     #doserr_33_syntax_err  ;33 syntax error (invalid filename)
        .byte   $2C
L90DD_25_WRITE_ERROR:
        lda     #doserr_25_write_err ;25 write error (write-verify error)
L90DF_ERROR:
        clc
        rts

L90E1:  jsr     L9011_TEST_0218_AND_STORE_FILE_TYPE ;maybe returns cbm dos error code in A
        bcc     L90DF_ERROR ;branch if error
        jsr     L8DE0_SOMEHOW_GETS_FILE_BLOCKS_USED_1
        inc     a ;increment number of blocks used
        bra     L910D

L90EC_ERROR:
        stz     BAD
        jsr     L8B13_MAYBE_ALLOCATES_SPACE_OR_CHECKS_DISK_FULL ;maybe returns cbm dos error code in A                               ; 90EF 20 13 8B                  ..
L90F2:  bcc     L90DF_ERROR ;branch if error
        jsr     L8FF3
        stz     V1541_DATA_BUF
        lda     V1541_FILE_TYPE
        cmp     #ftype_s_seq
        beq     L9106
        lda     #$40
        sta     V1541_DATA_BUF
L9106:  jsr     L8E91
        bcc     L90DF_ERROR
        eor     #$01
L910D:  pha                         ;push number of blocks used
        jsr     L91A4
        sty     V1541_DATA_BUF+2
        pla                         ;pull number of blocks used
        clc
        adc     $020A
        ldx     $020B
        bcc     L911F
        inx
L911F:  clc
        sbc     V1541_DATA_BUF+2
        bcs     L9126
        dex
L9126:  tay
        bne     L912A
        dex
L912A:  dec     a
        cpx     $020D
        bcc     L9137
        bne     L913A
        cmp     $020C
        bcs     L913A
L9137:  jmp     L90DD_25_WRITE_ERROR
; ----------------------------------------------------------------------------
L913A:  cpx     #$00
        bne     L9145
        cmp     STAH
        bcs     L9145
        jsr     L91D5
L9145:  lda     BAD
        beq     L9150
        jsr     L89E2
        jsr     L8D17 ;maybe returns cbm dos error in a
L9150:  jsr     L91A4
        cpy     #$00
        beq     L91A1
        dey
        sty     V1541_DATA_BUF+2
        sta     V1541_DATA_BUF+3
        lda     #$E0 ;ZP-address
        sta     SINNER
L9163:  jsr     L89AF
        ldy     #$FF
L9168:  jsr     GO_RAM_LOAD_GO_KERN
        sta     ($E4),y
        dey
        bne     L9168
        ldy     #$02
L9172:  lda     V1541_DATA_BUF+1,y
        sta     ($E4),y
        dey
        bpl     L9172
        sec
        lda     $E0
        sbc     #$FD
        bcs     L9183
        dec     $E1
L9183:  sta     $E0
        stz     V1541_DATA_BUF+3
        ldy     V1541_DATA_BUF+2
        dec     V1541_DATA_BUF+2
        tya
        bne     L9163
        bit     V1541_DATA_BUF
        bvc     L91A1
        ldy     #$04
        lda     SAH
        sta     ($E4),y
        dey
        lda     SAL
        sta     ($E4),y
L91A1:  jmp     L8D5B_UNKNOWN_DIR_RELATED
; ----------------------------------------------------------------------------
L91A4:  ldx     STAH
        lda     STAL
        bit     V1541_DATA_BUF
        bvc     L91B3
        sec
        sbc     #$02
        bcs     L91B3
        dex
L91B3:  ldy     #$00
L91B5:  cpx     EAH
        bcc     L91BD
        cmp     EAL
        bcs     L91C9
L91BD:  adc     #$FD
        bcc     L91C2
        inx
L91C2:  iny
        bne     L91B5
        lda     #$34
        clc
        rts
; ----------------------------------------------------------------------------
L91C9:  dex
        sta     $E0
L91CC:  stx     $E1
        clc
        lda     EAL
        sbc     $E0
        sec
        rts
; ----------------------------------------------------------------------------
L91D5:  pha
        sta     $E1
        stz     $E0
        lda     #STAH
        sta     SINNER
        lda     #$E0
        sta     $0360
L91E4:  lda     STAH
        cmp     EAH
        bne     L91F0
        lda     $B6
        cmp     EAL
        beq     L9206
L91F0:  ldy     #$00
        jsr     GO_RAM_LOAD_GO_KERN
        jsr     GO_RAM_STORE_GO_KERN
        inc     $E0
        bne     L91FE
L91FC:  inc     $E1
L91FE:  inc     $B6
        bne     L9204
        inc     STAH
L9204:  bra     L91E4
L9206:  lda     $E0
        sta     EAL
        lda     $E1
        sta     EAH
        pla
        sta     STAH
        stz     STAL
        rts
; ----------------------------------------------------------------------------
V1541_CLOSE:
        jsr     L921A_V1541_INTERNAL_CLOSE
        jmp     V1541_KERNAL_CALL_DONE
; ----------------------------------------------------------------------------
L921A_V1541_INTERNAL_CLOSE:
        jsr     V1541_SELECT_CHANNEL_GIVEN_SA
        bcs     L9221 ;branch if no error
        sec
        rts
L9221:  lda     V1541_DEFAULT_CHAN
        cmp     #doschan_15_command ;command channel?
        beq     L9240_RTS
        lda     V1541_ACTIV_FLAGS
        bit     #$20 ;is file open for writing?
        beq     L9240_RTS
        bit     #$80
        bne     L9240_RTS
        lda     V1541_ACTIV_E8
        beq     L9240_RTS
        jsr     L8DBE_UNKNOWN_CALLS_DOES_62_FILE_NOT_FOUND_ON_ERROR
        bcc     L9240_RTS ;branch if error
        jsr     L8D17
        jsr     L8D5B_UNKNOWN_DIR_RELATED
L9240_RTS:
        jmp     V1541_SELECT_CHANNEL_AND_CLEAR_IT
; ----------------------------------------------------------------------------
L9243_OPEN_V1541:
        jsr     L9249_V1541_INTERNAL_OPEN
        jmp     V1541_KERNAL_CALL_DONE
; ----------------------------------------------------------------------------
L9249_V1541_INTERNAL_OPEN:
        jsr     V1541_SELECT_CHANNEL_GIVEN_SA
        lda     V1541_DEFAULT_CHAN
        cmp     #doschan_15_command ;command channel
L9250:  bne     L9255_V1541_INTERNAL_OPEN_NOT_CMD_CHAN
        jmp     L9737_V1541_INTERNAL_OPEN_CMD_CHAN

        ;not the command channel

L9255_V1541_INTERNAL_OPEN_NOT_CMD_CHAN:
        jsr     L8C8B_CLEAR_ACTIVE_CHANNEL
        jsr     L8EAF_COPY_FNADR_FNLEN_THEN_SETUP_FOR_FILE_ACCESS
        bcc     L9287_ERROR
        bit     #$20
        bne     L9282
        LDX     BAD
        beq     L928C
        cpx     #'$'
L9268:  bne     L927B_NOT_DOLLAR
        ldx     V1541_FILE_MODE
        bne     L9287_ERROR
        jsr     V1541_SELECT_CHANNEL_GIVEN_SA
        jsr     L9041
        lda     #$50
        sta     V1541_ACTIV_FLAGS
        sec
L927A:  rts
; ----------------------------------------------------------------------------
L927B_NOT_DOLLAR:
        ldy     V1541_FILE_MODE
L927E:  cpy     #fmode_w_write
        beq     L9289
L9282:  lda     #doserr_33_syntax_err ;33 syntax error (invalid filename)
        .byte   $2C ;skip next two bytes
L9285:  lda #$21 ;33 syntax error (invalid filename)
L9287_ERROR:  clc
        rts
; ----------------------------------------------------------------------------
L9289:  stx     V1541_FILE_MODE
L928C:  bit     #$80
        beq     L9282
        ldy     #fmode_r_read
        ldx     V1541_FILE_MODE
        bne     L929A
        sty     V1541_FILE_MODE
L929A:  bit     #$40
        beq     L92A3
        cpx     V1541_FILE_MODE
        bne     L9285

L92A3:  jsr     L8D9F_SELECT_DIR_CHANNEL_AND_CLEAR_IT_THEN_UNKNOWN_THEN_FILENAME_COMPARE
        bcc     L9317_DO_STUFF_WITH_FILE_TYPE_AND_MODE ;branch if error (file not found)

        jsr     L9011_TEST_0218_AND_STORE_FILE_TYPE
        bcc     L92C7_ERROR

        ldy     V1541_FILE_MODE
        lda     #doserr_63_file_exists ;63 file exists
        cpy     #fmode_w_write
        beq     L92C7_ERROR
        lda     V1541_DATA_BUF
        bit     #$80
        beq     L92C9
        lda     #doserr_26_write_prot_on ;26 write protect on
        cpy     #$40
        beq     L92C7_ERROR
        cpy     #$41
        bne     L9315
L92C7_ERROR:
        clc
        rts
; ----------------------------------------------------------------------------
L92C9:  lda     V1541_DATA_BUF+1
        jsr     L8C9F
        bcc     L92E6
        lda     V1541_ACTIV_FLAGS
        and     #$20
        beq     L92DB
        lda     #doserr_60_write_file_open ;60 write file open
        bra     L92C7_ERROR
L92DB:  ldy     V1541_FILE_MODE
        cpy     #fmode_r_read
        beq     L92F8
L92E2:  lda     #doserr_60_write_file_open ;60 write file open
        bra     L92C7_ERROR
L92E6:  lda     V1541_DATA_BUF
        bit     #$20
        beq     L92F8
        ldy     V1541_FILE_MODE
        cpy     #fmode_m_modify
        beq     L92F8
        cpy     #$40
        bne     L92E2
L92F8:  ldy     V1541_FILE_MODE
        cpy     #'@' ;TODO weird for a mode maybe somehow A?
        bne     L930C
        jsr     L8D17 ;maybe returns cbm dos error in a
        jsr     L89E2
        lda     #fmode_w_write
        sta     V1541_FILE_MODE
        bra     L9317_DO_STUFF_WITH_FILE_TYPE_AND_MODE
L930C:  cpy     #fmode_m_modify
        beq     L9315
        jsr     L8E20_MAYBE_CHECKS_HEADER
        bcc     L92C7_ERROR ;branch if error
L9315:  bra     L9335

L9317_DO_STUFF_WITH_FILE_TYPE_AND_MODE:
        ldy     V1541_FILE_MODE
        cpy     #fmode_r_read
        beq     L9322_ERROR
        cpy     #fmode_m_modify
        bne     L9326
L9322_ERROR:
        lda     #doserr_62_file_not_found ;62 file not found
        clc
        rts

L9326:  lda     #fmode_w_write
        sta     V1541_FILE_MODE
        lda     V1541_FILE_TYPE
        bne     L9335
        lda     #ftype_s_seq
        sta     V1541_FILE_TYPE
L9335:  ldy     V1541_FILE_MODE
        cpy     #fmode_w_write
        bne     L935A
        jsr     L8B13_MAYBE_ALLOCATES_SPACE_OR_CHECKS_DISK_FULL
        bcc     L9358_ERROR ;branch on error
        jsr     L8FF3
        stz     V1541_DATA_BUF
        lda     V1541_FILE_TYPE
        cmp     #ftype_p_prg
        bne     L9353
        lda     #$40
        sta     V1541_DATA_BUF
L9353:  jsr     L8E91
        bcs     L935A
L9358_ERROR:
        clc
        rts

L935A:  jsr     V1541_SELECT_CHANNEL_GIVEN_SA
        jsr     L902D
        lda     #$10
        tsb     V1541_ACTIV_FLAGS
        ldy     V1541_FILE_MODE
L9367:  cpy     #fmode_m_modify
        beq     L9378
        cpy     #fmode_r_read
        bne     L937A
        lda     V1541_DEFAULT_CHAN
        cmp     #$0E
        bne     L9378
        dec     LDTND
L9378:  sec
        rts

L937A:  cpy     #$41
        bne     L938D
        jsr     L8D17  ;maybe returns cbm dos error in a
L9381:  jsr     L8B40_V1541_INTERNAL_CHRIN
        lda     #$47
        bcc     L9358_ERROR
        bit     SXREG
        bpl     L9381
L938D:  jsr     V1541_SELECT_CHANNEL_GIVEN_SA
        lda     #$20
        tsb     V1541_ACTIV_FLAGS
        tsb     V1541_DATA_BUF
        jmp     L8D63

; ----------------------------------------------------------------------------

L939A:  lda     V1541_02D6
L939D:  bne     L93A2
        sta     V1541_02D7
L93A2:  ldx     V1541_02D7
        beq     L93C0
        lda     $02D8
        beq     L941F_GET_DIRPART_ONLY
        lda     $0217,x
        inx
        tay
        bne     L93B8
        cpx     #$0A
        bcc     L93B8
        tax
L93B8:  stx     V1541_02D7
        stz     SXREG
        sec
        rts

L93C0:  ldx     V1541_02D6
        jmp     (L93C6,x)
L93C6:  .addr   L9414_INC_02D6_TWICE_STA_1_02D7_GET_DIRPART
        .addr   L93E4
        .addr   L93E9_LOOP
        .addr   L93D0
        .addr   L93D6

L93D0:  ldx     #$08
        lda     #$00
        bra     L93DA

L93D6:  ldx     #$00
        lda     #$FF
L93DA:  stx     V1541_02D6
        sta     SXREG
        lda     #$00
        sec
        rts

L93E4:  jsr     L8CBB_CLEAR_ACTIVE_CHANNEL_EXCEPT_FLAGS_THEN_BRA_L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6
        bra     L93EC

L93E9_LOOP:
        jsr     L8CC3
L93EC:  ldx     #$04
        stx     V1541_02D6
        bcc     L9414_INC_02D6_TWICE_STA_1_02D7_GET_DIRPART ;branch if error from L8CC3

        lda     #<$0238
        sta     V1541_FNADR
        lda     #>$0238
        stz     MON_MMU_MODE
L93FC:  sta     V1541_FNADR+1
        ldx     #$00
L9400:  lda     $0238,x
L9403:  beq     L940A
        inx
        cpx     #$14
        bne     L9400
L940A:  stx     $03A2
        jsr     L8FC3_COMPARE_FILENAME_INCL_WILDCARDS
        bcc     L93E9_LOOP ;filename does not match
        bra     L941A_STA_1_02D7_GET_DIRPART

L9414_INC_02D6_TWICE_STA_1_02D7_GET_DIRPART:
        inc     V1541_02D6
        inc     V1541_02D6

L941A_STA_1_02D7_GET_DIRPART:
        lda     #$01
        sta     V1541_02D7

L941F_GET_DIRPART_ONLY:
        jsr     L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6
        jsr     L942B_GET_V1541_DIR_PART
        dec     $02D8
        jmp     L939A

L942B_GET_V1541_DIR_PART:
        ldx     V1541_02D6
        jmp     (L942F_V1541_DIR_PART_HANDLERS-2,x)
L942F_V1541_DIR_PART_HANDLERS:
        .addr   L9457_GET_V1541_HEADER
        .addr   L94A8_GET_V1541_FILE
        .addr   L9488_GET_V1541_BLOCKS_USED

L9437_V1541_HEADER:
        .word $1001   ;load address
        .word $1001   ;pointer to next basic line
        .word 0       ;basic line number
        .byte $12,$22,"VIRTUAL 1541    ",$22," ID 00" ;basic line text
        .byte 0       ;end of basic line

;Put the start of the BASIC program and the first BASIC line
;with the disk header in the buffer
L9457_GET_V1541_HEADER:
        ldx     #$1F
L9459_LOOP:
        lda     L9437_V1541_HEADER,x
        sta     V1541_DATA_BUF,x
        dex
        bpl     L9459_LOOP
        jsr     L8DE4_SOMEHOW_GETS_FILE_BLOCKS_USED_2  ;sets A = blocks used
        sta     V1541_DATA_BUF+4  ;basic line number low byte
        rts

L9469_BLOCKS_USED:
        .word $1001   ;pointer to next basic line
        .word 0       ;basic line number
        .byte "BLOCKS USED.            "  ;basic line text
        .byte 0       ;end of basic line
        .byte 0,0     ;end of basic program

;Put a BASIC line with the "BLOCKS USED" in the buffer
L9488_GET_V1541_BLOCKS_USED:
        ldx     #$1E
L948A_LOOP:
        lda     L9469_BLOCKS_USED,x
        sta     V1541_DATA_BUF,x
        dex
        bpl     L948A_LOOP
        cld
        sec
        lda     $0208
        sbc     $020A
        sta     V1541_DATA_BUF+2  ;basic line number low byte
        lda     $0209
        sbc     $020B
        sta     V1541_DATA_BUF+3  ;basic line number high byte
        rts

;Put a BASIC line with a file in the buffer
L94A8_GET_V1541_FILE:
        ldx     #$04
L94AA_LOOP:
        inx
        lda     V1541_DATA_BUF,x
        beq     L94B4
        cpx     #$15
        bne     L94AA_LOOP
L94B4:  lda     #'"'
L94B6:  sta     V1541_DATA_BUF,x
        lda     #' '
        inx
        cpx     #' '
        bne     L94B6

        lda     V1541_DATA_BUF
        bit     #$30
        beq     L94CC
        ldx     #'*' ;splat file like "*PRG"
        stx     V1541_DATA_BUF+22

L94CC:  bit     #$80
        bne     L94D5

        jsr     L8DE0_SOMEHOW_GETS_FILE_BLOCKS_USED_1
        bra     L94D8

L94D5:  lda     V1541_DATA_BUF+3

L94D8:  sta     V1541_DATA_BUF+2  ;Set number of blocks used by file
                                  ;as the line number low byte
        lda     V1541_DATA_BUF
        and     #$40
        beq     L94EA
        lda     #'P' ;ftype_p_prg
        ldx     #'R'
        ldy     #'G'
        bra     L94F0
L94EA:  lda     #'S' ;ftype_s_seq
        ldx     #'E'
        ldy     #'Q'
L94F0:  sta     V1541_DATA_BUF+23   ;P    S
        stx     V1541_DATA_BUF+24   ;R or E
        sty     V1541_DATA_BUF+25   ;G    Q

        lda     #$01
        sta     V1541_DATA_BUF      ;pointer to next basic line (low byte)
        lda     #$10
        sta     V1541_DATA_BUF+1    ;pointer to next basic line (high byte)
        stz     V1541_DATA_BUF+3

        lda     #'"'
        sta     V1541_DATA_BUF+4    ;first byte of basic line (quote before filename)

        lda     V1541_DATA_BUF+2    ;A = size of file in blocks
        cmp     #100
        bcs     L9515 ;branch if >= 100
        jsr     L9522_SHIFT_BASIC_TEXT_RIGHT
L9515:  lda     V1541_DATA_BUF+2    ;A = size of file in blocks again
        cmp     #10
        bcc     L951F ;branch if < 10
        jsr     L9522_SHIFT_BASIC_TEXT_RIGHT
L951F:  jsr     L9522_SHIFT_BASIC_TEXT_RIGHT
        ;Fall through

;Prepend one space to the beginning of the BASIC text.  The number of blocks
;is shown as the BASIC line number.  It can vary (1-3 decimal digits) so these
;spaces are added to the BASIC text to keep the filenames aligned.
L9522_SHIFT_BASIC_TEXT_RIGHT:
        lda     #' '
        ldx     #$04
L9526_LOOP:
        ldy     V1541_DATA_BUF,x
        sta     V1541_DATA_BUF,x
        tya
        inx
        cpx     #$1F
        bne     L9526_LOOP

        lda     #0
        sta     V1541_DATA_BUF+31   ;end of basic line
        rts
; ----------------------------------------------------------------------------
LOAD__: sta     VERCHK
        stz     SATUS
        lda     FA
        bne     L9544
        ;Device 0
L9541_BAD_DEVICE:
        jmp     ERROR9 ;BAD DEVICE #
; ----------------------------------------------------------------------------
L9544:  cmp     #$01
        beq     L9550_LOAD_V1541_OR_IEC
        cmp     #$04
        bcc     L9541_BAD_DEVICE
        cmp     #$1E
        bcs     L9541_BAD_DEVICE
L9550_LOAD_V1541_OR_IEC: ;Device=1 (Virtual 1541), Device=4-29 (IEC)
        ldy     FNLEN
        bne     L9558_LOAD_FNLEN_OK
        jmp     ERROR8 ;MISSING FILE NAME
; ----------------------------------------------------------------------------
L9558_LOAD_FNLEN_OK:
        jsr     LUKING  ;Print "SEARCHING FOR " then do OUTFN
        ldx     SA
        stx     WRBASE  ;Save SA before changes
        stz     SA
        lda     FA
        dec     a
        beq     L957A
        lda     #$60
        sta     SA
        jsr     OPENI
        lda     FA
        jsr     TALK__
        lda     SA
        jsr     TKSA
        bra     L9592
; ----------------------------------------------------------------------------
L957A:  phx
        jsr     V1541_OPEN
        plx
        lda     SATUS
        bit     #$0C
        beq     L9592
L9585:  jmp     ERROR4 ;FILE NOT FOUND
; ----------------------------------------------------------------------------
L9588_CLSEI_OR_ERROR16_OOM:
        lda     SA
        beq     L958F_JMP_ERROR16
        jsr     CLSEI
L958F_JMP_ERROR16:
        jmp     ERROR16 ;OUT OF MEMORY
; ----------------------------------------------------------------------------
L9592:  jsr     L9661
        sta     EAL
        lda     #$02
        bit     SATUS
        bne     L9585
        jsr     L9661
        sta     EAH
        lda     WRBASE  ;Recall SA before changes
        bne     L95AF
        lda     $B4
        sta     EAL
        lda     $B5
        sta     EAH
L95AF:  lda     VERCHK
        bne     L95E4_VERIFY
        jsr     PRIMM80
        .byte   "LOADING",$0d,0
        lda     EAH
        cmp     #>$05F8
        bcc     L9588_CLSEI_OR_ERROR16_OOM
        cmp     #<$05F8
        bcs     L9588_CLSEI_OR_ERROR16_OOM
        cmp     $020A
        bcc     L95D4
        lda     $020B
        beq     L9588_CLSEI_OR_ERROR16_OOM
L95D4:  lda     SA
        bne     L95F0
        LDA     BAD
        cmp     #$40 ;'@'
        bne     L95F0
        jsr     L96D6_USED_BY_LOAD
        bra     L9651_LOAD_OR_VERIFY_DONE
; ----------------------------------------------------------------------------
L95E4_VERIFY:
        jsr     PRIMM80
        .byte   $0d,"VERIFY ",0
L95F0:  lda     #$02
        trb     SATUS
        jsr     LFDB9_STOP
        beq     L9657_STOP_PRESSED
L95F9:  jsr     L9661
        tax
        lda     SATUS
        lsr     a
        lsr     a
        bcs     L95F9
        txa
        ldy     VERCHK
        beq     L9622
        ldy     #$00
        sta     WRBASE                ;save .A
        lda     #EAL
        sta     SINNER
        jsr     GO_RAM_LOAD_GO_KERN
        cmp     WRBASE                ;compare with old .A
        beq     L963D
        lda     #$10
        jsr     UDST
        bra     L963D
L9622:  ldx     #$B2
        stx     $0360
        ldx     EAH
        cpx     #$F8
        bcs     L9637
        cpx     $020A
        bcc     L963A
        ldx     $020B
        bne     L963A
L9637:  jmp     L9588_CLSEI_OR_ERROR16_OOM
; ----------------------------------------------------------------------------
L963A:  jsr     GO_RAM_STORE_GO_KERN
L963D:  inc     EAL
        bne     L9643
        inc     EAH
L9643:  bit     SATUS
        bvc     L95F9
        lda     SA
        beq     L9651_LOAD_OR_VERIFY_DONE
        jsr     UNTLK
        jsr     CLSEI
L9651_LOAD_OR_VERIFY_DONE:
        ldx     EAL
        ldy     EAH
        clc
        rts
; ----------------------------------------------------------------------------
L9657_STOP_PRESSED:
        lda     SA
        bne     L965E
        jsr     CLSEI
L965E:  jmp     ERROR0  ;OK
; ----------------------------------------------------------------------------
L9661:  lda     SA
        beq     L9668
        jmp     ACPTR
L9668:  jmp     L971F
; ----------------------------------------------------------------------------
V1541_OPEN:
        jsr     L9671_V1541_INTERNAL_OPEN
        jmp     V1541_KERNAL_CALL_DONE
; ----------------------------------------------------------------------------
L9671_V1541_INTERNAL_OPEN:
        jsr     L8EAF_COPY_FNADR_FNLEN_THEN_SETUP_FOR_FILE_ACCESS
        BCC     L969A_ERROR ;branch if error
        BIT     #$20
        BNE     L969A_ERROR

        ldx     BAD
        cpx     #'$'
        bne     L969C_03A0_NOT_DOLLAR

        ;Opening the directory

        ldx     V1541_FILE_MODE
        bne     L9698_ERROR_34_SYNTAX_ERROR

        jsr     L9041
        jsr     L8C2A_JSR_V1541_SELECT_CHAN_17_JMP_L8C8B_CLEAR_ACTIVE_CHANNEL
        lda     #$40
        tsb     V1541_ACTIV_FLAGS
        bra     L96C0

L9692_ERROR_64_FILE_TYPE_MISMATCH:
        lda     #doserr_64_file_type_mism
        .byte   $2C
L9695_ERROR_60_WRITE_FILE_OPEN:
        lda     #doserr_60_write_file_open
        .byte   $2C
L9698_ERROR_34_SYNTAX_ERROR:
        lda     #doserr_34_syntax_err
L969A_ERROR:
        clc
        rts

L969C_03A0_NOT_DOLLAR:
        jsr     L8D9F_SELECT_DIR_CHANNEL_AND_CLEAR_IT_THEN_UNKNOWN_THEN_FILENAME_COMPARE
        bcc     L969A_ERROR ;branch if error (file not found)

        lda     V1541_DATA_BUF
        bit     #$20
        bne     L9695_ERROR_60_WRITE_FILE_OPEN
        bit     #$80
        bne     L96B1
        jsr     L8E20_MAYBE_CHECKS_HEADER
        bcc     L969A_ERROR ;branch if error
L96B1:  jsr     L9011_TEST_0218_AND_STORE_FILE_TYPE
        bcc     L969A_ERROR
        cpx     #ftype_s_seq
        beq     L9692_ERROR_64_FILE_TYPE_MISMATCH
        jsr     L8C2A_JSR_V1541_SELECT_CHAN_17_JMP_L8C8B_CLEAR_ACTIVE_CHANNEL
        jsr     L902D

L96C0:  lda     #$10
        tsb     V1541_ACTIV_FLAGS
        lda     BAD
        cmp     #$40 ;'@'
        bne     L96D1
        lda     V1541_ACTIV_FLAGS
        and     #$80
        beq     L96D4
L96D1:  stz     BAD
L96D4:  sec
        rts
; ----------------------------------------------------------------------------
;Called only from load
L96D6_USED_BY_LOAD:
        stz     V1541_ACTIV_E9
        dec     V1541_ACTIV_E9
L96DA_LOOP:
        inc     V1541_ACTIV_E9
        jsr     L89F9
        bcc     L9719
L96E1:  ldx     $020B
        lda     $020A
        bne     L96EA
        dex
L96EA:  dec     a
        jsr     L8A87
        ldy     #$02
        lda     ($E4),y
        bne     L96F5
        dec     a
L96F5:  sta     $E0
        lda     V1541_ACTIV_E9
        bne     L9703
L96FB:  lda     V1541_ACTIV_FLAGS
        and     #$40
        beq     L9703
        iny
        iny
L9703:  iny
        lda     ($E4),y
        phy
        ldy     #$00
        jsr     GO_RAM_STORE_GO_KERN
        inc     EAL
        bne     L9712
        inc     EAH
L9712:  ply
        cpy     $E0
        bne     L9703
        bra     L96DA_LOOP
; ----------------------------------------------------------------------------
L9719:  jsr     L89E2
        jmp     L8D17

L971F:  jsr     L9725 ;maybe returns a cbm dos error code
        jmp     V1541_KERNAL_CALL_DONE
; ----------------------------------------------------------------------------
L9725:  jsr     V1541_SELECT_CHAN_17 ;maybe returns a cbm dos error code
        bcc     L972D_RTS ;branch if error
        jsr     L8B46 ;maybe returns a cbm dos error code
L972D_RTS:  rts
; ----------------------------------------------------------------------------
;Called with Y=cmd len
;If Y>=$3C then return carry=0 and A=32 syntax error (long line)
;          else return carry=1 and A=32 (don't care)
L972E_V1541_CHECK_MAX_CMD_LEN:
        lda     #doserr_32_syntax_err
L9730:  cpy     #$3C ;if Y<3C then OK, else error 32
L9732:  rol     a
        eor     #$01
        ror     a
        rts
; ----------------------------------------------------------------------------
L9737_V1541_INTERNAL_OPEN_CMD_CHAN:
        jsr     V1541_SELECT_CHANNEL_GIVEN_SA
        lda     #$10
        tsb     V1541_ACTIV_FLAGS
        ldy     FNLEN
        sty     V1541_CMD_LEN
        jsr     L972E_V1541_CHECK_MAX_CMD_LEN
        bcs     L974A ;branch if no error
L9749_RTS:
        rts
; ----------------------------------------------------------------------------
L974A:  lda     #FNADR
        sta     SINNER
        dey
        bmi     L9749_RTS
L9752:  jsr     GO_RAM_LOAD_GO_KERN
        sta     V1541_CMD_BUF,y
        dey
        bpl     L9752
        bra     L9772_V1541_INTERPRET_CMD

L975D_V1541_CHROUT_CMD_CHAN:
        ldy     V1541_CMD_LEN
        jsr     L972E_V1541_CHECK_MAX_CMD_LEN
        bcs     L9766_STORE_CHR_IF_0D_INTERP_CMD ;branch if no error
        rts
; ----------------------------------------------------------------------------
L9766_STORE_CHR_IF_0D_INTERP_CMD:
        sta     V1541_CMD_BUF,Y
        inc     V1541_CMD_LEN
        cmp     #$0D ;CR?
        beq     L9772_V1541_INTERPRET_CMD
        sec
        rts

L9772_V1541_INTERPRET_CMD:
        lda     V1541_CMD_BUF
L9775:  ldx     #(4*2)-1 ;4 cmds in table, two chars each
L9777:  cmp     L978E_V1541_CMDS,x
        beq     L9783_FOUND_CMD_IN_TABLE
        dex
        bpl     L9777
        lda     #doserr_31_invalid_cmd
        clc
        rts

L9783_FOUND_CMD_IN_TABLE:
        txa
        and     #$FE
        pha
        jsr     L979E
        plx
        jmp     (L9796_V1541_CMD_HANDLERS,x)

L978E_V1541_CMDS:
        .byte "Ii", "Rr", "Ss", "Vv"
L9796_V1541_CMD_HANDLERS:
        .addr L8C6F_V1541_I_INITIALIZE
        .addr L980E_V1541_R_RENAME
L979A:  .addr L97D6_V1541_S_SCRATCH
        .addr L9842_V1541_V_VALIDATE

L979E:  ldy     V1541_CMD_LEN
        dey
        lda     #$96 ;TODO probably an address, see L8EB6
        ldx     #$02
        jmp     L8EB6

;Called twice from rename, not used anywhere else
L97A9_USED_BY_RENAME:
        jsr     L979E
L97AC:  lda     (V1541_FNADR)
        inc     V1541_FNADR
        bne     L97B4
        inc     V1541_FNADR+1
L97B4:  dec     V1541_FNLEN
        beq     L97D2_33_SYNTAX_ERROR
        cmp     #'='
        bne     L97AC
        jsr     L8EBD_SETUP_FOR_FILE_ACCESS_AND_DO_DIR_SEARCH_STUFF
        bcc     L97D4_CLC_RTS
        and     #$40
        ora     V1541_FILE_TYPE
        ora     V1541_FILE_MODE
        ora     BAD
        bne     L97D2_33_SYNTAX_ERROR
        jmp     L8D9F_SELECT_DIR_CHANNEL_AND_CLEAR_IT_THEN_UNKNOWN_THEN_FILENAME_COMPARE
; ----------------------------------------------------------------------------

L97D2_33_SYNTAX_ERROR:
        lda     #doserr_33_syntax_err ;Invalid filename
L97D4_CLC_RTS:
        clc
        rts
; ----------------------------------------------------------------------------
L97D6_V1541_S_SCRATCH:
        bcs     L97DC
L97D8_SCRATCH_NO_FILENAME:
        lda     #doserr_34_syntax_err ;34 No file given
        clc
        rts

L97DC:  bit     #$80
        beq     L97D8_SCRATCH_NO_FILENAME
        and     #$20
        ora     V1541_FILE_TYPE
        ora     V1541_FILE_MODE
        bne     L97D8_SCRATCH_NO_FILENAME
        lda     #$00
        pha
L97ED:
        jsr     L8D9F_SELECT_DIR_CHANNEL_AND_CLEAR_IT_THEN_UNKNOWN_THEN_FILENAME_COMPARE
        bcc     L9805_ERROR
L97F2:  tsx
        inc     stack+1,x
        jsr     L8D17  ;maybe returns cbm dos error in a
        lda     V1541_DATA_BUF
        and     #$80
        bne     L97ED
        jsr     L89E2
L9803:  bra     L97ED

L9805_ERROR:
        pla
        ldx     #$01
        ldy     #$00
        sec
        jmp     L9964_STORE_XAY_CLEAR_0217

; ----------------------------------------------------------------------------
L980E_V1541_R_RENAME:
        bcc     L9840_RENAME_ERROR
        bit     #$80
        beq     L983E_RENAME_INVALID_FILENAME
        and     #$40
        ora     BAD
        ora     V1541_FILE_MODE
        ORA     V1541_FILE_TYPE
        bne     L983E_RENAME_INVALID_FILENAME

        jsr     L8D9F_SELECT_DIR_CHANNEL_AND_CLEAR_IT_THEN_UNKNOWN_THEN_FILENAME_COMPARE
        lda     #doserr_63_file_exists
        bcs     L9840_RENAME_ERROR ;branch if no error (file exists, which is an error here)

        jsr     L97A9_USED_BY_RENAME
        bcc     L9840_RENAME_ERROR

        jsr     L979E
        jsr     L8FF3

L9833:  jsr     L8D5B_UNKNOWN_DIR_RELATED
        bcc     L9840_RENAME_ERROR

        jsr     L97A9_USED_BY_RENAME
        jmp     L8D17 ;maybe returns cbm dos error in a

L983E_RENAME_INVALID_FILENAME:
        lda     #doserr_33_syntax_err ;33 Invalid filename
L9840_RENAME_ERROR:
        clc
        rts

; ----------------------------------------------------------------------------
L9842_V1541_V_VALIDATE:
        jsr     L8C6F_V1541_I_INITIALIZE
        jsr     KL_RAMTAS
        cpx     $0209
        beq     L985B

L984D:  stx     $0209
        sta     $0208
        stx     $020B
        sta     $020A
        sec
        rts

L985B:  cmp     $0208
        bne     L984D
        cpx     $020A
        bcc     L984D
        bne     L986C
        cmp     $020A
        bcc     L984D
L986C:  jsr     L8CBB_CLEAR_ACTIVE_CHANNEL_EXCEPT_FLAGS_THEN_BRA_L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6
        bne     L9890
L9871:  jsr     L8CC3
        bcc     L988B ;branch if error
        jsr     L8CD1
        jsr     L8AD5_MAYBE_READS_BLOCK_HEADER
        ldy     #$02
        lda     V1541_ACTIV_EA
        sta     ($E4),y
L9882:  inc     V1541_ACTIV_E9
        beq     L9890
        jsr     L89F9
        bra     L9882
L988B:  bit     SXREG
        bpl     L9871
L9890:  jsr     L8CBB_CLEAR_ACTIVE_CHANNEL_EXCEPT_FLAGS_THEN_BRA_L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6
        bcc     L98D0
        bra     L98AB
L9897:  jsr     L8CC3
        BCC     L98D0 ;branch if error
        jsr     L8C2A_JSR_V1541_SELECT_CHAN_17_JMP_L8C8B_CLEAR_ACTIVE_CHANNEL
        LDA     V1541_DATA_BUF
        bit     #$80
        bne     L98B4
        lda     V1541_DATA_BUF+1
        sta     V1541_ACTIV_E8
L98AB:  jsr     L8AD5_MAYBE_READS_BLOCK_HEADER
        bcs     L98B9 ;branch if no error
        ;error
        lda     V1541_ACTIV_E9
        beq     L9897
L98B4:  jsr     L8D17 ;maybe returns cbm dos error in a
        bra     L9890

L98B9:  inc     V1541_ACTIV_E9
        ldy     #$02
        lda     ($E4),y
        beq     L98AB
        lda     V1541_ACTIV_E9
        pha
        jsr     L8DE0_SOMEHOW_GETS_FILE_BLOCKS_USED_1
        sta     V1541_ACTIV_E9 ;store number of blocks used
        pla
        cmp     V1541_ACTIV_E9
        BNE     L98B4
        BRA     L9897
L98D0:  ldx     #$3F
L98D2:  stz     V1541_CMD_BUF,x
        dex
        bpl     L98D2
        inc     V1541_CMD_BUF
        jsr     L8CBB_CLEAR_ACTIVE_CHANNEL_EXCEPT_FLAGS_THEN_BRA_L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6
L98DE:  bcc     L9917
        bra     L98E5
L98E2:  jsr     L8D17 ;maybe returns cbm dos error in a
L98E5:  jsr     L8CBB_CLEAR_ACTIVE_CHANNEL_EXCEPT_FLAGS_THEN_BRA_L8CCE_JSR_L8CE6_THEN_UPDATE_ACTIVE_CHANNEL_AND_03A7_03A6
L98E8:  bra     L98ED
L98EA:  jsr     L8CC3
L98ED:  bcc     L9917 ;branch if error
        jsr     L9932
        and     V1541_CMD_BUF,y
        bne     L98E2
        lda     PowersOfTwo,x
        ora     V1541_CMD_BUF,y
        sta     V1541_CMD_BUF,y
        lda     #$30
        trb     V1541_DATA_BUF
        bne     L990F
        lda     V1541_DATA_BUF+1
        jsr     L8E20_MAYBE_CHECKS_HEADER ;maybe returns cbm dos error code in A
        bcs     L98EA ;branch if no error
        ;error occurred
L990F:  jsr     L8D17 ;maybe returns cbm dos error in a
        jsr     L8D5B_UNKNOWN_DIR_RELATED
        bra     L98D0
L9917:  jsr     L8A81
L991A:  beq     L9930
L991C:  lda     ($E4)
        jsr     L9932
        and     V1541_CMD_BUF,y
        bne     L992B
        jsr     L89FF
        bra     L9917
L992B:  jsr     L8A61
        bcs     L991C
L9930:  sec
        rts
; ----------------------------------------------------------------------------
L9932:  pha
        lsr     a
        lsr     a
        lsr     a
        tay
        pla
        and     #$07
        tax
        lda     PowersOfTwo,x
L993E:  rts
; ----------------------------------------------------------------------------
V1541_KERNAL_CALL_DONE:
        tax ;Save error code in X
        lda     #$00
        bcs     L9955 ;branch if no error

        ;error occurred
        lda     V1541_ACTIV_E8
        ldy     V1541_ACTIV_E9
        jsr     L9964_STORE_XAY_CLEAR_0217
        lda     #$04
        cpx     #doserr_25_write_err ;25 write-verify error
        bne     L9953
        ora     #$08
L9953:  ldx     #$0D
L9955:  bit     SXREG
        bpl     L995C
        ora     #$40 ;EOF
L995C:  sta     SATUS
        stz     SXREG
        txa
L9962:  clc
        rts
; ----------------------------------------------------------------------------
L9964_STORE_XAY_CLEAR_0217:
        stx     $0210
        sta     $0211
        sty     $0212

        stz     $0217
        rts
; ----------------------------------------------------------------------------
V1541_ERROR_WORDS:
        .byte   "CHANNEL",0
        .byte   "COMMAND",0
        .byte   "DIRECTORY",0
        .byte   "DISK",0
        .byte   "DOS",0
        .byte   "ERROR",0
        .byte   "EXISTS",0
        .byte   "FILE",0
        .byte   "FILES",0
        .byte   "FOUND",0
        .byte   "FULL",0
        .byte   "ILLEGAL",0
        .byte   "INVALID",0
        .byte   "LARGE",0
        .byte   "LINE",0
        .byte   "LONG",0
        .byte   "MISMATCH",0
        .byte   "NO",0
        .byte   "NOT",0
        .byte   "OK",0
        .byte   "OPEN",0
        .byte   "PROTECT",0
        .byte   "READ",0
        .byte   "SCRATCHED",0
        .byte   "SYNTAX",0
        .byte   "SYSTEM",0
        .byte   "T&S",0
        .byte   "TOO",0
        .byte   "TYPE",0
        .byte   "VERIFY",0
L9A2A := *+2
L9A2B := *+3
        .byte   "WRITE",0
        .byte   0
; ----------------------------------------------------------------------------
        .byte   $77,$00,$00,$01,$36,$8C,$00,$14 ; 9A2F 77 00 00 01 36 8C 00 14  w...6...
        .byte   $47,$A4,$00,$19,$B8,$B1,$24,$1A ; 9A37 47 A4 00 19 B8 B1 24 1A  G.....$.
        .byte   $B8,$7F,$24,$1B,$87,$24,$00,$1F ; 9A3F B8 7F 24 1B 87 24 00 1F  ..$..$..
        .byte   $4F,$09,$00,$20,$62,$5D,$00,$21 ; 9A47 4F 09 00 20 62 5D 00 21  O.. b].!
        .byte   $96,$24,$00,$21,$96,$24,$00,$22 ; 9A4F 96 24 00 21 96 24 00 22  .$.!.$."
        .byte   $96,$24,$00,$27,$96,$24,$00,$34 ; 9A57 96 24 00 27 96 24 00 34  .$.'.$.4
        .byte   $31,$A8,$57,$3C,$B8,$31,$7A,$3D ; 9A5F 31 A8 57 3C B8 31 7A 3D  1.W<.1z=
        .byte   $31,$73,$7A                     ; 9A67 31 73 7A                 1sz
L9A6A:  .byte   $3E,$31,$73,$3C,$3F,$31,$2A,$00 ; 9A6A 3E 31 73 3C 3F 31 2A 00  >1s<?1*.
        .byte   $40,$31,$AC,$67,$43,$47,$9D,$A4 ; 9A72 40 31 AC 67 43 47 9D A4  @1.gCG..
        .byte   $46,$70,$01,$00,$47,$11,$24,$00 ; 9A7A 46 70 01 00 47 11 24 00  Fp..G.$.
        .byte   $47,$11,$24,$00,$48
L9A87:  .byte   $1B,$42,$00,$49,$20,$67,$24
L9A8E:  .byte   $00,$01,$02,$80,$81,$82,$83,$84 ; 9A8E 00 01 02 80 81 82 83 84  ........
        .byte   $85,$86,$87,$88                 ; 9A96 85 86 87 88              ....
L9A9A:  .byte   $41,$81,$10,$22,$42,$82,$10,$23 ; 9A9A 41 81 10 22 42 82 10 23  A.."B..#
        .byte   $43,$83,$00                     ; 9AA2 43 83 00                 C..
; ----------------------------------------------------------------------------
L9AA5_V1541_CHRIN_CMD_CHAN:
        lda     $0217
        inc     $0217
        ldy     #$0B
L9AAD_SEARCH_L9A8E_LOOP:
        cmp     L9A8E,y
        beq     L9AB8_FOUND_IN_L9A8E
        dey
        bpl     L9AAD_SEARCH_L9A8E_LOOP
        jmp     L9AE8_NOT_FOUND_IN_L9A8E
; ----------------------------------------------------------------------------
L9AB8_FOUND_IN_L9A8E:
        lda     L9A9A,y
        bne     L9ACD
        tax
        tay
        JSR     L9964_STORE_XAY_CLEAR_0217
        SEC
        ROR     $039d
        LDA     #$0d ;cr
        .byte $2c
L9AC9:  lda     #$2C ;,
        sec
        rts
; ----------------------------------------------------------------------------
L9ACD:  bit     #$10
        bne     L9AC9
        sta     $E0
        and     #$03
L9AD5:  tax
L9AD6:  lda     $020F,x
        jsr     BIN_TO_BCD_NIBS
        bit     $E0
        bmi     L9AE4
        txa
        bvs     L9AE4
        tya
L9AE4:  ora #$30
        sec
        rts
; ----------------------------------------------------------------------------
L9AE8_NOT_FOUND_IN_L9A8E:
        dec     a
        sta     $E0
        ldx     #$00
        lda     $0210
L9AF0_LOOP:
        inx
        inx
        inx
        inx
        beq     L9B12
        cmp     L9A2A,x
        bne     L9AF0_LOOP
L9AFB_OUTER_LOOP:
        lda     #$20
        ldy     L9A2B,x
        beq     L9B12
L9B02_INNER_LOOP:
        dec     $E0
        beq     L9B19
        iny
        lda     V1541_ERROR_WORDS-2,y
        bne     L9B02_INNER_LOOP
        inx
        txa
        and     #$03
        bne     L9AFB_OUTER_LOOP
L9B12:  lda     #$80
        sta     $0217
        lda     #$2C
L9B19:  sec
        rts
; ----------------------------------------------------------------------------
L9B1B_JMP_L9B1E_X:
        jmp     (L9B1E,x)
L9B1E:  .addr L9BF6_X00
        .addr L9BDA_X02
        .addr LA473_X04
        .addr L9C6B_X06
        .addr L9BE0_X08
        .addr LA2D1_X0A
        .addr L9CA4_X0C
        .addr L9CA7_X0E
        .addr L9CBB_X10
        .addr L9CBE_X12
        .addr L9F60_X14
        .addr L9F63_X16
        .addr LA0F6_X18
        .addr LA0F9_X1A
        .addr L9EF0_X1C
        .addr LA369_X1E
        .addr LA661_X20
        .addr LA6A7_X22
        .addr LA65A_X24
        .addr LA66B_X26
        .addr LA72B_X28
        .addr LA848_X2A
        .addr LA84F_X2C
        .addr LA898_X2E
        .addr LA92D_X30
        .addr LA28A_X32
        .addr LA2D9_X34
        .addr LA29A_X36
        .addr LA2DC_X38_UNKNOWN_INDIRECT_STUFF_LOAD
        .addr LA7DB_X3A
        .addr LA02B_X3C
        .addr L9FF1_X3E_INDIRECT_STUFF
        .addr LA1DB_X40
        .addr LA1DD_X42_INDIRECT_STUFF_LOAD
        .addr LA21A_X44
        .addr LA26B_X46
        .addr LA27B_X48
        .addr LA2AB_X4A
        .addr LA2CD_X4C
        .addr LA2D5_X4E
        .addr LA338_X50
        .addr LA421_X52
        .addr LA396_X54
        .addr LA226_X56
        .addr LA475_X58
        .addr LA2A8_X5A
        .addr LA7D8_X5C
        .addr L9F4E_X5E
        .addr L9F42_X60
        .addr L9F33_X62
        .addr L9F5A_X64
        .addr LA65F_X66
        .addr LA06D_X68
        .addr LA1B7_X6A
        .addr L9BCE_X6C
        .addr L9B9F_X6E
        .addr L9BA2_X70
        .addr L9B98_X72
        .addr L9B9B_X74
        .addr LA9C6_X76
        .addr LA9C9_X78

L9B98_X72:  jsr     LA02B_X3C                           ; 9B98 20 2B A0                  +.
L9B9B_X74:  ldy     #$FF                            ; 9B9B A0 FF                    ..
        bra     L9BA4                           ; 9B9D 80 05                    ..
L9B9F_X6E:  jsr     LA02B_X3C                           ; 9B9F 20 2B A0                  +.
L9BA2_X70:  ldy     #$00                            ; 9BA2 A0 00                    ..
L9BA4:  sty     $49                             ; 9BA4 84 49                    .I
        jsr     L9BF6_X00                           ; 9BA6 20 F6 9B                  ..
        lda     $2B                             ; 9BA9 A5 2B                    .+
        eor     $49                             ; 9BAB 45 49                    EI
        sta     $00                             ; 9BAD 85 00                    ..
        lda     $2C                             ; 9BAF A5 2C                    .,
        eor     $49                             ; 9BB1 45 49                    EI
        sta     $01                             ; 9BB3 85 01                    ..
        jsr     LA26B_X46                           ; 9BB5 20 6B A2                  k.
        jsr     L9BF6_X00                           ; 9BB8 20 F6 9B                  ..
        lda     $2C                             ; 9BBB A5 2C                    .,
        eor     $49                             ; 9BBD 45 49                    EI
        and     $01                             ; 9BBF 25 01                    %.
        eor     $49                             ; 9BC1 45 49                    EI
        tay                                     ; 9BC3 A8                       .
        lda     $2B                             ; 9BC4 A5 2B                    .+
        eor     $49                             ; 9BC6 45 49                    EI
        and     $00                             ; 9BC8 25 00                    %.
        eor     $49                             ; 9BCA 45 49                    EI
        bra     L9BDA_X02                           ; 9BCC 80 0C                    ..
L9BCE_X6C:  jsr     L9BF6_X00                           ; 9BCE 20 F6 9B                  ..
        lda     $2C                             ; 9BD1 A5 2C                    .,
        eor     #$FF                            ; 9BD3 49 FF                    I.
        tay                                     ; 9BD5 A8                       .
        lda     $2B                             ; 9BD6 A5 2B                    .+
        eor     #$FF                            ; 9BD8 49 FF                    I.
; ----------------------------------------------------------------------------
L9BDA_X02:  jsr     L9C60                           ; 9BDA 20 60 9C                  `.
L9BDD:  jmp     LA2B3                           ; 9BDD 4C B3 A2                 L..
; ----------------------------------------------------------------------------
L9BE0_X08:  lda     $2D                             ; 9BE0 A5 2D                    .-
        bmi     L9C05                           ; 9BE2 30 21                    0!
        lda     $25                             ; 9BE4 A5 25                    .%
        cmp     #$91                            ; 9BE6 C9 91                    ..
        bcs     L9C05                           ; 9BE8 B0 1B                    ..
        jsr     LA338_X50                           ; 9BEA 20 38 A3                  8.
        lda    $2b
        ldy    $2c
        sty    $06
        sta    $07
L9BF4:  rts
; ----------------------------------------------------------------------------
L9BF6_X00:  lda     $25                             ; 9BF6 A5 25                    .%
        cmp     #$90                            ; 9BF8 C9 90                    ..
        bcc     L9C0A                           ; 9BFA 90 0E                    ..
        lda     #<L9C58                         ; 9BFC A9 58                    .X
        ldy     #>L9C58                         ; 9BFE A0 9C                    ..
        jsr     LA2DC_X38_UNKNOWN_INDIRECT_STUFF_LOAD ; 9C00 20 DC A2                  ..
        beq     L9C0A                           ; 9C03 F0 05                    ..
L9C05:  ldx     #$0E                            ; 9C05 A2 0E                    ..
        jmp     LFB4B                           ; 9C07 4C 4B FB                 LK.
; ----------------------------------------------------------------------------
L9C0A:  jmp     LA338_X50
L9C0D:  inc     $3F                             ; 9C0D E6 3F                    .?
        bne     L9C13                           ; 9C0F D0 02                    ..
        inc     $40                             ; 9C11 E6 40                    .@
L9C13:  sei                                     ; 9C13 78                       x
        ldy     #$00                            ; 9C14 A0 00                    ..
        lda     #$3F ;ZP-address                ; 9C16 A9 3F                    .?
        sta     SINNER                          ; 9C18 8D 4E 03                 .N.
        jsr     GO_RAM_LOAD_GO_KERN             ; 9C1B 20 4A 03                  J.
        cli                                     ; 9C1E 58                       X
        cmp     #$3A                            ; 9C1F C9 3A                    .:
        bcs     L9C2D                           ; 9C21 B0 0A                    ..
        cmp     #$20                            ; 9C23 C9 20                    .
        beq     L9C0D                           ; 9C25 F0 E6                    ..
        sec                                     ; 9C27 38                       8
        sbc     #$30                            ; 9C28 E9 30                    .0
        sec                                     ; 9C2A 38                       8
        sbc     #$D0                            ; 9C2B E9 D0                    ..
L9C2D:  rts                                     ; 9C2D 60                       `
; ----------------------------------------------------------------------------
L9C2E:  lda     #$3F ;ZP-address                ; 9C2E A9 3F                    .?
        sta     SINNER                          ; 9C30 8D 4E 03                 .N.
        jmp     GO_RAM_LOAD_GO_KERN             ; 9C33 4C 4A 03                 LJ.
; ----------------------------------------------------------------------------
L9C36:  lda     #$08 ;ZP-address                ; 9C36 A9 08                    ..
        sta     SINNER                          ; 9C38 8D 4E 03                 .N.
        jmp     GO_RAM_LOAD_GO_KERN
L9C3E:  lda     #$08                            ; 9C3E A9 08                    ..
        sta     $0357                           ; 9C40 8D 57 03                 .W.
        jmp     GO_APPL_LOAD_GO_KERN            ; 9C43 4C 53 03                 LS.
; ----------------------------------------------------------------------------
L9C46:  lda     #$0A ;ZP-address                ; 9C46 A9 0A                    ..
        sta     SINNER                          ; 9C48 8D 4E 03                 .N.
        jmp     GO_RAM_LOAD_GO_KERN             ; 9C4B 4C 4A 03                 LJ.
; ----------------------------------------------------------------------------
L9C4E:  pha                                     ; 9C4E 48                       H
        lda     #$0A                            ; 9C4F A9 0A                    ..
        sta     $0360                           ; 9C51 8D 60 03                 .`.
        pla                                     ; 9C54 68                       h
        jmp     GO_RAM_STORE_GO_KERN            ; 9C55 4C 5C 03                 L\.
; ----------------------------------------------------------------------------
L9C58:  bcc     L9BDA_X02                           ; 9C58 90 80                    ..
        brk                                     ; 9C5A 00                       .
        brk                                     ; 9C5B 00                       .
        brk                                     ; 9C5C 00                       .
        brk                                     ; 9C5D 00                       .
        brk                                     ; 9C5E 00                       .
        brk                                     ; 9C5F 00                       .
L9C60:  ldx     #$00                            ; 9C60 A2 00                    ..
        stx     $02
        sta     $26                             ; 9C64 85 26                    .&
        sty     $27                             ; 9C66 84 27                    .'
        ldx     #$90                            ; 9C68 A2 90                    ..
        rts                                     ; 9C6A 60                       `
; ----------------------------------------------------------------------------
L9C6B_X06:  ldx     $3F                             ; 9C6B A6 3F                    .?
        ldy     $40                             ; 9C6D A4 40                    .@
        stx     $3B                             ; 9C6F 86 3B                    .;
        sty     $3C                             ; 9C71 84 3C                    .<
        ldx     $08                             ; 9C73 A6 08                    ..
        stx     $3F                             ; 9C75 86 3F                    .?
        clc                                     ; 9C77 18                       .
        adc     $08                             ; 9C78 65 08                    e.
        sta     $0A                             ; 9C7A 85 0A                    ..
        ldx     $09                             ; 9C7C A6 09                    ..
        stx     $40                             ; 9C7E 86 40                    .@
        bcc     L9C83                           ; 9C80 90 01                    ..
        inx                                     ; 9C82 E8                       .
L9C83:  stx     $0B                             ; 9C83 86 0B                    ..
        ldy     #$00                            ; 9C85 A0 00                    ..
        jsr     L9C46                           ; 9C87 20 46 9C                  F.
        pha                                     ; 9C8A 48                       H
        tya                                     ; 9C8B 98                       .
        jsr     L9C4E                           ; 9C8C 20 4E 9C                  N.
        jsr     L9C13                           ; 9C8F 20 13 9C                  ..
        jsr     LA396_X54                           ; 9C92 20 96 A3                  ..
        pla                                     ; 9C95 68                       h
        ldy     #$00                            ; 9C96 A0 00                    ..
        jsr     L9C4E                           ; 9C98 20 4E 9C                  N.
        ldx     $3B                             ; 9C9B A6 3B                    .;
        ldy     $3C                             ; 9C9D A4 3C                    .<
        stx     $3F                             ; 9C9F 86 3F                    .?
        sty     $40                             ; 9CA1 84 40                    .@
L9CA3:  rts                                     ; 9CA3 60                       `
; ----------------------------------------------------------------------------
L9CA4_X0C:  jsr     LA02B_X3C                           ; 9CA4 20 2B A0                  +.
L9CA7_X0E:  lda     $2D                             ; 9CA7 A5 2D                    .-
        eor     #$FF                            ; 9CA9 49 FF                    I.
        sta     $2D                             ; 9CAB 85 2D                    .-
        eor     $38                             ; 9CAD 45 38                    E8
        sta     $39                             ; 9CAF 85 39                    .9
        lda     $25                             ; 9CB1 A5 25                    .%
        jmp     L9CBE_X12                           ; 9CB3 4C BE 9C                 L..
; ----------------------------------------------------------------------------
L9CB6:  jsr     L9E56                           ; 9CB6 20 56 9E                  V.
        bcc     L9CF7                           ; 9CB9 90 3C                    .<
L9CBB_X10:  jsr     LA02B_X3C
L9CBE_X12:  bne     L9CC3                           ; 9CBE D0 03                    ..
        jmp     LA26B_X46                           ; 9CC0 4C 6B A2                 Lk.
; ----------------------------------------------------------------------------
L9CC3:  ldx     $3A                             ; 9CC3 A6 3A                    .:
        stx     $14                             ; 9CC5 86 14                    ..
        ldx     #$30                            ; 9CC7 A2 30                    .0
        lda     $30                             ; 9CC9 A5 30                    .0
L9CCB:  tay                                     ; 9CCB A8                       .
        beq     L9CA3                           ; 9CCC F0 D5                    ..
        sec                                     ; 9CCE 38                       8
        sbc     $25                             ; 9CCF E5 25                    .%
        beq     L9CF7                           ; 9CD1 F0 24                    .$
        bcc     L9CE7                           ; 9CD3 90 12                    ..
        sty     $25
        ldy     $38
        sty     $2D                             ; 9CD9 84 2D                    .-
L9CDB:  eor     #$FF                            ; 9CDB 49 FF                    I.
        adc     #$00                            ; 9CDD 69 00                    i.
        ldy     #$00                            ; 9CDF A0 00                    ..
        sty     $14                             ; 9CE1 84 14                    ..
        ldx     #$25                            ; 9CE3 A2 25                    .%
        bne     L9CEB                           ; 9CE5 D0 04                    ..
L9CE7:  ldy     #$00                            ; 9CE7 A0 00                    ..
        sty     $3A                             ; 9CE9 84 3A                    .:
L9CEB:  cmp     #$F9                            ; 9CEB C9 F9                    ..
        bmi     L9CB6                           ; 9CED 30 C7                    0.
        tay                                     ; 9CEF A8                       .
        lda     $3A                             ; 9CF0 A5 3A                    .:
        lsr     $01,x                           ; 9CF2 56 01                    V.
        jsr     L9E6D                           ; 9CF4 20 6D 9E                  m.
L9CF7:  bit     $39                             ; 9CF7 24 39                    $9
        bpl     L9D73                           ; 9CF9 10 78                    .x
        ldy     #$25                            ; 9CFB A0 25                    .%
        cpx     #$30                            ; 9CFD E0 30                    .0
        beq     L9D03                           ; 9CFF F0 02                    ..
        ldy     #$30                            ; 9D01 A0 30                    .0
L9D03:  sec                                     ; 9D03 38                       8
        eor     #$FF                            ; 9D04 49 FF                    I.
        adc     $14                             ; 9D06 65 14                    e.
        sta     $3A                             ; 9D08 85 3A                    .:
        lda     $07,y                           ; 9D0A B9 07 00                 ...
        sbc     $07,x                           ; 9D0D F5 07                    ..
        sta     $2C                             ; 9D0F 85 2C                    .,
        lda     $06,y                           ; 9D11 B9 06 00                 ...
        sbc     $06,x                           ; 9D14 F5 06                    ..
        sta     $2b
        lda     $05,y                           ; 9D18 B9 05 00                 ...
        sbc     $05,x                           ; 9D1B F5 05                    ..
        sta     $2A                             ; 9D1D 85 2A                    .*
        lda     $04,y                           ; 9D1F B9 04 00                 ...
        sbc     $04,x                           ; 9D22 F5 04                    ..
        sta     $29                             ; 9D24 85 29                    .)
        lda     $03,y                           ; 9D26 B9 03 00                 ...
        sbc     $03,x                           ; 9D29 F5 03                    ..
        sta     $28                             ; 9D2B 85 28                    .(
        lda     $02,y                           ; 9D2D B9 02 00                 ...
        sbc     $02,x                           ; 9D30 F5 02                    ..
        sta     $27                             ; 9D32 85 27                    .'
        lda     $01,y                           ; 9D34 B9 01 00                 ...
        sbc     $01,x                           ; 9D37 F5 01                    ..
        sta     $26                             ; 9D39 85 26                    .&
L9D3B:  bcs     L9D40                           ; 9D3B B0 03                    ..
        jsr     L9DDA                           ; 9D3D 20 DA 9D                  ..
L9D40:  ldy     #$00                            ; 9D40 A0 00                    ..
        tya                                     ; 9D42 98                       .
        clc                                     ; 9D43 18                       .
L9D44:  ldx     $26                             ; 9D44 A6 26                    .&
        bne     L9DB6                           ; 9D46 D0 6E                    .n
        ldx     $27                             ; 9D48 A6 27                    .'
        stx     $26                             ; 9D4A 86 26                    .&
        ldx     $28
        stx     $27                             ; 9D4E 86 27                    .'
        ldx     $29                             ; 9D50 A6 29                    .)
        stx     $28                             ; 9D52 86 28                    .(
        ldx     $2A                             ; 9D54 A6 2A                    .*
        stx     $29                             ; 9D56 86 29                    .)
        ldx     $2B                             ; 9D58 A6 2B                    .+
        stx     $2A                             ; 9D5A 86 2A                    .*
        ldx     $2C                             ; 9D5C A6 2C                    .,
L9D5E:  stx     $2B                             ; 9D5E 86 2B                    .+
L9D60:  ldx     $3A                             ; 9D60 A6 3A                    .:
        stx     $2C                             ; 9D62 86 2C                    .,
        sty     $3A                             ; 9D64 84 3A                    .:
        adc     #$08                            ; 9D66 69 08                    i.
        cmp     #$38                            ; 9D68 C9 38                    .8
        bne     L9D44                           ; 9D6A D0 D8                    ..
L9D6C:  lda     #$00                            ; 9D6C A9 00                    ..
L9D6E:  sta     $25
L9D70:  sta     $2D                             ; 9D70 85 2D                    .-
        rts                                     ; 9D72 60                       `
; ----------------------------------------------------------------------------
L9D73:  adc     $14                             ; 9D73 65 14                    e.
        sta     $3A                             ; 9D75 85 3A                    .:
        lda     $2C                             ; 9D77 A5 2C                    .,
        adc     $37                             ; 9D79 65 37                    e7
        sta     $2C                             ; 9D7B 85 2C                    .,
        lda     $2B                             ; 9D7D A5 2B                    .+
        adc     $36                             ; 9D7F 65 36                    e6
        sta     $2b
        lda     $2A                             ; 9D83 A5 2A                    .*
        adc     $35                             ; 9D85 65 35                    e5
        sta     $2A                             ; 9D87 85 2A                    .*
        lda     $29                             ; 9D89 A5 29                    .)
        adc     $34
        sta     $29
        lda     $28                             ; 9D8F A5 28                    .(
        adc     $33                             ; 9D91 65 33                    e3
        sta     $28                             ; 9D93 85 28                    .(
        lda     $27                             ; 9D95 A5 27                    .'
        adc     $32                             ; 9D97 65 32                    e2
        sta     $27                             ; 9D99 85 27                    .'
        lda     $26                             ; 9D9B A5 26                    .&
        adc     $31                             ; 9D9D 65 31                    e1
        sta     $26                             ; 9D9F 85 26                    .&
        jmp     L9DC3                           ; 9DA1 4C C3 9D                 L..
; ----------------------------------------------------------------------------
L9DA4:  adc     #$01                            ; 9DA4 69 01                    i.
        asl     $3A                             ; 9DA6 06 3A                    .:
        rol     $2C                             ; 9DA8 26 2C                    &,
        rol     $2B                             ; 9DAA 26 2B                    &+
        rol     $2A                             ; 9DAC 26 2A                    &*
        rol     $29                             ; 9DAE 26 29                    &)
        rol     $28                             ; 9DB0 26 28                    &(
        rol     $27                             ; 9DB2 26 27                    &'
        rol     $26                             ; 9DB4 26 26                    &&
L9DB6:  bpl     L9DA4                           ; 9DB6 10 EC                    ..
        sec                                     ; 9DB8 38                       8
        sbc     $25                             ; 9DB9 E5 25                    .%
        bcs     L9D6C                           ; 9DBB B0 AF                    ..
        eor     #$FF                            ; 9DBD 49 FF                    I.
        adc     #$01                            ; 9DBF 69 01                    i.
        sta     $25                             ; 9DC1 85 25                    .%
L9DC3:  bcc     L9DD9                           ; 9DC3 90 14                    ..
L9DC5:  inc     $25                             ; 9DC5 E6 25                    .%
        beq     L9E2F                           ; 9DC7 F0 66                    .f
        ror     $26                             ; 9DC9 66 26                    f&
        ror     $27                             ; 9DCB 66 27                    f'
        ror     $28                             ; 9DCD 66 28                    f(
        ror     $29                             ; 9DCF 66 29                    f)
        ror     $2A                             ; 9DD1 66 2A                    f*
        ror     $2B                             ; 9DD3 66 2B                    f+
        ror     $2C                             ; 9DD5 66 2C                    f,
        ror     $3A                             ; 9DD7 66 3A                    f:
L9DD9:  rts                                     ; 9DD9 60                       `
; ----------------------------------------------------------------------------
L9DDA:  lda     $2D                             ; 9DDA A5 2D                    .-
        eor     #$FF                            ; 9DDC 49 FF                    I.
        sta     $2D                             ; 9DDE 85 2D                    .-
L9DE0:  lda     $26                             ; 9DE0 A5 26                    .&
        eor     #$FF                            ; 9DE2 49 FF                    I.
        sta     $26                             ; 9DE4 85 26                    .&
        lda     $27                             ; 9DE6 A5 27                    .'
        eor     #$FF                            ; 9DE8 49 FF                    I.
        sta     $27                             ; 9DEA 85 27                    .'
        lda     $28                             ; 9DEC A5 28                    .(
        eor     #$FF                            ; 9DEE 49 FF                    I.
        sta     $28                             ; 9DF0 85 28                    .(
        lda     $29                             ; 9DF2 A5 29                    .)
        eor     #$FF                            ; 9DF4 49 FF                    I.
        sta     $29                             ; 9DF6 85 29                    .)
        lda     $2A                             ; 9DF8 A5 2A                    .*
        eor     #$FF                            ; 9DFA 49 FF                    I.
        sta     $2A                             ; 9DFC 85 2A                    .*
        lda     $2B                             ; 9DFE A5 2B                    .+
        eor     #$FF                            ; 9E00 49 FF                    I.
        sta     $2B                             ; 9E02 85 2B                    .+
        lda     $2C                             ; 9E04 A5 2C                    .,
        eor     #$FF                            ; 9E06 49 FF                    I.
        sta     $2C                             ; 9E08 85 2C                    .,
        lda     $3A                             ; 9E0A A5 3A                    .:
        eor     #$FF                            ; 9E0C 49 FF                    I.
        sta     $3A                             ; 9E0E 85 3A                    .:
        inc     $3a
        bne     L9E2E                           ; 9E12 D0 1A                    ..
L9E14:  inc     $2C                             ; 9E14 E6 2C                    .,
        bne     L9E2E                           ; 9E16 D0 16                    ..
        inc     $2B                             ; 9E18 E6 2B                    .+
        bne     L9E2E                           ; 9E1A D0 12                    ..
        inc     $2A                             ; 9E1C E6 2A                    .*
        bne     L9E2E                           ; 9E1E D0 0E                    ..
        inc     $29                             ; 9E20 E6 29                    .)
        bne     L9E2E                           ; 9E22 D0 0A                    ..
        inc     $28                             ; 9E24 E6 28                    .(
        bne     L9E2E                           ; 9E26 D0 06                    ..
        inc     $27                             ; 9E28 E6 27                    .'
        bne     L9E2E                           ; 9E2A D0 02                    ..
        inc     $26                             ; 9E2C E6 26                    .&
L9E2E:  rts                                     ; 9E2E 60                       `
; ----------------------------------------------------------------------------
L9E2F:  ldx     #$0F                            ; 9E2F A2 0F                    ..
        jmp     LFB4B                           ; 9E31 4C 4B FB                 LK.
; ----------------------------------------------------------------------------
L9E34:  ldx     #$0B                            ; 9E34 A2 0B                    ..
L9E36:  ldy     $07,x                           ; 9E36 B4 07                    ..
        sty     $3A                             ; 9E38 84 3A                    .:
        ldy     $06,x                           ; 9E3A B4 06                    ..
        sty     $07,x                           ; 9E3C 94 07                    ..
        ldy     $05,x                           ; 9E3E B4 05                    ..
        sty     $06,X
        ldy     $04,X
        sty     $05,X
        ldy     $03,X
        sty     $04,X
        ldy     $02,X
        sty     $03,x                           ; 9E4C 94 03                    ..
        ldy     $01,x                           ; 9E4E B4 01                    ..
        sty     $02,x                           ; 9E50 94 02                    ..
        ldy     $2F                             ; 9E52 A4 2F                    ./
        sty     $01,x                           ; 9E54 94 01                    ..
L9E56:  adc     #$08                            ; 9E56 69 08                    i.
        bmi     L9E36                           ; 9E58 30 DC                    0.
        beq     L9E36                           ; 9E5A F0 DA                    ..
        sbc     #$08                            ; 9E5C E9 08                    ..
        tay                                     ; 9E5E A8                       .
        lda     $3A                             ; 9E5F A5 3A                    .:
        bcs     L9E7D
L9E63:  asl     $01,x
        bcc     L9E69                           ; 9E65 90 02                    ..
        inc     $01,x                           ; 9E67 F6 01                    ..
L9E69:  ror     $01,x                           ; 9E69 76 01                    v.
        ror     $01,x                           ; 9E6B 76 01                    v.
L9E6D:  ror     $02,x                           ; 9E6D 76 02                    v.
        ror     $03,x                           ; 9E6F 76 03                    v.
        ror     $04,x                           ; 9E71 76 04                    v.
        ror     $05,x                           ; 9E73 76 05                    v.
        ror     $06,x                           ; 9E75 76 06                    v.
        ror     $07,x                           ; 9E77 76 07                    v.
        ror     a                               ; 9E79 6A                       j
        iny                                     ; 9E7A C8                       .
        bne     L9E63                           ; 9E7B D0 E6                    ..
L9E7D:  clc                                     ; 9E7D 18                       .
        rts                                     ; 9E7E 60                       `
; ----------------------------------------------------------------------------
L9E7F:  sta     ($00,x)                         ; 9E7F 81 00                    ..
        brk                                     ; 9E81 00                       .
        brk                                     ; 9E82 00                       .
        brk                                     ; 9E83 00                       .
        brk                                     ; 9E84 00                       .
        brk                                     ; 9E85 00                       .
        brk                                     ; 9E86 00                       .
L9E87:  php                                     ; 9E87 08                       .
        ror     LCD2D,x                         ; 9E88 7E 2D CD                 ~-.
        stz     $DB                             ; 9E8B 64 DB                    d.
        lda     ($F8,x)                         ; 9E8D A1 F8                    ..
        pla                                     ; 9E8F 68                       h
        ror     $F944,x                         ; 9E90 7E 44 F9                 ~D.
        cld                                     ; 9E93 D8                       .
        ldy     WIN_BTM_RGHT_Y,x                ; 9E94 B4 A6                    ..
        ;TODO probably data
        bbr7    $F4,$9F17                       ; 9E96 7F F4 7E                 ..~
        .byte   $63                             ; 9E99 63                       c
        rmb4    $AB                             ; 9E9A 47 AB                    G.
        lsr     $98                             ; 9E9C 46 98                    F.
        .byte   $BB                             ; 9E9E BB                       .
        tsb     $7F                             ; 9E9F 04 7F                    ..
L9EA1:  asl     $4D                             ; 9EA1 06 4D                    .M
        .byte   $42                             ; 9EA3 42                       B
        jmp     $11A0                           ; 9EA4 4C A0 11                 L..
; ----------------------------------------------------------------------------
        ror     $7F                             ; 9EA7 66 7F                    f.
        bit     $25                             ; 9EA9 24 25                    $%
        bit     #$EB                            ; 9EAB 89 EB                    ..
        cpx     #$15                            ; 9EAD E0 15                    ..
        lsr     $7F                             ; 9EAF 46 7F                    F.
        .byte   $53                             ; 9EB1 53                       S
        .byte   $0B                             ; 9EB2 0B                       .
        lda     ($53),y                         ; 9EB3 B1 53                    .S
        dec     $F6,x                           ; 9EB5 D6 F6                    ..
        cpy     $1380                           ; 9EB7 CC 80 13                 ...
        .byte   $BB                             ; 9EBA BB                       .
        .byte   $62                             ; 9EBB 62                       b
        smb0    $7C                             ; 9EBC 87 7C                    .|
        ;TODO probably data
        bbs5    $EE,$9E41                       ; 9EBE DF EE 80                 ...
        ror     $38,x                           ; 9EC1 76 38                    v8
        lsr     $D0E1                           ; 9EC3 4E E1 D0                 N..
        ;TODO probably data
        bbr1    $E8,$9E4B                       ; 9EC6 1F E8 82                 ...
        sec                                     ; 9EC9 38                       8
        tax                                     ; 9ECA AA                       .
        .byte   $3B                             ; 9ECB 3B                       ;
        and     #$5C                            ; 9ECC 29 5C                    )\
        rmb1    $EE                             ; 9ECE 17 EE                    ..
; ----------------------------------------------------------------------------
L9ED0:  bra     L9F07                           ; 9ED0 80 35                    .5
        tsb     $F3                             ; 9ED2 04 F3                    ..
        .byte   $33                             ; 9ED4 33                       3
        sbc     $68DE,y                         ; 9ED5 F9 DE 68                 ..h
; ----------------------------------------------------------------------------
L9ED8:  sta     ($35,x)                         ; 9ED8 81 35                    .5
        tsb     $F3                             ; 9EDA 04 F3                    ..
        .byte   $33                             ; 9EDC 33                       3
        sbc     $68DE,y                         ; 9EDD F9 DE 68                 ..h
; ----------------------------------------------------------------------------
L9EE0:  bra     $9E62 ;todo branches mid-instruction, probably data ; 9EE0 80 80                    ..
        brk                                     ; 9EE2 00                       .
        brk                                     ; 9EE3 00                       .
        brk                                     ; 9EE4 00                       .
        brk                                     ; 9EE5 00                       .
        brk                                     ; 9EE6 00                       .
        brk                                     ; 9EE7 00                       .
; ----------------------------------------------------------------------------
L9EE8:  bra     $9F1B ;todo branches mid-instruction, probably data ; 9EE8 80 31                    .1
        adc     ($17)                           ; 9EEA 72 17                    r.
        smb7    $D1                             ; 9EEC F7 D1                    ..
        .byte   $CF                             ; 9EEE CF                       .
        .byte   $7C                             ; 9EEF 7C                       |
; ----------------------------------------------------------------------------
L9EF0_X1C:  jsr     LA29A_X36                           ; 9EF0 20 9A A2                  ..
        beq     L9EF7                           ; 9EF3 F0 02                    ..
        bpl     L9EFA                           ; 9EF5 10 03                    ..
L9EF7:  jmp     L9C05                           ; 9EF7 4C 05 9C                 L..
; ----------------------------------------------------------------------------
L9EFA:  lda     $25
        sbc     #$7f
        pha
        lda     #$80
        sta     $25
        lda     #<L9ED0
        ldy     #>L9ED0
L9F07:  jsr     L9F3C_JSR_INDIRECT_STUFF_AND_JMP_L9CBE_X12
        lda     #<L9ED8
        ldy     #>L9ED8
        jsr     L9F54_JSR_INDIRECT_STUFF_AND_JMP_LA0F9_X1A
L9F11:  lda     #<L9E7F
        ldy     #>L9E7F
        jsr     L9F48_JSR_INDIRECT_STUFF_AND_JMP_L9CA7_X0E
L9F18:  lda     #<L9E87
        ldy     #>L9E87
L9F1C:  jsr     LA77E_UNKNOWN_OTHER_INDIRECT_STUFF
        lda     #<L9EE0
        ldy     #>L9EE0
        jsr     L9F3C_JSR_INDIRECT_STUFF_AND_JMP_L9CBE_X12
        pla
        jsr     LA421_X52
        lda     #<L9EE8
        ldy     #>L9EE8
L9F2E_PROBABLY_JSR_TO_INDIRECT_STUFF:
        jsr     L9FF1_X3E_INDIRECT_STUFF
L9F31:  bra     L9F63_X16                           ; 9F31 80 30                    .0
L9F33_X62:  jsr     LA06D_X68                           ; 9F33 20 6D A0                  m.
        bra     L9F63_X16                           ; 9F36 80 2B                    .+
L9F38:  lda     #<LA5BF                         ; 9F38 A9 BF                    ..
        ldy     #>LA5BF                         ; 9F3A A0 A5                    ..
L9F3C_JSR_INDIRECT_STUFF_AND_JMP_L9CBE_X12:
        jsr     L9FF1_X3E_INDIRECT_STUFF            ; 9F3C 20 F1 9F                  ..
        jmp     L9CBE_X12                           ; 9F3F 4C BE 9C                 L..
; ----------------------------------------------------------------------------
L9F42_X60:  jsr     LA06D_X68                           ; 9F42 20 6D A0                  m.
        jmp     L9CBE_X12                           ; 9F45 4C BE 9C                 L..
; ----------------------------------------------------------------------------
L9F48_JSR_INDIRECT_STUFF_AND_JMP_L9CA7_X0E:
        jsr     L9FF1_X3E_INDIRECT_STUFF            ; 9F48 20 F1 9F                  ..
        jmp     L9CA7_X0E                           ; 9F4B 4C A7 9C                 L..
; ----------------------------------------------------------------------------
L9F4E_X5E:  jsr     LA06D_X68                           ; 9F4E 20 6D A0                  m.
        jmp     L9CA7_X0E                           ; 9F51 4C A7 9C                 L..
; ----------------------------------------------------------------------------
L9F54_JSR_INDIRECT_STUFF_AND_JMP_LA0F9_X1A:
        jsr     L9FF1_X3E_INDIRECT_STUFF                           ; 9F54 20 F1 9F                  ..
        jmp     LA0F9_X1A                           ; 9F57 4C F9 A0                 L..
; ----------------------------------------------------------------------------
L9F5A_X64:  jsr     LA06D_X68                           ; 9F5A 20 6D A0                  m.
        jmp     LA0F9_X1A                           ; 9F5D 4C F9 A0                 L..
; ----------------------------------------------------------------------------
L9F60_X14:  jsr     LA02B_X3C                           ; 9F60 20 2B A0                  +.
L9F63_X16:  bne     L9F68                           ; 9F63 D0 03                    ..
        jmp     L9FF0                           ; 9F65 4C F0 9F                 L..
; ----------------------------------------------------------------------------
L9F68:  jsr     LA096                           ; 9F68 20 96 A0                  ..
        lda     #$00                            ; 9F6B A9 00                    ..
        sta     $0C                             ; 9F6D 85 0C                    ..
        sta     $0D                             ; 9F6F 85 0D                    ..
        sta     $0E                             ; 9F71 85 0E                    ..
        sta     $0F                             ; 9F73 85 0F                    ..
        sta     $10                             ; 9F75 85 10                    ..
        sta     $11                             ; 9F77 85 11                    ..
        sta     $12                             ; 9F79 85 12                    ..
        lda     $3A                             ; 9F7B A5 3A                    .:
        jsr     L9FA6                           ; 9F7D 20 A6 9F                  ..
        lda     $2C                             ; 9F80 A5 2C                    .,
        jsr     L9FA6                           ; 9F82 20 A6 9F                  ..
L9F85:  lda     $2B                             ; 9F85 A5 2B                    .+
        jsr     L9FA6                           ; 9F87 20 A6 9F                  ..
        lda     $2A                             ; 9F8A A5 2A                    .*
        jsr     L9FA6                           ; 9F8C 20 A6 9F                  ..
        lda     $29                             ; 9F8F A5 29                    .)
        jsr     L9FA6                           ; 9F91 20 A6 9F                  ..
        lda     $28                             ; 9F94 A5 28                    .(
        jsr     L9FA6                           ; 9F96 20 A6 9F                  ..
        lda     $27                             ; 9F99 A5 27                    .'
        jsr     L9FA6                           ; 9F9B 20 A6 9F                  ..
        lda     $26                             ; 9F9E A5 26                    .&
        jsr     L9FAB                           ; 9FA0 20 AB 9F                  ..
        jmp     LA198                           ; 9FA3 4C 98 A1                 L..
; ----------------------------------------------------------------------------
L9FA6:  bne     L9FAB                           ; 9FA6 D0 03                    ..
        jmp     L9E34                           ; 9FA8 4C 34 9E                 L4.
; ----------------------------------------------------------------------------
L9FAB:  lsr     a                               ; 9FAB 4A                       J
        ora     #$80                            ; 9FAC 09 80                    ..
L9FAE:  tay                                     ; 9FAE A8                       .
        bcc     L9FDC                           ; 9FAF 90 2B                    .+
        clc                                     ; 9FB1 18                       .
        lda     $12                             ; 9FB2 A5 12                    ..
        adc     $37                             ; 9FB4 65 37                    e7
        sta     $12                             ; 9FB6 85 12                    ..
        lda     $11                             ; 9FB8 A5 11                    ..
L9FBA:  adc     $36                             ; 9FBA 65 36                    e6
        sta     $11                             ; 9FBC 85 11                    ..
        lda     $10                             ; 9FBE A5 10                    ..
        adc     $35                             ; 9FC0 65 35                    e5
        sta     $10                             ; 9FC2 85 10                    ..
        lda     $0F                             ; 9FC4 A5 0F                    ..
        adc     $34                             ; 9FC6 65 34                    e4
        sta     $0F                             ; 9FC8 85 0F                    ..
        lda     $0E                             ; 9FCA A5 0E                    ..
        adc     $33                             ; 9FCC 65 33                    e3
        sta     $0E                             ; 9FCE 85 0E                    ..
        lda     $0D                             ; 9FD0 A5 0D                    ..
        adc     $32                             ; 9FD2 65 32                    e2
        sta     $0D                             ; 9FD4 85 0D                    ..
        lda     $0C                             ; 9FD6 A5 0C                    ..
        adc     $31                             ; 9FD8 65 31                    e1
        sta     $0C                             ; 9FDA 85 0C                    ..
L9FDC:  ror     $0C                             ; 9FDC 66 0C                    f.
        ror     $0D                             ; 9FDE 66 0D                    f.
        ror     $0E                             ; 9FE0 66 0E                    f.
        ror     $0F                             ; 9FE2 66 0F                    f.
        ror     $10                             ; 9FE4 66 10                    f.
        ror     $11                             ; 9FE6 66 11                    f.
        ror     $12                             ; 9FE8 66 12                    f.
        ror     $3A                             ; 9FEA 66 3A                    f:
        tya                                     ; 9FEC 98                       .
        lsr     a                               ; 9FED 4A                       J
        bne     L9FAE                           ; 9FEE D0 BE                    ..
L9FF0:  rts                                     ; 9FF0 60                       `
; ----------------------------------------------------------------------------
;Address in A (low byte) Y (high byte)
L9FF1_X3E_INDIRECT_STUFF:
        sta     $08                             ; 9FF1 85 08                    ..
        sty     $09                             ; 9FF3 84 09                    ..
        ldy     #$07                            ; 9FF5 A0 07                    ..
        lda     ($08),y                         ; 9FF7 B1 08                    ..
        sta     $37                             ; 9FF9 85 37                    .7
        dey                                     ; 9FFB 88                       .
        lda     ($08),y                         ; 9FFC B1 08                    ..
        sta     $36                             ; 9FFE 85 36                    .6
        dey                                     ; A000 88                       .
        lda     ($08),y                         ; A001 B1 08                    ..
        sta     $35                             ; A003 85 35                    .5
        dey                                     ; A005 88                       .
        lda     ($08),y                         ; A006 B1 08                    ..
        sta     $34                             ; A008 85 34                    .4
        dey                                     ; A00A 88                       .
        lda     ($08),y                         ; A00B B1 08                    ..
        sta     $33                             ; A00D 85 33                    .3
        dey                                     ; A00F 88                       .
        lda     ($08),y                         ; A010 B1 08                    ..
        sta     $32                             ; A012 85 32                    .2
        dey                                     ; A014 88                       .
        lda     ($08),y                         ; A015 B1 08                    ..
        sta     $38                             ; A017 85 38                    .8
        eor     $2D                             ; A019 45 2D                    E-
        sta     $39                             ; A01B 85 39                    .9
        lda     $38                             ; A01D A5 38                    .8
        ora     #$80                            ; A01F 09 80                    ..
        sta     $31                             ; A021 85 31                    .1
        dey                                     ; A023 88                       .
        lda     ($08),y                         ; A024 B1 08                    ..
        sta     $30                             ; A026 85 30                    .0
        lda     $25                             ; A028 A5 25                    .%
        rts                                     ; A02A 60                       `
; ----------------------------------------------------------------------------
LA02B_X3C:  sta     $08                             ; A02B 85 08                    ..
        sty     $09                             ; A02D 84 09                    ..
        ldy     #$07                            ; A02F A0 07                    ..
        jsr     L9C36                           ; A031 20 36 9C                  6.
        sta     $37                             ; A034 85 37                    .7
        dey                                     ; A036 88                       .
        jsr     L9C36                           ; A037 20 36 9C                  6.
        sta     $36                             ; A03A 85 36                    .6
        dey                                     ; A03C 88                       .
        jsr     L9C36                           ; A03D 20 36 9C                  6.
        sta     $35                             ; A040 85 35                    .5
        dey                                     ; A042 88                       .
        jsr     L9C36                           ; A043 20 36 9C                  6.
        sta     $34                             ; A046 85 34                    .4
        dey                                     ; A048 88                       .
        jsr     L9C36                           ; A049 20 36 9C                  6.
        sta     $33                             ; A04C 85 33                    .3
        dey                                     ; A04E 88                       .
        jsr     L9C36                           ; A04F 20 36 9C                  6.
        sta     $32                             ; A052 85 32                    .2
        dey                                     ; A054 88                       .
LA055:  jsr     L9C36                           ; A055 20 36 9C                  6.
        sta     $38                             ; A058 85 38                    .8
        eor     $2D                             ; A05A 45 2D                    E-
        sta     $39                             ; A05C 85 39                    .9
        lda     $38                             ; A05E A5 38                    .8
        ora     #$80                            ; A060 09 80                    ..
        sta     $31                             ; A062 85 31                    .1
        dey                                     ; A064 88                       .
        jsr     L9C36
        sta     $30                             ; A068 85 30                    .0
        lda     $25                             ; A06A A5 25                    .%
        rts                                     ; A06C 60                       `
; ----------------------------------------------------------------------------
LA06D_X68:  sta     $08                             ; A06D 85 08                    ..
        sty     $09                             ; A06F 84 09                    ..
        ldy     #$07
LA073:  jsr     L9C3E                           ; A073 20 3E 9C                  >.
        sta     $30,y                           ; A076 99 30 00                 .0.
        dey                                     ; A079 88                       .
        cpy     #$02                            ; A07A C0 02                    ..
        bcs     LA073                           ; A07C B0 F5                    ..
        jsr     L9C3E                           ; A07E 20 3E 9C                  >.
        sta     $38                             ; A081 85 38                    .8
        eor     $2D                             ; A083 45 2D                    E-
        sta     $39                             ; A085 85 39                    .9
        lda     $38                             ; A087 A5 38                    .8
        ora     #$80                            ; A089 09 80                    ..
        sta     $31                             ; A08B 85 31                    .1
        dey                                     ; A08D 88                       .
        jsr     L9C3E                           ; A08E 20 3E 9C                  >.
        sta     $30                             ; A091 85 30                    .0
        lda     $25                             ; A093 A5 25                    .%
        rts                                     ; A095 60                       `
; ----------------------------------------------------------------------------
LA096:  lda     $30                             ; A096 A5 30                    .0
LA098:  beq     LA0B9                           ; A098 F0 1F                    ..
        clc                                     ; A09A 18                       .
        adc     $25                             ; A09B 65 25                    e%
        bcc     LA0A3                           ; A09D 90 04                    ..
        bmi     LA0BE                           ; A09F 30 1D                    0.
        clc                                     ; A0A1 18                       .
        .byte   $2C                             ; A0A2 2C                       ,
LA0A3:  bpl     LA0B9                           ; A0A3 10 14                    ..
        adc     #$80                            ; A0A5 69 80                    i.
        sta     $25                             ; A0A7 85 25                    .%
        bne     LA0AE                           ; A0A9 D0 03                    ..
        jmp     L9D70                           ; A0AB 4C 70 9D                 Lp.
; ----------------------------------------------------------------------------
LA0AE:  lda     $39                             ; A0AE A5 39                    .9
        sta     $2D                             ; A0B0 85 2D                    .-
        rts                                     ; A0B2 60                       `
; ----------------------------------------------------------------------------
LA0B3:  lda     $2D                             ; A0B3 A5 2D                    .-
        eor     #$FF                            ; A0B5 49 FF                    I.
        bmi     LA0BE                           ; A0B7 30 05                    0.
LA0B9:  pla                                     ; A0B9 68                       h
        pla                                     ; A0BA 68                       h
        jmp     L9D6C                           ; A0BB 4C 6C 9D                 Ll.
; ----------------------------------------------------------------------------
LA0BE:  jmp     L9E2F                           ; A0BE 4C 2F 9E                 L/.
; ----------------------------------------------------------------------------
LA0C1:  jsr     LA27B_X48                           ; A0C1 20 7B A2                  {.
        tax                                     ; A0C4 AA                       .
        beq     LA0D7                           ; A0C5 F0 10                    ..
        clc                                     ; A0C7 18                       .
        adc     #$02                            ; A0C8 69 02                    i.
        bcs     LA0BE                           ; A0CA B0 F2                    ..
        ldx     #$00                            ; A0CC A2 00                    ..
        stx     $39                             ; A0CE 86 39                    .9
        jsr     L9CCB                           ; A0D0 20 CB 9C                  ..
        inc     $25                             ; A0D3 E6 25                    .%
        beq     LA0BE                           ; A0D5 F0 E7                    ..
LA0D7:  rts                                     ; A0D7 60                       `
; ----------------------------------------------------------------------------
LA0D8:  sty     $20                             ; A0D8 84 20                    .
        brk                                     ; A0DA 00                       .
        brk                                     ; A0DB 00                       .
        brk                                     ; A0DC 00                       .
        brk                                     ; A0DD 00                       .
        brk                                     ; A0DE 00                       .
        brk                                     ; A0DF 00                       .
LA0E0:  ldx     #$14                            ; A0E0 A2 14                    ..
        jmp     LFB4B                           ; A0E2 4C 4B FB                 LK.
; ----------------------------------------------------------------------------
LA0E5:  jsr     LA27B_X48
        lda     #<LA0D8
        ldy     #>LA0D8                         ; A0EA A0 A0                    ..
        ldx     #$00                            ; A0EC A2 00                    ..
LA0EE:  stx     $39                             ; A0EE 86 39                    .9
        jsr     LA1DD_X42_INDIRECT_STUFF_LOAD       ; A0F0 20 DD A1                  ..
        jmp     LA0F9_X1A                           ; A0F3 4C F9 A0                 L..
; ----------------------------------------------------------------------------
LA0F6_X18:  jsr     LA02B_X3C                           ; A0F6 20 2B A0                  +.
LA0F9_X1A:  beq     LA0E0                           ; A0F9 F0 E5                    ..
        jsr     LA28A_X32                           ; A0FB 20 8A A2                  ..
        lda     #$00                            ; A0FE A9 00                    ..
        sec                                     ; A100 38                       8
        sbc     $25                             ; A101 E5 25                    .%
        sta     $25                             ; A103 85 25                    .%
        jsr     LA096
        inc     $25
        beq     LA0BE
        ldx     #$f9
        lda     #$01
LA110:  ldy     $31                             ; A110 A4 31                    .1
        cpy     $26                             ; A112 C4 26                    .&
        bne     LA138                           ; A114 D0 22                    ."
        ldy     $32                             ; A116 A4 32                    .2
        cpy     $27                             ; A118 C4 27                    .'
        bne     LA138                           ; A11A D0 1C                    ..
        ldy     $33                             ; A11C A4 33                    .3
        cpy     $28                             ; A11E C4 28                    .(
        bne     LA138                           ; A120 D0 16                    ..
        ldy     $34                             ; A122 A4 34                    .4
        cpy     $29                             ; A124 C4 29                    .)
        bne     LA138                           ; A126 D0 10                    ..
        ldy     $35                             ; A128 A4 35                    .5
        cpy     $2A                             ; A12A C4 2A                    .*
        bne     LA138                           ; A12C D0 0A                    ..
        ldy     $36                             ; A12E A4 36                    .6
        cpy     $2B                             ; A130 C4 2B                    .+
        bne     LA138                           ; A132 D0 04                    ..
        ldy     $37                             ; A134 A4 37                    .7
        cpy     $2C                             ; A136 C4 2C                    .,
LA138:  php                                     ; A138 08                       .
        rol     a                               ; A139 2A                       *
        bcc     LA145                           ; A13A 90 09                    ..
        inx                                     ; A13C E8                       .
        sta     $12,x                           ; A13D 95 12                    ..
        beq     LA18B                           ; A13F F0 4A                    .J
        bpl     LA18F                           ; A141 10 4C                    .L
        lda     #$01                            ; A143 A9 01                    ..
LA145:  plp                                     ; A145 28                       (
        bcs     LA15C                           ; A146 B0 14                    ..
LA148:  asl     $37                             ; A148 06 37                    .7
LA14A:  rol     $36                             ; A14A 26 36                    &6
        rol     $35                             ; A14C 26 35                    &5
        rol     $34                             ; A14E 26 34                    &4
        rol     $33                             ; A150 26 33                    &3
        rol     $32                             ; A152 26 32                    &2
        rol     $31                             ; A154 26 31                    &1
        bcs     LA138                           ; A156 B0 E0                    ..
        bmi     LA110                           ; A158 30 B6                    0.
        bpl     LA138                           ; A15A 10 DC                    ..
LA15C:  tay                                     ; A15C A8                       .
        lda     $37                             ; A15D A5 37                    .7
        sbc     $2C                             ; A15F E5 2C                    .,
        sta     $37                             ; A161 85 37                    .7
        lda     $36                             ; A163 A5 36                    .6
        sbc     $2B                             ; A165 E5 2B                    .+
        sta     $36                             ; A167 85 36                    .6
        lda     $35                             ; A169 A5 35                    .5
        sbc     $2A                             ; A16B E5 2A                    .*
        sta     $35                             ; A16D 85 35                    .5
        lda     $34                             ; A16F A5 34                    .4
        sbc     $29                             ; A171 E5 29                    .)
        sta     $34                             ; A173 85 34                    .4
        lda     $33
        sbc     $28                             ; A177 E5 28                    .(
        sta     $33                             ; A179 85 33                    .3
        lda     $32                             ; A17B A5 32                    .2
        sbc     $27                             ; A17D E5 27                    .'
        sta     $32                             ; A17F 85 32                    .2
        lda     $31                             ; A181 A5 31                    .1
        sbc     $26                             ; A183 E5 26                    .&
        sta     $31                             ; A185 85 31                    .1
        tya                                     ; A187 98                       .
        jmp     LA148                           ; A188 4C 48 A1                 LH.
; ----------------------------------------------------------------------------
LA18B:  lda     #$40                            ; A18B A9 40                    .@
        bne     LA145                           ; A18D D0 B6                    ..
LA18F:  asl     a                               ; A18F 0A                       .
        asl     a                               ; A190 0A                       .
        asl     a                               ; A191 0A                       .
        asl     a                               ; A192 0A                       .
        asl     a                               ; A193 0A                       .
        asl     a                               ; A194 0A                       .
        sta     $3A                             ; A195 85 3A                    .:
        plp                                     ; A197 28                       (
LA198:  lda     $0C                             ; A198 A5 0C                    ..
        sta     $26                             ; A19A 85 26                    .&
        lda     $0D                             ; A19C A5 0D                    ..
        sta     $27                             ; A19E 85 27                    .'
        lda     $0E                             ; A1A0 A5 0E                    ..
        sta     $28                             ; A1A2 85 28                    .(
        lda     $0F                             ; A1A4 A5 0F                    ..
        sta     $29                             ; A1A6 85 29                    .)
        lda     $10                             ; A1A8 A5 10                    ..
        sta     $2A                             ; A1AA 85 2A                    .*
        lda     $11                             ; A1AC A5 11                    ..
        sta     $2B                             ; A1AE 85 2B                    .+
        lda     $12                             ; A1B0 A5 12                    ..
        sta     $2C                             ; A1B2 85 2C                    .,
        jmp     L9D40                           ; A1B4 4C 40 9D                 L@.
; ----------------------------------------------------------------------------
LA1B7_X6A:  sec                                     ; A1B7 38                       8
        sta     $08                             ; A1B8 85 08                    ..
        sty     $09                             ; A1BA 84 09                    ..
        ldy     #$07                            ; A1BC A0 07                    ..
LA1BE:  jsr     L9C3E                           ; A1BE 20 3E 9C                  >.
        sta     $25,y                           ; A1C1 99 25 00                 .%.
        dey                                     ; A1C4 88                       .
        cpy     #$02                            ; A1C5 C0 02                    ..
        bcs     LA1BE                           ; A1C7 B0 F5                    ..
        jsr     L9C3E                           ; A1C9 20 3E 9C                  >.
        sta     $2D                             ; A1CC 85 2D                    .-
        ora     #$80                            ; A1CE 09 80                    ..
        sta     $26                             ; A1D0 85 26                    .&
        dey                                     ; A1D2 88                       .
        jsr     L9C3E                           ; A1D3 20 3E 9C                  >.
        sta     $25                             ; A1D6 85 25                    .%
        sty     $3A                             ; A1D8 84 3A                    .:
        rts                                     ; A1DA 60                       `
; ----------------------------------------------------------------------------
LA1DB_X40:  clc                                     ; A1DB 18                       .
        .byte   $24 ;skip next byte (sec)       ; A1DC 24                       $
LA1DD_X42_INDIRECT_STUFF_LOAD:
        sec                                     ; A1DD 38                       8
        sta     $08                             ; A1DE 85 08                    ..
        sty     $09                             ; A1E0 84 09                    ..
        ldy     #$07                            ; A1E2 A0 07                    ..
        jsr     LA331_LOAD_INDIRECT_FROM_08_JMP_L9C36                           ; A1E4 20 31 A3                  1.
        sta     $2C                             ; A1E7 85 2C                    .,
        dey                                     ; A1E9 88                       .
        jsr     LA331_LOAD_INDIRECT_FROM_08_JMP_L9C36                           ; A1EA 20 31 A3                  1.
        sta     $2B                             ; A1ED 85 2B                    .+
        dey                                     ; A1EF 88                       .
        jsr     LA331_LOAD_INDIRECT_FROM_08_JMP_L9C36                           ; A1F0 20 31 A3                  1.
        sta     $2A                             ; A1F3 85 2A                    .*
        dey                                     ; A1F5 88                       .
        jsr     LA331_LOAD_INDIRECT_FROM_08_JMP_L9C36                           ; A1F6 20 31 A3                  1.
        sta     $29                             ; A1F9 85 29                    .)
        dey                                     ; A1FB 88                       .
        jsr     LA331_LOAD_INDIRECT_FROM_08_JMP_L9C36                           ; A1FC 20 31 A3                  1.
        sta     $28                             ; A1FF 85 28                    .(
        dey                                     ; A201 88                       .
        jsr     LA331_LOAD_INDIRECT_FROM_08_JMP_L9C36                           ; A202 20 31 A3                  1.
        sta     $27                             ; A205 85 27                    .'
        dey                                     ; A207 88                       .
        jsr     LA331_LOAD_INDIRECT_FROM_08_JMP_L9C36                           ; A208 20 31 A3                  1.
        sta     $2D                             ; A20B 85 2D                    .-
        ora     #$80                            ; A20D 09 80                    ..
        sta     $26                             ; A20F 85 26                    .&
        dey                                     ; A211 88                       .
        jsr     LA331_LOAD_INDIRECT_FROM_08_JMP_L9C36                           ; A212 20 31 A3                  1.
        sta     $25                             ; A215 85 25                    .%
        sty     $3A                             ; A217 84 3A                    .:
        rts                                     ; A219 60                       `
; ----------------------------------------------------------------------------
LA21A_X44:  tax                                     ; A21A AA                       .
        bra     LA227                           ; A21B 80 0A                    ..
LA21D:  ldx     #$1D                            ; A21D A2 1D                    ..
        .byte   $2C                             ; A21F 2C                       ,
LA220:  ldx     #$15                            ; A220 A2 15                    ..
        LDY     #$00
        beq     LA227                           ; A224 F0 01                    ..
LA226_X56:  tax                                     ; A226 AA                       .
LA227:  jsr     LA28A_X32                           ; A227 20 8A A2                  ..
        stx     $08                             ; A22A 86 08                    ..
        sty     $09                             ; A22C 84 09                    ..
        ldy     #$07                            ; A22E A0 07                    ..
        lda     #$08                            ; A230 A9 08                    ..
        sta     $0360                           ; A232 8D 60 03                 .`.
        lda     $2C                             ; A235 A5 2C                    .,
        jsr     GO_RAM_STORE_GO_KERN            ; A237 20 5C 03                  \.
        dey                                     ; A23A 88                       .
        lda     $2B                             ; A23B A5 2B                    .+
        jsr     GO_RAM_STORE_GO_KERN            ; A23D 20 5C 03                  \.
        dey                                     ; A240 88                       .
        lda     $2A                             ; A241 A5 2A                    .*
        jsr     GO_RAM_STORE_GO_KERN            ; A243 20 5C 03                  \.
        dey                                     ; A246 88                       .
        lda     $29                             ; A247 A5 29                    .)
        jsr     GO_RAM_STORE_GO_KERN            ; A249 20 5C 03                  \.
        dey                                     ; A24C 88                       .
        lda     $28                             ; A24D A5 28                    .(
        jsr     GO_RAM_STORE_GO_KERN            ; A24F 20 5C 03                  \.
        dey                                     ; A252 88                       .
        lda     $27                             ; A253 A5 27                    .'
        jsr     GO_RAM_STORE_GO_KERN            ; A255 20 5C 03                  \.
        dey                                     ; A258 88                       .
        lda     $2D                             ; A259 A5 2D                    .-
        ora     #$7F                            ; A25B 09 7F                    ..
        and     $26                             ; A25D 25 26                    %&
        jsr     GO_RAM_STORE_GO_KERN            ; A25F 20 5C 03                  \.
        dey                                     ; A262 88                       .
        lda     $25                             ; A263 A5 25                    .%
        jsr     GO_RAM_STORE_GO_KERN            ; A265 20 5C 03                  \.
        sty     $3A                             ; A268 84 3A                    .:
        rts                                     ; A26A 60                       `
; ----------------------------------------------------------------------------
LA26B_X46:  lda     $38                             ; A26B A5 38                    .8
LA26D:  sta     $2D                             ; A26D 85 2D                    .-
        ldx     #$08                            ; A26F A2 08                    ..
LA271:  lda     $2F,x                           ; A271 B5 2F                    ./
        sta     $24,x                           ; A273 95 24                    .$
        dex                                     ; A275 CA                       .
        bne     LA271                           ; A276 D0 F9                    ..
        stx     $3A                             ; A278 86 3A                    .:
        rts                                     ; A27A 60                       `
; ----------------------------------------------------------------------------
LA27B_X48:  jsr     LA28A_X32                           ; A27B 20 8A A2                  ..
LA27E:  ldx     #$09                            ; A27E A2 09                    ..
LA280:  lda     $24,x                           ; A280 B5 24                    .$
        sta     $2F,x                           ; A282 95 2F                    ./
        dex                                     ; A284 CA                       .
        bne     LA280                           ; A285 D0 F9                    ..
        stx     $3A                             ; A287 86 3A                    .:
LA289:  rts                                     ; A289 60                       `
; ----------------------------------------------------------------------------
LA28A_X32:  lda     $25                             ; A28A A5 25                    .%
        beq     LA289                           ; A28C F0 FB                    ..
        asl     $3A                             ; A28E 06 3A                    .:
        bcc     LA289                           ; A290 90 F7                    ..
LA292:  jsr     L9E14                           ; A292 20 14 9E                  ..
        bne     LA289                           ; A295 D0 F2                    ..
        jmp     L9DC5                           ; A297 4C C5 9D                 L..
; ----------------------------------------------------------------------------
LA29A_X36:  lda     $25                             ; A29A A5 25                    .%
        beq     LA2A7                           ; A29C F0 09                    ..
LA29E:  lda     $2D                             ; A29E A5 2D                    .-
LA2A0:  rol     a                               ; A2A0 2A                       *
        lda     #$FF                            ; A2A1 A9 FF                    ..
        bcs     LA2A7                           ; A2A3 B0 02                    ..
        lda     #$01                            ; A2A5 A9 01                    ..
LA2A7:  rts                                     ; A2A7 60                       `
; ----------------------------------------------------------------------------
LA2A8_X5A:  jsr     LA29A_X36                           ; A2A8 20 9A A2                  ..
LA2AB_X4A:  sta     $26                             ; A2AB 85 26                    .&
        lda     #$00                            ; A2AD A9 00                    ..
        sta     $27                             ; A2AF 85 27                    .'
        ldx     #$88                            ; A2B1 A2 88                    ..
LA2B3:  lda     $26                             ; A2B3 A5 26                    .&
        eor     #$FF                            ; A2B5 49 FF                    I.
        rol     a                               ; A2B7 2A                       *
LA2B8:  lda     #$00                            ; A2B8 A9 00                    ..
        sta     $2C                             ; A2BA 85 2C                    .,
        sta     $2B                             ; A2BC 85 2B                    .+
        sta     $2A                             ; A2BE 85 2A                    .*
        sta     $29                             ; A2C0 85 29                    .)
        sta     $28                             ; A2C2 85 28                    .(
LA2C4:  stx     $25                             ; A2C4 86 25                    .%
        sta     $3A                             ; A2C6 85 3A                    .:
        sta     $2D                             ; A2C8 85 2D                    .-
LA2CB = *+2
        jmp     L9D3B
LA2CD_X4C:  phy
        plx
        bra     LA2C4                           ; A2CF 80 F3                    ..
LA2D1_X0A:  phy                                     ; A2D1 5A                       Z
        plx                                     ; A2D2 FA                       .
        bra     LA2B8                           ; A2D3 80 E3                    ..
LA2D5_X4E:  phy                                     ; A2D5 5A                       Z
        plx                                     ; A2D6 FA                       .
        bra     LA2B3                           ; A2D7 80 DA                    ..
LA2D9_X34:  lsr     $2D                             ; A2D9 46 2D                    F-
        rts                                     ; A2DB 60                       `
; ----------------------------------------------------------------------------
LA2DC_X38_UNKNOWN_INDIRECT_STUFF_LOAD:
        sta     $0A                             ; A2DC 85 0A                    ..
        sty     $0B                             ; A2DE 84 0B                    ..
        ldy     #$00                            ; A2E0 A0 00                    ..
        lda     ($0A),y                         ; A2E2 B1 0A                    ..
        iny                                     ; A2E4 C8                       .
        tax                                     ; A2E5 AA                       .
        beq     LA29A_X36                           ; A2E6 F0 B2                    ..
        lda     ($0A),y                         ; A2E8 B1 0A                    ..
        eor     $2D                             ; A2EA 45 2D                    E-
        bmi     LA29E                           ; A2EC 30 B0                    0.
        cpx     $25                             ; A2EE E4 25                    .%
        bne     LA328                           ; A2F0 D0 36                    .6
        lda     ($0A),y                         ; A2F2 B1 0A                    ..
        ora     #$80                            ; A2F4 09 80                    ..
        cmp     $26                             ; A2F6 C5 26                    .&
        bne     LA328                           ; A2F8 D0 2E                    ..
        iny                                     ; A2FA C8                       .
        lda     ($0A),y                         ; A2FB B1 0A                    ..
        cmp     $27                             ; A2FD C5 27                    .'
        bne     LA328                           ; A2FF D0 27                    .'
        iny                                     ; A301 C8                       .
        lda     ($0A),y                         ; A302 B1 0A                    ..
        cmp     $28                             ; A304 C5 28                    .(
        bne     LA328                           ; A306 D0 20                    .
        iny                                     ; A308 C8                       .
        lda     ($0A),y                         ; A309 B1 0A                    ..
        cmp     $29                             ; A30B C5 29                    .)
        bne     LA328                           ; A30D D0 19                    ..
        iny                                     ; A30F C8                       .
        lda     ($0A),y                         ; A310 B1 0A                    ..
        cmp     $2A                             ; A312 C5 2A                    .*
        bne     LA328                           ; A314 D0 12                    ..
        iny                                     ; A316 C8                       .
        lda     ($0A),y                         ; A317 B1 0A                    ..
        cmp     $2B                             ; A319 C5 2B                    .+
        bne     LA328                           ; A31B D0 0B                    ..
        iny                                     ; A31D C8                       .
        lda     #$7F                            ; A31E A9 7F                    ..
        cmp     $3A                             ; A320 C5 3A                    .:
        lda     ($0A),y                         ; A322 B1 0A                    ..
        sbc     $2C                             ; A324 E5 2C                    .,
        beq     LA357                           ; A326 F0 2F                    ./
LA328:  lda     $2D                             ; A328 A5 2D                    .-
        bcc     LA32E                           ; A32A 90 02                    ..
        eor     #$FF                            ; A32C 49 FF                    I.
LA32E:  jmp     LA2A0                           ; A32E 4C A0 A2                 L..
; ----------------------------------------------------------------------------
LA331_LOAD_INDIRECT_FROM_08_JMP_L9C36:
        lda     ($08),y                         ; A331 B1 08                    ..
        bcs     LA357                           ; A333 B0 22                    ."
        jmp     L9C36                           ; A335 4C 36 9C                 L6.
; ----------------------------------------------------------------------------
LA338_X50:  lda     $25                             ; A338 A5 25                    .%
        beq     LA386                           ; A33A F0 4A                    .J
        sec                                     ; A33C 38                       8
        sbc     #$B8                            ; A33D E9 B8                    ..
        bit     $2D                             ; A33F 24 2D                    $-
        bpl     LA34C                           ; A341 10 09                    ..
        tax                                     ; A343 AA                       .
        lda     #$FF                            ; A344 A9 FF                    ..
        sta     $2F                             ; A346 85 2F                    ./
        jsr     L9DE0                           ; A348 20 E0 9D                  ..
        txa                                     ; A34B 8A                       .
LA34C:  ldx     #$25                            ; A34C A2 25                    .%
        cmp     #$F9                            ; A34E C9 F9                    ..
        bpl     LA358                           ; A350 10 06                    ..
        jsr     L9E56                           ; A352 20 56 9E                  V.
        sty     $2F                             ; A355 84 2F                    ./
LA357:  rts                                     ; A357 60                       `
; ----------------------------------------------------------------------------
LA358:  tay                                     ; A358 A8                       .
        lda     $2D                             ; A359 A5 2D                    .-
        and     #$80                            ; A35B 29 80                    ).
        lsr     $26                             ; A35D 46 26                    F&
        ora     $26                             ; A35F 05 26                    .&
        sta     $26                             ; A361 85 26                    .&
        jsr     L9E6D                           ; A363 20 6D 9E                  m.
        sty     $2F                             ; A366 84 2F                    ./
        rts                                     ; A368 60                       `
; ----------------------------------------------------------------------------
LA369_X1E:  lda     $25                             ; A369 A5 25                    .%
        cmp     #$B8                            ; A36B C9 B8                    ..
        bcs     LA395                           ; A36D B0 26                    .&
        jsr     LA338_X50                           ; A36F 20 38 A3                  8.
        sty     $3a
        lda     $2D                             ; A374 A5 2D                    .-
        sty     $2D                             ; A376 84 2D                    .-
        eor     #$80                            ; A378 49 80                    I.
        rol     a                               ; A37A 2A                       *
        lda     #$B8                            ; A37B A9 B8                    ..
        sta     $25                             ; A37D 85 25                    .%
        lda     $2C                             ; A37F A5 2C                    .,
        sta     $00                             ; A381 85 00                    ..
        jmp     L9D3B                           ; A383 4C 3B 9D                 L;.
; ----------------------------------------------------------------------------
LA386:  sta     $26                             ; A386 85 26                    .&
        sta     $27                             ; A388 85 27                    .'
        sta     $28                             ; A38A 85 28                    .(
        sta     $29                             ; A38C 85 29                    .)
        sta     $2a
        sta     $2B                             ; A390 85 2B                    .+
        sta     $2C                             ; A392 85 2C                    .,
        tay                                     ; A394 A8                       .
LA395:  rts                                     ; A395 60                       `
; ----------------------------------------------------------------------------
LA396_X54:  ldy     #$00                            ; A396 A0 00                    ..
LA398:  ldx     #$0D                            ; A398 A2 0D                    ..
LA39A:  sty     $21,x                           ; A39A 94 21                    .!
        dex                                     ; A39C CA                       .
        bpl     LA39A                           ; A39D 10 FB                    ..
        bcc     LA3B0                           ; A39F 90 0F                    ..
        cmp     #$2D                            ; A3A1 C9 2D                    .-
        bne     LA3A9                           ; A3A3 D0 04                    ..
        stx     $2E                             ; A3A5 86 2E                    ..
        beq     LA3AD                           ; A3A7 F0 04                    ..
LA3A9:  cmp     #$2B                            ; A3A9 C9 2B                    .+
        bne     LA3B2                           ; A3AB D0 05                    ..
LA3AD:  jsr     L9C0D                           ; A3AD 20 0D 9C                  ..
LA3B0:  bcc     LA40D                           ; A3B0 90 5B                    .[
LA3B2:  cmp     #$2E                            ; A3B2 C9 2E                    ..
        beq     LA3E4                           ; A3B4 F0 2E                    ..
        cmp     #$45                            ; A3B6 C9 45                    .E
        bne     LA3EA                           ; A3B8 D0 30                    .0
        jsr     L9C0D                           ; A3BA 20 0D 9C                  ..
        bcc     LA3D6                           ; A3BD 90 17                    ..
        cmp     #$AB                            ; A3BF C9 AB                    ..
LA3C1:  beq     LA3D1                           ; A3C1 F0 0E                    ..
        cmp     #$2D                            ; A3C3 C9 2D                    .-
        beq     LA3D1                           ; A3C5 F0 0A                    ..
        cmp     #$AA                            ; A3C7 C9 AA                    ..
        beq     LA3D3                           ; A3C9 F0 08                    ..
        cmp     #$2B                            ; A3CB C9 2B                    .+
        beq     LA3D3                           ; A3CD F0 04                    ..
LA3CF:  bne     LA3D8                           ; A3CF D0 07                    ..
LA3D1:  ror     $24                             ; A3D1 66 24                    f$
LA3D3:  jsr     L9C0D                           ; A3D3 20 0D 9C                  ..
LA3D6:  bcc     LA434                           ; A3D6 90 5C                    .\
LA3D8:  bit     $24                             ; A3D8 24 24                    $$
        bpl     LA3EA                           ; A3DA 10 0E                    ..
        lda     #$00                            ; A3DC A9 00                    ..
        sec                                     ; A3DE 38                       8
        sbc     $22                             ; A3DF E5 22                    ."
        jmp     LA3EC                           ; A3E1 4C EC A3                 L..
; ----------------------------------------------------------------------------
LA3E4:  ror     $23                             ; A3E4 66 23                    f#
        bit     $23                             ; A3E6 24 23                    $#
        bvc     LA3AD                           ; A3E8 50 C3                    P.
LA3EA:  lda     $22                             ; A3EA A5 22                    ."
LA3EC:  sec                                     ; A3EC 38                       8
        sbc     $21                             ; A3ED E5 21                    .!
        sta     $22                             ; A3EF 85 22                    ."
        beq     LA405                           ; A3F1 F0 12                    ..
        bpl     LA3FE                           ; A3F3 10 09                    ..
LA3F5:  jsr     LA0E5                           ; A3F5 20 E5 A0                  ..
        inc     $22                             ; A3F8 E6 22                    ."
        bne     LA3F5                           ; A3FA D0 F9                    ..
        beq     LA405                           ; A3FC F0 07                    ..
LA3FE:  jsr     LA0C1                           ; A3FE 20 C1 A0                  ..
        dec     $22                             ; A401 C6 22                    ."
        bne     LA3FE                           ; A403 D0 F9                    ..
LA405:  lda     $2E                             ; A405 A5 2E                    ..
        bmi     LA40A                           ; A407 30 01                    0.
        rts                                     ; A409 60                       `
; ----------------------------------------------------------------------------
LA40A:  jmp     LA6A7_X22                           ; A40A 4C A7 A6                 L..
; ----------------------------------------------------------------------------
LA40D:  pha                                     ; A40D 48                       H
LA40E:  bit     $23                             ; A40E 24 23                    $#
        bpl     LA414                           ; A410 10 02                    ..
        inc     $21                             ; A412 E6 21                    .!
LA414:  jsr     LA0C1                           ; A414 20 C1 A0                  ..
        pla                                     ; A417 68                       h
        sec                                     ; A418 38                       8
        sbc     #$30                            ; A419 E9 30                    .0
        jsr     LA421_X52                           ; A41B 20 21 A4                  !.
        jmp     LA3AD                           ; A41E 4C AD A3                 L..
; ----------------------------------------------------------------------------
LA421_X52:  pha                                     ; A421 48                       H
        jsr     LA27B_X48                           ; A422 20 7B A2                  {.
        pla                                     ; A425 68                       h
        jsr     LA2AB_X4A                           ; A426 20 AB A2                  ..
        lda     $38                             ; A429 A5 38                    .8
        eor     $2D                             ; A42B 45 2D                    E-
        sta     $39                             ; A42D 85 39                    .9
        ldx     $25                             ; A42F A6 25                    .%
        jmp     L9CBE_X12                           ; A431 4C BE 9C                 L..
; ----------------------------------------------------------------------------
LA434:  lda     $22                             ; A434 A5 22                    ."
        cmp     #$0A                            ; A436 C9 0A                    ..
        bcc     LA443                           ; A438 90 09                    ..
        lda     #$64                            ; A43A A9 64                    .d
        bit     $24                             ; A43C 24 24                    $$
        bmi     LA456                           ; A43E 30 16                    0.
        jmp     L9E2F                           ; A440 4C 2F 9E                 L/.
; ----------------------------------------------------------------------------
LA443:  asl     a                               ; A443 0A                       .
        asl     a                               ; A444 0A                       .
        clc                                     ; A445 18                       .
        adc     $22                             ; A446 65 22                    e"
        asl     a                               ; A448 0A                       .
        clc                                     ; A449 18                       .
        ldy     #$00                            ; A44A A0 00                    ..
        sta     $22                             ; A44C 85 22                    ."
        jsr     L9C2E                           ; A44E 20 2E 9C                  ..
        adc     $22
        sec                                     ; A453 38                       8
        sbc     #$30                            ; A454 E9 30                    .0
LA456:  sta     $22                             ; A456 85 22                    ."
        jmp     LA3D3                           ; A458 4C D3 A3                 L..
; ----------------------------------------------------------------------------
LA45B:  .byte   $AF,$35,$E6,$20,$F4,$7F,$FF,$CC ; A45B AF 35 E6 20 F4 7F FF CC  .5. ....
LA463:  .byte   $B2,$63,$5F,$A9,$31,$9F,$FF,$E8 ; A463 B2 63 5F A9 31 9F FF E8  .c_.1...
LA46B:  .byte   $B2,$63,$5F,$A9,$31,$9F,$FF,$FC ; A46B B2 63 5F A9 31 9F FF FC  .c_.1...
; ----------------------------------------------------------------------------
LA473_X04:  ldy     #$01
LA475_X58:  lda     #$20
        bit     $2d
        bpl     $a47d
        lda     #$2d
        sta     $00ff,Y
        sta     $2d
        sty     $3b
        iny
        lda     #$30
        ldx     $25
        bne     LA48E
; ----------------------------------------------------------------------------
LA48B:  jmp     LA5B2                           ; A48B 4C B2 A5                 L..
; ----------------------------------------------------------------------------
LA48E:  lda     #$00                            ; A48E A9 00                    ..
        cpx     #$80                            ; A490 E0 80                    ..
        beq     LA496                           ; A492 F0 02                    ..
        bcs     LA49F                           ; A494 B0 09                    ..
LA496:  lda     #<LA46B                         ; A496 A9 6B                    .k
        ldy     #>LA46B                         ; A498 A0 A4                    ..
        jsr     L9F2E_PROBABLY_JSR_TO_INDIRECT_STUFF ; A49A 20 2E 9F                  ..
        lda     #$F1                            ; A49D A9 F1                    ..
LA49F:  sta     $21                             ; A49F 85 21                    .!
LA4A1:  lda     #<LA463                         ; A4A1 A9 63                    .c
        ldy     #>LA463                         ; A4A3 A0 A4                    ..
        jsr     LA2DC_X38_UNKNOWN_INDIRECT_STUFF_LOAD ; A4A5 20 DC A2                  ..
        beq     LA4C8                           ; A4A8 F0 1E                    ..
        bpl     LA4BE                           ; A4AA 10 12                    ..
LA4AC:  lda     #<LA45B                         ; A4AC A9 5B                    .[
        ldy     #>LA45B                         ; A4AE A0 A4                    ..
        jsr     LA2DC_X38_UNKNOWN_INDIRECT_STUFF_LOAD ; A4B0 20 DC A2                  ..
LA4B3:  beq     LA4B7                           ; A4B3 F0 02                    ..
        bpl     LA4C5                           ; A4B5 10 0E                    ..
LA4B7:  jsr     LA0C1                           ; A4B7 20 C1 A0                  ..
        dec     $21                             ; A4BA C6 21                    .!
        bne     LA4AC                           ; A4BC D0 EE                    ..
LA4BE:  jsr     LA0E5                           ; A4BE 20 E5 A0                  ..
        inc     $21                             ; A4C1 E6 21                    .!
        bne     LA4A1                           ; A4C3 D0 DC                    ..
LA4C5:  jsr     L9F38                           ; A4C5 20 38 9F                  8.
LA4C8:  jsr     LA338_X50                           ; A4C8 20 38 A3                  8.
        ldx     #$01                            ; A4CB A2 01                    ..
        lda     $21
        clc
        adc     #$10                            ; A4D0 69 10                    i.
LA4D2:  bmi     LA4DD                           ; A4D2 30 09                    0.
        cmp     #$11                            ; A4D4 C9 11                    ..
        bcs     LA4DE                           ; A4D6 B0 06                    ..
        adc     #$FF                            ; A4D8 69 FF                    i.
        tax                                     ; A4DA AA                       .
        lda     #$02                            ; A4DB A9 02                    ..
LA4DD:  sec                                     ; A4DD 38                       8
LA4DE:  sbc     #$02                            ; A4DE E9 02                    ..
        sta     $22                             ; A4E0 85 22                    ."
        stx     $21                             ; A4E2 86 21                    .!
        txa                                     ; A4E4 8A                       .
        beq     LA4E9                           ; A4E5 F0 02                    ..
        bpl     LA4FC                           ; A4E7 10 13                    ..
LA4E9:  ldy     $3B                             ; A4E9 A4 3B                    .;
        lda     #$2E                            ; A4EB A9 2E                    ..
        iny                                     ; A4ED C8                       .
        sta     $FF,y                           ; A4EE 99 FF 00                 ...
        txa                                     ; A4F1 8A                       .
        beq     LA4FA                           ; A4F2 F0 06                    ..
        lda     #$30                            ; A4F4 A9 30                    .0
        iny                                     ; A4F6 C8                       .
        sta     $FF,y                           ; A4F7 99 FF 00                 ...
LA4FA:  sty     $3B                             ; A4FA 84 3B                    .;
LA4FC:  ldy     #$00                            ; A4FC A0 00                    ..
        ldx     #$80                            ; A4FE A2 80                    ..
LA500:  lda     $2C                             ; A500 A5 2C                    .,
        clc                                     ; A502 18                       .
        adc     LA5CD,y                         ; A503 79 CD A5                 y..
        sta     $2C                             ; A506 85 2C                    .,
        lda     $2B                             ; A508 A5 2B                    .+
        adc     LA5CC,y                         ; A50A 79 CC A5                 y..
        sta     $2B                             ; A50D 85 2B                    .+
        lda     $2A                             ; A50F A5 2A                    .*
        adc     LA5CB,y                         ; A511 79 CB A5                 y..
        sta     $2A                             ; A514 85 2A                    .*
        lda     $29                             ; A516 A5 29                    .)
        adc     LA5CA,y                         ; A518 79 CA A5                 y..
        sta     $29                             ; A51B 85 29                    .)
        lda     $28                             ; A51D A5 28                    .(
        adc     LA5C9,y                         ; A51F 79 C9 A5                 y..
        sta     $28                             ; A522 85 28                    .(
        lda     $27                             ; A524 A5 27                    .'
        adc     LA5C8,y                         ; A526 79 C8 A5                 y..
        sta     $27                             ; A529 85 27                    .'
        lda     $26                             ; A52B A5 26                    .&
        adc     LA5C7,y                         ; A52D 79 C7 A5                 y..
        sta     $26                             ; A530 85 26                    .&
        inx                                     ; A532 E8                       .
        bcs     LA539                           ; A533 B0 04                    ..
        bpl     LA500                           ; A535 10 C9                    ..
        bmi     LA53B                           ; A537 30 02                    0.
LA539:  bmi     LA500                           ; A539 30 C5                    0.
LA53B:  txa                                     ; A53B 8A                       .
        bcc     LA542                           ; A53C 90 04                    ..
        eor     #$ff
        adc     #$0a
LA542:  adc     #$2F                            ; A542 69 2F                    i/
        iny                                     ; A544 C8                       .
        iny                                     ; A545 C8                       .
        iny                                     ; A546 C8                       .
        iny                                     ; A547 C8                       .
        iny                                     ; A548 C8                       .
        iny                                     ; A549 C8                       .
        iny                                     ; A54A C8                       .
LA54B:  sty     $3D                             ; A54B 84 3D                    .=
        ldy     $3B                             ; A54D A4 3B                    .;
        iny                                     ; A54F C8                       .
        tax                                     ; A550 AA                       .
        and     #$7F                            ; A551 29 7F                    ).
        sta     $FF,y                           ; A553 99 FF 00                 ...
        dec     $21                             ; A556 C6 21                    .!
        BNE     LA560
        LDA     #$2e
        INY
        STA     $00ff,Y
LA560:  sty     $3B                             ; A560 84 3B                    .;
        ldy     $3D                             ; A562 A4 3D                    .=
LA564:  txa                                     ; A564 8A                       .
        eor     #$FF                            ; A565 49 FF                    I.
        and     #$80                            ; A567 29 80                    ).
        tax                                     ; A569 AA                       .
        cpy     #$69                            ; A56A C0 69                    .i
        beq     LA572                           ; A56C F0 04                    ..
        cpy     #$93                            ; A56E C0 93                    ..
        bne     LA500                           ; A570 D0 8E                    ..
LA572:  ldy     $3B                             ; A572 A4 3B                    .;
LA574:  lda     $00ff,Y
        dey
        cmp     #$30                            ; A578 C9 30                    .0
        beq     LA574                           ; A57A F0 F8                    ..
        cmp     #$2E                            ; A57C C9 2E                    ..
        beq     LA581                           ; A57E F0 01                    ..
        iny                                     ; A580 C8                       .
LA581:  lda     #$2B                            ; A581 A9 2B                    .+
        ldx     $22                             ; A583 A6 22                    ."
        beq     LA5B5                           ; A585 F0 2E                    ..
        bpl     LA591                           ; A587 10 08                    ..
        lda     #$00                            ; A589 A9 00                    ..
        sec                                     ; A58B 38                       8
        sbc     $22                             ; A58C E5 22                    ."
        tax                                     ; A58E AA                       .
        lda     #$2d
LA591:  sta     stack+1,y                       ; A591 99 01 01                 ...
        lda     #$45                            ; A594 A9 45                    .E
        sta     stack,y                         ; A596 99 00 01                 ...
        txa                                     ; A599 8A                       .
        ldx     #$2F                            ; A59A A2 2F                    ./
        sec                                     ; A59C 38                       8
LA59D:  inx                                     ; A59D E8                       .
        sbc     #$0A                            ; A59E E9 0A                    ..
        bcs     LA59D                           ; A5A0 B0 FB                    ..
        adc     #$3A                            ; A5A2 69 3A                    i:
        sta     stack+3,y                       ; A5A4 99 03 01                 ...
        txa                                     ; A5A7 8A                       .
        sta     stack+2,y                       ; A5A8 99 02 01                 ...
        lda     #$00                            ; A5AB A9 00                    ..
        sta     stack+4,y                       ; A5AD 99 04 01                 ...
        beq     LA5BA                           ; A5B0 F0 08                    ..
LA5B2:  sta     $FF,y                           ; A5B2 99 FF 00                 ...
LA5B5:  lda     #$00                            ; A5B5 A9 00                    ..
        sta     stack,y                         ; A5B7 99 00 01                 ...
LA5BA:  lda     #$00                            ; A5BA A9 00                    ..
        ldy     #$01                            ; A5BC A0 01                    ..
        rts                                     ; A5BE 60                       `
; ----------------------------------------------------------------------------
LA5BF:  bra     LA5C1                           ; A5BF 80 00                    ..

LA5C1:  brk                                     ; A5C1 00                       .
        brk                                     ; A5C2 00                       .
        brk                                     ; A5C3 00                       .
        brk                                     ; A5C4 00                       .
        brk                                     ; A5C5 00                       .
        brk                                     ; A5C6 00                       .
LA5C7:  .byte   $FF                             ; A5C7 FF                       .
LA5C8:  .byte   $A5                             ; A5C8 A5                       .
LA5C9:  .byte   $0C                             ; A5C9 0C                       .
LA5CA:  .byte   $EF                             ; A5CA EF                       .
LA5CB:  .byte   $85                             ; A5CB 85                       .
LA5CC:  .byte   $C0                             ; A5CC C0                       .
LA5CD:  brk                                     ; A5CD 00                       .
        brk                                     ; A5CE 00                       .
        ora     #$18                            ; A5CF 09 18                    ..
        lsr     $A072                           ; A5D1 4E 72 A0                 Nr.
        brk                                     ; A5D4 00                       .
        bbs7    $FF,LA5EF                       ; A5D5 FF FF 17                 ...
        .byte   $2B                             ; A5D8 2B                       +
        phy                                     ; A5D9 5A                       Z
LA5DA:  beq     LA5DC                           ; A5DA F0 00                    ..
LA5DC:  brk                                     ; A5DC 00                       .
        brk                                     ; A5DD 00                       .
        rmb1    $48                             ; A5DE 17 48                    .H
        ror     $E8,x                           ; A5E0 76 E8                    v.
        brk                                     ; A5E2 00                       .
LA5E3:  bbs7    $FF,LA5E3                       ; A5E3 FF FF FD                 ...
        .byte   $AB                             ; A5E6 AB                       .
        .byte   $F4                             ; A5E7 F4                       .
        trb     a:$00                           ; A5E8 1C 00 00                 ...
LA5EB:  brk                                     ; A5EB 00                       .
        brk                                     ; A5EC 00                       .
        .byte   $3B                             ; A5ED 3B                       ;
        txs                                     ; A5EE 9A                       .
LA5EF:  dex                                     ; A5EF CA                       .
        brk                                     ; A5F0 00                       .
        .byte   $FF                             ; A5F1 FF                       .
LA5F2:  .byte   $FF                             ; A5F2 FF                       .
LA5F3:  bbs7    $FA,LA5FF+1                     ; A5F3 FF FA 0A                 ...
        .byte   $1F                             ; A5F6 1F                       .
LA5F7:  brk                                     ; A5F7 00                       .
        brk                                     ; A5F8 00                       .
LA5F9:  brk                                     ; A5F9 00                       .
        brk                                     ; A5FA 00                       .
LA5FB:  brk                                     ; A5FB 00                       .
LA5FC:  tya                                     ; A5FC 98                       .
        stx     $80,y                           ; A5FD 96 80                    ..
LA5FF:  .byte   $FF                             ; A5FF FF                       .
        .byte   $FF                             ; A600 FF                       .
LA601:  bbs7    $FF,LA5F3+1                     ; A601 FF FF F0                 ...
LA604:  lda     a:$C0,x                         ; A604 BD C0 00                 ...
        brk                                     ; A607 00                       .
        brk                                     ; A608 00                       .
        brk                                     ; A609 00                       .
        ora     ($86,x)                         ; A60A 01 86                    ..
        ldy     #$FF                            ; A60C A0 FF                    ..
        .byte   $FF                             ; A60E FF                       .
        .byte   $FF                             ; A60F FF                       .
LA610:  bbs7    $FF,LA5EB                       ; A610 FF FF D8                 ...
        .byte   $F0                             ; A613 F0                       .
LA614:  brk                                     ; A614 00                       .
        brk                                     ; A615 00                       .
        brk                                     ; A616 00                       .
        brk                                     ; A617 00                       .
        brk                                     ; A618 00                       .
        .byte   $03                             ; A619 03                       .
        inx                                     ; A61A E8                       .
        .byte   $FF                             ; A61B FF                       .
        .byte   $FF                             ; A61C FF                       .
LA61D:  .byte $FF
        .byte $FF
        .byte $FF
LA620:  bbs7    $9C,LA623                       ; A620 FF 9C 00                 ...
LA623:  brk                                     ; A623 00                       .
        brk                                     ; A624 00                       .
        brk                                     ; A625 00                       .
        brk                                     ; A626 00                       .
        brk                                     ; A627 00                       .
        asl     a                               ; A628 0A                       .
        .byte   $FF                             ; A629 FF                       .
        .byte   $FF                             ; A62A FF                       .
LA62B:  .byte $FF, $FF, $FF                     ; A62B FF FF FF                 ...
LA62E:  .byte $FF, $FF, $FF                     ; A62E FF FF FF                 ...
LA631:  .byte $FF, $FF, $FF                     ; A631 FF FF FF                 ...
        .byte $DF, $0A, $80                     ; A634 DF 0A 80                 ...
        brk                                     ; A637 00                       .
LA638:  brk                                     ; A638 00                       .
        brk                                     ; A639 00                       .
        brk                                     ; A63A 00                       .
        .byte   $03                             ; A63B 03                       .
LA63C:  .byte   $4B                             ; A63C 4B                       K
        cpy     #$FF                            ; A63D C0 FF                    ..
        .byte   $FF                             ; A63F FF                       .
        .byte   $FF                             ; A640 FF                       .
LA641:  .byte $FF, $FF, $73
        rts                                     ; A644 60                       `
; ----------------------------------------------------------------------------
        brk                                     ; A645 00                       .
LA646:  brk                                     ; A646 00                       .
        brk                                     ; A647 00                       .
        brk                                     ; A648 00                       .
        brk                                     ; A649 00                       .
        asl     $FF10                           ; A64A 0E 10 FF                 ...
        .byte   $FF                             ; A64D FF                       .
        .byte   $FF                             ; A64E FF                       .
LA64F:  bbs7    $FF,LA64F                       ; A64F FF FF FD                 ...
        tay                                     ; A652 A8                       .
        brk                                     ; A653 00                       .
        brk                                     ; A654 00                       .
        brk                                     ; A655 00                       .
        brk                                     ; A656 00                       .
        brk                                     ; A657 00                       .
        brk                                     ; A658 00                       .
        .byte $3c
; ----------------------------------------------------------------------------
LA65A_X24:  jsr LA1DB_X40
        bra LA66B_X26
; ----------------------------------------------------------------------------
LA65F_X66:  bra LA66B_X26
; ----------------------------------------------------------------------------
LA661_X20:  jsr     LA27B_X48                           ; A661 20 7B A2                  {.
        lda     #<LA5BF                         ; A664 A9 BF                    ..
        ldy     #>LA5BF                         ; A666 A0 A5                    ..
        jsr     LA1DD_X42_INDIRECT_STUFF_LOAD                           ; A668 20 DD A1                  ..
LA66B_X26:  bne     LA670                           ; A66B D0 03                    ..
        jmp     LA72B_X28                           ; A66D 4C 2B A7                 L+.
; ----------------------------------------------------------------------------
LA670:  lda     $30                             ; A670 A5 30                    .0
        bne     LA677                           ; A672 D0 03                    ..
        jmp     L9D6E                           ; A674 4C 6E 9D                 Ln.
; ----------------------------------------------------------------------------
LA677:  ldx     #$41                            ; A677 A2 41                    .A
        ldy     #$00                            ; A679 A0 00                    ..
        jsr     LA227                           ; A67B 20 27 A2                  '.
        lda     $38                             ; A67E A5 38                    .8
        bpl     LA691                           ; A680 10 0F                    ..
        jsr     LA369_X1E                       ; A682 20 69 A3                  i.
        lda     #<MEM_0041                      ; A685 A9 41                    .A
        ldy     #>MEM_0041                      ; A687 A0 00                    ..
        jsr     LA2DC_X38_UNKNOWN_INDIRECT_STUFF_LOAD ; A689 20 DC A2                  ..
        bne     LA691                           ; A68C D0 03                    ..
        tya                                     ; A68E 98                       .
        ldy     $00                             ; A68F A4 00                    ..
LA691:  jsr     LA26D                           ; A691 20 6D A2                  m.
        tya                                     ; A694 98                       .
        pha                                     ; A695 48                       H
        jsr     L9EF0_X1C                           ; A696 20 F0 9E                  ..
        lda     #$41                            ; A699 A9 41                    .A
        ldy     #$00                            ; A69B A0 00                    ..
        jsr     L9F60_X14                           ; A69D 20 60 9F                  `.
        jsr     LA72B_X28                           ; A6A0 20 2B A7                  +.
        pla                                     ; A6A3 68                       h
        lsr     a                               ; A6A4 4A                       J
LA6A5:  bcc     LA6B1                           ; A6A5 90 0A                    ..
LA6A7_X22:  lda     $25                             ; A6A7 A5 25                    .%
        beq     LA6B1                           ; A6A9 F0 06                    ..
        lda     $2D                             ; A6AB A5 2D                    .-
        eor     #$FF                            ; A6AD 49 FF                    I.
        sta     $2D                             ; A6AF 85 2D                    .-
LA6B1:  rts                                     ; A6B1 60                       `
; ----------------------------------------------------------------------------
;TODO probably data
LA6B2:  sta     ($38,x)                         ; A6B2 81 38                    .8
        tax                                     ; A6B4 AA                       .
        .byte   $3B                             ; A6B5 3B                       ;
        and     #$5C                            ; A6B6 29 5C                    )\
        rmb1    $EE                             ; A6B8 17 EE                    ..
LA6BA:
        ora     $4A59                           ; A6BA 0D 59 4A                 .YJ
        brk                                     ; A6BD 00                       .
        brk                                     ; A6BE 00                       .
        brk                                     ; A6BF 00                       .
        brk                                     ; A6C0 00                       .
        brk                                     ; A6C1 00                       .
        brk                                     ; A6C2 00                       .
        ;TODO probably data
        eor     $DE61,x                         ; A6C3 5D 61 DE                 ]a.
        lda     ($87)                           ; A6C6 B2 87                    ..
LA6C8:  sbc     ($4C,x)                         ; A6C8 E1 4C                    .L
        trb     $7461                           ; A6CA 1C 61 74                 .at
        adc     $63                             ; A6CD 65 63                    ec
        txs                                     ; A6CF 9A                       .
        sta     $14D9                           ; A6D0 8D D9 14                 ...
        adc     $72                             ; A6D3 65 72                    er
        rmb6    INSRT                           ; A6D5 67 A8                    g.
        ldy     $765C                           ; A6D7 AC 5C 76                 .\v
        .byte   $44                             ; A6DA 44                       D
        adc     #$5A                            ; A6DB 69 5A                    iZ
        sta     ($9E)                           ; A6DD 92 9E                    ..
        stz     $3EAF                           ; A6DF 9C AF 3E                 ..>
        tsb     $316D                           ; A6E2 0C 6D 31                 .m1
        rts                                     ; A6E5 60                       `
; ----------------------------------------------------------------------------
;TODO probably data
        ora     ($1D),y                         ; A6E6 11 1D                    ..
        rol     $0E41                           ; A6E8 2E 41 0E                 .A.
        bvs     $A76C                           ; A6EB 70 7F                    p.
        sbc     $FE                             ; A6ED E5 FE                    ..
        bit     $8645                           ; A6EF 2C 45 86                 ,E.
        bit     $74                             ; A6F2 24 74                    $t
LA6F4:  and     ($84,x)                         ; A6F4 21 84                    !.
        bit     #$7C                            ; A6F6 89 7C                    .|
        rol     $3C,x                           ; A6F8 36 3C                    6<
        bmi     $A773                           ; A6FA 30 77                    0w
        rol     LFFC3_CLOSE                     ; A6FC 2E C3 FF                 ...
        bit     $3953,x                         ; A6FF 3C 53 39                 <S9
        .byte   $82                             ; A702 82                       .
        ply                                     ; A703 7A                       z
        ora     $5B95,x                         ; A704 1D 95 5B                 ..[
        adc     $73D2,x                         ; A707 7D D2 73                 }.s
        sty     $7C                             ; A70A 84 7C                    .|
        .byte   $63                             ; A70C 63                       c
        cli                                     ; A70D 58                       X
        lsr     SAL                             ; A70E 46 B8                    F.
        and     $05                             ; A710 25 05                    %.
        sed                                     ; A712 F8                       .
        ror     $FD75,x                         ; A713 7E 75 FD                 ~u.
        bbs6    $FC,LA72F                       ; A716 EF FC 16                 ...
        bit     L8074                           ; A719 2C 74 80                 ,t.
        and     ($72),y                         ; A71C 31 72                    1r
LA71E:  rmb1    $F7                             ; A71E 17 F7                    ..
        cmp     ($CF),y                         ; A720 D1 CF                    ..
        jmp     (MEM_0081,x)                    ; A722 7C 81 00                 |..
LA725:  brk                                     ; A725 00                       .
        brk                                     ; A726 00                       .
        brk                                     ; A727 00                       .
        brk                                     ; A728 00                       .
        brk                                     ; A729 00                       .
        brk                                     ; A72A 00                       .
LA72B_X28:
        lda     #<LA6B2                            ; A72B A9 B2                    ..
        ldy     #>LA6B2                            ; A72D A0 A6                    ..
LA72F:  jsr L9F2E_PROBABLY_JSR_TO_INDIRECT_STUFF
        lda     $3a
        adc     #$50
        bcc     LA73B
        jsr     LA292                           ; A738 20 92 A2                  ..
LA73B:  sta     $14                             ; A73B 85 14                    ..
        jsr     LA27E                           ; A73D 20 7E A2                  ~.
        lda     $25                             ; A740 A5 25                    .%
        cmp     #$88                            ; A742 C9 88                    ..
LA744:  bcc     LA749                           ; A744 90 03                    ..
LA746:  jsr     LA0B3                           ; A746 20 B3 A0                  ..
LA749:  jsr     LA369_X1E                       ; A749 20 69 A3                  i.
        lda     $00
        clc                                     ; A74E 18                       .
        adc     #$81                            ; A74F 69 81                    i.
        beq     LA746                           ; A751 F0 F3                    ..
        sec                                     ; A753 38                       8
        sbc     #$01                            ; A754 E9 01                    ..
        pha                                     ; A756 48                       H
        ldx     #$08                            ; A757 A2 08                    ..
LA759:  lda     $30,x                           ; A759 B5 30                    .0
        ldy     $25,x                           ; A75B B4 25                    .%
        sta     $25,x                           ; A75D 95 25                    .%
        sty     $30,x                           ; A75F 94 30                    .0
        dex                                     ; A761 CA                       .
        bpl     LA759                           ; A762 10 F5                    ..
        lda     $14                             ; A764 A5 14                    ..
        sta     $3A                             ; A766 85 3A                    .:
        jsr     L9CA7_X0E                           ; A768 20 A7 9C                  ..
        jsr     LA6A7_X22
        lda     #<LA6BA                            ; A76E A9 BA                    ..
        ldy     #>LA6BA                            ; A770 A0 A6                    ..
        jsr     LA794
        lda     #$00                            ; A775 A9 00                    ..
        sta     $39                             ; A777 85 39                    .9
        pla                                     ; A779 68                       h
        jsr     LA098                           ; A77A 20 98 A0                  ..
        rts                                     ; A77D 60                       `
; ----------------------------------------------------------------------------
LA77E_UNKNOWN_OTHER_INDIRECT_STUFF:
        sta     $3B                             ; A77E 85 3B                    .;
        sty     $3C                             ; A780 84 3C                    .<
        jsr     LA220                           ; A782 20 20 A2                   .
        lda     #$15                            ; A785 A9 15                    ..
        jsr     L9F60_X14                           ; A787 20 60 9F                  `.
        jsr     LA798                           ; A78A 20 98 A7                  ..
        lda     #$15                            ; A78D A9 15                    ..
        ldy     #$00                            ; A78F A0 00                    ..
        jmp     L9F60_X14                           ; A791 4C 60 9F                 L`.
; ----------------------------------------------------------------------------
LA794:  sta     $3B                             ; A794 85 3B                    .;
        sty     $3C                             ; A796 84 3C                    .<
LA798:  jsr     LA21D                           ; A798 20 1D A2                  ..
        lda     ($3B),y                         ; A79B B1 3B                    .;
        sta     $2E                             ; A79D 85 2E                    ..
        ldy     $3B                             ; A79F A4 3B                    .;
        iny                                     ; A7A1 C8                       .
        tya                                     ; A7A2 98                       .
        bne     LA7A7                           ; A7A3 D0 02                    ..
        inc     $3C                             ; A7A5 E6 3C                    .<
LA7A7:  sta     $3B                             ; A7A7 85 3B                    .;
        ldy     $3C                             ; A7A9 A4 3C                    .<
LA7AB:  jsr     L9F2E_PROBABLY_JSR_TO_INDIRECT_STUFF                           ; A7AB 20 2E 9F                  ..
        lda     $3B                             ; A7AE A5 3B                    .;
        ldy     $3C                             ; A7B0 A4 3C                    .<
        clc                                     ; A7B2 18                       .
        adc     #$08                            ; A7B3 69 08                    i.
        BCC     $a7b8
        iny
        sta     $3B                             ; A7B8 85 3B                    .;
        sty     $3C                             ; A7BA 84 3C                    .<
        jsr     L9F3C_JSR_INDIRECT_STUFF_AND_JMP_L9CBE_X12                           ; A7BC 20 3C 9F                  <.
        lda     #$1D                            ; A7BF A9 1D                    ..
        ldy     #$00                            ; A7C1 A0 00                    ..
        dec     $2E                             ; A7C3 C6 2E                    ..
        bne     LA7AB                           ; A7C5 D0 E4                    ..
        rts                                     ; A7C7 60                       `
; ----------------------------------------------------------------------------
LA7C8:  tya                                     ; A7C8 98                       .
        and     $44,x                           ; A7C9 35 44                    5D
        ply                                     ; A7CB 7A                       z
        brk                                     ; A7CC 00                       .
        brk                                     ; A7CD 00                       .
        brk                                     ; A7CE 00                       .
        brk                                     ; A7CF 00                       .
LA7D0:  pla                                     ; A7D0 68                       h
        plp                                     ; A7D1 28                       (
        lda     ($46),y                         ; A7D2 B1 46                    .F
        brk                                     ; A7D4 00                       .
        brk                                     ; A7D5 00                       .
        brk                                     ; A7D6 00                       .
        brk                                     ; A7D7 00                       .
LA7D8_X5C:  jsr     LA29A_X36                           ; A7D8 20 9A A2                  ..
LA7DB_X3A:  bmi     LA81A                           ; A7DB 30 3D                    0=
        bne     LA805                           ; A7DD D0 26                    .&
        lda     VIA1_T1CL                       ; A7DF AD 04 F8                 ...
        sta     $26                             ; A7E2 85 26                    .&
        lda     VIA1_T1CH                       ; A7E4 AD 05 F8                 ...
        sta     $2B                             ; A7E7 85 2B                    .+
        lda     VIA1_T2CL                       ; A7E9 AD 08 F8                 ...
        sta     $2A                             ; A7EC 85 2A                    .*
        lda     VIA2_T2CL                       ; A7EE AD 88 F8                 ...
        sta     $29                             ; A7F1 85 29                    .)
        lda     VIA2_T1CL                       ; A7F3 AD 84 F8                 ...
        sta     $28                             ; A7F6 85 28                    .(
        lda     VIA1_T2CL                       ; A7F8 AD 08 F8                 ...
        sta     $27                             ; A7FB 85 27                    .'
        lda     VIA1_T2CH                       ; A7FD AD 09 F8                 ...
        sta     $2C                             ; A800 85 2C                    .,
        jmp     LA832                           ; A802 4C 32 A8                 L2.
; ----------------------------------------------------------------------------
LA805:  lda     #<MEM_03AC                         ; A805 A9 AC                    ..
        ldy     #>MEM_03AC                         ; A807 A0 03                    ..
        jsr     LA1DD_X42_INDIRECT_STUFF_LOAD       ; A809 20 DD A1                  ..
        lda     #<LA7C8                         ; A80C A9 C8                    ..
        ldy     #>LA7C8                         ; A80E A0 A7                    ..
        jsr     L9F2E_PROBABLY_JSR_TO_INDIRECT_STUFF    ; A810 20 2E 9F                  ..
        lda     #<LA7D0                         ; A813 A9 D0                    ..
        ldy     #>LA7D0                         ; A815 A0 A7                    ..
        jsr     L9F3C_JSR_INDIRECT_STUFF_AND_JMP_L9CBE_X12  ; A817 20 3C 9F                  <.
LA81A:  ldx     $2C                             ; A81A A6 2C                    .,
        lda     $26                             ; A81C A5 26                    .&
        sta     $2C                             ; A81E 85 2C                    .,
        stx     $26                             ; A820 86 26                    .&
        ldx     $2A                             ; A822 A6 2A                    .*
        lda     $29                             ; A824 A5 29                    .)
        sta     $2A                             ; A826 85 2A                    .*
        stx     $29                             ; A828 86 29                    .)
        ldx     $27                             ; A82A A6 27                    .'
        lda     $2B                             ; A82C A5 2B                    .+
        sta     $27                             ; A82E 85 27                    .'
        stx     $2B                             ; A830 86 2B                    .+
LA832:  lda     #$00                            ; A832 A9 00                    ..
        sta     $2D                             ; A834 85 2D                    .-
        lda     $25                             ; A836 A5 25                    .%
        sta     $3A                             ; A838 85 3A                    .:
        lda     #$80                            ; A83A A9 80                    ..
        sta     $25                             ; A83C 85 25                    .%
        jsr     L9D40                           ; A83E 20 40 9D                  @.
        ldx     #>LAC03                         ; A841 A2 AC                    ..
        ldy     #<LAC03                         ; A843 A0 03                    ..
LA845:  jmp     LA227                           ; A845 4C 27 A2                 L'.
; ----------------------------------------------------------------------------
LA848_X2A:  lda     #<LA8C4                         ; A848 A9 C4                    ..
        ldy     #>LA8C4                          ; A84A A0 A8                    ..
        jsr     L9F3C_JSR_INDIRECT_STUFF_AND_JMP_L9CBE_X12  ; A84C 20 3C 9F                  <.
LA84F_X2C:  jsr     LA27B_X48                           ; A84F 20 7B A2                  {.
        lda     #<LA8CC                            ; A852 A9 CC                    ..
        ldy     #>LA8CC                            ; A854 A0 A8                    ..
        ldx     $38                             ; A856 A6 38                    .8
        jsr     LA0EE                           ; A858 20 EE A0                  ..
        jsr     LA27B_X48                           ; A85B 20 7B A2                  {.
        jsr     LA369_X1E                           ; A85E 20 69 A3                  i.
        lda     #$00                            ; A861 A9 00                    ..
        sta     $39                             ; A863 85 39                    .9
        jsr     L9CA7_X0E                           ; A865 20 A7 9C                  ..
        lda     #<LA8D4                         ; A868 A9 D4                    ..
        ldy     #>LA8D4
        jsr     L9F48_JSR_INDIRECT_STUFF_AND_JMP_L9CA7_X0E                           ; A86C 20 48 9F                  H.
        lda     $2D                             ; A86F A5 2D                    .-
        pha                                     ; A871 48                       H
        bpl     LA881                           ; A872 10 0D                    ..
        jsr     L9F38                           ; A874 20 38 9F                  8.
        lda     $2D                             ; A877 A5 2D                    .-
        bmi     LA884                           ; A879 30 09                    0.
        lda     $04                             ; A87B A5 04                    ..
        eor     #$FF                            ; A87D 49 FF                    I.
        sta     $04                             ; A87F 85 04                    ..
LA881:  jsr     LA6A7_X22                           ; A881 20 A7 A6                  ..
LA884:  lda     #<LA8D4                         ; A884 A9 D4                    ..
        ldy     #>LA8D4                         ; A886 A0 A8                    ..
        jsr     L9F3C_JSR_INDIRECT_STUFF_AND_JMP_L9CBE_X12                           ; A888 20 3C 9F                  <.
        pla                                     ; A88B 68                       h
        bpl     LA891                           ; A88C 10 03                    ..
        jsr     LA6A7_X22                           ; A88E 20 A7 A6                  ..
LA891:  lda     #<LA8DC                         ; A891 A9 DC                    ..
        ldy     #>LA8DC                         ; A893 A0 A8                    ..
        jmp     LA77E_UNKNOWN_OTHER_INDIRECT_STUFF                           ; A895 4C 7E A7                 L~.
; ----------------------------------------------------------------------------
LA898_X2E:  jsr     LA220                           ; A898 20 20 A2                   .
        lda     #$00                            ; A89B A9 00                    ..
        sta     $04                             ; A89D 85 04                    ..
        jsr     LA84F_X2C                           ; A89F 20 4F A8                  O.
        ldx     #$41                            ; A8A2 A2 41                    .A
        ldy     #$00                            ; A8A4 A0 00                    ..
        jsr     LA845                           ; A8A6 20 45 A8                  E.
        lda     #<MEM_0015                      ; A8A9 A9 15                    ..
        ldy     #>MEM_0015                      ; A8AB A0 00                    ..
        jsr     LA1DD_X42_INDIRECT_STUFF_LOAD   ; A8AD 20 DD A1                  ..
        lda     #$00                            ; A8B0 A9 00                    ..
        sta     $2D                             ; A8B2 85 2D                    .-
        lda     $04                             ; A8B4 A5 04                    ..
        jsr     LA8C0                           ; A8B6 20 C0 A8                  ..
        lda     #$41                            ; A8B9 A9 41                    .A
        ldy     #$00                            ; A8BB A0 00                    ..
        jmp     LA0F6_X18                           ; A8BD 4C F6 A0                 L..
; ----------------------------------------------------------------------------
LA8C0:  pha                                     ; A8C0 48                       H
        jmp     LA881                           ; A8C1 4C 81 A8                 L..
; ----------------------------------------------------------------------------
;TODO probably data
LA8C4:  sta     ($49,x)                         ; A8C4 81 49                    .I
        bbr0    $DA,$A86B                       ; A8C6 0F DA A2                 ...
        and     ($68,x)                         ; A8C9 21 68                    !h
        iny                                     ; A8CB C8                       .

LA8CC:  .byte   $83                             ; A8CC 83                       .
        eor     #$0F                            ; A8CD 49 0F                    I.
        phx                                     ; A8CF DA                       .
        ldx     #$21                            ; A8D0 A2 21                    .!
        pla                                     ; A8D2 68                       h
        iny                                     ; A8D3 C8                       .
LA8D4:  bbr7    $00,LA8D7                       ; A8D4 7F 00 00                 ...
LA8D7:  brk                                     ; A8D7 00                       .
        brk                                     ; A8D8 00                       .
        brk                                     ; A8D9 00                       .
        brk                                     ; A8DA 00                       .
        brk                                     ; A8DB 00                       .
LA8DC:  ora     #$7A                            ; A8DC 09 7A                    .z
        cmp     $20                             ; A8DE C5 20                    .
        and     ($08,x)                         ; A8E0 21 08                    !.
        .byte   $FC                             ; A8E2 FC                       .
        tax                                     ; A8E3 AA                       .
        trb     $7D                             ; A8E4 14 7D                    .}
        eor     $76,x                           ; A8E6 55 76                    Uv
        ora     $C957,y                         ; A8E8 19 57 C9                 .W.
        txs                                     ; A8EB 9A                       .
        ldy     LB780                           ; A8EC AC 80 B7                 ...
        dec     $DC,x                           ; A8EF D6 DC                    ..
        sed                                     ; A8F1 F8                       .
        tax                                     ; A8F2 AA                       .
        lda     L82FE,y                         ; A8F3 B9 FE 82                 ...
        stz     $7A,x                           ; A8F6 74 7A                    tz
        inc     a                               ; A8F8 1A                       .
        pla                                     ; A8F9 68                       h
        .byte   $0C                             ; A8FA 0C                       .
LA8FB:  ror     a                               ; A8FB 6A                       j
        .byte   $F4                             ; A8FC F4                       .
        sty     $F1                             ; A8FD 84 F1                    ..
        .byte   $83                             ; A8FF 83                       .
        smb2    $EF                             ; A900 A7 EF                    ..
        .byte   $44                             ; A902 44                       D
        sec                                     ; A903 38                       8
        .byte   $DC                             ; A904 DC                       .
        stx     $28                             ; A905 86 28                    .(
        bit     $431A,x                         ; A907 3C 1A 43                 <.C
        smb7    $3B                             ; A90A F7 3B                    .;
        sed                                     ; A90C F8                       .
        smb0    $99                             ; A90D 87 99                    ..
        adc     #$66                            ; A90F 69 66                    if
        .byte   $73                             ; A911 73                       s
        ora     $EC,x                           ; A912 15 EC                    ..
        .byte   $23                             ; A914 23                       #
        smb0    $23                             ; A915 87 23                    .#
        and     $E3,x                           ; A917 35 E3                    5.
        .byte   $3B                             ; A919 3B                       ;
        lda     a:$57                           ; A91A AD 57 00                 .W.
        stx     $A5                             ; A91D 86 A5                    ..
        eor     $31E7,x                         ; A91F 5D E7 31                 ].1
        and     L90F2                           ; A922 2D F2 90                 -..
        .byte   $83                             ; A925 83                       .
        eor     #$0F                            ; A926 49 0F                    I.
        phx                                     ; A928 DA                       .
        ldx     #$21                            ; A929 A2 21                    .!
        pla                                     ; A92B 68                       h
        iny                                     ; A92C C8                       .
LA92D_X30:  lda     $2D                             ; A92D A5 2D                    .-
        pha                                     ; A92F 48                       H
        bpl     LA935                           ; A930 10 03                    ..
        jsr     LA6A7_X22                           ; A932 20 A7 A6                  ..
LA935:  lda     $25                             ; A935 A5 25                    .%
        pha                                     ; A937 48                       H
        cmp     #$81                            ; A938 C9 81                    ..
        bcc     LA943                           ; A93A 90 07                    ..
        lda     #<L9E7F                         ; A93C A9 7F                    ..
        ldy     #>L9E7F                         ; A93E A0 9E                    ..
        jsr     L9F54_JSR_INDIRECT_STUFF_AND_JMP_LA0F9_X1A  ; A940 20 54 9F                  T.
LA943:  lda     #<LA95D                         ; A943 A9 5D                    .]
        ldy     #>LA95D                         ; A945 A0 A9                    ..
        jsr     LA77E_UNKNOWN_OTHER_INDIRECT_STUFF  ; A947 20 7E A7                  ~.
        pla                                     ; A94A 68                       h
        cmp     #$81                            ; A94B C9 81                    ..
        bcc     LA956                           ; A94D 90 07                    ..
        lda     #<LA8C4                         ; A94F A9 C4                    ..
        ldy     #>LA8C4                         ; A951 A0 A8                    ..
        jsr     L9F48_JSR_INDIRECT_STUFF_AND_JMP_L9CA7_X0E  ; A953 20 48 9F                  H.
LA956:  pla                                     ; A956 68                       h
        bpl     LA95C                           ; A957 10 03                    ..
        jmp     LA6A7_X22                           ; A959 4C A7 A6                 L..
; ----------------------------------------------------------------------------
LA95C:  rts                                     ; A95C 60                       `
; ----------------------------------------------------------------------------
;TODO probably data
LA95D:  tsb     $6275                           ; A95D 0C 75 62                 .ub
        inc     $07BA,x                         ; A960 FE BA 07                 ...
        trb     $3A                             ; A963 14 3A                    .:
        tay                                     ; A965 A8                       .
        sei                                     ; A966 78                       x
        dec     $D8,x                           ; A967 D6 D8                    ..
        dec     $5116                           ; A969 CE 16 51                 ..Q
        eor     $7A14                           ; A96C 4D 14 7A                 M.z
        rol     $7DD1,x                         ; A96F 3E D1 7D                 >.}
        .byte   $BD                             ; A972 BD                       .
        .byte   $4C                             ; A973 4C                       L
LA974:  rol     $88,x                           ; A974 36 88                    6.
        .byte   $7B                             ; A976 7B                       {
        .byte   $D7                             ; A977 D7                       .
LA978:  cpy     $23                             ; A978 C4 23                    .#
        .byte   $CB                             ; A97A CB                       .
        ora     ($6B,x)                         ; A97B 01 6B                    .k
        .byte   $9C                             ; A97D 9C                       .
LA97E:  jmp     ($1734,x)                       ; A97E 7C 34 17                 |4.
        asl     a                               ; A981 0A                       .
        dec     a                               ; A982 3A                       :
        .byte   $DC                             ; A983 DC                       .
        eor     ($78,x)                         ; A984 41 78                    Ax
        ;TODO probably data
        jmp     ($81F7,x)                       ; A986 7C F7 81                 |..
        .byte   $A3                             ; A989 A3                       .
        cmp     ($36,x)                         ; A98A C1 36                    .6
        rmb2    $00                             ; A98C 27 00                    '.
        adc     $AE19,x                         ; A98E 7D 19 AE                 }..
        adc     ($16,x)                         ; A991 61 16                    a.
        nop                                     ; A993 EA                       .
        tsx                                     ; A994 BA                       .
        eor     $B97D                           ; A995 4D 7D B9                 M}.
        rts                                     ; A998 60                       `
; ----------------------------------------------------------------------------
;TODO probably data
        bbs0    $78,LA9F9                       ; A999 8F 78 5D                 .x]
        .byte   $0B                             ; A99C 0B                       .
        tsx                                     ; A99D BA                       .
        adc     $7263,x                         ; A99E 7D 63 72                 }cr
        ora     ($44)                           ; A9A1 12 44                    .D
        .byte   $A1                             ; A9A3 A1                       .
LA9A4:  sta     $B4                             ; A9A4 85 B4                    ..
        ror     $4792,x                         ; A9A6 7E 92 47                 ~.G
        .byte   $FB                             ; A9A9 FB                       .
        .byte   $62                             ; A9AA 62                       b
        asl     $0D,x                           ; A9AB 16 0D                    ..
        .byte   $43                             ; A9AD 43                       C
        ror     LCC4C,x                         ; A9AE 7E 4C CC                 ~L.
        bbs3    $F0,LA974                       ; A9B1 BF F0 C0                 ...
        ply                                     ; A9B4 7A                       z
        stz     $7F                             ; A9B5 64 7F                    d.
        tax                                     ; A9B7 AA                       .
        tax                                     ; A9B8 AA                       .
        tax                                     ; A9B9 AA                       .
LA9BA:  stx     $B07D                           ; A9BA 8E 7D B0                 .}.
        cpy     #$80                            ; A9BD C0 80                    ..
        .byte   $7F                             ; A9BF 7F                       .
        .byte   $FF                             ; A9C0 FF                       .
        .byte   $ff, $ff, $f5, $b9, $2c
; ----------------------------------------------------------------------------
LA9C6_X76:  jsr     LA02B_X3C
; ----------------------------------------------------------------------------
LA9C9_X78:  jsr     L9BF6_X00
        lda     $2C                             ; A9CC A5 2C                    .,
        sta     $00                             ; A9CE 85 00                    ..
        lda     $2B                             ; A9D0 A5 2B                    .+
        sta     $01                             ; A9D2 85 01                    ..
        jsr     LA26B_X46                           ; A9D4 20 6B A2                  k.
        jsr     L9BF6_X00                           ; A9D7 20 F6 9B                  ..
        lda     $2C                             ; A9DA A5 2C                    .,
        eor     $00                             ; A9DC 45 00                    E.
        tay                                     ; A9DE A8                       .
        lda     $2B                             ; A9DF A5 2B                    .+
LA9E1:  eor     $01                             ; A9E1 45 01                    E.
        jmp     L9BDA_X02                           ; A9E3 4C DA 9B                 L..
; ----------------------------------------------------------------------------
LA9E6:  php                                     ; A9E6 08                       .
        sty     BAD                           ; A9E7 8C A0 03                 ...
        cpx     #$50                            ; A9EA E0 50                    .P
        bcs     LAA17                           ; A9EC B0 29                    .)
        stx     V1541_FNLEN                     ; A9EE 8E 9F 03                 ...
        tax                                     ; A9F1 AA                       .
        and     #$0F                            ; A9F2 29 0F                    ).
        sta     SXREG                           ; A9F4 8D 9D 03                 ...
        txa                                     ; A9F7 8A                       .
        lsr     a                               ; A9F8 4A                       J
LA9F9:  lsr     a                               ; A9F9 4A                       J
        lsr     a                               ; A9FA 4A                       J
        lsr     a                               ; A9FB 4A                       J
        inc     a                               ; A9FC 1A                       .
        sta     V1541_BYTE_TO_WRITE                           ; A9FD 8D 9E 03                 ...
        cld                                     ; AA00 D8                       .
        lda     #$FF                            ; AA01 A9 FF                    ..
        sta     $ED                             ; AA03 85 ED                    ..
        lda     VidMemHi                        ; AA05 A5 A0                    ..
        clc                                     ; AA07 18                       .
        adc     #$07                            ; AA08 69 07                    i.
        sta     $EE                             ; AA0A 85 EE                    ..
LAA0C:  lda     SXREG                           ; AA0C AD 9D 03                 ...
        inc     SXREG                           ; AA0F EE 9D 03                 ...
        cmp     V1541_BYTE_TO_WRITE                           ; AA12 CD 9E 03                 ...
        bcc     LAA19                           ; AA15 90 02                    ..
LAA17:  plp                                     ; AA17 28                       (
        rts                                     ; AA18 60                       `
; ----------------------------------------------------------------------------
LAA19:  stz     $EB                             ; AA19 64 EB                    d.
        lsr     a                               ; AA1B 4A                       J
        ror     $EB                             ; AA1C 66 EB                    f.
        adc     VidMemHi                        ; AA1E 65 A0                    e.
        sta     $EC                             ; AA20 85 EC                    ..
        ldy     V1541_FNLEN                           ; AA22 AC 9F 03                 ...
LAA25:  plp                                     ; AA25 28                       (
        php                                     ; AA26 08                       .
        lda     ($ED)                           ; AA27 B2 ED                    ..
        bcs     LAA31                           ; AA29 B0 06                    ..
        lda     ($EB),y                         ; AA2B B1 EB                    ..
        sta     ($ED)                           ; AA2D 92 ED                    ..
        lda     #$20                            ; AA2F A9 20                    .
LAA31:  sta     ($EB),y                         ; AA31 91 EB                    ..
        lda     $ED                             ; AA33 A5 ED                    ..
        asl     a                               ; AA35 0A                       .
        eor     #$A2                            ; AA36 49 A2                    I.
        bne     LAA47                           ; AA38 D0 0D                    ..
        ror     a                               ; AA3A 6A                       j
        sta     $ED                             ; AA3B 85 ED                    ..
        bmi     LAA47                           ; AA3D 30 08                    0.
        dec     $EE                             ; AA3F C6 EE                    ..
        lda     $EE                             ; AA41 A5 EE                    ..
        cmp     VidMemHi                        ; AA43 C5 A0                    ..
        bcc     LAA17                           ; AA45 90 D0                    ..
LAA47:  dec     $ED                             ; AA47 C6 ED                    ..
        dey                                     ; AA49 88                       .
        bmi     LAA0C                           ; AA4A 30 C0                    0.
        cpy     BAD                           ; AA4C CC A0 03                 ...
        bcs     LAA25                           ; AA4F B0 D4                    ..
        bra     LAA0C                           ; AA51 80 B9                    ..
; ----------------------------------------------------------------------------
LAA53:  stx     V1541_FILE_MODE                 ; AA53 8E A3 03                 ...
        sty     $03A7                           ; AA56 8C A7 03                 ...
        sty     $0357                           ; AA59 8C 57 03                 .W.
        pha                                     ; AA5C 48                       H
        and     #$07                            ; AA5D 29 07                    ).
        sta     $03A2                           ; AA5F 8D A2 03                 ...
        pla                                     ; AA62 68                       h
        eor     #$F8                            ; AA63 49 F8                    I.
        bit     #$F8                            ; AA65 89 F8                    ..
        beq     LAA7F                           ; AA67 F0 16                    ..
LAA6A := *+1
        ora     #$07
        sta     MON_MMU_MODE
        ldy     #$00                            ; AA6E A0 00                    ..
        ldx     #$00                            ; AA70 A2 00                    ..
LAA72:  jsr     GO_APPL_LOAD_GO_KERN            ; AA72 20 53 03                  S.
        beq     LAA81                           ; AA75 F0 0A                    ..
        cmp     #$0D                            ; AA77 C9 0D                    ..
        bne     LAA7C                           ; AA79 D0 01                    ..
        inx                                     ; AA7B E8                       .
LAA7C:  iny                                     ; AA7C C8                       .
        bne     LAA72                           ; AA7D D0 F3                    ..
LAA7F:  sec                                     ; AA7F 38                       8
        rts                                     ; AA80 60                       `

LAA81:  cpx     #$0F                            ; AA81 E0 0F                    ..
        bcs     LAA7F                           ; AA83 B0 FA                    ..
        stx     V1541_FILE_TYPE                           ; AA85 8E A4 03                 ...
        clc                                     ; AA88 18                       .
        jsr     LAB90                           ; AA89 20 90 AB                  ..
LAA8C:  ldx     V1541_FILE_TYPE                           ; AA8C AE A4 03                 ...
        lda     V1541_FILE_MODE                           ; AA8F AD A3 03                 ...
        bmi     LAA9C                           ; AA92 30 08                    0.
        cpx     V1541_FILE_MODE                           ; AA94 EC A3 03                 ...
        bcs     LAA9D                           ; AA97 B0 04                    ..
        lda     #$00                            ; AA99 A9 00                    ..
        .byte   $24  ;skip 1 byte               ; AA9B 24                       $
LAA9C:  txa                                     ; AA9C 8A                       .
LAA9D:  sta     V1541_FILE_MODE                           ; AA9D 8D A3 03                 ...
        jsr     LAAF7                           ; AAA0 20 F7 AA                  ..
        jsr     LB6DF_GET_KEY_BLOCKING          ; AAA3 20 DF B6                  ..
        cmp     #$91 ;UP                        ; AAA6 C9 91                    ..
        bne     LAAAD                           ; AAA8 D0 03                    ..
        inc     V1541_FILE_MODE                           ; AAAA EE A3 03                 ...
LAAAD:  cmp     #$11 ;DOWN                      ; AAAD C9 11                    ..
        bne     LAAB4                           ; AAAF D0 03                    ..
        dec     V1541_FILE_MODE                           ; AAB1 CE A3 03                 ...
LAAB4:  tax                                     ; AAB4 AA                       .
        lda     #$80                            ; AAB5 A9 80                    ..
        cpx     #$9D ;LEFT                      ; AAB7 E0 9D                    ..
        beq     LAAD9                           ; AAB9 F0 1E                    ..
        lsr     a                               ; AABB 4A                       J
        cpx     #$1D ;RIGHT                     ; AABC E0 1D                    ..
        beq     LAAD9                           ; AABE F0 19                    ..
        lsr     a                               ; AAC0 4A                       J
        cpx     #$0D ;RETURN                    ; AAC1 E0 0D                    ..
        beq     LAAD9                           ; AAC3 F0 14                    ..
        cpx     #$85 ;F1                        ; AAC5 E0 85                    ..
        bcc     LAA8C                           ; AAC7 90 C3                    ..
        cpx     #$8D ;F8 + 1                    ; AAC9 E0 8D                    ..
        bcs     LAA8C                           ; AACB B0 BF                    ..
        lda     LAA6A,x                         ; AACD BD 6A AA                 .j.
        cmp     $03A2                           ; AAD0 CD A2 03                 ...
        beq     LAAD7                           ; AAD3 F0 02                    ..
        ora     #$18                            ; AAD5 09 18                    ..
LAAD7:  eor     #$08                            ; AAD7 49 08                    I.
LAAD9:  and     MON_MMU_MODE                    ; AAD9 2D A1 03                 -..
        bit     #$F8                            ; AADC 89 F8                    ..
        beq     LAA8C                           ; AADE F0 AC                    ..
        sta     MON_MMU_MODE                    ; AAE0 8D A1 03                 ...
        sec                                     ; AAE3 38                       8
        jsr     LAB90                           ; AAE4 20 90 AB                  ..
        lda     MON_MMU_MODE                    ; AAE7 AD A1 03                 ...
        ldx     V1541_FILE_MODE                 ; AAEA AE A3 03                 ...
        clc                                     ; AAED 18                       .
        rts                                     ; AAEE 60                       `
; ----------------------------------------------------------------------------
        ;TODO probably data
        brk                                     ; AAEF 00                       .
        .byte   $02                             ; AAF0 02                       .
        tsb     $06                             ; AAF1 04 06                    ..
        ora     ($03,x)                         ; AAF3 01 03                    ..
        ora     $07                             ; AAF5 05 07                    ..

LAAF7:  stz     $03A5                           ; AAF7 9C A5 03                 ...
        lda     #$FF                            ; AAFA A9 FF                    ..
        sta     $03A6                           ; AAFC 8D A6 03                 ...
        ldy     $03A7                           ; AAFF AC A7 03                 ...
        sty     $0357                           ; AB02 8C 57 03                 .W.
LAB05:  jsr     LAB57                           ; AB05 20 57 AB                  W.
        lda     #$A5                            ; AB08 A9 A5                    ..
        jsr     LAB50                           ; AB0A 20 50 AB                  P.
        bne     LAB11                           ; AB0D D0 02                    ..
        lda     #$20                            ; AB0F A9 20                    .
LAB11:  clc                                     ; AB11 18                       .
        jsr     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT                           ; AB12 20 F9 B6                  ..
        inc     $03A6                           ; AB15 EE A6 03                 ...
        ldy     $03A6                           ; AB18 AC A6 03                 ...
        jsr     GO_APPL_LOAD_GO_KERN            ; AB1B 20 53 03                  S.
        beq     LAB24                           ; AB1E F0 04                    ..
        cmp     #$0D                            ; AB20 C9 0D                    ..
        bne     LAB11                           ; AB22 D0 ED                    ..
LAB24:  lda     #$0D                            ; AB24 A9 0D                    ..
        clc                                     ; AB26 18                       .
        jsr     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT                           ; AB27 20 F9 B6                  ..
        lda     #$67                            ; AB2A A9 67                    .g
        jsr     LAB50                           ; AB2C 20 50 AB                  P.
        bne     LAB33                           ; AB2F D0 02                    ..
        lda     #$A0                            ; AB31 A9 A0                    ..
LAB33:  sta     ($BD)                           ; AB33 92 BD                    ..
        lda     $03A5                           ; AB35 AD A5 03                 ...
        inc     $03A5                           ; AB38 EE A5 03                 ...
        cmp     V1541_FILE_TYPE                 ; AB3B CD A4 03                 ...
        bcc     LAB05                           ; AB3E 90 C5                    ..
        cmp     #$0E                            ; AB40 C9 0E                    ..
        bcs     LAB50                           ; AB42 B0 0C                    ..
        jsr     LAB57                           ; AB44 20 57 AB                  W.
        ldy     #$08                            ; AB47 A0 08                    ..
        lda     #$64                            ; AB49 A9 64                    .d
LAB4B:  sta     ($BD),y                         ; AB4B 91 BD                    ..
        dey                                     ; AB4D 88                       .
        bpl     LAB4B                           ; AB4E 10 FB                    ..
LAB50:  ldx     $03A5                           ; AB50 AE A5 03                 ...
        cpx     V1541_FILE_MODE                 ; AB53 EC A3 03                 ...
        rts                                     ; AB56 60                       `
; ----------------------------------------------------------------------------
LAB57:  ldx     $03A2                           ; AB57 AE A2 03                 ...
        ldy     LAB80,x                         ; AB5A BC 80 AB                 ...
        lda     #$08                            ; AB5D A9 08                    ..
        jsr     LAB50                           ; AB5F 20 50 AB                  P.
        bne     LAB66                           ; AB62 D0 02                    ..
        eor     #$80                            ; AB64 49 80                    I.
LAB66:  pha                                     ; AB66 48                       H
        lda     LAB70,x                         ; AB67 BD 70 AB                 .p.
        tax                                     ; AB6A AA                       .
        pla                                     ; AB6B 68                       h
        sec                                     ; AB6C 38                       8
        jmp     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT                           ; AB6D 4C F9 B6                 L..
; ----------------------------------------------------------------------------
LAB70:  .byte   $0E                             ; AB70 0E                       .
LAB71:  .byte   $0D,$0C,$0B,$0A,$09,$08,$07,$06 ; AB71 0D 0C 0B 0A 09 08 07 06  ........
        .byte   $05,$04,$03,$02,$01,$00,$00     ; AB79 05 04 03 02 01 00 00     .......
LAB80:  .byte   $00,$0A,$14,$1E,$28,$32,$3C,$46 ; AB80 00 0A 14 1E 28 32 3C 46  ....(2<F
LAB88:  .byte   $09,$13,$1D,$27,$31,$3B,$45,$4F ; AB88 09 13 1D 27 31 3B 45 4F  ...'1;EO
; ----------------------------------------------------------------------------
LAB90:  php
        ldx     #$04
        bcc     LAB97
        ldx     #$06
LAB97:  clc
        jsr     LD230_JMP_LD233_PLUS_X  ;-> LD297_X_06
        ldx     V1541_FILE_TYPE
        lda     LAB71,x
        eor     #$E0
        pha
        ldx     $03A2
        ldy     LAB80,x
        lda     LAB88,x
        tax
        pla
        plp
        jmp     LA9E6
; ----------------------------------------------------------------------------
KR_ShowChar_:
        phx
        phy
        bit     $0384
        bpl     LABC1
        bvc     LABC4
        jsr     L8948
        bra     LABC4
LABC1:  jsr     LABC8
LABC4:  ply
        plx
        clc
        rts
; ----------------------------------------------------------------------------
LABC8:  bit     $0382
        bpl     LABD6
        stz     $0382
        jsr     ESC_O_CANCEL_MODES
        jsr     LAEA6
LABD6:  pha
LABD7:  php
        pla
        bit     #$04
        bne     LABED
        lda     MEM_00AA
        and     $036D
        and     #$02
        beq     LABED
        ldx     #$02
        jsr     WaitXticks_
        bra     LABD7
LABED:  pla
        ldx     LSTCHR        ;X = last char
        sta     LSTCHR        ;Store this char as the last one for next time

        cmp     #$0D          ;Char = Return?
        beq     LAC2F
        cmp     #$8D          ;Char = Shift-Return?
        beq     LAC2F

        cpx     #$1B          ;Last char = ESC?
        bne     LAC03
        jmp     LB10E_ESC
; ----------------------------------------------------------------------------
LAC03:  cmp     #$1B          ;Char = Escape?
        bne     LAC08
        rts
; ----------------------------------------------------------------------------
LAC08:  bit     MEM_00AA
        bpl     LAC24
        ldy     INSRT
        beq     LAC19
        cmp     #$94          ;Char = Insert?
        beq     LAC2F
        dec     INSRT
        jmp     LAC3A
; ----------------------------------------------------------------------------
LAC19:  jsr     LB08E
        ldy     QTSW
        beq     LAC24
        cmp     #$14          ;Char = Delete?
        bne     LAC3A
LAC24:  cmp     #$13          ;Char = Home?
        bne     LAC2F
        cpx     #$13          ;Last char = Home?
        bne     LAC2F
        jmp     LAE5B
; ----------------------------------------------------------------------------
LAC2F:  bit     #$20
        bne     LAC3A
        bit     #$40
        bne     LAC3A
        jmp     DO_CTRL_CODE
; ----------------------------------------------------------------------------
LAC3A:  jsr     LB09B
        ldx     REVERSE
        beq     LAC4D_RVS_OFF

        ;Reverse is on
        ora     #$80

LAC4D_RVS_OFF:
        ldx     INSFLG
        beq     LAC4D_AUTO_INSRT_OFF

        ;Auto-insert (ESC-A) is on
        pha
        jsr     CODE_94_INSERT
        pla

LAC4D_AUTO_INSRT_OFF:
        jsr     PutCharAtCursorXY
LAC50:  ldx     CursorX
        cpx     WIN_BTM_RGHT_X
        beq     LAC59_EOL_REACHED
        inc     CursorX
LAC58_RTS:
        rts

LAC59_EOL_REACHED:
        lda     MEM_00AA
        bit     #$04
        beq     JMP_CTRL_1D_CRSR_RIGHT
        bit     #$20
        beq     LAC58_RTS
        ldy     CursorY
        jsr     LB059
        bcs     JMP_CTRL_1D_CRSR_RIGHT
        ldx     WIN_TOP_LEFT_X
        stx     CursorX
        ldy     CursorY
        sec
        jsr     LB06F
        ldy     CursorY
        cpy     WIN_BTM_RGHT_Y
        bne     LAC8E
        ldy     LSXP
        bmi     LAC88
        cpy     WIN_TOP_LEFT_Y
        beq     LAC88
        dec     LSXP
        bra     LAC8B
LAC88:  jsr     LB393_SET_LSXP_FF_SET_CARRY
LAC8B:  jmp     SCROLL_WIN_UP

LAC8E:  inc     CursorY
        jmp     SCROLL_WIN_DOWN

JMP_CTRL_1D_CRSR_RIGHT:
        jmp     CTRL_1D_CRSR_RIGHT
; ----------------------------------------------------------------------------
CTRL_CODES_AND_HANDLERS:
        .byte   $07 ;CHR$(7) Bell
        .addr   CODE_07_BELL

        .byte   $09 ;CHR$(9) Tab
        .addr   CODE_09_TAB

        .byte   $0A ;CHR$(10) Linefeed
        .addr   CODE_0A_LINEFEED

        .byte   $0D ;CHR$(13) Carriage Return
        .addr   CODE_0D_RETURN

        .byte   $0E ;CHR$(14) Lowercase Mode
        .addr   CODE_14_LOWERCASE

        .byte   $11 ;CHR$(17) Cursor Down
        .addr   CODE_11_CRSR_DOWN

        .byte   $12 ;CHR$(18) Reverse On
        .addr   CODE_12_RVS_ON

        .byte   $13 ;CHR$(19) Home
        .addr   CODE_13_HOME

        .byte   $14 ;CHR$(20) Delete
        .addr   CODE_14_DELETE

        .byte   $18 ;CHR$(24) Set or Clear Tab
        .addr   CODE_18_CTRL_X

        .byte   $19 ;CHR$(25) CTRL-Y Lock (Disables Shift-Commodore)
        .addr   CODE_19_CTRL_Y_LOCK

        .byte   $1A ;CHR$(26) CTRL-Z Unlock (Enables Shift-Commodore)
        .addr   CODE_1A_CTRL_Z_UNLOCK

        .byte   $1D ;CHR$(29) Cursor Right
        .addr   CTRL_1D_CRSR_RIGHT

        .byte   $8D ;CHR$(141) Shift-Return
        .addr   CODE_8D_SHIFT_RETURN

        .byte   $8E ;CHR$(142) Uppercase Mode
        .addr   CODE_8E_UPPERCASE

        .byte   $91 ;CHR$(145) Cursor Up
        .addr   CODE_91_CRSR_UP

        .byte   $92 ;CHR$(146) Reverse Off
        .addr   CODE_92_RVS_OFF

        .byte   $93 ;CHR$(147) Clear Screen
        .addr   CODE_93_CLR_SCR

        .byte   $94 ;CHR$(148) Insert
        .addr   CODE_94_INSERT

        .byte   $9D ;CHR$(157) Cursor Left
        .addr   CODE_9D_CRSR_LEFT
; ----------------------------------------------------------------------------
DO_CTRL_CODE:
        ldx     #$39
LACD4_LOOP:
        cmp     CTRL_CODES_AND_HANDLERS,x
        beq     JMP_TO_CTRL_CODE
        dex
        dex
        dex
        bpl     LACD4_LOOP
        rts
; ----------------------------------------------------------------------------
JMP_TO_CTRL_CODE:
        jmp     (CTRL_CODES_AND_HANDLERS+1,x)
; ----------------------------------------------------------------------------
;CHR$(25) CTRL-Y Lock
;Disables switching uppercase/lowercase mode when Shift-Commodore is pressed
CODE_19_CTRL_Y_LOCK:
        lda     #$40
        tsb     $036D
        rts
; ----------------------------------------------------------------------------
;CHR$(26) CTRL-Z Unlock
CODE_1A_CTRL_Z_UNLOCK:
;Enables switching uppercase/lowercase mode when Shift-Commodore is pressed
        lda     #$40
        trb     $036D
        rts
; ----------------------------------------------------------------------------
;Switch between uppercase and lowercase character sets
;Called from KBD_READ_MODIFIER_KEYS_DO_SWITCH_AND_CAPS
;when Shift + Commodore is pressed
SWITCH_CHARSET:
        bit     $036D
        bvs     CODE_19_CTRL_Y_LOCK ;If locked, branch to re-lock (does nothing) and return

        lda     #$01
        tsb     SETUP_LCD_A  ;Uppercase mode
        beq     JmpToSetUpLcdController
        ;Fall through to set lowercase mode

;CHR$(14) Lowercase Mode
CODE_14_LOWERCASE:
        lda     #$01
        trb     SETUP_LCD_A
        bne     JmpToSetUpLcdController
        rts
; ----------------------------------------------------------------------------
;CHR$(142) Uppercase Mode
CODE_8E_UPPERCASE:
        lda     #$01
        tsb     SETUP_LCD_A
        beq     JmpToSetUpLcdController
        rts
; ----------------------------------------------------------------------------
JmpToSetUpLcdController:
        sec
        jmp     LCDsetupGetOrSet
; ----------------------------------------------------------------------------
;CHR$(9) Tab
CODE_09_TAB:
        ldx     CursorX
        cpx     WIN_BTM_RGHT_X
        beq     LAD27
        lda     #$1D
        ldx     INSFLG
        beq     LAD1C
        lda     #$20
LAD1C:  jsr     LABD6
        jsr     CursorXtoTabMapIndex
        and     TABMAP,y
        beq     CODE_09_TAB
LAD27:  rts
; ----------------------------------------------------------------------------
CursorXtoTabMapIndex:
        lda     CursorX
        lsr     a
        lsr     a
        lsr     a
        tay
        lda     CursorX
        and     #$07
        tax
        lda     PowersOfTwo,x
        rts
; ----------------------------------------------------------------------------
;CHR$(24) CTRL-X
;Set or clear tab at current position
CODE_18_CTRL_X:
        jsr     CursorXtoTabMapIndex
        eor     TABMAP,y
        sta     TABMAP,y
        rts
; ----------------------------------------------------------------------------
;ESC-Y Set default tab stops (8 spaces)
ESC_Y_SET_DEFAULT_TABS:
        lda     #$80
        .byte   $2C

;ESC-Z Clear all tab stops
ESC_Z_CLEAR_ALL_TABS:
        lda     #$00
        ldx     #$09
LAD48:  sta     TABMAP,x
        dex
        bpl     LAD48
        rts
; ----------------------------------------------------------------------------
;CHR$(13) Carriage Return
;CHR$(141) Shift-Return
CODE_0D_RETURN:
CODE_8D_SHIFT_RETURN:
        lda     MEM_00AA
        lsr     a
        bcc     LAD65

        ;Check if the CTRL key is being pressed.  If so, and no interrupt
        ;is pending, pause before doing the linefeed.  This allows the user
        ;to slow down screen scrolling during LIST or DIRECTORY in BASIC.
        lda     #MOD_CTRL
        bit     MODKEY
        beq     LAD65 ;Branch to skip pause if CTRL is not down

        ;CTRL key being pressed
        php           ;Push processor status to test it
        pla           ;A = NV-BDIZC
        bit     #$04  ;Test Interrupt flag
        bne     LAD65 ;Branch to skip pause if interrupt flag is set

        ;Pause before doing the linefeed
        ldx     #$2D
        jsr     WaitXticks_

LAD65:  jsr     ESC_K_MOVE_TO_END_OF_LINE
        ldx     WIN_TOP_LEFT_X
        stx     CursorX
        jsr     CODE_0A_LINEFEED
        jmp     ESC_O_CANCEL_MODES
; ----------------------------------------------------------------------------
;CHR$(18) Reverse On
CODE_12_RVS_ON:
        lda     #$80
        sta     REVERSE
        rts
; ----------------------------------------------------------------------------
;CHR$(145) Cursor Up
CODE_91_CRSR_UP:
        ldy     CursorY
        cpy     WIN_TOP_LEFT_Y
        beq     LAD8A
        dec     CursorY
        dey
        jsr     LB059
        bcs     LAD89
        jsr     LB393_SET_LSXP_FF_SET_CARRY
LAD89:  rts

LAD8A:  lda     #$10
        bit     MEM_00AA
        bne     LAD91
        rts

LAD91:  jsr     LB393_SET_LSXP_FF_SET_CARRY
        bit     MEM_00AA
        bvc     LADA0
        jsr     SCROLL_WIN_DOWN
        ldy     WIN_TOP_LEFT_Y
        sty     CursorY
        rts

LADA0:  ldy     WIN_BTM_RGHT_Y
        sty     CursorY
        rts
; ----------------------------------------------------------------------------
;CHR$(10) Linefeed
;CHR$(17) Cursor Down
CODE_0A_LINEFEED:
CODE_11_CRSR_DOWN:
        ldy     CursorY
        cpy     WIN_BTM_RGHT_Y
        beq     LADB6
        jsr     LB059
        bcs     LADB3
        jsr     LB393_SET_LSXP_FF_SET_CARRY
LADB3:  inc     CursorY
        rts

LADB6:  lda     #$08
        bit     MEM_00AA
        bne     LADBD
        rts

LADBD:  jsr     LB393_SET_LSXP_FF_SET_CARRY
        bit     MEM_00AA
        bvc     LADCC
        jsr     SCROLL_WIN_UP
        ldy     WIN_BTM_RGHT_Y
        sty     CursorY
        rts

LADCC:  ldy     WIN_TOP_LEFT_Y
        sty     CursorY
        rts
; ----------------------------------------------------------------------------
;CHR$(29) Cursor Right
CTRL_1D_CRSR_RIGHT:
        ldx     WIN_BTM_RGHT_X
        cpx     CursorX
        beq     LADDA
        inc     CursorX
        rts

LADDA:  ldy     CursorY
        cpy     WIN_BTM_RGHT_Y
        beq     LADEF
        jsr     LB059
        bcs     LADE8
        jsr     LB393_SET_LSXP_FF_SET_CARRY
LADE8:  inc     CursorY
        ldx     WIN_TOP_LEFT_X
        stx     CursorX
        rts

LADEF:  lda     MEM_00AA
        bit     #$08
        bne     LADF6
        rts
LADF6:  ldx     WIN_TOP_LEFT_X
        stx     CursorX
        jsr     LB393_SET_LSXP_FF_SET_CARRY
        bit     #$40
        bne     LAE04
        jmp     CODE_13_HOME

LAE04:  ldy     CursorY
        jmp     SCROLL_WIN_UP
; ----------------------------------------------------------------------------
;CHR$(157) Cursor Left
CODE_9D_CRSR_LEFT:
        ldx     CursorX
        cpx     WIN_TOP_LEFT_X
        beq     LAE12
        dec     CursorX
        rts
; ----------------------------------------------------------------------------
LAE12:  ldy     CursorY
        cpy     WIN_TOP_LEFT_Y
        beq     LAE28
        dec     CursorY
        ldx     WIN_BTM_RGHT_X
        stx     CursorX
        dey
        jsr     LB059
        bcs     LAE27
        jsr     LB393_SET_LSXP_FF_SET_CARRY
LAE27:  rts
; ----------------------------------------------------------------------------
LAE28:  lda     MEM_00AA
        bit     #$10
        bne     LAE2F
        rts
; ----------------------------------------------------------------------------
LAE2F:  jsr     LB393_SET_LSXP_FF_SET_CARRY
        ldx     WIN_BTM_RGHT_X
        stx     CursorX
        bit     MEM_00AA
        bvc     LAE42
        jsr     SCROLL_WIN_DOWN
        ldy     WIN_TOP_LEFT_Y
        sty     CursorY
        rts
; ----------------------------------------------------------------------------
LAE42:  ldy     WIN_BTM_RGHT_Y
        sty     CursorY
        rts
; ----------------------------------------------------------------------------
;CHR$(147) Clear Screen
CODE_93_CLR_SCR:
        jsr     CODE_13_HOME
        ldy     WIN_BTM_RGHT_Y
LAE4C:  sty     CursorY
        jsr     ESC_D_DELETE_LINE
        ldy     CursorY
        dey
        cpy     WIN_TOP_LEFT_Y
        bpl     LAE4C
        jmp     LB087
; ----------------------------------------------------------------------------
LAE5B:  ldx     MEM_0380
        stx     WIN_TOP_LEFT_X
        ldx     CurMaxX
        stx     WIN_BTM_RGHT_X
        ldy     $037F
        sty     WIN_TOP_LEFT_Y
        ldy     CurMaxY
        sty     WIN_BTM_RGHT_Y
; ----------------------------------------------------------------------------
;CHR$(19) Home
CODE_13_HOME:
        jsr     LB393_SET_LSXP_FF_SET_CARRY
        ldx     WIN_TOP_LEFT_X
        stx     CursorX
        ldy     WIN_TOP_LEFT_Y
        sty     CursorY
        rts
; ----------------------------------------------------------------------------
;CHR$(20) Delete
CODE_14_DELETE:
        jsr     CODE_9D_CRSR_LEFT
        jsr     SaveCursorXY
LAE81:  ldx     CursorX
        cpx     WIN_BTM_RGHT_X
        bne     LAE8E
        ldy     CursorY
        jsr     LB059
        bcc     LAEA1
LAE8E:  jsr     CTRL_1D_CRSR_RIGHT
        jsr     GetCharAtCursorXY
        pha
        jsr     CODE_9D_CRSR_LEFT
        pla
        jsr     PutCharAtCursorXY
        jsr     CTRL_1D_CRSR_RIGHT
        bra     LAE81
LAEA1:  lda     #' '
        jsr     PutCharAtCursorXY
LAEA6:  ldx     SavedCursorX
        ldy     SavedCursorY
        stx     CursorX
        sty     CursorY
        rts
; ----------------------------------------------------------------------------
SaveCursorXY:
        ldx     CursorX
        ldy     CursorY
        stx     SavedCursorX
        sty     SavedCursorY
        rts
; ----------------------------------------------------------------------------
CompareCursorXYtoSaved:
        ldx     CursorX
        ldy     CursorY
        cpy     SavedCursorY
        bne     LAEC8
        cpx     SavedCursorX
LAEC8:  rts
; ----------------------------------------------------------------------------
;CHR$(148) Insert
CODE_94_INSERT:
        inc     INSRT
        bne     LAECF
        dec     INSRT
LAECF:  ldx     INSFLG
        beq     LAED5
        stz     INSRT
LAED5:  lda     #' '
        pha
        jsr     SaveCursorXY
        dec     CursorX
LAEDD:  jsr     LAC50
        jsr     GetCharAtCursorXY
        tax
        pla
        sta     (VidPtrLo)
        phx
        lda     CursorX
        cmp     WIN_BTM_RGHT_X
        bne     LAEDD
        lda     MEM_00AA
        bit     #$20
        beq     LAF16
        bit     #$04
        beq     LAF16
        cpx     #$20
        beq     LAF0F
        ldy     CursorY
        cpy     WIN_BTM_RGHT_Y
        bne     LAEDD
        ldy     SavedCursorY
        dey
        cpy     WIN_TOP_LEFT_Y
        bmi     LAEDD
        sty     SavedCursorY
        bra     LAEDD
LAF0F:  ldy     CursorY
        jsr     LB059
        bcs     LAEDD
LAF16:  pla
        jmp     LAEA6
; ----------------------------------------------------------------------------
PutSpaceAtCursorXY:
        lda     #' '
PutCharAtCursorXY:
        ldx     CursorX
        ldy     CursorY
        pha
        jsr     RegistersXY_to_VidPtr
        pla
        sta     (VidPtrLo)
        rts
; ----------------------------------------------------------------------------
;Set VidPtr to point to cursor position (CursorX, CursorY)
CursorXY_to_VidPtr:
        ldy     CursorY
        ldx     CursorX

;Set VidPtr to point to cursor position (X, Y)
RegistersXY_to_VidPtr:
        cld
        txa
        asl     a
        sta     VidPtrLo
        tya
        lsr     a
        ror     VidPtrLo
        adc     VidMemHi
        sta     VidPtrHi
        rts
; ----------------------------------------------------------------------------
LAF3A:  cld
        sec
        lda     WIN_BTM_RGHT_X
        sbc     WIN_TOP_LEFT_X
        rts
; ----------------------------------------------------------------------------
GetCharAtCursorXY:
        ldx     CursorX
        ldy     CursorY
        jsr     RegistersXY_to_VidPtr
        lda     (VidPtrLo)
        rts
; ----------------------------------------------------------------------------
;Scroll the current window up one line
SCROLL_WIN_UP:
        ldy     WIN_TOP_LEFT_Y
        cpy     CursorY
        beq     LAF7C
        ldx     WIN_TOP_LEFT_X
        jsr     RegistersXY_to_VidPtr
        jsr     LAF3A
        sta     $F3
        ldx     WIN_TOP_LEFT_Y
LAF5D:  lda     VidPtrLo
        ldy     VidPtrHi
        sta     $F1
        sty     $F2
        eor     #$80
        sta     VidPtrLo
        bmi     LAF6E
        iny
        sty     VidPtrHi
LAF6E:  ldy     $F3
LAF70:  lda     (VidPtrLo),y
        sta     ($F1),y
        dey
        bpl     LAF70
        inx
        cpx     CursorY
        bne     LAF5D
LAF7C:  jsr     LAFD3
        lda     #$C0
        tsb     $037D
        ldy     CursorY
        jmp     ESC_D_DELETE_LINE
; ----------------------------------------------------------------------------
;Scroll the current window down one line
SCROLL_WIN_DOWN:
        ldy     CursorY
        cpy     WIN_BTM_RGHT_Y
        beq     ESC_D_DELETE_LINE
        jsr     LAF3A
        sta     $F3
        ldy     WIN_BTM_RGHT_Y
LAF96:  phy
        ldx     WIN_TOP_LEFT_X
        jsr     RegistersXY_to_VidPtr
        lda     VidPtrLo
        ldy     VidPtrHi
        eor     #$80
        bpl     LAFA5
        dey
LAFA5:  sta     $F1
        sty     $F2
        ldy     $F3
LAFAB:  lda     ($F1),y
        sta     (VidPtrLo),y
        dey
        bpl     LAFAB
        ply
        dey
        cpy     CursorY
        bne     LAF96
        jsr     LAFF3
        lda     #$80
        tsb     $037D

;ESC-D Delete the current line
ESC_D_DELETE_LINE:
        ldy     CursorY
        ldx     WIN_TOP_LEFT_X
        jsr     RegistersXY_to_VidPtr
        jsr     LAF3A
        tay
        lda     #' '
LAFCD:  sta     (VidPtrLo),y
        dey
        bpl     LAFCD
        rts
; ----------------------------------------------------------------------------
LAFD3:  jsr     LB020
        eor     $F1
        and     $036A,x
        sta     $F1
        lda     $036A,x
        and     $F2
        lsr     a
        ora     $F1
        sta     $036A,x
        rol     $036A,x
LAFEB:  ror     $036A,x
        dex
        bpl     LAFEB
        bra     LB013

LAFF3:  jsr     LB020
        eor     $F2
        and     $036A,x
        sta     $F2
        lda     $036A,x
        and     $F1
        asl     a
        ora     $F2
        sta     $036A,x
        ror     $036a,X
LB00B:  rol     $036A,x
        inx
        cpx     #$02
        bne     LB00B
LB013:  ldy     WIN_TOP_LEFT_Y
        beq     LB01B
        dey
        jsr     LB07B
LB01B:  ldy     WIN_BTM_RGHT_Y
        jmp     LB07B
; ----------------------------------------------------------------------------
LB020:  ldy     $a2
        tya
        and     #$07                            ; B023 29 07                    ).
        tax                                     ; B025 AA                       .
        lda     LB049,x                         ; B026 BD 49 B0                 .I.
        sta     $F2                             ; B029 85 F2                    ..
        lda     LB051,x                         ; B02B BD 51 B0                 .Q.
        sta     $F1                             ; B02E 85 F1                    ..
LB030:  tya                                     ; B030 98                       .
        and     #$07                            ; B031 29 07                    ).
        tax                                     ; B033 AA                       .
        lda     PowersOfTwo,x                   ; B034 BD 41 B0                 .A.
        pha                                     ; B037 48                       H
        tya                                     ; B038 98                       .
        lsr     a                               ; B039 4A                       J
        lsr     a                               ; B03A 4A                       J
        lsr     a                               ; B03B 4A                       J
        and     #$01                            ; B03C 29 01                    ).
        tax                                     ; B03E AA                       .
        pla                                     ; B03F 68                       h
        rts                                     ; B040 60                       `
; ----------------------------------------------------------------------------
PowersOfTwo:
        .byte   $01,$02,$04,$08,$10,$20,$40,$80 ; B041 01 02 04 08 10 20 40 80  ..... @.
LB049:  .byte   $01,$03,$07,$0F,$1F,$3F,$7F,$FF ; B049 01 03 07 0F 1F 3F 7F FF  .....?..
LB051:  .byte   $FF,$FE,$FC,$F8,$F0,$E0,$C0,$80 ; B051 FF FE FC F8 F0 E0 C0 80  ........
; ----------------------------------------------------------------------------
LB059:  lda     #$04
        and     MEM_00AA
        beq     LB06D
        cpy     #$10
        bcs     LB06D
        jsr     LB030
        and     $036A,x
        beq     LB06D
        sec
        rts
; ----------------------------------------------------------------------------
LB06D:  clc
        rts
; ----------------------------------------------------------------------------
LB06F:  bcc     LB07B
        jsr     LB030
        ora     $036A,x
        sta     $036A,x
        rts
; ----------------------------------------------------------------------------
LB07B:  jsr     LB030
        eor     #$ff
        and     $036A,x
        sta     $036A,x
        rts
; ----------------------------------------------------------------------------
LB087:  stz     $036A
        stz     $036B
        rts
; ----------------------------------------------------------------------------
LB08E:  cmp     #$22
        bne     LB09A
        bit     QTSW
        stz     QTSW
        bvs     LB09A
        dec     QTSW
LB09A:  rts
; ----------------------------------------------------------------------------
LB09B:  cmp     #$FF
        bne     LB0A2_NOT_FF
        lda     #$5E    ;PETSCII up-arrow
        rts

LB0A2_NOT_FF:
        phx
        pha
        lsr     a       ;
        lsr     a       ;Shift bits 7-5 into bits 2-0
        lsr     a       ;   %11111111 -> %00000111
        lsr     a       ;To make index for LB0B0_BITS_TO_FLIP
        lsr     a       ;
        tax
        pla
        eor     LB0B0_BITS_TO_FLIP,x
        plx
        rts

LB0B0_BITS_TO_FLIP:
        .byte   $80     ;%000xxxxx  $00-1F
        .byte   $00     ;%001xxxxx  $20-3F
        .byte   $40     ;%010xxxxx  $40-5F
        .byte   $20     ;%011xxxxx  $60-7F
        .byte   $40     ;%100xxxxx  $80-9F
        .byte   $C0     ;%101xxxxx  $A0-BF
        .byte   $80     ;%110xxxxx  $C0-DF
        .byte   $80     ;%111xxxxx  $E0-EF
; ----------------------------------------------------------------------------
LB0B8:  sta     $F1
        and     #$3F
        asl     $F1
        bit     $F1
        bpl     LB0C4
        ora     #$80
LB0C4:  bcc     LB0CA
        ldx     QTSW
LB0C8:  bne     LB0CE
LB0CA:  bvs     LB0CE
        ora     #$40
LB0CE:  cmp     #$DE
        bne     LB0D4
        lda     #$FF
LB0D4:  rts
; ----------------------------------------------------------------------------
;Screen Editor escape codes
;
;These are very similar to the C128 escape codes;
;see the C128 Programmer's Reference Guide for the list.
;
;C128 codes missing on the LCD:
;  ESC-G (Enable Bell)
;  ESC-H (Disable Bell)
;  ESC-N (Return screen to normal video)
;  ESC-R (Set screen to reverse video)
;  ESC-S (Change to block cursor)
;  ESC-U (Change to underline cursor)
;  ESC-X (Swap 40/80 column output device)
;
ESC_KEYS_AND_HANDLERS:
        .byte   "A"
        .addr   ESC_A_AUTOINSERT_ON

        .byte   "B"
        .addr   ESC_B_SET_WIN_BTM_RIGHT

        .byte   "C"
        .addr   ESC_C_AUTOINSERT_OFF

        .byte   "D"
        .addr   ESC_D_DELETE_LINE

        .byte   "E"
        .addr   ESC_E_CRSR_BLINK_OFF

        .byte   "F"
        .addr   ESC_F_CRSR_BLINK_ON

        ;ESC-G (Enable Bell) and ESC-H (Disable Bell)
        ;from C128 are missing

        .byte   "I"
        .addr   ESC_I_INSERT_LINE

        .byte   "J"
        .addr   ESC_J_MOVE_TO_START_OF_LINE

        .byte   "K"
        .addr   ESC_K_MOVE_TO_END_OF_LINE

        .byte   "L"
        .addr   ESC_L_SCROLLING_ON

        .byte   "M"
        .addr   ESC_M_SCROLLING_OFF

        ;ESC-N (Normal video) and ESC-R (Reverse Video)
        ;from C128 are missing

        .byte   "O"
        .addr   ESC_O_CANCEL_MODES

        .byte   "P"
        .addr   ESC_P_ERASE_TO_START_OF_LINE

        .byte   "Q"
        .addr   ESC_Q_ERASE_TO_END_OF_LINE

        ;ESC-S (Block cursor) and ESC-U (Underline cursor)
        ;from C128 are missing

        .byte   "T"
        .addr   ESC_T_SET_WIN_TOP_LEFT

        .byte   "V"
        .addr   ESC_V_SCROLL_UP

        .byte   "W"
        .addr   ESC_W_SCROLL_DOWN

        ;ESC-X (Swap 40/80 column output device)
        ;from C128 is missing

        .byte   "Y"
        .addr   ESC_Y_SET_DEFAULT_TABS

        .byte   "Z"
        .addr   ESC_Z_CLEAR_ALL_TABS
; ----------------------------------------------------------------------------
LB10E_ESC:
        bit     LSTCHR
        bmi     LB126_RTS
        bvc     LB126_RTS
        lda     LSTCHR
        and     #$DF
        ldx     #$36
LB11C_LOOP:
        cmp     ESC_KEYS_AND_HANDLERS,x
        beq     LB127_JMP_TO_HANDLER
        dex
        dex
        dex
        bpl     LB11C_LOOP
LB126_RTS:
        rts
LB127_JMP_TO_HANDLER:
        jmp     (ESC_KEYS_AND_HANDLERS+1,x)
; ----------------------------------------------------------------------------
;ESC-A Enable auto-insert mode
ESC_A_AUTOINSERT_ON:
        sta     INSFLG ;Auto-insert = nonzero (on)
        stz     INSRT  ;Insert count = 0
        rts
; ----------------------------------------------------------------------------
;ESC-B Set bottom right of screen window at current position
ESC_B_SET_WIN_BTM_RIGHT:
        ldx     CursorX
        stx     WIN_BTM_RGHT_X
        ldy     CursorY
        sty     WIN_BTM_RGHT_Y
        jmp     LB087
; ----------------------------------------------------------------------------
;ESC-C Disable auto-insert mode
ESC_C_AUTOINSERT_OFF:
        stz     INSFLG
        rts
; ----------------------------------------------------------------------------
;ESC-I Insert line
ESC_I_INSERT_LINE:
        jsr     SCROLL_WIN_DOWN
        ldy     CursorY
        dey
        jsr     LB059
        iny
        jmp     LB06F
; ----------------------------------------------------------------------------
;ESC-J Move to start of current line
ESC_J_MOVE_TO_START_OF_LINE:
        ldx     WIN_TOP_LEFT_X
        stx     CursorX
        ldy     CursorY
LB150:  dey
        jsr     LB059
        bcs     LB150
        iny
        sty     CursorY
        rts
; ----------------------------------------------------------------------------
;ESC-K Move to end of current line
ESC_K_MOVE_TO_END_OF_LINE:
        dec     CursorY
LB15C:  inc     CursorY
        ldy     CursorY
        jsr     LB059
        bcs     LB15C
        ldx     WIN_BTM_RGHT_X
        stx     CursorX
        bra     LB16E
LB16B:  jsr     CODE_9D_CRSR_LEFT
LB16E:  jsr     GetCharAtCursorXY
        cmp     #$20
        bne     LB17F
        cpx     WIN_TOP_LEFT_X
        bne     LB16B
        dey
        jsr     LB059
        bcs     LB16B
LB17F:  rts
; ----------------------------------------------------------------------------
;ESC-L Enable scrolling
ESC_L_SCROLLING_ON:
        lda     #$40
        tsb     MEM_00AA
        rts
; ----------------------------------------------------------------------------
;ESC-M Disable scrolling
ESC_M_SCROLLING_OFF:
        lda     #$40
        trb     MEM_00AA
        rts
; ----------------------------------------------------------------------------
;ESC-Q Erase to end of current line
ESC_Q_ERASE_TO_END_OF_LINE:
        jsr     SaveCursorXY
        jsr     ESC_K_MOVE_TO_END_OF_LINE
        jsr     CompareCursorXYtoSaved
        bcs     LB19E
        jmp     LAEA6
; ----------------------------------------------------------------------------
;ESC-P Erase to start of current line
ESC_P_ERASE_TO_START_OF_LINE:
        jsr     SaveCursorXY
        jsr     ESC_J_MOVE_TO_START_OF_LINE
LB19E:  jsr     PutSpaceAtCursorXY
        jsr     CompareCursorXYtoSaved
        bne     LB1A7
        rts
LB1A7:  bpl     LB1AE
        jsr     CTRL_1D_CRSR_RIGHT
        bra     LB19E
LB1AE:  jsr     CODE_9D_CRSR_LEFT
        bra     LB19E
; ----------------------------------------------------------------------------
;ESC-T Set top left of screen window at cursor position
ESC_T_SET_WIN_TOP_LEFT:
        ldx     CursorX
        ldy     CursorY
        stx     WIN_TOP_LEFT_X
        sty     WIN_TOP_LEFT_Y
        jmp     LB087
; ----------------------------------------------------------------------------
;ESC-V Scroll up
ESC_V_SCROLL_UP:
        jsr     SaveCursorXY
        ldy     WIN_BTM_RGHT_Y
        sty     CursorY
        jsr     SCROLL_WIN_UP
        bra     LB1D4

;ESC-W Scroll down
ESC_W_SCROLL_DOWN:
        jsr     SaveCursorXY
        ldy     WIN_TOP_LEFT_Y
        sty     CursorY
        jsr     SCROLL_WIN_DOWN

LB1D4:  jsr     LB393_SET_LSXP_FF_SET_CARRY
        jmp     LAEA6
; ----------------------------------------------------------------------------
;Start the screen editor
SCINIT_:
        jsr     LB2E4_HIDE_CURSOR
        lda     #>$0828
        sta     VidMemHi
        ldx     #<$0828
        stx     $0368
        ldy     #$10
        sty     $0369
        lda     #16-1
        sta     CurMaxY
        lda     #80-1
        sta     CurMaxX
        stz     $037F
        stz     MEM_0380
        jsr     LAE5B
        lda     #$00
        tax
        ldy     VidMemHi
        clc
        jsr     LCDsetupGetOrSet
        jsr     CODE_93_CLR_SCR
        stz     QTSW
        stz     $0382
        stz     INSFLG
        stz     LSTCHR
        stz     INSFLG
        lda     #$ED
        sta     MEM_00AA
        stz     BLNOFF ;Blink = on
        jsr     ESC_Y_SET_DEFAULT_TABS
        ;Fall through to set initial modes

;ESC-O Cancel insert, quote, reverse modes
ESC_O_CANCEL_MODES:
        stz     QTSW  ;Quote mode = off
        stz     INSRT ;# chars to insert = 0
        ;Fall through to cancel reverse

;CHR$(146) Reverse Off
CODE_92_RVS_OFF:
        stz     REVERSE ;Reverse mode = off
        rts
; ----------------------------------------------------------------------------
LCDsetupGetOrSet:
; This routine is called by RESET routine, with carry set.
; It seems it's the only part where locations $FF80 - $FF83 are written.
; $FF80-$FF83 is the write-only registers of the LCD controller.
; It's called first with carry set from $87B5,
; then called second with carry clear from $B204
        php
        sei
        bcc     LCDsetupSet
        lda     SETUP_LCD_A
        ldx     SETUP_LCD_X
        ldy     SETUP_LCD_Y
LCDsetupSet:
        and     #$03
        sta     SETUP_LCD_A
        stx     SETUP_LCD_X
        sty     SETUP_LCD_Y
        ora     #$08
        sta     LCDCTRL_REG2
        sta     LCDCTRL_REG3
        stx     LCDCTRL_REG0
        tya
        asl     a
        sta     LCDCTRL_REG1
        lda     SETUP_LCD_A
        plp
        rts
; ----------------------------------------------------------------------------
EDITOR_LOCS:
        .word   CursorX,CursorY,WIN_TOP_LEFT_X,WIN_BTM_RGHT_X
        .word   WIN_BTM_RGHT_Y,WIN_TOP_LEFT_Y,QTSW
        .word   $037D  ;TODO name
        .word   INSRT,INSFLG
        .word   $00AA ;TODO name
        .word   BLNOFF,REVERSE
        .word   $036D,$036A,$036B ;TODO names
        .word   LSTCHR,TABMAP,TABMAP+1,TABMAP+2
        .word   TABMAP+3,TABMAP+4,TABMAP+5,TABMAP+6
        .word   TABMAP+7,TABMAP+8,TABMAP+9
        .word   VidMemHi,SETUP_LCD_A,SETUP_LCD_X,SETUP_LCD_Y
EDITOR_LOCS_SIZE = * - EDITOR_LOCS

;Given a pointer to a 62-byte area in MMU RAM mode, swap the state
;of the screen editor (the locations in EDITOR_LOCS) with values in
;that area.  If called twice, the first call will swap in a new editor
;state and the second call will restore the original state.
LB293_SWAP_EDITOR_STATE:
        stx     $F1
        sty     $F2
        jsr     LB2E4_HIDE_CURSOR
        stz     $0382
        lda     #$F1 ;ZP-address
        sta     SINNER
        sta     $0360
        ldy     #$00
        ldx     #$00
LB2A9_LOOP:
        lda     EDITOR_LOCS,x
        sta     VidPtrLo
        lda     EDITOR_LOCS+1,x
        sta     VidPtrHi

        lda     (VidPtrLo)              ;A = value in editor location (in MMU KERNAL mode)
        pha                             ;Push it onto the stack
        jsr     GO_RAM_LOAD_GO_KERN     ;Load value from MMU RAM mode
        sta     (VidPtrLo)              ;Store it in the editor location (in MMU KERNAL mode)
        pla                             ;Pop value that was there originally
        jsr     GO_RAM_STORE_GO_KERN    ;Save value in MMU RAM mode

        iny                             ;Increment indirect pointer
        inx                             ;Increment index to editor locations table
        inx                             ;  twice because each is a 16-bin address
        cpx     #EDITOR_LOCS_SIZE
        bne     LB2A9_LOOP
LB2C6_SEC_JMP_LCDsetupGetOrSet:
        sec
        jmp     LCDsetupGetOrSet
; ----------------------------------------------------------------------------
;ESC-E Set cursor to nonflashing mode
ESC_E_CRSR_BLINK_OFF:
        lda     #$80
        tsb     BLNOFF ;Blink=0x80 (Off)
        rts
; ----------------------------------------------------------------------------
;ESCF-F Set cursor to flashing mode
ESC_F_CRSR_BLINK_ON:
        lda     #$80
        trb     BLNOFF ;Blink=0 (On)
        rts
; ----------------------------------------------------------------------------
LB2D6_SHOW_CURSOR:
        jsr     LB2E4_HIDE_CURSOR
        jsr     CursorXY_to_VidPtr
        lda     (VidPtrLo)
        sta     CHAR_UNDER_CURSOR
        sec
        ror     BLNCT
        rts
; ----------------------------------------------------------------------------
LB2E4_HIDE_CURSOR:
        lda     #$FF
        trb     BLNCT
        beq     LB2EE
        lda     CHAR_UNDER_CURSOR
        sta     (VidPtrLo)
LB2EE:  rts
; ----------------------------------------------------------------------------
;Blink the cursor
;Called at 60 Hz by the default IRQ handler (see LFA44_VIA1_T1_IRQ).
BLINK:  lda     $0384
        bne     BLINK_RTS

        bit     BLNCT
        bpl     BLINK_RTS

        dec     BLNCT
        bmi     BLINK_RTS

        bit     BLNOFF
        bmi     LB305 ;Branch if blink is off

        lda     #$A0
        sta     BLNCT

LB305:  lda     CHAR_UNDER_CURSOR
        cmp     (VidPtrLo)
        bne     BLINK_STORE_AS_IS
        bit     CAPS_FLAGS
        bpl     BLINK_RVS_AND_STORE ;Branch if caps lock is off
        ;Caps lock is off
        and     #$80
        ora     #$1E ;probably makes "^" cursor when in caps mode
BLINK_RVS_AND_STORE:
        eor     #$80
BLINK_STORE_AS_IS:
        sta     (VidPtrLo)
BLINK_RTS:
        rts
; ----------------------------------------------------------------------------
LB319_CHRIN_DEV_3_SCREEN:
        lda     $80
        tsb     $0382
        bne     LB362
        jsr     LB393_SET_LSXP_FF_SET_CARRY
        bra     LB349

;CHRIN from keyboard
;Unlike other devices, CHRIN for the keyboard doesn't read one byte.  It
;reads keys until RETURN is pressed.  It returns one character from the
;input on the first call.  Each subsequent call returns the next character,
;until the end is reached, where 0x0D (return) is returned.
LB325_CHRIN_KEYBOARD:
        lda     #$80
        tsb     $0382
        bne     LB362
        jsr     SaveCursorXY
        stx     LSTP
        sty     LSXP
        bra     LB33A     ;blink cursor until return

LB337_LOOP:
        jsr     LABD6
;Input a line until carriage return
LB33A:  jsr     LB2D6_SHOW_CURSOR
        jsr     LB6DF_GET_KEY_BLOCKING
        pha
        jsr     LB2E4_HIDE_CURSOR
        pla
        cmp     #$0D  ;Return
        bne     LB337_LOOP

LB349:  stz     QTSW ;Quote mode = off
        jsr     ESC_K_MOVE_TO_END_OF_LINE
        jsr     SaveCursorXY
        ldy     LSXP
        bmi     LB35F
        sty     CursorY
        ldx     LSTP
        stx     CursorX
        bra     LB362

LB35F:  jsr     ESC_J_MOVE_TO_START_OF_LINE

LB362:  jsr     CompareCursorXYtoSaved
        bcc     LB36E
        lda     #$40
        tsb     $0382
        bne     LB387
LB36E:  jsr     GetCharAtCursorXY
        jsr     LB0B8
        jsr     LB08E
        bit     $0382
        bvs     LB383
        pha
        jsr     CTRL_1D_CRSR_RIGHT
        pla
LB381:  clc
        rts
; ----------------------------------------------------------------------------
LB383:  cmp     #' '
        bne     LB381
LB387:  jsr     ESC_K_MOVE_TO_END_OF_LINE
        stz     QTSW ;Quote mode = off
        stz     $0382
        lda     #$0D
        clc
        rts
; ----------------------------------------------------------------------------
LB393_SET_LSXP_FF_SET_CARRY:
        stz     LSXP
        dec     LSXP
        rts

; ----------------------------------------------------------------------------
; Keyboard Matrix Tables
; There are 5 tables representing combinations of the MODIFIER keys:
; 1. NO MODIFIER				NOTE:
; 2. SHIFT					    Keys shown assume TEXT mode
; 3. CAPS-LOCK					IE: $41 is "a" (which is opposite to ASCII)
; 4. COMMODORE
; 5. CTRL
;
; KEY: GR=Graphic Symbol			Character Changes:
;      S- Shifted				      126/$7E = PI
;      C- Control				      127/$7F = "|" (pipe)
;      {} Unknown Code				166/$A6 = "{"
;						                  168/$A8 = "}"
;
;NORMAL (no modifier key)                         C0     C1    C2    C3    C4    C5    C6    C7
KBD_MATRIX_NORMAL:                              ; ----- ----- ----- ----- ----- ----- ----- -----
        .byte   $40,$87,$86,$85,$88,$09,$0D,$14 ; @     F5    F3    F1    F7    TAB   RETRN DEL
        .byte   $8A,$45,$53,$5A,$34,$41,$57,$33 ; F4    e     s     z     4     a     w     3
        .byte   $58,$54,$46,$43,$36,$44,$52,$35 ; x     t     f     c     6     d     r     5
        .byte   $56,$55,$48,$42,$38,$47,$59,$37 ; v     u     h     b     8     g     y     7
        .byte   $4E,$4F,$4B,$4D,$30,$4A,$49,$39 ; n     o     k     m     0     j     i     9
        .byte   $2C,$2D,$3A,$2E,$91,$4C,$50,$11 ; ,     -     :     .     UP    l     p     DOWN
        .byte   $2F,$2B,$3D,$1B,$1D,$3B,$2A,$9D ; /     +     =     ESC   RIGHT ;     *     LEFT
        .byte   $8B,$51,$8C,$20,$32,$89,$13,$31 ; F6    q     F8    SPACE 2     F2    HOME  1

;SHIFT                                            C0     C1    C2    C3    C4    C5    C6    C7
KBD_MATRIX_SHIFT:                               ; ----- ----- ----- ----- ----- ----- ----- -----
        .byte   $BA,$87,$86,$85,$88,$09,$8D,$94 ; GR    F5    F3    F1    F7    TAB   S-RTN INS
        .byte   $8A,$65,$73,$7A,$24,$61,$77,$23 ; F4    E     S     Z     $     A     W     #
        .byte   $78,$74,$66,$63,$26,$64,$72,$25 ; X     T     F     C     &     D     R     %
        .byte   $76,$75,$68,$62,$28,$67,$79,$27 ; V     U     H     B     (     G     Y     '
        .byte   $6E,$6F,$6B,$6D,$5E,$6A,$69,$29 ; N     O     K     M     ^     J     I     )
        .byte   $3C,$60,$5B,$3E,$91,$6C,$70,$11 ; <     S-SPC [     >     UP    L     P     DOWN
        .byte   $3F,$7B,$7D,$1B,$1D,$5D,$A9,$9D ; ?     {     }     ESC   RIGHT ]     GR    LEFT
        .byte   $8B,$71,$8C,$A0,$22,$89,$93,$21 ; F6    Q     F8    S-SPC "     F2    CLS   !

;CAPS-LOCK key                                    C0    C1    C2    C3    C4    C5    C6    C7
KBD_MATRIX_CAPS:                            ; ----- ----- ----- ----- ----- ----- ----- -----
        .byte   $40,$87,$86,$85,$88,$09,$0D,$14 ; @     F5    F3    F1    F7    TAB   RETRN DEL
        .byte   $8A,$65,$73,$7A,$34,$61,$77,$33 ; F4    E     S     Z     4     A     W     3
        .byte   $78,$74,$66,$63,$36,$64,$72,$35 ; X     T     F     C     6     D     R     5
        .byte   $76,$75,$68,$62,$38,$67,$79,$37 ; V     U     H     B     8     G     Y     7
        .byte   $6E,$6F,$6B,$6D,$30,$6A,$69,$39 ; N     O     K     M     0     J     I     9
        .byte   $2C,$2D,$3A,$2E,$91,$6C,$70,$11 ; ,     -     :     .     UP    l     P     DOWN
        .byte   $2F,$2B,$3D,$1B,$1D,$3B,$2A,$9D ; /     +     =     ESC   RIGHT ;     *     LEFT
        .byte   $8B,$71,$8C,$20,$32,$89,$13,$31 ; F6    Q     F8    SPACE 2     F2    HOME  1

;Commodore key                                    C0    C1    C2    C3    C4    C5    C6    C7
KBD_MATRIX_CBMKEY:                              ; ----- ----- ----- ----- ----- ----- ----- -----
        .byte   $BA,$87,$86,$85,$88,$09,$8D,$94 ; GR    F5    F3    F1    F7    TAB   S-RTN INS
        .byte   $8A,$B1,$AE,$AD,$24,$B0,$B3,$23 ; F4    GR    GR    GR    $     GR    GR    #
        .byte   $BD,$A3,$BB,$BC,$26,$AC,$B2,$25 ; GR    GR    GR    GR    &     GR    GR    %
        .byte   $BE,$B8,$B4,$BF,$28,$A5,$B7,$27 ; GR    GR    GR    GR    (     GR    GR    '
        .byte   $AA,$B9,$A1,$A7,$5F,$B5,$A2,$29 ; GR    GR    GR    GR    ~?    GR    GR    )		; ? "~" not in original set
        .byte   $2C,$5C,$A6,$2E,$91,$B6,$AF,$11 ; ,     \     {     .     UP    GR    GR    DOWN
        .byte   $A4,$7C,$FF,$1B,$1D,$A8,$7F,$9D ; GR    |     PI    ESC   RIGHT }     GR    LEFT	; $7C=Pipe
        .byte   $8B,$AB,$8A,$A0,$32,$89,$93,$31 ; F6    GR    F4?   GR    2     F2    CLS   1		; ? Is F4 an error?

;CTRL key                                         C0    C1    C2    C3    C4    C5    C6    C7
KBD_MATRIX_CTRL:                                ; ----- ----- ----- ----- ----- ----- ----- -----
        .byte   $80,$87,$86,$85,$88,$09,$0D,$14 ; @     F5    F3    F1    F7    TAB   RETRN DEL
        .byte   $8A,$05,$13,$1A,$34,$01,$17,$33 ; F4    CT-E  HOME  CT-Z  4     CT-A  CT-W  3
        .byte   $18,$14,$06,$03,$36,$04,$12,$35 ; CT-Z  DEL   CT-F  STOP  6     CT-D  RVS   5
        .byte   $16,$15,$08,$02,$38,$07,$19,$37 ; CT-V  CT-U  LOCK  CT-B  8     CT-G  CT-Y  7
        .byte   $0E,$0F,$0B,$0D,$1E,$0A,$09,$39 ; TEXT  CT-O  CT-K  RETRN UARRW CT-J  CT-I  9
        .byte   $12,$1C,$1B,$92,$91,$0C,$10,$11 ; RVS   CT-\  ESC   R-OFF UP    CT-L  CT-P  DOWN
        .byte   $1F,$2B,$3D,$1B,$1D,$1D,$2A,$9D ; {$1F} +     =     ESC   RIGHT RIGHT *     LEFT	; Why CTRL-] = RIGHT?
        .byte   $8B,$11,$8C,$20,$32,$89,$13,$31 ; F6    CT-Q  F8    SPACE 2     F2    HOME  1
; ------------------------------------------------------------------------------------------------

KEYB_INIT:
        lda     #$09
        sta     MEM_03F6
        lda     #$1E
        sta     MEM_0367
        lda     #$01
        sta     MEM_0366
        sta     MEM_0365
        lda     #$FF
        sta     MEM_038E
        lda     #<LFA87_JMP_RTS_IN_KERN_MODE
        sta     RAMVEC_MEM_0336
        lda     #>LFA87_JMP_RTS_IN_KERN_MODE
        sta     RAMVEC_MEM_0336+1
        ;Fall through

;looks like clearing the keyboard buffer
LB4FB_RESET_KEYD_BUFFER:
        php
        sei
        stz     MEM_03F7
        stz     MEM_03F8
        stz     MEM_03F9
        plp
        rts
; ----------------------------------------------------------------------------
;Called at 60 Hz by the default IRQ handler (see LFA44_VIA1_T1_IRQ).
;Scan the keyboard
KL_SCNKEY:
        lda     MEM_00F4
        beq     LB54C
        dec     MEM_00F4
        lda     MEM_00AB
        and     #$07
        tax
        lda     PowersOfTwo,x
        eor     #$FF
        sta     VIA1_PORTA
        lda     MEM_00AB
        lsr     a
        lsr     a
        lsr     a
        tay
        jsr     KBD_TRIGGER_AND_READ_NORMAL_KEYS
        and     PowersOfTwo,y
        beq     LB52E
        lda     MEM_0365
        sta     MEM_00F4
LB52E:  lda     MEM_00AB
        eor     #$07
        tax
        lda     KBD_MATRIX_NORMAL,x
        cmp     #$85   ;F1
        bcc     LB53E
        cmp     #$8C+1 ;F8 +1
        bcc     LB549  ;Branch if key is F1-F8
LB53E:  dec     MEM_00F5
        bpl     LB549
        lda     MEM_0366
        sta     MEM_00F5
        bne     LB585
LB549:  jmp     KBD_READ_MODIFIER_KEYS_DO_SWITCH_AND_CAPS
; ----------------------------------------------------------------------------
LB54C:  lda     #$00
        sta     VIA1_PORTA
        jsr     KBD_TRIGGER_AND_READ_NORMAL_KEYS
        beq     LB549
        ldx     #$07
LB558:  lda     PowersOfTwo,x
        eor     #$FF
        sta     VIA1_PORTA
        jsr     KBD_TRIGGER_AND_READ_NORMAL_KEYS
        bne     LB56A
        dex
        bpl     LB558
        bra     LB549
LB56A:  ldy     #$FF
LB56C:  iny
        lsr     a
        bcc     LB56C
        tya
        asl     a
        asl     a
        asl     a
        dec     a
LB575:  inc     a
        dex
        bpl     LB575
        sta     MEM_00AB
        lda     MEM_0365
        sta     MEM_00F4
        lda     MEM_0367
        sta     MEM_00F5
LB585:  lda     MEM_00AB
        eor     #$07
        tax

        jsr     KBD_READ_MODIFIER_KEYS_DO_SWITCH_AND_CAPS
        and     #MOD_CTRL ;CTRL-key pressed?
        beq     LB5AC_NO_CTRL ;Branch if no

        ;TODO what does MEM_00AA do?
        lda     #$02
        and     MEM_00AA
        beq     LB5AC_NO_CTRL

        ;Check for CTRL-Q
        ldy     KBD_MATRIX_NORMAL,x
        cpy     #$51;'Q'
        bne     LB5A3_CHECK_CTRL_S

        ;CTRL-Q pressed (in BASIC, performs cursor down)
        ;reset bit 1 of 036D
        trb     $036D
        bra     LB5E1_JMP_LBFBE ;UNKNOWN_SECS/MINS

LB5A3_CHECK_CTRL_S:
        ;Check for CTRL-S
        cpy     #$53;'S'
        bne     LB5AC_NO_CTRL

        ;CTRL-S pressed (in BASIC, performs Home)
        ;set bit 1 of $036D
        tsb     $036D
        bra     LB5E1_JMP_LBFBE ;UNKNOWN_SECS/MINS

;No CTRL-key combination pressed
LB5AC_NO_CTRL:
        lda     MODKEY
        and     MEM_038E

        ldy     KBD_MATRIX_CTRL,x
        bit     #MOD_CTRL
        bne     LB5D0_GOT_KEYCODE     ;Branch to keep code from this matrix if CTRL pressed

        ldy     KBD_MATRIX_CBMKEY,x
        bit     #MOD_CBM              ;Branch to keep code from this matrix if CBM pressed
        bne     LB5D0_GOT_KEYCODE

        ldy     KBD_MATRIX_SHIFT,x
        bit     #MOD_SHIFT            ;Branch to keep code from this matrix if SHIFT pressed
        bne     LB5D0_GOT_KEYCODE

        ldy     KBD_MATRIX_CAPS,x
        bit     #MOD_CAPS
        bne     LB5D0_GOT_KEYCODE     ;Branch to keep code from this matrix if CAPS pressed

        ldy     KBD_MATRIX_NORMAL,x   ;Otherwise, use code from normal matrix

LB5D0_GOT_KEYCODE:
        tya                           ;A=key from matrix

        ldy     MEM_03FA
LB5D4:  bne     LB5E1_JMP_LBFBE       ;UNKNOWN_SECS/MINS

        ldy     KBD_MATRIX_NORMAL,x
        jsr     LFA84
        sta     MEM_00AC
        jsr     PUT_KEY_INTO_KEYD_BUFFER

LB5E1_JMP_LBFBE:
        jmp     LBFBE ;UNKNOWN_SECS/MINS

; ----------------------------------------------------------------------------
KBD_TRIGGER_AND_READ_NORMAL_KEYS:
;Read "normal" (non-modifier) keys
;
;CLCD's keyboard is read through VIA1's SR.  PB0 seems to trigger (0->1)
;the keyboard "controller" to provide bits through serial transfer.
        lda     VIA1_PORTB
        and     #%11111110
        sta     VIA1_PORTB ;PB0=0
        inc     VIA1_PORTB ;PB0=1 Start Key Read
        lda     VIA1_SR

KBD_READ_SR:
        lda     #$04
KBD_READ_SR_WAIT:
        bit     VIA1_IFR
        beq     KBD_READ_SR_WAIT
        lda     VIA1_SR
        rts

; ----------------------------------------------------------------------------
KBD_READ_MODIFIER_KEYS_DO_SWITCH_AND_CAPS:
;Read the modifier keys (SHIFT, CTRL, etc.)
;Swap upper/lowercase
;Toggle CAPS lock
;
        jsr     KBD_READ_SR
        sta     MODKEY
LB602:  and     #MOD_CBM+MOD_SHIFT
        eor     #MOD_CBM+MOD_SHIFT
        ora     SWITCH_COUNT
        bne     LB613
        jsr     SWITCH_CHARSET ;Switch uppercase/lowercase mode
        lda     #$3C ;Initial count for debounce
        sta     SWITCH_COUNT
LB613:  dec     SWITCH_COUNT
        bpl     LB61B
        stz     SWITCH_COUNT
LB61B:  lda     #MOD_CAPS
        trb     MODKEY
        beq     LB62F_CAPS_PRESSED
        lda     CAPS_FLAGS
        bit     #$40
        bne     LB634
        eor     #$C0
        sta     CAPS_FLAGS
        bra     LB634
LB62F_CAPS_PRESSED:
        lda     #$40
        trb     CAPS_FLAGS
LB634:  bit     CAPS_FLAGS
        bpl     LB63D
        lda     #MOD_CAPS
        tsb     MODKEY
LB63D:  lda     MODKEY
        rts

; ----------------------------------------------------------------------------
;TODO probably put key into buffer
PUT_KEY_INTO_KEYD_BUFFER:
        php                                     ; B640 08                       .
        sei                                     ; B641 78                       x
        phx                                     ; B642 DA                       .
        ldx     MEM_03F7                        ; B643 AE F7 03                 ...
        dex                                     ; B646 CA                       .
        bpl     LB64C                           ; B647 10 03                    ..
        ldx     MEM_03F6                        ; B649 AE F6 03                 ...
LB64C:  cpx     MEM_03F8                        ; B64C EC F8 03                 ...
        bne     LB655                           ; B64F D0 04                    ..
        plx                                     ; B651 FA                       .
        plp                                     ; B652 28                       (
        sec                                     ; B653 38                       8
        rts                                     ; B654 60                       `
LB655:  and     #$FF                            ; B655 29 FF                    ).
        beq     LB668                           ; B657 F0 0F                    ..
LB659:  ldx     MEM_03F7                        ; B659 AE F7 03                 ...
        sta     KEYD,x                          ; B65C 9D EC 03                 ...
        dex                                     ; B65F CA                       .
        bpl     LB665                           ; B660 10 03                    ..
        ldx     MEM_03F6                        ; B662 AE F6 03                 ...
LB665:  stx     MEM_03F7                        ; B665 8E F7 03                 ...
LB668:  plx                                     ; B668 FA                       .
        plp                                     ; B669 28                       (
        clc                                     ; B66A 18                       .
        rts                                     ; B66B 60                       `
; ----------------------------------------------------------------------------
;todo probably get key from buffer
GET_KEY_FROM_KEYD_BUFFER:
        ldx     MEM_03F8                        ; B66C AE F8 03                 ...
        lda     #$00                            ; B66F A9 00                    ..
        cpx     MEM_03F7                        ; B671 EC F7 03                 ...
        beq     LB683                           ; B674 F0 0D                    ..
        lda     KEYD,x                          ; B676 BD EC 03                 ...
        dex                                     ; B679 CA                       .
        bpl     LB67F                           ; B67A 10 03                    ..
        ldx     MEM_03F6                        ; B67C AE F6 03                 ...
LB67F:  stx     MEM_03F8                        ; B67F 8E F8 03                 ...
        clc                                     ; B682 18                       .
LB683:  rts                                     ; B683 60                       `
; ----------------------------------------------------------------------------
LB684_STA_03F9:
        sta     MEM_03F9                        ; B684 8D F9 03                 ...
        rts                                     ; B687 60                       `
; ----------------------------------------------------------------------------
LB688_GET_KEY_NONBLOCKING:
        phx
        phy

        lda     MEM_03F9
        stz     MEM_03F9
        bne     LB6D1_NONZERO

        ldx     #$0C
        jsr     LD230_JMP_LD233_PLUS_X    ;-> LD2B2_X_0C
        tax
        bne     LB6D1_NONZERO

        jsr     GET_KEY_FROM_KEYD_BUFFER
        bcc     LD294_LD233_0A_THEN_0C

        lda     #doschan_14_cmd_app
LB6A1:  jsr     V1541_SELECT_CHANNEL_A
        bcc     LB6C0_V1541_SELECT_ERROR ;branch on error

        rol     MEM_03FA

        lda     MODKEY
        lsr     a ;Bit 0 = MOD_STOP
        bcs     LB6BD_STOP_OR_V1541_L8B46_ERROR ;Branch if STOP pressed

        jsr     L8B46 ;maybe returns a cbm dos error code
        bcc     LB6BD_STOP_OR_V1541_L8B46_ERROR

        bit     SXREG
        bpl     LB6BB_BRA_LD294_LD233_0A_THEN_0C

        jsr     L8C8B_CLEAR_ACTIVE_CHANNEL

LB6BB_BRA_LD294_LD233_0A_THEN_0C:
        bra     LD294_LD233_0A_THEN_0C

LB6BD_STOP_OR_V1541_L8B46_ERROR:
        jsr     L8C8B_CLEAR_ACTIVE_CHANNEL

LB6C0_V1541_SELECT_ERROR:
        stz     MEM_03FA
        lda     #$00
        bra     LB6D9_DONE

LD294_LD233_0A_THEN_0C:
        ldx     #$0A
        jsr     LD230_JMP_LD233_PLUS_X    ;-> LD263_X_0A
        ldx     #$0C
        jsr     LD230_JMP_LD233_PLUS_X    ;-> LD2B2_X_0C

LB6D1_NONZERO:
        tax
        beq     LB6D9_DONE
        pha
        jsr     LBFBE ;UNKNOWN_SECS/MINS
        pla

LB6D9_DONE:
        ply
        plx
        cmp     #$00
        clc
        rts
; ----------------------------------------------------------------------------

LB6DF_GET_KEY_BLOCKING:
        jsr     LBFF2
        jsr     LB688_GET_KEY_NONBLOCKING
        beq     LB6DF_GET_KEY_BLOCKING
        rts
; ----------------------------------------------------------------------------
LB6E8_STOP:
        lda     MODKEY
        eor     #MOD_STOP
        and     #MOD_STOP
        bne     LB6F8_RTS
        php
        jsr     CLRCH
        jsr     LB4FB_RESET_KEYD_BUFFER
        plp
LB6F8_RTS:
        rts
; ----------------------------------------------------------------------------
;There seems to be two different behaviors
;depending on the carry flag when entering this routine
LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT:
        bcc     LB710_CARRY_CLEAR_ENTRY
        ;carry set entry
        sta     $03FD
        txa
        lsr     a
        clc
        cld
        adc     VidMemHi
        sta     $BE
        txa
LB707:  lsr     a
        tya
        bcc     LB70D
        ora     #$80
LB70D:  sta     $BD
        rts

LB710_CARRY_CLEAR_ENTRY:
        phx
        phy
        LDX     $03fd
        beq     LB754_DONE_SEC
        cpx     #$80
        beq     LB754_DONE_SEC
        cmp     #$0d
        bne     LB729
LB71F:  lda     #$20
        clc
        jsr     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
        bcc     LB71F
        bra     LB754_DONE_SEC
LB729:  cmp     #$12
        bne     LB734_NE_12
        lda     #$80
        tsb     $03FD
        bra     LB750_DONE_CLC
LB734_NE_12:
        cmp     #$92 ;Reverse off
        bne     LB73F_NE_92
        lda     #$80
        trb     $03FD
        bra     LB750_DONE_CLC
LB73F_NE_92:
        dec     $03FD
        jsr     LB09B
        bit     $03FD
        bpl     LB74C_NC
        eor     #$80
LB74C_NC:
        sta     ($BD)
        inc     $BD
LB750_DONE_CLC:
        clc
        ply
        plx
        rts
LB754_DONE_SEC:
        sec
        ply
        plx
        rts
; ----------------------------------------------------------------------------
LB758:  cpx     #$00
        beq     LB760
        sta     $B0
        stx     $B1
LB760:  ldy     #0
        lda     ($B0),y
        tax
        iny
        lda     ($B0),y
        asl     a
        sta     $F6
        txa
        lsr     a
        tax
        ror     $F6
        adc     VidMemHi
        sta     $F7
        iny
        lda     ($B0),y
        sta     $03FE
        iny
        lda     ($B0),y
        sta     $03FF
LB780:  iny
        lda     ($B0),y
        sta     $0400
        ldx     #$00
LB788_LOOP:
        lda     LINE_INPUT_BUF,x
        beq     LB799
        cpx     $03FE
        beq     LB795
        inx
        bne     LB788_LOOP
LB795:  sec
        lda     #$00
        rts
; ----------------------------------------------------------------------------
LB799:  stx     $0403
        stx     $0402
        jsr     LB8B3
        lda     $0400
LB7A5:  and     #$02
LB7A7:  beq     LB7AB
        clc
        rts
; ----------------------------------------------------------------------------
LB7AB:
        jsr     SET_CURSOR_XY_FROM_PTR_B0_AND_0404_THEN_TURN_ON_CURSOR
LB7AE_LOOP_UNTIL_KEY:
        jsr     LBFF2
        jsr     LB688_GET_KEY_NONBLOCKING
        bne     LB7BE_GOT_KEY
        lda     MODKEY
        and     #MOD_STOP
        beq     LB7AE_LOOP_UNTIL_KEY
        lda     #$03
LB7BE_GOT_KEY:
        sta     $0401
        ldy     #$05
LB7C3:  lda     ($B0),y
        beq     LB7DE
        cmp     $0401
        beq     LB7CF
        iny
        bne     LB7C3
LB7CF:  pha
        jsr     LB8B3
        ldx     $0402
        lda     #$00
        sta     LINE_INPUT_BUF,x
        pla
        clc
        rts

LB7DE:  tax
        lda     $0401
LB7E2_SEARCH_LOOP:
        cmp     LB7F4_KEYCODES,x
        beq     LB7EE_FOUND
        inx
        cpx     #$06
        bne     LB7E2_SEARCH_LOOP
        beq     LB80C_KEYCODE_NOT_FOUND
LB7EE_FOUND:
        jsr     LB806_DISPATCH
        jmp     LB7AB

LB7F4_KEYCODES:
        .byte $94 ;insert
        .byte $14 ;delete
        .byte $1d ;cursor right
        .byte $9d ;cursor left
        .byte $93 ;clear screen
        .byte $8d ;shift-return

LB7FB_KEYCODE_HANDLERS:
        .addr LB845_94_INSERT
        .addr LB86C_14_DELETE
        .addr LB889_1D_CURSOR_RIGHT
        .addr LB897_9D_CURSOR_LEFT
        .addr LB8A2_93_CLEAR
        .addr LB8AD_8D_SHIFT_RETURN

LB806_DISPATCH:
        txa
        asl     a
        tax
        jmp     (LB7FB_KEYCODE_HANDLERS,x)

LB80C_KEYCODE_NOT_FOUND:
        tax
        and     #$7F
        cmp     #$20
        bcc     LB7AE_LOOP_UNTIL_KEY
        txa
        ldx     $0403
        sta     LINE_INPUT_BUF,x
        cpx     $0402
        bne     LB82A
        ldx     $0402
        cpx     $03FE
        beq     LB82E
        inc     $0402
LB82A:  inx
        stx     $0403
LB82E:  lda     $0400
        and     #$01
        beq     LB83F
        cpx     $03FE
        bne     LB83F
        lda     #$00
        jmp     LB7CF

LB83F:  jsr     LB8B3
        jmp     LB7AB
; ----------------------------------------------------------------------------
LB845_94_INSERT:
        ldx     $0402
        cpx     $03FE
        beq     LB869
        cpx     $0403
        beq     LB869
LB852:  lda     LINE_INPUT_BUF,x
        sta     LINE_INPUT_BUF+1,x
        cpx     $0403
        beq     LB861
        dex
        jmp     LB852

LB861:  lda     #$20
        sta     LINE_INPUT_BUF,x
        inc     $0402
LB869:  jmp     LB8B3
; ----------------------------------------------------------------------------
LB86C_14_DELETE:
        ldx     $0403
        beq     LB886
        dec     $0403
        dex
LB875:  lda     LINE_INPUT_BUF+1,x
        sta     LINE_INPUT_BUF,x
        cpx     $0402
        beq     LB883
        inx
        bne     LB875
LB883:  dec     $0402
LB886:  jmp     LB8B3
; ----------------------------------------------------------------------------
LB889_1D_CURSOR_RIGHT:
        lda     $0403
        cmp     $0402
        beq     LB894
        inc     $0403
LB894:  jmp     LB8B3
; ----------------------------------------------------------------------------
LB897_9D_CURSOR_LEFT:
        lda     $0403
        beq     LB89F
        dec     $0403
LB89F:  jmp     LB8B3
; ----------------------------------------------------------------------------
LB8A2_93_CLEAR:
        lda     #$00
LB8A4:  sta     $0402
        sta     $0403
        jmp     LB8B3
; ----------------------------------------------------------------------------
LB8AD_8D_SHIFT_RETURN:
        lda     $0403
        jmp     LB8A4
; ----------------------------------------------------------------------------
LB8B3:  jsr     LB2E4_HIDE_CURSOR
        ldy     #$00
        ldx     #$00
        lda     $0403
        sec
        sbc     $03FF
        bcc     LB8CB
        tax
        lda     $03FF
        sbc     #$01
        bne     LB8CE
LB8CB:  lda     $0403
LB8CE:  sta     $0404
LB8D1_LOOP:
        cpx     $0402
        beq     LB8F3
        lda     LINE_INPUT_BUF,X
        phx
        jsr     LB09B
        plx
        sta     $0401
        lda     $0400
        and     #$80
        ora     $0401
LB8E9:  sta     ($F6),y
        inx
        iny
        cpy     $03FF
        bne     LB8D1_LOOP
        rts

LB8F3:  lda     $0400
        and     #$80
        ora     #$20
LB8FA_LOOP:
        sta     ($F6),y
        iny
        cpy     $03FF
        bne     LB8FA_LOOP
        rts
; ----------------------------------------------------------------------------
SET_CURSOR_XY_FROM_PTR_B0_AND_0404_THEN_TURN_ON_CURSOR:
        ldy     #$00
        lda     ($B0),y     ;X position
        tax

        iny
        lda     ($B0),y     ;Y position
        clc
        adc     $0404       ;Y = Y + value at $0404
        tay

        clc
        jsr     PLOT_
        jsr     LB2D6_SHOW_CURSOR
        rts
; ----------------------------------------------------------------------------
LB918_CHRIN___OR_LB688_GET_KEY_NONBLOCKING:
        lda     DFLTN
        and     #$1F
        bne     CHRIN__
LB91F:  jmp     LB688_GET_KEY_NONBLOCKING
; ----------------------------------------------------------------------------
LB922_PLY_PLX_RTS:
        ply
        plx
LB924_RTS:
        rts
; ----------------------------------------------------------------------------
CHRIN__:phx
        phy
        lda     #>(LB922_PLY_PLX_RTS-1)
        pha
        lda     #<(LB922_PLY_PLX_RTS-1)
        pha

        lda     DFLTN
        and     #$1F
        bne     LB937_NOT_KEYBOARD
        ;Device 0 keyboard or >31
        jmp     LB325_CHRIN_KEYBOARD

LB937_NOT_KEYBOARD:
        cmp     #$02 ;RS-232
        bne     LB948_NOT_RS232
LB93C = * + 1

        ;Device 2 RS-232
        jsr     AGETCH          ;Get byte from RS-232
        pha
        lda     SA
        and     #$0F            ;SA & 0x0F sets translation mode
        tax
        pla
        jmp     TRANSL_INCOMING_CHAR  ;Translate char before returning it

LB948_NOT_RS232:
        bcs     LB94D ;Device >= 2
        ;Device 1 Virtual 1541
        jmp     V1541_CHRIN

LB94D:  cmp     #$03 ;Screen
        bne     LB954_NOT_SCREEN

        ;Device 3 Screen
        jmp     LB319_CHRIN_DEV_3_SCREEN

LB954_NOT_SCREEN:
        cmp     #$1E ;30=Centronics
        bne     LB95B_NOT_CENTRONICS
        ;Device 30 (Centronics)
        jmp     ERROR6 ;NOT INPUT FILE

LB95B_NOT_CENTRONICS:
        ;Device 4-29 (IEC)
        bcc     ACPTR_IF_ST_OK_ELSE_0D

        ;Device 31 (RTC)
        jmp     RTC_CHRIN

; ----------------------------------------------------------------------------

;If ST=0, read a byte from IEC.
;Otherwise, return a carriage return (0x0D).
ACPTR_IF_ST_OK_ELSE_0D:
        lda     SATUS
        bne     LB968
        sec
        jmp     ACPTR
LB968:  lda     #$0D
        clc
        rts

; ----------------------------------------------------------------------------
;NBSOUT
CHROUT__:
        ;Push X and Y onto stack, will be popped on return by LB922_PLY_PLX_RTS
        phx
        phy

        ;Push return address LB922_PLY_PLX_RTS
        ldx     #>(LB922_PLY_PLX_RTS-1)
        phx
        ldx     #<(LB922_PLY_PLX_RTS-1)
        phx

LB974:  pha ;Push byte to write

        ;Get device number into X
        lda     DFLTO
        and     #$1F
        tax

        pla ;Pull byte to write
        cpx     #$01  ;1 = Virtual 1541
        bne     LB983
        jmp     V1541_CHROUT ;CHROUT to Virtual 1541

LB983:  bcs     LB988
LB985:  jmp     KR_ShowChar_ ;X=0

LB988:  cpx     #$03
        beq     LB985 ;X=3 (Screen)
        bcs     LB994

        ;Device = 2 (ACIA)
        jsr     TRANSL_OUTGOING_CHAR_GIVEN_SA
        jmp     ACIA_CHROUT

LB994:  cpx     #$1E  ;30
        bne     LB9A7
        ;Device = 30 (Centronics port)
        ldx     SA
        pha
        lda     SA
        and     #$0F            ;SA & 0x0F sets translation mode
        tax
        pla
        jsr     TRANSL_OUTGOING_CHAR_GIVEN_SA  ;Translate char before sending it
        jmp     CENTRONICS_CHROUT

LB9A7:  bcc     LB9AC
        jmp     RTC_CHROUT

LB9AC:  sec
        jmp     CIOUT ;IEC

; ----------------------------------------------------------------------------

;Translate character before sending it to ACIA TX or Centronics out
;Translation mode is set by secondary address
;Set X=SA & $0F, A=char to translate
TRANSL_OUTGOING_CHAR_GIVEN_SA:
        pha
LB9B1:  lda     SA
        and     #$0F
        tax
        pla
        jmp     TRANSL_OUTGOING_CHAR

; ----------------------------------------------------------------------------

CHKIN__:jsr     LOOKUP
        beq     LB9C2
        jmp     ERROR3 ;FILE NOT OPEN

LB9C2:  jsr     JZ100
        beq     JX320_NEW_DFLTN   ;Device 0 (Keyboard)

        cmp     #$1E
        bcs     JX320_NEW_DFLTN   ;Device >= 30 (30=Centronics, 31=RTC)
        cmp     #$01

        beq     LB9FE             ;Device 1 (Virtual 1541)
        cmp     #$03

        beq     JX320_NEW_DFLTN   ;Device 3 (Screen)
        bcs     LB9E1_CHKIN_IEC   ;Device 4-29 (IEC)

        jsr     LBF4D_CHKIN_ACIA  ;Device 2 (ACIA)
        bcs     LB9E0_RTS_ONLY    ;Branch if failed (never fails)

        lda     FA
JX320_NEW_DFLTN:
        sta     DFLTN
        clc
LB9E0_RTS_ONLY:
        rts

LB9E1_CHKIN_IEC:
        tax
        jsr     TALK__
        bit     SATUS
        bmi     LB9FB_JMP_ERROR5
        lda     SA
        bpl     JX340
        jsr     LBD5B
        jmp     JX350

JX340:  jsr     TKSA
JX350:  txa
        bit     SATUS
        bpl     JX320_NEW_DFLTN
LB9FB_JMP_ERROR5:
        jmp     ERROR5 ;DEVICE NOT PRESENT
LB9FE:  jsr     L9962
        bcc     JX320_NEW_DFLTN
        bra     LB9FB_JMP_ERROR5

; ----------------------------------------------------------------------------

;NCKOUT
CHKOUT__:
        jsr     LOOKUP
        beq     LBA0D
        jmp     ERROR3 ;FILE NOT OPEN

LBA0D:  jsr     JZ100
        bne     LBA15
        jmp     ERROR7 ;NOT OUTPUT FILE

LBA15:  cmp     #$1E
        bcs     LBA32
        cmp     #$02
        beq     LBA2B
        bcs     LBA25
        jsr     L9962
        bcc     LBA32
        rts

LBA25:  cmp     #$03
        beq     LBA32
        bne     LBA37
LBA2B:  jsr     LBF4D_CHKIN_ACIA
        bcs     LBA36
        lda     FA
LBA32:  sta     DFLTO
        clc
LBA36:  rts

LBA37:  tax
        jsr     LISTN
        bit     SATUS
        bmi     LBA50
        lda     SA
        bpl     LBA48
        jsr     SCATN
        bne     LBA4B
LBA48:  jsr     SECND
LBA4B:  txa
        bit     SATUS
        bpl     LBA32
LBA50:  jmp     ERROR5 ;DEVICE NOT PRESENT

; ----------------------------------------------------------------------------

;NCLOSE
;Called with logical file name in A
CLOSE__:ror     WRBASE        ;save serial close flag (used below in JX120_CLOSE_IEC)
        jsr     JLTLK         ;look file up
        beq     JX050         ;file is open, branch to close it
        clc                   ;else return
        rts

JX050:  jsr     JZ100         ;extract table data
        txa                   ;save table index
        pha

        lda     FA
        beq     JX150             ;Device 0 (Keyboard)

        cmp     #$1E
        bcs     JX150             ;Device >= 30 (30=Centronics, 31=RTC)

        cmp     #$03
        beq     JX150             ;Device 3 (Screen)
        bcs     JX120_CLOSE_IEC   ;Device 4-29 (IEC)

        cmp     #$02
        bne     LBA79_CLOSE_V1541 ;Device = 1 (Virtual 1541)

        jsr     ACIA_CLOSE        ;Device = 2 (ACIA)
        bra     JX150

LBA79_CLOSE_V1541:
        jsr     V1541_CLOSE
        bra     JX150

JX120_CLOSE_IEC:
        bit     WRBASE        ;do a real close?
        bpl     ROPEN         ;yep
        lda     FA            ;no if a disk & sa=$f
        cmp     #$08
        bcc     ROPEN         ;>8 ==>not a disk, do real close
        lda     SA
        and     #$0F
        cmp     #15           ;command channel?
        beq     JX150         ;yes, sa=$f, no real close

ROPEN:  jsr     CLSEI

; entry to remove a give logical file
; from table of logical, primary,
; and secondary addresses

JX150:  pla                   ;get table index off stack
        tax
        dec     LDTND
        cpx     LDTND         ;is deleted file at end?
        beq     JX170         ;yes...done

; delete entry in middle by moving
; last entry to that position.

        ldy     LDTND
        lda     LAT,y
        sta     LAT,x
        lda     FAT,y
        sta     FAT,x
        lda     SAT,y
        sta     SAT,x
JX170:  clc                   ;close exit
        rts
; ----------------------------------------------------------------------------
;LOOKUP TABLIZED LOGICAL FILE DATA
;
LOOKUP: stz     SATUS
        txa
JLTLK:  ldx     LDTND
JX600:  dex
        bmi     JZ101
        cmp     LAT,x
        bne     JX600
        rts
; ----------------------------------------------------------------------------
;ROUTINE TO FETCH TABLE ENTRIES
;
JZ100:  lda     LAT,x
        sta     LA
        lda     SAT,x
        sta     SA
        lda     FAT,x
        sta     FA
JZ101:  rts
; ----------------------------------------------------------------------------
;NCLALL
;*************************************
;* clall -- close all logical files  *
;* deletes all table entries and     *
;* restores default i/o channels     *
;* and clears serial port devices.   *
;*************************************
CLALL__:stz     LDTND     ;Forget all files

;NCLRCH
;****************************************
;* clrch -- clear channels              *
;* unlisten or untalk serial devcs, but *
;* leave others alone. default channels *
;* are restored.                        *
;****************************************
;
;XXX This is a bug.  This routine assumes that any device > 3 is an
;IEC device that needs to be UNTLKed or UNLSNed.  That was true on other
;machines but the LCD has two new devices, the Centronics port ($1E / 30)
;and the RTC ($1F / 31), that are not IEC.  When one of these devices is
;open, this routine will needlessly send UNLSN or UNTLK to IEC.  This can
;be seen at the power-on menu.  The menu continuously polls the RTC via
;CHRIN and calls CLALL after each poll, which comes here (CLRCHN), and an
;unnecessary UNTLK is sent.  To fix this, ignore devices $1E and $1F here.
CLRCHN__:
        ldx     #3        ;Device 3 (Screen)

        cpx     DFLTO     ;Compare 3 to default output channel
        bcs     LBAE1     ;Branch if DFLTO <= 3 (not IEC)
        jsr     UNLSN     ;Device is IEC so UNLSN

LBAE1:  cpx     DFLTN     ;Compare 3 to default input channel
        bcs     LBAE9     ;Branch if DFLTN <= 3 (not IEC)
        jsr     UNTLK     ;Device is IEC so UNTLK

LBAE9:  stx     DFLTO     ;Default output device = 3 (Screen)
        stz     DFLTN     ;Default output device = 0 (Keyboard)
        rts

; ----------------------------------------------------------------------------

;NOPEN
Open__: ldx     LA
        jsr     LOOKUP
        bne     OP100
        jmp     ERROR2 ;FILE OPEN

OP100:  ldx     LDTND
        cpx     #$0C
        bcc     OP110
        jmp     ERROR1 ;TOO MANY FILES

OP110:  inc     LDTND
        lda     LA
        sta     LAT,x
        lda     SA
        ora     #$60
        sta     SA
        sta     SAT,x
        lda     FA
        sta     FAT,x
;
;PERFORM DEVICE SPECIFIC OPEN TASKS
;
        beq     LBB2F_CLC_RTS     ;Device 0 (Keyboard), nothing to do.

        cmp     #$1E              ;Device 30 (Centronics port)
        beq     LBB2F_CLC_RTS     ;Nothing to do

        bcc     LBB25_OPEN_LT_30  ;Device <30

        ;Device 31 (RTC)
        jmp     RTC_OPEN

;Device < 30
LBB25_OPEN_LT_30:
        cmp     #$03              ;3 (Screen)
        beq     LBB2F_CLC_RTS     ;Return OK
        bcc     LBB31_OPEN_LT_3   ;Device < 3

        sec
        jsr     OPENI    ;Device 4-29
LBB2F_CLC_RTS:
        clc
        rts

;Device < 3
LBB31_OPEN_LT_3:
        cmp     #$02
        bne     LBB3B_OPEN_NOT_2

        ;Device 2 RS232
        jsr     ACIA_INIT
        jmp     ACIA_OPEN

LBB3B_OPEN_NOT_2:
        ;Device 1 Virtual 1541
        jmp     L9243_OPEN_V1541

OP175_OPEN_CLC_RTS:
        clc
        rts

; ----------------------------------------------------------------------------
;OPEN to IEC bus
;OPEN_IEC
OPENI:
        lda     SA
        bmi     OP175_OPEN_CLC_RTS  ;no sa...done

        ldy     FNLEN
        beq     OP175_OPEN_CLC_RTS  ;no file name...done

        stz     SATUS         ;clear the serial status

        lda     FA
        jsr     LISTN         ;device la to listen
        bit     SATUS         ;anybody home?
        bmi     UNP           ;nope

        lda     SA
        ora     #$F0
        jsr     SECND

        lda     SATUS         ;anybody home?...get a dev -pres?
        bpl     OP35          ;yes...continue

;  this routine is called by other
;  kernal routines which are called
;  directly by os. kill return
;  address to return to os.
UNP:    pla
        pla
        jmp     ERROR5 ;DEVICE NOT PRESENT

OP35:   lda     FNLEN
        beq     OP45          ;no name...done sequence

;
;  send file name over serial
;
        ldy     #$00
OP40:   lda     #FNADR
        sta     SINNER
        jsr     GO_RAM_LOAD_GO_KERN   ;Get byte from filename
        jsr     CIOUT                 ;Send it to IEC
        iny
        cpy     FNLEN
        bne     OP40
OP45:   jmp     CUNLSN

; ----------------------------------------------------------------------------

SAVEING:jsr     PRIMM80
        .byte   "SAVEING ",0  ;Not "SAVING" like all other CBM computers
        bra     OUTFN

; ----------------------------------------------------------------------------

LUKING: jsr     PRIMM80
        .byte   "SEARCHING FOR ",0
        ;Fall through

; ----------------------------------------------------------------------------

OUTFN:  bit     MSGFLG
        bpl     LBBBF
        ldy     FNLEN
        beq     LBBBC
        ldy     #$00
LBBAB:  lda     #FNADR
        sta     SINNER
        jsr     GO_RAM_LOAD_GO_KERN
        jsr     KR_ShowChar_
        iny
        cpy     FNLEN
        bne     LBBAB
LBBBC:  jmp     CRLF
; ----------------------------------------------------------------------------
LBBBF:  rts
; ----------------------------------------------------------------------------
SAVE__:
        lda     FA
        bne     LBBC7
LBBC4_BAD_DEVICE:
        jmp     ERROR9 ;BAD DEVICE #
; ----------------------------------------------------------------------------
LBBC7:  cmp     #$03
        beq     LBBC4_BAD_DEVICE
        cmp     #$02
        beq     LBBC4_BAD_DEVICE
        ldy     FNLEN
        bne     LBBD7
        jmp     ERROR8 ;MISSING FILE NAME
; ----------------------------------------------------------------------------
LBBD7:  cmp     #$01   ;Virtual 1541?
        bne     LBBE1_SAVE_IEC
        ;Virtual 1541
        jsr     SAVEING ;Print SAVEING then OUTFN
        jmp     L9085_V1541_SAVE
; ----------------------------------------------------------------------------
;SAVE to IEC
LBBE1_SAVE_IEC:
        lda     #$61
        sta     SA
        jsr     OPENI
        jsr     SAVEING ;Print SAVEING then OUTFN

        lda     FA
        jsr     LISTN
        lda     SA
        jsr     SECND
        ldy     #$00

        ;RD300 from C64 KERNAL inlined
        lda     STAH
        sta     SAH
        lda     $B6
        sta     SAL

        lda     SAL
        jsr     CIOUT
        lda     SAH
        jsr     CIOUT

LBC09:  ;CMPSTE from C64 KERNAL inlined
        sec
        lda     SAL
        sbc     EAL
        lda     SAH
        sbc     EAH

        bcs     LBC33
        lda     #SAL
        sta     SINNER
        jsr     GO_RAM_LOAD_GO_KERN
        jsr     CIOUT
        jsr     LFDB9_STOP
        bne     LBC2B
        jsr     CLSEI
        lda     #$00
        sec
        rts
; ----------------------------------------------------------------------------
LBC2B:  inc     SAL
        bne     LBC09
        inc     SAH
        bne     LBC09
LBC33:  jsr     UNLSN
; ----------------------------------------------------------------------------
CLSEI:  bit     SA
        bmi     CLSEI2
        lda     FA
        jsr     LISTN
        lda     SA
        and     #$EF
        ora     #$E0
        jsr     SECND
CUNLSN: jsr     UNLSN
CLSEI2: clc
        rts
; ----------------------------------------------------------------------------
ERROR0: lda     #$00  ;OK
        .byte   $2C
ERROR1: lda     #$01  ;TOO MANY OPEN FILES
        .byte   $2C
ERROR2: lda     #$02  ;FILE OPEN
        .byte   $2C
ERROR3: lda     #$03  ;FILE NOT OPEN
        .byte   $2C
ERROR4: lda     #$04  ;FILE NOT FOUND
        .byte   $2C
ERROR5: lda     #$05  ;DEVICE NOT PRESENT
        .byte   $2C
ERROR6: lda     #$06  ;NOT INPUT FILE
        .byte   $2C
ERROR7: lda     #$07  ;NOT OUTPUT FILE
        .byte   $2C
ERROR8: lda     #$08  ;MISSING FILE NAME
        .byte   $2C
ERROR9: lda     #$09  ;BAD DEVICE #
        .byte   $2C
ERROR16:lda     #$0A  ;OUT OF MEMORY
        pha
        jsr     CLRCH
        bit     MSGFLG
        bvc     EREXIT
        jsr     PRIMM
        .byte   $0d,"I/O ERROR #",0
        pla
        pha
        jsr     PRINT_BCD_NIBS
        jsr     CRLF
EREXIT: pla
        sec
        rts

; ----------------------------------------------------------------------------

;Send TALK to IEC
TALK__:
        ora     #$40          ;A = 0x40 (TALK)
        .byte   $2C           ;Skip next 2 bytes

;Send LISTEN to IEC
LISTN:
        ora     #$20          ;A = 0x20 (LISTEN)

;Send a command byte to IEC
;Start of LIST1 from C64 KERNAL
LIST1:  pha
        bit     C3P0          ;Character left in buf?
        bpl     LIST2         ;No...

        ;Send buffered character
        sec                   ;Set EOI flag
        ror     R2D2
        jsr     ISOUR         ;Send last character
        lsr     C3P0          ;Buffer clear flag
        lsr     R2D2          ;Clear EOI flag

LIST2:  pla                   ;TALK/LISTEN address
        sta     BSOUR         ;Byte buffer for output (FF means no character)
        sei
        jsr     DATAHI        ;Set data line high
        cmp     #$3F          ;CLKHI only on UNLISTEN
        bne     LIST5
        jsr     CLKHI         ;Set clock line high

LIST5:  lda     VIA1_PORTB
        ora     #$08
        sta     VIA1_PORTB    ;Assert ATN (turns VIA PA3 on)

ISOURA: sei
        jsr     CLKLO         ;Set clock line low
        jsr     DATAHI
        jsr     W1MS

;Send last byte to IEC
ISOUR:  sei
        jsr     DATAHI        ;Make sure data is released / Set data line high
        jsr     DEBPIA        ;Data should be low / Debounce VIA PA then ASL A
        bcs     NODEV         ;Branch to device not present error
        jsr     CLKHI         ;Set clock line high

        bit     VIA1_PORTB    ;XXX The C64 KERNAL does not have this
        bvs     NODEV         ;XXX but the TED-series KERNAL does.

        bit     R2D2          ;EOI flag test
        bpl     NOEOI

;Do the EOI
ISR02:  jsr     DEBPIA        ;Wait for DATA to go high / Debounce VIA PA then ASL A
        bcc     ISR02

ISR03:  jsr     DEBPIA        ;Wait for DATA to go low / Debounce VIA PA then ASL A
        bcs     ISR03

NOEOI:  jsr     DEBPIA        ;Wait for DATA high / Debounce VIA PA then ASL A
        bcc     NOEOI
        jsr     CLKLO         ;Set clock line low

        ;Set to send data
        lda     #$08          ;Count 8 bits
        sta     IECCNT

ISR01:  lda     VIA1_PORTB    ;Debounce the bus
        cmp     VIA1_PORTB
        bne     ISR01
        eor     #$C0          ;XXX different from c64 (same change in debpia)
        asl     a             ;Set the flags
        bcc     FRMERR        ;Data must be high
        ror     BSOUR         ;Next bit into carry
        bcs     ISRHI
        jsr     DATALO        ;Set data line low
        bne     ISRCLK

ISRHI:  jsr     DATAHI        ;Set data line high

ISRCLK: jsr     CLKHI         ;Set clock line high
        nop
        nop
        nop
        nop
        lda     VIA1_PORTB
        and     #$DF          ;Data high
        ora     #$10          ;Clock low
        sta     VIA1_PORTB
        dec     IECCNT
        bne     ISR01
        ;XXX VC-1541-DOS first stores in 0 VIA1_T2CL here
        lda     #$04          ;XXX different from C64 (VIA vs CIA)
        sta     VIA1_T2CH
        ;XXX VC-1541-DOS does "lda via_ifr" here before the next line

ISR04:  lda     VIA1_IFR      ;XXX different from C64 (VIA vs CIA)
        and     #$20          ;XXX but same as VC-1541-DOS
        bne     FRMERR        ;XXX
        jsr     DEBPIA        ;Debounce VIA PA then ASL A
        bcs     ISR04
        cli
        rts
; ----------------------------------------------------------------------------
NODEV:  lda     #$80          ;A = SATUS bit for device not present error
        .byte   $2C           ;Skip next 2 bytes

FRMERR: lda     #$03          ;A = SATUS bits timeout during write
                              ;(C64 KERNAL calls this "framing")

;Commodore Serial Bus Error Entry
CSBERR: jsr     UDST          ;KERNAL SATUS = SATUS | A
        cli                   ;IRQ's were off...turn on
        clc                   ;Make sure no KERNAL error returned
        bcc     DLABYE        ;Branch always to turn ATN off, release all lines

;Send secondary address for LISTEN to IEC
SECND:
        sta     BSOUR         ;Buffer character
        jsr     ISOURA        ;Send it

;Release ATN after LISTEN
SCATN:
        lda     VIA1_PORTB
        and     #$F7
        sta     VIA1_PORTB    ;Release ATN
        rts

; ----------------------------------------------------------------------------

;Send secondary address for TALK to IEC
TKSA:
        sta     BSOUR         ;Buffer character
        jsr     ISOURA        ;Send secondary address
LBD5B:  sei                   ;No IRQ's here
        jsr     DATALO        ;Set data line low
        jsr     SCATN         ;Release ATN
        jsr     CLKHI         ;Set clock line high

TKATN1: jsr     DEBPIA        ;Wait for clock to go low / Debounce VIA PA then ASL A
        bmi     TKATN1
        cli                   ;IRQ's okay now
        rts

; ----------------------------------------------------------------------------

;Send a byte to IEC
;Buffered output to IEC
CIOUT:
        bit     C3P0          ;Buffered char?
        bmi     CI2           ;Yes...send last

        sec                   ;No...
        ror     C3P0          ;Set buffered char flag
        bne     CI4           ;Branch always

CI2:    pha                   ;Save current char
        jsr     ISOUR         ;Send last char
        pla                   ;Restore current char

CI4:    sta     BSOUR         ;Buffer current char
        clc                   ;Carry-Good exit
        rts

; ----------------------------------------------------------------------------

;Send UNTALK to IEC
UNTLK:  sei
        jsr     CLKLO         ;Set clock line low
        lda     VIA1_PORTB
        ora     #$08
        sta     VIA1_PORTB    ;Assert ATN (turns VIA PB3 on)
        lda     #$5F          ;A = 0x5F (UNTALK)
        .byte   $2C           ;Skip next 2 bytes

;Send UNLISTEN to IEC
UNLSN:  lda     #$3F          ;A = 0x3F (UNLISTEN)
        jsr     LIST1         ;Send it

;Release all lines
DLABYE: jsr     SCATN         ;Always release ATN

;Delay approx 60 us then release clock and data
DLADLH: txa
        ldx     #10

DLAD00: dex
        bne     DLAD00
        tax
        jsr     CLKHI         ;Set clock line high
                              ;XXX this matches the C64 but VC-1541-DOS stores also 0 in C3P0 here
        jmp     DATAHI        ;Set data line high

; ----------------------------------------------------------------------------

;Read a byte from IEC
;Input a byte from serial bus
ACPTR:  sei                   ;No IRQ allowed
        lda     #$00          ;Set EOI/ERROR Flag
        sta     IECCNT
        jsr     CLKHI         ;Make sure clock line is released / Set clock line high

ACP00A: jsr     DEBPIA        ;Wait for clock high / Debounce VIA PA then ASL A
        bpl     ACP00A

EOIACP: lda     #$01          ;XXX different from C64 (VIA vs CIA)
        sta     VIA1_T2CH     ;VC-1541-DOS also stores 0 in VIA1_T2CL first

        jsr     DATAHI        ;Data line high (Makes timing more like VIC-20) / Set data line high
                              ;XXX VC-1541-DOS does "lda via_ifr" here before the next line

ACP00:  lda     VIA1_IFR      ;XXX Check the timer
        and     #$20          ;XXX different from C64 (VIA vs CIA) but same as VC-1541-DOS
        bne     ACP00B        ;Ran out...
        jsr     DEBPIA        ;Check the clock line / Debounce VIA PA then ASL A
        bmi     ACP00         ;No, not yet
        bpl     ACP01         ;Yes...

ACP00B: lda     IECCNT        ;Check for error (twice thru timeouts)
        beq     ACP00C
        lda     #$02          ;A = SATUS bit for timeout error
        jmp     CSBERR        ;ST = 2 read timeout

;Timer ran out, do an EOI thing
ACP00C: jsr     DATALO        ;Set data line low
        jsr     CLKHI         ;Delay and then set DATAHI (fix for 40us C64) / Set clock line high
        lda     #$40          ;A = SATUS bit for End of File (EOF)
        jsr     UDST          ;KERNAL SATUS = SATUS | A
        inc     IECCNT        ;Go around again for error check on EOI
        bne     EOIACP

;Do the byte transfer
ACP01:  lda     #$08          ;Set up counter
        sta     IECCNT

ACP03:  lda     VIA1_PORTB    ;Wait for clock high
        cmp     VIA1_PORTB    ;Debounce
        bne     ACP03
        eor     #$C0          ;XXX different from C64 (lines inverted)
        asl     a             ;Shift data into carry
        bpl     ACP03         ;Clock still low...
        ror     BSOUR1        ;Rotate data in

ACP03A: lda     VIA1_PORTB    ;Wait for clock low
        cmp     VIA1_PORTB    ;Debounce
        bne     ACP03A
        eor     #$C0          ;XXX different from C64 (lines inverted)
        asl     a
        bmi     ACP03A
        dec     IECCNT
        bne     ACP03         ;More bits...
        ;...exit...
        jsr     DATALO        ;Set data line low
        bit     SATUS         ;Check for EOI
        bvc     ACP04         ;None...

        jsr     DLADLH        ;Delay approx 60 then set data high

ACP04:  lda     BSOUR1
        cli                   ;IRQ is OK
        clc                   ;Good exit
        rts
; ----------------------------------------------------------------------------
CLKHI:
;Set clock line high (allows IEC CLK to be pulled to 5V)
;Write 0 to VIA port bit, so 7406 output is Hi-Z
        lda     VIA1_PORTB
        and     #$EF
        sta     VIA1_PORTB
        rts
; ----------------------------------------------------------------------------
CLKLO:
; Set VIA1 port-B bit#4.
        lda     VIA1_PORTB
        ora     #$10
        sta     VIA1_PORTB
        rts
; ----------------------------------------------------------------------------
DATAHI:
;Set data line high (allows IEC DATA to be pulled up to 5V)
;Write 0 to VIA port bit, so 7406 output is Hi-Z
        lda     VIA1_PORTB
        and     #$DF
        sta     VIA1_PORTB
        rts
; ----------------------------------------------------------------------------
DATALO:
;Set data line low (holds IEC DATA to GND)
;Write 1 to VIA port bit, so 7406 output is GND
        lda     VIA1_PORTB
        ora     #$20
        sta     VIA1_PORTB
        rts
; ----------------------------------------------------------------------------
DEBPIA:
;Debounce VIA PA, invert bits 7 (data in) and 6 (clock in), then ASL A
        lda     VIA1_PORTB
        cmp     VIA1_PORTB
        bne     DEBPIA
        eor     #$C0          ;XXX different from C64 (lines inverted)
        asl     a
        rts
; ----------------------------------------------------------------------------
;Delay 1 ms using loop
W1MS:   txa                   ;Save .X
        ldx     #$B8          ;XXX same as C64 but VC-1541-DOS has $C0 here
W1MS1:  dex                   ;5us loop
        bne     W1MS1
        tax                   ;Restore X
        rts
; ----------------------------------------------------------------------------
;Initialize RS-232 variables and reset ACIA
;AINIT
ACIA_INIT:
        stz     $0389
        stz     $0388
        lda     #$40
        sta     $038A
        lda     #$30
        sta     $038B
        lda     #$10
        sta     $038C
        bra     LBE6C
LBE69:  stz     ACIA_ST       ;programmed reset of the acia
LBE6C:  php
        sei
        stz     $040F
        stz     $0410
        stz     $C3
        stz     $038D
        plp
        rts
; ----------------------------------------------------------------------------
;ACIA interrupt occurred
;Called from default interrupt handler (DEFVEC_IRQ)
;RS-232 related
;Similar to AOUT in TED-series KERNAL
ACIA_IRQ:
        lda     ACIA_ST
        bit     #$10          ;Bit 4 = Transmit Data Register Empty (0=not empty, 1=empty)
        beq     TXNMT_AIN     ;tx reg is busy
        ldx     $040E
        lda     #$40
        bit     $C3
        bne     LBE9A
        lda     #$20
        bit     $C3
        bne     TXNMT_AIN
        ldx     $040D
        lda     #$80
        bit     $C3
        beq     TXNMT_AIN
LBE9A:  stx     ACIA_DATA
        trb     $C3
        cpx     #$00
        beq     TXNMT_AIN
        lda     #$10
        cpx     $0388
        bne     TRYCS
        tsb     $C3
        bra     TXNMT_AIN
TRYCS:  cpx     $0389
        bne     TXNMT_AIN
        trb     $C3

;Similar to AIN in TED-series KERNAL
TXNMT_AIN:
        lda     ACIA_ST
        bit     #$08
        beq     RXFULL
        ldx     ACIA_DATA     ;X = byte received from ACIA
        and     #$07          ;Bit 0,1,2 = Error Flags (Parity, Framing, Overrun)
        bne     LBECE         ;Branch if an error occurred
        ;No receive error
        cpx     #0
        beq     LBED9_GOT_NULL
        lda     #' '
        cpx     $0388
        bne     LBED1
LBECE:  tsb     $C3
        rts
LBED1:  cpx     $0389
        bne     LBED9_GOT_NULL
        trb     $C3
        rts

LBED9_GOT_NULL:
        ldy     $038D
        cpy     $038A
        bcs     RXFULL
        inc     $038D
        cpy     $038B
        bcc     LBEFB
        ldy     $0388
        beq     LBEFB
        lda     #$10
        bit     $C3
        bne     LBEFB
        sty     $040E
        lda     #$40
        tsb     $C3
LBEFB:  txa
        ldx     $040F
        bne     LBF04
        ldx     $038A
LBF04:  dex
        sta     MEM_04C0,x
        stx     $040F
RXFULL:  rts
; ----------------------------------------------------------------------------
;CHROUT to RS-232
ACIA_CHROUT:
        tax
LBF0D:  lda     MODKEY
        lsr     a ;Bit 0 = MOD_STOP
        bit     $C3
        bpl     LBF16
        bcc     LBF0D
LBF16:  stx     $040D
        lda     #$80
        tsb     $C3
        rts
; ----------------------------------------------------------------------------
;Get byte from RS-232 input buffer
AGETCH: ldy     $038D
        tya
        beq     LBF4D_CHKIN_ACIA
        dec     $038D
        ldx     $0389
        beq     LBF3E
        cpy     $038C
        bcs     LBF3E
        lda     #$10
        bit     $C3
        beq     LBF3E
        stx     $040E
        lda     #$40
        tsb     $C3
LBF3E:  ldx     $0410
        bne     LBF46
        ldx     $038A
LBF46:  dex
        lda     MEM_04C0,x
        stx     $0410
LBF4D_CHKIN_ACIA:
        clc
        rts
; ----------------------------------------------------------------------------
;Updates time-of-day (TOD) clock.
;Called at 60 Hz by the default IRQ handler (see LFA44_VIA1_T1_IRQ).
UDTIM__:dec     JIFFIES
        bpl     UDTIM_RTS

        ;JIFFIES=0 which means 1 second has elapsed

        ;Reset jiffies for next time
        lda     #59
        sta     JIFFIES

        ;Increment seconds
        lda     #59
        inc     TOD_SECS
        cmp     TOD_SECS
        bcs     UDTIM_UNKNOWN

        ;Seconds rolled over
        ;Seconds=0, Increment minutes
        stz     TOD_SECS
        inc     TOD_MINS
        cmp     TOD_MINS
        bcs     UDTIM_UNKNOWN

        ;Minutes rolled over
        ;Minutes=0, Increment Hours
        stz     TOD_MINS
        inc     TOD_HOURS
        lda     #23
        cmp     TOD_HOURS
        bcs     UDTIM_UNKNOWN

        ;Hours rolled over
        ;Hours=0
        stz     TOD_HOURS

;TODO UNKNOWN_MINS / UNKNOWN_SECS are some kind of countdown, maybe for timeouts
UDTIM_UNKNOWN:
        ;Do nothing if both are zero
        lda     UNKNOWN_SECS
        ora     UNKNOWN_MINS
        beq     UDTIM_ALARM
        ;Decrement secs/mins
        dec     UNKNOWN_SECS
        bpl     UDTIM_ALARM
        ldx     #59
        stx     UNKNOWN_SECS
        dec     UNKNOWN_MINS

;Locations ALARM_HRS, ALARM_MINS, and ALARM_SECS count down the time remaining
;until an alarm sounds.  3 beeps sound in the final seconds of the countdown.
UDTIM_ALARM:
        ;Check if it's time to beep the alarm
        lda     ALARM_SECS
        and     #%11111100
        ora     ALARM_MINS
        ora     ALARM_HOURS
        bne     UDTIM_ALARM_DECR
        ;Beep or pause between beeps
        lda     ALARM_SECS
        beq     UDTIM_RTS
        jsr     BELL
UDTIM_ALARM_DECR:
        ;Decrement alarm secs/mins/hours
        dec     ALARM_SECS
        bpl     UDTIM_RTS
        lda     #59
        sta     ALARM_SECS
        dec     ALARM_MINS
        bpl     UDTIM_RTS
        sta     ALARM_MINS
        dec     ALARM_HOURS
UDTIM_RTS:
        rts
; ----------------------------------------------------------------------------
LBFBE:  php
        sei
        stz     UNKNOWN_SECS
        lda     $0780
        bne     LBFC9
        dec     a
LBFC9:  sta     UNKNOWN_MINS
        plp
        rts
; ----------------------------------------------------------------------------
LBFCE_SETTIM:
        sei
        lda     TOD_HOURS
        ldx     TOD_MINS
        ldy     TOD_SECS
        ;Fall through into LBFD8_RDTIM
; ----------------------------------------------------------------------------
LBFD8_RDTIM:
        sei
        sta     TOD_HOURS
        stx     TOD_MINS
        sty     TOD_SECS
        cli
        rts
; ----------------------------------------------------------------------------
WaitXticks_:
; Waits for multiple of 1/60 seconds. Interrupt must be enabled, since it
; used TOD's 1/60 val.
; Input: X = number of 1/60 seconds.
        pha
LBFE5:  lda     JIFFIES
LBFE8:  cmp     JIFFIES
        beq     LBFE8
        dex
        bpl     LBFE5
        pla
        rts
; ----------------------------------------------------------------------------
LBFF2:  pha
        phx
        phy
        jsr     LC009_CHECK_MODKEY_AND_UNKNOWN_SECS_MINS
        bcc     LBFFD
        jsr     L84C5
LBFFD:  lda     $0335
        beq     LC005
        jsr     LFA78
LC005:  ply
        plx
        pla
        rts
; ----------------------------------------------------------------------------
LC009_CHECK_MODKEY_AND_UNKNOWN_SECS_MINS:
        lda     MODKEY
        and     #MOD_BIT_7 + MOD_BIT_5
        tax
        php
        sei
        lda     UNKNOWN_SECS
        ora     UNKNOWN_MINS
        bne     LC019
        inx
LC019:  plp
        txa
        cmp     #$01
        rts

; ----------------------------------------------------------------------------

DTMF_CHAR_TO_T1CL_VALUE_INDEX:
        ;Ordered by chars: "0123456789#*"
        ;Each entry is an offset to the T1CL_VALUES table
        .byte   $01,$00,$01,$02,$00,$01,$02,$00,$01,$02,$00,$02
DTMF_T1CL_VALUES:
        .byte   $9D,$76,$51

DTMF_DIGIT_TO_LOOP_COUNTS_INDEX:
        ;Ordered by chars: "0123456789#*"
        ;Each entry is an offset to the two loop iteration tables
        .byte   $03,$00,$00,$00,$01,$01,$01,$02,$02,$02,$03,$03
DTMF_OUTER_LOOP_COUNTS:
        .byte   $8B,$9A,$AA,$BC
DTMF_INNER_DELAY_LOOP_COUNTS:
        .byte   $8C,$7E,$72,$67

;Play the DTMF tone for a character
;This is used to dial the telephone for the modem
;Called with one of these characters in A: 0123456789#*
DTMF_PLAY_TONE_FOR_CHAR:
        ldx     #$09
        jsr     WaitXticks_
        php
        sei
        cmp     #'#'
        bne     DTMF_PLAY_NOT_POUND
LC04C:  lda     #$0b
DTMF_PLAY_NOT_POUND:
        and     #$0f
        tax

        lda     #$C0
        tsb     VIA2_ACR

        ldy     DTMF_CHAR_TO_T1CL_VALUE_INDEX,x
        lda     DTMF_T1CL_VALUES,y
        sta     VIA2_T1CL
        lda     #$01
        sta     VIA2_T1CH

        ldy     DTMF_DIGIT_TO_LOOP_COUNTS_INDEX,x
        ldx     DTMF_OUTER_LOOP_COUNTS,y

DTMF_PLAY_OUTER_LOOP:
        lda     VIA2_PORTB
        eor     #$01          ;PB1 turns DTMF generator circuit on/off
        sta     VIA2_PORTB

        lda     DTMF_INNER_DELAY_LOOP_COUNTS,y
DTMF_PLAY_INNER_DELAY_LOOP:
        dec     a
        bne     DTMF_PLAY_INNER_DELAY_LOOP

        dex
        bne     DTMF_PLAY_OUTER_LOOP

        lda     #$C0
        trb     VIA2_ACR
        plp
        rts

; ----------------------------------------------------------------------------

;OPEN the ACIA
ACIA_OPEN:
        lda     #FNADR
        sta     SINNER
        ldx     FNLEN ;FNLEN = 0?
        beq     LC0A6_CLC_RTS
        stz     ACIA_ST
        ldy     #$00
        jsr     GO_RAM_LOAD_GO_KERN
        sta     ACIA_CTRL                 ;First char -> ACIA_CTRL
        cpx     #$01 ;FNLEN = 1?
        beq     LC0A6_CLC_RTS
        iny
        jsr     GO_RAM_LOAD_GO_KERN
        cpx     #$02 ;FNLEN = 2?
        bne     LC0A8_FNLEN_GT_2
        sta     ACIA_CMD                  ;Second char -> ACIA_CMD
LC0A6_CLC_RTS:
        clc
        rts

;FNLEN > 2
LC0A8_FNLEN_GT_2:
        and     #$E0
        sta     ACIA_CMD                  ;Second char & $E0 -> ACIA_CMD

        jsr     LC193_VIA2_PB1_OFF
        jsr     LC1A1_ACIA_DTR_HI_ENABLE_RX_TX
        jsr     LC1AD_VIA2_PB4_ON

        ldy     #$02
        jsr     GO_RAM_LOAD_GO_KERN       ;A = third char

        cmp     #$41 ;'A'
        beq     LC0C3_GOT_A

        cmp     #$41 ;'A' again (weird)
        bne     LC0CE_NOT_A

LC0C3_GOT_A:
        jsr     LC1DF_LOOP_78_WHILE_WAITING_FOR_ACIA_DCD_OR_DSR_OR_STOP_KEY
        bcs     LC0E3_ERROR ;Timeout or STOP pressed
        jsr     LC1BB_ACIA_CMD_BIT_2_ON_WAIT_2_TICKS_CLC ;TODO probably phone on hook or off hook
        jmp     LC1B4_VIA2_PB4_OFF

LC0CE_NOT_A:
        jsr     LC1BB_ACIA_CMD_BIT_2_ON_WAIT_2_TICKS_CLC ;TODO probably phone on hook or off hook
        jsr     LC189_WAIT_76_TICKS_CLC
        lda     #$02
        jsr     DIAL_CHARS_IN_ACIA_FILENAME
        bcs     LC0E3_ERROR
        jsr     DIAL_CHAR_HANDLER_W_WAITS_FOR_ACIA_DCD_OR_DSR_OR_STOP_KEY
        bcs     LC0E3_ERROR
        jmp     LC1B4_VIA2_PB4_OFF

LC0E3_ERROR:
        lda     LA
        jmp     LFCF1_APPL_CLOSE

; ----------------------------------------------------------------------------

;CLOSE the ACIA
ACIA_CLOSE:
        php
        sei
        jsr     ACIA_INIT
        plp
        jmp     LC200_VIA2_PB4_OFF_ACIA_BITS_OFF_VIA2_PB1_ON_JMP_UDST

; ----------------------------------------------------------------------------

;Dial the phone number in the filename passed to OPEN
DIAL_CHARS_IN_ACIA_FILENAME:
        pha
        and     #$7F
        cmp     FNLEN
        bcc     LC0FC
        pla
        clc
        rts
LC0FC:  tay
        jsr     GO_RAM_LOAD_GO_KERN ;A = next byte from filename (number to dial?)
        jsr     LC110_DIAL_CHAR
        jsr     LC1F0_ACIA_CMD_BIT_2_OFF_WAIT_THEN_BACK_ON
        pla
        inc     a
        bcs     LC10F_RTS
        lda     MODKEY
        lsr     a ;Bit 0 = MOD_STOP
        bcc     DIAL_CHARS_IN_ACIA_FILENAME ;Keep going unless STOP pressed
LC10F_RTS:
        rts

;Dial one digit of the phone number in the ACIA filename
LC110_DIAL_CHAR:
        bit     #$40
        beq     LC116_FIND_CHAR
        and     #$DF
LC116_FIND_CHAR:
        ldy     #$0F
LC118_FIND_CHAR_LOOP:
        cmp     DIAL_CHARS,y
        bne     LC123_KEEP_GOING
        ldx     DIAL_CHAR_HANDLER_OFFSETS,y
        jmp     (DIAL_CHAR_HANDLERS,x)
LC123_KEEP_GOING:
        dey
        bpl     LC118_FIND_CHAR_LOOP
        clc
        rts

DIAL_CHARS:
        .byte   "0123456789#*RTW,"

DIAL_CHAR_HANDLER_OFFSETS:
        .byte   $00 ;0 -> DIAL_CHAR_HANDLER_0_TO_9_POUND_STAR
        .byte   $00 ;1
        .byte   $00 ;2
        .byte   $00 ;3
        .byte   $00 ;4
        .byte   $00 ;5
        .byte   $00 ;6
        .byte   $00 ;7
        .byte   $00 ;8
        .byte   $00 ;9
        .byte   $00 ;#
        .byte   $00 ;*
        .byte   $02 ;R -> DIAL_CHAR_HANDLER_R
        .byte   $04 ;T -> DIAL_CHAR_HANDLER_T
        .byte   $06 ;W -> DIAL_CHAR_HANDLER_W_WAITS_FOR_ACIA_DCD_OR_DSR_OR_STOP_KEY
        .byte   $08 ;, -> DIAL_CHAR_HANDLER_COMMA_WAITS_3B_TICKS_CLC

DIAL_CHAR_HANDLERS:
        .addr   DIAL_CHAR_HANDLER_0_TO_9_POUND_STAR
        .addr   DIAL_CHAR_HANDLER_R
        .addr   DIAL_CHAR_HANDLER_T
        .addr   DIAL_CHAR_HANDLER_W_WAITS_FOR_ACIA_DCD_OR_DSR_OR_STOP_KEY
        .addr   DIAL_CHAR_HANDLER_COMMA_WAITS_3B_TICKS_CLC

;Dial a "T" in ACIA device OPEN filename
DIAL_CHAR_HANDLER_T:
        tsx
        lda     stack+3,x
        ora     #$80
        bra     LC160

;Dial a "R" in ACIA device OPEN filename
DIAL_CHAR_HANDLER_R:
        tsx
        lda     stack+3,x
        and     #$7F
LC160:  sta     stack+3,x
        clc
        rts

;Dial a "0"-"9", "#", and "*" in ACIA device OPEN filename
;Dial the character with a DTMF tone or rotary pulses
DIAL_CHAR_HANDLER_0_TO_9_POUND_STAR:
        tsx
        ldy     stack+3,x
        bpl     PULSE_DIAL_CHAR
        jsr     DTMF_PLAY_TONE_FOR_CHAR
        clc
        rts
PULSE_DIAL_CHAR:
        cmp     #'0'
        bcc     LC188_RTS
        and     #$0F
        bne     PULSE_DIAL_LOOP
        lda     #$0A
PULSE_DIAL_LOOP:
        pha
        jsr     LC1C4_ACIA_CMD_BIT_2_OFF_WAIT_4_TICKS_CLC ;TODO probably phone on hook or off hook
        jsr     LC1BB_ACIA_CMD_BIT_2_ON_WAIT_2_TICKS_CLC ;TODO probably phone on hook or off hook
        pla
        dec     a
        bne     PULSE_DIAL_LOOP
        jsr     DIAL_CHAR_HANDLER_COMMA_WAITS_3B_TICKS_CLC
LC188_RTS:
        rts

LC189_WAIT_76_TICKS_CLC:
        jsr     DIAL_CHAR_HANDLER_COMMA_WAITS_3B_TICKS_CLC

;Dial a "," in ACIA device OPEN filename
DIAL_CHAR_HANDLER_COMMA_WAITS_3B_TICKS_CLC:
        ldx     #$3B
WAIT_X_TICKS_CLC:
        jsr     WaitXticks_
        clc
        rts

; ----------------------------------------------------------------------------
LC193_VIA2_PB1_OFF:
        lda     #$02
        trb     VIA2_PORTB
        bra     DIAL_CHAR_HANDLER_COMMA_WAITS_3B_TICKS_CLC
; ----------------------------------------------------------------------------
LC19A_VIA2_PB1_ON:
        lda     #$02
        tsb     VIA2_PORTB
        clc
        rts
; ----------------------------------------------------------------------------
LC1A1_ACIA_DTR_HI_ENABLE_RX_TX:
        lda     #$01
        tsb     ACIA_CMD
        rts
; ----------------------------------------------------------------------------
LC1A7_ACIA_DTR_LO_DISABLE_RX_TX:
        lda     #$01
        trb     ACIA_CMD
        rts
; ----------------------------------------------------------------------------
LC1AD_VIA2_PB4_ON:
        lda     #$08
        tsb     VIA2_PORTB
        clc
        rts
; ----------------------------------------------------------------------------
LC1B4_VIA2_PB4_OFF:
        lda     #$08
        trb     VIA2_PORTB
        clc
        rts
; ----------------------------------------------------------------------------
LC1BB_ACIA_CMD_BIT_2_ON_WAIT_2_TICKS_CLC: ;TODO probably phone on hook or off hook
        lda     #$04
        tsb     ACIA_CMD
        ldx     #$02
        bra     WAIT_X_TICKS_CLC
; ----------------------------------------------------------------------------
LC1C4_ACIA_CMD_BIT_2_OFF_WAIT_4_TICKS_CLC: ;TODO probably phone on hook or off hook
        lda     #$04
        trb     ACIA_CMD
        ldx     #$04
        bra     WAIT_X_TICKS_CLC
; ----------------------------------------------------------------------------
DIAL_CHAR_HANDLER_W_WAITS_FOR_ACIA_DCD_OR_DSR_OR_STOP_KEY:
        lda     ACIA_ST
        bit     #%00100000 ;Bit 5 DCD Carrier Detect (0=carrier, 1=no carrier)
        beq     DIAL_CHAR_HANDLER_COMMA_WAITS_3B_TICKS_CLC
        bit     #%01000000 ;Bit 6 DSR Data Set Ready (0=ready, 1=no ready)
        bne     LC1DD_SEC_RTS
        lda     MODKEY
        lsr     a ;Bit 0 = MOD_STOP
        bcc     DIAL_CHAR_HANDLER_W_WAITS_FOR_ACIA_DCD_OR_DSR_OR_STOP_KEY ;Keep waiting if STOP not pressed
LC1DD_SEC_RTS:
        sec
        rts
; ----------------------------------------------------------------------------
LC1DF_LOOP_78_WHILE_WAITING_FOR_ACIA_DCD_OR_DSR_OR_STOP_KEY:
        ldy     #$78
LC1E1_LOOP:
        ldx     #$01
        jsr     WaitXticks_
        jsr     DIAL_CHAR_HANDLER_W_WAITS_FOR_ACIA_DCD_OR_DSR_OR_STOP_KEY
        bcc     LC1EF_RTS
        dey
        bne     LC1E1_LOOP
        sec
LC1EF_RTS:
        rts
; ----------------------------------------------------------------------------
LC1F0_ACIA_CMD_BIT_2_OFF_WAIT_THEN_BACK_ON:
        lda     #$04
        trb     ACIA_CMD
        lda     #T0+1
LC1F7_LOOP:
        dec     a
        bne     LC1F7_LOOP
        lda     #$04
        tsb     ACIA_CMD
        rts
; ----------------------------------------------------------------------------
;Called only from ACIA_CLOSE
LC200_VIA2_PB4_OFF_ACIA_BITS_OFF_VIA2_PB1_ON_JMP_UDST:
        jsr     LC1B4_VIA2_PB4_OFF
        jsr     LC1C4_ACIA_CMD_BIT_2_OFF_WAIT_4_TICKS_CLC ;TODO probably phone on hook or off hook
        jsr     LC1A7_ACIA_DTR_LO_DISABLE_RX_TX
        jsr     LC19A_VIA2_PB1_ON
        lda     #$80 ;maybe BREAK detected?
        jmp     UDST

; ----------------------------------------------------------------------------

LC211_RTC_REGISTERS:
      .assert (* - LC211_RTC_REGISTERS) = RTC_HOURS, error
      .byte $04   ;$04=H1
                  ;$05=H10

      .assert (* - LC211_RTC_REGISTERS) = RTC_MINUTES, error
      .byte $02   ;$02=MI1
                  ;$03=MI10

      .assert (* - LC211_RTC_REGISTERS) = RTC_SECONDS, error
      .byte $00   ;$00=S1
                  ;$01=S10

      .assert (* - LC211_RTC_REGISTERS) = RTC_24H_AMPM, error
      .byte $04   ;$04=H1
                  ;$05=H10

      .assert (* - LC211_RTC_REGISTERS) = RTC_DOW, error
      .byte $06   ;$06=W
                  ;$07=don't care

      .assert (* - LC211_RTC_REGISTERS) = RTC_DAY, error
      .byte $07   ;$07=D1
                  ;$08=D10

      .assert (* - LC211_RTC_REGISTERS) = RTC_MONTH, error
      .byte $09   ;$09=MO1
                  ;$0A=MO10

      .assert (* - LC211_RTC_REGISTERS) = RTC_YEAR, error
      .byte $0B   ;$0B=Y1
                  ;$0C=Y10

RTC_DATA_SIZE = * - LC211_RTC_REGISTERS

; ----------------------------------------------------------------------------
;OPEN to RTC device 31
;
;OPENing the RTC device makes the RTC hardware available for reading via CHRIN
;or for synchronizing with the software TOD clock via CHROUT.  OPEN will also
;set the RTC hardware time when passed a filename with 8 bytes of time data.
RTC_OPEN:
        stz     RTC_IDX
        lda     FNLEN
        beq     LC22A               ;No filename just opens
        cmp     #$08
        beq     RTC_SET_FROM_OPEN   ;Filename of 8 bytes sets time
        lda     #$01                ;Any other length is an error
        jsr     UDST
LC22A:  clc
        rts

;Set RTC from 8 bytes of time data in filename
RTC_SET_FROM_OPEN:
        lda     #FNADR
        sta     SINNER

        ;Copy the 8 bytes of the filename into RTC_DATA
        ldy     #RTC_DATA_SIZE-1
LC233_LOOP:
        jsr     GO_RAM_LOAD_GO_KERN ;Get byte from filename
        sta     RTC_DATA,y
        dey
        bpl     LC233_LOOP

        lda     RTC_DATA+RTC_24H_AMPM
        ror     a
        ror     a
        ror     a
        and     #%11000000
        ora     RTC_DATA
        sta     RTC_DATA+RTC_24H_AMPM

        jsr     RTC_ENABLE ;Enable RTC (clears data & control, then CS2=1)
        lda     #%10000000
        tsb     VIA2_PORTA
        php
        sei
        ldy     #$0E ;RTC register $0e??? TODO what is this???
        lda     #%01000000 ;PB6 = RTC Address Write (AW)
        jsr     RTC_SET_AND_CLEAR_BITS
        ldx     #RTC_MINUTES
LC25D_LOOP:
        lda     RTC_DATA,x
        jsr     RTC_WRITE_BYTE_TO_HW
        inx
        cpx     #RTC_DATA_SIZE
        bne     LC25D_LOOP
        jsr     RTC_DISABLE ;Disable RTC (CS2=0)
        plp
        stz     RTC_IDX
        clc
        rts
; ----------------------------------------------------------------------------
;CHROUT to RTC device 31
;
;Sending any character to the RTC device will read the hardware RTC time
;and set the software TOD clock (TI$) to it.  The character sent is ignored.
RTC_CHROUT:
        jsr     RTC_READ_ALL_RTC_DATA_FROM_HW ;Read 8 bytes of time data from the RTC into RTC_DATA
        php
        sei
        sed
        lda     RTC_DATA
        ldx     RTC_DATA+RTC_24H_AMPM
        bne     LC287
        cmp     #$12
        bne     LC290
        lda     #$00
        bra     LC290
LC287:  dex
        bne     LC290
        cmp     #$12
        beq     LC290
        adc     #$12

LC290:  jsr     RTC_SHIFT_LOOKUP_SUBTRACT
        sta     TOD_HOURS

        lda     RTC_DATA+RTC_MINUTES
        jsr     RTC_SHIFT_LOOKUP_SUBTRACT
        sta     TOD_MINS

        lda     RTC_DATA+RTC_SECONDS
        jsr     RTC_SHIFT_LOOKUP_SUBTRACT
        sta     TOD_SECS

        stz     RTC_IDX
        plp
        rts
; ----------------------------------------------------------------------------
;CHRIN from RTC device 31
;
;Reading a character from the RTC device will read the RTC hardware and return
;8 bytes of time data followed by a carriage return ($0D).  The software TOD
;clock is not affected.  Reading past the CR will read the RTC hardware again
;and return new time data.
RTC_CHRIN:
        ldx     RTC_IDX
        beq     RTC_READ_ALL_RTC_DATA_AND_GET_FIRST_VALUE
        cpx     #RTC_DATA_SIZE
        bcc     RTC_GET_NEXT_VALUE
        lda     #$0D ;Carriage return
        stz     RTC_IDX
        clc
        rts
; ----------------------------------------------------------------------------
RTC_READ_ALL_RTC_DATA_AND_GET_FIRST_VALUE:
        jsr     RTC_READ_ALL_RTC_DATA_FROM_HW
        stz     RTC_IDX
        ;Fall through
; ----------------------------------------------------------------------------
RTC_GET_NEXT_VALUE:
        ldx     RTC_IDX
        lda     RTC_DATA,x
        inc     RTC_IDX
        clc
        rts
; ----------------------------------------------------------------------------
;Read 8 bytes of time data from the RTC chip into RTC_DATA
RTC_READ_ALL_RTC_DATA_FROM_HW:

        ;Read 8 bytes of data from the RTC

        jsr     RTC_ENABLE ;Enable RTC (clears data & control, then CS2=1)
        ldx     #RTC_DATA_SIZE-1
LC2D3_LOOP:
        jsr     RTC_READ_BYTE_FROM_HW
        sta     RTC_DATA,x
        dex
        bpl     LC2D3_LOOP
        jsr     RTC_DISABLE ;Disable RTC (CS2=0)

        ;Compare the 8 bytes we just read by reading them again.  If they
        ;don't read back the same, start over from the top.  This is needed
        ;because the time might change mid-reading, and a rollover (e.g. minutes
        ;into hours) would give an invalid time.

        jsr     RTC_ENABLE ;Enable RTC (clears data & control, then CS2=1)
        ldx     #RTC_DATA_SIZE-1
LC2E4_LOOP:
        jsr     RTC_READ_BYTE_FROM_HW
        cmp     RTC_DATA,x
        bne     RTC_READ_ALL_RTC_DATA_FROM_HW
        dex
        bne     LC2E4_LOOP
        jsr     RTC_DISABLE ;Disable RTC (CS2=0)

        lda     RTC_DATA+RTC_HOURS
        and     #%00111111
        sta     RTC_DATA+RTC_HOURS

        lda     RTC_DATA+RTC_DOW
        and     #%00001111
        sta     RTC_DATA+RTC_DOW

        lda     RTC_DATA+RTC_24H_AMPM
        rol     a
        rol     a
        rol     a
        and     #%00000011
        sta     RTC_DATA+RTC_24H_AMPM

        rts
; ----------------------------------------------------------------------------
;X = offset to LC211_RTC_REGISTERS table
;Returns value in A
RTC_READ_BYTE_FROM_HW:
        ldy     LC211_RTC_REGISTERS,x ;Y = RTC register number
        phy
        jsr     RTC_READ_REGISTER_NIB ;Read low nibble into bits 3-0 of A
        sta     RTC_IDX               ;Store low nibble temporarily
        ply
        iny                           ;Increment to next RTC register number
        jsr     RTC_READ_REGISTER_NIB ;Read high nibble into bits 3-0 of A
        asl     a                     ;Rotate into high nibble of A
        asl     a
        asl     a
        asl     a
        ora     RTC_IDX               ;Add low nibble
        rts

; ----------------------------------------------------------------------------

RTC_WRITE_BYTE_TO_HW:
        pha
        and     #$0F
        ldy     LC211_RTC_REGISTERS,x
        jsr     LC337_RTC_WRITE_REGISTER_NIB
        pla
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        ldy     LC211_RTC_REGISTERS,x
        iny
        ;Fall through

LC337_RTC_WRITE_REGISTER_NIB:
        pha
        lda     #%01000000 ;PA6 = RTC Address Write (AW)
        jsr     RTC_SET_AND_CLEAR_BITS
        ply
        lda     #%00100000 ;PA5 = RTC Write (WR)
        ;Fall through

; ----------------------------------------------------------------------------
;Called with RTC register number in Y
;Called with bits to strobe high->low in A
;	pa7 = rtc "stop"
;	pa6 = rtc "address write"
;	pa5 = rtc "write"
;	pa4 = rtc "read"
;	pa0-3 = rtc data
;Set RTC bits in Y, then Set->Clear RTC bits in A
RTC_SET_AND_CLEAR_BITS:
        pha

        lda     #%01111111
        trb     VIA2_PORTA ;Clear all bits except for STOP

        tya
        tsb     VIA2_PORTA ;Set bits specified by Y (RTC register number)

        pla
        tsb     VIA2_PORTA ;Set bits specified by A (assert RTC control signals)
        trb     VIA2_PORTA ;Clear bits specified by A (release RTC control signals)
        rts
; ----------------------------------------------------------------------------
; Read a register from the RTC chip.  A register is a nibble.
; $40 is for AW (address write) signal for the RTC.
; Input: Y = RTC register number
; Output: A = nibble read
RTC_READ_REGISTER_NIB:
        lda     #%01000000 ;PB6 = RTC Address Write (AW)
        jsr     RTC_SET_AND_CLEAR_BITS
        lda     #%00011111
        tsb     VIA2_PORTA
        ldy     VIA2_PORTA
        trb     VIA2_PORTA
        tya
        and     #$0F
        rts
; ----------------------------------------------------------------------------
;Enable RTC (clears data & control, then CS2=1)
RTC_ENABLE:
        stz     VIA2_PORTA ;PA7=0 Don't care (not connected to MSM58321)
                           ;PA6=0 MSM58321 AW (Address Write)
                           ;PA5=0 MSM58321 WR (Write)
                           ;PA4=0 MSM58321 RD (Read)
                           ;PA3=0 MSM58321 DATA3
                           ;PA2=0 MSM58321 DATA2
                           ;PA1=0 MSM58321 DATA1
                           ;PA0=0 MSM58321 DATA0

        lda     #%00000010 ;PB1=RTCEN
        tsb     VIA1_PORTB ;Set PB1=1 to set MSM58321 CS1=1 (RTC enabled)
        rts
; ----------------------------------------------------------------------------
;Disable RTC (CS2=0)
RTC_DISABLE:
        lda     #%00000010 ;PB1=RTCEN
        trb     VIA1_PORTB ;Set PB1=0 to set MSM58321 CS1=0 (RTC disabled)
        rts
; ----------------------------------------------------------------------------
;Used for converting RTC values to TOD values
RTC_SHIFT_LOOKUP_SUBTRACT:
        pha
        lsr     a
        lsr     a
        lsr     a
        lsr     a
        tay
        pla
        cld
        sec
        sbc     LC382,y
        rts
LC382:  .byte 0, 6, 12, 18, 24, 30, 36, 42, 48, 54

; ----------------------------------------------------------------------------
;CHROUT to Centronics
;Wait for /BUSY to go high, or STOP key pressed, or timeout
;Returns carry=1 if error (STOP or timeout)
;
;XXX CLCD Version Differences
;
;  /BUSY
;   - On Bil Herd's prototype (this firmware), Centronics /BUSY is PB6.
;   - On the schematics, PB6 is Barcode Data In and Centronics /BUSY is PB2.
;
;  74HC374 CP
;   - On Bil Herd's prototype (this firmware), 74HC374 CP is PB5.
;   - On the schematics, PB5 is Modem-related and 74HC374 CP is PB1.
;
CENTRONICS_CHROUT:
        ldx     SATUS
        bne     LC3AC

        pha                 ;Save byte to send
        ldy     #$F0        ;Y = number of loops before timeout
LC393:  lda     VIA2_PORTB
        and     #%01000000  ;PB6 = Centronics /BUSY input (XXX See "version differences" above)
        bne     LC3B0       ;Branch if /BUSY=high

        lda     MODKEY
        lsr     a           ;Bit 0 = MOD_STOP
        lda     #$00
        bcs     LC3AB       ;Return early if pressed

        ldx     #$01
        jsr     WaitXticks_

        dey
        bne     LC393 ;Loop until timeout

        lda     #$01
LC3AB:  plx
LC3AC:  sec
        jmp     UDST

;/BUSY has gone high, so send the byte now
;Always returns carry=0 (OK)
LC3B0:  ldx     #$03
LC3B2:  dex
        bpl     LC3B2 ;delay a bit after /BUSY=1

        ;PA0-7 = 74HC374 inputs 0-7 (data lines)
        pla                 ;A = byte to send
        sta     VIA2_PORTA  ;Put byte on 74HC374 input lines

        ;Pulse 74HC374 CP low->high so 74HC374 latches its input lines.
        ;This puts the byte on the Centronics data lines.
        ;PB5 = 74HC374 CP input (XXX See "version differences" above)
        lda     #%00100000
        trb     VIA2_PORTB  ;PB5 = low
        tsb     VIA2_PORTB  ;PB5 = high (74HC374 latches on rising edge)

        ;Pulse Centronics /STB high -> low.
        ;This signals the printer that a byte is ready on the data lines.
        ;CA2 = Centronics /STB
        lda     #$02
        tsb     VIA2_PCR    ;CA2 = high
        trb     VIA2_PCR    ;CA2 = low (Centronics latches on falling edge)

        clc
        rts

; ----------------------------------------------------------------------------

;Translate a character received from the ACIA RX
;Called with A = char, X = channel (from secondary address & $0F)
;Channel number specifies translation mode (0-6, 0=no translation)
;Returns translated char in A, destroys X, preserves Y
;        carry set if channel number is bad, otherwise carry clear.
TRANSL_INCOMING_CHAR:
        pha     ;Push original char onto stack
        lda     LC44A_INCOMING_CHAR_OFFSETS_TABLE_POS-1,x
        bra     TRANSLATE

;Translate a character before sending it to ACIA TX or Centronics
;Called with A = char, X = channel (from secondary address & $0F)
;Returns translated char in A, destroys X, preserves Y
;Channel number specifies translation mode (0-6, 0=no translation)
;Returns translated char in A, destroys X, preserves Y
;        carry set if channel number is bad, otherwise carry clear.
TRANSL_OUTGOING_CHAR:
        pha     ;Push original char onto stack
        lda     TRANSL_OUTGOING_CHAR_OFFSETS_TABLE_POS-1,x
        ;Fall through

;Translate a character
;Called with:
; A = starting index to TRANSL_HANDLER_OFFSETS table
; X = Channel number
; Byte on top of stack is original char to translate
;Returns:
; A = translated character
; carry = set if bad channel (not 0-6), otherwise carry clear
TRANSLATE:
        cpx     #$00 ;Channel = 0?
        bne     LC3DC_NONZERO
        clc                   ;Carry clear = channel ok
LC3DA_NO_CHANGE:
        pla                   ;Pull original character off stack
        rts

LC3DC_NONZERO:
        cpx     #$07
        bcs     LC3DA_NO_CHANGE  ;Branch if channel number >= 7 (carry set = bad channel)

        ;Channel number is 1-6
        plx                   ;X = Pull original character to translate
        phy                   ;Push whatever Y was on entry
        tay                   ;Y = starting index to TRANSL_HANDLER_OFFSETS table
        txa                   ;A = original character to translate

LC3E4_TRY_NEXT_HANDLER:
        phy                               ;Save handler-to-try index to handler we're trying
        ldx     TRANSL_HANDLER_OFFSETS,y  ;Get the handler's offset in the address table
        jsr     JMP_TO_TRANSL_HANDLER_X   ;Call the handler
        ply                               ;Get the handler-to-try index back
        iny                               ;Increment to try the next handler
        bcs     LC3E4_TRY_NEXT_HANDLER    ;Keep trying until a handler returns carry clear

        ply                               ;Pull whatever Y was on entry
        rts

JMP_TO_TRANSL_HANDLER_X:
        jmp     (TRANSL_HANDLERS,x)
TRANSL_HANDLERS:
        .addr   TRANSL_HANDLER_X00
        .addr   TRANSL_HANDLER_X02
        .addr   TRANSL_HANDLER_X04
        .addr   TRANSL_HANDLER_X06
        .addr   TRANSL_HANDLER_X08
        .addr   TRANSL_HANDLER_X0A
        .addr   TRANSL_HANDLER_X0C
        .addr   TRANSL_HANDLER_X0E
        .addr   TRANSL_HANDLER_X10
        .addr   TRANSL_HANDLER_X12
        .addr   TRANSL_HANDLER_X14
        .addr   TRANSL_HANDLER_X16
        .addr   TRANSL_HANDLER_X18
        .addr   TRANSL_HANDLER_X1A
        .addr   TRANSL_HANDLER_X1C
        .addr   TRANSL_HANDLER_X1E
        .addr   TRANSL_HANDLER_X20

TRANSL_HANDLER_OFFSETS:
;Each is an offset to the TRANSL_HANDLERS table above
;The handlers are called in order until one returns carry clear
;Last handler is always TRANSL_HANDLER_X00 which just returns carry clear
        .byte   $02,$04,$06,$08,$0A,$0C,0     ;$00-06  Outgoing char on Channel 1
        .byte   $02,$06,$08,$0A,$0C,0         ;$07-0C  Outgoing char on Channel 2
        .byte   $02,$18,$06,$1E,$0A,$1C,$10,0 ;$0D-14  Outgoing char on Channel 3
        .byte   $02,$16,$0E,0                 ;$15-18  Outgoing char on Channel 4,5
        .byte   $02,$1A,$1C,$10,0             ;$19-1D  Outgoing char on Channel 6
        .byte   $04,$06,$14,0                 ;$1E-21  Incoming char on Channel 1
        .byte   $04,$12,0                     ;$22-24  Incoming char on Channel 2
        .byte   $04,$20,0                     ;$25-27  Incoming char on Channel 3
        .byte   $06,$14,0                     ;$28-2A  Incoming char on Channel 4
        .byte   $12,0                         ;$2B-2C  Incoming char on Channel 5
        .byte   $20,0                         ;$2B-2E  Incoming char on Channel 6

TRANSL_OUTGOING_CHAR_OFFSETS_TABLE_POS:
;Each is a starting position in the TRANSL_HANDLER_OFFSETS table above
        .byte   $00   ;Outgoing char on Channel 1
        .byte   $07   ;Outgoing char on Channel 2
        .byte   $0D   ;Outgoing char on Channel 3
        .byte   $15   ;Outgoing char on Channel 4
        .byte   $15   ;Outgoing char on Channel 5
        .byte   $19   ;Outgoing char on Channel 6

LC44A_INCOMING_CHAR_OFFSETS_TABLE_POS:
;Each is a starting position in the TRANSL_HANDLER_OFFSETS table above
        .byte   $1E   ;Incoming char on Channel 1
        .byte   $22   ;Incoming char on Channel 2
        .byte   $25   ;Incoming char on Channel 3
        .byte   $28   ;Incoming char on Channel 4
        .byte   $2B   ;Incoming char on Channel 5
        .byte   $2D   ;Incoming char on Channel 6

; ----------------------------------------------------------------------------
TRANSL_HANDLER_X20:
        cmp     #$5E
        bcc     LC462
        cmp     #$80
        bcs     LC462
        sec
        sbc     #$5E
        tay
        lda     LC464,y
        clc
        rts
LC462:  sec
        rts
LC464:  .byte   $71,$7F,$62,$60,$7B,$AE,$BD,$AD
        .byte   $B0,$B1,$3E,$7F,$7A,$56,$AC,$BB
        .byte   $BE,$BC,$B8,$68,$A9,$B2,$B3,$B1
        .byte   $AB,$76,$6E,$6D,$B7,$AF,$67,$68
        .byte   $78,$7E
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X00:
        clc
        rts
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X04:
        cmp     #'A'
        bcc     LC494
        cmp     #'Z'+1
        bcs     LC494
        eor     #$20  ;swap upper/lower
        clc
        rts
LC494:  sec
        rts
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X06:
        cmp     #'a'
        bcc     LC4A2
        cmp     #'z'+1
        bcs     LC4A2
        eor     #$20  ;swap lower/upper
        clc
        rts
LC4A2:  sec
        rts
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X14:
        ldx     #$04
LC4A6_LOOP:
        cmp     LC4B5,x
        beq     LC4B0_FOUND
        dex
        bpl     LC4A6_LOOP
        sec
        rts
LC4B0_FOUND:
        lda     LC4BD,x
        clc
        rts
LC4B5:  .byte   $7B,$7D,$7E,$60,$5F,$7B,$7D,$60
LC4BD:  .byte   $A6,$A8,$5F,$BA,$A4,$E6,$E8,$FA
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X02:
        cmp     #$80
        bcc     LC4D1
        cmp     #$A0
        bcs     LC4D1
        and     #$7F
        clc
        rts
LC4D1:  sec
        rts
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X1A:
        cmp     #$60
        bcc     LC4E4
        cmp     #$80
        bcs     LC4E4
        sec
        sbc     #$60
LC4DE:  tay
        lda     LC4F3,y
        clc
        rts
LC4E4:  cmp     #$C0
        bcc     LC4F1
        cmp     #$E0
        bcs     LC4F1
        sec
        sbc     #$C0
        bra     LC4DE
LC4F1:  sec
        rts
LC4F3:  .byte   $61,$73,$60,$61,$7A,$7A,$7B,$7C
        .byte   $7D,$63,$65,$64,$4C,$79,$78,$66
        .byte   $63,$5E,$7B,$6B,$7C,$66,$77,$4F
        .byte   $7E,$7D,$6A,$62,$60,$60,$7F,$5F
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X1C:
        cmp     #$A0
        bcc     LC524
        cmp     #$C0
        bcs     LC524
        sec
        sbc     #$A0
LC51E:  tay
        lda     LC533,y
        clc
        rts
LC524:  cmp     #$E0
        bcc     LC531
        cmp     #$FF
        bcs     LC531
        sec
        sbc     #$E0
        bra     LC51E
LC531:  sec
        rts
LC533:  .byte   $20,$7C,$7B,$7A,$7B,$7C,$74,$7D
        .byte   $76,$72,$7D,$76,$6C,$65,$63,$7B
        .byte   $66,$75,$73,$74,$7C,$7C,$7D,$7A
        .byte   $7A,$7B,$64,$6D,$6F,$64,$6E,$25
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X10:
        cmp     #$FF
        bne     LC55B
        lda     #$7F
        clc
        rts
LC55B:  sec
        rts
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X12:
        cmp     #$5F
        bne     LC565
        lda     #$A4
        clc
        rts
LC565:  sec
        rts
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X18:
        ldx     #$08
LC569:  cmp     LC581,x
        beq     LC573
        dex
        bpl     LC569
        sec
        rts
LC573:  lda     LC578,x
        clc
        rts
LC578:  .byte   $5B,$5C,$5D,$2D,$27,$5F,$5B,$5D,$27
LC581:  .byte   $A6,$7C,$A8,$5F,$BA,$A4,$E6,$E8,$FA
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X16:
        ldx     #$07
LC58C:  cmp     LC4BD,x
        beq     LC596
        dex
        bpl     LC58C
        sec
        rts
LC596:  lda     LC4B5,x
        clc
        rts
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X1E:
        cmp     #$7B
        bcc     LC5AC
        cmp     #$80
        bcs     LC5AC
        sec
        sbc     #$60
        tay
        lda     LC4F3,y
        clc
        rts
LC5AC:  sec
        rts
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X0A:
        cmp     #$C1
        bcc     LC5BA
        cmp     #$DB
        bcs     LC5BA
        eor     #$80
        clc
        rts
LC5BA:  sec
        rts
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X08:
        ldx     #$0A
LC5BE:  cmp     LC5CD,x
        beq     LC5C8
        dex
        bpl     LC5BE
        sec
        rts
LC5C8:  lda     LC5D8,x
        clc
        rts
LC5CD:  .byte   $A6,$A8,$BA,$5F,$A4,$E6,$E8,$FA,$7B,$7E,$7F
LC5D8:  .byte   $7B,$7D,$60,$7E,$5F,$7B,$7D,$60,$20,$20,$20
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X0C:
        cmp     #$A0
        bcc     LC5ED
        cmp     #$C0
        bcs     LC5ED
        bra     LC5F1
LC5ED:  cmp     #$E0
        bcc     LC5F5
LC5F1:  lda     #$20
        clc
        rts
LC5F5:  sec
        rts
; ----------------------------------------------------------------------------
TRANSL_HANDLER_X0E:
        cmp     #$60
        bcc     LC601
        cmp     #$80
        bcs     LC601
        bra     LC605
LC601:  cmp     #$A0
        bcc     LC609
LC605:  lda     #$20
        clc
        rts
LC609:  sec
        rts

; ----------------------------------------------------------------------------
;Bell-related
JMP_BELL_RELATED_X:
        jmp     (LC60E,x)
LC60E:  .addr   UDBELL
        .addr   LC61E
        .addr   LC626
        .addr   LC63F
        .addr   BELL
; ----------------------------------------------------------------------------
;Called at 60 Hz by the default IRQ handler (see LFA44_VIA1_T1_IRQ).
;Bell-related
UDBELL: jsr     LC63F
        bcs     LC634
        rts
; ----------------------------------------------------------------------------
;Bell-related
LC61E:  sta     VIA2_T2CL
        sty     VIA2_T2CH
        bra     LC63F
; ----------------------------------------------------------------------------
;Bell-related
LC626:  php
        sei
        eor     #$FF
        sta     $041A
        tya
        eor     #$FF
        sta     $041B
        .byte   $2C
LC634:  php
        sei
        inc     $041A
        bne     LC63E
        inc     $041B
LC63E:  .byte   $2C
LC63F:  php
        sei
        lda     $041A
        ora     $041B
        beq     LC654
        lda     #$10
        tsb     VIA2_ACR
        sta     VIA2_SR
        plp
        sec
        rts
; ----------------------------------------------------------------------------
;Bell-related
LC654:  lda     #$10
        trb     VIA2_ACR
        plp
        clc
        rts
; ----------------------------------------------------------------------------
;CTRL$(7) Bell
CODE_07_BELL:
BELL:   lda     #$A0
        tay
        jsr     LC61E
        lda     #$06
        ldy     #$00
        jmp     LC626


MMU_HELPER_ROUTINES:
; ----------------------------------------------------------------------------
; The following routines will be copied from $0338 to the RAM and
; used from there. Guessed purpose: the ROM itself is not always paged in, so
; we need them to be in RAM. Note about the "dummy writes", those (maybe ...)
; used to set/reset flip-flops to switch on/off mapping of various parts of
; the memories, but dunno what exactly :(
; ----------------------------------------------------------------------------
; My best guess so far: dummy writes to ...
; * $FA00: enables lower parts of KERNAL to be "seen"
; * $FA80: disables the above but enable ROM mapped from $4000 to be seen
; * $FB00: disables all mapped, but the "high area"
; "High area" is the end of the KERNAL & some I/O registers from
; at $FA00 (or probably from $F800?) and needs to be always (?)
; seen.
; ----------------------------------------------------------------------------
; This will be $0338 in RAM. It's even used by BASIC for example, the guessed
; purpose: allow to use RAM for BASIC even at an area where there is BASIC
; ROM paged in (from $4000) during its execution. $033C will be the RAM zp
; loc of LDA (zp),Y op.
;GO_RAM_LOAD_GO_APPL:
        sta     MMU_MODE_RAM
        lda     ($00),y ;TODO add symbol for ZP address
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
; This will be $0341 in RAM.
; $0345 will be the RAM zp loc of STA (zp),Y op.
; This routine is also used by BASIC.
; It seems ZP loc of STA is modified in RAM.
;GO_RAM_STORE_GO_APPL:
        sta     MMU_MODE_RAM
        sta     ($00),y ;TODO add symbol for ZP address
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
; This will be $034A in RAM.
; "SINNER" ($034E) will be the RAM zp loc of LDA (zp),Y op.
;GO_RAM_LOAD_GO_KERN:
        sta     MMU_MODE_RAM
;GO_NOWHERE_LOAD_GO_KERN:
        lda     ($00),y         ;ZP address is GRLGK_ADDR
        sta     MMU_MODE_KERN
        rts
; ----------------------------------------------------------------------------
; This will be $0353 in RAM.
; $0357 will be the RAM zp loc of LDA (zp),Y op.
;GO_APPL_LOAD_GO_KERN:
        sta     MMU_MODE_APPL
        lda     ($00),y ;TODO add symbol for ZP address
        sta     MMU_MODE_KERN
        rts
; ----------------------------------------------------------------------------
; This will be $035C in RAM.
; $0360 will be the RAM zp loc of STA (zp),Y op.
;GO_RAM_STORE_GO_KERN:
        sta     MMU_MODE_RAM
;GO_NOWHERE_STORE_GO_KERN:
        sta     ($00),y ;TODO add symbol for ZP address
        sta     MMU_MODE_KERN
        rts
; ----------------------------------------------------------------------------
MMU_HELPER_ROUTINES_SIZE = * - MMU_HELPER_ROUTINES


KL_RESTOR:
        ldx     #<VECTSS
        ldy     #>VECTSS
        clc
KL_VECTOR:
        php
        sei
        stx     FNADR
        sty     FNADR+1
        ldx     #MMU_HELPER_ROUTINES_SIZE-1
LC6A3_LOOP:
        lda     MMU_HELPER_ROUTINES,x   ;from this ROM
        sta     GO_RAM_LOAD_GO_APPL,x   ;into RAM
        dex
        bpl     LC6A3_LOOP
        ldy     #FNADR
        sty     SINNER
        sty     $0360
        ldy     #36-1 ;36 bytes (18 vectors)
LC6B6:  lda     RAMVEC_IRQ,y
        bcs     LC6BE
        jsr     GO_RAM_LOAD_GO_KERN
LC6BE:  sta     RAMVEC_IRQ,y
        bcc     LC6C6
        jsr     GO_RAM_STORE_GO_KERN
LC6C6:  dey
        bpl     LC6B6
        plp
        rts
; ----------------------------------------------------------------------------
;Back up the KERNAL RAM vectors
;Swap 36 bytes between RAMVEC_IRQ and RAMVEC_BACKUP
SWAP_RAMVEC:
        sei
        ldx     #36-1 ;36 bytes (18 vectors)
LC6CE_LOOP:
        ldy     RAMVEC_IRQ,x
        lda     RAMVEC_BACKUP,x
        sta     RAMVEC_IRQ,x
        tya
        sta     RAMVEC_BACKUP,x
        dex
        bpl     LC6CE_LOOP
        rts
; ----------------------------------------------------------------------------

;
; Start of the machine language monitor
;

; ----------------------------------------------------------------------------
MON_START:
        stz     MEM_03B7
        stz     MON_MMU_MODE
        ldx     #$FF
        stx     $03BB
        txs
        ldx     #$00
        jsr     LD230_JMP_LD233_PLUS_X  ;-> LD247_X_00
        jsr     PRIMM
        .byte   $0D,"COMMODORE LCD MONITOR",0
        bra     LC748
; ----------------------------------------------------------------------------
MON_BRK:
        cld
        ldx     #$05
LC70F:  pla
        sta     $03B5,x
        dex
        bpl     LC70F
        jsr     LB2E4_HIDE_CURSOR
        jsr     KL_RESTOR
        jsr     CLRCH
        tsx
        stx     $03BB
        cpx     #$0A
        bcs     LC72A
        ldx     #$FF
        txs
LC72A:  php
        jsr     PRIMM
        .byte   $0D,"BREAK",0
        plp
        bcs     LC748
        jsr     PRIMM
        .byte   " STACK RESET",0
LC748:  lda     #$C0
        sta     MSGFLG
        lda     #$00
        sta     T2
        sta     T2+1
        cli
        ;Fall through
; ----------------------------------------------------------------------------
MON_CMD_REGISTERS:
        jsr     MON_PRINT_REGS_WITH_HEADER
        bra     MON_MAIN_INPUT
; ----------------------------------------------------------------------------
MON_BAD_COMMAND:
        jsr     KL_RESTOR
        jsr     CLRCH
        jsr     PRIMM
        .byte   $1D,$1D,":?",0
        ;Fall through
; ----------------------------------------------------------------------------
;Input a monitor command and dispatch it
MON_MAIN_INPUT:
        jsr     CRLF
        stz     CHRPTR
        ldx     #$00
LC76E_GET_NEXT_CHAR:
        jsr     LFD3D_CHRIN ;BASIN
        sta     LINE_INPUT_BUF,x
        stx     BUFEND
        inx
        cpx     #80  ;80 chars is max line length
        beq     LC77F_GOT_LINE
        cmp     #$0D ;Return
        bne     LC76E_GET_NEXT_CHAR
LC77F_GOT_LINE:
        jsr     GNC
        beq     MON_MAIN_INPUT
        cmp     #' '
        beq     LC77F_GOT_LINE
        ldx     #MON_CMD_COUNT-1 ;X = point to last entry in commands table
LC78A_CMD_SEARCH_LOOP:
        cmp     MON_COMMANDS,x
        beq     LC794_FOUND_CMD
        dex
        bpl     LC78A_CMD_SEARCH_LOOP
        bmi     MON_BAD_COMMAND
LC794_FOUND_CMD:
        cpx     #MON_CMD_LOAD_IDX
        bcs     LC7A6_FOUND_CMD_L_S_V ;Branch if >= "L" index (command is L,S,V)
        ;Command is not L,S,V
        txa
        asl     a
        tax
        lda     MON_CMD_ENTRIES+1,x
        pha
        lda     MON_CMD_ENTRIES,x
        pha
        jmp     PARSE
LC7A6_FOUND_CMD_L_S_V:
        sta     V1541_FNLEN
        jsr     CRLF
        jmp     MON_CMD_LOAD_SAVE_VERIFY
; ----------------------------------------------------------------------------
MON_CMD_MEMORY:
        bcs     LC7B9
        jsr     T0TOT2
        jsr     PARSE
        bcc     LC7BF
LC7B9:  lda     #8-1  ;8 lines of memory to print
        sta     T0
        bne     LC7D0_LOOP
LC7BF:  jsr     SUB0M2
        lsr     a
        ror     T0
        lsr     a
        ror     T0
        lsr     a
        ror     T0
        lsr     a
        ror     T0
        sta     T0+1
LC7D0_LOOP:
        jsr     LFDB9_STOP
        beq     LC7E2
        jsr     MON_PRINT_LINE_OF_MEMORY
        lda     #$10
        jsr     ADDT2
        jsr     DECT0
        bcs     LC7D0_LOOP
LC7E2:  jmp     MON_MAIN_INPUT
; ----------------------------------------------------------------------------
;Command ";" allows the user to modify the registers by typing over:
;  "   PC  SR AC XR YR SP MODE OPCODE   MNEMONIC"
;  "; 0000 00 00 00 00 FF  02  00       BRK"
MON_CMD_MODIFY_REGISTERS:
        bcs     LC81B_DONE ;Branch if no args

        ;Set new PC
        lda     T0
        ldy     T0+1
        sta     $03B6 ;PC low
        sty     $03B5 ;PC high

        ;Set new SR, AC, XR, YR, SP
        ldy     #$00
LC7F3_LOOP:
        jsr     PARSE
        bcs     LC81B_DONE
        lda     T0
        sta     MEM_03B7,y
        iny
        cpy     #$05 ;0=SR, 1=AC, 2=XR,3=YR,4=SP
        bcc     LC7F3_LOOP

        ;Set new MODE
        ;Valid values are 0, 1, 2.  Any other value leaves mode unchanged.
        jsr     PARSE
        bcs     LC81B_DONE
        lda     T0
        bne     LC810_MODE_NOT_0
        stz     MON_MMU_MODE        ;Keep 0 for MMU_MODE_RAM
        bra     LC81B_DONE
LC810_MODE_NOT_0:
        cmp     #$01
        beq     LC818_STA_MMU_MODE  ;Keep 1 for MMU_MODE_APPL
        cmp     #$02
        bne     LC81B_DONE
LC818_STA_MMU_MODE:
        sta     MON_MMU_MODE        ;Keep 2 for MMU_MODE_KERN

LC81B_DONE:
        jsr     PRIMM
        .byte   $91,$91,$00  ;Cursor Up twice
        jmp     MON_CMD_REGISTERS
; ----------------------------------------------------------------------------
MON_CMD_MODIFY_MEMORY:
        bcs     LC83A_MODFIY_DONE ;Branch if no arg
        jsr     T0TOT2
        ldy     #$00
LC82B_LOOP:
        jsr     PARSE
        bcs     LC83A_MODFIY_DONE ;Branch if no input
        lda     T0
        jsr     LCC4B
        iny
        cpy     #$10
        bcc     LC82B_LOOP
LC83A_MODFIY_DONE:
        jsr     ESC_O_CANCEL_MODES
        lda     #$91 ;CHR($145) Cursor Up
        jsr     KR_ShowChar_
        jsr     MON_PRINT_LINE_OF_MEMORY
        jmp     MON_MAIN_INPUT
; ----------------------------------------------------------------------------
MON_CMD_GO:
        bcs     LC854
        lda     T0
        sta     $03B6
        lda     T0+1
        sta     $03B5
LC854:  jsr     CRLF
        ldx     $03BB
        txs
        ldx     $03B5
        ldy     $03B6
        bne     LC864
        dex
LC864:  dey
        phx
        phy
        ldx     MON_MMU_MODE
        cpx     #$03
        bcc     LC870
        ldx     #$02
LC870:  lda     LC886,x
        pha
        lda     LC889,x
        pha
        lda     MEM_03B7
        pha
        ldx     $03B9
        ldy     $03BA
        lda     $03B8
        rti
; ----------------------------------------------------------------------------
LC886:  .byte   $FD,$FD,$FD                     ; C886 FD FD FD                 ...
LC889:  .byte   "~zf"                           ; C889 7E 7A 66                 ~zf

MON_COMMANDS:
        .byte   "X" ;Exit
        .byte   "M" ;Memory
        .byte   "R" ;Registers
        .byte   "G" ;Go
        .byte   "T" ;Transfer
        .byte   "C" ;Compare
        .byte   "D" ;Disassemble
        .byte   "A" ;Assemble
        .byte   "." ;Alias for Assemble
        .byte   "H" ;Hunt
        .byte   "F" ;Fill
        .byte   ">" ;Modify Memory
        .byte   ";" ;Modify Registers
        .byte   "W" ;Walk
MON_CMD_LOAD_IDX = * - MON_COMMANDS
        .byte   "L" ;Load     \
        .byte   "S" ;Save      | L,S,V are handled separately, not in the table below
        .byte   "V" ;Verify   /
MON_CMD_COUNT = * - MON_COMMANDS

MON_CMD_ENTRIES:
        .word  MON_CMD_EXIT-1
        .word  MON_CMD_MEMORY-1
        .word  MON_CMD_REGISTERS-1
        .word  MON_CMD_GO-1
        .word  MON_CMD_TRANSFER-1
        .word  MON_CMD_COMPARE-1
        .word  MON_CMD_DISASSEMBLE-1
        .word  MON_CMD_ASSEMBLE-1
        .word  MON_CMD_ASSEMBLE-1
        .word  MON_CMD_HUNT-1
        .word  MON_CMD_FILL-1
        .word  MON_CMD_MODIFY_MEMORY-1
        .word  MON_CMD_MODIFY_REGISTERS-1
        .word  MON_CMD_WALK-1
; ----------------------------------------------------------------------------
MON_PRINT_LINE_OF_MEMORY:
        jsr     PRIMM
        .byte   $0D,">",0
        jsr     PUTT2
        ldy     #0
LC8C4:  tya
        and     #$03
        bne     LC8CF
        jsr     PRIMM
        .byte   "  ",0
LC8CF:  jsr     PICK1
        jsr     PUTHXS
        iny
        cpy     #$10
        bcc     LC8C4
        jsr     PRIMM
        .byte   ":",$12,$00
        ldy     #$00
LC8E2:  jsr     PICK1
        and     #$7F
        cmp     #$20
        bcs     LC8ED
        lda     #'.'
LC8ED:  jsr     KR_ShowChar_
        iny
        cpy     #$10
        bcc     LC8E2
        rts
; ----------------------------------------------------------------------------
MON_CMD_COMPARE:
        stz     TMPC
        lda     #$00
        sta     WRAP
        bra     LC909_TRANSFER_OR_COMPARE
; ----------------------------------------------------------------------------
MON_CMD_TRANSFER:
        lda     #$80
        sta     WRAP
        jsr     LCB7E
        bcs     LC952_TRANSFER_BAD_ARG
        bra     LC913

LC909_TRANSFER_OR_COMPARE:
        jsr     LCB67
        bcs     LC952_TRANSFER_BAD_ARG
        jsr     PARSE
        bcs     LC952_TRANSFER_BAD_ARG
LC913:  jsr     CRLF
        ldy     #$00
LC918:  jsr     PICK1
        bit     WRAP
        bpl     LC922
        jsr     LCC46
LC922:  pha
        jsr     LCC6A
        sta     MSAL
        pla
        cmp     MSAL
        beq     LC935
        jsr     LFDB9_STOP
        beq     LC94F_TRANSFER_DONE
        jsr     PUTT2
LC935:  lda     TMPC
        beq     LC941
        jsr     DECT0
        jsr     LCB52
        bra     LC94A
LC941:  inc     T0
        bne     LC947
        inc     T0+1
LC947:  jsr     INCT2
LC94A:  jsr     DECT1
        bcs     LC918
LC94F_TRANSFER_DONE:
        jmp     MON_MAIN_INPUT
LC952_TRANSFER_BAD_ARG:
        jmp     MON_BAD_COMMAND
; ----------------------------------------------------------------------------
MON_CMD_HUNT:
        jsr     LCB67
        bcs     LC9B6_HUNT_BAD_ARG
        ldy     #$00
        jsr     GNC
        cmp     #$27
        bne     HT50
        jsr     GNC
HT30:  sta     HULP,y  ;TODO TED-series monitor source says HULP here is a bug; see ht30 there
        iny
        jsr     GNC
        beq     LC98A
        cpy     #$20
        bne     HT30
        beq     LC98A
HT50:  sty     BAD
        jsr     PARGOT
HT60:  lda     T0
        sta     HULP,y ;TODO TED-series monitor source says HULP here is a bug; see ht60 there
        iny
        jsr     PARSE
        bcs     LC98A
        cpy     #$20
        bne     HT60
LC98A:  sty     V1541_FNLEN
        jsr     CRLF
LC990:  ldx     #$00
        ldy     #$00
LC994:  jsr     PICK1
        cmp     HULP,x
        bne     LC9AB
        iny
        inx
        cpx     V1541_FNLEN
        bne     LC994
        jsr     LFDB9_STOP
        beq     LC9B3_HUNT_DONE
        jsr     PUTT2
LC9AB:  jsr     INCT2
        jsr     DECT1
        bcs     LC990
LC9B3_HUNT_DONE:
        jmp     MON_MAIN_INPUT
LC9B6_HUNT_BAD_ARG:
        jmp     MON_BAD_COMMAND
; ----------------------------------------------------------------------------
MON_CMD_LOAD_SAVE_VERIFY:
        ldy     #$01
        sty     FA
        sty     SA
        dey
        sty     FNLEN
        sty     SATUS
        sty     VERCHK
        lda     #>HULP
        sta     FNADR+1
        lda     #<HULP
        sta     FNADR
LC9D0:  jsr     GNC
        beq     LCA33_TRY_LOAD_OR_VERIFY
        cmp     #' '
        beq     LC9D0
        cmp     #'"'
        bne     LC9F5_LSV_BAD_ARG
        ldx     CHRPTR
LC9DF_LOOP:
        cpx     BUFEND
        bcs     LCA33_TRY_LOAD_OR_VERIFY
        lda     LINE_INPUT_BUF,x
        inx
        cmp     #'"'
        beq     LC9F8_TRY_SAVE
        sta     (FNADR),y
        inc     FNLEN
        iny
        cpy     #$11
        bcc     LC9DF_LOOP
LC9F5_LSV_BAD_ARG:
        jmp     MON_BAD_COMMAND

LC9F8_TRY_SAVE:
        stx     CHRPTR
        jsr     GNC
        jsr     PARSE
        bcs     LCA33_TRY_LOAD_OR_VERIFY
        lda     T0
        beq     LC9F5_LSV_BAD_ARG
        cmp     #$03
        beq     LC9F5_LSV_BAD_ARG
        sta     FA
        jsr     PARSE
        bcs     LCA33_TRY_LOAD_OR_VERIFY
        jsr     T0TOT2
        jsr     PARSE
        bcs     LC9F5_LSV_BAD_ARG
        jsr     CRLF
        ldx     T0
        ldy     T0+1
        lda     V1541_FNLEN
        cmp     #'S' ;SAVE
        bne     LC9F5_LSV_BAD_ARG
        lda     #$00
        sta     SA
        lda     #T2
        jsr     LFD82_SAVE_AND_GO_KERN
LCA30_LSV_DONE:
        jmp     MON_MAIN_INPUT

LCA33_TRY_LOAD_OR_VERIFY:
        lda     V1541_FNLEN
        cmp     #'V' ;VERIFY
        beq     LCA40
        cmp     #'L' ;LOAD
        bne     LC9F5_LSV_BAD_ARG
        lda     #$00
LCA40:  jsr     LFD63_LOAD_THEN_GO_KERN
        lda     SATUS
        and     #$10
        beq     LCA30_LSV_DONE
        jsr     PRIMM
        .byte   "ERROR",0
        bra     LCA30_LSV_DONE
; ----------------------------------------------------------------------------
MON_CMD_FILL:
        jsr     LCB67
        bcs     LCA70_FILL_BAD_ARG
        jsr     PARSE
        bcs     LCA70_FILL_BAD_ARG
        ldy     #$00
LCA60_FILL_LOOP:
        lda     T0
        jsr     LCC4B
        jsr     INCT2
        jsr     DECT1
        bcs     LCA60_FILL_LOOP
        jmp     MON_MAIN_INPUT
LCA70_FILL_BAD_ARG:
        jmp     MON_BAD_COMMAND
; ----------------------------------------------------------------------------
;Decrement CHRPTR then parse 16-bit hex value from user input
PARGOT:
        dec     CHRPTR

;Parse 16-bit hex value from user input
PARSE:
        lda     #$00
        sta     T0
        sta     T0+1
        sta     V1541_BYTE_TO_WRITE ;not really; location has multiple uses
                                    ;it's called SYREG here
PAR005:
        jsr     GNC
        beq     PAR040
        cmp     #' '
        beq     PAR005
PAR006:
        cmp     #' '
        beq     PAR030
        cmp     #','
        beq     PAR030
        cmp     #'0'
        bcc     PARERR
        cmp     #'F'+1
        bcs     PARERR
        cmp     #'9'+1
        bcc     PAR010
        cmp     #'A'
        bcc     PARERR
        sbc     #$08
PAR010: sbc     #$2F
        asl     a
        asl     a
        asl     a
        asl     a
        ldx     #$04
PAR015: asl     a
        rol     T0
        rol     T0+1
        dex
        bne     PAR015
        inc     V1541_BYTE_TO_WRITE ;not really; location has multiple uses
        jsr     GNC
        bne     PAR006
PAR030:
        lda     V1541_BYTE_TO_WRITE ;not really; location has multiple uses
        clc
PAR040:
        rts
PARERR:
        pla
        pla
        jmp     MON_BAD_COMMAND
; ----------------------------------------------------------------------------
;print t2 as 4 hex digits: .x destroyed, .y preserved
;Print a hex word given at ZP locs and then a space.
PUTT2:
        lda     T2
        ldx     T2+1

;Print a hex word and then a space.
;Input: X = high byte, A = low byte
PUTWRD:
        pha
        txa
        jsr     PUTHEX
        pla

;Print a hex byte and a space
PUTHXS:
        jsr     PUTHEX

;Print a space
PUTSPC:
        lda     #$20
        .byte   $2C

;Print a carriage return
CRLF:
        lda     #$0D ;CHR$(13) Carriage Return
        jmp     KR_ShowChar_
; ----------------------------------------------------------------------------
;  print .a as 2 hex digits
; Byte as hex print function, prints byte in A as hex number.
; X is saved to $39D and loaded back then.
PUTHEX:
        stx     SXREG
        jsr     MAKHEX
        jsr     KR_ShowChar_
        txa
        ldx     SXREG
        jmp     KR_ShowChar_
; ----------------------------------------------------------------------------
;  convert .a to 2 hex digits & put msb in .a, lsb in .x
; Byte to hex converter
; Input: A = byte
; Output: A = high nibble hex ASCII digit, X = low nibble hex ASCII digit
MAKHEX:
        pha
        jsr     MAKHX1
        tax
        pla
        lsr     a
        lsr     a
        lsr     a
        lsr     a

MAKHX1:
; Nibble to hex converter
; Input: A = byte (low nibble is used only)
; Output: A = hex ASCII digit
        and     #$0F
        cmp     #$0A
        bcc     MAKHX2
        adc     #$06
MAKHX2:  adc     #'0'
        rts
; ----------------------------------------------------------------------------
;Get next character
GNC:
        stx     SXREG
        ldx     CHRPTR
        cpx     BUFEND
        bcs     GNC99
        lda     LINE_INPUT_BUF,x
        cmp     #':'                ;eol-return with z=1
        beq     GNC99
        inc     CHRPTR
GNC98:  php
        ldx     SXREG
        plp
        rts
GNC99:  lda     #$00
        beq     GNC98

T0TOT2:  lda     T0
        sta     T2
        lda     T0+1
        sta     T2+1
        rts
; ----------------------------------------------------------------------------
SUB0M2: sec
        lda     T0
        sbc     T2
        sta     T0
        lda     T0+1
        sbc     T2+1
        sta     T0+1
        rts
; ----------------------------------------------------------------------------
DECT0:  lda     #$01
SUBT0:  sta     SXREG
        sec
        lda     T0
        sbc     SXREG
        sta     T0
        lda     T0+1
        sbc     #$00
        sta     T0+1
        rts
; ----------------------------------------------------------------------------
DECT1:  sec
        lda     T1
        sbc     #$01
        sta     T1
        lda     T1+1
        sbc     #$00
        sta     T1+1
        rts
; ----------------------------------------------------------------------------
LCB52:  lda     T2
        bne     LCB58
        dec     T2+1
LCB58:  dec     T2
        rts
; ----------------------------------------------------------------------------
INCT2:  lda     #$01
ADDT2:  clc
        adc     T2
        sta     T2
        bcc     LCB66
        inc     T2+1
LCB66:  rts
; ----------------------------------------------------------------------------
LCB67:  bcs     LCB7D
        jsr     T0TOT2
        jsr     PARSE
        bcs     LCB7D
        jsr     SUB0M2
        lda     T0
        sta     T1
        lda     T0+1
        sta     T1+1
        clc
LCB7D:  rts
; ----------------------------------------------------------------------------
LCB7E:  bcs     LCBE0
        jsr     T0TOT2
        jsr     PARSE
        bcs     LCBE0
        lda     T0
        sta     MSAL
        lda     T0+1
        sta     $D3
        jsr     PARSE
        lda     T0+1
        pha
        lda     T0
        pha
        cmp     T2
        bcc     LCBAB
        bne     LCBA5
        lda     T0+1
        cmp     T2+1
        bcc     LCBAB
LCBA5:  lda     #$01
        sta     TMPC
        bra     LCBAD
LCBAB:  stz     TMPC
LCBAD:  lda     MSAL
        sta     T0
        lda     $D3
        sta     T0+1
        jsr     SUB0M2
        lda     T0
        sta     T1
        lda     T0+1
        sta     T1+1
        lda     TMPC
        beq     LCBD9
        lda     MSAL
        sta     T2
        lda     $D3
        sta     T2+1
        pla
        clc
        adc     T1
        sta     T0
        pla
        adc     T1+1
        sta     T0+1
        clc
        rts
; ----------------------------------------------------------------------------
LCBD9:  pla
        sta     T0
        pla
        sta     T0+1
        clc
LCBE0:  rts
; ----------------------------------------------------------------------------
MON_PRINT_HEADER_FOR_REGS:
        jsr     PRIMM
        .byte   $0d,"   PC  SR AC XR YR SP MODE OPCODE   MNEMONIC",0
        rts

MON_PRINT_REGS_WITH_HEADER:
        jsr     MON_PRINT_HEADER_FOR_REGS

MON_PRINT_REGS_WITHOUT_HEADER:
        jsr     PRIMM
        .byte   $0D,"; ",0

        lda     $03B5 ;PC high
        jsr     PUTHEX

        ldy     #$00
LCC25_LOOP:
        lda     $03B5+1,y
        jsr     PUTHXS ;0=PC low, 1=SR, 2=AC, 3=XR, 4=YR, 5=SP
        iny
        cpy     #$06
        bcc     LCC25_LOOP

        jsr     PUTSPC
        lda     MON_MMU_MODE
        jsr     PUTHXS

        lda     $03B6 ;PC low
        sta     T2
        lda     $03B5 ;PC high
        sta     T2+1
        jmp     MON_DISASM_OPCODE_MNEMONIC
; ----------------------------------------------------------------------------
LCC46:  pha
        lda     #T0
        bra     LCC4E
LCC4B:  pha
LCC4C:  lda     #T2
LCC4E:  sta     $0360
        sta     $0360
        lda     MON_MMU_MODE
        and     #$03
        asl     a
        tax
        pla
        jmp     (LCC5F,x)                 ;MON_MMU_MODE:
LCC5F:  .addr   GO_RAM_STORE_GO_KERN      ;0 stores to MMU_MODE_RAM
        .addr   GO_APPL_STORE_GO_KERN     ;1 stores to MMU_MODE_APPL
        .addr   GO_NOWHERE_STORE_GO_KERN  ;2 stores to MMU_MODE_KERN (stays in MMU_MODE_KERN)
        .addr   GO_RAM_STORE_GO_KERN      ;3 stores to MMU_MODE_RAM again
; ----------------------------------------------------------------------------
PICK1:  lda     #T2
        .byte   $2C
LCC6A:  lda     #T0
        .byte   $2C
LCC6D:  lda     #$D0
        phx
        jsr     LCC77
        plx
        eor     #$00
        rts
; ----------------------------------------------------------------------------
LCC77:  sta     SINNER
        sta     $0357
        lda     MON_MMU_MODE
        and     #$03
        asl     a
        tax
        jmp     (LCC87,x)                 ;MON_MMU_MODE:
LCC87:  .addr   GO_RAM_LOAD_GO_KERN       ;0 loads from MMU_MODE_RAM
        .addr   GO_APPL_LOAD_GO_KERN      ;1 loads from MMU_MODE_APPL
        .addr   GO_NOWHERE_LOAD_GO_KERN   ;2 loads from MMU_MODE_KERN (stays in MMU_MODE_KERN)
        .addr   GO_RAM_LOAD_GO_KERN       ;3 loads from MMU_MODE_RAM again
; ----------------------------------------------------------------------------
MON_CMD_DISASSEMBLE:
        bcs     LCC99
        jsr     T0TOT2
        jsr     PARSE
        bcc     LCC9F
LCC99:  lda     #$14
        sta     T0
        bne     DISA30
LCC9F:  jsr     SUB0M2
DISA30: jsr     CRLF
        jsr     LFDB9_STOP
        beq     LCCBB
        jsr     LCCBE_DISASM_DOT_ADDR_OPCODE_MNEUMONIC
        inc     LENGTH
        lda     LENGTH
        jsr     ADDT2
        lda     LENGTH
        jsr     SUBT0
        bcs     DISA30
LCCBB:  jmp     MON_MAIN_INPUT
; ----------------------------------------------------------------------------
;". B000  25 F1    AND $F1"
;DIS300
LCCBE_DISASM_DOT_ADDR_OPCODE_MNEUMONIC:
        jsr     PRIMM
        .byte   ". ",0

;"B000  25 F1    AND $F1"
;DIS400
MON_DISASM_ADDR_OPCODE_MNEUMONIC:
        jsr     PUTT2

;" 25 F1    AND $F1"
MON_DISASM_OPCODE_MNEMONIC:
        jsr     PUTSPC
        ldy     #$00
        jsr     PICK1
        sta     $03A2
        jsr     DSET
        pha
        ldx     LENGTH
        inx
PRADR0:  dex
        bpl     PRADRL
        jsr     PRIMM
        .byte   "   ",0
        jmp     PRADRM
; ----------------------------------------------------------------------------
PRADRL: jsr     PICK1
        jsr     PUTHXS
PRADRM: iny
        cpy     #$03
        bcc     PRADR0
        pla
        ldx     #$03
        jsr     PRNME
        ldx     #$06
PRADR1: cpx     #$03
        bne     PRADR3
        ldy     LENGTH
        beq     PRADR3
PRADR2: lda     FORMAT
        cmp     #$E8
        bcs     RELADR
        jsr     PICK1
        jsr     PUTHEX
        dey
        bne     PRADR2
PRADR3: asl     FORMAT
        bcc     PRADR4
        lda     CHAR1,x       ;todo should be CHAR1-1
        jsr     KR_ShowChar_
        pha
        lda     $03A2
        cmp     #$7C
        bne     LCD2C
        pla
        lda     CHAR2,x       ;todo should be CHAR2-1
        beq     PRADR4
        bra     LCD32
LCD2C:  pla
LCD2D:  lda     LCE18,x
        beq     PRADR4
LCD32:  jsr     KR_ShowChar_
PRADR4:  dex
        bne     PRADR1
        rts
; ----------------------------------------------------------------------------
RELADR:  jsr     PICK1
        jsr     PCADJ3
        clc
        adc     #$01
        bne     RELAD2
        inx
RELAD2:  jmp     PUTWRD
; ----------------------------------------------------------------------------
PCADJ3:  ldx     T2+1
        tay
        bpl     PCADJ4
        dex
PCADJ4:  sec
        adc     T2
        bcc     PCRTS
        inx
PCRTS:  rts
; ----------------------------------------------------------------------------
DSET:  lsr     a
        tay
        bcc     IEVEN
        lsr     a
        bcs     ERR
        tax
        cmp     #$22
        beq     LCD92
        lsr     a
        lsr     a
        lsr     a
        ora     #$80
        tay
        txa
        and     #$03
        bcc     LCD6E
        adc     #$03
LCD6E:  ora     #$80
IEVEN:  lsr     a
        tax
        lda     NMODE,x
        bcs     RTMODE
        lsr     a
        lsr     a
        lsr     a
        lsr     a
RTMODE:  and     #$0F
        bne     GETFMT
ERR:  ldy     #$88
        lda     #$00
GETFMT:  tax
        lda     NMODE2,x
        sta     FORMAT
        and     #$03
        sta     LENGTH
        tya
        ldy     #$00
        rts
; ----------------------------------------------------------------------------
LCD92:  ldy     #$16
        lda     #$01
        bra     GETFMT
; ----------------------------------------------------------------------------
; print mnemonic
; enter x=3 characters
PRNME:  tay
        lda     LCEA7_PRNME,y
        tay
        lda     LCE25_PRNME,y
        sta     T1
        iny
        lda     LCE25_PRNME,y
        sta     T1+1
PRMN1:  lda     #$00
        ldy     #$05
PRMN2:  asl     T1+1
        rol     T1
        rol     a
        dey
        bne     PRMN2
        adc     #$3F
        jsr     KR_ShowChar_
        dex
        bne     PRMN1
        jmp     PUTSPC
; ----------------------------------------------------------------------------
NMODE:  .byte   $40,$22,$45,$33,$D8,$2F,$45,$39 ; CDBF 40 22 45 33 D8 2F 45 39  @"E3./E9
        .byte   $30,$22,$45,$33,$D8,$FF,$45,$99 ; CDC7 30 22 45 33 D8 FF 45 99  0"E3..E.
        .byte   $40,$02,$45,$33,$D8,$0F,$44,$09 ; CDCF 40 02 45 33 D8 0F 44 09  @.E3..D.
        .byte   $40,$22,$45,$B3,$D8,$FF,$44,$E9 ; CDD7 40 22 45 B3 D8 FF 44 E9  @"E...D.
        .byte   $D0,$22,$44,$33,$D8,$FC,$44,$39 ; CDDF D0 22 44 33 D8 FC 44 39  ."D3..D9
        .byte   $11,$22,$44,$33,$D8,$FC,$44,$9A ; CDE7 11 22 44 33 D8 FC 44 9A  ."D3..D.
        .byte   $10,$22,$44,$33,$D8,$0F,$44,$09 ; CDEF 10 22 44 33 D8 0F 44 09  ."D3..D.
        .byte   $10,$22,$44,$33,$D8,$0F,$44,$09 ; CDF7 10 22 44 33 D8 0F 44 09  ."D3..D.
        .byte   $62,$13,$7F,$A9                 ; CDFF 62 13 7F A9              b...
NMODE2: .byte   $00,$21,$81,$82,$00,$00,$59,$4D ; CE03 00 21 81 82 00 00 59 4D  .!....YM
        .byte   $49,$92,$86,$4A,$85,$9D,$4E     ; CE0B 49 92 86 4A 85 9D 4E     I..J..N
CHAR1:  .byte   $91,$2C,$29,$2C,$23,$28         ; CE12 91 2C 29 2C 23 28        .,),#(
LCE18:  .byte   $24,$59,$00,$58,$24,$24         ; CE18 24 59 00 58 24 24        $Y.X$$
CHAR2:  .byte   $00,$58,$00,$58,$24,$24,$00     ; CE1E 00 58 00 58 24 24 00     .X.X$$.
LCE25_PRNME:
        .byte   $11, $48, $13, $ca, $15, $1a, $19, $08
        .byte   $19, $28, $19, $a4, $1a, $aa, $1b, $94
        .byte   $1b, $cc, $1c, $5a, $1c, $c4, $1c, $d8
        .byte   $1d, $c8, $1d, $e8, $23, $48, $23, $4a
        .byte   $23, $54, $23, $6e, $23, $a2, $24, $72
        .byte   $24, $74, $29, $88, $29, $b2, $29, $b4
        .byte   $34, $26, $53, $c8, $53, $f2, $53, $f4
        .byte   $5b, $a2, $5d, $26, $69, $44, $69, $72
        .byte   $69, $74, $6d, $26, $7c, $22, $84, $c4
        .byte   $8a, $44, $8a, $62, $8a, $72, $8a, $74
        .byte   $8b, $44, $8b, $62, $8b, $72, $8b, $74
        .byte   $9c, $1a, $9c, $26, $9d, $54, $9d, $68
        .byte   $a0, $c8, $a1, $88, $a1, $8a, $a1, $94
        .byte   $a5, $44, $a5, $72, $a5, $74, $a5, $76
        .byte   $a8, $b2, $a8, $b4, $ac, $c6, $ad, $06
        .byte   $ad, $32, $ae, $44, $ae, $68, $ae, $84
        .byte   $00, $00
LCEA7_PRNME:
        .byte   $16, $00, $76, $04, $4a, $04, $76, $04
        .byte   $12, $46, $74, $04, $1c, $32, $74, $04
        .byte   $3a, $00, $0c, $58, $52, $58, $0c, $58
        .byte   $0e, $02, $0c, $58, $62, $2a, $0c, $58
        .byte   $5c, $00, $00, $42, $48, $42, $38, $42
        .byte   $18, $30, $00, $42, $20, $4e, $00, $42
        .byte   $5e, $00, $6e, $5a, $50, $5a, $38, $5a
        .byte   $1a, $00, $6e, $5a, $66, $56, $38, $5a
        .byte   $14, $00, $6c, $6a, $2e, $7a, $6c, $6a
        .byte   $06, $68, $6c, $6a, $7e, $7c, $6e, $6e
        .byte   $40, $3e, $40, $3e, $72, $70, $40, $3e
        .byte   $08, $3c, $40, $3e, $22, $78, $40, $3e
        .byte   $28, $00, $28, $2a, $36, $2c, $28, $2a
        .byte   $10, $24, $00, $2a, $1e, $4c, $00, $2a
        .byte   $26, $00, $26, $32, $34, $44, $26, $32
        .byte   $0a, $60, $00, $32, $64, $54, $26, $32
        .byte   $46, $02, $30, $00, $68, $3c, $24, $60
        .byte   $80, $0d, $20, $20, $20
; ----------------------------------------------------------------------------
;ASSEM
MON_CMD_ASSEMBLE:
        bcc     AS005
        jmp     MON_BAD_COMMAND
AS005:  jsr     T0TOT2
AS010:  ldx     #$00
        stx     HULP+1
AS020:  jsr     GNC
        bne     AS025
        cpx     #$00
        bne     AS025
        jmp     MON_MAIN_INPUT
; ----------------------------------------------------------------------------
AS025:  cmp     #$20                            ; CF4D C9 20                    .
        beq     AS010                           ; CF4F F0 EB                    ..
        sta     MSAL,x                           ; CF51 95 D2                    ..
        inx                                     ; CF53 E8                       .
        cpx     #$03                            ; CF54 E0 03                    ..
        bne     AS020                           ; CF56 D0 E9                    ..
AS030:  dex                                     ; CF58 CA                       .
        bmi     LCF6E                           ; CF59 30 13                    0.
        lda     MSAL,x                           ; CF5B B5 D2                    ..
        sec                                     ; CF5D 38                       8
        sbc     #$3F                            ; CF5E E9 3F                    .?
        ldy     #$05                            ; CF60 A0 05                    ..
AS040:  lsr     a                               ; CF62 4A                       J
        ror     HULP+1                           ; CF63 6E 51 04                 nQ.
        ror     HULP                           ; CF66 6E 50 04                 nP.
        dey                                     ; CF69 88                       .
        bne     AS040                           ; CF6A D0 F6                    ..
        bra     AS030                           ; CF6C 80 EA                    ..
; ----------------------------------------------------------------------------
LCF6E:  stz     T0                             ; CF6E 64 C7                    d.
        stz     $D5                             ; CF70 64 D5                    d.
        ldx     #$02                            ; CF72 A2 02                    ..
AS050:  jsr     GNC                           ; CF74 20 FD CA                  ..
        beq     LCFC4                           ; CF77 F0 4B                    .K
        cmp     #' '                            ; CF79 C9 20                    .
        beq     AS050                           ; CF7B F0 F7                    ..
        cmp     #'$'                            ; CF7D C9 24                    .$
        beq     LCFAE                           ; CF7F F0 2D                    .-
        cmp     #'F'+1                          ; CF81 C9 47                    .G
        bcs     AS070                           ; CF83 B0 37                    .7
        cmp     #'0'                            ; CF85 C9 30                    .0
        bcc     AS070                           ; CF87 90 33                    .3
        cmp     #'9'+1                          ; CF89 C9 3A                    .:
LCF8B:  bcc     LCF93                           ; CF8B 90 06                    ..
        cmp     #'A'                            ; CF8D C9 41                    .A
        bcc     AS070                           ; CF8F 90 2B                    .+
        adc     #$08                            ; CF91 69 08                    i.
LCF93:  and     #$0F                            ; CF93 29 0F                    ).
        ldy     #$03                            ; CF95 A0 03                    ..
LCF97:  asl     T0                             ; CF97 06 C7                    ..
        rol     T0+1                             ; CF99 26 C8                    &.
        dey                                     ; CF9B 88                       .
        bpl     LCF97                           ; CF9C 10 F9                    ..
        ora     T0                             ; CF9E 05 C7                    ..
        sta     T0                             ; CFA0 85 C7                    ..
        inc     $D5                             ; CFA2 E6 D5                    ..
        lda     $D5                             ; CFA4 A5 D5                    ..
        cmp     #$04                            ; CFA6 C9 04                    ..
        beq     LCFB6                           ; CFA8 F0 0C                    ..
        cmp     #$01                            ; CFAA C9 01                    ..
        bne     AS050                           ; CFAC D0 C6                    ..
LCFAE:  inc     $D5                             ; CFAE E6 D5                    ..
        lda     #$24                            ; CFB0 A9 24                    .$
        sta     HULP,x                         ; CFB2 9D 50 04                 .P.
        inx                                     ; CFB5 E8                       .
LCFB6:  lda     #$30                            ; CFB6 A9 30                    .0
        sta     HULP,x                         ; CFB8 9D 50 04                 .P.
        inx                                     ; CFBB E8                       .
AS070:  sta     HULP,x                         ; CFBC 9D 50 04                 .P.
        inx                                     ; CFBF E8                       .
        cpx     #$10                            ; CFC0 E0 10                    ..
        bcc     AS050                           ; CFC2 90 B0                    ..
LCFC4:  stx     T1                             ; CFC4 86 C9                    ..
        ldx     #$00                            ; CFC6 A2 00                    ..
        stx     WRAP                             ; CFC8 86 D0                    ..
AS110:  ldx     #$00                            ; CFCA A2 00                    ..
        stx     TMPC                             ; CFCC 86 D1                    ..
        lda     WRAP                             ; CFCE A5 D0                    ..
        jsr     DSET                           ; CFD0 20 55 CD                  U.
        ldx     FORMAT                           ; CFD3 AE B4 03                 ...
        stx     T1+1                             ; CFD6 86 CA                    ..
        tax                                     ; CFD8 AA                       .
        lda     LCEA7_PRNME,x                         ; CFD9 BD A7 CE                 ...
        tax                                     ; CFDC AA                       .
        inx                                     ; CFDD E8                       .
        lda     LCE25_PRNME,x                         ; CFDE BD 25 CE                 .%.
        jsr     TSTRX                           ; CFE1 20 B4 D0                  ..
        dex                                     ; CFE4 CA                       .
        lda     LCE25_PRNME,x                         ; CFE5 BD 25 CE                 .%.
        jsr     TSTRX                           ; CFE8 20 B4 D0                  ..
        ldx     #$06                            ; CFEB A2 06                    ..
AS210:  cpx     #$03                            ; CFED E0 03                    ..
        bne     AS230                           ; CFEF D0 13                    ..
        ldy     LENGTH                          ; CFF1 A4 CF                    ..
        beq     AS230                           ; CFF3 F0 0F                    ..
AS220:  lda     FORMAT                           ; CFF5 AD B4 03                 ...
        cmp     #$E8                            ; CFF8 C9 E8                    ..
        lda     #'0'                            ; CFFA A9 30                    .0
        bcs     AS250                           ; CFFC B0 31                    .1
        jsr     TST2
        dey
        bne     AS220                           ; D002 D0 F1                    ..
AS230:  asl     FORMAT                           ; D004 0E B4 03                 ...
        bcc     AS240                           ; D007 90 14                    ..
        lda     #$7C                            ; D009 A9 7C                    .|
        cmp     WRAP                             ; D00B C5 D0                    ..
        beq     LD022                           ; D00D F0 13                    ..
        lda     CHAR1,x                         ; D00F BD 12 CE                 ...
        jsr     TSTRX
        lda     LCE18,x                         ; D015 BD 18 CE                 ...
        BEQ     AS240
LD01A:  jsr     TSTRX                           ; D01A 20 B4 D0                  ..
AS240:  dex                                     ; D01D CA                       .
        bne     AS210                           ; D01E D0 CD                    ..
        bra     AS300                           ; D020 80 13                    ..
; ----------------------------------------------------------------------------
LD022:  lda     CHAR1,x                         ; D022 BD 12 CE                 ...
        jsr     TSTRX                           ; D025 20 B4 D0                  ..
        lda     CHAR2,x                         ; D028 BD 1E CE                 ...
        beq     AS240                           ; D02B F0 F0                    ..
        bra     LD01A                           ; D02D 80 EB                    ..
AS250:  jsr     TST2                           ; D02F 20 B1 D0                  ..
        jsr     TST2                           ; D032 20 B1 D0                  ..
AS300:  lda     T1                             ; D035 A5 C9                    ..
        cmp     TMPC                             ; D037 C5 D1                    ..
        beq     AS310                           ; D039 F0 03                    ..
        jmp     TST05                           ; D03B 4C C0 D0                 L..
; ----------------------------------------------------------------------------
AS310:  ldy     LENGTH
        beq     AS500
        lda     T1+1
        cmp     #$9D
        bne     LD06A

        lda     T0
        sbc     T2
        tax
        lda     T0+1
        sbc     T2+1

        bcc     AS320
        bne     AERR
        cpx     #$82
        bcs     AERR
        bcc     AS340
AS320:  tay
        iny
        bne     AERR
        cpx     #$82
        bcc     AERR
AS340:  dex
        dex
        txa
        ldy     LENGTH
        bne     AS420
LD06A:  lda     LA,y
AS420:  jsr     LCC4B
        dey
        bne     LD06A
AS500:  lda     WRAP
        jsr     LCC4B
        jsr     PRIMM
        .byte   $0D,$91,"A ",0
        jsr     MON_DISASM_ADDR_OPCODE_MNEUMONIC

        inc     LENGTH
        lda     LENGTH
        jsr     ADDT2

        jsr     LB4FB_RESET_KEYD_BUFFER
        lda     #'A'
        ldx     #' '
        jsr     PUT_A_THEN_X_INTO_KEYD_BUFFER
        lda     T2+1
        jsr     MAKHEX_THEN_PUT_A_THEN_X_INTO_KEYD_BUFFER
        lda     T2
        jsr     MAKHEX_THEN_PUT_A_THEN_X_INTO_KEYD_BUFFER
        lda     #' '
        jsr     PUT_KEY_INTO_KEYD_BUFFER
        jmp     MON_MAIN_INPUT
; ----------------------------------------------------------------------------
MAKHEX_THEN_PUT_A_THEN_X_INTO_KEYD_BUFFER:
        jsr     MAKHEX

PUT_A_THEN_X_INTO_KEYD_BUFFER:
        phx     ;Push X onto stack
        jsr     PUT_KEY_INTO_KEYD_BUFFER
        pla     ;Pull it back as A
        jmp     PUT_KEY_INTO_KEYD_BUFFER
; ----------------------------------------------------------------------------
TST2:  jsr     TSTRX
TSTRX:  stx     SXREG
        ldx     TMPC
        cmp     HULP,x
        beq     TST10
        pla
        pla
TST05:  inc     WRAP
        beq     AERR
        jmp     AS110
; ----------------------------------------------------------------------------
AERR:
        jmp     MON_BAD_COMMAND
; ----------------------------------------------------------------------------
TST10:  inx
        stx     TMPC
        ldx     SXREG
        rts
; ----------------------------------------------------------------------------
MON_CMD_WALK:
        lda     #$01
        bcs     LD0D7
        lda     T0
LD0D7:  sta     V1541_FILE_MODE
        jsr     MON_PRINT_HEADER_FOR_REGS
        bra     LD11C_MON_WALK_LD11C
LD0DF:  jsr     MON_PRINT_REGS_WITHOUT_HEADER
        jsr     LFDB9_STOP
        beq     LD0F9_JMP_MON_MAIN_INPUT
        dec     V1541_FILE_MODE
        bne     LD11C_MON_WALK_LD11C
        jsr     LB4FB_RESET_KEYD_BUFFER
        lda     #fmode_w_write
        jsr     PUT_KEY_INTO_KEYD_BUFFER
        lda     #' '
        jsr     PUT_KEY_INTO_KEYD_BUFFER
LD0F9_JMP_MON_MAIN_INPUT:
        jmp     MON_MAIN_INPUT
; ----------------------------------------------------------------------------
LD0FC_MON_WALK_OPCODE_TO_HANDLER:
        .addr LD1BA_MON_WALK_OPCODE_20_JSR      ;jump to this address
        .byte $20 ;JSR                          ;  when byte equals this

        .addr LD1D1_MON_WALK_OPCODE_60_RTS
        .byte $60 ;RTS

        .addr LD201_MON_WALK_OPCODE_4C_JMP
        .byte $4c ;JMP

        .addr LD20B_MON_WALK_OPCODE_40_RTI
        .byte $40 ;RTI

        .addr LD1E5_MON_WALK_OPCODE_6C_JMP_IND
        .byte $6c ;JMP ($abcd)

        .addr LD1E8_7C_MON_WALK_OPCODE_7C_JMP_IND_X
        .byte $7c ;JMP ($abcd,X)

LD10E_MON_WALK_CODE_WRITTEN_TO_LINE_INPUT_BUF:
        nop                                     ; D10E EA                       .
        nop                                     ; D10F EA                       .
        sta     MMU_MODE_KERN                   ; D110 8D 00 FA                 ...
        jmp     LD1A3                           ; D113 4C A3 D1                 L..
        sta     MMU_MODE_KERN                   ; D116 8D 00 FA                 ...
        jmp     LD17D                           ; D119 4C 7D D1                 L}.

; ----------------------------------------------------------------------------
LD11C_MON_WALK_LD11C:
        ldx     #$0E
LD11E:  lda     LD10E_MON_WALK_CODE_WRITTEN_TO_LINE_INPUT_BUF,x
        sta     LINE_INPUT_BUF+1,x
        dex
        bpl     LD11E
        jsr     LD216
        sta     LINE_INPUT_BUF
        cmp     #$80
        beq     LD139
        bit     #$0F
        bne     LD143
        bit     #$10
        beq     LD143
LD139:  lda     #$07
        sta     LINE_INPUT_BUF+1
        jsr     LD216
        bra     LD168
LD143:  ldx     #$0F
LD145:  cmp     LD0FC_MON_WALK_OPCODE_TO_HANDLER+2,x
        bne     LD14D
        jmp     (LD0FC_MON_WALK_OPCODE_TO_HANDLER,x)
LD14D:  dex
        dex
        dex
        bpl     LD145
        jsr     DSET
        ldy     LENGTH
        beq     LD168
        jsr     LD216
        sta     LINE_INPUT_BUF+1
        dey
        beq     LD168
        jsr     LD216
        sta     LINE_INPUT_BUF+2
LD168:  ldy     $03BA
        lda     $03B8
        ldx     $03BB
        txs
        ldx     MEM_03B7
        phx
        ldx     $03B9
        plp
        jmp     LINE_INPUT_BUF ;actually code; see LD11E
; ----------------------------------------------------------------------------
LD17D:  php
        pha
        phy
        lda     $03B6
        bne     LD188
        dec     $03B5
LD188:  dec     $03B6
        jsr     LD216
        clc
        tay
        bpl     LD195
        dec     $03B5
LD195:  adc     $03B6
        bcc     LD19D
        inc     $03B5
LD19D:  sta     $03B6
        ply
        pla
        plp
LD1A3:  php
        stx     $03B9
        plx
        stx     MEM_03B7
        tsx
        stx     $03BB
        sta     $03B8
        sty     $03BA
LD1B5:  cli
        cld
        jmp     LD0DF
; ----------------------------------------------------------------------------
LD1BA_MON_WALK_OPCODE_20_JSR:
        jsr     LD216
        tax
        ldy     $03B5
        phy
        ldy     $03B6
        phy
        jsr     LD216
        dec     $03BB
        dec     $03BB
        bra     LD1DD
; ----------------------------------------------------------------------------
LD1D1_MON_WALK_OPCODE_60_RTS:
        plx
        pla
        inx
        bne     LD1D7
        inc     a
LD1D7:  inc     $03BB
        inc     $03BB
LD1DD:  sta     $03B5
        stx     $03B6
        bra     LD1B5
; ----------------------------------------------------------------------------
LD1E5_MON_WALK_OPCODE_6C_JMP_IND:
        ldy     $03B9
        ;Fall through
; ----------------------------------------------------------------------------
LD1E8_7C_MON_WALK_OPCODE_7C_JMP_IND_X:
        ldy     #$00
        jsr     LD216
        pha
        jsr     LD216
        sta     TMPC
        pla
        sta     WRAP
        jsr     LCC6D
        pha
        iny
        jsr     LCC6D
        plx
        bra     LD1DD
; ----------------------------------------------------------------------------
LD201_MON_WALK_OPCODE_4C_JMP:
        jsr     LD216
        pha
        jsr     LD216
        plx
        bra     LD1DD
; ----------------------------------------------------------------------------
LD20B_MON_WALK_OPCODE_40_RTI:
        pla
        sta     MEM_03B7
        plx
        pla
        inc     $03BB
        bra     LD1D7
LD216:  phy
        ldy     #$00
        lda     $03B6
        sta     WRAP
        lda     $03B5
        sta     TMPC
        jsr     LCC6D
        inc     $03B6
        bne     LD22E
        inc     $03B5
LD22E:  ply
        rts
; ----------------------------------------------------------------------------

;
; End of the machine language monitor
;

; ----------------------------------------------------------------------------

LD230_JMP_LD233_PLUS_X:
        jmp     (LD233,x)
LD233:  .addr   LD247_X_00
        .addr   LD28C_X_02
        .addr   LD255_X_04
        .addr   LD297_X_06
        .addr   LD26A_X_08
        .addr   LD263_X_0A
        .addr   LD2B2_X_0C
        .addr   LD318_X_0E
        .addr   LD252_X_10
        .addr   LD294_X_12
; ----------------------------------------------------------------------------
LD247_X_00:
        stz     $041C
        sta     $F8
        sty     $F9
        stz     $041D
        rts
; ----------------------------------------------------------------------------
LD252_X_10:
        lda     #$10
        .byte   $2C
        ;Fall through
; ----------------------------------------------------------------------------
LD255_X_04:
        lda     #$20
        ldx     $041C
        beq     LD262
        tsb     $041C
        stz     $041D
LD262:  rts
; ----------------------------------------------------------------------------
LD263_X_0A:
        sta     $041D
        stz     $041E
        rts
; ----------------------------------------------------------------------------
LD26A_X_08:
        sty     $C0
        sta     $BF
        lda     $041C
        beq     LD277
        and     #$38
        beq     LD278
LD277:  rts

LD278:  lda     $041D
        beq     LD28A
        lda     FKEY_TO_INDEX-$85,x  ;-$85 for F1
        eor     $041C
        and     $07
        bne     LD28A
        stz     $041D
LD28A:  bra     LD297_X_06
; ----------------------------------------------------------------------------
LD28C_X_02:
        sty     $039C
        and     #$CF
        sta     $041C
        ;Fall through
; ----------------------------------------------------------------------------
LD294_X_12:
        lda     #$10
        .byte   $2C
        ;Fall through (skipping two bytes)
; ----------------------------------------------------------------------------
LD297_X_06:
        lda     #$20
        ldx     $041C
        beq     LD2AA
        trb     $041C
        lda     #$30
        bit     $041C
        bne     LD2AA
        bvs     LD327
LD2AA:  rts

LD2AB_UPDATE_041D_RTS:
        lda     $041D
        stz     $041D
        rts
; ----------------------------------------------------------------------------
LD2B2_X_0C:
        lda     $041D
        cmp     #$85  ;F1
        bcc     LD2AB_UPDATE_041D_RTS
        cmp     #$8D  ;F8 +1
        bcs     LD2AB_UPDATE_041D_RTS
        tay
        ldx     FKEY_TO_INDEX-$85,y  ;-$85 for F1
        lda     $041C
        bit     #$30
        bne     LD2AB_UPDATE_041D_RTS
        bit     #$08
        beq     LD2DB
        txa
        ;A now contains a 0-7 for keys F1-F8
        eor     $041C
        and     #$07
        bne     LD2DB
        lda     #$BF
        sta     $0357
        bra     LD2FC
LD2DB:  bit     $041C
        bvc     LD2AB_UPDATE_041D_RTS
        lda     #$F8
        sta     $0357
        ldy     $041E
        bne     LD2FC
LD2EA:  dex
        bmi     LD2F9
LD2ED:  jsr     GO_APPL_LOAD_GO_KERN
        iny
        beq     LD2F9
        cmp     #$00
        bne     LD2ED
        beq     LD2EA
LD2F9:  sty     $041E
LD2FC:  ldy     $041E
        inc     $041E
        beq     LD309
        jsr     GO_APPL_LOAD_GO_KERN
        bne     LD30F
LD309:  stz     $041E
        stz     $041D
LD30F:  rts

;F1->0, F2->1, F3->2, ... F8->7
FKEY_TO_INDEX:
        .byte 0   ;$85 F1
        .byte 2   ;$86 F3
        .byte 4   ;$85 F5
        .byte 6   ;$86 F7
        .byte 1   ;$89 F2
        .byte 3   ;$8A F4
        .byte 5   ;$8B F6
        .byte 7   ;$8C F8
; ----------------------------------------------------------------------------
LD318_X_0E:
        ldx     $039C
        phx
        sta     $039C
        jsr     LD329
        plx
        stx     $039C
        rts
; ----------------------------------------------------------------------------
LD327:  ldy     #$F8
LD329:  sty     $0357
        ldx     #$00
        ldy     #$00
LD330_OUTER_LOOP:
        phx
        phy
        ldy     LD366_FKEY_COLUMNS,x
        ldx     $039C
        lda     #$89
        sec
        jsr     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
        lda     #$65 ;TODO graphics character
        ldy     #$09
        sta     ($BD),y
        ply
LD345_INNER_LOOP:
        jsr     GO_APPL_LOAD_GO_KERN
        beq     LD359
        cmp     #$08 ;todo length of an f-bar menu bar slot label?
        bcs     LD353
        jsr     LD36E_EXITQUITMORE
        bra     LD356
LD353:  jsr     LD3A9_CLC_JMP_LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
LD356:  iny
        bne     LD345_INNER_LOOP
LD359:  lda     #$0D ;TODO signals an empty f-key menu bar slot?
        jsr     LD3A9_CLC_JMP_LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
        iny
        plx
        inx
        cpx     #$08
        bcc     LD330_OUTER_LOOP
        rts
LD366_FKEY_COLUMNS:
        ;      F1,F2,F3,F4,F5,F6,F7,F8
        .byte   0,10,20,30,40,50,60,70  ;Starting column on bottom screen line
; ----------------------------------------------------------------------------
LD36E_EXITQUITMORE:
        dec     a
        beq     LD382
        dec     a
        asl     a
        asl     a
        tax
LD375_LOOP:
        lda     LD391_EXITQUITMORE,x
        jsr     LD3A9_CLC_JMP_LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
        inx
        txa
        and     #$03
        bne     LD375_LOOP ;loop for 4 chars ("EXIT")
        rts
; ----------------------------------------------------------------------------
LD382:  phy
        ldy     #$00
LD385:  lda     ($BF),y
        beq     LD38F
        jsr     LD3A9_CLC_JMP_LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
        iny
        bne     LD385
LD38F:  ply
        rts
; ----------------------------------------------------------------------------
LD391_EXITQUITMORE:
        .byte   "EXIT","QUIT","MORE"
        .byte   "exit","quit","more"
; ----------------------------------------------------------------------------
LD3A9_CLC_JMP_LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT:
        clc
        jmp     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
; ----------------------------------------------------------------------------
MEMBOT__:
        rol     a
        inc     a
        ror     a
        bcc     LD3E4
        phx
        lda     #$FF
        sta     MemBotLoByte
        lda     #$F7
        sta     MemBotHiByte
        ldx     $020B
        bne     LD3CE
        cmp     $020A
        bcc     LD3CE
        lda     $020A
        dec     a
        sta     MemBotHiByte
LD3CE:  plx
        cpy     MemBotHiByte
        bcc     LD3DD
        bne     LD3E4
        cpx     MemBotLoByte
        bcc     LD3DD
        bne     LD3E4
LD3DD:  stx     MemBotLoByte
        sty     MemBotHiByte
        clc
LD3E4:  php
        ldy     MemBotHiByte
        stz     $020D
        sty     $020C
        jsr     LD3F6
        ldx     MemBotLoByte
        plp
        rts
; ----------------------------------------------------------------------------
LD3F6:  cld
        sec
        lda     $020A
        sbc     $020C
        tax
        lda     $020B
        sbc     $020D
        bcs     LD409
        ldx     #$01
LD409:  beq     LD40D
        ldx     #$00
LD40D:  dex
        stx     $BC
        rts
; ----------------------------------------------------------------------------
LD411:  clc
        ldy     #$FF
        jsr     MEMBOT__
        clc
        ldy     #$00
MEMTOP__:
        bcs     LD42F
        cpy     #$10
        bcs     LD429
        ldx     #$00
        ldy     #$10
        jsr     LD429
        sec
        rts
; ----------------------------------------------------------------------------
LD429:  sty     MemTopHiByte
        stx     MemTopLoByte
LD42F:  ldx     MemTopLoByte
        ldy     MemTopHiByte
        clc
        rts
; ----------------------------------------------------------------------------
LD437:  phx
        phy
        cld
        stz     $E5
        asl     a
        sta     $E4
        asl     a
        rol     $E5
        adc     $E4
        pha
        lda     $E5
        adc     #$F7
        ldx     #$03
        jsr     L8A87
        pla
        sta     $E4
        ply
        plx
        stx     $DA
        sty     $D9
        lda     #$D9
        sta     SINNER
        sta     $0360
        ldx     #$07
LD461:  lda     #$00
        cpx     #$06
        bcs     LD46B
        txa
        tay
        lda     ($E4),y
LD46B:  ldy     #$07
LD46D:  asl     a
        pha
        jsr     GO_RAM_LOAD_GO_KERN
        ror     a
        jsr     GO_RAM_STORE_GO_KERN
        pla
        dey
        bpl     LD46D
        dex
        bpl     LD461
        jmp     L8A81

; ----------------------------------------------------------------------------

;$D480-F6FF is filler.  It contains 6502 code but it's actually from the C128
;BASIC at $9480-B6FF.  This is garbage to the CLCD and is not used.  If it is
;zeroed out, the CLCD works normally.  This area is available for new code.
.list off
.include "c128.asm"
.list on

;$F700-F9FF contains part of the CLCD character set.  The CLCD has a
;separate character ROM that is used for the text mode.  It is not yet
;known if this data is actually used (e.g. by a graphics mode).
.list off
.include "charset.asm"
.list on

; ----------------------------------------------------------------------------
        sei
        sta     MMU_MODE_KERN
        jmp     L87C5
; ----------------------------------------------------------------------------
; The actual RESET routine, pointed by the RESET hardware vector. Notice the
; usage $FA00, seems to be a dummy write (no actual LDA before it, etc).
; Maybe it's just for enabling the lower part of the KERNAL to be mapped, so
; we can jump there, or something like that.
RESET:  sei
        sta     MMU_MODE_KERN
        jmp     KL_RESET
; ----------------------------------------------------------------------------
; The IRQ routine, pointed by the IRQ hardware vector.
IRQ:    pha
        phx
        phy
        sta     MMU_SAVE_MODE
        sta     MMU_MODE_APPL
        tsx

        lda     stack+4,x             ;A = NV-BDIZC
        and     #$10                  ;Test for BRK flag
        bne     LFA28_BRK             ;Branch if BRK flag is set

        lda     #>(RETURN_FROM_IRQ-1)
        pha
        lda     #<(RETURN_FROM_IRQ-1)
        pha
        jmp     (RAMVEC_IRQ)

LFA28_BRK:
        jmp     (RAMVEC_BRK)
; ----------------------------------------------------------------------------
DEFVEC_BRK:
; Default BRK handler, drops into monitor
        sta     MMU_MODE_KERN
        jmp     MON_BRK
; ----------------------------------------------------------------------------
DEFVEC_IRQ:
; Default IRQ handler, where IRQ RAM vector ($314) points to by default.
        sta     MMU_MODE_KERN

        lda     ACIA_ST
        bpl     LFA3C               ;Branch if interrupt was not caused by ACIA
        jsr     ACIA_IRQ            ;Service ACIA, then come back here for VIA1

LFA3C:  bit     VIA1_IFR
        bpl     LFA43               ;Branch if IRQ was not caused by VIA1
        bvs     LFA44_VIA1_T1_IRQ   ;Branch if VIA1 Timer 1 caused the interrupt

LFA43:  rts
; ----------------------------------------------------------------------------
;VIA1 Timer 1 Interrupt Occurred
LFA44_VIA1_T1_IRQ:
        lda     VIA1_T1CL
        lda     VIA1_T1LL
        jsr     KL_SCNKEY
        jsr     BLINK
        jsr     UDTIM__
        jsr     UDBELL
        sta     MMU_MODE_APPL
        jmp     (RAMVEC_NMI)
; ----------------------------------------------------------------------------
DEFVEC_NMI:
        sta     MMU_MODE_KERN
        rts
; ----------------------------------------------------------------------------
RETURN_FROM_IRQ:
        ply
        plx
        pla
        sta     MMU_RECALL_MODE
NMI:    rti
; ----------------------------------------------------------------------------
LFA67:  jsr     LFA6D
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
LFA6D:  phy
        pha
        jmp     RTS_IN_APPL_MODE
; ----------------------------------------------------------------------------
GO_APPL_STORE_GO_KERN:
        sta     MMU_MODE_APPL
        jmp     GO_NOWHERE_STORE_GO_KERN
; ----------------------------------------------------------------------------
LFA78:  jsr     LFA7E
LFA7B_JMP_RTS_IN_KERN_MODE:
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
LFA7E:
; An interesting example for addresses like $FA80 are write only registers,
; but on read, normal ROM content is read as opcodes, as $FA80 here is inside
; and opcode itself.
        sta     MMU_MODE_APPL
        jmp     (RAMVEC_MEM_0334)
; ----------------------------------------------------------------------------
LFA84:  jsr     LFA8A
LFA87_JMP_RTS_IN_KERN_MODE:
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
LFA8A:  sta     MMU_MODE_APPL
        jmp     (RAMVEC_MEM_0336)  ;Contains LFA87_JMP_RTS_IN_KERN_MODE by default
; ----------------------------------------------------------------------------
; Default values of "RAM vectors" copied to $314 into the RAM. The "missing"
; vector in the gap seems to be "monitor" entry (according to C128's ROM) but
; points to RTS in CLCD. The the last two vectors are unknown, not exists on
; C128.
VECTSS: .addr   DEFVEC_IRQ
        .addr   DEFVEC_BRK
        .addr   DEFVEC_NMI
        .addr   DEFVEC_OPEN
        .addr   DEFVEC_CLOSE
        .addr   DEFVEC_CHKIN
        .addr   DEFVEC_CHKOUT
        .addr   DEFVEC_CLRCHN
        .addr   DEFVEC_CHRIN
        .addr   DEFVEC_CHROUT
        .addr   DEFVEC_STOP
        .addr   DEFVEC_GETIN
        .addr   DEFVEC_CLALL
        .addr   DEFVEC_UNKNOWN_LFAB4
        .addr   DEFVEC_LOAD
        .addr   DEFVEC_SAVE
        .addr   LFA7B_JMP_RTS_IN_KERN_MODE
        .addr   LFA87_JMP_RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
DEFVEC_UNKNOWN_LFAB4:
        rts
; ----------------------------------------------------------------------------
LFAB5:  sta     MMU_MODE_KERN
        jsr     LD437
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFABF:  sta     MMU_MODE_KERN
        jsr     LC009_CHECK_MODKEY_AND_UNKNOWN_SECS_MINS
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFAC9:  sta     MMU_MODE_KERN
        jsr     LB6DF_GET_KEY_BLOCKING
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFAD3:  sta     MMU_MODE_KERN
        jsr     L821D
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFADD:  sta     MMU_MODE_KERN
        jsr     L8426
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFAE7:  sta     MMU_MODE_KERN
        jsr     L80E0_DRAW_FKEY_BAR_AND_WAIT_FOR_FKEY_OR_RETURN
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFAF1:  sta     MMU_MODE_KERN
        jsr     LAA53
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFAFB:  sta     MMU_MODE_KERN
        jsr     LA9E6
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFB05:  sta     MMU_MODE_KERN
        jsr     L84FB
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFB0F:  sta     MMU_MODE_KERN
        jsr     LBFF2
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFB19:  sta     MMU_MODE_KERN
        jsr     LB09B
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFB23:  sta     MMU_MODE_KERN
        jsr     L80C6
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFB2D:  sta     MMU_MODE_KERN
        jsr     L81FB
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFB37:  sta     MMU_MODE_KERN
        jsr     L8459
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFB41:  sta     MMU_MODE_KERN
        jsr     L9B1B_JMP_L9B1E_X
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFB4B:  sta     MMU_MODE_APPL
        jmp     (MEM_0300)
; ----------------------------------------------------------------------------
PRIMM00:
; This stuff prints (zero terminated) string after the JSR to the screen (by
; using the return address from the stack). The multiple entry points seems
; to be about the fact that "kernal messages control byte" should be checked
; or not, and such ...
        pha
        lda     #$00
        bra     LFB5E

PRIMM80:
        pha
        lda     #$80
        bra     LFB5E

PRIMM:
        pha
        lda     #$01
LFB5E:  phx
        pha
        bra     LFB77
LFB62:  plx
        phx
        bpl     LFB6B
        bit     MSGFLG
        bpl     LFB71
LFB6B:  sta     MMU_MODE_KERN
        jsr     KR_ShowChar_
LFB71:  txa
        bne     LFB77
        sta     MMU_MODE_APPL
LFB77:  tsx
        inc     stack+4,x
        bne     MMU_RECALL_MODE
        inc     stack+5,x
        lda     stack+4,x
        sta     $F1
        lda     stack+5,x
        sta     $F2
        lda     ($F1)
        bne     LFB62
        plx
        plx
        pla
        rts
; ----------------------------------------------------------------------------
; Code from here clearly shows many examples for the need to "dummy write"
; some "MMU registers" - $FA00 - (maybe only a flip-flop) before jumping to
; lower address in the KERNAL ROM.  Usually there is even an operation like
; that after the call - $FA80. My guess: the top of the kernal is always (?)
; mapped into the CPU address space, but lower addresses are not; so you need
; to "page in" first. However I don't know _exactly_ what happens with
; $FA00/$FA80 (set/reset a flip-flop, but what memory region is affected then
; exactly).
KR_LB758:
        sta     MMU_MODE_KERN
        jsr     LB758
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_LD230_JMP_LD233_PLUS_X:
        sta     MMU_MODE_KERN
        jsr     LD230_JMP_LD233_PLUS_X
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_LB293_SWAP_EDITOR_STATE:
        sta     MMU_MODE_KERN
        jsr     LB293_SWAP_EDITOR_STATE
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
WaitXticks:
        sta     MMU_MODE_KERN
        jsr     WaitXticks_
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_JMP_BELL_RELATED_X:
        sta     MMU_MODE_KERN
        jsr     JMP_BELL_RELATED_X
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFBC4_SHOW_OR_HIDE_CURSOR:
        sta     MMU_MODE_KERN
        pha
        bcs     LFBCF_SHOW
        jsr     LB2E4_HIDE_CURSOR
        bra     LFBD2_DONE
LFBCF_SHOW:
        jsr     LB2D6_SHOW_CURSOR
LFBD2_DONE:
        pla
        jmp     RTS_IN_APPL_MODE
; ----------------------------------------------------------------------------
KR_LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT:
        sta     MMU_MODE_KERN
        jsr     LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_ShowChar:
        sta     MMU_MODE_KERN
        jsr     KR_ShowChar_
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_LCDsetupGetOrSet:
        sta     MMU_MODE_KERN
        jsr     LCDsetupGetOrSet
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_LB684_STA_03F9:
        sta     MMU_MODE_KERN
        jsr     LB684_STA_03F9
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_LB688_GET_KEY_NONBLOCKING:
        sta     MMU_MODE_KERN
        jsr     LB688_GET_KEY_NONBLOCKING
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_LFC08_JSR_LB4FB_RESET_KEYD_BUFFER:
        sta     MMU_MODE_KERN
        jsr     LB4FB_RESET_KEYD_BUFFER
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_PUT_KEY_INTO_KEYD_BUFFER:
        sta     MMU_MODE_KERN
        jsr     PUT_KEY_INTO_KEYD_BUFFER
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_SCINIT:
        sta     MMU_MODE_KERN
        jsr     KL_SCINIT
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KL_SCINIT:
        ldx     #$00
        jsr     LD230_JMP_LD233_PLUS_X  ;-> LD247_X_00
        jmp     SCINIT_
; ----------------------------------------------------------------------------
KR_IOINIT:
        sta     MMU_MODE_KERN
        jsr     KL_IOINIT
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_RAMTAS:
        sta     MMU_MODE_KERN
        jsr     KL_RAMTAS
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_RESTOR:
        sta     MMU_MODE_KERN
        jsr     KL_RESTOR
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_VECTOR:
        sta     MMU_MODE_KERN
        jsr     KL_VECTOR
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
SetMsg_:sta     MSGFLG
        rts
; ----------------------------------------------------------------------------
LSTNSA_:sta     MMU_MODE_KERN
        jsr     SECND
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
TALKSA_:sta     MMU_MODE_KERN
        jsr     TKSA
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
MEMBOT_:sta     MMU_MODE_KERN
        jsr     MEMBOT__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
MEMTOP_:sta     MMU_MODE_KERN
        jsr     MEMTOP__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
KR_SCNKEY:
        sta     MMU_MODE_KERN
        jsr     KL_SCNKEY
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
IECIN_: sta     MMU_MODE_KERN
        jsr     ACPTR
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
IECOUT_:sta     MMU_MODE_KERN
        jsr     CIOUT
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
UNTALK_:sta     MMU_MODE_KERN
        jsr     UNTLK
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
UNLSTN_:sta     MMU_MODE_KERN
        jsr     UNLSN
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LISTEN_:sta     MMU_MODE_KERN
        jsr     LISTN
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
TALK_:  sta     MMU_MODE_KERN
        jsr     TALK__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
READST_:lda     SATUS
UDST:   ora     SATUS
        sta     SATUS
        rts
; ----------------------------------------------------------------------------
SETLFS_:sta     LA
        stx     FA
        sty     SA
        rts
; ----------------------------------------------------------------------------
SETNAM_:sta     FNLEN
        stx     FNADR
        sty     FNADR+1
        rts
; ----------------------------------------------------------------------------
Open_:  sta     MMU_MODE_APPL
        jsr     Open
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
DEFVEC_OPEN:
        sta     MMU_MODE_KERN
        jsr     Open__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFCF1_APPL_CLOSE:
        sta     MMU_MODE_APPL
        jsr     LFFC3_CLOSE
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
DEFVEC_CLOSE:
        sta     MMU_MODE_KERN
        jsr     CLOSE__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
        sta     MMU_MODE_APPL
        jsr     LFFC6_CHKIN
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
DEFVEC_CHKIN:
        sta     MMU_MODE_KERN
        jsr     CHKIN__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
        sta     MMU_MODE_APPL
        jsr     LFFC9_CHKOUT
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
DEFVEC_CHKOUT:
        sta     MMU_MODE_KERN
        jsr     CHKOUT__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
CLRCH:  sta     MMU_MODE_APPL
        jsr     LFFCC_CLRCH
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
DEFVEC_CLRCHN:
        sta     MMU_MODE_KERN
        jsr     CLRCHN__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFD3D_CHRIN:
        sta     MMU_MODE_APPL
        jsr     LFFCF_CHRIN ;BASIN
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
DEFVEC_CHRIN:
        sta     MMU_MODE_KERN
        jsr     CHRIN__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
        sta     MMU_MODE_APPL
        jsr     LFFD2_CHROUT
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
DEFVEC_CHROUT:
        sta     MMU_MODE_KERN
        jsr     CHROUT__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFD63_LOAD_THEN_GO_KERN:
        jsr     LOAD_
RTS_IN_KERN_MODE:
        sta     MMU_MODE_KERN
        rts
; ----------------------------------------------------------------------------
LOAD_:  stx     $B4
        sty     $B5
        sta     MMU_MODE_APPL
        jmp     (RAMVEC_LOAD)
; ----------------------------------------------------------------------------
DEFVEC_LOAD:
        sta     MMU_MODE_KERN
        jsr     LOAD__
RTS_IN_APPL_MODE:
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
        sta     MMU_MODE_RAM
        rts
; ----------------------------------------------------------------------------
LFD82_SAVE_AND_GO_KERN:
        jsr     SAVE_
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
SAVE_:  stx     EAL
        sty     EAH
        tax
        lda     $00,x
        sta     STAL
        lda     $01,x
        sta     STAH
        sta     MMU_MODE_APPL
        jmp     (RAMVEC_SAVE)
; ----------------------------------------------------------------------------
DEFVEC_SAVE:
        sta     MMU_MODE_KERN
        jsr     SAVE__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
RDTIM_: sta     MMU_MODE_KERN
        jsr     LBFD8_RDTIM
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
SETTIM_:sta     MMU_MODE_KERN
        jsr     LBFCE_SETTIM
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFDB9_STOP:
        sta     MMU_MODE_APPL
        jsr     LFFE1_STOP
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
DEFVEC_STOP:
        sta     MMU_MODE_KERN
        jsr     LB6E8_STOP
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
        sta     MMU_MODE_APPL
        jsr     LFFE4_GETIN
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
DEFVEC_GETIN:
        sta     MMU_MODE_KERN
        jsr     LB918_CHRIN___OR_LB688_GET_KEY_NONBLOCKING
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
LFDDF_JSR_LFFE7_CLALL:
        sta     MMU_MODE_APPL
        jsr     LFFE7_CLALL
        jmp     RTS_IN_KERN_MODE
; ----------------------------------------------------------------------------
DEFVEC_CLALL:
        sta     MMU_MODE_KERN
        jsr     CLALL__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
UDTIM_: sta     MMU_MODE_KERN
        jsr     UDTIM__
        sta     MMU_MODE_APPL
        rts
; ----------------------------------------------------------------------------
; SCREEN. Fetch number of screen rows and columns.
; On CLCD the screen's resolution is 80*16 chars.
SCREEN_:ldx     #80
        ldy     #16
        rts
; ----------------------------------------------------------------------------
; PLOT.   Save or restore cursor position.
; Input:  Carry: 0 = Restore from input, 1 = Save to output; X = Cursor
; column
;         (if Carry = 0); Y = Cursor row (if Carry = 0).
; Output: X = Cursor column (if Carry = 1); Y = Cursor row (if Carry = 1).
;         Used registers: X, Y.
PLOT_:  bcs     LFE07
        sty     CursorX
        stx     CursorY
LFE07:  ldy     CursorX
        ldx     CursorY
        rts
; ----------------------------------------------------------------------------
; IOBASE. Fetch VIA #1 base address.
; Input: -
; Output: X/Y = VIA #1 base address.
; Used registers: X, Y.
IOBASE_:ldx     #<VIA1_PORTB
        ldy     #>VIA1_PORTB
        rts
; ----------------------------------------------------------------------------

UNUSED:
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF,$FF
        .byte   $FF,$FF,$FF,$FF,$FF,$FF

; ----------------------------------------------------------------------------

;
;Start of CLCD KERNAL jump table
;

; ----------------------------------------------------------------------------
        jmp     LFAB5                           ; FF27 4C B5 FA                 L..
; ----------------------------------------------------------------------------
        jmp     LFABF  ;check modifier keys and unknown timer   ; FF2A 4C BF FA                 L..
; ----------------------------------------------------------------------------
        jmp     LFAC9  ;get key blocking                         ; FF2D 4C C9 FA                 L..
; ----------------------------------------------------------------------------
        jmp     LFAD3                           ; FF30 4C D3 FA                 L..
; ----------------------------------------------------------------------------
        jmp     LFADD  ;L8426 monitor calls here to exit too; might go back to menu   ; FF33 4C DD FA                 L..
; ----------------------------------------------------------------------------
        jmp     LFAE7  ;draw f-key bar and wait for k-key or return   ; FF36 4C E7 FA                 L..
; ----------------------------------------------------------------------------
        jmp     LFAF1  ;v1541, cursor key, f-key so maybe file navigation ; FF39 4C F1 FA                 L..
; ----------------------------------------------------------------------------
        jmp     LFAFB  ;seems to use v1541 and screen ; FF3C 4C FB FA                 L..
; ----------------------------------------------------------------------------
; Power off with saving the state.
        jmp     LFB05                           ; FF3F 4C 05 FB                 L..
; ----------------------------------------------------------------------------
        jmp     LFB0F  ;possibly f-key related  ; FF42 4C 0F FB                 L..
; ----------------------------------------------------------------------------
        jmp     LFB19  ;LB09B maybe convert char for quote mode? ; FF45 4C 19 FB                 L..
; ----------------------------------------------------------------------------
        jmp     LFB23                           ; FF48 4C 23 FB                 L#.
; ----------------------------------------------------------------------------
        jmp     LFB2D                           ; FF4B 4C 2D FB                 L-.
; ----------------------------------------------------------------------------
        jmp     LFB37                           ; FF4E 4C 37 FB                 L7.
; ----------------------------------------------------------------------------
        jmp     LFB41  ;giant table of indirect access stuff  ; FF51 4C 41 FB                 LA.
; ----------------------------------------------------------------------------
        jmp     PRIMM00   ;print immediate      ; FF54 4C 51 FB                 LQ.
; ----------------------------------------------------------------------------
        jmp     KR_LB758 ;screen and LINE_INPUT_BUF related   ; FF57 4C 92 FB                 L..
; ----------------------------------------------------------------------------
        jmp     KR_LD230_JMP_LD233_PLUS_X ;f-key, maybe menu related  ; FF5A 4C 9C FB                 L..
; ----------------------------------------------------------------------------
        jmp     KR_LB293_SWAP_EDITOR_STATE                           ; FF5D 4C A6 FB                 L..
; ----------------------------------------------------------------------------
        jmp     WaitXticks                      ; FF60 4C B0 FB                 L..
; ----------------------------------------------------------------------------
        jmp     KR_JMP_BELL_RELATED_X                           ; FF63 4C BA FB                 L..
; ----------------------------------------------------------------------------
        jmp     LFBC4_SHOW_OR_HIDE_CURSOR                           ; FF66 4C C4 FB                 L..
; ----------------------------------------------------------------------------
        jmp     KR_LB6F9_MAYBE_PUT_CHAR_IN_FKEY_BAR_SLOT                           ; FF69 4C D6 FB                 L..
; ----------------------------------------------------------------------------
        jmp     KR_ShowChar                        ; FF6C 4C E0 FB                 L..
; ----------------------------------------------------------------------------
        jmp     KR_LCDsetupGetOrSet                           ; FF6F 4C EA FB                 L..
; ----------------------------------------------------------------------------
        jmp     KR_LB684_STA_03F9 ;Keyboard related           ; FF72 4C F4 FB                 L..
; ----------------------------------------------------------------------------
        jmp     KR_LB688_GET_KEY_NONBLOCKING       ; FF75 4C FE FB                 L..
; ----------------------------------------------------------------------------
        jmp     KR_LFC08_JSR_LB4FB_RESET_KEYD_BUFFER                           ; FF78 4C 08 FC                 L..
; ----------------------------------------------------------------------------
        jmp     KR_PUT_KEY_INTO_KEYD_BUFFER        ; FF7B 4C 12 FC                 L..
; ----------------------------------------------------------------------------
;unused kernal jump table entry
        .byte   $FF, $FF, $FF                   ; FF7E FF FF FF
; ------------------------------------------------------------------------------
; Begin of the table of the kernal vectors (well, compared with "standard
; KERNAL entries" on Commodore 64, I can just guess if there is not so much
; difference on the CLCD)
; ------------------------------------------------------------------------------
KJ_SCINIT:
        jmp     KR_SCINIT                       ; FF81 4C 1C FC                 L..
; ----------------------------------------------------------------------------
KJ_IOINIT:
        jmp     KR_IOINIT                       ; FF84 4C 2E FC                 L..
; ----------------------------------------------------------------------------
KJ_RAMTAS:
        jmp     KR_RAMTAS                       ; FF87 4C 38 FC                 L8.
; ----------------------------------------------------------------------------
KJ_RESTOR:
        jmp     KR_RESTOR                       ; FF8A 4C 42 FC                 LB.
; ----------------------------------------------------------------------------
KJ_VECTOR:
        jmp     KR_VECTOR                       ; FF8D 4C 4C FC                 LL.
; ----------------------------------------------------------------------------
SetMsg: jmp     SetMsg_                         ; FF90 4C 56 FC                 LV.
; ----------------------------------------------------------------------------
LSTNSA: jmp     LSTNSA_                         ; FF93 4C 5A FC                 LZ.
; ----------------------------------------------------------------------------
TALKSA: jmp     TALKSA_                         ; FF96 4C 64 FC                 Ld.
; ----------------------------------------------------------------------------
MEMBOT: jmp     MEMBOT_                         ; FF99 4C 6E FC                 Ln.
; ----------------------------------------------------------------------------
MEMTOP: jmp     MEMTOP_                         ; FF9C 4C 78 FC                 Lx.
; ----------------------------------------------------------------------------
KJ_SCNKEY:
        jmp     KR_SCNKEY                       ; FF9F 4C 82 FC                 L..
; ----------------------------------------------------------------------------
; The following entry (three bytes) would be "SETTMO. Unknown. (Set serial
; bus timeout.)" according to the C64 KERNAL, however on CLCD it is unused.
SETTMO: rts                                     ; FFA2 60                       `
        rts                                     ; FFA3 60                       `
        rts                                     ; FFA4 60                       `
; ----------------------------------------------------------------------------
IECIN:  jmp     IECIN_                          ; FFA5 4C 8C FC                 L..
; ----------------------------------------------------------------------------
IECOUT: jmp     IECOUT_                         ; FFA8 4C 96 FC                 L..
; ----------------------------------------------------------------------------
UNTALK: jmp     UNTALK_                         ; FFAB 4C A0 FC                 L..
; ----------------------------------------------------------------------------
UNLSTN: jmp     UNLSTN_                         ; FFAE 4C AA FC                 L..
; ----------------------------------------------------------------------------
LISTEN: jmp     LISTEN_                         ; FFB1 4C B4 FC                 L..
; ----------------------------------------------------------------------------
; TALK. Send TALK command to serial bus.
; Input: A = Device number.
TALK:   jmp     TALK_                           ; FFB4 4C BE FC                 L..
; ----------------------------------------------------------------------------
; READST. Fetch status of current input/output device, value of ST
; variable. (For RS232, status is cleared.)
; Output: A = Device status.
READST: jmp     READST_                          ; FFB7 4C C8 FC                 L..
; ----------------------------------------------------------------------------
; SETLFS. Set file parameters.
; Input: A = Logical number; X = Device number; Y = Secondary address.
SETLFS: jmp     SETLFS_                          ; FFBA 4C CF FC                 L..
; ----------------------------------------------------------------------------
; SETNAM. Set file name parameters.
; Input: A = File name length; X/Y = Pointer to file name.
SETNAM: jmp     SETNAM_                          ; FFBD 4C D6 FC                 L..
; ----------------------------------------------------------------------------
; "OPEN". Must call SETLFS_ and SETNAM_ beforehand.
; RAMVEC_OPEN points to $FCE7 in RAM by default.
Open:   jmp     (RAMVEC_OPEN)                   ; FFC0 6C 1A 03                 l..
; ----------------------------------------------------------------------------
LFFC3_CLOSE:  jmp     (RAMVEC_CLOSE)                  ; FFC3 6C 1C 03                 l..
; ----------------------------------------------------------------------------
LFFC6_CHKIN:  jmp     (RAMVEC_CHKIN)                  ; FFC6 6C 1E 03                 l..
; ----------------------------------------------------------------------------
LFFC9_CHKOUT:  jmp     (RAMVEC_CHKOUT)                 ; FFC9 6C 20 03                 l .
; ----------------------------------------------------------------------------
LFFCC_CLRCH:  jmp     (RAMVEC_CLRCHN)                 ; FFCC 6C 22 03                 l".
; ----------------------------------------------------------------------------
LFFCF_CHRIN:  jmp     (RAMVEC_CHRIN)                  ; FFCF 6C 24 03                 l$.
; ----------------------------------------------------------------------------
LFFD2_CHROUT:  jmp     (RAMVEC_CHROUT)                 ; FFD2 6C 26 03                 l&.
; ----------------------------------------------------------------------------
; LOAD. Load or verify file. (Must call SETLFS_ and SETNAM_ beforehand.)
; Input: A: 0 = Load, 1-255 = Verify; X/Y = Load address (if secondary
; address = 0).
; Output: Carry: 0 = No errors, 1 = Error; A = KERNAL error code (if Carry =
; 1); X/Y = Address of last byte loaded/verified (if Carry = 0).
; Used registers: A, X, Y.
; Real address: $F49E.
LOAD:   jmp     LOAD_                           ; FFD5 4C 6A FD                 Lj.
; ----------------------------------------------------------------------------
; SAVE. Save file. (Must call SETLFS_ and SETNAM_ beforehand.)
; Input: A = Address of zero page register holding start address of memory
; area to save; X/Y = End address of memory area plus 1.
; Output: Carry: 0 = No errors, 1 = Error; A = KERNAL error code (if Carry =
; 1).
; Used registers: A, X, Y.
; Real address: $F5DD.
SAVE:   jmp     SAVE_                           ; FFD8 4C 88 FD                 L..
; ----------------------------------------------------------------------------
; RDTIM. Read Time of Day
; Input: A/X/Y = New TOD value.
; Output: –
; Used registers: –
; Real address: $F6E4.
RDTIM:  jmp     RDTIM_                           ; FFDB 4C A5 FD                 L..
; ----------------------------------------------------------------------------
; SETTIM. Set Time of Day
; Input: –
; Output: A/X/Y = Current TOD value.
; Used registers: A, X, Y.
SETTIM: jmp     SETTIM_                           ; FFDE 4C AF FD                 L..
; ----------------------------------------------------------------------------
; STOP. Query Stop key indicator, at memory address $0091; if pressed, call
; CLRCHN and clear keyboard buffer.
; Input: –
; Output: Zero: 0 = Not pressed, 1 = Pressed; Carry: 1 = Pressed.
; Used registers: A, X.
; Vector in RAM ($328) seems to point to $FDC2
LFFE1_STOP:  jmp     (RAMVEC_STOP)                   ; FFE1 6C 28 03                 l(.
; ----------------------------------------------------------------------------
; GETIN. Read byte from default input. (If not keyboard, must call OPEN and
; CHKIN beforehand.)
; Input: –
; Output: A = Byte read.
; Used registers: A, X, Y.
LFFE4_GETIN:  jmp     (RAMVEC_GETIN)                  ; FFE4 6C 2A 03                 l*.
; ----------------------------------------------------------------------------
LFFE7_CLALL:  jmp     (RAMVEC_CLALL)                  ; FFE7 6C 2C 03                 l,.
; ----------------------------------------------------------------------------
; UDTIM. Update Time of Day, at memory address $0390-$0392, and
; Stop key indicator
UDTIM:  jmp     UDTIM_                          ; FFEA 4C F2 FD                 L..
; ----------------------------------------------------------------------------
; SCREEN. Fetch number of screen rows and columns.
SCREEN: jmp     SCREEN_                         ; FFED 4C FC FD                 L..
; ----------------------------------------------------------------------------
; PLOT. Save or restore cursor position.
; Input: Carry: 0 = Restore from input, 1 = Save to output; X = Cursor column
; (if Carry = 0); Y = Cursor row (if Carry = 0).
; Output: X = Cursor column (if Carry = 1); Y = Cursor row (if Carry = 1).
; Used registers: X, Y.
PLOT:   jmp     PLOT_                           ; FFF0 4C 01 FE                 L..
; ----------------------------------------------------------------------------
; IOBASE. Fetch VIA #1 base address.
; Input: -
; Output: X/Y = VIA #1 base address .
; Used registers: X, Y.
IOBASE: jmp     IOBASE_                         ; FFF3 4C 0C FE                 L..
; ----------------------------------------------------------------------------
; Four unused bytes, this is the same as with C64.
        .byte   $FF, $FF, $FF, $FF              ; FFF6 FF FF FF FF
; ----------------------------------------------------------------------------

NMI_VECTOR:
; The 65xx hardware vectors (NMI, RESET, IRQ).
        .addr   NMI                             ; FFFA 66 FA                    f.
RES_VECTOR:
; This is the RESET vector.
        .addr   RESET                           ; FFFC 07 FA                    ..
IRQ_VECTOR:
        .addr   IRQ                             ; FFFE 0E FA                    ..
