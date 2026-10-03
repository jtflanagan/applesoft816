/*
 * asprof - run ROM AppleSoft on an emulated 65C02 Apple IIe and profile
 * where the cycles go.
 *
 * The machine: 64K main RAM, the 16K enhanced IIe ROM ($C000-$FFFF image,
 * internal $Cxxx ROM switched by INTCXROM/SLOTC3ROM/INTC8ROM), empty slots,
 * no language card RAM, no aux memory, a keyboard fed from a script, the
 * speaker counted.  It boots to the ] prompt like a IIe with no disk
 * controller, then either installs a tokenized program at $801 (--prog) or
 * types a listing in (--type), then types RUN and the script's keys.
 *
 * Attribution: a shadow call stack follows JSR and stack unwinding.  Every
 * instruction's cycles go to
 *   - EXCLUSIVE: the innermost cost centre on the shadow stack, else a
 *     fallback by PC (CHRGET in zero page, user code in RAM, the monitor);
 *   - INCLUSIVE: every cost centre on the stack (so these overlap).
 * Only cycles while a program runs (CURLIN+1 != $FF) are counted.
 *
 * Script syntax: literal characters, newlines ignored, '#' lines comments,
 * {CR} {ESC} {SP} for those keys, {SNAP} to print the text screen when the
 * program next asks for a key.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vrEmu6502.h"

#define CLOCK_HZ 1020484.0 /* the IIe's average CPU clock (stretched cycles) */

static void die(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(2);
}

static uint8_t *slurp(const char *path, size_t *len)
{
  FILE *f = fopen(path, "rb");
  if (!f) die("%s: cannot open", path);
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc(n + 1);
  if (fread(b, 1, n, f) != (size_t)n) die("%s: short read", path);
  fclose(f);
  b[n] = 0;
  *len = n;
  return b;
}

/* ---------------------------------------------------------------- machine */

static uint8_t ram[0x10000];
static uint8_t rom[0x4000]; /* $C000-$FFFF */
static bool intcxrom, slotc3rom, intc8rom;
static uint64_t cycles;
static uint64_t speaker_toggles;

/* keyboard: a queue of keys; a key is latched only when the program polls */
#define K_SNAP 0x100
static uint16_t *keyq;
static size_t nkeys, kpos;
static uint8_t kbd_latch;
static bool kbd_strobe;
static bool holding_keys = true; /* until the boot reaches the ] prompt */
static bool at_prompt_event;     /* first poll at the prompt: install */
static uint64_t empty_since;     /* cycle of the first poll with nothing */
static bool empty_polling;

static void snap_screen(void);
static VrEmu6502 *cpu;

/* A poll from the firmware's KEYIN (the enhanced IIe's KEYIN at $FD1B goes
 * straight to the internal $Cx ROM, GOTOCX2) is a wait for a key.  Polls that
 * only peek - AppleSoft's Ctrl-C check ISCNTC $D858, the monitor's Ctrl-S
 * check VIDWAIT $FB78 on every carriage return - are not. */
static bool polled_by_input_routine(void)
{
  uint16_t pc = vrEmu6502GetCurrentOpcodeAddr(cpu);
  if (getenv("ASPROF_PCS")) {
    static uint32_t seen[0x10000];
    if (!seen[pc]++) fprintf(stderr, "empty poll from %04X at cycle %llu\n", pc, (unsigned long long)cycles);
  }
  return pc >= 0xC100 && pc < 0xD000;
}

static void kbd_poll(void)
{
  if (kbd_strobe) return;
  if (holding_keys) {
    /* the ] prompt: KEYIN waiting in direct mode (not the boot's VIDWAIT
     * poll, which comes before AppleSoft's cold start NEWs $801) */
    if (ram[0x76] == 0xFF && polled_by_input_routine()) at_prompt_event = true;
    return;
  }
  while (kpos < nkeys && keyq[kpos] == K_SNAP) {
    snap_screen();
    kpos++;
  }
  if (kpos < nkeys) {
    kbd_latch = (uint8_t)keyq[kpos++];
    kbd_strobe = true;
    empty_polling = false;
  } else if (!empty_polling && polled_by_input_routine()) {
    if (getenv("ASPROF_DEBUG")) fprintf(stderr, "empty poll pc=%04X cyc=%llu kpos=%zu curlin=%02X%02X\n", vrEmu6502GetCurrentOpcodeAddr(cpu), (unsigned long long)cycles, kpos, ram[0x76], ram[0x75]);
    empty_polling = true;
    empty_since = cycles;
  }
}

