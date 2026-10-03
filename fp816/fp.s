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
; 24-bit address in FP_PTR.  FP_ERR is 0, or the AppleSoft error code
; ($45 = ?OVERFLOW) when the operation failed (FAC is then undefined).
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
FP_SP    = $F0          ; 2 bytes: S at entry, for error unwinding (ours)
FP_ERR   = $F2          ; error code, 0 = none (ours)

ERR_OVERFLOW = $45

; ---- entry/exit ----
.macro  ENTER
        php
        rep     #$30
        .a16
        .i16
        tsc
        sta     FP_SP
        sep     #$30
        .a8
        .i8
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
        bne     @le7c6
        jmp     copy_arg_to_fac
@le7c6: ldx     FAC_EXT
        stx     ARG_EXT
        ldx     #ARG
        lda     ARG
        tay                     ; FADD.2
        beq     @rts
        sec
        sbc     FAC
        beq     fadd_3
        bcc     @le7ea
        sty     FAC
        ldy     ARGSIGN
        sty     FACSIGN
        eor     #$FF
        adc     #$00
        ldy     #$00
        sty     ARG_EXT
        ldx     #FAC
        bne     @le7ee
@le7ea: ldy     #$00
        sty     FAC_EXT
@le7ee: cmp     #$F9
        bmi     fadd_1
        tay
        lda     FAC_EXT
        lsr     $01,x
        jsr     shift_right_4
        bra     fadd_3
@rts:   rts

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
