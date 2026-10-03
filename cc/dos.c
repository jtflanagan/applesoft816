/* dos.c - BASIC.SYSTEM's commands (docs/M5.md), at the prompt and from a
 * running program (CHR$(4)), as ProDOS BASIC 1.7 (2.4.3's) has them: the
 * line is read with its spaces dropped, the longest command name first,
 * then a pathname and ",X" options (A B E L R F S D @ T V; $ for hex).
 * Errors are BASIC.SYSTEM's numbers and messages; ONERR sees the numbers.
 * Output is checked against BASIC.SYSTEM itself in KEGS (tests/dos).
 */
#include <stdint.h>
#include <string.h>

#include "dos.h"
#include "machine.h"

enum {
  C_APPEND, C_BLOAD, C_BRUN, C_BSAVE, C_BYE, C_CAT, C_CATALOG, C_CHAIN, C_CLOSE, C_CREATE, C_DASH,
  C_DELETE, C_EXEC, C_FLUSH, C_FRE, C_INN, C_LOAD, C_LOCK, C_MON, C_NOMON, C_OPEN, C_POSITION,
  C_PRN, C_PREFIX, C_READ, C_RENAME, C_RESTORE, C_RUN, C_SAVE, C_STORE, C_UNLOCK, C_VERIFY, C_WRITE,
  C_COUNT
};
static const char *const names[C_COUNT] = {
  "APPEND", "BLOAD", "BRUN", "BSAVE", "BYE", "CAT", "CATALOG", "CHAIN", "CLOSE", "CREATE", "-",
  "DELETE", "EXEC", "FLUSH", "FRE", "IN#", "LOAD", "LOCK", "MON", "NOMON", "OPEN", "POSITION",
  "PR#", "PREFIX", "READ", "RENAME", "RESTORE", "RUN", "SAVE", "STORE", "UNLOCK", "VERIFY", "WRITE"
};

/* BASIC.SYSTEM's errors */
enum {
  B_RANGE = 2, B_NO_DEVICE, B_WRITE_PROTECTED, B_END_OF_DATA, B_PATH_NOT_FOUND, B_PATH_NOT_FOUND2,
  B_IO, B_DISK_FULL, B_FILE_LOCKED, B_INVALID_PARAMETER, B_NO_BUFFERS, B_FILE_TYPE, B_TOO_LARGE,
  B_NOT_DIRECT, B_SYNTAX, B_DIRECTORY_FULL, B_FILE_NOT_OPEN, B_DUPLICATE, B_FILE_BUSY, B_STILL_OPEN
};
static const char *const messages[] = {
  "RANGE ERROR", "NO DEVICE CONNECTED", "WRITE PROTECTED", "END OF DATA", "PATH NOT FOUND",
  "PATH NOT FOUND", "I/O ERROR", "DISK FULL", "FILE LOCKED", "INVALID PARAMETER", "NO BUFFERS AVAILABLE",
  "FILE TYPE MISMATCH", "PROGRAM TOO LARGE", "NOT DIRECT COMMAND", "SYNTAX ERROR", "DIRECTORY FULL",
  "FILE NOT OPEN", "DUPLICATE FILE NAME", "FILE BUSY", "FILE(S) STILL OPEN"
};

/* (BASIC.SYSTEM 1.7 follows its message with a blank line once a PREFIX
 * with a pathname has been set - from then on, whatever the prefix - and
 * always after NOT DIRECT COMMAND: seen in KEGS, not understood) */
static uint8_t prefix_set;

void dos_message(uint8_t err)
{
  crout();
  print(err >= B_RANGE && err <= B_STILL_OPEN ? messages[err - B_RANGE] : "I/O ERROR");
  crout();
  if (err == B_NOT_DIRECT || prefix_set) crout();
}

uint8_t dos_error(uint8_t e)
{
  switch (e) {
  case 0x28: return B_NO_DEVICE;
  case 0x2B: return B_WRITE_PROTECTED;
  case 0x40: case 0x44: case 0x45: return B_PATH_NOT_FOUND2;
  case 0x46: return B_PATH_NOT_FOUND;
  case 0x42: case 0x56: return B_NO_BUFFERS;
  case 0x43: return B_FILE_NOT_OPEN;
  case 0x47: return B_DUPLICATE;
  case 0x48: return B_DISK_FULL;
  case 0x49: return B_DIRECTORY_FULL;
  case 0x4A: case 0x4B: return B_FILE_TYPE;
  case 0x4C: return B_END_OF_DATA;
  case 0x4D: return B_RANGE;
  case 0x4E: return B_FILE_LOCKED;
  case 0x50: return B_FILE_BUSY;
  case 0x53: return B_INVALID_PARAMETER;
  }
  return B_IO;
}