static uint8_t io_read(uint16_t a, bool dbg)
{
  switch (a & 0xFF) {
  case 0x00:
    if (!dbg) kbd_poll();
    return kbd_latch | (kbd_strobe ? 0x80 : 0);
  case 0x10:
    if (!dbg) kbd_strobe = false;
    return kbd_latch;
  case 0x19: /* RDVBLBAR: bit 7 low during vertical blank */
    return (cycles % 17030) < 12480 ? 0x80 : 0x00;
  case 0x30:
    if (!dbg) speaker_toggles++;
    return 0;
  default:
    return 0;
  }
}

static void io_write(uint16_t a, uint8_t v)
{
  (void)v;
  switch (a & 0xFF) {
  case 0x06: intcxrom = false; break;
  case 0x07: intcxrom = true; break;
  case 0x0A: slotc3rom = false; break;
  case 0x0B: slotc3rom = true; break;
  case 0x10: kbd_strobe = false; break;
  case 0x30: speaker_toggles++; break;
  default: break;
  }
}

static uint8_t cx_read(uint16_t a, bool dbg)
{
  if (a == 0xCFFF && !dbg) intc8rom = false;
  if (intcxrom) return rom[a - 0xC000];
  if (a < 0xC400 && a >= 0xC300) {
    if (slotc3rom) return 0xFF;
    if (!dbg) intc8rom = true;
    return rom[a - 0xC000];
  }
  if (a >= 0xC800 && intc8rom) return rom[a - 0xC000];
  return 0xFF; /* empty slot */
}

static uint8_t mem_read(uint16_t a, bool dbg)
{
  if (a < 0xC000) return ram[a];
  if (a < 0xC100) return io_read(a, dbg);
  if (a < 0xD000) return cx_read(a, dbg);
  return rom[a - 0xC000];
}

static void mem_write(uint16_t a, uint8_t v)
{
  if (a < 0xC000) ram[a] = v;
  else if (a < 0xC100) io_write(a, v);
  else if (a == 0xCFFF) intc8rom = false;
  /* $D000-$FFFF: ROM (no language card RAM) */
}

static void snap_screen(void)
{
  printf("---- screen at cycle %llu ----\n", (unsigned long long)cycles);
  for (int r = 0; r < 24; r++) {
    char line[41];
    uint16_t base = 0x400 + (r & 7) * 0x80 + (r >> 3) * 0x28;
    for (int c = 0; c < 40; c++) {
      uint8_t ch = ram[base + c] & 0x7F;
      if (ch < 0x20) ch += 0x40;
      line[c] = (char)ch;
    }
    line[40] = 0;
    printf("|%s|\n", line);
  }
}

/* ---------------------------------------------------------------- symbols */

typedef struct {
  char name[64];
  uint16_t addr;
} Sym;
static Sym syms[4096];
static int nsyms;

static void load_syms(const char *path)
{
  size_t n;
  char *t = (char *)slurp(path, &n);
  for (char *line = strtok(t, "\r\n"); line; line = strtok(NULL, "\r\n")) {
    unsigned addr;
    char name[64];
    if (line[0] == ';' || line[0] == '\t' || line[0] == ' ') continue;
    if (sscanf(line, "%x %63s", &addr, name) == 2 && nsyms < 4096) {
      syms[nsyms].addr = (uint16_t)addr;
      snprintf(syms[nsyms].name, sizeof syms[nsyms].name, "%s", name);
      nsyms++;
    }
  }
  free(t);
}

