; vm.s - the applesoft816 bytecode VM (docs/INTERP.md).
;
; One image per bank: this VM and fp816 (linked in), then the program the
; compiler writes after them.  Bank-independent: everything is addressed
; within the bank (PBR = DBR = this bank while the VM runs), bank 0 through
; long operands, and the bank byte comes from PHK.
;
; Machine state while running: native, M = 1 (8-bit A), X = 0 (16-bit X/Y),
; D = the VM's direct page in bank 0 (shared with fp816: fp816's offsets for
; FAC/ARG and its scratch, the VM's own at $20-$4F), S in page 1 (the ROM's
; emulation-mode calls need it there), DBR = PBR = this bank.
;
; Bytecode is direct-threaded: each operation is the 16-bit address of its
; handler, then its operands.  X is the instruction pointer, at the
; operation's own word; dispatch is JMP (0,X), which reads the PROGRAM bank.
;
; Bit-exactness: the operations are the ROM's own steps (each names the ROM
; routine it follows); floating point is fp816, checked against the ROM.

        .p816
        .smart  off

        .import fp_load_fac, fp_load_arg, fp_store_fac, fp_addt, fp_subt, fp_cmp
        .import fp_sgn, fp_ayint, fp_givayf, fp_negop, fp_fout, fp_round, fp_multt
        .import fp_divt, fp_pwrt, fp_float, fp_uint16, fp_add, fp_table
        .import fp_int, fp_abs, fp_sqr, fp_rnd, fp_log, fp_exp, fp_cos, fp_sin, fp_tan, fp_atn
        .import fp_conint, fp_fin

; ---- direct page ----
; fp816's (see fp.s)
FP_PTR   = $5E
FAC      = $9D
FACSIGN  = $A2
ARG      = $A5
ARGSIGN  = $AA
SGNCPR   = $AB
FAC_EXT  = $AC
FP_ERR   = $F2
FP_RES   = $F3
FP_INT   = $F8
FP_BUF   = $FD
FP_TXT   = $FA
; the VM's
VM_IP    = $20          ; 2: IP saved across calls
VM_FSP   = $22          ; 2: expression stack pointer (grows up)
VM_CSP   = $24          ; 2: control stack pointer (FOR/GOSUB frames, grows up)
CURLIN   = $26          ; 2: current line ($FFxx in direct mode)
VM_S     = $28          ; 2: S at vm_run, for END and errors
COUT_VEC = $2A          ; 3: long pointer to the bank-0 COUT stub (set by the loader)
VM_FLASH = $2D          ; 1: FLASH.BIT ($40 while FLASH)
VM_SPEED = $2E          ; 1: SPEEDZ (1 = SPEED=255)
VM_ERR   = $2F          ; 1: why the run ended: 0 END, else the AppleSoft error code
VM_T0    = $30          ; 2: scratch
VM_T1    = $32          ; 2: scratch
VM_T2    = $34          ; 2: scratch
VM_CLIM  = $36          ; 2: control stack limit
VM_PROG  = $38          ; 2: the program header's address
VM_CBASE_PTR = $3A      ; 2: the control stack's base (empty)
SACC     = $3C          ; 4: the string accumulator: length, address (2), bank
VM_SSP   = $40          ; 2: the string temporary stack (grows up, 4 bytes an entry)
SPTR     = $42          ; 3: long pointer, scratch
SPTR2    = $45          ; 3: long pointer, scratch
SH_BANK  = $48          ; 1: the string heap's bank (set by the loader)
SH_FREE  = $49          ; 2: the next free byte of the current half
SH_END   = $4B          ; 2: the end of the current half
SH_HALF  = $4D          ; 1: which half (0: $0000-$7FFF, 1: $8000-$FFFF)
VM_T3    = $4E          ; 2: scratch
VM_T4    = $B0          ; 2: scratch (a free fp816 offset)
VM_T5    = $B2          ; 2: scratch
VM_NEWF  = $B4          ; 2: the collector's free pointer in the new half
VM_SDESC = $B6          ; 4: scratch descriptor (in the direct page)

ERR_NEXT_WITHOUT_FOR  = $00
ERR_RETURN_WITHOUT_GOSUB = $16
ERR_ILLQTY  = $35
ERR_OUT_OF_MEMORY = $4D
ERR_UNDEF_STATEMENT = $5A
ERR_BAD_SUBSCRIPT = $6B
ERR_REDIM   = $78
ERR_FORMULA = $BF
ERR_STRING_TOO_LONG = $B0

FRAME_FOR   = $81       ; the ROM's FOR token, as its frames are marked
FRAME_GOSUB = $B0       ; the ROM's GOSUB token
FOR_SIZE    = 19        ; var 2, sign 1, step 5, limit 5, body 2, line 2, mark 1... (+1 spare)
GOSUB_SIZE  = 5         ; return IP 2, line 2, mark 1

.macro  NEXTOP  n
        .repeat n
        inx
        .endrep
        jmp     ($0000,x)
.endmacro

; call an fp816 entry (RTL-returning) from within this bank; X (IP) kept
.macro  FP      entry
        stx     VM_IP
        phk
        jsr     entry
        ldx     VM_IP
.endmacro

; ... and fail with its error, if it had one
.macro  FPCHK   entry
        FP      entry
        lda     FP_ERR
        beq     :+
        jmp     vm_error
:
.endmacro

; FP_PTR = Y, in this bank
.macro  PTRY
        sty     FP_PTR
        phk
        pla
        sta     FP_PTR+2
.endmacro

        .segment "HEADER"
; ---- the image header, at the bank's $0000 ----
        jmp     vm_run          ; +$00  JSL here to run the program (see vm_run)
        nop
        .byte   "A816"          ; +$04
        .word   1               ; +$08  version
        .word   OP_COUNT        ; +$0A
        .word   optable         ; +$0C  handler address of each operation, by number
        .word   prog_start      ; +$0E  where the program image begins (first free byte)
        .word   stub_start      ; +$10  the bank-0 COUT stub: copy it into bank 0...
        .word   stub_end - stub_start ; +$12  ...this many bytes, point COUT_VEC at it
        .word   msgtab          ; +$14
        .word   fp_table        ; +$16  fp816's jump table (fp.s), for the platform's FIN

        .segment "CODE"

; The operations, in their numbering (the compiler's op.h follows this order).
optable:
        .word   op_end, op_line, op_ldc, op_ldv, op_stv, op_push
        .word   op_add, op_sub, op_mul, op_div, op_pow, op_neg, op_not, op_and, op_or, op_rel
        .word   op_prnum, op_prstr, op_prcr, op_prcomma
        .word   op_goto, op_ifnot, op_gosub, op_return
        .word   op_forbegin, op_forlim, op_forstep, op_next, op_next0
        .word   op_idx, op_lda, op_sta, op_dim, op_err, op_fn
        .word   op_slit, op_sldv, op_sstv, op_spush, op_scat, op_srel, op_sprint
        .word   op_left, op_right, op_midp, op_mid2, op_mid3, op_len, op_asc, op_chr
        .word   op_str, op_val, op_fre
        .word   op_ldvi, op_stvi, op_ldai, op_stai, op_slda, op_ssta
OP_COUNT = (* - optable) / 2

; =========================================================================
; Entry and exit
; =========================================================================

; vm_run: JSL with D = the VM's direct page (COUT_VEC, VM_FLASH, VM_SPEED
; set), native mode, any M/X, S in page 1.  Runs the program from its entry
; to END or an error; returns with VM_ERR = 0 (END) or the error code, the
; caller's P and DBR restored.
vm_run:
        php
        phb
        phk
        plb
        rep     #$30
        .a16
        .i16
        tsc
        sta     VM_S
        lda     #prog_start
        sta     VM_PROG
        tay
        lda     a:$0002,y       ; program header: expression stack base
        sta     VM_FSP
        lda     a:$0004,y       ; control stack base, limit
        sta     VM_CSP
        sta     VM_CBASE_PTR
        lda     a:$0006,y
        sta     VM_CLIM
        lda     a:$000C,y       ; the string temporary stack
        sta     VM_SSP
        stz     SH_FREE         ; the string heap: the low half, empty
        lda     #$8000
        sta     SH_END
        lda     #$FFFF
        sta     CURLIN
        ldx     a:$0000,y       ; entry IP
        lda     COUT_VEC        ; patch the COUT trampoline
        sta     cout_target
        sep     #$20
        .a8
        lda     COUT_VEC+2
        sta     cout_target+2
        stz     SH_HALF
        stz     VM_ERR
        jmp     ($0000,x)

