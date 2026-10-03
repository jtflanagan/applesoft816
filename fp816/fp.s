; fp816: AppleSoft's floating point for the 65816, bit-exact with the ROM.
;
; Stage 1: a line-for-line port of the ROM routines (IIgs ROM 3 bank $FF ==
; enhanced IIe, $E7A0-$F10A), in native mode, on any direct page, with long
; operand pointers.  Every routine is checked against the ROM's own by
; fptest; the 16-bit rewrites come after, against the same oracle.
;
; Calling: native mode (E=0), any M/X (preserved), D = the FP direct page,
; DBR anything.  JSL an entry in the jump table at the start of the bank;
; each returns with RTL.  A memory operand is a 5-byte packed float at the
; 24-bit address in FP_PTR (not preserved).  FP_ERR is 0, or the AppleSoft
; error code ($45 ?OVERFLOW, $85 ?DIVISION BY ZERO) when the operation
; failed (FAC is then undefined).  FP_RES carries a byte result (FCOMP, SIGN).
;
; The direct page uses AppleSoft's zero-page offsets for FAC and ARG so a
; state can be compared byte for byte with the ROM's.

        .p816
        .smart  off

; ---- direct page (offsets as AppleSoft's zero page) ----
FP_PTR   = $5E          ; 3 bytes: operand address (ours; AppleSoft INDEX is 2)
ARG_EXT  = $92          ; ARG.EXTENSION
FAC      = $9D          ; FAC exponent; mantissa $9E-$A1
FACSIGN  = $A2
SHIFTEXT = $A4          ; SHIFT.SIGN.EXT: fill byte for whole-byte right shifts
ARG      = $A5          ; ARG exponent; mantissa $A6-$A9
ARGSIGN  = $AA
SGNCPR   = $AB          ; sign compare: ARGSIGN eor FACSIGN
FAC_EXT  = $AC          ; FAC.EXTENSION (rounding byte)
RESULT   = $62          ; FMULT/FDIV product/quotient, 4 bytes
FP_SP    = $F0          ; 2 bytes: S at entry, for error unwinding (ours)
FP_ERR   = $F2          ; error code, 0 = none (ours)
FP_RES   = $F3          ; byte result (ours)
FP_DPP   = $F4          ; 3 bytes: long pointer to this direct page (ours)

CHARAC   = $0D          ; INT leaves FAC's low mantissa byte here
INDX     = $99          ; FIN: digits after the point
EXPON    = $9A          ; FIN: decimal exponent
DPFLG    = $9B          ; FIN: decimal point seen
EXPSGN   = $9C          ; FIN: exponent sign
SERLEN   = $A3          ; FIN: mantissa sign ($FF negative)
FP_TXT   = $FA          ; 3 bytes: text pointer for FIN (ours; AppleSoft TXTPTR)
LINNUM   = $50          ; GETADR's result, 2 bytes
FP_INT   = $F8          ; 2 bytes: GIVAYF's input, low first (ours)

ERR_OVERFLOW = $45
ERR_DIVZERO  = $85
ERR_ILLQTY   = $35

; ---- entry/exit ----
.macro  ENTER
        php
        rep     #$30
        .a16
        .i16
        tsc
        sta     FP_SP
        tdc
        sta     FP_DPP
        sep     #$30
        .a8
        .i8
        stz     FP_DPP+2
        stz     FP_ERR
.endmacro

.macro  LEAVE
        plp
        rtl
.endmacro

        .segment "CODE"

; ---- jump table: the public entries, 4 bytes apart ----
        jmp     fp_load_fac        ; +$00  FAC = [FP_PTR]
        nop
        jmp     fp_load_arg        ; +$04  ARG = [FP_PTR]; SGNCPR
        nop
        jmp     fp_store_fac       ; +$08  [FP_PTR] = round(FAC)
        nop
        jmp     fp_add             ; +$0C  FAC = [FP_PTR] + FAC   (ROM FADD)
        nop
        jmp     fp_sub             ; +$10  FAC = [FP_PTR] - FAC   (ROM FSUB)
        nop
        jmp     fp_addt            ; +$14  FAC = ARG + FAC        (ROM FADDT)
        nop
        jmp     fp_subt            ; +$18  FAC = ARG - FAC        (ROM FSUBT)
        nop
        jmp     fp_mul             ; +$1C  FAC = [FP_PTR] * FAC   (ROM FMULT)
        nop
        jmp     fp_div             ; +$20  FAC = [FP_PTR] / FAC   (ROM FDIV)
        nop
        jmp     fp_cmp             ; +$24  FP_RES = FCOMP([FP_PTR]): 0 =, 1, $FF
        nop
        jmp     fp_mul10           ; +$28  FAC = FAC * 10         (ROM MUL10)
        nop
        jmp     fp_div10           ; +$2C  FAC = FAC / 10         (ROM DIV10)
        nop
        jmp     fp_sgn             ; +$30  FAC = SGN(FAC)         (ROM SGN)
        nop
        jmp     fp_abs             ; +$34  FAC = ABS(FAC)         (ROM ABS)
        nop
        jmp     fp_int             ; +$38  FAC = INT(FAC)         (ROM INT)
        nop
        jmp     fp_qint            ; +$3C  FAC mantissa = 32-bit integer (ROM QINT)
        nop
        jmp     fp_ayint           ; +$40  FAC+3,4 = signed 16-bit or ?ILLEGAL QUANTITY
        nop
        jmp     fp_givayf          ; +$44  FAC = signed 16-bit FP_INT (ROM GIVAYF)
        nop
        jmp     fp_conint          ; +$48  FP_RES = byte 0-255 or ?ILLEGAL QUANTITY
        nop
        jmp     fp_getadr          ; +$4C  LINNUM = 16-bit address or ?ILLEGAL QUANTITY
        nop
        jmp     fp_fin             ; +$50  FAC = the number at FP_TXT+1 (CHRGET, FIN)
        nop
        jmp     fp_negop           ; +$54  FAC = -FAC              (ROM NEGOP)
        nop

; ---- public entries ----
        .a8
        .i8

fp_load_fac:
        ENTER
        jsr     load_fac_from_ptr
        LEAVE

fp_load_arg:
        ENTER
        jsr     load_arg_from_ptr
        LEAVE

fp_store_fac:
        ENTER
        jsr     store_fac_at_ptr_rounded
        LEAVE

fp_add:
        ENTER
        jsr     load_arg_from_ptr  ; returns A = FAC exponent, flags from it
        jsr     faddt
        LEAVE

fp_sub:
        ENTER
        jsr     load_arg_from_ptr
        jsr     fsubt
        LEAVE

fp_addt:
        ENTER
        lda     FAC
        jsr     faddt
        LEAVE

fp_subt:
        ENTER
        jsr     fsubt
        LEAVE

fp_mul:
        ENTER
        jsr     load_arg_from_ptr
        jsr     fmultt
        LEAVE

fp_div:
        ENTER
        jsr     load_arg_from_ptr
        jsr     fdivt
        LEAVE

fp_cmp:
        ENTER
        jsr     fcomp
        sta     FP_RES
        LEAVE

fp_mul10:
        ENTER
        jsr     mul10
        LEAVE

fp_div10:
        ENTER
        jsr     div10
        LEAVE

fp_sgn:
        ENTER
        jsr     sgn
        LEAVE

fp_abs:
        ENTER
        lsr     FACSIGN         ; ABS ($EBAF)
        LEAVE

fp_int:
        ENTER
        jsr     int
        LEAVE

fp_qint:
        ENTER
        jsr     qint
        LEAVE

fp_ayint:
        ENTER
        jsr     ayint
        LEAVE

fp_givayf:
        ENTER
        lda     FP_INT+1
        ldy     FP_INT
        jsr     givayf
        LEAVE

fp_conint:
        ENTER
        jsr     conint
        stx     FP_RES
        LEAVE

fp_getadr:
        ENTER
        jsr     getadr
        LEAVE

; FIN as AppleSoft calls it: CHRGET to the first character, then FIN.
; FP_TXT is left on the character that ended the number.
fp_fin:
        ENTER
        jsr     chrget
        jsr     fin
        LEAVE

fp_negop:
        ENTER
        jsr     negop
        LEAVE

; Error: unwind to the entry's frame and return with FP_ERR set.
fp_error:                       ; A = error code
        rep     #$30
        .a16
        .i16
        and     #$00FF
        tax
        lda     FP_SP
        tcs
        sep     #$30
        .a8
        .i8
        stx     FP_ERR
        LEAVE

; =========================================================================
; The port.  ROM labels in comments; same instructions, same order, so the
; flags each step leaves for the next are the ROM's.
; =========================================================================

; LOAD.ARG.FROM.YA (EB.. $E9E3)
load_arg_from_ptr:
        ldy     #$04
        lda     [FP_PTR],y
        sta     ARG+4
        dey
        lda     [FP_PTR],y
        sta     ARG+3
        dey
        lda     [FP_PTR],y
        sta     ARG+2
        dey
        lda     [FP_PTR],y
        sta     ARGSIGN
        eor     FACSIGN
        sta     SGNCPR
        lda     ARGSIGN
        ora     #$80
        sta     ARG+1
        dey
        lda     [FP_PTR],y
        sta     ARG
        lda     FAC
        rts

; LOAD.FAC.FROM.YA ($EAF9)
load_fac_from_ptr:
        ldy     #$04
        lda     [FP_PTR],y
        sta     FAC+4
        dey
        lda     [FP_PTR],y
        sta     FAC+3
        dey
        lda     [FP_PTR],y
        sta     FAC+2
        dey
        lda     [FP_PTR],y
        sta     FACSIGN
        ora     #$80
        sta     FAC+1
        dey
        lda     [FP_PTR],y
        sta     FAC
        sty     FAC_EXT
        rts

; STORE.FAC.AT.YX.ROUNDED ($EB2B)
store_fac_at_ptr_rounded:
        jsr     round_fac
        ldy     #$04
        lda     FAC+4
        sta     [FP_PTR],y
        dey
        lda     FAC+3
        sta     [FP_PTR],y
        dey
        lda     FAC+2
        sta     [FP_PTR],y
        dey
        lda     FACSIGN
        ora     #$7F
        and     FAC+1
        sta     [FP_PTR],y
        dey
        lda     FAC
        sta     [FP_PTR],y
        sty     FAC_EXT
        rts

; COPY.ARG.TO.FAC ($EB53)
copy_arg_to_fac:
        lda     ARGSIGN
mfa:    sta     FACSIGN
        ldx     #$05
@1:     lda     SHIFTEXT,x
        sta     FAC-1,x
        dex
        bne     @1
        stx     FAC_EXT
        rts

; ROUND.FAC ($EB72)
round_fac:
        lda     FAC
        beq     @rts
        asl     FAC_EXT
        bcc     @rts
        jsr     increment_fac_mantissa
        bne     @rts
        jmp     normalize_fac_6
@rts:   rts

; FSUBT ($E7AA)
fsubt:
        lda     FACSIGN
        eor     #$FF
        sta     FACSIGN
        eor     ARGSIGN
        sta     SGNCPR
        lda     FAC
        jmp     faddt

; FADD.1 ($E7B9)
fadd_1:
        jsr     shift_right
        bcc     fadd_3

; FADDT ($E7C1): FAC = ARG + FAC; A = FAC exponent with flags on entry
faddt:
        bne     faddt_le7c6
        jmp     copy_arg_to_fac
faddt_le7c6: ldx     FAC_EXT
        stx     ARG_EXT
        ldx     #ARG
        lda     ARG
fadd_2: tay                     ; FADD.2 ($E7CE): A = ARG exponent
        beq     faddt_rts
        sec
        sbc     FAC
        beq     fadd_3
        bcc     faddt_le7ea
        sty     FAC
        ldy     ARGSIGN
        sty     FACSIGN
        eor     #$FF
        adc     #$00
        ldy     #$00
        sty     ARG_EXT
        ldx     #FAC
        bne     faddt_le7ee
faddt_le7ea: ldy     #$00
        sty     FAC_EXT
faddt_le7ee: cmp     #$F9
        bmi     fadd_1
        tay
        lda     FAC_EXT
        lsr     $01,x
        jsr     shift_right_4
        bra     fadd_3
faddt_rts:   rts

; FADD.3 ($E7FA).  The ROM picks the minuend with Y ("lda $04,y" is
; absolute,Y on zero page - there is no zp,Y LDA) and the subtrahend with X;
; X is whichever operand was shifted, Y the other.  Absolute,Y would ignore
; D, so the two cases are written out.  CPX comes before SEC, as in the ROM.
fadd_3:
        bit     SGNCPR
        bpl     fadd_4
        cpx     #ARG
        beq     @fac_minus_arg
        sec                     ; ARG - FAC (FAC was shifted)
        eor     #$FF
        adc     ARG_EXT
        sta     FAC_EXT
        lda     ARG+4
        sbc     FAC+4
        sta     FAC+4
        lda     ARG+3
        sbc     FAC+3
        sta     FAC+3
        lda     ARG+2
        sbc     FAC+2
        sta     FAC+2
        lda     ARG+1
        sbc     FAC+1
        sta     FAC+1
        bra     normalize_fac_1
@fac_minus_arg:
        sec                     ; FAC - ARG (ARG was shifted, or neither)
        eor     #$FF
        adc     ARG_EXT
        sta     FAC_EXT
        lda     FAC+4
        sbc     ARG+4
        sta     FAC+4
        lda     FAC+3
        sbc     ARG+3
        sta     FAC+3
        lda     FAC+2
        sbc     ARG+2
        sta     FAC+2
        lda     FAC+1
        sbc     ARG+1
        sta     FAC+1
        ; fall into NORMALIZE.FAC.1

; NORMALIZE.FAC.1 ($E829)
normalize_fac_1:
        bcs     normalize_fac_2
        jsr     complement_fac

; NORMALIZE.FAC.2 ($E82E)
normalize_fac_2:
        ldy     #$00
        tya
        clc
@le832: ldx     FAC+1
        bne     normalize_fac_4
        ldx     FAC+2
        stx     FAC+1
        ldx     FAC+3
        stx     FAC+2
        ldx     FAC+4
        stx     FAC+3
        ldx     FAC_EXT
        stx     FAC+4
        sty     FAC_EXT
        adc     #$08
        cmp     #$20
        bne     @le832

; ZERO.FAC ($E84E)
zero_fac:
        lda     #$00
sta_in_fac_sign_and_exp:
        sta     FAC
sta_in_fac_sign:
        sta     FACSIGN
        rts

; FADD.4 ($E855)
fadd_4:
        adc     ARG_EXT
        sta     FAC_EXT
        lda     FAC+4
        adc     ARG+4
        sta     FAC+4
        lda     FAC+3
        adc     ARG+3
        sta     FAC+3
        lda     FAC+2
        adc     ARG+2
        sta     FAC+2
        lda     FAC+1
        adc     ARG+1
        sta     FAC+1
        jmp     normalize_fac_5

; NORMALIZE.FAC.3 ($E874)
normalize_fac_3:
        adc     #$01
        asl     FAC_EXT
        rol     FAC+4
        rol     FAC+3
        rol     FAC+2
        rol     FAC+1
; NORMALIZE.FAC.4 ($E880)
normalize_fac_4:
        bpl     normalize_fac_3
        sec
        sbc     FAC
        bcs     zero_fac
        eor     #$FF
        adc     #$01
        sta     FAC
; NORMALIZE.FAC.5 ($E88D)
normalize_fac_5:
        bcc     rts_11
; NORMALIZE.FAC.6 ($E88F)
normalize_fac_6:
        inc     FAC
        beq     overflow
        ror     FAC+1
        ror     FAC+2
        ror     FAC+3
        ror     FAC+4
        ror     FAC_EXT
rts_11: rts

; COMPLEMENT.FAC ($E89E)
complement_fac:
        lda     FACSIGN
        eor     #$FF
        sta     FACSIGN
complement_fac_mantissa:
        lda     FAC+1
        eor     #$FF
        sta     FAC+1
        lda     FAC+2
        eor     #$FF
        sta     FAC+2
        lda     FAC+3
        eor     #$FF
        sta     FAC+3
        lda     FAC+4
        eor     #$FF
        sta     FAC+4
        lda     FAC_EXT
        eor     #$FF
        sta     FAC_EXT
        inc     FAC_EXT
        bne     rts_12
; INCREMENT.FAC.MANTISSA ($E8C6)
increment_fac_mantissa:
        inc     FAC+4
        bne     rts_12
        inc     FAC+3
        bne     rts_12
        inc     FAC+2
        bne     rts_12
        inc     FAC+1
rts_12: rts

; OVERFLOW ($E8D5)
overflow:
        lda     #ERR_OVERFLOW
        jmp     fp_error

; SHIFT.RIGHT ($E8F0) and friends.  X points at the operand's exponent byte
; (FAC or ARG, or RESULT-1 for FMULT).  The ROM's "op $nn,x" is direct-page
; indexed, which on the 65816 is D+nn+X: the same code works on any direct
; page.
shift_right_1:
        ldx     #$61
shift_right_2:
        ldy     $04,x
        sty     FAC_EXT
        ldy     $03,x
        sty     $04,x
        ldy     $02,x
        sty     $03,x
        ldy     $01,x
        sty     $02,x
        ldy     SHIFTEXT
        sty     $01,x
shift_right:
        adc     #$08
        bmi     shift_right_2
        beq     shift_right_2
        sbc     #$08
        tay
        lda     FAC_EXT
        bcs     shift_right_5
shift_right_l:
        asl     $01,x
        bcc     shift_right_le903
        inc     $01,x
shift_right_le903:
        ror     $01,x
        ror     $01,x
shift_right_4:
        ror     $02,x
        ror     $03,x
        ror     $04,x
        ror     a
        iny
        bne     shift_right_l
shift_right_5:
        clc
        rts

; =========================================================================
; Multiply, divide, compare
; =========================================================================

; FMULTT ($E982): FAC = ARG * FAC; A = FAC exponent with flags on entry
fmultt:
        bne     @le987
        rts
@le987: jsr     add_exponents
        lda     #$00
        sta     RESULT
        sta     RESULT+1
        sta     RESULT+2
        sta     RESULT+3
        lda     FAC_EXT
        jsr     multiply_1
        lda     FAC+4
        jsr     multiply_1
        lda     FAC+3
        jsr     multiply_1
        lda     FAC+2
        jsr     multiply_1
        lda     FAC+1
        jsr     multiply_2
        jmp     copy_result_into_fac

; MULTIPLY.1 ($E9B0)
multiply_1:
        bne     multiply_2
        jmp     shift_right_1

; MULTIPLY.2 ($E9B5)
multiply_2:
        lsr     a
        ora     #$80
@le9b8: tay
        bcc     @le9d4
        clc
        lda     RESULT+3
        adc     ARG+4
        sta     RESULT+3
        lda     RESULT+2
        adc     ARG+3
        sta     RESULT+2
        lda     RESULT+1
        adc     ARG+2
        sta     RESULT+1
        lda     RESULT
        adc     ARG+1
        sta     RESULT
@le9d4: ror     RESULT
        ror     RESULT+1
        ror     RESULT+2
        ror     RESULT+3
        ror     FAC_EXT
        tya
        lsr     a
        bne     @le9b8
        rts

; ADD.EXPONENTS ($EA0E).  On underflow it drops its caller's frame (PLA PLA)
; and zeroes FAC, returning to the caller's caller - kept as in the ROM.
add_exponents:
        lda     ARG
add_exponents_1:
        beq     zero
        clc
        adc     FAC
        bcc     @lea1b
        bmi     jov
        clc
        bra     @lea1d          ; ROM: .byte $2C (BIT abs) skips the BPL;
                                ; that stray read can hit $C010 - not here
@lea1b: bpl     zero
@lea1d: adc     #$80
        sta     FAC
        bne     @lea26
        jmp     sta_in_fac_sign
@lea26: lda     SGNCPR
        sta     FACSIGN
        rts

; OUTOFRNG ($EA2B) (used by EXP)
outofrng:
        lda     FACSIGN
        eor     #$FF
        bmi     jov
; ZERO ($EA31)
zero:   pla
        pla
        jmp     zero_fac

jov:    jmp     overflow

; MUL10 ($EA39)
mul10:
        jsr     copy_fac_to_arg_rounded
        tax
        beq     @lea4f
        clc
        adc     #$02
        bcs     jov
        ldx     #$00
        stx     SGNCPR
        jsr     fadd_2
        inc     FAC
        beq     jov
@lea4f: rts

con_ten:
        .byte   $84, $20, $00, $00, $00

; DIV10 ($EA55)
div10:
        jsr     copy_fac_to_arg_rounded
        lda     #<con_ten
        sta     FP_PTR
        lda     #>con_ten
        sta     FP_PTR+1
        lda     #^con_ten
        sta     FP_PTR+2
        ldx     #$00
; DIV ($EA5E)
div:    stx     SGNCPR
        jsr     load_fac_from_ptr
        jmp     fdivt

; FDIVT ($EA69): FAC = ARG / FAC; A = FAC exponent with flags on entry
fdivt:
        beq     @leae1
        jsr     round_fac
        lda     #$00
        sec
        sbc     FAC
        sta     FAC
        jsr     add_exponents
        inc     FAC
        beq     jov
        ldx     #$FC
        lda     #$01
@lea80: ldy     ARG+1
        cpy     FAC+1
        bne     @lea96
        ldy     ARG+2
        cpy     FAC+2
        bne     @lea96
        ldy     ARG+3
        cpy     FAC+3
        bne     @lea96
        ldy     ARG+4
        cpy     FAC+4
@lea96: php
        rol     a
        bcc     @leaa3
        inx
        ; ROM: sta $65,x with X = $FD..$01 - on the 6502 that wraps in zero
        ; page ($62..$66); native-mode dp,X does not wrap, so store through
        ; a long pointer to this direct page at the wrapped offset.
        php
        pha
        txa
        clc
        adc     #$65
        tay
        pla
        sta     [FP_DPP],y
        plp
        beq     @lead1
        bpl     @lead5
        lda     #$01
@leaa3: plp
        bcs     @leab4
@leaa6: asl     ARG+4
        rol     ARG+3
        rol     ARG+2
        rol     ARG+1
        bcs     @lea96
        bmi     @lea80
        bpl     @lea96
@leab4: tay
        lda     ARG+4
        sbc     FAC+4
        sta     ARG+4
        lda     ARG+3
        sbc     FAC+3
        sta     ARG+3
        lda     ARG+2
        sbc     FAC+2
        sta     ARG+2
        lda     ARG+1
        sbc     FAC+1
        sta     ARG+1
        tya
        jmp     @leaa6
@lead1: lda     #$40
        bne     @leaa3
@lead5: asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        sta     FAC_EXT
        plp
        jmp     copy_result_into_fac
@leae1: lda     #ERR_DIVZERO
        jmp     fp_error

; COPY.RESULT.INTO.FAC ($EAE6)
copy_result_into_fac:
        lda     RESULT
        sta     FAC+1
        lda     RESULT+1
        sta     FAC+2
        lda     RESULT+2
        sta     FAC+3
        lda     RESULT+3
        sta     FAC+4
        jmp     normalize_fac_2

; COPY.FAC.TO.ARG.ROUNDED ($EB63)
copy_fac_to_arg_rounded:
        jsr     round_fac
maf:    ldx     #$06
@leb68: lda     FAC-1,x
        sta     SHIFTEXT,x
        dex
        bne     @leb68
        stx     FAC_EXT
        rts

; SIGN ($EB82): A = 0, 1 or $FF
sign:   lda     FAC
        beq     sign_rts
sign1:  lda     FACSIGN
sign2:  rol     a
        lda     #$FF
        bcs     sign_rts
        lda     #$01
sign_rts:   rts

; SGN ($EB90), FLOAT ($EB93)
sgn:    jsr     sign
float:  sta     FAC+1
        lda     #$00
        sta     FAC+2
        ldx     #$88
float_1:
        lda     FAC+1
        eor     #$FF
        rol     a
float_2:
        lda     #$00
        sta     FAC+4
        sta     FAC+3
        stx     FAC
        sta     FAC_EXT
        sta     FACSIGN
        jmp     normalize_fac_1

; FCOMP ($EBB2): compare [FP_PTR] with FAC -> A = 0 (equal), 1, $FF
fcomp:
        ldy     #$00
        lda     [FP_PTR],y
        iny
        tax
        beq     sign
        lda     [FP_PTR],y
        eor     FACSIGN
        bmi     sign1
        cpx     FAC
        bne     @lebe9
        lda     [FP_PTR],y
        ora     #$80
        cmp     FAC+1
        bne     @lebe9
        iny
        lda     [FP_PTR],y
        cmp     FAC+2
        bne     @lebe9
        iny
        lda     [FP_PTR],y
        cmp     FAC+3
        bne     @lebe9
        iny
        lda     #$7F
        cmp     FAC_EXT
        lda     [FP_PTR],y
        sbc     FAC+4
        beq     @rts
@lebe9: lda     FACSIGN
        bcc     @lebef
        eor     #$FF
@lebef: jmp     sign2
@rts:   rts

; =========================================================================
; Float <-> integer
; =========================================================================

; QINT ($EBF2): FAC mantissa = INT(FAC) as a 32-bit two's-complement integer
qint:
        lda     FAC
        beq     qint_3
        sec
        sbc     #$A0
        bit     FACSIGN
        bpl     @lec06
        tax
        lda     #$FF
        sta     SHIFTEXT
        jsr     complement_fac_mantissa
        txa
@lec06: ldx     #FAC
        cmp     #$F9
        bpl     qint_2
        jsr     shift_right
        sty     SHIFTEXT
        rts
; QINT.2 ($EC12)
qint_2: tay
        lda     FACSIGN
        and     #$80
        lsr     FAC+1
        ora     FAC+1
        sta     FAC+1
        jsr     shift_right_4
        sty     SHIFTEXT
        rts

; INT ($EC23)
int:    lda     FAC
        cmp     #$A0
        bcs     int_rts
        jsr     qint
        sty     FAC_EXT
        lda     FACSIGN
        sty     FACSIGN
        eor     #$80
        rol     a
        lda     #$A0
        sta     FAC
        lda     FAC+4
        sta     CHARAC
        jmp     normalize_fac_1
; QINT.3 ($EC40)
qint_3: sta     FAC+1
        sta     FAC+2
        sta     FAC+3
        sta     FAC+4
        tay
int_rts:
        rts

; -32768 as the ROM has it at $E0FE: its last byte is the $20 of the next
; routine's JSR, so it is a hair below -32768 - kept, it decides AYINT's range
neg32768:
        .byte   $90, $80, $00, $00, $20

; MKINT ($E108), AYINT ($E10C): FAC+3/FAC+4 = signed 16-bit (high, low)
mkint:  lda     FACSIGN
        bmi     mi1
ayint:  lda     FAC
        cmp     #$90
        bcc     mi2
        lda     #<neg32768
        sta     FP_PTR
        lda     #>neg32768
        sta     FP_PTR+1
        lda     #^neg32768
        sta     FP_PTR+2
        jsr     fcomp
mi1:    bne     iqerr
mi2:    jmp     qint

iqerr:  lda     #ERR_ILLQTY
        jmp     fp_error

; GIVAYF ($E2F2): FAC = the signed 16-bit integer in A (high), Y (low)
givayf: ldx     #$00
        sta     FAC+1
        sty     FAC+2
        ldx     #$90
        jmp     float_1         ; (the ROM also clears VALTYP here)

; CONINT ($E6FB): X = FAC as a byte, or ?ILLEGAL QUANTITY
conint: jsr     mkint
        ldx     FAC+3
        bne     iqerr
        ldx     FAC+4
        rts

; GETADR ($E752): LINNUM = FAC as a 16-bit address (-65535..65535)
getadr: lda     FAC
        cmp     #$91
        bcs     iqerr
        jsr     qint
        lda     FAC+3
        ldy     FAC+4
        sty     LINNUM
        sta     LINNUM+1
        rts

; =========================================================================
; Text -> number
; =========================================================================

; CHRGET / CHRGOT ($00B1 / $00B7, copied from GENERIC.CHRGET $F10B): the
; same instructions over a long text pointer, so A and the flags are the
; ROM's: spaces skipped, C clear only for '0'-'9', Z set for 0 and ':'.
; The pointer carries into its middle byte, not the bank, as TXTPTR does.
chrget: inc     FP_TXT
        bne     chrgot
        inc     FP_TXT+1
chrgot: lda     [FP_TXT]
        cmp     #$3A
        bcs     @rts
        cmp     #$20
        beq     chrget
        sec
        sbc     #$30
        sec
        sbc     #$D0
@rts:   rts

; FIN ($EC4A): A = the first character, C clear if a digit (from CHRGET)
fin:    ldy     #$00
        ldx     #$0A
@lec4e: sty     INDX,x
        dex
        bpl     @lec4e
        bcc     fin_2
        cmp     #$2D            ; '-'
        bne     @lec5d
        stx     SERLEN
        beq     fin_1
@lec5d: cmp     #$2B            ; '+'
        bne     fin_3
fin_1:  jsr     chrget
fin_2:  bcc     fin_9
fin_3:  cmp     #$2E            ; '.'
        beq     fin_10
        cmp     #$45            ; 'E'
        bne     fin_7
        jsr     chrget
        bcc     fin_5
        cmp     #$C9            ; token '-'
        beq     @lec85
        cmp     #$2D            ; '-'
        beq     @lec85
        cmp     #$C8            ; token '+'
        beq     fin_4
        cmp     #$2B            ; '+'
        beq     fin_4
        bne     fin_6
@lec85: ror     EXPSGN
fin_4:  jsr     chrget
fin_5:  bcc     getexp
fin_6:  bit     EXPSGN
        bpl     fin_7
        lda     #$00
        sec
        sbc     EXPON
        jmp     fin_8

fin_10: ror     DPFLG
        bit     DPFLG
        bvc     fin_1
fin_7:  lda     EXPON
fin_8:  sec
        sbc     INDX
        sta     EXPON
        beq     @lecb9
        bpl     @lecb2
@leca9: jsr     div10
        inc     EXPON
        bne     @leca9
        beq     @lecb9
@lecb2: jsr     mul10
        dec     EXPON
        bne     @lecb2
@lecb9: lda     SERLEN
        bmi     @lecbe
        rts
@lecbe: jmp     negop

fin_9:  pha
        bit     DPFLG
        bpl     @lecc8
        inc     INDX
@lecc8: jsr     mul10
        pla
        sec
        sbc     #$30
        jsr     addacc
        jmp     fin_1

; ADDACC ($ECD5): FAC = FAC + A (a digit)
addacc: pha
        jsr     copy_fac_to_arg_rounded
        pla
        jsr     float
        lda     ARGSIGN
        eor     FACSIGN
        sta     SGNCPR
        ldx     FAC
        jmp     faddt

; GETEXP ($ECE8)
getexp: lda     EXPON
        cmp     #$0A
        bcc     @lecf7
        lda     #$64
        bit     EXPSGN
        bmi     @led05
        jmp     overflow
@lecf7: asl     a
        asl     a
        clc
        adc     EXPON
        asl     a
        clc
        ldy     #$00
        adc     [FP_TXT],y
        sec
        sbc     #$30
@led05: sta     EXPON
        jmp     fin_4

; NEGOP ($EED0)
negop:  lda     FAC
        beq     @rts
        lda     FACSIGN
        eor     #$FF
        sta     FACSIGN
@rts:   rts
