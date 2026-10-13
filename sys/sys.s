; sys.s - AS816.SYSTEM: applesoft816 under ProDOS 8 on a IIgs (docs/M5.md).
;
; ProDOS 8 runs it at $2000 in emulation mode.  It checks for a IIgs, gets
; whole banks from the Memory Manager (the VM's, the string heap's, the
; shell's code and data, the program text's, a spare for CHAIN), loads
; AS816.VM and AS816.CC
; into them, sets up the resident bank-0 block (res.inc: the VM's direct
; page and stubs, the MLI and ROM trampolines, I/O buffers) and calls the
; shell (cc/, crt0's entry) through the resident EXIT block, which also
; leaves: the banks back, QUIT - or, if the shell left a SYS file in the
; CHAIN bank ("-"), that run at $2000.  Ctrl-Reset comes back into the shell
; warm, at its prompt, as BASIC.SYSTEM's does (EXIT's x_reset).

        .p816
        .smart  off
        .include "res.inc"
        .import __RESIDENT_LOAD__, __RESIDENT_RUN__, __RESIDENT_SIZE__
        .import __EXIT_LOAD__, __EXIT_RUN__, __EXIT_SIZE__

COUT     = $FDED
CROUT    = $FD8E
RDKEY    = $FD0C
IDROUT   = $FE1F
PRBYTE   = $FDDA
MLI      = $BF00
DEVNUM   = $BF30
TOOLS    = $E10000

; loader scratch in page zero ($06-$09: free under ProDOS 8)
HND      = $06          ; 4: a handle

; the VM image's header (vm/vm.s)
VMH_STUB = $10
VMH_STUBLEN = $12
; its direct page (vm/vm.s)
DP_COUT_VEC = $2A
DP_FLASH = $2D
DP_SPEED = $2E
DP_SH_BANK = $48
DP_RNDSEED = $C9
; the shell image's header (cc/crt0.s)
CCH_PARAMS = $08
; its parameter block (cc/main.c, struct CcParams)
; the shell's parameter block (cc/main_sh.c, struct ShParams)
SHP_VM_BANK = 0
SHP_TEXT_BANK = 1
SHP_VM_DP = 2
SHP_STR_BANK = 4
SHP_XFER_BANK = 5
SHP_STAGE_BANK = 6
SHP_CC_BANK = 7
SHP_CC_DATA = 8
SHP_USER_ID = 9
SHP_LAUNCH = 11         ; 2: out, a SYS file's length in the CHAIN bank (0: QUIT)

        .segment "LOADER"
start:
        cld
        ldx     #$FB            ; S below $01FC: AppleSoft's INPUT owns $01FC-$01FF
        txs
        sec
        jsr     IDROUT          ; carry clear: a IIgs
        bcc     @gs
        ldx     #msg_notgs - msgs
        jmp     fail
@gs:
        ldx     #0              ; the resident trampolines to their place
