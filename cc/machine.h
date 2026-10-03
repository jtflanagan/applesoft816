/* machine.h - the shell's reach into the machine (machine.c, glue.s): bank 0,
 * the screen through the ROM, the MLI, the VM bank. */
#ifndef AS816_MACHINE_H
#define AS816_MACHINE_H

#include <stdint.h>

#include "res.h"
#include "shell.h"

/* the ROM and its page zero */
#define COUT 0xFDED
#define GETLN 0xFD6A
#define HOME 0xFC58
#define INPORT 0xFE8B
#define OUTPORT 0xFE95
#define CH 0x24
#define PROMPT 0x33
#define INBUF 0x0200

/* the MLI */
#define MLI_GET_TIME 0x82
#define MLI_CREATE 0xC0
#define MLI_DESTROY 0xC1
#define MLI_RENAME 0xC2
#define MLI_SET_FILE_INFO 0xC3
#define MLI_GET_FILE_INFO 0xC4
#define MLI_ONLINE 0xC5
#define MLI_SET_PREFIX 0xC6
#define MLI_GET_PREFIX 0xC7
#define MLI_OPEN 0xC8
#define MLI_NEWLINE 0xC9
#define MLI_READ 0xCA
#define MLI_WRITE 0xCB
#define MLI_CLOSE 0xCC
#define MLI_FLUSH 0xCD
#define MLI_SET_MARK 0xCE
#define MLI_GET_MARK 0xCF
#define MLI_SET_EOF 0xD0
#define MLI_GET_EOF 0xD1
#define MLI_EOF 0x4C
/* ProDOS 8's global page */
#define P8_DATE 0xBF90

typedef struct { uint8_t n; uint16_t path, buf; uint8_t ref; } OpenParms;
typedef struct { uint8_t n, ref; uint16_t buf, req, trans; } ReadParms;   /* READ and WRITE */
typedef struct { uint8_t n, ref; } RefParms;                             /* CLOSE, FLUSH */
typedef struct { uint8_t n, ref; uint8_t pos[3]; } MarkParms;            /* SET/GET_MARK, SET/GET_EOF */
typedef struct { uint8_t n, ref, mask, ch; } NewlineParms;
typedef struct { uint8_t n; uint16_t path; } PathParms;                  /* DESTROY, SET/GET_PREFIX */
typedef struct { uint8_t n; uint16_t path, path2; } RenameParms;
typedef struct {
  uint8_t n; uint16_t path; uint8_t access, type; uint16_t aux; uint8_t storage; uint16_t cdate, ctime;
} CreateParms;
typedef struct {
  uint8_t n; uint16_t path; uint8_t access, type; uint16_t aux; uint8_t storage;
  uint16_t blocks, mdate, mtime, cdate, ctime;
} InfoParms;                                                             /* GET_FILE_INFO (n = 10) */
typedef struct {
  uint8_t n; uint16_t path; uint8_t access, type; uint16_t aux; uint8_t null[3];
  uint16_t mdate, mtime;
} SetInfoParms;                                                          /* SET_FILE_INFO (n = 7) */

/* bank 0 */
void to0(uint16_t a, const void *src, uint16_t n);
void from0(void *dst, uint16_t a, uint16_t n);
/* the VM bank, the text bank */
uint16_t vm_read16(uint16_t a);
void to_text(uint16_t a, const void *src, uint16_t n);
void from_text(void *dst, uint16_t a, uint16_t n);

/* the screen, through COUT */
void cout(uint8_t c);
void print(const char *s);
void crout(void);
void print_u(uint32_t v);
void print_w(uint32_t v, uint8_t width);   /* right-aligned in width */
void print_hex(uint16_t v, uint8_t digits);
void print_centi(uint32_t v);
/* AppleSoft's own: CR "?SYNTAX ERROR" bell (the prompt's CR follows) */
void as_error(const char *m);

/* the MLI: parameters copied to bank 0 (RES_PARMS) and back; its error */
uint8_t mli(uint8_t cmd, void *parms, uint8_t n);
void put_path(const uint8_t *p);           /* length first, into RES_PATH */
uint8_t open_path(uint8_t *ref);           /* RES_PATH, with RES_FBUF */
void close_ref(uint8_t ref);
void p8_now(uint16_t *date, uint16_t *time);   /* ProDOS 8's clock */

#endif
