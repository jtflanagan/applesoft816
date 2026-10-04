/* dos.h - BASIC.SYSTEM's commands (dos.c), for the prompt and CHR$(4). */
#ifndef AS816_DOS_H
#define AS816_DOS_H

#include <stdint.h>

/* dos_command's answers: not one of its commands (AppleSoft's, then), done,
 * BYE, the program must stop (a RUN, LOAD, CHAIN is pending), or
 * DOS_ERROR | BASIC.SYSTEM's error number (2-21, ONERR's) */
#define DOS_NOT 0xFF
#define DOS_OK 0
#define DOS_BYE 1
#define DOS_STOP 2
#define DOS_ERROR 0x80

/* a command line (7-bit, any case, spaces anywhere); deferred = from a
 * running program (CHR$(4)).  Errors are printed unless quiet (ONERR). */
uint8_t dos_command(const uint8_t *text, uint8_t len, int deferred, int quiet);

/* BASIC.SYSTEM's message for an error number, printed: CR, it, CR */
void dos_message(uint8_t err);
/* an MLI error as BASIC.SYSTEM's number */
uint8_t dos_error(uint8_t mli_err);

/* the VM's text-file requests (VM_CBREQ 1 flush WRITE's output, 2 READ a
 * line, 3 READ a character): 0 or BASIC.SYSTEM's error */
uint8_t dos_io(uint8_t req);
/* WRITE is on: RESTART's CR goes to the file */
int dos_writing(void);
void dos_put_cr(void);
void dos_put(const uint8_t *p, uint8_t n);       /* bytes to WRITE's file */
/* EXEC's next line for the prompt (0: none, the keyboard) */
int dos_exec_line(uint8_t *line, uint8_t *len);
int dos_execing(void);
/* back at the prompt: READ/WRITE off; after a program, FILE(S) STILL OPEN */
void dos_prompt(int after_run);

/* "-" on a SYS file: its length in the CHAIN bank, for the loader (BYE) */
extern uint16_t dos_launch;

/* what dos.c needs of shell.c */
uint8_t prog_load(const uint8_t *path);           /* a BAS file into memory; an MLI error */
uint8_t prog_save(const uint8_t *path);
void prog_run(int32_t line);                     /* -1: from the start */
void prog_pending(uint8_t what, const uint8_t *path, int32_t line);   /* after the VM: 1 RUN, 2 LOAD, 4 CHAIN */
void prog_chain(const uint8_t *path, int32_t line);   /* CHAIN at the prompt */
int prog_vars(uint16_t *prog);                   /* compiled, for STORE/RESTORE (shell.c) */
/* store.c: STORE, RESTORE (a VAR file); 0 or BASIC.SYSTEM's error */
uint8_t prog_store(const uint8_t *path);
uint8_t prog_restore(const uint8_t *path);

/* BASIC.SYSTEM's errors */
enum {
  B_RANGE = 2, B_NO_DEVICE, B_WRITE_PROTECTED, B_END_OF_DATA, B_PATH_NOT_FOUND, B_PATH_NOT_FOUND2,
  B_IO, B_DISK_FULL, B_FILE_LOCKED, B_INVALID_PARAMETER, B_NO_BUFFERS, B_FILE_TYPE, B_TOO_LARGE,
  B_NOT_DIRECT, B_SYNTAX, B_DIRECTORY_FULL, B_FILE_NOT_OPEN, B_DUPLICATE, B_FILE_BUSY, B_STILL_OPEN
};

#endif
