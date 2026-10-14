; res.inc - the resident bank-0 block AS816.SYSTEM sets up (cc/res.h is
; the same in C).  Everything below $9600 is the program's (HIMEM).
RES_DP     = $9600      ; the VM's direct page (fp816's too)
RES_VMSTUB = $9700      ; the VM's stubs, copied from its image (<= $80 bytes)
RES_MLI    = $9780      ; JSL: an MLI call, command RES_CMD, parameters at RES_PARMS
RES_ROM    = $97C0      ; JSL: JSR RES_TGT in emulation mode, RES_A/X/Y in and out, RES_P out
RES_CMD    = $9800      ; 1
RES_ERR    = $9801      ; 1: the MLI's error (0 = none)
RES_A      = $9802
RES_X      = $9803
RES_Y      = $9804
RES_P      = $9805
RES_TGT    = $9806      ; 2
RES_PARMS  = $9880      ; 128: MLI parameters
RES_PATH   = $9900      ; 128: a pathname (length byte first)
RES_SCRATCH = $9980     ; 128
RES_EXIT   = $9A00      ; 512: the way out and Ctrl-Reset (sys.s, segment EXIT)
RES_RESETF = $9A00      ; 1: set by Ctrl-Reset: the shell enters warm, from the prompt
RES_FBUF   = $9C00      ; 1024: OPEN's buffer
RES_IOBUF  = $A000      ; 2048: transfers
RES_IOSIZE = $0800
RES_TBUF   = $A800      ; 5 x 1024: OPEN's buffers for text files ($A800-$BBFF)
RES_NTBUF  = 5
RES_CCDP   = $BC00      ; 256: the compiler's FIN's direct page (cc/glue_cc.s)
RES_END    = $BD00