/* ---- the command line, parsed ---- */
enum { O_A = 1, O_B = 2, O_E = 4, O_L = 8, O_R = 16, O_F = 32, O_S = 64, O_D = 128, O_AT = 256, O_T = 512 };
typedef struct {
  uint8_t cmd;
  uint8_t path[65], path2[65];      /* length first; 0: none */
  uint16_t have;                    /* the options given */
  uint32_t b;                       /* (a byte offset: past 64K) */
  uint16_t a, e, l, r, f, s, d, at;
  uint8_t t;
} Cmd;

static uint8_t s[256], n, i;        /* the line: upper case, no spaces */

static const struct { char name[4]; uint8_t type; } types[] = {
  {"TXT", 0x04}, {"BIN", 0x06}, {"DIR", 0x0F}, {"ADB", 0x19}, {"AWP", 0x1A}, {"ASP", 0x1B},
  {"S16", 0xB3}, {"PAS", 0xEF}, {"CMD", 0xF0}, {"INT", 0xFA}, {"IVR", 0xFB}, {"BAS", 0xFC},
  {"VAR", 0xFD}, {"REL", 0xFE}, {"SYS", 0xFF}
};

static int is_digit(uint8_t c) { return c >= '0' && c <= '9'; }
static int is_hex(uint8_t c) { return is_digit(c) || (c >= 'A' && c <= 'F'); }

/* a pathname: 1, 0 if none starts here, -1 if malformed */
static int get_path(uint8_t *out)
{
  uint8_t k = 0, c;
  out[0] = 0;
  if (i >= n || !((s[i] >= 'A' && s[i] <= 'Z') || s[i] == '/')) return 0;
  while (i < n && (c = s[i]) != ',') {
    if (!((c >= 'A' && c <= 'Z') || is_digit(c) || c == '.' || c == '/') || k == 64) return -1;
    out[++k] = c;
    i++;
  }
  out[0] = k;
  return 1;
}

static int get_num32(uint32_t *v)
{
  uint8_t k = 0;
  *v = 0;
  if (i < n && s[i] == '$') {
    i++;
    while (i < n && is_hex(s[i])) *v = *v * 16 + (uint32_t)(is_digit(s[i]) ? s[i] - '0' : s[i] - 'A' + 10), i++, k++;
  } else
    while (i < n && is_digit(s[i])) *v = *v * 10 + (uint32_t)(s[i] - '0'), i++, k++;
  return k > 0;
}
static int get_num(uint16_t *v)
{
  uint32_t w;
  if (!get_num32(&w) || w > 0xFFFF) return 0;
  *v = (uint16_t)w;
  return 1;
}

static int get_type(uint8_t *t)
{
  uint32_t v;
  uint8_t k;
  if (i < n && s[i] == '$') {
    if (!get_num32(&v) || v > 0xFF) return 0;
    *t = (uint8_t)v;
    return 1;
  }
  for (k = 0; k < sizeof types / sizeof types[0]; k++)
    if (i + 3 <= n && !memcmp(s + i, types[k].name, 3)) { *t = types[k].type; i = (uint8_t)(i + 3); return 1; }
  return 0;
}

/* the options after the path(s): 0, or B_SYNTAX */
static uint8_t get_options(Cmd *c)
{
  while (i < n) {
    uint8_t o;
    uint16_t *v = 0;
    uint16_t bit = 0;
    if (s[i++] != ',' || i >= n) return B_SYNTAX;
    o = s[i++];
    switch (o) {
    case 'A': v = &c->a, bit = O_A; break;
    case 'B': if (!get_num32(&c->b)) return B_SYNTAX; c->have |= O_B; continue;
    case 'E': v = &c->e, bit = O_E; break;
    case 'L': v = &c->l, bit = O_L; break;
    case 'R': v = &c->r, bit = O_R; break;
    case 'F': v = &c->f, bit = O_F; break;
    case 'S': v = &c->s, bit = O_S; break;
    case 'D': v = &c->d, bit = O_D; break;
    case '@': v = &c->at, bit = O_AT; break;
    case 'V': { uint16_t ignored; if (!get_num(&ignored)) return B_SYNTAX; continue; }
    case 'T':
      if (!get_type(&c->t)) return B_SYNTAX;
      c->have |= O_T;
      continue;
    default: return B_SYNTAX;
    }
    if (!get_num(v)) return B_SYNTAX;
    c->have |= bit;
  }
  return 0;
}

