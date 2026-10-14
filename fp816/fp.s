; fp816: AppleSoft's floating point for the 65816, bit-exact with the ROM.
;
; Stage 1: a line-for-line port of the ROM routines (IIgs ROM 3 bank $FF ==
; enhanced IIe, $E7A0-$F10A), in native mode, on any direct page, with long
; operand pointers.  Every routine is checked against the ROM's own by
; fptest; the 16-bit rewrites come after, against the same oracle.
;
; Placement: position-independent across banks - load the image at offset
; $0000 of any bank (the OS allocates one); the code finds its own bank with
; PHK, nothing depends on the bank it was linked for.  No ROM, no card: it
; runs on any IIgs or IIgs emulator.
;
; Calling: native mode (E=0), any M/X (preserved), D = the FP direct page,
; DBR anything (preserved; set to this bank inside).  JSL an entry in the jump table at the start of the bank;
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
FP_BUF   = $FD          ; 3 bytes: FOUT's output buffer MINUS ONE (ours; the ROM's $00FF)
VARPNT   = $83          ; FOUT scratch
SERPNT   = $AD          ; FOUT scratch
FP_SER   = $AD          ; 3 bytes: polynomial coefficient pointer (the ROM's SERPNT, made long)
CPRMASK  = $16          ; SIN/TAN quadrant flag
TEMP3    = $8A          ; 5-byte float temporaries, as in AppleSoft's zero page
TEMP1    = $93
TEMP2    = $98
RNDSEED  = $C9          ; 5 bytes: RND's seed (AppleSoft: 80 4F C7 52 58 at cold start)
LINNUM   = $50          ; GETADR's result, 2 bytes
FP_INT   = $F8          ; 2 bytes: GIVAYF's input, low first (ours)
; stage 2's scratch ($67-$81: free in AppleSoft's offsets and the VM's)
DW_A = $67                      ; 4: ARG's mantissa, little-endian (FDIVT)
DW_F = $6B                      ; 4: FAC's
DW_Q = $6F                      ; 2: quotient bits coming in, behind a sentinel 1
DW_R = $71                      ; 6: the quotient words: bits 1-16, 17-32, then 33-34

ERR_OVERFLOW = $45
ERR_DIVZERO  = $85
ERR_ILLQTY   = $35

; ---- entry/exit ----
.macro  ENTER
        php
        phb                     ; constants and tables are read with DBR = our bank
        phk
        plb
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
        plb
        plp
        rtl
.endmacro

; FP_PTR = a constant in this bank
; FP_PTR = this direct page + off (a float temporary; bank 0)
.macro  DPTR    off
        rep     #$21            ; 16-bit A, carry clear
        .a16
        lda     FP_DPP
        adc     #off
        sta     FP_PTR
        sep     #$20
        .a8
        stz     FP_PTR+2
.endmacro

; FP_SER = a coefficient table in this bank
.macro  SPTR    addr
        lda     #<(addr)
        sta     FP_SER
        lda     #>(addr)
        sta     FP_SER+1
        phk                     ; the bank we are running in
        pla
        sta     FP_SER+2
.endmacro

.macro  PTR     addr
        lda     #<(addr)
        sta     FP_PTR
        lda     #>(addr)
        sta     FP_PTR+1
        phk                     ; the bank we are running in
        pla
        sta     FP_PTR+2
.endmacro

        .segment "CODE"

; ---- jump table: the public entries, 4 bytes apart ----
fp_table:
        .export fp_table
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
        jmp     fp_fout            ; +$58  [FP_BUF+1..] = FAC as text, 0-terminated (ROM FOUT)
        nop
        jmp     fp_sqr             ; +$5C  FAC = SQR(FAC)
        nop
        jmp     fp_pwr             ; +$60  FAC = [FP_PTR] ^ FAC    (ROM FPWRT, base in ARG)
        nop
        jmp     fp_log             ; +$64  FAC = LOG(FAC)
        nop
        jmp     fp_exp             ; +$68  FAC = EXP(FAC)
        nop
        jmp     fp_rnd             ; +$6C  FAC = RND(FAC), seed at RNDSEED
        nop
        jmp     fp_sin             ; +$70  FAC = SIN(FAC)
        nop
        jmp     fp_cos             ; +$74  FAC = COS(FAC)
        nop
        jmp     fp_tan             ; +$78  FAC = TAN(FAC)
        nop
        jmp     fp_atn             ; +$7C  FAC = ATN(FAC)
        nop
        jmp     fp_round           ; +$80  ROUND.FAC (FRMEVL rounds before every push)
        nop
        jmp     fp_multt           ; +$84  FAC = ARG * FAC        (ROM FMULTT)
        nop
        jmp     fp_divt            ; +$88  FAC = ARG / FAC        (ROM FDIVT)
        nop
        jmp     fp_pwrt            ; +$8C  FAC = ARG ^ FAC        (ROM FPWRT)
        nop
        jmp     fp_float           ; +$90  FAC = the signed byte FP_INT (ROM FLOAT)
        nop
        jmp     fp_uint16          ; +$94  FAC = the unsigned word FP_INT (LINPRT's FLOAT.2)
        nop
        jmp     fp_next            ; +$98  NEXT: [FP_PTR] += FAC, rounded; FP_RES = FCOMP([FP_SER])
        nop
        jmp     fp_ayint16         ; +$9C  FAC+3,4 = AYINT (the rest of FAC undefined)
        nop

        .export fp_load_fac, fp_load_arg, fp_store_fac, fp_add, fp_sub, fp_addt, fp_subt
        .export fp_mul, fp_div, fp_cmp, fp_mul10, fp_div10, fp_sgn, fp_abs, fp_int, fp_qint
        .export fp_ayint, fp_givayf, fp_conint, fp_getadr, fp_fin, fp_negop, fp_fout
        .export fp_sqr, fp_pwr, fp_log, fp_exp, fp_rnd, fp_sin, fp_cos, fp_tan, fp_atn
        .export fp_round, fp_multt, fp_divt, fp_pwrt, fp_float, fp_uint16, fp_next, fp_ayint16

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

; AYINT's result alone (subscripts, integer variables): FAC+3 (high), FAC+4
; = the integer, ?ILLEGAL QUANTITY as AYINT; the rest of FAC undefined.  A
; positive number below 32768 is its top 16 mantissa bits shifted (what
; QINT comes to); anything else is AYINT's own way.
fp_ayint16:
        ENTER
        lda     FACSIGN
        bmi     @slow
        lda     FAC
        cmp     #$90
        bcs     @slow
        cmp     #$81
        bcc     @zero
        rep     #$30
        .a16
        .i16
        and     #$00FF
        tax                     ; the exponent
        lda     FAC+1
        xba                     ; the top 16 mantissa bits
        cpx     #$0089
        bcs     @bits
        xba                     ; 8 or more to go: a byte at once
        and     #$00FF
@l1:    cpx     #$0088
        beq     @put
        lsr     a
        inx
        bra     @l1
@bits:  cpx     #$0090
        beq     @put
        lsr     a
        inx
        bra     @bits
@put:   xba
        sta     FAC+3           ; high, low
        sep     #$30
        .a8
        .i8
        LEAVE
@zero:  stz     FAC+3
        stz     FAC+4
        LEAVE
@slow:  jsr     ayint
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

fp_fout:
        ENTER
        jsr     fout
        LEAVE

fp_sqr: ENTER
        jsr     sqr
        LEAVE

fp_pwr: ENTER
        jsr     load_arg_from_ptr  ; A = FAC exponent, flags from it
        jsr     fpwrt
        LEAVE

fp_log: ENTER
        jsr     log
        LEAVE

fp_exp: ENTER
        jsr     exp
        LEAVE

fp_rnd: ENTER
        jsr     rnd
        LEAVE

fp_sin: ENTER
        jsr     sin
        LEAVE

fp_cos: ENTER
        jsr     cos
        LEAVE

fp_tan: ENTER
        jsr     tan
        LEAVE

fp_atn: ENTER
        jsr     atn
        LEAVE

fp_round:
        ENTER
        jsr     round_fac
        LEAVE

fp_multt:
        ENTER
        lda     FAC
        jsr     fmultt
        LEAVE

fp_divt:
        ENTER
        lda     FAC
        jsr     fdivt
        LEAVE

fp_pwrt:
        ENTER
        lda     FAC
        jsr     fpwrt
        LEAVE

fp_float:
        ENTER
        lda     FP_INT
        jsr     float
        LEAVE

; LINPRT ($ED24): FLOAT.2 with $9E/$9F = the word (high, low), X = $90, C = 1
fp_uint16:
        ENTER
        lda     FP_INT+1
        sta     FAC+1
        lda     FP_INT
        sta     FAC+2
        ldx     #$90
        sec
        jsr     float_2
        LEAVE

; NEXT ($DCF9) after the step is in FAC: FAC (the step, FACSIGN its sign
; byte) += the variable at [FP_PTR] (FADD), stored back there rounded
; (STORE.FAC.AT.YX.ROUNDED), then FP_RES = FCOMP of the result with the limit
; at [FP_SER].  FAC is undefined afterwards.  When the variable, the step and
; the limit are whole numbers below 32768 and the sum is not zero, next_int
; does it with integers: the same stored bytes, the same comparison.
fp_next:
        ENTER
        jsr     next_int
        bcc     @done
        jsr     load_arg_from_ptr
        jsr     faddt
        jsr     store_fac_at_ptr_rounded
        lda     FP_SER
        sta     FP_PTR
        lda     FP_SER+1
        sta     FP_PTR+1
        lda     FP_SER+2
        sta     FP_PTR+2
        jsr     fcomp
        sta     FP_RES
@done:  LEAVE

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
; NEXT's integer path (fp_next): not a port - checked against the ROM's FADD,
; STORE.FAC.AT.YX.ROUNDED and FCOMP by fptest's NEXT family.
; =========================================================================

NX_V = RESULT                   ; 2: the variable, then the sum (signed)
NX_S = RESULT+2                 ; 2: the step (signed)
NX_L = TEMP3                    ; 2: the limit (signed)

; A = the packed number at [ptr] as a signed integer, C clear; C set when
; it is not a whole number below 32768.  16-bit A, X, Y.
.macro  PACKED_WHOLE ptr
        ldy     #$0003
        lda     [ptr],y         ; the low mantissa bytes: none in a whole
        bne     :+              ; number below 32768
        lda     [ptr]
        and     #$00FF
        tax                     ; the exponent
        ldy     #$0001
        lda     [ptr],y
        xba                     ; sign + mantissa 1 : mantissa 2
        tay                     ; (the sign: bit 15)
        ora     #$8000
        jsr     whole
        bcs     :++
        cpy     #$8000
        bcc     :++             ; (C clear)
        eor     #$FFFF
        inc     a
        clc
        bra     :++
:       sec
:
.endmacro

; the VM's integer NEXT (vm.s) uses these three as fp_next does; all three
; run with 16-bit A, X, Y, DBR anything, D = the FP direct page
        .export fp_packed_int, fp_int_packed, fp_whole
        .a16
        .i16
fp_packed_int:                  ; A = [FP_PTR] as above
        PACKED_WHOLE FP_PTR
        rts
fp_whole = whole

; [FP_PTR] = A (signed, not zero), packed as STORE.FAC.AT.YX.ROUNDED
; packs it: the sum of two whole numbers is exact.
fp_int_packed:
        sta     NX_V
        bpl     @pos
        eor     #$FFFF
        inc     a
@pos:   ldy     #$0090          ; normalize: exponent in Y, mantissa top in A
        cmp     #$0100
        bcs     @norm
        xba
        ldy     #$0088
@norm:  cmp     #$8000
        bcs     @put
        asl     a
        dey
        bra     @norm
@put:   xba                     ; mantissa 1 : mantissa 2, as stored
        and     #$FF7F
        bit     NX_V
        bpl     @plus
        ora     #$0080          ; the sign, in mantissa 1's top bit
@plus:  phy
        ldy     #$0001
        sta     [FP_PTR],y
        lda     #$0000
        ldy     #$0003
        sta     [FP_PTR],y
        pla
        sep     #$20
        .a8
        sta     [FP_PTR]        ; the exponent
        rep     #$20
        .a16
        rts

; C clear: done (stored, FP_RES set); C set: not for this path, nothing
; written.  Entered with M = X = 1, left that way.
        .a8
        .i8
next_int:
        lda     FAC_EXT         ; the step: FAC with no rounding byte and no
        ora     FAC+3           ; low mantissa bytes
        ora     FAC+4
        beq     @step
        sec
        rts
@step:  rep     #$30
        .a16
        .i16
        lda     FAC
        and     #$00FF
        tax
        lda     FAC+1
        xba                     ; mantissa 1 : mantissa 2
        jsr     whole
        bcs     @no1
        bit     FACSIGN-1       ; N = FACSIGN's bit 7
        bpl     @s
        eor     #$FFFF
        inc     a
@s:     sta     NX_S
        PACKED_WHOLE FP_SER
        bcs     @no1
        sta     NX_L
        PACKED_WHOLE FP_PTR
        bcc     @sum
@no1:   jmp     @no
@sum:
        clc
        adc     NX_S
        bvs     @no             ; beyond 16 bits
        beq     @no             ; zero: the ROM's own way
        pha
        sec                     ; FCOMP: 0 equal, 1 above, $FF below
        sbc     NX_L
        beq     @eq
        bvc     @nv
        eor     #$8000
@nv:    bmi     @lt
        ldx     #$0001
        bra     @cmp
@lt:    ldx     #$00FF
        bra     @cmp
@eq:    ldx     #$0000
@cmp:   stx     FP_RES          ; (and FP_RES+1, a scratch byte)
        pla
        jsr     fp_int_packed
        sep     #$30
        .a8
        .i8
        clc
        rts
        .a16
        .i16
@no:    sep     #$30
        .a8
        .i8
        sec
        rts

; whole: A = the magnitude of the number with exponent X and mantissa top
; A (bit 15 set, the low 16 bits zero), C clear; C set when it is not a
; whole number below 32768.  Y kept.  16-bit A and X.
        .a16
        .i16
whole:  cpx     #$0081
        bcc     @small
        cpx     #$0090
        bcs     @no
        cpx     #$0089
        bcs     @bits
        bit     #$00FF          ; 8 or more fraction bits: a byte at once
        bne     @no
        xba
        and     #$00FF
        cpx     #$0088
        beq     @ok
@l1:    lsr     a
        bcs     @no
        inx
        cpx     #$0088
        bne     @l1
        bra     @ok
@bits:  lsr     a
        bcs     @no
        inx
        cpx     #$0090
        bne     @bits
@ok:    clc
        rts
@small: cpx     #$0000
        bne     @no
        lda     #$0000          ; zero, whatever its mantissa
        clc
        rts
@no:    sec
        rts
        .a8
        .i8

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

; ROUND.FAC ($EB72), INCREMENT.MANTISSA ($EB7A)
round_fac:
        lda     FAC
        beq     round_rts
        asl     FAC_EXT
        bcc     round_rts
increment_mantissa:
        jsr     increment_fac_mantissa
        bne     round_rts
        jmp     normalize_fac_6
round_rts:
        rts

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

; FMULTT ($E982): FAC = ARG * FAC; A = FAC exponent with flags on entry.
;
; Stage 2.  The ROM multiplies by FAC's five mantissa bytes, the rounding
; byte first, each through MULTIPLY.1/.2: for a byte b it shifts the 40-bit
; RESULT:FAC.EXTENSION right eight times, adding ARG's mantissa to RESULT
; for each set bit - which comes to acc = (acc >> 8) + b * ARG exactly (the
; bits shifted out never carry back).  A zero byte goes to SHIFT.RIGHT.1
; instead, whose carry in is MULTIPLY.2's last (1) or SHIFT.RIGHT's own (0):
; after a nonzero byte it is acc >> 8, but a zero byte after a zero byte
; shifts RESULT one bit more while FAC.EXTENSION keeps the byte shifted
; into it (Applesoft's multiply bug).  The last byte (FAC+1) is always
; MULTIPLY.2's.  Here: the four 8 x 8 products of b by ARG's bytes from
; quarter squares (ab = sq(a+b) - sq(|a-b|), sq(n) = n*n/4), added in
; 16-bit halves.  fptest holds it to the ROM's own FMULT and every
; function that multiplies.
MW_ACC = $67                    ; 7: the product so far, little-endian (5, 6 stay 0)
MW_P   = $6E                    ; 8: b times ARG+4, ARG+3, ARG+2, ARG+1 (16 bits each)
MW_PB1 = $76                    ; 2: sq + 2b
MW_PB2 = $78                    ; 2: sqs + 2(255 - b)
MW_X   = $7A                    ; 8: ARG+4, ARG+3, ARG+2, ARG+1, doubled (indices)

fmultt:
        bne     @le987
        rts
@le987: jsr     add_exponents   ; (underflow: back to our caller's caller)
        rep     #$30
        .a16
        .i16
        lda     ARG+4
        and     #$00FF
        asl     a
        sta     MW_X
        lda     ARG+3
        and     #$00FF
        asl     a
        sta     MW_X+2
        lda     ARG+2
        and     #$00FF
        asl     a
        sta     MW_X+4
        lda     ARG+1
        and     #$00FF
        asl     a
        sta     MW_X+6
        stz     MW_ACC
        stz     MW_ACC+2
        stz     MW_ACC+4
        stz     MW_ACC+5
        ldx     #$0000          ; 1 after a nonzero byte (SHIFT.RIGHT.1's carry in)
        lda     FAC_EXT
        jsr     mul_byte
        lda     FAC+4
        jsr     mul_byte
        lda     FAC+3
        jsr     mul_byte
        lda     FAC+2
        jsr     mul_byte
        lda     FAC+1
        and     #$00FF
        jsr     mul_nz          ; (MULTIPLY.2, whatever it is)
        lda     MW_ACC+3        ; RESULT = bytes 4..1, FAC.EXTENSION byte 0
        xba
        sta     RESULT
        lda     MW_ACC+1
        xba
        sta     RESULT+2
        sep     #$30
        .a8
        .i8
        lda     MW_ACC
        sta     FAC_EXT
        jmp     copy_result_into_fac

        .a16
        .i16
; one multiplier byte (A, its high byte junk): MULTIPLY.1
mul_byte:
        and     #$00FF
        bne     mul_nz
        cpx     #$0000
        beq     @bug
        ldx     #$0000          ; after a nonzero byte: acc >> 8
        lda     MW_ACC+1
        sta     MW_ACC
        lda     MW_ACC+3
        sta     MW_ACC+2
        stz     MW_ACC+4
        rts
@bug:   lda     MW_ACC+1        ; after a zero byte: byte 0 = byte 1, bytes
        sta     MW_ACC          ; 1-4 = bytes 1-4 >> 9
        lda     MW_ACC+3
        sta     MW_ACC+2
        stz     MW_ACC+4
        lsr     MW_ACC+3
        ror     MW_ACC+1
        rts

; acc = (acc >> 8) + b * ARG, b = A (MULTIPLY.2)
mul_nz: asl     a
        sta     MW_PB2          ; (2b, for a moment)
        clc
        adc     #.loword(sq)
        sta     MW_PB1
        lda     #.loword(sqs + 510)
        sec
        sbc     MW_PB2
        sta     MW_PB2
        ldy     MW_X            ; the four products
        lda     (MW_PB1),y
        sec
        sbc     (MW_PB2),y
        sta     MW_P
        ldy     MW_X+2
        lda     (MW_PB1),y
        sec
        sbc     (MW_PB2),y
        sta     MW_P+2
        ldy     MW_X+4
        lda     (MW_PB1),y
        sec
        sbc     (MW_PB2),y
        sta     MW_P+4
        ldy     MW_X+6
        lda     (MW_PB1),y
        sec
        sbc     (MW_PB2),y
        sta     MW_P+6
        lda     MW_ACC+1        ; acc >> 8
        sta     MW_ACC
        lda     MW_ACC+3
        sta     MW_ACC+2
        stz     MW_ACC+4
        clc                     ; + the products at bytes 0 and 2
        lda     MW_ACC
        adc     MW_P
        sta     MW_ACC
        lda     MW_ACC+2
        adc     MW_P+4
        sta     MW_ACC+2
        lda     MW_ACC+4
        adc     #$0000
        sta     MW_ACC+4
        clc                     ; + the products at bytes 1 and 3
        lda     MW_ACC+1
        adc     MW_P+2
        sta     MW_ACC+1
        lda     MW_ACC+3
        adc     MW_P+6
        sta     MW_ACC+3
        lda     MW_ACC+5
        adc     #$0000
        sta     MW_ACC+5
        ldx     #$0001
        rts
        .a8
        .i8

; the quarter squares: sq(n) = n*n/4 for n = 0..510, and sqs(n) = sq(|n - 255|)
sq:
        .repeat 511, n
        .word   (n * n) / 4
        .endrepeat
sqs:
        .repeat 511, n
        .word   ((n - 255) * (n - 255)) / 4
        .endrepeat

; MULTIPLY.1 ($E9B0), MULTIPLY.2 ($E9B5): stage 1's, kept for the functions
; that reach them (none now) and as the reference
multiply_1:
        bne     multiply_2
        jmp     shift_right_1

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
        PTR     con_ten
        ldx     #$00
; DIV ($EA5E)
div:    stx     SGNCPR
        jsr     load_fac_from_ptr
        jmp     fdivt

; FDIVT ($EA69): FAC = ARG / FAC; A = FAC exponent with flags on entry
fdivt:
        bne     @nz
        jmp     @leae1
@nz:    jsr     round_fac
        lda     #$00
        sec
        sbc     FAC
        sta     FAC
        jsr     add_exponents
        inc     FAC
        beq     jov
        ; Stage 2: the ROM's restoring division (34 quotient bits: RESULT's
        ; 32, then 2 into FAC.EXTENSION's top; after a bit, if it is 1, ARG
        ; -= FAC; ARG <<= 1; the next bit is the bit shifted out, else
        ; ARG >= FAC compared only when ARG's top bit is set; nothing after
        ; the last bit) in 16-bit halves of little-endian copies
        rep     #$30
        .a16
        .i16
        lda     ARG+3
        xba
        sta     DW_A
        lda     ARG+1
        xba
        sta     DW_A+2
        lda     FAC+3
        xba
        sta     DW_F
        lda     FAC+1
        xba
        sta     DW_F+2
        ldx     #$0000          ; the next quotient word (DW_R+x)
        lda     #$0001          ; behind a sentinel bit
        sta     DW_Q
@cmp:   lda     DW_A+2          ; C = ARG >= FAC
        cmp     DW_F+2
        bne     @c
        lda     DW_A
        cmp     DW_F
@c:     bcc     @zero
@one:   rol     DW_Q            ; a 1
        bcc     @sub
        jsr     div_word
        beq     @done
@sub:   lda     DW_A            ; ARG -= FAC
        sec
        sbc     DW_F
        sta     DW_A
        lda     DW_A+2
        sbc     DW_F+2
        sta     DW_A+2
@shift: asl     DW_A            ; ARG <<= 1
        rol     DW_A+2
        bcs     @one            ; a bit out: 1
        bit     DW_A+2
        bmi     @cmp            ; the top bit set: compare
@zero:  rol     DW_Q            ; else 0
        bcc     @shift
        jsr     div_word
        bne     @shift
@done:  lda     DW_A+2          ; ARG as the ROM leaves it
        xba
        sta     ARG+1
        lda     DW_A
        xba
        sta     ARG+3
        lda     DW_R            ; RESULT: bits 1-32
        xba
        sta     RESULT
        lda     DW_R+2
        xba
        sta     RESULT+2
        sep     #$30
        .a8
        .i8
        lda     DW_R+4          ; bits 33, 34 (the ROM stores them at $66 too,
        sta     $66             ; RESULT's wrapped fifth byte)
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        asl     a
        sta     FAC_EXT
        jmp     copy_result_into_fac
@leae1: lda     #ERR_DIVZERO
        jmp     fp_error


; a quotient word is full (DW_Q's sentinel out): kept, the next begun - the
; third takes only two bits (sentinel $4000); Z set when all 34 are in
        .a16
        .i16
div_word:
        lda     DW_Q
        sta     DW_R,x
        inx
        inx
        cpx     #$0006
        beq     @r
        lda     #$0001
        cpx     #$0004
        bne     @s
        lda     #$4000
@s:     sta     DW_Q            ; (Z clear)
@r:     rts
        .a8
        .i8

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
        PTR     neg32768
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

; =========================================================================
; Number -> text
; =========================================================================

; FMULT ($E97F) / FADD ($E7BE) on [FP_PTR], for internal use
fmult:  jsr     load_arg_from_ptr
        jmp     fmultt
fadd:   jsr     load_arg_from_ptr
        jmp     faddt

; FADDH ($E7A0): FAC = FAC + 0.5
faddh:  PTR     con_half
        jmp     fadd

con_99999999_9: .byte $9B, $3E, $BC, $1F, $FD
con_999999999:  .byte $9E, $6E, $6B, $27, $FD
con_billion:    .byte $9E, $6E, $6B, $28, $00
con_half:       .byte $80, $00, $00, $00, $00
; DECTBL ($EE69): -1e8, +1e7, -1e6, ... +10, -1 as 32-bit two's complement
dectbl: .byte   $FA, $0A, $1F, $00
        .byte   $00, $98, $96, $80
        .byte   $FF, $F0, $BD, $C0
        .byte   $00, $01, $86, $A0
        .byte   $FF, $FF, $D8, $F0
        .byte   $00, $00, $03, $E8
        .byte   $FF, $FF, $FF, $9C
        .byte   $00, $00, $00, $0A
        .byte   $FF, $FF, $FF, $FF

; FOUT ($ED34).  The ROM writes the text at $0100 with "sta $FF,y" (abs,Y:
; there is no zp,Y STA) and "sta $0100..$0104,y"; here [FP_BUF],y with
; FP_BUF standing for $00FF, so $0100+k,y is index y+k+1.
fout:   ldy     #$01
fout_1: lda     #$2D            ; '-'
        dey
        bit     FACSIGN
        bpl     @led41
        iny
        sta     [FP_BUF],y
@led41: sta     FACSIGN
        sty     SERPNT
        iny
        lda     #$30
        ldx     FAC
        bne     @led4f
        jmp     fout_4
@led4f: lda     #$00
        cpx     #$80
        beq     @led57
        bcs     @led60
@led57: PTR     con_billion
        jsr     fmult
        lda     #$F7
@led60: sta     INDX
@led62: PTR     con_999999999
        jsr     fcomp
        beq     @led89
        bpl     @led7f
@led6d: PTR     con_99999999_9
        jsr     fcomp
        beq     @led78
        bpl     @led86
@led78: jsr     mul10
        dec     INDX
        bne     @led6d
@led7f: jsr     div10
        inc     INDX
        bne     @led62
@led86: jsr     faddh
@led89: jsr     qint
; FOUT.2 ($ED8C)
        ldx     #$01
        lda     INDX
        clc
        adc     #$0A
        bmi     @led9e
        cmp     #$0B
        bcs     @led9f
        adc     #$FF
        tax
        lda     #$02
@led9e: sec
@led9f: sbc     #$02
        sta     EXPON
        stx     INDX
        txa
        beq     @ledaa
        bpl     @ledbd
@ledaa: ldy     SERPNT
        lda     #$2E            ; '.'
        iny
        sta     [FP_BUF],y
        txa
        beq     @ledbb
        lda     #$30
        iny
        sta     [FP_BUF],y
@ledbb: sty     SERPNT
@ledbd: ldy     #$00
        ldx     #$80
@ledc1: lda     FAC+4
        clc
        adc     dectbl+3,y
        sta     FAC+4
        lda     FAC+3
        adc     dectbl+2,y
        sta     FAC+3
        lda     FAC+2
        adc     dectbl+1,y
        sta     FAC+2
        lda     FAC+1
        adc     dectbl,y
        sta     FAC+1
        inx
        bcs     @lede5
        bpl     @ledc1
        bmi     @lede7
@lede5: bmi     @ledc1
@lede7: txa
        bcc     @ledee
        eor     #$FF
        adc     #$0A
@ledee: adc     #$2F
        iny
        iny
        iny
        iny
        sty     VARPNT
        ldy     SERPNT
        iny
        tax
        and     #$7F
        sta     [FP_BUF],y
        dec     INDX
        bne     @lee09
        lda     #$2E
        iny
        sta     [FP_BUF],y
@lee09: sty     SERPNT
        ldy     VARPNT
        txa
        eor     #$FF
        and     #$80
        tax
        cpy     #$24
        bne     @ledc1
; FOUT.3 ($EE17): drop trailing zeros (and a bare point), then the exponent
        ldy     SERPNT
@lee19: lda     [FP_BUF],y
        dey
        cmp     #$30
        beq     @lee19
        cmp     #$2E
        beq     @lee26
        iny
@lee26: lda     #$2B            ; '+'
        ldx     EXPON
        beq     fout_5
        bpl     @lee36
        lda     #$00
        sec
        sbc     EXPON
        tax
        lda     #$2D            ; '-'
@lee36: iny                     ; sta $0101,y
        iny
        sta     [FP_BUF],y
        lda     #$45            ; 'E'
        dey                     ; sta $0100,y
        sta     [FP_BUF],y
        dey
        txa
        ldx     #$2F
        sec
@lee42: inx
        sbc     #$0A
        bcs     @lee42
        adc     #$3A
        iny                     ; sta $0103,y
        iny
        iny
        iny
        sta     [FP_BUF],y
        txa
        dey                     ; sta $0102,y
        sta     [FP_BUF],y
        lda     #$00
        iny                     ; sta $0104,y
        iny
        sta     [FP_BUF],y
        bra     fout_6          ; ROM: beq FOUT.6 right after lda #0
; FOUT.4 ($EE57), FOUT.5 ($EE5A), FOUT.6 ($EE5F)
fout_4: sta     [FP_BUF],y      ; "0" for zero
fout_5: lda     #$00
        iny                     ; sta $0100,y
        sta     [FP_BUF],y
fout_6: lda     #$00
        ldy     #$01
        rts

; =========================================================================
; Functions
; =========================================================================

fdiv:   jsr     load_arg_from_ptr
        jmp     fdivt
fsub:   jsr     load_arg_from_ptr
        jmp     fsubt

; The ROM's constants and coefficient tables, byte for byte ($E913-$E940,
; $EEDB-$EF08, $EFA6-$EFAE, $F066-$F093, $F0CE-$F10A).
rom_e913:
        .byte   $81, $00, $00, $00, $00                 ; CON.ONE
        .byte   $03                                     ; POLY.LOG
        .byte   $7F, $5E, $56, $CB, $79
        .byte   $80, $13, $9B, $0B, $64
        .byte   $80, $76, $38, $93, $16
        .byte   $82, $38, $AA, $3B, $20
        .byte   $80, $35, $04, $F3, $34                 ; CON.SQR.HALF
        .byte   $81, $35, $04, $F3, $34                 ; CON.SQR.TWO
        .byte   $80, $80, $00, $00, $00                 ; CON.NEG.HALF
        .byte   $80, $31, $72, $17, $F8                 ; CON.LOG.TWO
con_one      = rom_e913 + $00
poly_log     = rom_e913 + $05
con_sqr_half = rom_e913 + $1A
con_sqr_two  = rom_e913 + $1F
con_neg_half = rom_e913 + $24
con_log_two  = rom_e913 + $29

rom_eedb:
        .byte   $81, $38, $AA, $3B, $29                 ; CON.LOG.E
        .byte   $07                                     ; POLY.EXP
        .byte   $71, $34, $58, $3E, $56
        .byte   $74, $16, $7E, $B3, $1B
        .byte   $77, $2F, $EE, $E3, $85
        .byte   $7A, $1D, $84, $1C, $2A
        .byte   $7C, $63, $59, $58, $0A
        .byte   $7E, $75, $FD, $E7, $C6
        .byte   $80, $31, $72, $18, $10
        .byte   $81, $00, $00, $00, $00
con_log_e    = rom_eedb + $00
poly_exp     = rom_eedb + $05

; CON.RND.1 and CON.RND.2 overlap, and RND.2's last byte is the $20 that
; starts RND's code in the ROM - kept, it is part of the sequence
rom_efa6:
        .byte   $98, $35, $44, $7A, $68, $28, $B1, $46, $20
con_rnd_1    = rom_efa6 + 0
con_rnd_2    = rom_efa6 + 4

rom_f066:
        .byte   $81, $49, $0F, $DA, $A2                 ; CON.PI.HALF
        .byte   $83, $49, $0F, $DA, $A2                 ; CON.PI.DOUB
        .byte   $7F, $00, $00, $00, $00                 ; QUARTER
        .byte   $05                                     ; POLY.SIN
        .byte   $84, $E6, $1A, $2D, $1B
        .byte   $86, $28, $07, $FB, $F8
        .byte   $87, $99, $68, $89, $01
        .byte   $87, $23, $35, $DF, $E1
        .byte   $86, $A5, $5D, $E7, $28
        .byte   $83, $49, $0F, $DA, $A2
con_pi_half  = rom_f066 + $00
con_pi_doub  = rom_f066 + $05
quarter      = rom_f066 + $0A
poly_sin     = rom_f066 + $0F

poly_atn:                                               ; $F0CE
        .byte   $0B
        .byte   $76, $B3, $83, $BD, $D3
        .byte   $79, $1E, $F4, $A6, $F5
        .byte   $7B, $83, $FC, $B0, $10
        .byte   $7C, $0C, $1F, $67, $CA
        .byte   $7C, $DE, $53, $CB, $C1
        .byte   $7D, $14, $64, $70, $4C
        .byte   $7D, $B7, $EA, $51, $7A
        .byte   $7D, $63, $30, $88, $7E
        .byte   $7E, $92, $44, $99, $3A
        .byte   $7E, $4C, $CC, $91, $C7
        .byte   $7F, $AA, $AA, $AA, $13
        .byte   $81, $00, $00, $00, $00

; SQR ($EE8D): ARG = FAC, FAC = 0.5, then FPWRT
sqr:    jsr     copy_fac_to_arg_rounded
        PTR     con_half
        jsr     load_fac_from_ptr
; FPWRT ($EE97): FAC = ARG ^ FAC; A = FAC exponent with flags on entry
fpwrt:  bne     @nz             ; ROM: beq EXP (out of range here)
        jmp     exp
@nz:    lda     ARG
        bne     @leea0
        jmp     sta_in_fac_sign_and_exp
@leea0: DPTR    TEMP3
        jsr     store_fac_at_ptr_rounded
        lda     ARGSIGN
        bpl     @leeba
        jsr     int
        DPTR    TEMP3
        jsr     fcomp
        bne     @leeba
        tya
        ldy     CHARAC
@leeba: jsr     mfa
        tya
        pha
        jsr     log
        DPTR    TEMP3
        jsr     fmult
        jsr     exp
        pla
        lsr     a
        bcc     @rts
        jmp     negop
@rts:   rts

; LOG ($E941)
log:    jsr     sign
        beq     giq
        bpl     log_2
giq:    jmp     iqerr
log_2:  lda     FAC
        sbc     #$7F
        pha
        lda     #$80
        sta     FAC
        PTR     con_sqr_half
        jsr     fadd
        PTR     con_sqr_two
        jsr     fdiv
        PTR     con_one
        jsr     fsub
        SPTR    poly_log
        jsr     polynomial_odd
        PTR     con_neg_half
        jsr     fadd
        pla
        jsr     addacc
        PTR     con_log_two
        jmp     fmult           ; the ROM falls into FMULT

; EXP ($EF09)
exp:    PTR     con_log_e
        jsr     fmult
        lda     FAC_EXT
        adc     #$50
        bcc     @lef19
        jsr     increment_mantissa
@lef19: sta     ARG_EXT
        jsr     maf
        lda     FAC
        cmp     #$88
        bcc     @lef27
@lef24: jsr     outofrng
@lef27: jsr     int
        lda     CHARAC
        clc
        adc     #$81
        beq     @lef24
        sec
        sbc     #$01
        pha
        ldx     #$05
@lef37: lda     ARG,x
        ldy     FAC,x
        sta     FAC,x
        sty     ARG,x
        dex
        bpl     @lef37
        lda     ARG_EXT
        sta     FAC_EXT
        jsr     fsubt
        jsr     negop
        SPTR    poly_exp
        jsr     polynomial
        lda     #$00
        sta     SGNCPR
        pla
        jsr     add_exponents_1
        rts

; POLYNOMIAL.ODD ($EF5C): FAC = x * P(x^2), table at FP_SER
polynomial_odd:
        DPTR    TEMP1
        jsr     store_fac_at_ptr_rounded
        DPTR    TEMP1
        jsr     fmult
        jsr     sermain
        DPTR    TEMP1
        jmp     fmult

; POLYNOMIAL ($EF72) / SERMAIN ($EF76): Horner over the table at FP_SER
; (a count, then count+1 coefficients) with x in TEMP2
polynomial:
sermain:
        DPTR    TEMP2
        jsr     store_fac_at_ptr_rounded
        lda     [FP_SER]
        sta     SERLEN
        inc     FP_SER
        bne     @ptr
        inc     FP_SER+1
@ptr:   jsr     ser_to_ptr
@lef89: jsr     fmult
        rep     #$21
        .a16
        lda     FP_SER
        adc     #$0005
        sta     FP_SER
        sep     #$20
        .a8
        jsr     ser_to_ptr
        jsr     fadd
        DPTR    TEMP2
        dec     SERLEN
        bne     @lef89
        rts

ser_to_ptr:
        lda     FP_SER
        sta     FP_PTR
        lda     FP_SER+1
        sta     FP_PTR+1
        lda     FP_SER+2
        sta     FP_PTR+2
        rts

; RND ($EFAE)
rnd:    jsr     sign
        tax
        bmi     @lefcc
        DPTR    RNDSEED
        jsr     load_fac_from_ptr
        txa
        beq     @rts
        PTR     con_rnd_1
        jsr     fmult
        PTR     con_rnd_2
        jsr     fadd
@lefcc: ldx     FAC+4
        lda     FAC+1
        sta     FAC+4
        stx     FAC+1
        lda     #$00
        sta     FACSIGN
        lda     FAC
        sta     FAC_EXT
        lda     #$80
        sta     FAC
        jsr     normalize_fac_2
        DPTR    RNDSEED
        jmp     store_fac_at_ptr_rounded    ; GO.MOVMF
@rts:   rts

; COS ($EFEA), SIN ($EFF1)
cos:    PTR     con_pi_half
        jsr     fadd
sin:    jsr     copy_fac_to_arg_rounded
        PTR     con_pi_doub
        ldx     ARGSIGN
        jsr     div
        jsr     copy_fac_to_arg_rounded
        jsr     int
        lda     #$00
        sta     SGNCPR
        jsr     fsubt
        PTR     quarter
        jsr     fsub
        lda     FACSIGN
        pha
        bpl     sin_1
        jsr     faddh
        lda     FACSIGN
        bmi     sin_2
        lda     CPRMASK
        eor     #$FF
        sta     CPRMASK
sin_1:  jsr     negop
sin_2:  PTR     quarter
        jsr     fadd
        pla
        bpl     @lf033
        jsr     negop
@lf033: SPTR    poly_sin
        jmp     polynomial_odd

; TAN ($F03A)
tan:    DPTR    TEMP1
        jsr     store_fac_at_ptr_rounded
        lda     #$00
        sta     CPRMASK
        jsr     sin
        DPTR    TEMP3
        jsr     store_fac_at_ptr_rounded
        DPTR    TEMP1
        jsr     load_fac_from_ptr
        lda     #$00
        sta     FACSIGN
        lda     CPRMASK
        jsr     tan_1
        DPTR    TEMP3
        jmp     fdiv
tan_1:  pha
        jmp     sin_1

; ATN ($F09E)
atn:    lda     FACSIGN
        pha
        bpl     @lf0a6
        jsr     negop
@lf0a6: lda     FAC
        pha
        cmp     #$81
        bcc     @lf0b4
        PTR     con_one
        jsr     fdiv
@lf0b4: SPTR    poly_atn
        jsr     polynomial_odd
        pla
        cmp     #$81
        bcc     @lf0c7
        PTR     con_pi_half
        jsr     fsub
@lf0c7: pla
        bpl     @rts
        jmp     negop
@rts:   rts