/* the label at or below a ROM address; symbols are global labels only */
static const char *sym_below(uint16_t a)
{
  int best = -1;
  for (int i = 0; i < nsyms; i++)
    if (syms[i].addr >= 0xD000 && syms[i].addr <= a && (best < 0 || syms[i].addr > syms[best].addr))
      best = i;
  return best < 0 ? "?" : syms[best].name;
}

static int sym_addr(const char *name)
{
  for (int i = 0; i < nsyms; i++)
    if (!strcmp(syms[i].name, name)) return syms[i].addr;
  return -1;
}

/* ------------------------------------------------------------ categories */

/* Exclusive attribution is by WHERE THE PC IS: AppleSoft reaches most of its
 * work by tail jumps and RTS dispatch (statements, operators, FIN from
 * FRM.ELEMENT), so a JSR-based call stack cannot see it.  Ranges follow the
 * S-C DocuMentor labels (AppleWin's A2_BASIC.SYM); the first match wins, so
 * the specific ranges come before the general ones. */
enum {
  C_DISPATCH, C_CHRGET, C_EVAL, C_VARS, C_FMATH, C_FMOVE, C_FUNCS, C_FIN, C_FOUT,
  C_FNDLIN, C_LINGET, C_FLOW, C_FORNEXT, C_SKIP, C_LET, C_PRINT, C_INPUT, C_STR,
  C_GC, C_STMT, C_HIRES, C_LORES, C_OUT, C_KEYIN, C_USER, C_FIRM, C_MON, C_OTHER,
  NCAT
};
static const char *cat_name[NCAT] = {
    "statement dispatch",  "CHRGET scanner",     "expression eval",    "variables/arrays",
    "float arithmetic",    "float load/store",   "math functions",     "FIN text->number",
    "FOUT number->text",   "FNDLIN line search", "LINGET line# parse", "GOTO/GOSUB/IF/ON",
    "FOR/NEXT",            "skip REM/DATA/IF",   "LET",                "PRINT",
    "INPUT/GET/READ",      "strings",            "string GC",          "other statements",
    "hi-res graphics",     "lo-res graphics",    "monitor screen out", "monitor keyboard",
    "user machine code",   "Cx firmware",        "monitor (other)",    "AppleSoft (other)"};