/* ---- files ---- */
static uint8_t file_info(const uint8_t *path, InfoParms *info)
{
  put_path(path);
  memset(info, 0, sizeof *info);
  info->n = 10, info->path = RES_PATH;
  return mli(MLI_GET_FILE_INFO, info, sizeof *info);
}

static uint8_t create(const uint8_t *path, uint8_t type, uint16_t aux)
{
  CreateParms c;
  put_path(path);
  c.n = 7, c.path = RES_PATH, c.access = 0xC3, c.type = type, c.aux = aux;
  c.storage = type == 0x0F ? 0x0D : 0x01;
  p8_now(&c.cdate, &c.ctime);
  return mli(MLI_CREATE, &c, sizeof c);
}

static uint8_t set_mark(uint8_t ref, uint32_t pos)
{
  MarkParms m;
  m.n = 2, m.ref = ref, m.pos[0] = (uint8_t)pos, m.pos[1] = (uint8_t)(pos >> 8), m.pos[2] = (uint8_t)(pos >> 16);
  return mli(MLI_SET_MARK, &m, sizeof m);
}
static uint32_t get_eof(uint8_t ref)
{
  MarkParms m;
  m.n = 2, m.ref = ref;
  if (mli(MLI_GET_EOF, &m, sizeof m)) return 0;
  return (uint32_t)m.pos[0] | (uint32_t)m.pos[1] << 8 | (uint32_t)m.pos[2] << 16;
}

/* BLOAD: the file's bytes into bank 0; *addr = where.  Not over $00-$1F
 * (C's registers while the shell runs) nor $9600 up (ours, ProDOS's). */
static uint8_t bload(Cmd *c, uint16_t *addr)
{
  InfoParms info;
  ReadParms r;
  uint8_t e, ref;
  uint32_t a, len, eof, done = 0;
  if ((e = file_info(c->path, &info)) != 0) return dos_error(e);
  if (info.type != ((c->have & O_T) ? c->t : 0x06)) return B_FILE_TYPE;
  a = (c->have & O_A) ? c->a : info.aux;
  if ((e = open_path(&ref)) != 0) return dos_error(e);
  eof = get_eof(ref);
  if (c->have & O_B) {
    if (c->b > eof) { close_ref(ref); return B_RANGE; }
    set_mark(ref, c->b);
  }
  len = (c->have & O_L) ? c->l : (c->have & O_E) ? c->e - a + 1 : eof - ((c->have & O_B) ? c->b : 0);
  if (a < 0x20 || a + len > RES_DP) { close_ref(ref); return B_RANGE; }
  r.n = 4, r.ref = ref, r.buf = RES_IOBUF;
  while (done < len) {
    r.req = (uint16_t)(len - done < RES_IOSIZE ? len - done : RES_IOSIZE);
    r.trans = 0;
    e = mli(MLI_READ, &r, sizeof r);
    if (e == MLI_EOF || (!e && !r.trans)) break;
    if (e) { close_ref(ref); return dos_error(e); }
    xfer_far = (uint32_t)RES_IOBUF - 1, xfer_far2 = (uint32_t)(a + done) - 1, xfer_n = r.trans;
    far_move();
    done += r.trans;
  }
  close_ref(ref);
  *addr = (uint16_t)a;
  return 0;
}

/* BSAVE: bank 0's bytes into the file, made if need be */
static uint8_t bsave(Cmd *c)
{
  InfoParms info;
  ReadParms w;
  uint8_t e, ref, type = (c->have & O_T) ? c->t : 0x06;
  uint32_t len, done = 0;
  if (!(c->have & O_A) || !(c->have & (O_L | O_E))) return B_SYNTAX;
  len = (c->have & O_L) ? c->l : c->e - c->a + 1;
  if (c->a + len > 0x10000) return B_RANGE;
  e = file_info(c->path, &info);
  if (e == 0x46) {
    if ((e = create(c->path, type, (uint16_t)c->a)) != 0) return dos_error(e);
  } else if (e)
    return dos_error(e);
  else if (info.type != type)
    return B_FILE_TYPE;
  put_path(c->path);
  if ((e = open_path(&ref)) != 0) return dos_error(e);
  if (c->have & O_B) set_mark(ref, c->b);
  w.n = 4, w.ref = ref, w.buf = RES_IOBUF;
  while (done < len && !e) {
    w.req = (uint16_t)(len - done < RES_IOSIZE ? len - done : RES_IOSIZE);
    xfer_far = (uint32_t)(c->a + done) - 1, xfer_far2 = (uint32_t)RES_IOBUF - 1, xfer_n = w.req;
    far_move();
    e = mli(MLI_WRITE, &w, sizeof w);
    done += w.req;
  }
  close_ref(ref);
  return e ? dos_error(e) : 0;
}

