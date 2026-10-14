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

        .import fp_addt, fp_subt, fp_cmp
        .import fp_sgn, fp_givayf, fp_negop, fp_fout, fp_round, fp_multt
        .import fp_divt, fp_pwrt, fp_float, fp_uint16, fp_table
        .import fp_int, fp_abs, fp_sqr, fp_rnd, fp_log, fp_exp, fp_cos, fp_sin, fp_tan, fp_atn
        .import fp_conint, fp_fin, fp_getadr, fp_ayint16, fp_next, fp_packed_int, fp_int_packed, fp_whole

; ---- direct page ----
; fp816's (see fp.s)
FP_PTR   = $5E
FP_SER   = $AD          ; 3: fp_next's limit pointer
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
VM_IOMODE = $10         ; 1: text files (the shell sets it): bit 0 WRITE (OUTDO to the
                        ;    file), bit 1 READ (INPUT/GET from the file), bit 2 EXEC
                        ;    (INPUT/GET from the EXEC file, unless READ)
VM_IOQUIET = $11        ; 1: an INPUT/GET reading a file: nothing shows (prompts, echo)
VM_CBREQ = $12          ; 1: what the shell is called for: 0 a command, 1 out_buf to the
                        ;    file, 2 a line into $0200 (length in RC_X), 3 a key (RC_A)