@mv:    lda     __RESIDENT_LOAD__,x
        sta     __RESIDENT_RUN__,x
        inx
        cpx     #<__RESIDENT_SIZE__
        bne     @mv
        jsr     set_prefix

        clc
        xce
        rep     #$30
        .a16
        .i16
        pha                     ; GetNewID: a user ID for an application ($1000);
        pea     $1000           ; (MMStartUp has no current application to name
        ldx     #$2003          ; under ProDOS 8 alone: idErr)
        jsl     TOOLS
        bcc     @id
        sta     err_code
        pla
        ldx     #msg_mm - msgs
        jmp     fail_native
@id:    pla
        sta     user_id
        ldx     #0              ; vm_bank ... stage_bank (banks:)
@alloc: phx
        jsr     alloc_bank
        plx
        sep     #$20
        .a8
        sta     banks,x
        rep     #$20
        .a16
        inx
        cpx     #NBANKS
        bne     @alloc
        sec
        xce
        .a8
        .i8

        lda     #<name_vm
        ldx     #>name_vm
        ldy     vm_bank
        jsr     load_file
        lda     #<name_sh
        ldx     #>name_sh
        ldy     sh_bank
        jsr     load_file
        lda     #<name_cc
        ldx     #>name_cc
        ldy     cc_bank
        jsr     load_file

        clc
        xce
        rep     #$30
        .a16
        .i16
        ; the VM's direct page: clear, then what vm_run expects set
        ldx     #$00FE
@zdp:   stz     RES_DP,x
        dex
        dex
        bpl     @zdp
        lda     #RES_VMSTUB
        sta     RES_DP + DP_COUT_VEC    ; and +2 (bank 0) is 0
        lda     #$0100
        sta     RES_DP + DP_FLASH       ; FLASH 0, SPEED 1
        lda     #$4F80                  ; AppleSoft's RND seed (80 4F C7 52, then 0)
        sta     RES_DP + DP_RNDSEED
        lda     #$52C7
        sta     RES_DP + DP_RNDSEED + 2
        sep     #$20
        .a8
        lda     str_bank
        sta     RES_DP + DP_SH_BANK
        ; the VM's stubs into bank 0 (they are bank-0 code)
        lda     vm_bank
        sta     HND+2
        rep     #$20
        .a16
        stz     HND
        ldy     #VMH_STUBLEN
        lda     [HND],y
        and     #$00FF
        pha
        ldy     #VMH_STUB
        lda     [HND],y
        sta     HND
        ply                     ; Y = the length
        sep     #$20
        .a8
@stub:  dey
        bmi     @stubbed
        lda     [HND],y
        sta     RES_VMSTUB,y
        bra     @stub
@stubbed:

        ; the shell's parameters: its data bank, the header's block address
        lda     sh_bank
        sta     HND+2
        stz     HND
        stz     HND+1
        ldy     #CCH_PARAMS
        rep     #$20
        .a16
        lda     [HND],y
        sta     HND
        sep     #$20
        .a8
        lda     sh_data
        sta     HND+2
        sep     #$10            ; (byte tables: 8-bit index registers)
        .i8
        ldx     #0              ; the banks the shell is told about
@sp:    ldy     shp_from,x
        lda     banks,y
        ldy     shp_to,x
        sta     [HND],y
        inx
        cpx     #SHP_N
        bne     @sp
        rep     #$10
        .i16
        ldy     #SHP_VM_DP
        lda     #<RES_DP
        sta     [HND],y
        iny
        lda     #>RES_DP
        sta     [HND],y
        ldy     #SHP_USER_ID
        lda     user_id
        sta     [HND],y
        iny
        lda     user_id+1
        sta     [HND],y

        ; the way out, resident (the program owns $0800-$95FF from here):
        ; its data, the reset vector (the old one kept for the way out)
        rep     #$30
        .a16
        .i16
        ldx     #0
@xc:    lda     __EXIT_LOAD__,x
        sta     __EXIT_RUN__,x
        inx
        inx
        cpx     #__EXIT_SIZE__ + 1
        bcc     @xc
        lda     user_id
        sta     x_user_id
        lda     HND
        sta     x_hnd
        lda     $03F2
        sta     x_oldrst
        sep     #$20
        .a8
        lda     HND+2
        sta     x_hnd+2
        lda     $03F4
        sta     x_oldrst+2
        lda     sh_bank
        sta     x_sh_bank
        lda     sh_data
        sta     x_sh_data
        lda     xfer_bank
        sta     x_xfer
        lda     #<x_reset
        sta     $03F2
        lda     #>x_reset
        sta     $03F3
        eor     #$A5
        sta     $03F4
        sep     #$30
        .i8
        ldx     #0              ; cold
        jmp     x_enter

; ---- a whole bank from the Memory Manager: A (16-bit) = its bank ----
        .a16
        .i16
alloc_bank:
        pha                     ; result: the handle
        pha
        pea     $0001           ; size $00010000
        pea     $0000
        lda     user_id
        pha
        pea     $C018           ; locked, fixed, within one bank, not special memory
        pea     $0000           ; location (unused)
        pea     $0000
        ldx     #$0902          ; NewHandle
        jsl     TOOLS
        bcc     @ok
        sta     err_code
        pla
        pla
        ldx     #msg_mem - msgs
        jmp     fail_native
@ok:    pla
        sta     HND
        pla
        sta     HND+2
        ldy     #2
        lda     [HND],y         ; the block's bank (its address is $bb0000)
        and     #$00FF
        rts

; ---- load a file: A/X its name (length byte first), Y the bank: at $0000 ----
        .a8
        .i8
load_file:
        sta     open_path
        stx     open_path+1
        sty     @mvn_dst
        jsr     MLI
        .byte   $C8             ; OPEN
        .word   open_parms
        bcs     @err
        lda     open_ref
        sta     read_ref
        sta     close_ref
        stz     dest
        stz     dest+1
@read:  jsr     MLI
        .byte   $CA             ; READ
        .word   read_parms
        bcc     @got
        cmp     #$4C            ; end of file
        beq     @done
        bra     @err
@got:   lda     read_trans
        ora     read_trans+1
        beq     @done
        clc
        xce
        rep     #$30
        .a16
        .i16
        lda     read_trans
        dec     a
        ldx     #RES_IOBUF
        ldy     dest
        phb
        .byte   $54             ; MVN bank 0 -> mvn_dst
@mvn_dst:
        .byte   0, 0
        plb
        lda     dest
        clc
        adc     read_trans
        sta     dest
        sec
        xce
        .a8
        .i8
        bra     @read
@done:  jsr     MLI
        .byte   $CC             ; CLOSE
        .word   close_parms
        rts
@err:   ldx     #msg_load - msgs
        jmp     fail

; ---- the prefix: if none, the boot volume's ----
set_prefix:
        jsr     MLI
        .byte   $C7             ; GET_PREFIX
        .word   prefix_parms
        lda     RES_PATH
        bne     @rts
        lda     DEVNUM
        sta     online_unit
        jsr     MLI
        .byte   $C5             ; ON_LINE
        .word   online_parms
        lda     RES_PATH+1      ; the volume name: length in the low nibble
        and     #$0F
        tax
        inx                     ; "/" + name + "/"
        inx
        stx     RES_PATH
        lda     #'/'
        sta     RES_PATH+1
        sta     RES_PATH,x
        jsr     MLI
        .byte   $C6             ; SET_PREFIX
        .word   prefix_parms
@rts:   rts

; ---- leaving ----
        .a16
        .i16
fail_native:
        sec
        xce
        .a8
        .i8
fail:                           ; X = the message's offset in msgs
        jsr     CROUT
@l:     lda     msgs,x
        beq     @key
        ora     #$80
        jsr     COUT
        inx
        bra     @l
        lda     err_code+1          ; the toolbox's error, if any
        ora     err_code
        beq     @key
        lda     #' '|$80
        jsr     COUT
        lda     #'$'|$80
        jsr     COUT
        lda     err_code+1
        jsr     PRBYTE
        lda     err_code
        jsr     PRBYTE
@key:   jsr     RDKEY
quit:   clc
        xce
quit_native:
        rep     #$30
        .a16
        .i16
        lda     user_id
        beq     @q
        pha                     ; DisposeAll, DeleteID
        ldx     #$1102
        jsl     TOOLS
        lda     user_id
        pha
        ldx     #$2103
        jsl     TOOLS
@q:     sec
        xce
        .a8
        .i8
        jsr     MLI
        .byte   $65             ; QUIT
        .word   quit_parms
        brk

; ---- resident at RES_EXIT: the shell's call and the way out ----
; The loader's memory is the program's once the shell runs (HGR alone
; clears $2000-$3FFF), so everything after the shell lives here.
        .segment "EXIT"
x_resetf:  .byte 0              ; RES_RESETF: Ctrl-Reset happened (the shell clears it)
x_user_id: .word 0
x_sh_bank: .byte 0
x_sh_data: .byte 0
x_xfer:    .byte 0
x_hnd:     .res  3              ; the shell's parameter block (long)
x_oldrst:  .res  3              ; $3F2-$3F4 as found

; the shell: JSL its bank's $0000 with A = its data bank, X = 0 cold or
; $5A warm; native, M = X = 1 on entry.  Then the way out.
        .a8
        .i8
x_enter:
        lda     x_sh_bank
        sta     @jsl + 3
        lda     x_hnd           ; the parameter block, for SHP_LAUNCH after
        sta     HND
        lda     x_hnd+1
        sta     HND+1
        lda     x_hnd+2
        sta     HND+2
        lda     x_sh_data
@jsl:   jsl     $000000         ; bank patched above
        rep     #$30
        .a16
        .i16
        lda     x_oldrst        ; the reset vector as it was
        sta     $03F2
        sep     #$20
        .a8
        lda     x_oldrst+2
        sta     $03F4
        rep     #$20
        .a16
        ldy     #SHP_LAUNCH
        lda     [HND],y
        bne     x_launch
        lda     x_user_id       ; DisposeAll, DeleteID, QUIT
        pha
        ldx     #$1102
        jsl     TOOLS
        lda     x_user_id
        pha
        ldx     #$2103
        jsl     TOOLS
        sec
        xce
        .a8
        .i8
        jsr     MLI
        .byte   $65             ; QUIT
        .word   x_quit
        brk
x_quit: .byte   4, 0
        .word   0
        .byte   0
        .word   0

; "-" on a SYS file: the launcher to $1F00 (the file goes over $2000 up,
; perhaps over this block too), told the length, the bank, the ID
        .a16
        .i16
x_launch:
        pha
        ldx     #0
@cp:    lda     x_tramp,x
        sta     TRAMP,x
        inx
        inx
        cpx     #x_tramp_end - x_tramp + 1
        bcc     @cp
        pla
        sta     T_LEN
        lda     x_user_id
        sta     T_ID
        sep     #$20
        .a8
        lda     x_xfer
        sta     T_MVN + 2       ; MVN's operands: destination, source
        rep     #$20
        .a16
        jmp     TRAMP

; Ctrl-Reset (the ROM's JMP ($3F2), emulation mode): native, the stack
; afresh, the flag, the shell warm - back at its prompt
        .a8
        .i8
x_reset:
        clc
        xce
        rep     #$10
        .i16
        ldx     #$01FB          ; as the loader set it: below AppleSoft's INPUT bytes
        txs
        sep     #$30
        .i8
        lda     #1
        sta     x_resetf
        ldx     #$5A            ; warm
        jmp     x_enter

; the launcher's bytes, run at TRAMP ($1F00): the file to $2000, the banks
; back, ProDOS 8's bitmap as at a system program's start, the file run in
; emulation.  (Its own addresses: TRAMP + offset.)
TRAMP    = $1F00
T_LEN    = TRAMP + x_tlen - x_tramp
T_ID     = TRAMP + x_tid - x_tramp
T_MVN    = TRAMP + x_tmvn - x_tramp
        .a16
        .i16
x_tramp:
        lda     T_LEN
        dec     a
        ldx     #0
        ldy     #$2000
        phb
x_tmvn: .byte   $54, $00, $00   ; MVN source bank (patched) to bank 0
        plb
        lda     T_ID            ; DisposeAll, DeleteID
        pha
        ldx     #$1102
        jsl     TOOLS
        lda     T_ID
        pha
        ldx     #$2103
        jsl     TOOLS
        lda     #0
        tcd
        sep     #$30
        .a8
        .i8
        ldx     #$17
        lda     #0
@bm:    sta     $BF58,x
        dex
        bpl     @bm
        lda     #$CF            ; pages 0, 1, 4-7
        sta     $BF58
        lda     #$01            ; $BF
        sta     $BF6F
        sec
        xce
        ldx     #$FF
        txs
        jmp     $2000
x_tlen: .word   0
x_tid:  .word   0
x_tramp_end:

        .segment "LOADER"
; ---- data ----
user_id: .word  0
err_code: .word 0
banks:
vm_bank:   .byte 0
str_bank:  .byte 0
sh_bank:   .byte 0              ; the shell image (AS816.SH): code, data
sh_data:   .byte 0
cc_bank:   .byte 0              ; the compiler image (AS816.CC): code, data
cc_data:   .byte 0
text_bank: .byte 0
xfer_bank: .byte 0              ; CHAIN's variables
stage_bank: .byte 0             ; the variables' table (STORE, CHAIN)
NBANKS = * - banks
; which of banks: the shell gets, and where in its block
shp_from: .byte vm_bank - banks, text_bank - banks, str_bank - banks, xfer_bank - banks
        .byte   stage_bank - banks, cc_bank - banks, cc_data - banks
shp_to: .byte   SHP_VM_BANK, SHP_TEXT_BANK, SHP_STR_BANK, SHP_XFER_BANK
        .byte   SHP_STAGE_BANK, SHP_CC_BANK, SHP_CC_DATA
SHP_N = * - shp_to
dest:    .word  0

open_parms:
        .byte   3
open_path: .word 0
        .word   RES_FBUF
open_ref: .byte 0
read_parms:
        .byte   4
read_ref: .byte 0
        .word   RES_IOBUF
        .word   RES_IOSIZE
read_trans: .word 0
close_parms:
        .byte   1
close_ref: .byte 0
prefix_parms:
        .byte   1
        .word   RES_PATH
online_parms:
        .byte   2
online_unit: .byte 0
        .word   RES_PATH+1
quit_parms:
        .byte   4, 0
        .word   0
        .byte   0
        .word   0

name_vm: .byte  8, "AS816.VM"
name_cc: .byte  8, "AS816.CC"
name_sh: .byte  8, "AS816.SH"
msgs:
msg_notgs: .byte "APPLESOFT816 NEEDS AN APPLE IIGS", 0
msg_mm:    .byte "AS816: NO USER ID", 0
msg_mem:   .byte "AS816: NOT ENOUGH MEMORY", 0
msg_load:  .byte "AS816: CANNOT LOAD AS816.VM/.SH/.CC", 0

; =========================================================================
; The resident trampolines, run at RES_MLI (bank 0).  Both are JSL'd from
; native code in any bank, with any M/X; both return with the caller's P,
; D and DBR.  S must be in page 1 (it always is).
; =========================================================================
        .segment "RESIDENT"
        .a8
        .i8
; MLI: command at RES_CMD, parameters at RES_PARMS; error to RES_ERR
mli_entry:
        php
        phd
        phb
        pea     $0000
        pld
        phk
        plb
        sep     #$30
        lda     RES_CMD
        sta     @cmd
        sec
        xce
        jsr     MLI
@cmd:   .byte   0
        .word   RES_PARMS
        clc
        xce
        sta     RES_ERR
        plb
        pld
        plp
        rtl
        .res    $40 - (* - mli_entry)

; ROM: JSR (RES_TGT) in emulation mode with A, X, Y from RES_A/X/Y; they
; and P come back there
rom_entry:
        php
        phd
        phb
        pea     $0000
        pld
        phk
        plb
        sep     #$30
        sec
        xce
        lda     RES_A
        ldx     RES_X
        ldy     RES_Y
        pea     @back - 1
        jmp     (RES_TGT)
@back:  sta     RES_A
        stx     RES_X
        sty     RES_Y
        php
        pla
        sta     RES_P
        clc
        xce
        plb
        pld
        plp
        rtl