/* ---- CATALOG and CAT ---- */
static const char months[] = "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC";

static void print2(uint8_t v)          /* two digits */
{
  cout((uint8_t)('0' + v / 10 % 10));
  cout((uint8_t)('0' + v % 10));
}
static void print_date(uint16_t d, uint16_t t, int with_time)
{
  uint8_t m = (uint8_t)((d >> 5) & 15);
  if (!d || !m || m > 12) {
    print("<NO DATE>");
    if (with_time) print("      ");
    return;
  }
  print_w(d & 31, 2);
  cout('-');
  cout((uint8_t)months[3 * (m - 1)]), cout((uint8_t)months[3 * (m - 1) + 1]), cout((uint8_t)months[3 * (m - 1) + 2]);
  cout('-');
  print2((uint8_t)((d >> 9) % 100));
  if (with_time) {
    cout(' ');
    print_w(t >> 8, 2);
    cout(':');
    print2((uint8_t)t);
  }
}
static void print_type(uint8_t t)
{
  uint8_t k;
  for (k = 0; k < sizeof types / sizeof types[0]; k++)
    if (types[k].type == t) { print(types[k].name); return; }
  cout('$');
  print_hex(t, 2);
}

/* the full pathname of path (the prefix before a partial one) into out */
static uint8_t full_path(const uint8_t *path, uint8_t *out)
{
  PathParms pp = {1, RES_PATH};
  uint8_t e, k;
  if (path[0] && path[1] == '/') { memcpy(out, path, (size_t)path[0] + 1); return 0; }
  if ((e = mli(MLI_GET_PREFIX, &pp, sizeof pp)) != 0) return e;
  from0(out, RES_PATH, 65);
  if (out[0] + path[0] > 64) return 0x40;
  for (k = 1; k <= path[0]; k++) out[++out[0]] = path[k];
  return 0;
}

static uint8_t catalog(Cmd *c, int wide)
{
  ReadParms r;
  InfoParms vol;
  uint8_t dir[65], vname[17], e, ref, blk[512], first = 1, k;
  uint16_t at;
  if ((e = full_path(c->path, dir)) != 0) return dos_error(e);
  if (dir[0] > 1 && dir[dir[0]] == '/') dir[0]--;                  /* (a prefix ends in /) */
  for (k = 1; k <= dir[0] && k < 16 && (k == 1 || dir[k] != '/'); k++) vname[k] = dir[k];
  vname[0] = (uint8_t)(k - 1);
  if ((e = file_info(vname, &vol)) != 0) return dos_error(e);
  put_path(dir);
  if ((e = open_path(&ref)) != 0) return dos_error(e);
  r.n = 4, r.ref = ref, r.buf = RES_IOBUF, r.req = 512;
  crout();
  for (;;) {
    if (mli(MLI_READ, &r, sizeof r) || r.trans != 512) break;
    from0(blk, RES_IOBUF, 512);
    for (at = 4; at + 0x27 <= 512; at += 0x27) {
      uint8_t *x = blk + at, st = x[0] >> 4, len = x[0] & 15, type = x[0x10];
      if (first) {                        /* the directory's own header */
        first = 0;
        if (st == 0xF) cout('/');
        for (k = 0; k < len; k++) cout(x[1 + k]);
        crout();
        crout();
        print(wide ? " NAME           TYPE  BLOCKS  MODIFIED         CREATED          ENDFILE SUBTYPE"
                   : " NAME           TYPE  BLOCKS  MODIFIED");
        crout();
        crout();
        continue;
      }
      if (!st || ((c->have & O_T) && type != c->t)) continue;
      cout((x[0x1E] & 0xC2) == 0xC2 ? ' ' : '*');
      for (k = 0; k < 15; k++) cout(k < len ? x[1 + k] : ' ');
      cout(' ');
      print_type(type);
      print_w((uint16_t)(x[0x13] | x[0x14] << 8), 8);
      print("  ");
      print_date((uint16_t)(x[0x21] | x[0x22] << 8), (uint16_t)(x[0x23] | x[0x24] << 8), wide);
      if (wide) {
        print("  ");
        print_date((uint16_t)(x[0x18] | x[0x19] << 8), (uint16_t)(x[0x1A] | x[0x1B] << 8), 1);
        print_w((uint32_t)x[0x15] | (uint32_t)x[0x16] << 8 | (uint32_t)x[0x17] << 16, 9);
        if (type == 0x04) { print(" R="); print_w((uint16_t)(x[0x1F] | x[0x20] << 8), 5); }
        if (type == 0x06) { print(" A=$"); print_hex((uint16_t)(x[0x1F] | x[0x20] << 8), 4); }
      }
      crout();
    }
  }
  close_ref(ref);
  crout();
  print("BLOCKS FREE:");
  print_w((uint16_t)(vol.aux - vol.blocks), 5);
  print("     BLOCKS USED:");
  print_w(vol.blocks, 5);
  if (wide) {
    print("     TOTAL BLOCKS:");
    print_w(vol.aux, 5);
  }
  crout();
  crout();
  return 0;
}

