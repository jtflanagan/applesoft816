# Lemonade Stand

- Original: Bob Jamison (MECC); Apple version by Charlie Kellner, February
  1979, (c) 1979 Apple Computer.
- Image: `lemonade_stand.dsk`, from the Asimov archive
  `images/games/misc/lemonade_stand.dsk` (mirror
  mirrors.apple2.org.za/ftp.apple.asimov.net), sha1
  b52f68f6beb7d8834e22e70356cd53a4201eb282.  DOS 3.3: `HELLO` runs
  `LEMONADE` (47 sectors, 11,750 bytes tokenized, ends at $35E7).
- Not on any System Master we hold (680-0210-A, 680-0051-00/-01,
  680-0186-A checked).  The callapple "LC" lower-case version is a remake.
- `LEMONADE.bas`: the tokenized program as it sits at $801
  (`tools/dsk33.py get`); `LEMONADE.lst`: `tools/detok.py` listing.
- `play1.keys`: one player, five days, ESC, no replay (asprof script).

What it exercises: two POKEd machine-code routines (`$302` speaker tone,
cycle-timed; `$3600` lo-res recolour, reading COLOR `$30`, CH/CV, WNDTOP,
JSR `$FBC1`, with an original typo `LDA $30A2`), a banner PRINTed as
characters in GR mode, text-window POKEs 32-35, `CALL -936/-958`,
`LOMEM: 14080`, `RND(-1)` reseeding, `STR$` money formatting, FOR delay
loops, GOTO out of GOSUB, DATA with empty and unquoted items, a DOS exit via
`CALL 976`.
