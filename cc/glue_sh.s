; glue_sh.s - the shell image's glue (main_sh.c): the machine around it -
; the ROM, the MLI, the VM, the compiler image - and the program text.
        .include "imag.inc"

        .text

; ---- bank 0, and the machine around the shell (sys/res.inc, cc/res.h) ----
        .set    RES_MLI, 0x9780
        .set    RES_ROM, 0x97C0
        .set    RES_CMD, 0x9800
        .set    RES_ERR, 0x9801
        .set    RES_A, 0x9802
        .set    RES_TGT, 0x9806

; zp_swap: page zero's $00-$1F (C's registers) <-> zp_save (the machine's,
; saved by crt0).  Around every call out to the machine - the ROM, the MLI,
; the VM - so they see the machine's page zero; no __rc register is valid
; between the two swaps.
zp_swap:
        ldx     #0x1f
zp_swap_1:
        lda     0x00,x
        tay
        lda     mos16(zp_save),x
        sta     0x00,x
        tya
        sta     mos16(zp_save),x
        dex
        bpl     zp_swap_1
        rts

; uint8_t mli_call(uint8_t cmd): the MLI with RES_PARMS; its error, 0 = none
        .global mli_call
mli_call:
        .byte   0x8f                    ; sta RES_CMD (long, bank 0)
        .short  RES_CMD
        .byte   0
        jsr     zp_swap
        .byte   0x22                    ; jsl RES_MLI
        .short  RES_MLI
        .byte   0
        jsr     zp_swap
        .byte   0xaf                    ; lda RES_ERR
        .short  RES_ERR
        .byte   0
        rts

; uint8_t rom_call(uint16_t addr): JSR addr in emulation mode, A/X/Y from
; RES_A/X/Y (and back there, with P); returns A
        .global rom_call
rom_call:
        .byte   0x8f                    ; sta RES_TGT
        .short  RES_TGT
        .byte   0
        txa
        .byte   0x8f                    ; sta RES_TGT+1
        .short  RES_TGT + 1
        .byte   0
        jsr     zp_swap
        .byte   0x22                    ; jsl RES_ROM
        .short  RES_ROM
        .byte   0
        jsr     zp_swap
        .byte   0xaf                    ; lda RES_A
        .short  RES_A
        .byte   0
        rts

; uint8_t vm_call(uint16_t entry): JSL the VM bank's entry (vm_run at
; $0000, vm_direct) with D = its direct page; returns its VM_ERR
        .global vm_call
vm_call:
        sta     mos16(vm_entry)
        stx     mos16(vm_entry+1)
        jsr     zp_swap
        phk                             ; the VM's RTL comes back to vm_back
        .byte   0xf4                    ; pea vm_back - 1
        .short  vm_back - 1
        lda     mos16(vm_bank)          ; "return" to vm_bank:entry
        pha
        rep     #0x20
        lda     mos16(vm_entry)
        dec     a
        pha
        lda     mos16(vm_dp)
        tcd
        sep     #0x20
        rtl
vm_back:
        .byte   0xf4                    ; pea 0
        .short  0
        pld
        sep     #0x30
        jsr     zp_swap
        lda     mos16(vm_dp)
        sta     __rc2
        lda     mos16(vm_dp+1)
        sta     __rc3
        stz     __rc4
        ldy     #0x2f                   ; VM_ERR
        lda     [__rc2],y
        rts

; uint16_t fix_links(void): AppleSoft's FIX.LINKS over the program at $0801
; of the text bank - each line's link from where its 0 is - up to the line
; whose link's high byte is 0; returns that end's address
        .global fix_links
fix_links:
        lda     mos16(text_base+2)
        sta     __rc4
        rep     #0x30
        .byte   0xa9                    ; lda #$0801
        .short  0x0801
        sta     __rc2
fl_line:
        sep     #0x20
        .byte   0xa0                    ; ldy #1
        .short  1
        lda     [__rc2],y
        beq     fl_end
        .byte   0xa0                    ; ldy #4
        .short  4
fl_scan:
        lda     [__rc2],y
        beq     fl_zero
        iny
        bra     fl_scan
fl_zero:
        iny                             ; the next line
        rep     #0x20
        tya
        clc
        adc     __rc2
        sta     [__rc2]
        sta     __rc2
        bra     fl_line
fl_end:
        sep     #0x30
        lda     __rc2
        ldx     __rc3
        rts

; cmd_entry: the VM's JSL for a CHR$(4) command (vm/vm.s outdo), with A =
; this data bank, D = the VM's page, DBR = its bank, M = 1, X = 0.  C's
; registers come back into page zero (the VM had the machine's there) for
; vm_command(); its answer goes back in A.  Everything else as it was.
cmd_entry:
        php
        phb
        phd
        phx
        phy
        pha
        plb
        .byte   0xf4                    ; pea 0
        .short  0
        pld
        sep     #0x30
        jsr     zp_swap
        jsr     vm_command
        sta     mos16(cmd_answer)
        jsr     zp_swap
        lda     mos16(cmd_answer)
        rep     #0x10
        ply
        plx
        pld
        plb
        plp
        rtl

; void vm_hook(void): tell the VM (its page: VM_CMDDBR, VM_CMDVEC) where
; cmd_entry is
        .global vm_hook
vm_hook:
        lda     mos16(vm_dp)
        sta     __rc2
        lda     mos16(vm_dp+1)
        sta     __rc3
        stz     __rc4
        phb                             ; VM_CMDDBR ($1A): this data bank
        pla
        ldy     #0x1a
        sta     [__rc2],y
        lda     #mos16lo(cmd_entry)     ; VM_CMDVEC ($1B): this bank's cmd_entry
        iny
        sta     [__rc2],y
        lda     #mos16hi(cmd_entry)
        iny
        sta     [__rc2],y
        phk
        pla
        iny
        sta     [__rc2],y
        rts

; void cc_call(uint8_t warm): the compiler image (main_cc.c) - JSL its code
; bank's $0000 with A = its data bank and X = $5A when warm (it has run: its
; crt0 keeps its .data/.bss); its crt0 keeps our page zero
        .global cc_call
cc_call:
        sta     __rc2
        phk                             ; its RTL comes back to cc_back
        .byte   0xf4                    ; pea cc_back - 1
        .short  cc_back - 1
        lda     mos16(cc_bank)          ; "return" to cc_bank:$0000
        pha
        .byte   0xf4                    ; pea $FFFF
        .short  0xffff
        ldx     #0
        lda     __rc2
        beq     cc_cold
        ldx     #0x5a
cc_cold:
        lda     mos16(cc_data)
        rtl
cc_back:
        rts