/* ---- the commands ---- */
static uint8_t run(Cmd *c, int deferred)
{
  uint8_t e;
  int32_t line = (c->have & O_AT) ? (int32_t)c->at : -1;
  if (deferred) { prog_pending(1, c->path, line); return DOS_STOP; }
  if ((e = prog_load(c->path)) != 0) return DOS_ERROR | dos_error(e);
  prog_run(line);
  return DOS_OK;
}

static uint8_t command(Cmd *c, int deferred)
{
  uint8_t e = 0, k;
  uint16_t addr;
  InfoParms info;
  switch (c->cmd) {
  case C_BYE: return DOS_BYE;
  case C_CAT: case C_CATALOG: e = catalog(c, c->cmd == C_CATALOG); break;
  case C_PREFIX:
    if (!c->path[0]) {
      PathParms pp = {1, RES_PATH};
      uint8_t p[65];
      if ((e = mli(MLI_GET_PREFIX, &pp, sizeof pp)) != 0) { e = dos_error(e); break; }
      from0(p, RES_PATH, 65);
      crout();
      for (k = 1; k <= p[0]; k++) cout(p[k]);
      crout();
      crout();
    } else {
      PathParms pp = {1, RES_PATH};
      put_path(c->path);
      if ((e = mli(MLI_SET_PREFIX, &pp, sizeof pp)) != 0) e = dos_error(e);
      else prefix_set = 1;
    }
    break;
  case C_CREATE: if ((e = create(c->path, (c->have & O_T) ? c->t : 0x0F, 0)) != 0) e = dos_error(e); break;
  case C_DELETE: {
    PathParms d = {1, RES_PATH};
    put_path(c->path);
    if ((e = mli(MLI_DESTROY, &d, sizeof d)) != 0) e = dos_error(e);
    break;
  }
  case C_RENAME: {
    RenameParms rn = {2, RES_PATH, RES_PATH + 0x41};
    put_path(c->path);
    to0(RES_PATH + 0x41, c->path2, (uint16_t)(c->path2[0] + 1));
    if ((e = mli(MLI_RENAME, &rn, sizeof rn)) != 0) e = dos_error(e);
    break;
  }
  case C_LOCK: case C_UNLOCK: {
    SetInfoParms si;
    if ((e = file_info(c->path, &info)) != 0) { e = dos_error(e); break; }
    memset(&si, 0, sizeof si);
    si.n = 7, si.path = RES_PATH, si.type = info.type, si.aux = info.aux, si.mdate = info.mdate, si.mtime = info.mtime;
    si.access = c->cmd == C_LOCK ? (uint8_t)(info.access & ~0xC2) : (uint8_t)(info.access | 0xC2);
    if ((e = mli(MLI_SET_FILE_INFO, &si, sizeof si)) != 0) e = dos_error(e);
    break;
  }
  case C_VERIFY: if ((e = file_info(c->path, &info)) != 0) e = dos_error(e); break;
  case C_BLOAD: e = bload(c, &addr); break;
  case C_BRUN:
    if ((e = bload(c, &addr)) == 0) rom_call(addr);
    break;
  case C_BSAVE: e = bsave(c); break;
  case C_RUN: return run(c, deferred);
  case C_LOAD:
    if (deferred) { prog_pending(2, c->path, -1); return DOS_STOP; }
    if ((e = prog_load(c->path)) != 0) e = dos_error(e);
    break;
  case C_SAVE: if ((e = prog_save(c->path)) != 0) e = dos_error(e); break;
  case C_DASH:                            /* by the file's type: RUN, BRUN (EXEC: with text files) */
    if ((e = file_info(c->path, &info)) != 0) { e = dos_error(e); break; }
    if (info.type == 0xFC) return run(c, deferred);
    if (info.type == 0x06) {
      if ((e = bload(c, &addr)) == 0) rom_call(addr);
      break;
    }
    e = B_FILE_TYPE;
    break;
  case C_PRN: case C_INN:
    if (c->have & O_A) {                  /* PR#A$addr: the hook itself */
      uint8_t h[2] = {(uint8_t)c->a, (uint8_t)(c->a >> 8)};
      to0(c->cmd == C_PRN ? 0x36 : 0x38, h, 2);
    } else {
      poke0(RES_A, (uint8_t)c->s);
      rom_call(c->cmd == C_PRN ? OUTPORT : INPORT);
    }
    break;
  case C_FRE: case C_MON: case C_NOMON: case C_CLOSE: case C_FLUSH: break;
  default: e = B_SYNTAX; break;           /* (OPEN, READ, WRITE, APPEND, POSITION, EXEC, CHAIN, STORE, RESTORE: next) */
  }
  return e ? (uint8_t)(DOS_ERROR | e) : DOS_OK;
}

