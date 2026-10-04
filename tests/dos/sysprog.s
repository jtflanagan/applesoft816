; sysprog.s - a system program for "-" (tests/dos/sys1.txt): a 40-column
; screen, then what it was started with - its path at $0280, ProDOS 8's
; bitmap ($BF58-$BF6F) - and it waits for ever.
        .p02
        .org    $2000
start:  sta     $C00C           ; 80 columns off, 80STORE off
        sta     $C000
        jsr     $FB2F           ; INIT
        jsr     $FE93           ; SETVID
        jsr     $FE89           ; SETKBD
        jsr     $FC58           ; HOME
        ldx     #0
@m:     lda     msg,x
        beq     @p
        jsr     $FDED
        inx
        bne     @m
@p:     ldx     #0
@pa:    cpx     $0280
        beq     @bm
        lda     $0281,x
        ora     #$80
        jsr     $FDED
        inx
        bne     @pa
@bm:    jsr     $FD8E
        ldx     #0
@b:     lda     $BF58,x
        jsr     $FDDA           ; PRBYTE
        inx
        cpx     #$18
        bne     @b
        jsr     $FD8E
@w:     jmp     @w
msg:    .byte   $D3, $D9, $D3, $A0, $CF, $CB, $A0, 0
