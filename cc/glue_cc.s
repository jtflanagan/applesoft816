; glue_cc.s - the compiler image's own glue (main_cc.c): FIN through the VM.
        .include "imag.inc"

        .text

; int16_t fin_call(uint16_t text, uint8_t *fac): fp816's FIN on the number
; at text (this data bank), on a direct page of its own (RES_CCDP, $BC00:
; a lazy compile runs while the program does, whose page is the VM's);
; fac[7] = exponent, mantissa, sign, rounding byte; returns the characters
; FIN took, or -1 for its error.  The call: D = that page, a JML through a
; pointer in bank 0 (JML [abs] reads bank 0 whatever D is), back by fp816's
; RTL.
        .global fin_call
fin_call:
        sta     __rc4                   ; text
        stx     __rc5
        stz     __rc6                   ; __rc6-8: FIN's direct page, long
        lda     #$bc
        sta     __rc7
        stz     __rc8
        lda     #0                      ; SHIFT.SIGN.EXT: 0, as outside QINT
        ldy     #$a4
        sta     [__rc6],y
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

; uint8_t scan_skip(const uint8_t *t, uint8_t j, uint8_t n): the index of
; the first byte from t[j] (below n) the compiler's scan acts on - a letter,
; ":", REM DATA & SHLOAD RECALL STORE SAVE LOAD ONERR RESUME TRACE DEF -
; passing over quoted text; n if none.  (compile.c's scan_skip, in C for the
; PC build: the same.)  t in __rc2/3, j in A, n in X.
        .global scan_skip
scan_skip:
        tay
        stx     __rc4
        cpy     __rc4
        bcs     ss_done
ss_loop:
        lda     (__rc2),y
        tax
        lda     mos16(scan_class),x
        bne     ss_special
ss_next:
        iny
        cpy     __rc4
        bne     ss_loop
ss_done:
        tya
        rts
ss_special:
        cmp     #2
        bne     ss_done
ss_quote:                               ; to the closing quote, past it
        iny
        cpy     __rc4
        beq     ss_done
        lda     (__rc2),y
        cmp     #0x22
        bne     ss_quote
        bra     ss_next

        .section .rodata.scan_class,"a"
scan_class:
        .byte   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        .byte   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        .byte   0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        .byte   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0
        .byte   0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
        .byte   1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
        .byte   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        .byte   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        .byte   0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        .byte   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0
        .byte   0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1
        .byte   0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0
        .byte   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        .byte   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        .byte   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        .byte   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        .text
