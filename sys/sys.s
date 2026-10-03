; sys.s - AS816.SYSTEM: applesoft816 under ProDOS 8 on a IIgs (docs/M5.md).
;
; ProDOS 8 runs it at $2000 in emulation mode.  It checks for a IIgs, gets
; whole banks from the Memory Manager (the VM's, the string heap's, the
; shell's code and data, the program text's), loads AS816.VM and AS816.CC
; into them, sets up the resident bank-0 block (res.inc: the VM's direct
; page and stubs, the MLI and ROM trampolines, I/O buffers) and calls the
; shell (cc/, crt0's entry).  When the shell returns: the banks back, QUIT.

        .p816
        .smart  off
        .include "res.inc"
        .import __RESIDENT_LOAD__, __RESIDENT_RUN__, __RESIDENT_SIZE__

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
CCP_VM_BANK = 0
CCP_TEXT_BANK = 1
CCP_VM_DP = 2
CCP_MODE = 20
CCP_STR_BANK = 21
CCP_USER_ID = 22
CCP_DATA_BANK = 24

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
        ldx     #0              ; vm_bank, str_bank, cc_bank, cc_data, text_bank
@alloc: phx
        jsr     alloc_bank
        plx
        sep     #$20
        .a8
        sta     banks,x
        rep     #$20
        .a16
        inx
        cpx     #5
        bne     @alloc
        sec
        xce
        .a8
        .i8

        lda     #<name_vm
        ldx     #>name_vm
        ldy     vm_bank
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
        lda     cc_bank
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
        lda     cc_data
        sta     HND+2
        ldy     #CCP_VM_BANK
        lda     vm_bank
        sta     [HND],y
        ldy     #CCP_TEXT_BANK
        lda     text_bank
        sta     [HND],y
        ldy     #CCP_VM_DP
        lda     #<RES_DP
        sta     [HND],y
        iny
        lda     #>RES_DP
        sta     [HND],y
        ldy     #CCP_MODE
        lda     #1
        sta     [HND],y
        ldy     #CCP_STR_BANK
        lda     str_bank
        sta     [HND],y
        ldy     #CCP_USER_ID
        lda     user_id
        sta     [HND],y
        iny
        lda     user_id+1
        sta     [HND],y

        ; the shell: JSL its bank's $0000 with A = its data bank
        lda     cc_bank
        sta     shell_call + 3
        lda     cc_data
shell_call:
        jsl     $000000         ; bank patched above
        sep     #$30
        jmp     quit_native

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

; ---- data ----
user_id: .word  0
err_code: .word 0
banks:
vm_bank:   .byte 0
str_bank:  .byte 0
cc_bank:   .byte 0
cc_data:   .byte 0
text_bank: .byte 0
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
msgs:
msg_notgs: .byte "APPLESOFT816 NEEDS AN APPLE IIGS", 0
msg_mm:    .byte "AS816: NO USER ID", 0
msg_mem:   .byte "AS816: NOT ENOUGH MEMORY", 0
msg_load:  .byte "AS816: CANNOT LOAD AS816.VM/.CC", 0

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
