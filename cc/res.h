/* res.h - the resident bank-0 block AS816.SYSTEM sets up (sys/res.inc). */
#ifndef AS816_RES_H
#define AS816_RES_H
#define RES_DP 0x9600u
#define RES_VMSTUB 0x9700u
#define RES_MLI 0x9780u
#define RES_ROM 0x97C0u
#define RES_CMD 0x9800u
#define RES_ERR 0x9801u
#define RES_A 0x9802u
#define RES_X 0x9803u
#define RES_Y 0x9804u
#define RES_P 0x9805u
#define RES_TGT 0x9806u
#define RES_PARMS 0x9880u
#define RES_PATH 0x9900u
#define RES_SCRATCH 0x9980u
#define RES_EXIT 0x9A00u               /* 512: the way out and Ctrl-Reset (sys/sys.s, EXIT) */
#define RES_RESETF 0x9A00u             /* 1: set by Ctrl-Reset: the shell enters warm, at its prompt */
#define RES_FBUF 0x9C00u
#define RES_IOBUF 0xA000u
#define RES_IOSIZE 0x0800u
#define RES_TBUF 0xA800u               /* 5 x 1K: OPEN's buffers for text files */
#define RES_NTBUF 5
#define RES_CCDP 0xBC00u               /* 256: the compiler's FIN's direct page */
#define RES_END 0xBD00u
#endif
