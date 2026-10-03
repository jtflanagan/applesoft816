; glue.s - the compiler's reach beyond its data bank (cc/main.c).
;
; The calling convention (llvm-mos): a 32-bit argument's bytes arrive in A,
; X, __rc2, __rc3; a 16-bit one in A, X; a second argument in the next
; registers; a byte result leaves in A, a 16-bit one in A (low) and X.
; C runs with D = 0, DBR = its data bank, M = X = 1.
        .include "imag.inc"

        .text

; uint8_t peek24(uint32_t addr)
        .global peek24
peek24:
        sta     __rc4
        stx     __rc5
        lda     __rc2
        sta     __rc6
        lda     [__rc4]
        rts

; far_in / far_out: xfer_n bytes (1-65535) between the long address
; xfer_far and xfer_near in this data bank, in (far to near) or out.  Both
; addresses are ONE BELOW the first byte (main.c sets them so), which lets
; the loop count Y down from xfer_n to 1.
        .global far_in
far_in:
        jsr     xfer_setup
in_loop:
        lda     [__rc2],y
        sta     (__rc5),y
        dey
        bne     in_loop
        sep     #$10
        rts

        .global far_out
far_out:
        jsr     xfer_setup
out_loop:
        lda     (__rc5),y
        sta     [__rc2],y
        dey
        bne     out_loop
        sep     #$10
        rts

; __rc2-4 = xfer_far, __rc5-6 = xfer_near, X/Y 16-bit, Y = xfer_n
xfer_setup:
        lda     mos16(xfer_far)
        sta     __rc2
        lda     mos16(xfer_far+1)
        sta     __rc3
        lda     mos16(xfer_far+2)
        sta     __rc4
        lda     mos16(xfer_near)
        sta     __rc5
        lda     mos16(xfer_near+1)
        sta     __rc6
        rep     #$10
        ldy     mos16(xfer_n)
        rts

; int16_t fin_call(uint16_t text, uint8_t *fac): fp816's FIN on the number
; at text (this data bank), with the VM's direct page; fac[7] = exponent,
; mantissa, sign, rounding byte; returns the characters FIN took, or -1 for
; its error.  The call: D = the VM's page, a JML through a pointer in bank
; 0 (JML [abs] reads bank 0 whatever D is), back by fp816's RTL.
        .global fin_call
fin_call:
        sta     __rc4                   ; text
        stx     __rc5
        lda     mos16(vm_dp)                   ; __rc6-8: the VM's direct page, long
        sta     __rc6
        lda     mos16(vm_dp+1)
        sta     __rc7
        stz     __rc8
        lda     __rc4                   ; FP_TXT ($FA) = text - 1, this bank
        sec
        sbc     #1
        ldy     #$fa
        sta     [__rc6],y
        lda     __rc5
        sbc     #0
        iny
        sta     [__rc6],y
        phb
        pla
        iny
        sta     [__rc6],y
        lda     #0                      ; FAC.EXTENSION
        ldy     #$ac
        sta     [__rc6],y
        lda     mos16(fp_fin_addr)             ; __rc12-14: the entry
        sta     __rc12
        lda     mos16(fp_fin_addr+1)
        sta     __rc13
        lda     mos16(vm_bank)
        sta     __rc14
        phk                             ; the return frame for its RTL
        .byte   0xf4                    ; pea fin_back - 1
        .short  fin_back - 1
        rep     #$20
        lda     __rc6
        tcd
        sep     #$20
        .byte   0xdc                    ; jml [__rc12] (absolute, bank 0)
        .short  __rc12
fin_back:
        .byte   0xf4                    ; pea 0
        .short  0
        pld                             ; D = 0 again
        ldy     #$f2                    ; FP_ERR
        lda     [__rc6],y
        bne     fin_fail
        ldy     #$9d                    ; FAC: $9D-$A2, then the rounding byte
        lda     [__rc6],y
        sta     (__rc2)
        ldx     #1
fin_fac:
        txa
        clc
        adc     #$9d
        tay
        lda     [__rc6],y
        phx
        ply
        sta     (__rc2),y
        inx
        cpx     #6
        bne     fin_fac
        ldy     #$ac
        lda     [__rc6],y
        ldy     #6
        sta     (__rc2),y
        ldy     #$fa                    ; taken: FP_TXT - text
        lda     [__rc6],y
        sec
        sbc     __rc4
        sta     __rc10
        iny
        lda     [__rc6],y
        sbc     __rc5
        tax
        lda     __rc10
        rts
fin_fail:
        lda     #$ff
        tax
        rts

; ---- bank 0, and the machine around the shell (sys/res.inc, cc/res.h) ----
        .set    RES_MLI, 0x9780
        .set    RES_ROM, 0x97C0
        .set    RES_CMD, 0x9800
        .set    RES_ERR, 0x9801
        .set    RES_A, 0x9802
        .set    RES_TGT, 0x9806

; uint8_t peek0(uint16_t a)
        .global peek0
peek0:
        sta     __rc2
        stx     __rc3
        stz     __rc4
        lda     [__rc2]
        rts

; void poke0(uint16_t a, uint8_t v)     (v in __rc2)
        .global poke0
poke0:
        ldy     __rc2
        sta     __rc2
        stx     __rc3
        stz     __rc4
        tya
        sta     [__rc2]
        rts

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

; far_move: xfer_n bytes from xfer_far to xfer_far2 (both one below)
        .global far_move
far_move:
        jsr     xfer_setup
        lda     mos16(xfer_far2)
        sta     __rc7
        lda     mos16(xfer_far2+1)
        sta     __rc8
        lda     mos16(xfer_far2+2)
        sta     __rc9
move_loop:
        lda     [__rc2],y
        sta     [__rc7],y
        dey
        bne     move_loop
        sep     #0x10
        rts

; far_hash: hash_acc over xfer_n bytes at xfer_far (one below), last byte
; first: h = (h rotated left 1) ^ byte
        .global far_hash
far_hash:
        jsr     xfer_setup
        rep     #0x20
        lda     mos16(hash_acc)
        sta     __rc7
hash_loop:
        lda     [__rc2],y
        .byte   0x29                    ; and #$00FF (sixteen bits)
        .short  0x00ff
        sta     __rc9
        lda     __rc7
        asl     a
        .byte   0x69                    ; adc #0
        .short  0
        eor     __rc9
        sta     __rc7
        dey
        bne     hash_loop
        sta     mos16(hash_acc)
        sep     #0x30
        rts

; far_move_up: as far_move, but first byte first - for a move to lower
; addresses that overlaps (far_move, last byte first, is the one for higher)
        .global far_move_up
far_move_up:
        jsr     xfer_setup
        lda     mos16(xfer_far2)
        sta     __rc7
        lda     mos16(xfer_far2+1)
        sta     __rc8
        lda     mos16(xfer_far2+2)
        sta     __rc9
        sty     __rc10                  ; n (sixteen bits)
        .byte   0xa0                    ; ldy #1
        .short  1
up_loop:
        lda     [__rc2],y
        sta     [__rc7],y
        cpy     __rc10
        beq     up_done
        iny
        bra     up_loop
up_done:
        sep     #0x10
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