; leave: VM_ERR set
vm_leave:
        rep     #$20
        .a16
        lda     VM_S
        tcs
        sep     #$20
        .a8
        plb
        plp
        rtl

; vm_error: A = the AppleSoft error code.  As ERROR ($D412): CR, "?", the
; message, " ERROR" and the bell, then " IN " and the line unless direct.
; (ONERR: M3.)
vm_error:
        sta     VM_ERR
        jsr     crdo
        lda     #'?'
        jsr     outdo
        rep     #$20
        .a16
        lda     VM_ERR
        and     #$00FF
        tay                     ; Y = the message's offset
        sep     #$20
        .a8
@msg:   lda     msgtab,y
        pha
        jsr     outdo
        iny
        pla
        bpl     @msg
        ldy     #msg_error - msgtab
        jsr     strout_y
        lda     CURLIN+1
        cmp     #$FF
        beq     @done
        ldy     #msg_in - msgtab
        jsr     strout_y
        rep     #$20            ; LINPRT: the line through FOUT
        .a16
        lda     CURLIN
        sta     FP_INT
        sep     #$20
        .a8
        FP      fp_uint16
        jsr     prfac
@done:  jmp     vm_leave

; print the 0-terminated text at msgtab+Y
strout_y:
@l:     lda     msgtab,y
        beq     @rts
        jsr     outdo
        iny
        bra     @l
@rts:   rts

; ERROR.MESSAGES ($D260): each message's last byte has its top bit set; the
; error code is the offset of its message.  Then " ERROR" + bell, " IN ".
msgtab:
        .byte   "NEXT WITHOUT FO", 'R'|$80
        .byte   "SYNTA", 'X'|$80
        .byte   "RETURN WITHOUT GOSU", 'B'|$80
        .byte   "OUT OF DAT", 'A'|$80
        .byte   "ILLEGAL QUANTIT", 'Y'|$80
        .byte   "OVERFLO", 'W'|$80
        .byte   "OUT OF MEMOR", 'Y'|$80
        .byte   "UNDEF'D STATEMEN", 'T'|$80
        .byte   "BAD SUBSCRIP", 'T'|$80
        .byte   "REDIM'D ARRA", 'Y'|$80
        .byte   "DIVISION BY ZER", 'O'|$80
        .byte   "ILLEGAL DIREC", 'T'|$80
        .byte   "TYPE MISMATC", 'H'|$80
        .byte   "STRING TOO LON", 'G'|$80
        .byte   "FORMULA TOO COMPLE", 'X'|$80
        .byte   "CAN'T CONTINU", 'E'|$80
        .byte   "UNDEF'D FUNCTIO", 'N'|$80
msg_error:
        .byte   " ERROR", $07, 0
msg_in: .byte   " IN ", 0

; =========================================================================
; Output: OUTDO ($DB5C), CRDO ($DAFB), through the bank-0 COUT stub
; =========================================================================

; OUTDO: A = the character; high bit set, FLASH applied to printables, COUT,
; then the monitor's WAIT with SPEEDZ.  A, X, Y kept.
outdo:  pha
        phy
        ora     #$80
        cmp     #$A0
        bcc     @raw
        ora     VM_FLASH
@raw:   stx     VM_T2
        ldy     VM_SPEED
        phk
        jsr     cout_tramp
        ldx     VM_T2
        ply
        pla
        rts

; JML long, its target patched from COUT_VEC by vm_run.  (JML [abs] would
; read its pointer from bank 0 at that absolute address, not the direct page.)
cout_tramp:
        .byte   $5C             ; JML long
cout_target:
        .faraddr 0

crdo:   lda     #$0D
        jmp     outdo

; print the 0-terminated FOUT text at fout_buf
prfac:  phy
        ldy     #fout_buf - 1
        sty     FP_BUF
        phk
        pla
        sta     FP_BUF+2
        FP      fp_fout
        ldy     #$0000
@l:     lda     fout_buf,y
        beq     @done
        jsr     outdo
        iny
        bra     @l
@done:  ply
        rts

fout_buf:
        .res    20

; ---- the bank-0 COUT stub (copied into bank 0 by the loader) ----
; JML'd to with A = the character, Y = SPEEDZ (low byte), native; calls the
; monitor's COUT and WAIT in emulation mode with D = 0, DBR = 0, then RTLs.
; Position-independent: no jumps within itself.
stub_start:
        php
        phd
        phb
        pea     $0000
        pld
        phk
        plb
        sec
        xce                     ; emulation: S must be in page 1 (it is)
        phy
        jsr     $FDED           ; COUT
        pla
        jsr     $FCA8           ; WAIT (SPEEDZ)
        clc
        xce
        plb
        pld
        plp
        rtl
stub_end:

; =========================================================================
; The operations
; =========================================================================

op_end:
        stz     VM_ERR
        jmp     vm_leave

; LINE n: the line's number into CURLIN; AppleSoft's Ctrl-C check (ISCNTC)
op_line:
        ldy     a:$0002,x
        sty     CURLIN
        lda     f:$00C000       ; keyboard
        bpl     @go
        cmp     #$83            ; Ctrl-C
        beq     @break
@go:    NEXTOP  4
@break: jmp     op_end          ; (M3: BREAK IN n, CONT)

; LDC k: FAC = the constant at k: exp, mantissa 4, sign, rounding byte -
; what FIN made of the constant's text at compile time
op_ldc:
        ldy     a:$0002,x
        rep     #$20
        .a16
        lda     a:$0000,y
        sta     FAC
        lda     a:$0002,y
        sta     FAC+2
        lda     a:$0004,y
        sta     FAC+4           ; mantissa low byte and the sign
        sep     #$20
        .a8
        lda     a:$0006,y
        sta     FAC_EXT
        NEXTOP  4

; LDV v: FAC = the packed variable at v (LOAD.FAC.FROM.YA)
op_ldv:
        ldy     a:$0002,x
        lda     a:$0000,y
        sta     FAC
        lda     a:$0001,y
        sta     FACSIGN
        ora     #$80
        sta     FAC+1
        lda     a:$0002,y
        sta     FAC+2
        lda     a:$0003,y
        sta     FAC+3
        lda     a:$0004,y
        sta     FAC+4
        stz     FAC_EXT
        NEXTOP  4

; STV v: the variable at v = FAC, rounded (STORE.FAC.AT.YX.ROUNDED)
op_stv:
        ldy     a:$0002,x
        PTRY
        FPCHK   fp_store_fac
        NEXTOP  4