static const struct {
  uint16_t lo, hi;
  uint8_t cat;
} ranges[] = {
    {0xD365, 0xD392, C_FORNEXT},  /* GTFORPNT */
    {0xD393, 0xD3E2, C_VARS},     /* BLTU, CHKMEM: room for a new variable */
    {0xD61A, 0xD648, C_FNDLIN},   /* FNDLIN, FL1 */
    {0xD766, 0xD7D1, C_FORNEXT},  /* FOR, STEP */
    {0xD7D2, 0xD86D, C_DISPATCH}, /* NEWSTT, TRACE, EXECUTE.STATEMENT, COLON, ISCNTC */
    {0xD921, 0xD994, C_FLOW},     /* GOSUB, GOTO, POP, RETURN */
    {0xD995, 0xD9C8, C_SKIP},     /* DATA, DATAN, REMN */
    {0xD9C9, 0xDA0B, C_FLOW},     /* IF, ON */
    {0xDA0C, 0xDA45, C_LINGET},
    {0xDA46, 0xDAD4, C_LET},      /* LET, PUTSTR */
    {0xDAD5, 0xDB70, C_PRINT},    /* PRINT, CRDO, PR.COMMA/TAB/SPC, STROUT, OUTDO */
    {0xDB71, 0xDCF8, C_INPUT},    /* errors, GET, INPUT, READ, FINDATA */
    {0xDCF9, 0xDD66, C_FORNEXT},  /* NEXT */
    {0xDD67, 0xDFCC, C_EVAL},     /* FRMNUM..FRMEVL..relops, STRCMP */
    {0xDFE3, 0xE2DD, C_VARS},     /* PTRGET, MAKE.NEW.VARIABLE, AYINT, ARRAY.. */
    {0xE3C5, 0xE483, C_STR},      /* STR$, string space */
    {0xE484, 0xE596, C_GC},       /* GARBAG */
    {0xE597, 0xE745, C_STR},      /* CAT, MOVSTR, FRESTR, CHR$ LEFT$ MID$ LEN ASC VAL */
    {0xE913, 0xE97E, C_FUNCS},    /* LOG and its constants, inside the arithmetic */
    {0xE9E3, 0xEA0D, C_FMOVE},    /* LOAD.ARG.FROM.YA */
    {0xEAF9, 0xEB71, C_FMOVE},    /* LOAD.FAC.FROM.YA, STORE.FAC.*, COPY ARG/FAC */
    {0xE7A0, 0xEC49, C_FMATH},    /* FADD..FDIV, NORMALIZE, SHIFT, FCOMP, FLOAT, QINT, INT */
    {0xEC4A, 0xED18, C_FIN},
    {0xED19, 0xEE8C, C_FOUT},     /* LINPRT, PRINT.FAC, FOUT */
    {0xEE8D, 0xF10A, C_FUNCS},    /* SQR, ^, EXP, LOG polys, RND, SIN COS TAN ATN */
    {0xF1D5, 0xF3D7, C_STMT},     /* CALL, PLOT/HLIN/VLIN glue, VTAB, HIMEM.., GR, TEXT */
    {0xDFCD, 0xDFE2, C_STMT},     /* PDL, DIM */
    {0xE2DE, 0xE3C4, C_STMT},     /* FRE, POS, DEF FN, FN */
    {0xE746, 0xE79F, C_STMT},     /* GETADR, PEEK, POKE, WAIT */
    {0xF3D8, 0xF7FF, C_HIRES},
    {0xF800, 0xF87F, C_LORES},    /* PLOT HLINE VLINE CLRSCR SETCOL SCRN */
    {0xFB40, 0xFB4A, C_LORES},    /* SETGR */
    {0xFD0C, 0xFD8F, C_KEYIN},    /* RDKEY, KEYIN, GETLN */
    {0xFB78, 0xFCA7, C_OUT},      /* VIDWAIT, VTAB, CLREOP, HOME, SCROLL, CLREOL */
    {0xFDED, 0xFE00, C_OUT},      /* COUT, COUT1, STORADV */
    {0xF800, 0xFFFF, C_MON},
    {0xD000, 0xF7FF, C_OTHER},
};

static uint8_t cat_at[0x10000];

static void build_cat_map(void)
{
  for (int a = 0; a < 0x10000; a++) {
    uint8_t c = a >= 0xB1 && a <= 0xC8 ? C_CHRGET : a < 0xC000 ? C_USER : a < 0xD000 ? C_FIRM : C_OTHER;
    if (a >= 0xD000)
      for (size_t i = 0; i < sizeof ranges / sizeof ranges[0]; i++)
        if (a >= ranges[i].lo && a <= ranges[i].hi) {
          c = ranges[i].cat;
          break;
        }
    cat_at[a] = c;
  }
}

/* --------------------------------------------------------------- routines */

/* Inclusive cost and call counts for routines reached by JSR, from a shadow
 * call stack.  Names are S-C DocuMentor labels; a few monitor entries without
 * a label in AppleWin's tables are given by address. */
static struct {
  const char *name;
  int addr;
} centres[] = {
    {"FIN", 0}, {"FOUT", 0}, {"PTRGET", 0}, {"FNDLIN", 0}, {"FL1", 0}, {"LINGET", 0}, {"GTFORPNT", 0},
    {"DATAN", 0}, {"REMN", 0}, {"FRMEVL", 0}, {"FRMNUM", 0}, {"GARBAG", 0}, {"GETSPA", 0},
    {"LOAD.FAC.FROM.YA", 0}, {"LOAD.ARG.FROM.YA", 0}, {"STORE.FAC.AT.YX.ROUNDED", 0},
    {"FADD", 0}, {"FSUB", 0}, {"FMULT", 0}, {"FDIV", 0}, {"FCOMP", 0}, {"FPWRT", 0}, {"SQR", 0}, {"LOG", 0},
    {"EXP", 0}, {"SIN", 0}, {"COS", 0}, {"TAN", 0}, {"ATN", 0}, {"INT", 0}, {"RND", 0},
    {"COUT", 0}, {"HOME", 0}, {"CLREOP", 0}, {"GETLN", 0}, {"KEYIN", 0}, {"=FD0C", 0}, /* RDKEY */
    {"=FC22", 0}, /* VTAB */ {"=FC9C", 0}, /* CLREOL */ {"=F800", 0}, /* PLOT */
    {"=F819", 0}, /* HLINE */ {"=F828", 0}, /* VLINE */ {"=F864", 0}, /* SETCOL */
};
#define NCENTRES (int)(sizeof centres / sizeof centres[0])
static int16_t centre_at[0x10000]; /* address -> centre index, -1 */

