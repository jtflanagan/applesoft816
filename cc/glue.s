; glue.s - the C images' reach beyond their data bank: far reads, writes and
; moves, bank 0 (shared by the shell, main_sh.c, and the compiler, main_cc.c).
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
        .global xfer_setup
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