; PUSH: FRMEVL's FRM.STACK: round FAC, push it (sign kept apart)
op_push:
        FPCHK   fp_round
        ldy     VM_FSP
        rep     #$20
        .a16
        lda     FAC
        sta     a:$0000,y
        lda     FAC+2
        sta     a:$0002,y
        lda     FAC+4           ; low mantissa byte, sign
        sta     a:$0004,y
        tya
        clc
        adc     #$0006
        sta     VM_FSP
        sep     #$20
        .a8
        NEXTOP  2

; pop the pushed operand into ARG, SGNCPR (FRM.PERFORM.2)
poparg: ldy     VM_FSP
        dey
        dey
        dey
        dey
        dey
        dey
        sty     VM_FSP
        rep     #$20
        .a16
        lda     a:$0000,y
        sta     ARG
        lda     a:$0002,y
        sta     ARG+2
        lda     a:$0004,y
        sta     ARG+4           ; ARG+4 low mantissa, ARG+5 = ARGSIGN
        sep     #$20
        .a8
        lda     ARGSIGN
        eor     FACSIGN
        sta     SGNCPR
        rts

op_add: jsr     poparg
        FPCHK   fp_addt
        NEXTOP  2
op_sub: jsr     poparg
        FPCHK   fp_subt
        NEXTOP  2
op_mul: jsr     poparg
        FPCHK   fp_multt
        NEXTOP  2
op_div: jsr     poparg
        FPCHK   fp_divt
        NEXTOP  2
op_pow: jsr     poparg
        FPCHK   fp_pwrt
        NEXTOP  2
op_neg: jsr     poparg          ; unary minus: the ROM pushed and pops too
        FP      fp_negop
        NEXTOP  2

; NOT (EQUOP $DE98): FAC = 1 if FAC is 0, else 0 (SNGFLT)
op_not: jsr     poparg
        ldy     #$0000
        lda     FAC
        bne     @f
        iny
@f:     jmp     sngflt

; AND ($DF55), OR ($DF4F): on the exponents (0 = false); FAC = 0 or 1
op_and: jsr     poparg
        ldy     #$0000
        lda     ARG
        beq     sngflt
        lda     FAC
        beq     sngflt
        iny
        bra     sngflt
op_or:  jsr     poparg
        ldy     #$0000
        lda     ARG
        ora     FAC
        beq     sngflt
        iny
; SNGFLT ($E301): FAC = Y (0 or 1) as GIVAYF makes it
sngflt: sty     FP_INT
        FP      fp_givayf
        NEXTOP  2

; REL m: RELOPS ($DF65) for numbers: re-pack ARG's sign, FCOMP ARG with FAC,
; CMPDONE ($DFC1): (result + 1) * 2 + C, AND the operator mask m (> 1, = 2,
; < 4); FAC = 0 or 1 (FLOAT)
op_rel: jsr     poparg
        lda     ARGSIGN
        ora     #$7F
        and     ARG+1
        sta     ARG+1
        rep     #$21            ; FP_PTR = this direct page + ARG (bank 0)
        .a16
        tdc
        adc     #ARG
        sta     FP_PTR
        sep     #$20
        .a8
        stz     FP_PTR+2
        FP      fp_cmp
        lda     FP_RES          ; 0, 1 or $FF; $FF comes with C set (CMPDONE)
        clc
        adc     #$01            ; $FF+1 = 0, 0+1 = 1, 1+1 = 2
        asl     a
        cmp     #$00            ; was $FF: ROL brought in C = 1
        bne     @shifted
        lda     #$01
@shifted:
        and     a:$0002,x
        beq     @f
        lda     #$01
@f:     sta     FP_INT
        FP      fp_float
        NEXTOP  3

