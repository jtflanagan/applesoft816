# asprof: ROM AppleSoft on an emulated IIe, profiled.
# The CPU core is the sibling clone ~/repos/vrEmu6502 (MIT).
VREMU   ?= ../../vrEmu6502/src
APPLEWIN ?= ../../AppleWin
ROM     ?= $(APPLEWIN)/resource/Apple2e_Enhanced.rom
SYMS    = --sym $(APPLEWIN)/bin/A2_BASIC.SYM --sym $(APPLEWIN)/bin/APPLE2E.SYM
CFLAGS  ?= -O2 -g -Wall -Wextra -std=c11

asprof: asprof.c $(VREMU)/vrEmu6502.c $(VREMU)/vrEmu6502.h
	$(CC) $(CFLAGS) -I$(VREMU) -o $@ asprof.c $(VREMU)/vrEmu6502.c

RUN = ./asprof --rom $(ROM) $(SYMS)

# make bm N=3        one Rugg/Feldman benchmark
bm: asprof
	$(RUN) --type ../bench/rugg_feldman/BM$(N).bas --snap-end

lemonade: asprof
	$(RUN) --prog ../bench/lemonade/LEMONADE.bas --keys ../bench/lemonade/play1.keys --snap-end

clean:
	rm -f asprof

.PHONY: bm lemonade clean