static void resolve_centres(void)
{
  memset(centre_at, 0xFF, sizeof centre_at);
  for (int i = 0; i < NCENTRES; i++) {
    int a = centres[i].name[0] == '=' ? (int)strtol(centres[i].name + 1, NULL, 16)
                                      : sym_addr(centres[i].name);
    if (a < 0) die("symbol %s not found", centres[i].name);
    centres[i].addr = a;
    centre_at[a] = (int16_t)i;
  }
}

/* -------------------------------------------------------------- profiler */

typedef struct {
  uint16_t target, site;
  uint8_t sp; /* S after the JSR's push */
  int16_t centre;
  bool from_user; /* called from machine code in RAM */
} Frame;
static Frame frames[512];
static int nframes;

static uint64_t excl[NCAT];
static uint64_t centre_incl[NCENTRES], centre_calls[NCENTRES];
static uint64_t line_cycles[0x10000];
static uint64_t run_cycles, run_insns;
static uint64_t pc_cycles[0x10000];
static uint16_t cur_line;      /* CURLIN as of the last statement dispatch */
static int exec_stmt_addr;

static bool in_chrget(uint16_t pc) { return pc >= 0xB1 && pc <= 0xC8; }

static void attribute(uint16_t pc, unsigned c)
{
  bool seen_centre[NCENTRES] = {0};
  bool user = false;
  for (int i = 0; i < nframes; i++) {
    if (frames[i].centre >= 0) seen_centre[frames[i].centre] = true;
    user |= frames[i].from_user;
  }
  /* ROM routines a CALLed routine uses are that routine's cost */
  excl[user ? C_USER : cat_at[pc]] += c;
  for (int k = 0; k < NCENTRES; k++)
    if (seen_centre[k]) centre_incl[k] += c;
  line_cycles[cur_line] += c;
  pc_cycles[pc] += c;
  run_cycles += c;
  run_insns++;
}

static void push_frame(uint16_t site, uint16_t target, uint8_t sp, bool running)
{
  if (nframes == (int)(sizeof frames / sizeof frames[0])) die("shadow stack overflow");
  Frame *f = &frames[nframes++];
  f->site = site;
  f->target = target;
  f->sp = sp;
  f->centre = centre_at[target];
  f->from_user = site < 0xC000 && !in_chrget(site);
  if (running && f->centre >= 0) centre_calls[f->centre]++;
}

/* ------------------------------------------------------------------ main */

static void load_script(const char *text, uint16_t **q, size_t *n, size_t *cap)
{
  bool comment = false, bol = true;
  for (const char *p = text; *p; p++) {
    if (bol && *p == '#') comment = true;
    if (*p == '\n') {
      comment = false;
      bol = true;
      continue;
    }
    bol = false;
    if (comment || *p == '\r') continue;
    uint16_t k = (uint8_t)*p;
    if (*p == '{') {
      const char *e = strchr(p, '}');
      if (!e) die("script: unterminated {");
      size_t l = e - p - 1;
      if (l == 2 && !strncmp(p + 1, "CR", 2)) k = 0x0D;
      else if (l == 3 && !strncmp(p + 1, "ESC", 3)) k = 0x1B;
      else if (l == 2 && !strncmp(p + 1, "SP", 2)) k = 0x20;
      else if (l == 4 && !strncmp(p + 1, "SNAP", 4)) k = K_SNAP;
      else die("script: unknown {%.*s}", (int)l, p + 1);
      p = e;
    }
    if (*n == *cap) *q = realloc(*q, (*cap = *cap * 2 + 64) * sizeof **q);
    (*q)[(*n)++] = k;
  }
}