; PRNUM: FOUT, then the text through OUTDO (PRINT's STRLIT/STRPRT)
op_prnum:
        jsr     prfac
        NEXTOP  2

; PRSTR s: the string at s (length byte, then text) through OUTDO (STRPRT)
op_prstr:
        ldy     a:$0002,x
        lda     a:$0000,y
        sta     VM_T0
        stz     VM_T0+1
@l:     lda     VM_T0
        beq     @done
        iny
        lda     a:$0000,y
        jsr     outdo
        dec     VM_T0
        bra     @l
@done:  NEXTOP  4

op_prcr:
        jsr     crdo
        NEXTOP  2

; PRCOMMA: PR.COMMA ($DB03) as the IIgs ROM has it.  Its column helper
; (LD8DD) ends with BIT $C01F, which leaves N = the 80-column switch, so:
; 40 columns - CH < 24: spaces to the next multiple of 16, else a new line
; (as the IIe); 80 columns - OURCH < 73 (the CMP #$49): spaces, else the
; same CH-style test.  Spaces: target - CH (LF7CB), none if negative.
op_prcomma:
        stx     VM_IP
        lda     f:$00C01F       ; 80COL
        bmi     @col80
        lda     f:$000024       ; CH
        bra     @test24
@col80: lda     f:$00057B       ; OURCH
        cmp     #$49
        bmi     @tab
@test24:
        cmp     #$18
        bcc     @tab
        jsr     crdo
        bra     @done
@tab:   adc     #$10            ; C = 0 on both ways here
        and     #$F0
        sta     VM_T0           ; the target column
        lda     f:$00C01F
        bmi     @t80
        lda     VM_T0
        sec
        sbc     f:$000024
        bra     @spaces
@t80:   lda     VM_T0
        sec                     ; (PR.TAB.OR.SPC's SEC)
        sbc     f:$00057B
@spaces:
        bcc     @done
        tay                     ; Y = how many (8-bit count in a 16-bit Y)
        rep     #$20
        .a16
        tya
        and     #$00FF
        tay
        sep     #$20
        .a8
@sp:    cpy     #$0000
        beq     @done
        lda     #' '
        jsr     outdo
        dey
        bra     @sp
@done:  ldx     VM_IP
        NEXTOP  2

op_goto:
        ldy     a:$0002,x
        tyx
        jmp     ($0000,x)

; IFNOT a: IF's test - FAC's exponent 0 means false: go to a (the next line)
op_ifnot:
        lda     FAC
        bne     @true
        ldy     a:$0002,x
        tyx
        jmp     ($0000,x)
@true:  NEXTOP  4

; GOSUB a: frame [return IP, CURLIN, $B0]
op_gosub:
        ldy     VM_CSP
        rep     #$20
        .a16
        tya
        clc
        adc     #GOSUB_SIZE
        cmp     VM_CLIM
        bcs     @full
        sta     VM_CSP
        txa
        clc
        adc     #$0004
        sta     a:$0000,y       ; return IP
        lda     CURLIN
        sta     a:$0002,y
        sep     #$20
        .a8
        lda     #FRAME_GOSUB
        sta     a:$0004,y
        ldy     a:$0002,x
        tyx
        jmp     ($0000,x)
        .a16
@full:  sep     #$20
        .a8
        lda     #ERR_OUT_OF_MEMORY
        jmp     vm_error

; RETURN: drop FOR frames down to the GOSUB frame (GTFORPNT with no
; variable to match), or ?RETURN WITHOUT GOSUB
op_return:
@scan:  jsr     frame_top_mark
        cmp     #FRAME_GOSUB
        beq     @found
        cmp     #FRAME_FOR
        bne     @none
        jsr     drop_frame
        bra     @scan
@found: rep     #$20
        .a16
        lda     VM_CSP
        sec
        sbc     #GOSUB_SIZE
        sta     VM_CSP
        tay
        lda     a:$0002,y
        sta     CURLIN
        lda     a:$0000,y
        tax
        sep     #$20
        .a8
        jmp     ($0000,x)
@none:  lda     #ERR_RETURN_WITHOUT_GOSUB
        jmp     vm_error

; A = the mark of the frame on top of the control stack (0 if it is empty)
frame_top_mark:
        ldy     VM_CSP
        rep     #$20
        .a16
        tya
        cmp     VM_CBASE_PTR
        sep     #$20
        .a8
        beq     @empty
        dey
        lda     a:$0000,y
        rts
@empty: lda     #$00
        rts

; drop the frame on top (FOR or GOSUB)
drop_frame:
        jsr     frame_top_mark
        cmp     #FRAME_FOR      ; (8-bit: the mark)
        bne     @gosub
        rep     #$20
        .a16
        lda     VM_CSP
        sec
        sbc     #FOR_SIZE
        bra     @set
        .a8
@gosub: rep     #$20
        .a16
        lda     VM_CSP
        sec
        sbc     #GOSUB_SIZE
@set:   sta     VM_CSP
        sep     #$20
        .a8
        rts

; FOR frame, from its base:  +0 var  +2 step sign  +3 step (5, FAC bytes)
;  +8 limit (5, packed)  +13 body IP  +15 CURLIN  +17 spare  +18 mark $81

; find the FOR frame for variable VM_T1 (0 = any), dropping the FOR frames
; above it (GTFORPNT); Y = its base, Z clear if found
find_for:
@scan:  jsr     frame_top_mark
        cmp     #FRAME_FOR
        bne     @no
        rep     #$20
        .a16
        lda     VM_CSP
        sec
        sbc     #FOR_SIZE
        tay
        lda     VM_T1
        beq     @yes
        cmp     a:$0000,y
        beq     @yes
        sty     VM_CSP          ; not ours: drop it, look below
        sep     #$20
        .a8
        bra     @scan
@yes:   sep     #$20
        .a8
        lda     #$01            ; Z clear
        rts
@no:    lda     #$00            ; Z set
        rts

; FORBEGIN v: after the start value is stored - drop an open FOR on v (and
; the FOR frames above it), then open a new frame for v
op_forbegin:
        ldy     a:$0002,x
        sty     VM_T1
        stx     VM_IP
        ldy     VM_CSP
        sty     VM_T0
        jsr     find_for
        beq     @nofor
        sty     VM_CSP          ; drop it and everything above
        bra     @open
@nofor: ldy     VM_T0           ; none: the stack as it was
        sty     VM_CSP
@open:  ldy     VM_CSP
        rep     #$20
        .a16
        tya
        clc
        adc     #FOR_SIZE
        cmp     VM_CLIM
        bcs     @full
        sta     VM_CSP
        lda     VM_T1
        sta     a:$0000,y       ; the variable
        sep     #$20
        .a8
        lda     #FRAME_FOR
        sta     a:FOR_SIZE-1,y
        ldx     VM_IP
        NEXTOP  4
        .a16
@full:  sep     #$20
        .a8
        lda     #ERR_OUT_OF_MEMORY
        jmp     vm_error

; FORLIM: the limit (FAC): pack its sign into the mantissa, round, keep the
; five bytes (FOR $D79A: ORA #$7F / AND $9E, FRM.STACK.3)
op_forlim:
        lda     FACSIGN
        ora     #$7F
        and     FAC+1
        sta     FAC+1
        FPCHK   fp_round
        ldy     VM_CSP
        rep     #$20
        .a16
        tya
        sec
        sbc     #FOR_SIZE
        tay
        lda     FAC
        sta     a:$0008,y
        lda     FAC+2
        sta     a:$000A,y
        sep     #$20
        .a8
        lda     FAC+4
        sta     a:$000C,y
        NEXTOP  2

; FORSTEP: the step (FAC): its SIGN, then rounded, its five FAC bytes (STEP
; $D7AF, FRM.STACK.2/3); the loop body starts after this operation
op_forstep:
        lda     FAC             ; SIGN ($EB82)
        beq     @sg
        lda     FACSIGN
        asl     a
        lda     #$FF
        bcs     @sg
        lda     #$01
@sg:    pha
        FPCHK   fp_round
        ldy     VM_CSP
        rep     #$20
        .a16
        tya
        sec
        sbc     #FOR_SIZE
        tay
        lda     FAC
        sta     a:$0003,y
        lda     FAC+2
        sta     a:$0005,y
        txa
        clc
        adc     #$0002
        sta     a:$000D,y       ; body IP
        lda     CURLIN
        sta     a:$000F,y
        sep     #$20
        .a8
        lda     FAC+4
        sta     a:$0007,y
        pla
        sta     a:$0002,y       ; step sign
        NEXTOP  2

; NEXT v / NEXT: NEXT ($DCF9).  FAC = the step (FACSIGN = the sign byte),
; FADD the variable, store it rounded (SETFOR), FCOMP the limit; the loop
; ends when that comparison equals the step's sign.
op_next0:
        ldy     #$0000
        sty     VM_T1
        stx     VM_IP
        lda     #$02
        bra     next_common
op_next:
        ldy     a:$0002,x
        sty     VM_T1
        stx     VM_IP
        lda     #$04
next_common:
        sta     VM_T2+1         ; this operation's length
        jsr     find_for
        bne     @found
        lda     #ERR_NEXT_WITHOUT_FOR
        jmp     vm_error
@found: sty     VM_T0           ; the frame's base
        tya                     ; FP_PTR = the step bytes
        rep     #$21
        .a16
        tya
        adc     #$0003
        tay
        sep     #$20
        .a8
        PTRY
        FP      fp_load_fac
        ldy     VM_T0
        lda     a:$0002,y
        sta     FACSIGN
        rep     #$20
        .a16
        lda     a:$0000,y       ; the variable
        tay
        sep     #$20
        .a8
        PTRY
        FPCHK   fp_add          ; FAC = var + step
        ldy     VM_T0
        rep     #$20
        .a16
        lda     a:$0000,y
        tay
        sep     #$20
        .a8
        PTRY
        FPCHK   fp_store_fac    ; SETFOR
        rep     #$21
        .a16
        lda     VM_T0
        adc     #$0008          ; the limit
        tay
        sep     #$20
        .a8
        PTRY
        FP      fp_cmp
        ldy     VM_T0
        lda     FP_RES
        sec
        sbc     a:$0002,y
        beq     @done
        rep     #$20            ; again: the body, the FOR's line
        .a16
        lda     a:$000F,y
        sta     CURLIN
        lda     a:$000D,y
        tax
        sep     #$20
        .a8
        jmp     ($0000,x)
@done:  sty     VM_CSP          ; drop the frame, carry on after NEXT
        ldx     VM_IP
        lda     VM_T2+1
        cmp     #$04
        beq     @n4
        NEXTOP  2
@n4:    NEXTOP  4

; ---- arrays ----
; Array descriptor (in this bank), 4 + 2*n bytes: +0 element storage (0 =
; not dimensioned yet), +2 dimension count n (byte), +3 element size (5
; real, 2 integer, 4 string), +4 n bounds (DIM's values).
; IDX: the subscript in FAC as an integer (MAKINT: negative or > 32767 is
; ?ILLEGAL QUANTITY), pushed on the expression stack (2 bytes)
op_idx:
        lda     FACSIGN         ; MKINT: a negative sign byte is enough
        bpl     @pos
        lda     #ERR_ILLQTY
        jmp     vm_error
@pos:   FPCHK   fp_ayint
        ldy     VM_FSP
        lda     FAC+4
        sta     a:$0000,y
        lda     FAC+3
        sta     a:$0001,y
        iny
        iny
        sty     VM_FSP
        NEXTOP  2

; pop n subscripts for descriptor Y and compute the element address into Y;
; dimensions an undimensioned array 10 per subscript first
; (row-major as AppleSoft lays arrays out: first subscript varies fastest)
elem_addr:
        sty     VM_T1           ; the descriptor
        rep     #$20
        .a16
        lda     a:$0000,y
        bne     @have
        jsr     autodim
@have:  ldy     VM_T1
        lda     a:$0002,y       ; n (byte)
        and     #$00FF
        asl     a
        sta     VM_T2           ; 2n
        ; subscripts on the expression stack, first pushed first: walk from
        ; the last (innermost index) to the first
        lda     #$0000
        sta     VM_T0           ; the linear index
        lda     VM_FSP
        sec
        sbc     VM_T2
        sta     VM_FSP          ; popped
        ldy     VM_T2
@dim:   cpy     #$0000
        beq     @done
        dey
        dey
        ; VM_T0 = VM_T0 * (bound_k + 1) + sub_k, k from the last dimension down
        phy
        tya
        clc
        adc     VM_T1
        tay
        lda     a:$0004,y       ; bound_k
        ply
        sta     VM_T2+0         ; (reuse: bound)
        phy
        tya
        clc
        adc     VM_FSP
        tay
        lda     a:$0000,y       ; sub_k
        ply
        cmp     VM_T2
        beq     @inr
        bcs     @bad
@inr:   pha
        phy
        lda     VM_T2
        inc     a
        jsr     mul16           ; VM_T0 = VM_T0 * A
        ply
        pla
        clc
        adc     VM_T0
        sta     VM_T0
        bra     @dim
@done:  ldy     VM_T1           ; * the element size
        lda     a:$0003,y
        and     #$00FF
        jsr     mul16
        lda     VM_T0
        ldy     VM_T1
        clc
        adc     a:$0000,y
        tay
        sep     #$20
        .a8
        rts
@bad:   sep     #$20
        .a8
        lda     #ERR_BAD_SUBSCRIPT
        jmp     vm_error

; VM_T0 = VM_T0 * A (16-bit, small: the arrays fit the bank)
        .a16
mul16:  sta     VM_T2+0
        phx
        ldx     #$0000
        lda     VM_T0
@l:     lsr     VM_T2
        bcc     @s
        pha
        txa
        clc
        adc     1,s
        tax
        pla
@s:     asl     a
        ldy     VM_T2
        bne     @l
        stx     VM_T0
        plx
        rts
        .a8

; dimension descriptor VM_T1 as AppleSoft does on first use: 10 per subscript
        .a16
autodim:
        ldy     VM_T1
        lda     a:$0002,y
        and     #$00FF
        tax
        tya
        clc
        adc     #$0004
        tay
@b:     lda     #10
        sta     a:$0000,y
        iny
        iny
        dex
        bne     @b
        jmp     alloc_array
        .a8

; storage for descriptor VM_T1 from the array heap (bounds set); zeroed
        .a16
alloc_array:
        ldy     VM_T1
        lda     a:$0002,y
        and     #$00FF
        tax
        lda     #$0001
        sta     VM_T0
        tya
        clc
        adc     #$0004
        tay
@m:     phy
        lda     a:$0000,y
        inc     a
        jsr     mul16
        ply
        iny
        iny
        dex
        bne     @m
        ldy     VM_T1           ; * the element size = bytes
        lda     a:$0003,y
        and     #$00FF
        jsr     mul16
        ldy     VM_PROG
        lda     a:$0008,y       ; the array heap's free pointer
        tax
        clc
        adc     VM_T0
        bcs     @oom
        cmp     a:$000A,y       ; its end
        bcs     @oom
        sta     a:$0008,y
        ldy     VM_T1
        txa
        sta     a:$0000,y       ; storage
        ldy     VM_T0
@z:     cpy     #$0000          ; zero it
        beq     @ok
        sep     #$20
        .a8
        stz     a:$0000,x
        rep     #$20
        .a16
        inx
        dey
        bra     @z
@ok:    rts
@oom:   sep     #$20
        .a8
        lda     #ERR_OUT_OF_MEMORY
        jmp     vm_error
        .a8

; LDA d: FAC = the element (subscripts popped) - LOAD.FAC.FROM.YA
op_lda: stx     VM_IP
        ldy     a:$0002,x
        jsr     elem_addr
        ldx     VM_IP
        lda     a:$0000,y
        sta     FAC
        lda     a:$0001,y
        sta     FACSIGN
        ora     #$80
        sta     FAC+1
        lda     a:$0002,y
        sta     FAC+2
        lda     a:$0003,y
        sta     FAC+3
        lda     a:$0004,y
        sta     FAC+4
        stz     FAC_EXT
        NEXTOP  4

; STA d: the element = FAC rounded (the subscripts were pushed before the
; value was computed, as LET finds its variable first)
op_sta: stx     VM_IP
        ldy     a:$0002,x
        jsr     elem_addr
        ldx     VM_IP
        PTRY
        FPCHK   fp_store_fac
        NEXTOP  4

; DIM d: bounds from the n subscripts pushed (IDX), ?REDIM'D ARRAY if done
op_dim: stx     VM_IP
        ldy     a:$0002,x
        sty     VM_T1
        rep     #$20
        .a16
        lda     a:$0000,y
        bne     @redim
        lda     a:$0002,y
        and     #$00FF
        asl     a
        sta     VM_T2
        lda     VM_FSP
        sec
        sbc     VM_T2
        sta     VM_FSP
        jsr     dim_copy
        jsr     alloc_array
        sep     #$20
        .a8
        ldx     VM_IP
        NEXTOP  4
        .a16
@redim: sep     #$20
        .a8
        lda     #ERR_REDIM
        jmp     vm_error

; copy the popped bounds (at VM_FSP, n of them) into descriptor VM_T1
        .a16
dim_copy:
        ldy     VM_T1
        lda     a:$0002,y
        and     #$00FF
        tax                     ; n
        lda     VM_FSP
        sta     VM_T0
        tya
        clc
        adc     #$0004
        sta     VM_T2           ; the descriptor's bounds
@c:     ldy     VM_T0
        lda     a:$0000,y
        ldy     VM_T2
        sta     a:$0000,y
        inc     VM_T0
        inc     VM_T0
        inc     VM_T2
        inc     VM_T2
        dex
        bne     @c
        rts
        .a8

; ERR c: stop with AppleSoft error c - a syntax error the ROM would only find
; when it got here, a jump to a line that does not exist.  c >= $F0: a
; construct not implemented yet - leave without a message.
op_err: lda     a:$0002,x
        cmp     #$F0
        bcc     @msg
        sta     VM_ERR
        jmp     vm_leave
@msg:   jmp     vm_error

; FN f: FAC = the function f of FAC (UNARY $DF0C after PARCHK): SGN INT ABS
; SQR RND LOG EXP COS SIN TAN ATN
op_fn:  stx     VM_IP
        lda     a:$0002,x
        rep     #$20
        .a16
        and     #$00FF
        asl     a
        tax
        sep     #$20
        .a8
        phk
        jsr     (fntab,x)
        ldx     VM_IP
        lda     FP_ERR
        beq     @ok
        jmp     vm_error
@ok:    NEXTOP  3

fntab:  .word   fp_sgn, fp_int, fp_abs, fp_sqr, fp_rnd, fp_log, fp_exp, fp_cos, fp_sin
        .word   fp_tan, fp_atn


; =========================================================================
; Strings
; =========================================================================
; A string is a descriptor: length, address (2), bank.  Literals live in this
; bank (the compiler's pool: a length byte, then the text) and are used in
; place, as AppleSoft points into the program text; everything else is in
; the string heap's bank, each string after a 2-byte header (0, or where the
; collector moved it).  The heap is two halves; the collector copies what is
; live into the other half.  Its roots: the string variables and string
; arrays (the compiler lists them in the program header), the string
; temporary stack, and SACC.  An operation that allocates does so before it
; reads its operands, so a collection can move them first.

; salloc: A16 = the length (1-255).  SPTR = the new string's data (header 0).
; ?OUT OF MEMORY if a collection does not make room.
        .a16
salloc: sta     VM_T3
        jsr     @fits
        bcc     @ok
        jsr     gc_strings
        jsr     @fits
        bcc     @ok
        sep     #$20
        .a8
        lda     #ERR_OUT_OF_MEMORY
        jmp     vm_error
        .a16
@ok:    lda     SH_FREE
        sta     SPTR
        clc
        adc     VM_T3
        adc     #$0002
        sta     SH_FREE
        sep     #$20
        .a8
        lda     SH_BANK
        sta     SPTR+2
        lda     #$00            ; the header: not moved
        sta     [SPTR]
        ldy     #$0001
        sta     [SPTR],y
        rep     #$20
        .a16
        lda     SPTR
        clc
        adc     #$0002
        sta     SPTR
        rts
; C clear if VM_T3 bytes + header fit
@fits:  lda     SH_FREE
        clc
        adc     VM_T3
        bcs     @no
        adc     #$0002
        bcs     @no
        cmp     SH_END
        beq     @yes
        bcs     @no
@yes:   clc
        rts
@no:    sec
        rts
        .a8

; the collector
        .a16
gc_strings:
        phx
        phy
        lda     SH_HALF
        and     #$00FF
        beq     @to1
        lda     #$0000          ; to the low half
        sta     VM_NEWF
        lda     #$8000
        bra     @go
@to1:   lda     #$8000
        sta     VM_NEWF
        lda     #$0000          ; (the end: $10000 - wraps to 0)
@go:    sta     VM_T5           ; the new half's end
        ; SACC, through the scratch descriptor in the direct page... via a
        ; bank copy: fwd_desc works on this bank, so SACC is copied out
        lda     SACC
        sta     sacc_copy
        lda     SACC+2
        sta     sacc_copy+2
        ldy     #sacc_copy
        jsr     fwd_desc
        lda     sacc_copy
        sta     SACC
        lda     sacc_copy+2
        sta     SACC+2
        ; the string temporary stack
        ldy     VM_PROG
        lda     a:$000C,y
@st:    cmp     VM_SSP
        bcs     @vars
        tay
        pha
        jsr     fwd_desc
        pla
        clc
        adc     #$0004
        bra     @st
        ; string variables: a list of their addresses
@vars:  ldy     VM_PROG
        lda     a:$000E,y
        sta     VM_T4           ; the list
        lda     a:$0010,y
        sta     VM_T0           ; how many
@v:     lda     VM_T0
        beq     @arrays
        dec     VM_T0
        ldy     VM_T4
        lda     a:$0000,y
        tay
        jsr     fwd_desc
        inc     VM_T4
        inc     VM_T4
        bra     @v
        ; string arrays: a list of their descriptors
@arrays:
        ldy     VM_PROG
        lda     a:$0012,y
        sta     VM_T4
        lda     a:$0014,y
        sta     VM_T2           ; how many (VM_T2: mul16 is not used below)
@a:     lda     VM_T2
        beq     @done
        dec     VM_T2
        ldy     VM_T4
        lda     a:$0000,y       ; the descriptor
        tay
        lda     a:$0000,y       ; its storage
        beq     @anext
        sta     VM_T1           ; the element pointer
        jsr     elem_count      ; A = how many elements
        sta     VM_T3
@e:     lda     VM_T3
        beq     @anext
        dec     VM_T3
        ldy     VM_T1
        jsr     fwd_desc
        lda     VM_T1
        clc
        adc     #$0004
        sta     VM_T1
        bra     @e
@anext: inc     VM_T4
        inc     VM_T4
        bra     @a
@done:  lda     VM_NEWF
        sta     SH_FREE
        lda     VM_T5
        bne     @end
        lda     #$FFFF          ; the high half ends at $FFFF
@end:   sta     SH_END
        sep     #$20
        .a8
        lda     SH_HALF
        eor     #$01
        sta     SH_HALF
        rep     #$20
        .a16
        ply
        plx
        rts

; A = the number of elements of descriptor Y: the product of (bound + 1)
elem_count:
        phx
        lda     a:$0002,y
        and     #$00FF
        tax
        lda     #$0001
        sta     VM_T0+0
        sty     VM_SDESC
@m:     lda     VM_SDESC
        clc
        adc     #$0004
        sta     VM_SDESC
        tay
        lda     a:$0000,y
        inc     a
        phx
        jsr     mul16
        plx
        dex
        bne     @m
        lda     VM_T0
        plx
        rts
; (elem_count uses VM_T0 as mul16's accumulator: callers keep nothing there)

; move the string of descriptor Y (in this bank) into the new half, or point
; it where it already went
fwd_desc:
        lda     a:$0000,y
        and     #$00FF
        beq     @rts            ; empty: nothing to move
        sta     VM_SDESC+2      ; the length
        sep     #$20
        .a8
        lda     a:$0003,y
        cmp     SH_BANK
        rep     #$20
        .a16
        bne     @rts            ; not in the heap: a literal
        lda     a:$0001,y       ; the old address
        sec
        sbc     #$0002
        sta     SPTR            ; its header
        sep     #$20
        .a8
        lda     SH_BANK
        sta     SPTR+2
        sta     SPTR2+2
        rep     #$20
        .a16
        lda     [SPTR]
        beq     @copy
        sta     a:$0001,y       ; already moved
@rts:   rts
@copy:  phy
        lda     VM_NEWF         ; the new header and data
        sta     SPTR2
        lda     #$0000
        sta     [SPTR2]
        lda     VM_NEWF
        clc
        adc     #$0002
        sta     VM_SDESC        ; the new address
        sta     [SPTR]          ; the old header: where it went
        lda     SPTR
        clc
        adc     #$0002
        sta     SPTR            ; old data
        lda     VM_SDESC
        sta     SPTR2           ; new data
        sep     #$20
        .a8
        ldy     #$0000
@c:     lda     [SPTR],y
        sta     [SPTR2],y
        iny
        cpy     VM_SDESC+2
        bne     @c
        rep     #$20
        .a16
        lda     VM_SDESC
        clc
        adc     VM_SDESC+2
        sta     VM_NEWF
        ply
        lda     VM_SDESC
        sta     a:$0001,y
        rts
        .a8

sacc_copy:
        .res    4

; ---- the string operations ----

; SLIT k: SACC = the literal at k (length byte, then text), in place
op_slit:
        ldy     a:$0002,x
        lda     a:$0000,y
        sta     SACC
        iny
        sty     SACC+1
        phk
        pla
        sta     SACC+3
        NEXTOP  4

; SLDV v / SSTV v: SACC = the variable's descriptor, and back
op_sldv:
        ldy     a:$0002,x
        rep     #$20
        .a16
        lda     a:$0000,y
        sta     SACC
        lda     a:$0002,y
        sta     SACC+2
        sep     #$20
        .a8
        NEXTOP  4
op_sstv:
        ldy     a:$0002,x
        rep     #$20
        .a16
        lda     SACC
        sta     a:$0000,y
        lda     SACC+2
        sta     a:$0002,y
        sep     #$20
        .a8
        NEXTOP  4

; SPUSH: SACC onto the string temporary stack
op_spush:
        jsr     spush
        NEXTOP  2
spush:  ldy     VM_SSP
        rep     #$20
        .a16
        lda     SACC
        sta     a:$0000,y
        lda     SACC+2
        sta     a:$0002,y
        iny
        iny
        iny
        iny
        sty     VM_SSP
        sep     #$20
        .a8
        rts

; pop the string temporary stack's top into VM_SDESC (Y = where it was)
spop:   ldy     VM_SSP
        dey
        dey
        dey
        dey
        sty     VM_SSP
        rep     #$20
        .a16
        lda     a:$0000,y
        sta     VM_SDESC
        lda     a:$0002,y
        sta     VM_SDESC+2
        sep     #$20
        .a8
        rts

; SPTR2 = the data of descriptor VM_SDESC; SPTR... (helpers)
sdesc_ptr2:
        rep     #$20
        .a16
        lda     VM_SDESC+1
        sta     SPTR2
        sep     #$20
        .a8
        lda     VM_SDESC+3
        sta     SPTR2+2
        rts
sacc_ptr2:
        rep     #$20
        .a16
        lda     SACC+1
        sta     SPTR2
        sep     #$20
        .a8
        lda     SACC+3
        sta     SPTR2+2
        rts

; copy VM_T3 (A8 count, 0-255) bytes from SPTR2+VM_T4 to SPTR+VM_T5
copy_bytes:
        lda     VM_T3
        beq     @rts
        phx
        rep     #$20
        .a16
        lda     VM_T4
        clc
        adc     SPTR2
        sta     SPTR2
        lda     VM_T5
        clc
        adc     SPTR
        sta     SPTR
        sep     #$20
        .a8
        ldy     #$0000
@c:     lda     [SPTR2],y
        sta     [SPTR],y
        iny
        tya
        cmp     VM_T3
        bne     @c
        plx
@rts:   rts

; SCAT: CAT ($E597): SACC = (the stack's top) + SACC; ?STRING TOO LONG past 255
op_scat:
        stx     VM_IP
        ldy     VM_SSP
        dey                     ; (abs,Y carries into the next bank: no $FFFC,Y)
        dey
        dey
        dey
        lda     a:$0000,y       ; the left one's length (the top entry)
        clc
        adc     SACC
        bcc     @fits
        lda     #ERR_STRING_TOO_LONG
        jmp     vm_error
@fits:  sta     VM_T2           ; the total
        rep     #$20
        .a16
        and     #$00FF
        beq     @empty
        jsr     salloc          ; may move both operands
@empty: sep     #$20
        .a8
        jsr     spop            ; the left one
        lda     VM_SDESC
        sta     VM_T3
        jsr     sdesc_ptr2
        stz     VM_T4
        stz     VM_T4+1
        stz     VM_T5
        stz     VM_T5+1
        jsr     copy_bytes
        rep     #$20
        .a16
        lda     SPTR            ; (copy_bytes advanced SPTR by VM_T5 = 0)
        pha
        sep     #$20
        .a8
        lda     SACC
        sta     VM_T3
        jsr     sacc_ptr2
        lda     VM_SDESC        ; after the left one's bytes
        sta     VM_T5
        jsr     copy_bytes
        rep     #$20
        .a16
        pla
        sta     SACC+1
        sep     #$20
        .a8
        lda     VM_T2
        sta     SACC
        lda     SH_BANK
        sta     SACC+3
        ldx     VM_IP
        NEXTOP  2

; SREL m: STRCMP ($DF7D): the stack's top (left) against SACC (right); the
; result as CMPDONE makes it, AND m, FAC = 0 or 1
op_srel:
        stx     VM_IP
        jsr     spop
        jsr     sdesc_ptr2      ; SPTR2 = left
        rep     #$20
        .a16
        lda     SACC+1
        sta     SPTR
        sep     #$20
        .a8
        lda     SACC+3
        sta     SPTR+2          ; SPTR = right
        lda     VM_SDESC        ; lengths: the shorter is the count; the
        cmp     SACC            ; result if the common part is equal
        beq     @eqlen
        bcc     @lshort
        lda     SACC            ; left longer
        sta     VM_T3
        lda     #$FF
        bra     @set
@lshort:
        lda     VM_SDESC
        sta     VM_T3
        lda     #$01
        bra     @set
@eqlen: lda     SACC
        sta     VM_T3
        lda     #$00
@set:   sta     VM_T0           ; the length result
        stz     VM_T3+1         ; the count, zero-extended (16-bit Y compares)
        ldy     #$0000
@cmp:   cpy     VM_T3
        beq     @same
        lda     [SPTR2],y
        cmp     [SPTR],y
        bne     @diff
        iny
        bra     @cmp
@diff:  lda     #$FF            ; left > right
        bcs     @res
        lda     #$01
        bra     @res
@same:  lda     VM_T0
@res:   ldx     VM_IP
        clc                     ; CMPDONE: $FF -> 1, 0 -> 2, 1 -> 4
        adc     #$01
        asl     a
        bne     @shifted
        lda     #$01
@shifted:
        and     a:$0002,x
        beq     @f
        lda     #$01
@f:     sta     FP_INT
        FP      fp_float
        NEXTOP  3

; SPRINT: SACC through OUTDO (STRPRT)
op_sprint:
        jsr     sacc_ptr2
        ldy     #$0000
@l:     tya
        cmp     SACC
        beq     @done
        lda     [SPTR2],y
        jsr     outdo
        iny
        bra     @l
@done:  NEXTOP  2

; byte argument from FAC (CONINT: ?ILLEGAL QUANTITY unless 0-255) into A
conint: FPCHK   fp_conint
        lda     FP_RES
        rts

; LEFT$(s,n): n from FAC; s on the stack; n = 0 is ?ILLEGAL QUANTITY
op_left:
        stx     VM_IP
        jsr     conint
        beq     iq
        sta     VM_T0           ; n
        jsr     sub_setup       ; VM_SDESC = s (still on the stack)
        lda     VM_T0
        cmp     VM_SDESC
        bcc     @n
        lda     VM_SDESC
@n:     sta     VM_T3
        stz     VM_T4
        stz     VM_T4+1
        bra     sub_finish
iq:     lda     #ERR_ILLQTY
        jmp     vm_error

; RIGHT$(s,n)
op_right:
        stx     VM_IP
        jsr     conint
        beq     iq
        sta     VM_T0
        jsr     sub_setup
        lda     VM_T0
        cmp     VM_SDESC
        bcc     @n
        lda     VM_SDESC
@n:     sta     VM_T3
        lda     VM_SDESC
        sec
        sbc     VM_T3
        sta     VM_T4
        stz     VM_T4+1
        bra     sub_finish

; MIDP: MID$'s position from FAC (a byte, pushed); MID2: no count; MID3: the
; count from FAC.  Position 0 is ?ILLEGAL QUANTITY - after the count's own
; conversion, as SUBSTRING.SETUP checks it last.
op_midp:
        stx     VM_IP
        jsr     conint
        ldy     VM_FSP
        sta     a:$0000,y
        iny
        iny
        sty     VM_FSP
        ldx     VM_IP
        NEXTOP  2
op_mid2:
        stx     VM_IP
        lda     #$FF
        bra     mid
op_mid3:
        stx     VM_IP
        jsr     conint
mid:    sta     VM_T0           ; the count
        ldy     VM_FSP
        dey
        dey
        sty     VM_FSP
        lda     a:$0000,y       ; the position
        beq     iq
        dec     a
        sta     VM_T4           ; offset
        stz     VM_T4+1
        jsr     sub_setup
        lda     VM_T4
        cmp     VM_SDESC
        bcc     @in
        stz     VM_T3           ; past the end: ""
        bra     sub_finish
@in:    lda     VM_SDESC
        sec
        sbc     VM_T4
        cmp     VM_T0
        bcc     @c
        lda     VM_T0
@c:     sta     VM_T3
        ; fall into sub_finish

; allocate, then pop s (it stayed on the stack, a root, through any
; collection) and copy
sub_finish:
        rep     #$20
        .a16
        lda     VM_T3
        and     #$00FF
        beq     @e
        jsr     salloc
@e:     sep     #$20
        .a8
        jsr     spop
        jsr     sdesc_ptr2
        stz     VM_T5
        stz     VM_T5+1
        rep     #$20
        .a16
        lda     SPTR
        pha
        sep     #$20
        .a8
        jsr     copy_bytes
        rep     #$20
        .a16
        pla
        sta     SACC+1
        sep     #$20
        .a8
        lda     VM_T3
        sta     SACC
        lda     SH_BANK
        sta     SACC+3
        ldx     VM_IP
        NEXTOP  2

; VM_SDESC = the stack's top, without popping it
sub_setup:
        ldy     VM_SSP
        dey
        dey
        dey
        dey
        rep     #$20
        .a16
        lda     a:$0000,y
        sta     VM_SDESC
        lda     a:$0002,y
        sta     VM_SDESC+2
        sep     #$20
        .a8
        rts

; LEN(s): FAC = the length (SNGFLT)
op_len: lda     SACC
        sta     FP_INT
        stz     FP_INT+1
        FP      fp_givayf
        NEXTOP  2

; ASC(s): FAC = the first byte; "" is ?ILLEGAL QUANTITY
op_asc: lda     SACC
        bne     :+
        brl     iq
:       jsr     sacc_ptr2
        lda     [SPTR2]
        sta     FP_INT
        stz     FP_INT+1
        FP      fp_givayf
        NEXTOP  2

; CHR$(n): a one-byte string
op_chr: stx     VM_IP
        jsr     conint
        pha
        rep     #$20
        .a16
        lda     #$0001
        jsr     salloc
        sep     #$20
        .a8
        pla
        sta     [SPTR]
        lda     #$01
        sta     SACC
        rep     #$20
        .a16
        lda     SPTR
        sta     SACC+1
        sep     #$20
        .a8
        lda     SH_BANK
        sta     SACC+3
        ldx     VM_IP
        NEXTOP  2

; STR$(x): FOUT's text as a string
op_str: stx     VM_IP
        ldy     #fout_buf - 1
        sty     FP_BUF
        phk
        pla
        sta     FP_BUF+2
        FP      fp_fout
        ldy     #$0000          ; its length
@n:     lda     fout_buf,y
        beq     @len
        iny
        bra     @n
@len:   tya
        sta     VM_T3
        rep     #$20
        .a16
        and     #$00FF
        jsr     salloc
        sep     #$20
        .a8
        ldy     #$0000
@c:     cpy     VM_T3
        beq     @done
        lda     fout_buf,y
        sta     [SPTR],y
        iny
        bra     @c
@done:  lda     VM_T3
        sta     SACC
        rep     #$20
        .a16
        lda     SPTR
        sta     SACC+1
        sep     #$20
        .a8
        lda     SH_BANK
        sta     SACC+3
        ldx     VM_IP
        NEXTOP  2

; VAL(s): FIN over the text, a 0 put after it meanwhile and the byte put
; back, as VAL ($E707) does
op_val: stx     VM_IP
        jsr     sacc_ptr2
        ldy     SACC
        rep     #$20            ; Y = the length
        .a16
        tya
        and     #$00FF
        tay
        sep     #$20
        .a8
        lda     [SPTR2],y
        pha
        lda     #$00
        sta     [SPTR2],y
        phy
        rep     #$20
        .a16
        lda     SPTR2           ; FP_TXT = the text - 1 (fp_fin's CHRGET first)
        dec     a
        sta     FP_TXT
        sep     #$20
        .a8
        lda     SPTR2+2
        sta     FP_TXT+2
        FP      fp_fin
        ply
        jsr     sacc_ptr2
        pla
        sta     [SPTR2],y
        lda     FP_ERR
        beq     @ok
        jmp     vm_error
@ok:    NEXTOP  2

; FRE(x): collect, FAC = the free bytes of the half (at most 32767)
op_fre: stx     VM_IP
        rep     #$20
        .a16
        jsr     gc_strings
        lda     SH_END
        sec
        sbc     SH_FREE
        bpl     @f
        lda     #$7FFF
@f:     sta     FP_INT
        sep     #$20
        .a8
        FP      fp_givayf
        ldx     VM_IP
        NEXTOP  2

; ---- integer variables (%) ----

; LDVI v: FAC = the integer at v (high byte first), as GIVAYF
op_ldvi:
        ldy     a:$0002,x
        lda     a:$0000,y
        sta     FP_INT+1
        lda     a:$0001,y
        sta     FP_INT
        FP      fp_givayf
        NEXTOP  4

; STVI v: LET2: ROUND.FAC, AYINT (?ILLEGAL QUANTITY), the two bytes
op_stvi:
        FPCHK   fp_round
        FPCHK   fp_ayint
        ldy     a:$0002,x
        lda     FAC+3
        sta     a:$0000,y
        lda     FAC+4
        sta     a:$0001,y
        NEXTOP  4

; integer and string array elements
op_ldai:
        stx     VM_IP
        ldy     a:$0002,x
        jsr     elem_addr
        ldx     VM_IP
        lda     a:$0000,y
        sta     FP_INT+1
        lda     a:$0001,y
        sta     FP_INT
        FP      fp_givayf
        NEXTOP  4
op_stai:
        stx     VM_IP
        ldy     a:$0002,x
        jsr     elem_addr
        sty     VM_T4
        ldx     VM_IP
        FPCHK   fp_round
        FPCHK   fp_ayint
        ldy     VM_T4
        lda     FAC+3
        sta     a:$0000,y
        lda     FAC+4
        sta     a:$0001,y
        NEXTOP  4
op_slda:
        stx     VM_IP
        ldy     a:$0002,x
        jsr     elem_addr
        ldx     VM_IP
        rep     #$20
        .a16
        lda     a:$0000,y
        sta     SACC
        lda     a:$0002,y
        sta     SACC+2
        sep     #$20
        .a8
        NEXTOP  4
op_ssta:
        stx     VM_IP
        ldy     a:$0002,x
        jsr     elem_addr
        ldx     VM_IP
        rep     #$20
        .a16
        lda     SACC
        sta     a:$0000,y
        lda     SACC+2
        sta     a:$0002,y
        sep     #$20
        .a8
        NEXTOP  4

; ---- the end of the VM: the program image the compiler writes starts here
        .segment "TAIL"
prog_start:
