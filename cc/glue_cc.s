; glue_cc.s - the compiler image's own glue (main_cc.c): FIN through the VM.
        .include "imag.inc"

        .text

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