VM_OUTLEN = $13         ; 1: bytes in out_buf
VM_CMDLEN = $18         ; 1: a CHR$(4) command being gathered: its length ($FF: none)
VM_LASTCR = $19         ; 1: the last character out was a return (a Ctrl-D starts a command)
VM_CMDDBR = $1A         ; 1: the shell's data bank, for its command entry
VM_CMDVEC = $1B         ; 3: the shell's command entry (JSL; bank 0: no shell, Ctrl-D prints)
VM_CH    = $1E          ; 1: outdo's character, 7-bit
VM_CHRAW = $1F          ; 1: ... as it came
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
RC_A     = $BA          ; romcall's results: A, X, Y, P (8-bit)
RC_X     = $BB
RC_Y     = $BC
RC_P     = $BD
RC_TGT   = $BE          ; 3: long pointer to romcall's JSR operand in bank 0
VM_INMODE = $CE         ; 1: INPUTFLG: 0 INPUT, $98 READ, $40 GET
DATPTR   = $CF          ; 2: READ's place in the program's text (VM_TXTBANK)
DATLIN   = $D1          ; 2: the line of the DATA being read
VM_INSTMT = $D3         ; 2: the INPUT operation's IP (?REENTER starts it again)
INPTR    = $D5          ; 3: the input text pointer between items (long)
VM_ITYPE = $D8          ; 1: the item's type: 0 real, 1 integer, 2 string
VM_ITGT  = $D9          ; 2: the item's variable or element (this bank)
VM_TRACE = $DB          ; 1: TRACE on ($80)
VM_STMTIP = $DC         ; 2: the current statement's IP (STMT, when ONERR is used)
VM_STMTCSP = $DE        ; 2: ... and the control stack then
VM_ERRIP = $E0          ; 2: RESUME's statement
VM_ERRCSP = $E2         ; 2: ... and its control stack
VM_ONERR = $E4          ; 2: the ONERR handler's line-table entry (0: none)
VM_DENTRY = $E6         ; 2: vm_direct's code (the shell's direct line)
VM_FRESH = $E8          ; 1: vm_direct CLEARs first (the image is new, or was edited)
VM_CONTIP = $E9         ; 2: where CONT goes on (OLDTEXT; 0: ?CAN'T CONTINUE)
VM_CONTLIN = $EB        ; 2: ... and its line (OLDLIN)
VM_TXTBANK = $ED        ; 1: the program's text's bank (header +$2A): READ's DATA
VM_LAZY  = $EE          ; 2: the line-table entry to compile (the shell's request 7);
                        ;    0 back: it did not fit
LINNUM   = $50          ; 2: GETADR's result (fp816)

ERR_NEXT_WITHOUT_FOR  = $00
ERR_RETURN_WITHOUT_GOSUB = $16
ERR_ILLQTY  = $35
ERR_OUT_OF_MEMORY = $4D
ERR_UNDEF_STATEMENT = $5A
ERR_BAD_SUBSCRIPT = $6B
ERR_REDIM   = $78
ERR_FORMULA = $BF
ERR_CANT_CONTINUE = $D2
ERR_STRING_TOO_LONG = $B0

FRAME_FOR   = $81       ; the ROM's FOR token, as its frames are marked
FRAME_GOSUB = $B0       ; the ROM's GOSUB token
FOR_SIZE    = 30        ; the FOR frame (find_for), its mark last
F_FLAGS     = $11       ; in it: bit 0 the limit is whole (F_LIMIT), bit 1 the step
F_STEP      = $12       ;   (F_STEP), bit 2 F_CACHE/F_CVAL hold the variable as NEXT
F_LIMIT     = $14       ;   stored it last
F_CACHE     = $16
F_CVAL      = $1B
NX_OUT      = $C2       ; fp_int_packed's packed number (5)
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
        .word   vm_direct       ; +$18  JSL here for a direct line (see vm_direct)
        .word   cmd_buf         ; +$1A  a CHR$(4) command for the shell: length, then text
        .word   out_buf         ; +$1C  WRITE's output for the shell (VM_OUTLEN bytes)

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
        .word   op_inpstart, op_readstart, op_getstart, op_item, op_inpend, op_restore
        .word   op_on, op_pop, op_deffn, op_fncall, op_fnret, op_pos, op_peek, op_adr, op_poke
        .word   op_wait, op_call, op_prn, op_inn, op_htab, op_vtab, op_home, op_normal
        .word   op_inverse, op_flash, op_speed, op_trace, op_notrace, op_text, op_gr, op_color
        .word   op_plot, op_hlin, op_vlin, op_scrn, op_stop, op_clear, op_run, op_onerr
        .word   op_resume, op_stmt, op_tab, op_spc, op_bytepush, op_lomem, op_himem
        .word   op_cont
        .word   op_hgr, op_hgr2, op_hcolor, op_hx, op_hplot, op_hplotto, op_hposn, op_shape
        .word   op_draw, op_xdraw, op_rot, op_scale, op_usr, op_amper, op_list, op_new, op_del, op_pdl
        .word   op_ldci, op_sliti
        .word   op_argv, op_argk, op_argki, op_adda, op_suba, op_mula, op_diva, op_powa
        .word   op_anda, op_ora, op_rela
OP_COUNT = (* - optable) / 2

; =========================================================================
; Entry and exit
; =========================================================================

; vm_run: JSL with D = the VM's direct page (COUT_VEC, VM_FLASH, VM_SPEED
; set), native mode, any M/X, S in page 1 and below $01FC: AppleSoft keeps
; $01FC-$01FF (INPUT puts a ',' at $01FF, the byte before its buffer).  Runs the program from its entry
; to END or an error; returns with VM_ERR = 0 (END) or the error code, the
; caller's P and DBR restored.
vm_run:
        php
        phb
        jsr     vm_setup
        jsr     clear_all       ; a RUN is a CLEAR first: the image may have run before
        stz     VM_FRESH
        rep     #$20
        .a16
        ldy     VM_PROG
        ldx     a:$0000,y       ; entry IP
        sep     #$20
        .a8
        stz     VM_ERR
        jmp     ($0000,x)

; vm_direct: JSL, as vm_run, for the code at VM_DENTRY in direct mode
; (CURLIN $FFFF) with the program's state as it stands - variables, FOR and
; GOSUB frames, DATA, strings - or CLEARed first if VM_FRESH (the image is
; new, or the program was edited: AppleSoft forgets its variables then)
vm_direct:
        php
        phb
        jsr     vm_setup
        lda     VM_FRESH
        beq     @go
        stz     VM_FRESH
        jsr     clear_all
@go:    rep     #$20
        .a16
        ldx     VM_DENTRY
        sep     #$20
        .a8
        stz     VM_ERR
        jmp     ($0000,x)

; what both entries do: DBR, VM_S (the caller's stack, under the return
; address and the two pushes), the stacks' bases, CURLIN, the trampolines.
; Returns with M = 1, X = 0.
vm_setup:
        phk
        plb
        rep     #$30
        .a16
        .i16
        tsc
        clc
        adc     #2              ; as it was before this JSR
        sta     VM_S
        lda     #prog_start
        sta     VM_PROG
        tay
        lda     a:$0002,y       ; program header: expression stack base
        sta     VM_FSP
        lda     a:$0004,y       ; control stack base, limit
        sta     VM_CBASE_PTR
        lda     a:$0006,y
        sta     VM_CLIM
        lda     a:$000C,y       ; the string temporary stack
        sta     VM_SSP
        lda     a:$002A,y       ; (and the text's bank, below)
        sta     VM_T0
        lda     #$FFFF
        sta     CURLIN
        lda     COUT_VEC        ; patch the COUT trampoline
        sta     cout_target
        clc                     ; and romcall's: the stub block's base + offsets
        adc     #romcall_stub - stub_start
        sta     romcall_target
        lda     COUT_VEC
        clc
        adc     #romcall_jsr + 1 - stub_start
        sta     RC_TGT
        sep     #$20
        .a8
        lda     COUT_VEC+2
        sta     cout_target+2
        sta     romcall_target+2
        sta     RC_TGT+2
        rep     #$20            ; the shell's command entry
        .a16
        lda     VM_CMDVEC
        sta     cmd_target
        sep     #$20
        .a8
        lda     VM_CMDVEC+2
        sta     cmd_target+2
        lda     VM_T0
        sta     VM_TXTBANK
        stz     VM_IOQUIET
        lda     #$FF            ; no command being gathered; a line starts
        sta     VM_CMDLEN
        lda     #1
        sta     VM_LASTCR
        rts

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
        stz     VM_IOQUIET
        sta     VM_ERR
        pha                     ; ONERR set ($D8 bit 7) and a handler: HANDLERR
        lda     f:$0000D8
        bpl     @noerr
        ldy     VM_ONERR
        beq     @noerr
        pla
        jmp     handlerr
@noerr: pla
        clc
        jsr     cont_stmt
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
        jsr     linprt
@done:  jmp     vm_leave

; cont_stmt: a running program stopped by an error goes on, at CONT,
; from the start of the statement that failed (C=0; C=1: the one after it,
; for a BASIC.SYSTEM error, which stops as AppleSoft's STOP) (NEWSTT keeps OLDTEXT there;
; OLDLIN stays what STOP/END/Ctrl-C left).  The failing operation's IP is
; X, or VM_IP when X is not in the code (a handler borrowed it); the
; statement is the last in the compiler's table (header +$24, +$26) that
; starts at or before it.
cont_stmt:
        lda     #0              ; (C=1: the statement after the failing one)
        rol     a
        sta     VM_T2
        lda     CURLIN+1
        cmp     #$FF
        beq     @rts8
        ldy     VM_PROG
        rep     #$20
        .a16
        txa
        cmp     a:$0000,y       ; the code: from the entry to header +$28
        bcc     @ip
        cmp     a:$0028,y
        bcc     @have
@ip:    lda     VM_IP
        cmp     a:$0000,y
        bcc     @none
        cmp     a:$0028,y
        bcs     @none
@have:  sta     VM_T0
        lda     a:$0024,y
        sta     VM_T1
        lda     a:$0026,y
        sta     VM_T4           ; hi
        stz     VM_T3           ; lo: entries below lo start at or before T0
@bs:    lda     VM_T3
        cmp     VM_T4
        bcs     @done
        clc
        adc     VM_T4
        lsr     a
        sta     VM_T5
        asl     a
        clc
        adc     VM_T1
        tay
        lda     a:$0000,y
        cmp     VM_T0
        beq     @le
        bcs     @gt
@le:    lda     VM_T5
        inc     a
        sta     VM_T3
        bra     @bs
@gt:    lda     VM_T5
        sta     VM_T4
        bra     @bs
@done:  lda     VM_T3
        beq     @none
        dec     a               ; the failing statement's entry
        sta     VM_T3
        lda     VM_T2
        and     #$00FF
        clc
        adc     VM_T3
        ldy     VM_PROG
        cmp     a:$0026,y       ; past the last: the END after the code
        bcc     @entry
        lda     a:$0028,y
        dec     a
        dec     a
        bra     @set
@entry: asl     a
        clc
        adc     VM_T1
        tay
        lda     a:$0000,y
@set:   sta     VM_CONTIP
@none:  sep     #$20
        .a8
@rts8:  rts

; LINPRT: CURLIN through FOUT
linprt: rep     #$20
        .a16
        lda     CURLIN
        sta     FP_INT
        sep     #$20
        .a8
        FP      fp_uint16
        jmp     prfac

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
msg_break:
        .byte   $0D, "BREAK", $07, 0

; =========================================================================
; Output: OUTDO ($DB5C), CRDO ($DAFB), through the bank-0 COUT stub
; =========================================================================

; OUTDO: A = the character; high bit set, FLASH applied to printables, COUT,
; then the monitor's WAIT with SPEEDZ.  A, X, Y kept.
;
; With a shell (VM_CMDVEC), BASIC.SYSTEM's commands first: a Ctrl-D as the
; first character of a line starts one, the return ends it; none of it is
; printed.  The shell runs it (cmd_tramp: A = 0 on, 1 leave: VM_ERR $FC, else
; an error ONERR takes, A its number).
outdo:  pha
        phy
        sta     VM_CHRAW
        lda     VM_CMDVEC+2
        beq     @show
        lda     VM_CHRAW
        and     #$7F
        sta     VM_CH
        lda     VM_CMDLEN
        cmp     #$FF
        beq     @line
        lda     VM_CH           ; gathering a command
        cmp     #$0D
        beq     @run
        lda     VM_CMDLEN
        cmp     #$FE
        bcs     @done           ; (too long: the rest is dropped)
        rep     #$20
        .a16
        and     #$00FF
        tay
        sep     #$20
        .a8
        lda     VM_CH
        sta     cmd_buf+1,y
        inc     VM_CMDLEN
        bra     @done
@run:   jsr     io_flush        ; what WRITE gathered goes first
        lda     VM_CMDLEN
        sta     cmd_buf
        lda     #$FF
        sta     VM_CMDLEN
        lda     #1
        sta     VM_LASTCR
        stz     VM_CBREQ
        jsr     shell_call
        bra     @done
@line:  lda     VM_IOQUIET      ; an INPUT reading a file shows nothing, and what
        bne     @done           ; it does not show does not start a line either
        lda     VM_CH
        cmp     #$04
        bne     @plain
        lda     VM_LASTCR
        beq     @plain
        stz     VM_CMDLEN       ; a command starts
        bra     @done
@plain: stz     VM_LASTCR
        cmp     #$0D
        bne     @io
        inc     VM_LASTCR
@io:    lda     VM_IOMODE       ; WRITE: into out_buf, to the file at a return
        lsr     a
        bcc     @show
        rep     #$20
        .a16
        lda     VM_OUTLEN
        and     #$00FF
        tay
        sep     #$20
        .a8
        lda     VM_CH
        sta     out_buf,y
        inc     VM_OUTLEN
        cmp     #$0D
        beq     @flush
        lda     VM_OUTLEN
        cmp     #$FF
        bcc     @done
@flush: jsr     io_flush
        bra     @done
@show:  lda     VM_CHRAW
        jmp     outdo_show
@done:  ply
        pla
        rts

; io_flush: out_buf to the WRITE file (the shell), if anything is there
io_flush:
        lda     VM_OUTLEN
        beq     @rts
        lda     #1
        sta     VM_CBREQ
        jsr     shell_call
        stz     VM_OUTLEN
@rts:   rts

; shell_call: the shell for VM_CBREQ, X kept; its answer: 0 on;
; 1 leave (VM_ERR $FC: RUN another, BYE...); $80 + n: BASIC.SYSTEM's error
; n, said - BREAK, as AppleSoft's STOP, CONT on at the next statement;
; else n for ONERR (HANDLERR)
shell_call:
        phx
        lda     VM_CMDDBR
        phk
        jsr     cmd_tramp
        plx
        cmp     #0
        beq     @rts
        cmp     #1
        beq     @leave
        cmp     #$80
        bcs     @break
        jmp     handlerr        ; ONERR's, A = its number
@leave: lda     #$FC
        sta     VM_ERR
        jmp     vm_leave
@break: sec                     ; CONT: the statement after this one
        jsr     cont_stmt
        jmp     break_msg
@rts:   rts

outdo_show:
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

cmd_tramp:
        .byte   $5C
cmd_target:
        .faraddr 0

romcall_tramp:
        .byte   $5C
romcall_target:
        .faraddr 0

; romcall: call the ROM routine at VM_T4 in emulation mode (D = 0, DBR =
; 0) with A, X, Y (low bytes); results in RC_A/X/Y/P.  Leaves A 8-bit,
; X/Y 16-bit, X = VM_IP.
romcall:
        pha
        rep     #$20
        .a16
        lda     VM_T4
        sta     [RC_TGT]
        sep     #$20
        .a8
        pla
        phk
        jsr     romcall_tramp
        rep     #$10
        .i16
        ldx     VM_IP
        rts

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

; romcall: JML'd to with A, X, Y (8-bit values) for the ROM routine whose
; address vm patched into the JSR below; results into the VM's direct page
; (RC_A, RC_X, RC_Y, RC_P).  Returns with M = X = 1.
romcall_stub:
        phd
        phb
        pea     $0000
        pld
        phk
        plb
        sec
        xce
romcall_jsr:
        jsr     $0000           ; patched
        clc
        xce
        plb
        pld
        sta     RC_A
        stx     RC_X
        sty     RC_Y
        php
        pla
        sta     RC_P
        rtl
stub_end:

; =========================================================================
; The operations
; =========================================================================

; END: in a running program, CONT goes on after it (END.., $D870)
op_end:
        inx
        inx
        jsr     cont_here
        stz     VM_ERR
        jmp     vm_leave

; cont_here: CONT will go on at X, in CURLIN - unless this is a direct line
cont_here:
        lda     CURLIN+1
        cmp     #$FF
        beq     @rts
        stx     VM_CONTIP
        ldy     CURLIN
        sty     VM_CONTLIN
@rts:   rts

; BREAK, and IN n unless direct; then back to the shell (VM_ERR $FD)
break_msg:
        stz     VM_IOQUIET
        ldy     #msg_break - msgtab
        jsr     strout_y
        lda     CURLIN+1
        cmp     #$FF
        beq     @out
        ldy     #msg_in - msgtab
        jsr     strout_y
        jsr     linprt
@out:   lda     #$FD
        sta     VM_ERR
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
@break: sta     f:$00C010       ; the key taken (INCHR)
        jsr     cont_here       ; CONT: this line again from its start
        jmp     break_msg

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

; LDCI k: FAC = k, the constant's seven bytes here (its first use; later
; uses LDC them where they are)
op_ldci:
        lda     a:$0008,x
        sta     FAC_EXT
        rep     #$21
        .a16
        lda     a:$0002,x
        sta     FAC
        lda     a:$0004,x
        sta     FAC+2
        lda     a:$0006,x
        sta     FAC+4           ; mantissa low byte and the sign
        txa
        adc     #$0009
        tax
        sep     #$20
        .a8
        jmp     ($0000,x)

; ARGV v / ARGK k / ARGKI k: PUSH, a load and the next operation's pop
; without the stack (the compiler fuses them): ARG = FAC rounded (FRM.STACK),
; FAC = the operand, SGNCPR = ARGSIGN eor FACSIGN (FRM.PERFORM.2)
fac_to_arg:
        jsr     round_fac
        rep     #$20
        .a16
        lda     FAC
        sta     ARG
        lda     FAC+2
        sta     ARG+2
        lda     FAC+4           ; low mantissa byte, the sign
        sta     ARG+4
        sep     #$20
        .a8
        rts
op_argv:
        jsr     fac_to_arg
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
        lda     ARGSIGN
        eor     FACSIGN
        sta     SGNCPR
        NEXTOP  4
op_argk:
        jsr     fac_to_arg
        ldy     a:$0002,x
        rep     #$20
        .a16
        lda     a:$0000,y
        sta     FAC
        lda     a:$0002,y
        sta     FAC+2
        lda     a:$0004,y
        sta     FAC+4
        sep     #$20
        .a8
        lda     a:$0006,y
        sta     FAC_EXT
        lda     ARGSIGN
        eor     FACSIGN
        sta     SGNCPR
        NEXTOP  4
op_argki:
        jsr     fac_to_arg
        lda     a:$0008,x
        sta     FAC_EXT
        rep     #$21
        .a16
        lda     a:$0002,x
        sta     FAC
        lda     a:$0004,x
        sta     FAC+2
        lda     a:$0006,x
        sta     FAC+4
        txa
        adc     #$0009
        tax
        sep     #$20
        .a8
        lda     ARGSIGN
        eor     FACSIGN
        sta     SGNCPR
        jmp     ($0000,x)

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
        jsr     store_fac_y
        NEXTOP  4

; PUSH: FRMEVL's FRM.STACK: round FAC, push it (sign kept apart)
op_push:
        jsr     round_fac
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

; round_fac: ROUND.FAC ($EB72) - here, unless the rounding byte carries
; into the mantissa: then fp816's (the increment, ?OVERFLOW).  X, Y kept.
round_fac:
        lda     FAC
        beq     @r
        lda     FAC_EXT
        bmi     @carry
        asl     FAC_EXT
@r:     rts
@carry: phy
        FPCHK   fp_round
        ply
        rts

; store_fac_y: the variable at Y (this bank) = FAC, rounded
; (STORE.FAC.AT.YX.ROUNDED: the sign packed into the mantissa, FAC_EXT = 0)
store_fac_y:
        jsr     round_fac
        lda     FACSIGN
        ora     #$7F
        and     FAC+1
        xba                     ; B = the packed first mantissa byte
        lda     FAC
        rep     #$20
        .a16
        sta     a:$0000,y       ; exponent, sign + mantissa 1
        lda     FAC+2
        sta     a:$0002,y
        sep     #$20
        .a8
        lda     FAC+4
        sta     a:$0004,y
        stz     FAC_EXT
        rts

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

; (each binary operation's A entry: its operand is in ARG already - ARGV,
; ARGK, ARGKI put it there, fused from PUSH, a load and the pop)
op_add: jsr     poparg
op_adda:
        FPCHK   fp_addt
        NEXTOP  2
op_sub: jsr     poparg
op_suba:
        FPCHK   fp_subt
        NEXTOP  2
op_mul: jsr     poparg
op_mula:
        FPCHK   fp_multt
        NEXTOP  2
op_div: jsr     poparg
op_diva:
        FPCHK   fp_divt
        NEXTOP  2
op_pow: jsr     poparg
op_powa:
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
op_anda:
        ldy     #$0000
        lda     ARG
        beq     sngflt
        lda     FAC
        beq     sngflt
        iny
        bra     sngflt
op_or:  jsr     poparg
op_ora:
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
op_rela:
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
        ldy     a:$0002,x       ; the line's entry in the line table
        lda     a:$0001,y       ; (0: no code yet - code is above $3000)
        beq     @lazy
        ldx     a:$0000,y
        jmp     ($0000,x)
@lazy:  jsr     lazy
        jmp     ($0000,x)

; lazy: Y = a line-table entry whose line has no code yet: compile it (and
; a run of lines after it: the shell's request 7, the compiler's
; as816_more), X = its code.  ?OUT OF MEMORY if it does not fit (X, the
; operation's IP, says where).
lazy:   sty     VM_LAZY
        phy
        lda     #7
        sta     VM_CBREQ
        jsr     shell_call      ; (X kept)
        ply
        lda     VM_LAZY
        ora     VM_LAZY+1
        beq     @oom
        ldx     a:$0000,y
        rts
@oom:   lda     #ERR_OUT_OF_MEMORY
        jmp     vm_error

; IFNOT e: IF's test - FAC's exponent 0 means false: go to entry e's code
; (the next line's)
op_ifnot:
        lda     FAC
        bne     @true
        ldy     a:$0002,x
        lda     a:$0001,y
        beq     @lazy
        ldx     a:$0000,y
        jmp     ($0000,x)
@lazy:  jsr     lazy
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
        ldy     a:$0002,x       ; the line's entry
        lda     a:$0001,y
        beq     @lazy
        ldx     a:$0000,y
        jmp     ($0000,x)
@lazy:  jsr     lazy
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
;  +8 limit (5, packed)  +13 body IP  +15 CURLIN  +17 flags  +18 step and
;  +20 limit as integers  +22 the variable's bytes as NEXT stored them
;  last, +27 their value  +29 mark $81

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
        lda     #$00
        sta     a:F_FLAGS,y
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
        jsr     round_fac
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
        stx     VM_IP           ; a whole limit: as an integer too
        rep     #$21
        .a16
        tya
        adc     #$0008
        sta     FP_PTR
        sep     #$20
        .a8
        phk
        pla
        sta     FP_PTR+2
        rep     #$20
        .a16
        phy
        jsr     fp_packed_int
        ply
        bcs     @no
        sta     a:F_LIMIT,y
        sep     #$20
        .a8
        lda     a:F_FLAGS,y
        ora     #$01
        sta     a:F_FLAGS,y
        bra     @done
        .a16
@no:    sep     #$20
        .a8
@done:  ldx     VM_IP
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
        jsr     round_fac
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
        lda     FAC+3           ; a whole step: as an integer too (as NEXT
        ora     FAC+4           ; loads it: FAC+1 with bit 7, the sign apart)
        bne     @done
        stx     VM_IP
        rep     #$20
        .a16
        lda     FAC
        and     #$00FF
        tax
        lda     FAC+1
        xba
        ora     #$8000
        phy
        jsr     fp_whole
        ply
        bcs     @no
        sta     VM_T1
        sep     #$20
        .a8
        lda     a:$0002,y
        asl     a               ; C = the sign
        rep     #$20
        .a16
        lda     VM_T1
        bcc     @pos
        eor     #$FFFF
        inc     a
@pos:   sta     a:F_STEP,y
        sep     #$20
        .a8
        lda     a:F_FLAGS,y
        ora     #$02
        sta     a:F_FLAGS,y
        bra     @ip
        .a16
@no:    sep     #$20
        .a8
@ip:    ldx     VM_IP
@done:  NEXTOP  2

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
        ldy     VM_CSP          ; the usual case first: the loop's own FOR
        cpy     VM_CBASE_PTR    ; frame on top, its numbers cached
        beq     @find
        dey
        lda     a:$0000,y
        cmp     #FRAME_FOR
        bne     @find
        rep     #$20
        .a16
        tya
        sec
        sbc     #FOR_SIZE-1
        tay
        lda     VM_T1           ; (NEXT v: v's frame; NEXT: any)
        beq     @top
        cmp     a:$0000,y
        bne     @find16
@top:   sep     #$20
        .a8
        sty     VM_T0
        lda     a:F_FLAGS,y
        cmp     #$07
        bne     @found2
        jmp     next_cached
        .a16
@find16:
        sep     #$20
        .a8
@find:  jsr     find_for
        bne     @found
        lda     #ERR_NEXT_WITHOUT_FOR
        jmp     vm_error
@found: sty     VM_T0           ; the frame's base
@found2:
next_common_found2:
        lda     a:F_FLAGS,y
        and     #$03
        cmp     #$03
        beq     @int
        jmp     @float
; the step and the limit are whole numbers: fp816's integer NEXT (fp_next's
; next_int, checked against the ROM), the variable's last value cached
@int:   rep     #$20
        .a16
        lda     a:$0000,y       ; FP_PTR = the variable (this bank)
        sta     FP_PTR
        tax
        sep     #$20
        .a8
        phk
        pla
        sta     FP_PTR+2
        lda     a:F_FLAGS,y
        and     #$04
        rep     #$20
        .a16
        beq     @decode
        lda     a:$0000,x       ; still the bytes NEXT stored?
        cmp     a:F_CACHE,y
        bne     @decode
        lda     a:$0002,x
        cmp     a:F_CACHE+2,y
        bne     @decode
        lda     a:$0003,x
        cmp     a:F_CACHE+3,y
        bne     @decode
        lda     a:F_CVAL,y
        bra     @add
@decode:
        jsr     fp_packed_int   ; (X, Y lost)
        bcs     @float16
        ldy     VM_T0
@add:   clc
        adc     a:F_STEP,y
        bvs     @float16        ; beyond 16 bits
        beq     @float16        ; zero: the ROM's own way
        sta     a:F_CVAL,y
        sec                     ; FCOMP with the limit: 0 =, 1 above, $FF below
        sbc     a:F_LIMIT,y
        beq     @eq
        bvc     @nv
        eor     #$8000
@nv:    sep     #$20
        .a8
        bmi     @lt
        lda     #$01
        bra     @res
@lt:    lda     #$FF
        bra     @res
        .a16
@eq:    sep     #$20
        .a8
        lda     #$00
@res:   sta     VM_T1
        rep     #$20
        .a16
        lda     a:F_CVAL,y
        jsr     fp_int_packed   ; the variable and the cache = the sum (Y lost)
        ldy     VM_T0
        ldx     FP_PTR
        lda     NX_OUT
        sta     a:$0000,x
        sta     a:F_CACHE,y
        lda     NX_OUT+2
        sta     a:$0002,x
        sta     a:F_CACHE+2,y
        lda     NX_OUT+3
        sta     a:$0003,x
        sta     a:F_CACHE+3,y
        sep     #$20
        .a8
        lda     a:F_FLAGS,y
        ora     #$04
        sta     a:F_FLAGS,y
        lda     VM_T1
        bra     @decide
        .a16
@float16:
        sep     #$20
        .a8
; otherwise fp_next: FAC = the step, FADD the variable, store it, FCOMP
@float: ldx     VM_IP
        ldy     VM_T0
        rep     #$20            ; FAC = the step (LOAD.FAC.FROM.YA of its
        .a16                    ; five bytes: FAC+1 with bit 7, FAC_EXT 0)
        lda     a:$0003,y
        sta     FAC
        lda     a:$0005,y
        sta     FAC+2
        lda     a:$0000,y       ; FP_PTR = the variable
        sta     FP_PTR
        tya                     ; FP_SER = the limit
        clc
        adc     #$0008
        sta     FP_SER
        sep     #$20
        .a8
        phk
        pla
        sta     FP_PTR+2
        sta     FP_SER+2
        lda     a:$0007,y
        sta     FAC+4
        lda     FAC+1
        ora     #$80
        sta     FAC+1
        stz     FAC_EXT
        lda     a:$0002,y       ; FACSIGN = the step's sign
        sta     FACSIGN
        FPCHK   fp_next
        ldy     VM_T0
        lda     FP_RES
@decide:
        sec                     ; the loop ends when FCOMP gave the step's sign
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

; NEXT's usual case (next_common): Y = VM_T0 = the frame, its step and
; limit whole, the variable as NEXT stored it last - unless the body changed
; it (then the general paths, with the frame found)
next_cached:
        rep     #$20
        .a16
        ldx     a:$0000,y       ; the variable
        lda     a:$0000,x
        cmp     a:F_CACHE,y
        bne     @miss
        lda     a:$0002,x
        cmp     a:F_CACHE+2,y
        bne     @miss
        lda     a:$0003,x
        cmp     a:F_CACHE+3,y
        bne     @miss
        lda     a:F_CVAL,y
        clc
        adc     a:F_STEP,y
        bvs     @miss           ; beyond 16 bits, or zero: the general paths
        bne     @hit
@miss:  sep     #$20
        .a8
        ldy     VM_T0
        jmp     next_common_found2
        .a16
@hit:   sta     a:F_CVAL,y
        jsr     fp_int_packed   ; NX_OUT = the sum packed (X kept, Y lost)
        ldy     VM_T0
        lda     NX_OUT          ; the variable and the cache
        sta     a:$0000,x
        sta     a:F_CACHE,y
        lda     NX_OUT+2
        sta     a:$0002,x
        sta     a:F_CACHE+2,y
        lda     NX_OUT+3
        sta     a:$0003,x
        sta     a:F_CACHE+3,y
        lda     a:F_CVAL,y      ; FCOMP with the limit against the step's sign:
        sec                     ; done when above with a positive step, below
        sbc     a:F_LIMIT,y     ; with a negative one, equal with STEP 0
        beq     @eq
        bvc     @nv
        eor     #$8000
@nv:    bmi     @below
        lda     a:F_STEP,y      ; above
        beq     @again
        bpl     @done
        bra     @again
@below: lda     a:F_STEP,y
        bmi     @done
        bra     @again
@eq:    lda     a:F_STEP,y
        beq     @done
@again: lda     a:$000F,y       ; the body again, the FOR's line
        sta     CURLIN
        lda     a:$000D,y
        tax
        sep     #$20
        .a8
        jmp     ($0000,x)
        .a16
@done:  sep     #$20            ; drop the frame, on after NEXT
        .a8
        sty     VM_CSP
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
@pos:   FPCHK   fp_ayint16
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
        cmp     #$0001
        bne     @many
        ldy     VM_FSP          ; one subscript: popped, checked, times the
        dey                     ; element size (5, 2, 4) by shifts
        dey
        sty     VM_FSP
        lda     a:$0000,y
        ldy     VM_T1
        cmp     a:$0004,y
        beq     @in1
        bcc     @in1
        jmp     @bad
@in1:   sta     VM_T0
        lda     a:$0003,y
        and     #$00FF
        cmp     #$0004
        beq     @x4
        bcs     @x5
        lda     VM_T0           ; 2
        asl     a
        bra     @add
@x4:    lda     VM_T0
        asl     a
        asl     a
        bra     @add
@x5:    lda     VM_T0
        asl     a
        asl     a
        clc
        adc     VM_T0
@add:   clc
        adc     a:$0000,y
        tay
        sep     #$20
        .a8
        rts
        .a16
@many:  asl     a
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
        lda     a:$0008,y       ; the array heap's free pointer: it grows down,
        sec                     ; from the stacks to its floor (the code's
        sbc     VM_T0           ; end, which a lazy compile raises)
        bcc     @oom
        cmp     a:$000A,y
        bcc     @oom
        sta     a:$0008,y
        tax
        ldy     VM_T1
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
        jsr     store_fac_y
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
        pei     (VM_T0)         ; the callers' scratch survives a collection
        pei     (VM_T1)
        pei     (VM_T2)
        pei     (VM_T3)
        pei     (VM_T4)
        pei     (VM_T5)
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
        sta     VM_T2           ; how many (kept across elem_count: mul16 uses VM_T2)
@a:     lda     VM_T2
        beq     @done
        dec     VM_T2
        ldy     VM_T4
        lda     a:$0000,y       ; the descriptor
        tay
        lda     a:$0000,y       ; its storage
        beq     @anext
        sta     VM_T1           ; the element pointer
        pei     (VM_T2)
        jsr     elem_count      ; A = how many elements
        sta     VM_T3
        pla
        sta     VM_T2
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
        pla
        sta     VM_T5
        pla
        sta     VM_T4
        pla
        sta     VM_T3
        pla
        sta     VM_T2
        pla
        sta     VM_T1
        pla
        sta     VM_T0
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
        tya
        clc
        adc     #$0002
        sta     VM_SDESC        ; (the bounds: +4, +6, ...)
@m:     lda     VM_SDESC
        clc
        adc     #$0002
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

; SLITI n text: SACC = the literal here (its length n, then its text)
op_sliti:
        lda     a:$0002,x
        sta     SACC
        rep     #$21
        .a16
        txa
        adc     #$0003
        sta     SACC+1          ; the text's address
        lda     SACC
        and     #$00FF
        adc     SACC+1          ; past it
        tax
        sep     #$20
        .a8
        phk
        pla
        sta     SACC+3
        jmp     ($0000,x)

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
        jsr     round_fac
        FPCHK   fp_ayint16
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
        jsr     round_fac
        FPCHK   fp_ayint16
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


; =========================================================================
; INPUT, READ, GET (PROCESS.INPUT.LIST $DBEB, one item at a time)
; =========================================================================

; CHRGET/CHRGOT over FP_TXT (the ROM's, as fp816 has them)
vchrget:
        inc     FP_TXT
        bne     vchrgot
        inc     FP_TXT+1
vchrgot:
        lda     [FP_TXT]
        cmp     #$3A
        bcs     @rts
        cmp     #$20
        beq     vchrget
        sec
        sbc     #$30
        sec
        sbc     #$D0
@rts:   rts

; INLIN ($D52C): GETLN with prompt $80; at most 239 characters; a 0 after;
; the high bits off.  The text pointer is left at $01FF.
inlin:  lda     VM_IOMODE       ; READ, EXEC: the line from the file (the shell)
        and     #$06
        beq     @kbd
        lda     #2
        sta     VM_CBREQ
        jsr     shell_call
        bra     @got
@kbd:   lda     #$80
        sta     f:$000033
        ldy     #$FD6A
        sty     VM_T4
        jsr     romcall
@got:   lda     RC_X
        cmp     #$EF
        bcc     @l
        lda     #$EF
@l:     rep     #$20
        .a16
        and     #$00FF
        tay
        sep     #$20
        .a8
        lda     #$00
        tyx
        sta     f:$000200,x
@strip: cpx     #$0000
        beq     @done
        lda     f:$0001FF,x
        and     #$7F
        sta     f:$0001FF,x
        dex
        bra     @strip
@done:  ldx     VM_IP
        ldy     #$01FF
        sty     FP_TXT
        stz     FP_TXT+2
        rts

; INPSTART p: INPUT's start: the prompt (literal p) or "?", INLIN
op_inpstart:
        stx     VM_INSTMT
        stx     VM_IP
        jsr     io_quiet
        lda     a:$0002,x       ; 0: no prompt; else its length + 1, its text
        beq     @ques
        dec     a
        sta     VM_T0
        txy                     ; STRPRT the text
        iny
        iny
@p:     lda     VM_T0
        beq     @line
        iny
        lda     a:$0000,y
        jsr     outdo
        dec     VM_T0
        bra     @p
@ques:  lda     #'?'            ; OUTQUES
        jsr     outdo
@line:  lda     #','
        sta     f:$0001FF
        jsr     inlin
        stz     VM_INMODE
        jsr     save_inptr
        ldx     VM_IP           ; past the prompt: 3 + its length
        lda     a:$0002,x
        beq     @n
        dec     a
@n:     rep     #$21
        .a16
        and     #$00FF
        adc     #$0003
        adc     VM_IP
        tax
        sep     #$20
        .a8
        jmp     ($0000,x)

; READSTART: from DATPTR in the program's text
op_readstart:
        lda     #$98
        sta     VM_INMODE
        ldy     DATPTR
        sty     INPTR
        lda     VM_TXTBANK
        sta     INPTR+2
        NEXTOP  2

; GETSTART: INPTR = $0201, a 0 there: the first item reads a key
op_getstart:
        jsr     io_quiet
        lda     #$40
        sta     VM_INMODE
        lda     #$00
        sta     f:$000201
        ldy     #$0201
        sty     INPTR
        stz     INPTR+2
        NEXTOP  2

save_inptr:
        ldy     FP_TXT
        sty     INPTR
        lda     FP_TXT+2
        sta     INPTR+2
        rts

; ITEM t a: one variable: t = type (0 real, 1 integer, 2 string) + $80 for
; an array element (a = its descriptor, the subscripts pushed), else a =
; the variable
op_item:
        stx     VM_IP
        lda     a:$0002,x
        and     #$03
        sta     VM_ITYPE
        ldy     a:$0003,x
        lda     a:$0002,x
        bpl     @var
        jsr     elem_addr       ; the element first, as PTRGET does
        ldx     VM_IP
@var:   sty     VM_ITGT
        ldy     INPTR           ; TXTPTR = the input text
        sty     FP_TXT
        lda     INPTR+2
        sta     FP_TXT+2
        jsr     vchrgot
        beq     @end
        jmp     instart
@end:   lda     VM_INMODE       ; the text is used up
        bmi     @findata
        and     #$40
        beq     @more_input
        ; GET: a key into $0200, the text pointer before it
        lda     VM_IOMODE       ; READ, EXEC: the file's next character
        and     #$06
        beq     @key
        lda     #3
        sta     VM_CBREQ
        jsr     shell_call
        bra     @gotkey
@key:   ldy     #$FD0C
        sty     VM_T4
        jsr     romcall
@gotkey:
        lda     RC_A
        and     #$7F
        sta     f:$000200
        ldy     #$01FF
        sty     FP_TXT
        stz     FP_TXT+2
        jmp     instart
@more_input:                    ; INPUT: "??" and another line
        lda     #'?'
        jsr     outdo
        lda     #'?'
        jsr     outdo
        jsr     inlin
        jmp     instart
@findata:                       ; READ: the next DATA statement in the text
        jmp     findata         ; (FINDATA), FP_TXT at a ":" or a line's 0

; FINDATA ($D995): from FP_TXT, at a ":" or a line's 0, to the next
; statement that is DATA (DATAN between: ":" or 0 ends a statement, quotes
; respected); DATLIN its line; FP_TXT at the DATA token (its text - 1)
findata:
@fd:    lda     [FP_TXT]
        bne     @stmt           ; ":": the next statement follows it
        rep     #$20
        .a16
        ldy     #$0001          ; a line's 0: the next line's link, number,
        lda     [FP_TXT],y      ; text
        beq     @nodata
        ldy     #$0003
        lda     [FP_TXT],y
        sta     DATLIN
        lda     FP_TXT
        clc
        adc     #$0004          ; (the number's high byte: the text - 1)
        sta     FP_TXT
        sep     #$20
        .a8
@stmt:  ldy     #$0001
        lda     [FP_TXT],y
        cmp     #$83            ; DATA
        beq     @data
        stz     VM_T0           ; not DATA: to its end (in quotes: VM_T0)
@scan:  iny
        lda     [FP_TXT],y
        beq     @at
        cmp     #'"'
        bne     @nq
        lda     VM_T0
        eor     #$01
        sta     VM_T0
        bra     @scan
@nq:    cmp     #':'
        bne     @scan
        lda     VM_T0
        bne     @scan
@at:    rep     #$21
        .a16
        tya
        adc     FP_TXT
        sta     FP_TXT
        sep     #$20
        .a8
        bra     @fd
@data:  rep     #$20
        .a16
        inc     FP_TXT
        sep     #$20
        .a8
        jmp     instart
        .a16
@nodata:
        sep     #$20
        .a8
        lda     #$2A            ; ?OUT OF DATA (in the READ's line)
        jmp     vm_error

; INSTART ($DC2B): the item's first character
instart:
        jsr     vchrget
        lda     VM_ITYPE
        cmp     #$02
        beq     @string
        jmp     in_number
@string:
        lda     VM_INMODE
        and     #$40
        beq     @notget
        lda     #$00            ; GET: the whole buffer from $0200
        sta     FP_TXT          ; (the ROM's INX/STX TXTPTR)
        sta     VM_T0           ; CHARAC = ENDCHR = 0
        sta     VM_T0+1
        clc
        bra     @scan
@notget:
        lda     [FP_TXT]
        cmp     #'"'
        beq     @quoted
        lda     #':'            ; unquoted: to ":" or ","
        sta     VM_T0
        lda     #','
        sta     VM_T0+1
        clc
        bra     @scan
@quoted:
        sta     VM_T0
        sta     VM_T0+1
        sec                     ; the text starts after the quote
@scan:  rep     #$20
        .a16
        lda     FP_TXT          ; SPTR2 = the text's start
        adc     #$0000
        sta     SPTR2
        sep     #$20
        .a8
        lda     FP_TXT+2
        sta     SPTR2+2
        ldy     #$0000          ; STRLT2's scan
@s:     lda     [SPTR2],y
        beq     @zero
        cmp     VM_T0
        beq     @term
        cmp     VM_T0+1
        beq     @term
        iny
        bra     @s
@term:  cmp     #'"'            ; a closing quote is passed over (C = 1)
        beq     @ended
@zero:  clc                     ; (STRLT2's LE408: the 0 or another terminator)
@ended: lda     #$00            ; C: past a closing quote
        rol     a
        sta     VM_T5
        stz     VM_T5+1
        tya
        sta     VM_T3           ; the length
        stz     VM_T3+1
        rep     #$21            ; the text pointer after it (POINT)
        .a16
        tya
        adc     SPTR2
        adc     VM_T5
        sta     FP_TXT
        sep     #$20
        .a8
        ; the string: text in the input buffer (bank 0) is copied into the
        ; heap; text in the pool (this bank) is used in place, as AppleSoft
        ; points into its program
        lda     SPTR2+2
        beq     @copy
        lda     VM_T3
        sta     SACC
        ldy     SPTR2
        sty     SACC+1
        lda     SPTR2+2
        sta     SACC+3
        bra     @put
@copy:  rep     #$20
        .a16
        lda     SPTR2           ; (salloc may collect: the text is not in the heap)
        pha
        lda     VM_T3
        beq     @empty
        jsr     salloc
@empty: pla
        sta     SPTR2
        sep     #$20
        .a8
        stz     SPTR2+2
        stz     VM_T4
        stz     VM_T4+1
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
@put:   ldy     VM_ITGT         ; PUTSTR
        rep     #$20
        .a16
        lda     SACC
        sta     a:$0000,y
        lda     SACC+2
        sta     a:$0002,y
        sep     #$20
        .a8
        jmp     in_more

; a number: FIN where the text is, then LET2 (INPUT.DATA $DC69)
in_number:
        lda     f:$000200       ; INPUT: an empty line is ?REENTER (INPFIN)
        bne     @data
        lda     VM_INMODE
        bne     @data
        jmp     reenter
@data:  rep     #$20            ; fp_fin: CHRGET first, so the text - 1
        .a16
        lda     FP_TXT
        dec     a
        sta     FP_TXT
        sep     #$20
        .a8
        FP      fp_fin
        lda     FP_ERR
        beq     @ok
        jmp     vm_error
@ok:    lda     VM_ITYPE
        bne     @int
        ldy     VM_ITGT         ; SETFOR: stored rounded
        jsr     store_fac_y
        bra     in_more
@int:   jsr     round_fac
        FPCHK   fp_ayint16
        ldy     VM_ITGT
        lda     FAC+3
        sta     a:$0000,y
        lda     FAC+4
        sta     a:$0001,y

; INPUT.MORE ($DC72): the item must end the text or be followed by ","
in_more:
        jsr     vchrgot
        beq     @ok
        cmp     #','
        beq     @ok
        jmp     inputerr
@ok:    jsr     save_inptr
        ldx     VM_IP
        NEXTOP  5

; INPUTERR ($DB71): INPUT - ?REENTER; READ - ?SYNTAX ERROR in the DATA's
; line; GET - ?SYNTAX ERROR, no line
inputerr:
        lda     VM_INMODE
        beq     reenter
        bmi     @read
        ldy     #$FF40
        sty     CURLIN
        bra     @syn
@read:  ldy     DATLIN
        sty     CURLIN
@syn:   lda     #$10
        jmp     vm_error

; RESPERR ($DB87): "?REENTER" and the INPUT statement from its start
; (ONERR: later)
reenter:
        ldy     #$0000
@m:     lda     msg_reenter,y
        beq     @go
        jsr     outdo
        iny
        bra     @m
@go:    ldx     VM_INSTMT
        jmp     ($0000,x)
msg_reenter:
        .byte   "?REENTER", $0D, 0
msg_extra:
        .byte   "?EXTRA IGNORED", $0D, 0

; INPEND: INPDONE ($DCC6): READ keeps its place; INPUT reports what was left
op_inpend:
        lda     VM_INMODE
        bpl     @inp
        ldy     INPTR           ; READ: SETDA
        sty     DATPTR
        NEXTOP  2
@inp:   ldy     INPTR
        sty     FP_TXT
        lda     INPTR+2
        sta     FP_TXT+2
        lda     [FP_TXT]
        beq     @done
        lda     VM_INMODE       ; (GET leaves nothing behind)
        bne     @done
        ldy     #$0000
@m:     lda     msg_extra,y
        beq     @done
        jsr     outdo
        iny
        bra     @m
@done:  stz     VM_IOQUIET
        NEXTOP  2

; io_quiet: an INPUT/GET reading a file shows nothing till its end (INPEND)
io_quiet:
        lda     VM_IOMODE
        and     #$02
        sta     VM_IOQUIET
        rts

; RESTORE: back to the pool's start
op_restore:
        ldy     VM_PROG
        lda     a:$0016,y
        sta     DATPTR
        lda     a:$0017,y
        sta     DATPTR+1
        NEXTOP  2


; =========================================================================
; More statements
; =========================================================================

; ON k f a1..ak: GETBYT; 1..k picks a(n), else on (ONGOTO $D9EC); f = 0
; GOTO, 1 GOSUB; a = 0: the line does not exist
op_on:  stx     VM_IP
        jsr     conint
        sta     VM_T0
        ldx     VM_IP
        lda     a:$0002,x       ; k
        sta     VM_T1
        stz     VM_T1+1
        lda     VM_T0
        beq     @next
        cmp     VM_T1
        beq     @pick
        bcs     @next
@pick:  rep     #$20
        .a16
        and     #$00FF
        asl     a
        clc
        adc     VM_IP
        tay                     ; Y + 2 = a(n): the table starts at +4
        lda     a:$0002,y       ; (a line-table entry)
        beq     @undef
        tay
        lda     a:$0000,y
        bne     @have
        sep     #$20            ; no code yet: compiled now
        .a8
        ldx     VM_IP
        jsr     lazy
        rep     #$20
        .a16
        txa
@have:  sta     VM_T2           ; the target
        lda     VM_T1           ; this operation's length: 4 + 2k
        asl     a
        clc
        adc     #$0004
        adc     VM_IP
        sta     VM_T3           ; the return point
        sep     #$20
        .a8
        ldx     VM_IP
        lda     a:$0003,x
        beq     @goto
        ldy     VM_CSP          ; GOSUB: the frame [return, CURLIN, $B0]
        rep     #$20
        .a16
        tya
        clc
        adc     #GOSUB_SIZE
        cmp     VM_CLIM
        bcs     @full
        sta     VM_CSP
        lda     VM_T3
        sta     a:$0000,y
        lda     CURLIN
        sta     a:$0002,y
        sep     #$20
        .a8
        lda     #FRAME_GOSUB
        sta     a:$0004,y
@goto:  ldx     VM_T2
        jmp     ($0000,x)
        .a16
@full:  sep     #$20
        .a8
        lda     #ERR_OUT_OF_MEMORY
        jmp     vm_error
        .a16
@undef: sep     #$20
        .a8
        lda     #ERR_UNDEF_STATEMENT
        jmp     vm_error
@next:  rep     #$20            ; past the table
        .a16
        lda     VM_T1
        asl     a
        clc
        adc     #$0004
        adc     VM_IP
        tax
        sep     #$20
        .a8
        jmp     ($0000,x)

; POP: the GOSUB frame (and FOR frames above it) dropped, no return
op_pop: stx     VM_IP
@scan:  jsr     frame_top_mark
        cmp     #FRAME_GOSUB
        beq     @found
        cmp     #FRAME_FOR
        bne     @none
        jsr     drop_frame
        bra     @scan
@found: jsr     drop_frame
        ldx     VM_IP
        NEXTOP  2
@none:  lda     #ERR_RETURN_WITHOUT_GOSUB
        jmp     vm_error

; DEFFN e v s: function entry e (in the table) = this body (at +8),
; parameter v; then on at s (past the body)
op_deffn:
        ldy     a:$0002,x       ; the entry
        rep     #$20
        .a16
        txa
        clc
        adc     #$0008
        sta     a:$0000,y
        lda     a:$0004,x
        sta     a:$0002,y
        lda     a:$0006,x
        tax
        sep     #$20
        .a8
        jmp     ($0000,x)

; FNCALL e: FUNCT ($E354) - ?UNDEF'D FUNCTION if not DEFined; the
; parameter's value kept, the argument (FAC) stored in it rounded, the body
; run; FNRET puts the parameter back
op_fncall:
        ldy     a:$0002,x
        rep     #$20
        .a16
        lda     a:$0000,y
        bne     @def
        sep     #$20
        .a8
        lda     #$E0
        jmp     vm_error
        .a16
@def:   sta     VM_T2           ; the body
        lda     a:$0002,y
        sta     VM_T1           ; the parameter
        ldy     VM_FSP          ; frame: saved value (5), parameter, return
        tya
        clc
        adc     #$000C
        sta     VM_FSP
        phy
        ldy     VM_T1
        lda     a:$0000,y
        ply
        sta     a:$0000,y
        phy
        ldy     VM_T1
        lda     a:$0002,y
        ply
        sta     a:$0002,y
        phy
        ldy     VM_T1
        lda     a:$0004,y
        ply
        sta     a:$0004,y
        lda     VM_T1
        sta     a:$0006,y
        txa
        clc
        adc     #$0004
        sta     a:$0008,y
        sep     #$20
        .a8
        ldy     VM_T1
        jsr     store_fac_y
        ldx     VM_T2
        jmp     ($0000,x)

op_fnret:
        rep     #$20
        .a16
        lda     VM_FSP
        sec
        sbc     #$000C
        sta     VM_FSP
        tay
        lda     a:$0006,y
        sta     VM_T1
        lda     a:$0000,y
        phy
        ldy     VM_T1
        sta     a:$0000,y
        ply
        lda     a:$0002,y
        phy
        ldy     VM_T1
        sta     a:$0002,y
        ply
        lda     a:$0004,y
        phy
        ldy     VM_T1
        sep     #$20
        .a8
        sta     a:$0004,y       ; (5 bytes: the low one of this word)
        ply
        rep     #$20
        .a16
        lda     a:$0008,y
        tax
        sep     #$20
        .a8
        jmp     ($0000,x)

; POS: FAC = CH (SNGFLT)
op_pos: lda     f:$000024
sngflt_a:
        sta     FP_INT
        stz     FP_INT+1
        FP      fp_givayf
        NEXTOP  2

; PEEK: GETADR, then the byte in bank 0
op_peek:
        FPCHK   fp_getadr
        ldy     LINNUM
        sty     SPTR
        stz     SPTR+2
        lda     [SPTR]
        bra     sngflt_a

; ADR: GETADR, the address pushed (POKE, WAIT, CALL take it)
op_adr: FPCHK   fp_getadr
        ldy     VM_FSP
        rep     #$20
        .a16
        lda     LINNUM
        sta     a:$0000,y
        sep     #$20
        .a8
        iny
        iny
        sty     VM_FSP
        NEXTOP  2

; pop a pushed address or byte into VM_T0
pop2:   ldy     VM_FSP
        dey
        dey
        sty     VM_FSP
        rep     #$20
        .a16
        lda     a:$0000,y
        sta     VM_T0
        sep     #$20
        .a8
        rts

; BYTEPUSH r: GETBYT, pushed; r = 1: ?ILLEGAL QUANTITY unless < 80 (the
; lo-res coordinate checks)
op_bytepush:
        stx     VM_IP
        jsr     conint
        ldx     VM_IP
        pha
        lda     a:$0002,x
        beq     @ok
        pla
        pha
        cmp     #$50
        bcc     @ok
        brl     iq
@ok:    pla
        ldy     VM_FSP
        sta     a:$0000,y
        lda     #$00
        sta     a:$0001,y
        iny
        iny
        sty     VM_FSP
        NEXTOP  3

; POKE: the byte (FAC) to the pushed address, in bank 0
op_poke:
        stx     VM_IP
        jsr     conint
        pha
        jsr     pop2
        ldy     VM_T0
        sty     SPTR
        stz     SPTR+2
        pla
        sta     [SPTR]
        ldx     VM_IP
        NEXTOP  2

; WAIT: address, mask, xor pushed (xor 0 if absent): until (byte EOR xor)
; AND mask is not 0
op_wait:
        jsr     pop2
        lda     VM_T0
        sta     VM_T5           ; xor
        jsr     pop2
        lda     VM_T0
        sta     VM_T5+1         ; mask
        jsr     pop2
        ldy     VM_T0
        sty     SPTR
        stz     SPTR+2
@w:     lda     [SPTR]
        eor     VM_T5
        and     VM_T5+1
        beq     @w
        NEXTOP  2

; CALL: the machine code at the address, in emulation mode, bank 0 - except
; AppleSoft's own stack fixes, which have nothing to fix here
op_call:
        stx     VM_IP
        FPCHK   fp_getadr
        ldy     LINNUM
        cpy     #$F328          ; CALL -3288: the ONERR stack fix
        beq     @done
        cpy     #$D683          ; CALL 54915 (STKINI): the stack emptied
        bne     @go
        ldy     VM_CBASE_PTR
        sty     VM_CSP
        bra     @done
@go:    sty     VM_T4
        lda     LINNUM+1        ; A = the address's high byte, Y = low (GETADR)
        ldy     LINNUM
        jsr     romcall
@done:  ldx     VM_IP
        NEXTOP  2

; PDL(n): CONINT, PREAD ($FB1E, X = n), SNGFLT of its Y
op_pdl: stx     VM_IP
        jsr     conint
        rep     #$20
        .a16
        and     #$00FF
        tax
        sep     #$20
        .a8
        ldy     #$FB1E
        sty     VM_T4
        jsr     romcall
        lda     RC_Y
        jmp     sngflt_a

; USR(x): JSR $000A, FAC in page zero: ours ($9D-$AC: FAC, ARG, the
; rounding byte) there and back around the call
op_usr: stx     VM_IP
        ldx     #$000F
@out:   lda     FAC,x
        sta     f:$00009D,x
        dex
        bpl     @out
        ldy     #$000A
        sty     VM_T4
        jsr     romcall
        ldx     #$000F
@in:    lda     f:$00009D,x
        sta     FAC,x
        dex
        bpl     @in
        ldx     VM_IP
        NEXTOP  2

; AMPER n, token, text: & - and the GS's SHLOAD, RECALL, STORE, SAVE, LOAD,
; which its AppleSoft sends the same way - JMPs to $3F5 with A and the
; flags CHRGOT's and TXTPTR after the token.  The statement's text (the
; token first, then a 0) goes to the input buffer, after a stub
; (JSR CHRGOT, JMP $3F5); CHRGET from the ROM into page zero.  After it,
; as NEWSTT: the statement's end (TXTPTR at the 0), or SYNTAX ERROR.
AMP_BUF  = $0200
op_amper:
        stx     VM_IP
        ldx     #$0017          ; CHRGET ($F10B, as COLDST copies it)
@cg:    lda     f:$FFF10B,x
        sta     f:$0000B1,x
        dex
        bpl     @cg
        ldx     #$0005
@st:    lda     amp_stub,x
        sta     f:AMP_BUF,x
        dex
        bpl     @st
        ldy     VM_IP
        lda     a:$0002,y       ; the text's length
        sta     VM_T0
        stz     VM_T0+1
        ldx     #$0000
@t:     cpx     VM_T0
        beq     @t0
        lda     a:$0003,y
        sta     f:AMP_BUF+6,x
        iny
        inx
        bra     @t
@t0:    lda     #$00
        sta     f:AMP_BUF+6,x
        rep     #$20
        .a16
        lda     #AMP_BUF+7      ; TXTPTR: after the token
        sta     f:$0000B8
        sep     #$20
        .a8
        ldy     #AMP_BUF
        sty     VM_T4
        jsr     romcall
        rep     #$20
        .a16
        lda     f:$0000B8
        sta     SPTR
        sep     #$20
        .a8
        stz     SPTR+2
        lda     [SPTR]
        beq     @next
        lda     #$10            ; SYNTAX
        jmp     vm_error
@next:  rep     #$20
        .a16
        lda     VM_T0
        clc
        adc     VM_IP
        adc     #$0003
        tax
        sep     #$20
        .a8
        jmp     ($0000,x)
amp_stub:
        .byte   $20, $B7, $00   ; JSR CHRGOT
        .byte   $4C, $F5, $03   ; JMP $3F5

; LIST a b, NEW, DEL a b: the program's text is the shell's (VM_CBREQ 4,
; 5, 6, the line numbers in VM_T0/VM_T1).  LIST goes on; NEW and DEL
; end at the prompt (the shell leaves), DEL goes on if no line is in range.
op_list:
        lda     #$04
        bra     textop
op_del: lda     #$06
textop: pha
        jsr     io_flush        ; what WRITE gathered goes first
        rep     #$20
        .a16
        lda     a:$0002,x
        sta     VM_T0
        lda     a:$0004,x
        sta     VM_T1
        sep     #$20
        .a8
        pla
        sta     VM_CBREQ
        jsr     shell_call
        NEXTOP  6
op_new: lda     #$05
        sta     VM_CBREQ
        jsr     shell_call
        NEXTOP  2

; PR# n / IN# n: OUTPORT / INPORT
op_prn: ldy     #$FE95
        bra     port
op_inn: ldy     #$FE8B
port:   stx     VM_IP
        sty     VM_T5
        jsr     conint
        ldy     VM_T5
        sty     VM_T4
        jsr     romcall
        NEXTOP  2

; HTAB n (the GS's $F7E7): n-1 into CH, a carriage return for each full
; width (40, or WNDWDTH if wider)
op_htab:
        stx     VM_IP
        jsr     conint
        dec     a
        sta     VM_T0           ; X
@l:     lda     f:$000021
        cmp     #$28
        bcs     @w
        lda     #$28
@w:     sta     VM_T1           ; the width: max(40, WNDWDTH)
        lda     VM_T0
        sec
        sbc     VM_T1
        bcc     @set
        sta     VM_T0
        jsr     crdo
        bra     @l
@set:   lda     VM_T0
        sta     f:$000024
        ldx     VM_IP
        NEXTOP  2

; VTAB n: n-1 < 24 or ?ILLEGAL QUANTITY; TABV
op_vtab:
        stx     VM_IP
        jsr     conint
        dec     a
        cmp     #$18
        bcc     @ok
        brl     iq
@ok:    ldy     #$FB5B
        sty     VM_T4
        jsr     romcall
        NEXTOP  2

op_home:
        stx     VM_IP
        ldy     #$FC58
        sty     VM_T4
        jsr     romcall
        NEXTOP  2

; NORMAL / INVERSE / FLASH: INVFLG ($32) and FLASH.BIT
op_normal:
        lda     #$FF
        sta     f:$000032
        stz     VM_FLASH
        NEXTOP  2
op_inverse:
        lda     #$3F
        sta     f:$000032
        stz     VM_FLASH
        NEXTOP  2
op_flash:
        lda     #$7F
        sta     f:$000032
        lda     #$40
        sta     VM_FLASH
        NEXTOP  2

; SPEED= n: SPEEDZ = 256 - n
op_speed:
        stx     VM_IP
        jsr     conint
        eor     #$FF
        inc     a
        sta     VM_SPEED
        ldx     VM_IP
        NEXTOP  2

op_trace:
        lda     #$80
        sta     VM_TRACE
        NEXTOP  2
op_notrace:
        stz     VM_TRACE
        NEXTOP  2

; TEXT: $C054, SETTXT; GR: $C056 $C053, SETGR
op_text:
        stx     VM_IP
        lda     f:$00C054
        ldy     #$FB39
        sty     VM_T4
        jsr     romcall
        NEXTOP  2
op_gr:  stx     VM_IP
        lda     f:$00C056
        lda     f:$00C053
        ldy     #$FB40
        sty     VM_T4
        jsr     romcall
        NEXTOP  2

; COLOR= n: SETCOL
op_color:
        stx     VM_IP
        jsr     conint
        ldy     #$F864
        sty     VM_T4
        jsr     romcall
        NEXTOP  2

; PLOT x,y: x pushed (< 80), y (FAC) < 48; x < 40 (40 columns); PLOT
op_plot:
        stx     VM_IP
        jsr     conint
        cmp     #$30
        bcs     @iq
        pha
        jsr     pop2
        lda     VM_T0
        cmp     #$28
        bcs     @iq
        tay                     ; Y = x
        pla                     ; A = y
        phx
        ldx     #$F800
        stx     VM_T4
        plx
        jsr     romcall
        NEXTOP  2
@iq:    brl     iq

; HLIN a,b AT y: a (< 80) and b pushed; y (FAC) < 80; the ends in order,
; the right one < 40, y < 48; HLINE
op_hlin:
        stx     VM_IP
        jsr     conint
        cmp     #$50
        bcs     @iq
        sta     VM_T5           ; y
        jsr     pop2
        lda     VM_T0
        sta     VM_T1           ; b
        jsr     pop2
        lda     VM_T0           ; a
        cmp     VM_T1
        bcc     @ord
        ldy     VM_T1           ; swap: FIRST the smaller
        sta     VM_T1
        tya
@ord:   sta     f:$0000F0       ; FIRST
        lda     VM_T1
        sta     f:$00002C       ; H2
        sta     f:$00002D
        cmp     #$28
        bcs     @iq
        lda     VM_T5
        cmp     #$30
        bcs     @iq
        lda     f:$0000F0
        tay
        lda     VM_T5
        phx
        ldx     #$F819
        stx     VM_T4
        plx
        jsr     romcall
        NEXTOP  2
@iq:    brl     iq

; VLIN a,b AT x: a (< 80) and b (< 48) pushed; x (FAC) < 80; ordered; x
; < 40; VLINE
op_vlin:
        stx     VM_IP
        jsr     conint
        cmp     #$50
        bcs     @iq
        sta     VM_T5           ; x
        jsr     pop2
        lda     VM_T0
        cmp     #$30
        bcs     @iq
        sta     VM_T1           ; b
        jsr     pop2
        lda     VM_T0
        cmp     VM_T1
        bcc     @ord
        ldy     VM_T1
        sta     VM_T1
        tya
@ord:   sta     f:$0000F0       ; FIRST = the top
        lda     VM_T1
        sta     f:$00002D       ; V2 = the bottom
        sta     f:$00002C
        lda     VM_T5
        cmp     #$28
        bcs     @iq
        tay
        lda     f:$0000F0
        phx
        ldx     #$F828
        stx     VM_T4
        plx
        jsr     romcall
        NEXTOP  2
@iq:    brl     iq

; SCRN(x,y): x pushed, y (FAC); SCRN
op_scrn:
        stx     VM_IP
        jsr     conint
        cmp     #$30
        bcs     @iq
        pha
        jsr     pop2
        lda     VM_T0
        cmp     #$28
        bcs     @iq
        tay
        pla
        phx
        ldx     #$F871
        stx     VM_T4
        plx
        jsr     romcall
        lda     RC_A
        brl     sngflt_a
@iq:    brl     iq

; TAB(n): to column n-1 (LF7CB: target - CH spaces, none if behind)
op_tab: stx     VM_IP
        jsr     conint
        dec     a
        sec
        sbc     f:$000024
        bcc     spc_done
        bra     spaces
; SPC(n): n spaces
op_spc: stx     VM_IP
        jsr     conint
spaces: sta     VM_T0
@s:     lda     VM_T0
        beq     spc_done
        lda     #' '
        jsr     outdo
        dec     VM_T0
        bra     @s
spc_done:
        ldx     VM_IP
        NEXTOP  2

; STOP: "\rBREAK" and the bell, " IN n"
op_stop:
        inx
        inx
        jsr     cont_here
        jmp     break_msg

; ---- hi-res: AppleSoft's checks, the ROM's routines (bit for bit theirs) ----

; HGR, HGR2: the ROM's ($F3E2, $F3D8): switches, HGR.PAGE, the page cleared
op_hgr: stx     VM_IP
        ldy     #$F3E2
        bra     hgr_call
op_hgr2:
        stx     VM_IP
        ldy     #$F3D8
hgr_call:
        sty     VM_T4
        jsr     romcall
        NEXTOP  2

; HCOLOR= n (< 8): HGR.COLOR ($E4) from the ROM's COLORTBL ($F6F6)
op_hcolor:
        stx     VM_IP
        jsr     conint
        cmp     #$08
        bcs     @iq
        rep     #$20
        .a16
        and     #$00FF
        tax
        sep     #$20
        .a8
        lda     f:$FFF6F6,x
        sta     f:$0000E4
        ldx     VM_IP
        NEXTOP  2
@iq:    brl     iq

; HX: HFNS's x - GETADR, < 280 - pushed
op_hx:  stx     VM_IP
        FPCHK   fp_getadr
        lda     LINNUM+1
        beq     @ok
        cmp     #$01
        bne     @iq
        lda     LINNUM
        cmp     #$18
        bcs     @iq
@ok:    ldy     VM_FSP
        rep     #$20
        .a16
        lda     LINNUM
        sta     a:$0000,y
        sep     #$20
        .a8
        iny
        iny
        sty     VM_FSP
        NEXTOP  2
@iq:    brl     iq

; HPLOT x,y: y (FAC: GETBYT, < 192), x pushed (HX); HPLOT0 ($F457), with A
; = y, X/Y = x.  HPOSN ($F411) the same, the place only.  HPLOT TO x,y:
; HGLIN ($F53A), with A/X = x, Y = y.
op_hplot:
        ldy     #$F457
        bra     hplot_xy
op_hposn:
        ldy     #$F411
hplot_xy:
        sty     VM_T4
        jsr     hfns_y
        rep     #$20
        .a16
        lda     VM_T0
        and     #$00FF
        tax                     ; X = x low
        lda     VM_T0
        xba
        and     #$00FF
        tay                     ; Y = x high
        sep     #$20
        .a8
        lda     VM_T5           ; A = y
        jsr     romcall
        NEXTOP  2
op_hplotto:
        ldy     #$F53A
        sty     VM_T4
        jsr     hfns_y
        rep     #$20
        .a16
        lda     VM_T0+1
        and     #$00FF
        tax                     ; X = x high
        lda     VM_T5
        and     #$00FF
        tay                     ; Y = y
        sep     #$20
        .a8
        lda     VM_T0           ; A = x low
        jsr     romcall
        NEXTOP  2

; hfns_y: y (FAC, GETBYT) < 192 into VM_T5, x (pushed) into VM_T0
hfns_y: stx     VM_IP
        jsr     conint
        cmp     #$C0
        bcs     @iq
        sta     VM_T5
        jmp     pop2
@iq:    pla                     ; (this call's return)
        pla
        brl     iq

; SHAPE n: DRWPNT ($F72D): n (GETBYT) against the table's count, the shape's
; address (the table, $E8, + its entry n) into HGR.SHAPE ($1A)
op_shape:
        stx     VM_IP
        jsr     conint
        sta     VM_T5
        lda     f:$0000E8
        sta     SPTR
        lda     f:$0000E9
        sta     SPTR+1
        stz     SPTR+2
        lda     VM_T5
        cmp     [SPTR]
        beq     @ok
        bcs     @iq
@ok:    rep     #$20
        .a16
        lda     VM_T5
        and     #$00FF
        asl     a
        tay
        lda     [SPTR],y
        clc
        adc     SPTR
        sta     f:$00001A
        sep     #$20
        .a8
        ldx     VM_IP
        NEXTOP  2
@iq:    brl     iq

; DRAW / XDRAW: DRAW1 ($F605) / XDRAW1 ($F661) with A = HGR.ROTATION ($F9)
op_draw:
        stx     VM_IP
        ldy     #$F605
        bra     draw_call
op_xdraw:
        stx     VM_IP
        ldy     #$F661
draw_call:
        sty     VM_T4
        lda     f:$0000F9
        jsr     romcall
        NEXTOP  2

; ROT= n, SCALE= n: GETBYT into HGR.ROTATION ($F9), HGR.SCALE ($E7)
op_rot: stx     VM_IP
        jsr     conint
        sta     f:$0000F9
        ldx     VM_IP
        NEXTOP  2
op_scale:
        stx     VM_IP
        jsr     conint
        sta     f:$0000E7
        ldx     VM_IP
        NEXTOP  2

; CONT: on from where END, STOP or Ctrl-C left the program (CONT, $D898)
op_cont:
        ldx     VM_CONTIP
        bne     @go
        lda     #ERR_CANT_CONTINUE
        jmp     vm_error
@go:    ldy     VM_CONTLIN
        sty     CURLIN
        jmp     ($0000,x)

; CLEAR: variables zero, arrays undimensioned, strings and DATA reset, the
; stacks empty (CLEARC)
op_clear:
        stx     VM_IP
        jsr     clear_all
        ldx     VM_IP
        NEXTOP  2

clear_all:
        ldy     VM_PROG
        rep     #$20
        .a16
        lda     a:$001A,y       ; the variables: start, end (the descriptors)
        sta     VM_T0
        lda     a:$001C,y
        sta     VM_T1
        sep     #$20
        .a8
        ldy     VM_T0
@z:     cpy     VM_T1
        beq     @desc
        lda     #$00
        sta     a:$0000,y
        iny
        bra     @z
@desc:  ldy     VM_PROG         ; descriptors keep their dimension counts:
        rep     #$20            ; the compiler's copy is restored
        .a16
        lda     a:$001E,y       ; the pristine descriptors' copy, and count
        sta     VM_T0
        lda     a:$0020,y
        sta     VM_T2
        lda     a:$001C,y
        sta     VM_T1           ; where they live
        sep     #$20
        .a8
        ldy     #$0000
@d:     cpy     VM_T2
        beq     @heap
        phy
        rep     #$20
        .a16
        tya
        clc
        adc     VM_T0
        tay
        sep     #$20
        .a8
        lda     a:$0000,y
        ply
        phy
        pha
        rep     #$20
        .a16
        tya
        clc
        adc     VM_T1
        tay
        sep     #$20
        .a8
        pla
        sta     a:$0000,y
        ply
        iny
        bra     @d
@heap:  ldy     VM_PROG         ; direct lines' variables (header +$22, 0: none),
        rep     #$20            ; up to the bank's end
        .a16
        lda     a:$0022,y
        tay
        sep     #$20
        .a8
        beq     @heap2
@dz:    lda     #$00
        sta     a:$0000,y
        iny
        bne     @dz
@heap2: ldy     #$0000          ; no CONT after a CLEAR (STKINI)
        sty     VM_CONTIP
        ldy     VM_PROG         ; the array heap from its start
        rep     #$20
        .a16
        lda     a:$0018,y
        sta     a:$0008,y
        lda     a:$000C,y
        sta     VM_SSP
        lda     a:$0002,y
        sta     VM_FSP
        lda     a:$0016,y
        sta     DATPTR
        lda     VM_CBASE_PTR
        sta     VM_CSP
        stz     SH_FREE         ; strings: the low half, empty
        lda     #$8000
        sta     SH_END
        sep     #$20
        .a8
        stz     SH_HALF
        rts

; RUN e: CLEAR, then the program from entry e's code
op_run: ldy     a:$0002,x
        lda     a:$0001,y
        bne     @have
        jsr     lazy
        bra     @go
@have:  ldx     a:$0000,y
@go:    stx     VM_T5
        jsr     clear_all
        ldx     VM_T5
        jmp     ($0000,x)

; LOMEM: n: GETADR (range), then CLEAR (LOMEM $F2A6); HIMEM: n: GETADR
op_lomem:
        stx     VM_IP
        FPCHK   fp_getadr
        jsr     clear_all
        ldx     VM_IP
        NEXTOP  2
op_himem:
        FPCHK   fp_getadr
        NEXTOP  2

; ONERR GOTO a: ONERR's flag ($D8 bit 7) and the handler
op_onerr:
        ldy     a:$0002,x       ; the handler compiled now, not in the error
        lda     a:$0001,y
        bne     @ok
        phx
        jsr     lazy
        plx
@ok:    ldy     a:$0002,x
        sty     VM_ONERR
        lda     f:$0000D8
        ora     #$80
        sta     f:$0000D8
        NEXTOP  4

; STMT: a statement starts (in programs with ONERR, RESUME or TRACE): its
; place and the control stack's, for RESUME; TRACE's "#n " (TRACE. $D805,
; which NEWSTT passes for every statement)
op_stmt:
        stx     VM_STMTIP
        ldy     VM_CSP
        sty     VM_STMTCSP
        bit     VM_TRACE
        bpl     @go
        stx     VM_IP
        lda     #'#'
        jsr     outdo
        jsr     linprt
        lda     #' '
        jsr     outdo
        ldx     VM_IP
@go:    NEXTOP  2

; HANDLERR ($F2E9): A = the code: $DE, the line in $DA/$DB, RESUME's
; statement; the expression stacks emptied; the handler
handlerr:
        stz     VM_IOQUIET      ; (an error inside INPUT: what follows shows)
        sta     f:$0000DE
        lda     CURLIN
        sta     f:$0000DA
        lda     CURLIN+1
        sta     f:$0000DB
        ldy     VM_STMTIP
        sty     VM_ERRIP
        ldy     VM_STMTCSP
        sty     VM_ERRCSP
        ldy     VM_PROG
        rep     #$20
        .a16
        lda     a:$0002,y
        sta     VM_FSP
        lda     a:$000C,y
        sta     VM_SSP
        lda     VM_S            ; the machine stack as at dispatch
        tcs
        sep     #$20
        .a8
        rep     #$10
        .i16
        ldy     VM_ONERR        ; the handler's line
        ldx     a:$0000,y
        jmp     ($0000,x)

; RESUME: the statement that failed, again
op_resume:
        lda     f:$0000DA
        sta     CURLIN
        lda     f:$0000DB
        sta     CURLIN+1
        ldy     VM_ERRCSP
        sty     VM_CSP
        ldx     VM_ERRIP
        jmp     ($0000,x)

; ---- the end of the VM: the program image the compiler writes starts here
; a CHR$(4) command for the shell (outdo): its length, then its text
cmd_buf:
        .res    256
; WRITE's output on its way to the file (outdo, io_flush)
out_buf:
        .res    256

        .segment "TAIL"
prog_start:
