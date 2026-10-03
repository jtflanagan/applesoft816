; crt0.s - the compiler's way in and out on the 65816 (cc/link.ld).
;
; JSL to the code bank's $0000.  The data bank is the next one.  What the
; caller gets back: every register (P, A, X, Y, DBR, D) and page zero's
; $00-$1F.  What C gets: D = 0 (its imaginary registers), DBR = the data
; bank, M = X = 1, .rodata/.data copied in, .bss cleared, the soft stack.
;
; Sixteen-bit immediates and absolute operands are spelled out (.byte
; opcode, .short operand): the assembler sizes `ldx #symbol` as eight bits
; under rep #$30, and gives a symbol below $100 a direct-page mode.
        .include "imag.inc"

        .section .entry,"ax",@progbits
        .global __cc_entry
__cc_entry:
        .byte   0x4c                    ; $0000  jmp entry
        .short  entry
        .byte   0
        .ascii  "CC16"                  ; $0004
        .short  cc_params               ; $0008  the parameter block (data bank)
        .short  __image_end             ; $000A  the code bank's bytes to load

entry:
        php
        rep     #$30
        pha
        phx
        phy
        phb
        phd
        .byte   0xa9                    ; lda #0
        .short  0
        tcd                             ; D = 0
        cld
        sep     #$30
        phk                             ; DBR = the code bank + 1
        pla
        inc     a
        pha
        plb
        ldx     #$1f                    ; the caller's page zero
save_zp:
        lda     $00,x
        sta     mos16(zp_save),x
        dex
        bpl     save_zp
        jsr     crt_init
        jsr     cc_main
        ldx     #$1f
restore_zp:
        lda     mos16(zp_save),x
        sta     $00,x
        dex
        bpl     restore_zp
        rep     #$30
        pld
        plb
        ply
        plx
        pla
        plp
        rtl

; .rodata/.data from the code bank's image into the data bank, .bss
; cleared, the soft stack; leaves M = X = 1.  Long pointers both ways:
; __rc2-4 the image (code bank), __rc5-7 the data bank.
crt_init:
        phk
        pla
        sta     __rc4
        phb
        pla
        sta     __rc7
        lda     #__copy_lma_lo
        sta     __rc2
        lda     #__copy_lma_hi
        sta     __rc3
        lda     #__copy_vma_lo
        sta     __rc5
        lda     #__copy_vma_hi
        sta     __rc6
        rep     #$10                    ; X/Y 16-bit, A 8-bit
        .byte   0xa0                    ; ldy #0
        .short  0
copy:   .byte   0xc0                    ; cpy #__copy_size
        .short  __copy_size
        beq     copied
        lda     [__rc2],y
        sta     [__rc5],y
        iny
        bra     copy
copied: lda     #__bss_off_lo
        sta     __rc5
        lda     #__bss_off_hi
        sta     __rc6
        .byte   0xa0                    ; ldy #0
        .short  0
zero:   .byte   0xc0                    ; cpy #__bss_size
        .short  __bss_size
        beq     zeroed
        lda     #0
        sta     [__rc5],y
        iny
        bra     zero
zeroed: sep     #$30
        lda     #__stack_lo
        sta     __rc0
        lda     #__stack_hi
        sta     __rc1
        rts

        .section .noinit.zp_save,"aw",@nobits
zp_save:
        .zero   32