static void type_listing(const char *text, uint16_t **q, size_t *n, size_t *cap)
{
  for (const char *p = text; *p; p++) {
    if (*p == '\r') continue;
    if (*n == *cap) *q = realloc(*q, (*cap = *cap * 2 + 64) * sizeof **q);
    (*q)[(*n)++] = *p == '\n' ? 0x0D : (uint8_t)*p;
  }
}

static void usage(void)
{
  die("usage: asprof --rom ROM --sym A2_BASIC.SYM --sym APPLE2E.SYM\n"
      "              (--prog TOKENIZED | --type LISTING) [--keys SCRIPT]\n"
      "              [--max-cycles N] [--idle N] [--lines N] [--syms N] [--snap-end]\n"
      "              [--cout FILE]       every byte the running program sends to COUT ($FDED)\n"
      "              [--dump-prog FILE]  the tokenized program ($801..PRGEND) at the end\n"
      "              [--no-run]          type the listing in, do not RUN it (with --dump-prog)");
}

int main(int argc, char **argv)
{
  const char *rom_path = NULL, *prog_path = NULL, *type_path = NULL, *keys_path = NULL;
  uint64_t max_cycles = 4000000000ULL, idle_limit = 3000000;
  int top_lines = 25, top_syms = 25;
  bool snap_end = false;
  const char *cout_path = NULL, *dump_path = NULL;
  bool no_run = false;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    const char *v = i + 1 < argc ? argv[i + 1] : NULL;
    if (!strcmp(a, "--rom") && v) rom_path = argv[++i];
    else if (!strcmp(a, "--sym") && v) load_syms(argv[++i]);
    else if (!strcmp(a, "--prog") && v) prog_path = argv[++i];
    else if (!strcmp(a, "--type") && v) type_path = argv[++i];
    else if (!strcmp(a, "--keys") && v) keys_path = argv[++i];
    else if (!strcmp(a, "--max-cycles") && v) max_cycles = strtoull(argv[++i], NULL, 0);
    else if (!strcmp(a, "--idle") && v) idle_limit = strtoull(argv[++i], NULL, 0);
    else if (!strcmp(a, "--lines") && v) top_lines = atoi(argv[++i]);
    else if (!strcmp(a, "--syms") && v) top_syms = atoi(argv[++i]);
    else if (!strcmp(a, "--snap-end")) snap_end = true;
    else if (!strcmp(a, "--cout") && v) cout_path = argv[++i];
    else if (!strcmp(a, "--dump-prog") && v) dump_path = argv[++i];
    else if (!strcmp(a, "--no-run")) no_run = true;
    else usage();
  }
  if (!rom_path || !nsyms || (!prog_path == !type_path)) usage();

  size_t n;
  uint8_t *r = slurp(rom_path, &n);
  if (n != sizeof rom) die("%s: %zu bytes, want a 16K $C000-$FFFF image", rom_path, n);
  memcpy(rom, r, n);
  free(r);
  resolve_centres();
  build_cat_map();
  if ((exec_stmt_addr = sym_addr("EXECUTE.STATEMENT")) < 0) die("EXECUTE.STATEMENT not found");

  size_t cap = 0;
  if (type_path) {
    uint8_t *t = slurp(type_path, &n);
    type_listing((char *)t, &keyq, &nkeys, &cap);
    free(t);
  }
  if (!no_run) load_script("RUN{CR}", &keyq, &nkeys, &cap);
  size_t run_keys = nkeys; /* counting starts once RUN's Return is taken */
  if (keys_path) {
    uint8_t *t = slurp(keys_path, &n);
    load_script((char *)t, &keyq, &nkeys, &cap);
    free(t);
  }
  size_t prog_len = 0;
  uint8_t *prog = prog_path ? slurp(prog_path, &prog_len) : NULL;
  if (prog && 0x801 + prog_len > 0x9600) die("%s: too big", prog_path);

  cpu = vrEmu6502New(CPU_65C02, mem_read, mem_write);
  vrEmu6502Reset(cpu);

  bool running = false, ever_ran = false;
  FILE *cout_f = NULL;
  bool cout_done = false;
  int restart_addr = sym_addr("RESTART");
  if (restart_addr < 0) die("RESTART not in the symbols");
  if (cout_path && !(cout_f = fopen(cout_path, "wb"))) die("%s: cannot write", cout_path);
  const char *why = "max cycles";
  while (cycles < max_cycles) {
    uint16_t pc = vrEmu6502GetPC(cpu);
    uint8_t op = mem_read(pc, true);
    /* the program's output: from RUN until AppleSoft's RESTART (END, STOP,
     * an error - its message included - all end there; the ] prompt after) */
    if (cout_f && pc == restart_addr && ever_ran) cout_done = true;
    if (cout_f && running && !cout_done && pc == 0xFDED) fputc(vrEmu6502GetAcc(cpu), cout_f);
    uint8_t sp0 = vrEmu6502GetStackPointer(cpu);
    unsigned c = vrEmu6502InstCycle(cpu);
    cycles += c;
    /* CURLIN is written a byte at a time (NEXT restores it from the FOR
     * frame); sample it where a statement is dispatched, when it is whole */
    if (pc == exec_stmt_addr) cur_line = ram[0x75] | ram[0x76] << 8;
    if (running) attribute(pc, c);

    if (op == 0x20)
      push_frame(pc, mem_read(pc + 1, true) | mem_read(pc + 2, true) << 8, (uint8_t)(sp0 - 2), running);
    uint8_t sp = vrEmu6502GetStackPointer(cpu);
    while (nframes && sp > frames[nframes - 1].sp) nframes--;

    if (at_prompt_event) {
      at_prompt_event = false;
      holding_keys = false;
      if (prog) {
        memcpy(ram + 0x801, prog, prog_len);
        ram[0x800] = 0;
        uint16_t end = (uint16_t)(0x801 + prog_len);
        ram[0x67] = 0x01, ram[0x68] = 0x08;              /* TXTTAB */
        for (int zp = 0x69; zp <= 0x6D; zp += 2)          /* VARTAB ARYTAB STREND */
          ram[zp] = end & 0xFF, ram[zp + 1] = end >> 8;
        ram[0xAF] = end & 0xFF, ram[0xB0] = end >> 8;     /* PRGEND */
        if (getenv("ASPROF_MEM")) {
          fprintf(stderr, "install at cycle %llu pc=%04X\n", (unsigned long long)cycles, vrEmu6502GetPC(cpu));
          for (int z = 0x67; z < 0x74; z++) fprintf(stderr, "%02X=%02X ", z, ram[z]);
          fprintf(stderr, "\n");
        }
      }
    }
    /* counting: after RUN's Return, while a line runs, until the script
     * is exhausted (the ] prompt waits with CURLIN still on the last line) */
    bool now = !holding_keys && kpos >= run_keys && ram[0x76] != 0xFF && !empty_polling;
    if (now && !running) ever_ran = true;
    if (getenv("ASPROF_DEBUG")) {
      static size_t last_k = 0;
      static uint8_t last_hi = 0;
      if (kpos != last_k || ram[0x76] != last_hi)
        fprintf(stderr, "cyc=%llu kpos=%zu/%zu curlin=%02X%02X now=%d empty=%d hold=%d\n",
                (unsigned long long)cycles, kpos, run_keys, ram[0x76], ram[0x75], now, empty_polling, holding_keys);
      last_k = kpos, last_hi = ram[0x76];
    }
    running = now;
    if (empty_polling && cycles - empty_since > idle_limit) {
      why = ever_ran ? "script exhausted, keyboard idle" : "never ran (idle at the prompt)";
      break;
    }
  }

  if (snap_end) snap_screen();
  if (cout_f) fclose(cout_f);
  if (dump_path) {
    FILE *f = fopen(dump_path, "wb");
    if (!f) die("%s: cannot write", dump_path);
    unsigned start = ram[0x67] | ram[0x68] << 8, end = ram[0xAF] | ram[0xB0] << 8;
    if (start != 0x801 || end < start || end > 0xC000) die("odd program pointers %04X-%04X", start, end);
    fwrite(ram + start, 1, end - start, f);
    fclose(f);
  }
  if (getenv("ASPROF_MEM")) {
    for (int z = 0x67; z < 0x74; z++) fprintf(stderr, "%02X=%02X ", z, ram[z]);
    fprintf(stderr, "\nAF=%02X B0=%02X  $800:", ram[0xAF], ram[0xB0]);
    for (int z = 0x800; z < 0x810; z++) fprintf(stderr, " %02X", ram[z]);
    fprintf(stderr, "\n");
  }
  printf("stopped: %s after %llu cycles; %zu of %zu keys used; speaker toggles %llu\n", why,
         (unsigned long long)cycles, kpos, nkeys, (unsigned long long)speaker_toggles);
  printf("program: %llu cycles = %.3f s at 1.0205 MHz, %llu instructions\n\n",
         (unsigned long long)run_cycles, run_cycles / CLOCK_HZ, (unsigned long long)run_insns);
  if (!run_cycles) return 1;

  int order[NCAT];
  for (int k = 0; k < NCAT; k++) order[k] = k;
  for (int i = 0; i < NCAT; i++)
    for (int j = i + 1; j < NCAT; j++)
      if (excl[order[j]] > excl[order[i]]) {
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
      }
  printf("%-22s %14s %6s   (exclusive: where the PC is)\n", "category", "cycles", "%");
  for (int i = 0; i < NCAT; i++) {
    int k = order[i];
    if (!excl[k]) continue;
    printf("%-22s %14llu %5.1f%%\n", cat_name[k], (unsigned long long)excl[k], 100.0 * excl[k] / run_cycles);
  }

  printf("\n%-24s %10s %14s %10s %6s   (inclusive, JSR calls only)\n", "routine", "calls", "cycles", "avg/call", "%");
  for (int i = 0; i < NCENTRES; i++) {
    if (!centre_calls[i]) continue;
    printf("%-24s %10llu %14llu %10.1f %5.1f%%\n", centres[i].name, (unsigned long long)centre_calls[i],
           (unsigned long long)centre_incl[i], (double)centre_incl[i] / centre_calls[i],
           100.0 * centre_incl[i] / run_cycles);
  }

  /* exclusive cycles by the ROM routine the PC is in */
  {
    static uint64_t by_sym[4096];
    static const char *names[4096];
    int nn = 0;
    for (int a = 0; a < 0x10000; a++) {
      if (!pc_cycles[a]) continue;
      const char *nm = in_chrget((uint16_t)a) ? "CHRGET (zero page)"
                       : a < 0xC000            ? "(user code in RAM)"
                       : a < 0xD000            ? "(Cx firmware)"
                                               : sym_below((uint16_t)a);
      int k;
      for (k = 0; k < nn && strcmp(names[k], nm); k++) {}
      if (k == nn) names[nn++] = nm;
      by_sym[k] += pc_cycles[a];
    }
    printf("\n%-24s %14s %6s   (exclusive, by ROM label)\n", "PC in", "cycles", "%");
    for (int t = 0; t < top_syms; t++) {
      int b = -1;
      for (int k = 0; k < nn; k++)
        if (by_sym[k] && (b < 0 || by_sym[k] > by_sym[b])) b = k;
      if (b < 0) break;
      printf("%-24s %14llu %5.1f%%\n", names[b], (unsigned long long)by_sym[b], 100.0 * by_sym[b] / run_cycles);
      by_sym[b] = 0;
    }
  }

  printf("\nhot lines (cycles while CURLIN = line)\n");
  for (int t = 0; t < top_lines; t++) {
    int best = -1;
    for (int l = 0; l < 0x10000; l++)
      if (line_cycles[l] && (best < 0 || line_cycles[l] > line_cycles[best])) best = l;
    if (best < 0) break;
    printf("%6d %14llu %5.1f%%\n", best, (unsigned long long)line_cycles[best],
           100.0 * line_cycles[best] / run_cycles);
    line_cycles[best] = 0;
  }
  return 0;
}
