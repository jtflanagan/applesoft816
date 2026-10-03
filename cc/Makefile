# The compiler for the 65816 (llvm-mos-65816, as the GS control panel uses):
# cc.bin, loaded at $0000 of a bank, its data bank the next one.
TC      ?= $(HOME)/opt/llvm-mos-65816-root/usr/lib/llvm-mos-65816
CC816    = $(TC)/bin/mos-common-clang
FLAGS    = -mcpu=mosw65816 -Os -fno-jump-tables -mlto-zp=0 -nostartfiles -I../compiler -T link.ld -Wl,-Map=cc.map
SRC = crt0.s glue.s main.c shell.c ../compiler/compile.c ../compiler/parse.c

cc.bin: $(SRC) link.ld ../compiler/compile.h
	$(CC816) $(FLAGS) -o $@ $(SRC)
	python3 lint_absolute.py $(TC)/bin/llvm-objdump cc.bin.elf

clean:
	rm -f cc.bin cc.bin.elf cc.map
.PHONY: clean