uint8_t dos_command(const uint8_t *text, uint8_t len, int deferred, int quiet)
{
  static Cmd c;
  uint8_t k, best = 0xFF, blen = 0, e;
  int p;
  for (n = 0, k = 0; k < len; k++) {
    uint8_t ch = text[k] & 0x7F;
    if (ch == ' ') continue;
    s[n++] = ch >= 'a' && ch <= 'z' ? (uint8_t)(ch - 32) : ch;
  }
  for (k = 0; k < C_COUNT; k++) {          /* the longest name the line starts with */
    uint8_t l = (uint8_t)strlen(names[k]);
    if (l <= n && l > blen && !memcmp(s, names[k], l)) best = k, blen = l;
  }
  if (best == 0xFF) return DOS_NOT;
  memset(&c, 0, sizeof c);
  c.cmd = best;
  i = blen;
  if (best == C_PRN || best == C_INN) {   /* PR#n, PR#A$addr */
    if (i < n && s[i] == 'A') { i++; if (!get_num(&c.a)) return DOS_NOT; c.have |= O_A; }
    else if (!get_num(&c.s)) return DOS_NOT;
    if (i != n) return DOS_NOT;
    return command(&c, deferred);
  }
  p = get_path(c.path);
  if (p < 0) { e = B_SYNTAX; goto fail; }
  if (!p) {
    /* RUN, LOAD, SAVE, RESTORE, STORE with no name are AppleSoft's (RUN 100) */
    if (best == C_RUN || best == C_LOAD || best == C_SAVE || best == C_RESTORE || best == C_STORE ||
        best == C_DASH)
      return DOS_NOT;
    if (best != C_CAT && best != C_CATALOG && best != C_PREFIX && best != C_CLOSE && best != C_FLUSH &&
        best != C_BYE && best != C_FRE && best != C_MON && best != C_NOMON) {
      e = B_SYNTAX;
      goto fail;
    }
  }
  if (best == C_RENAME) {
    if (i >= n || s[i++] != ',' || get_path(c.path2) <= 0) { e = B_SYNTAX; goto fail; }
  }
  if ((e = get_options(&c)) != 0) goto fail;
  if (!deferred && (best == C_OPEN || best == C_READ || best == C_WRITE || best == C_APPEND || best == C_POSITION)) {
    e = B_NOT_DIRECT;
    goto fail;
  }
  k = command(&c, deferred);
  if (!(k & DOS_ERROR) || k == DOS_NOT) return k;
  e = (uint8_t)(k & 0x7F);
fail:
  if (!quiet) dos_message(e);
  return (uint8_t)(DOS_ERROR | e);
}
