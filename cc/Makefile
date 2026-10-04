# The two C images on the 65816 (llvm-mos-65816), each a code bank and a
# data bank (link.ld, crt0.s): cc.bin, the compiler (AS816.CC; gsrun --cc),
# and sh.bin, the shell (AS816.SH), which calls the compiler (cccall.c).
TC      ?= $(HOME)/opt/llvm-mos-65816-root/usr/lib/llvm-mos-65816
CC816    = $(TC)/bin/mos-common-clang
FLAGS    = -mcpu=mosw65816 -Os -fno-jump-tables -mlto-zp=0 -nostartfiles -I../compiler -T link.ld
CC_SRC   = crt0.s glue.s glue_cc.s main_cc.c ../compiler/compile.c
SH_SRC   = crt0.s glue.s glue_sh.s main_sh.c shell.c dos.c machine.c cccall.c store.c ../compiler/parse.c

all: cc.bin sh.bin

cc.bin: $(CC_SRC) link.ld ../compiler/compile.h
	$(CC816) $(FLAGS) -Wl,-Map=cc.map -o $@ $(CC_SRC)
	python3 lint_absolute.py $(TC)/bin/llvm-objdump cc.bin.elf

sh.bin: $(SH_SRC) link.ld ../compiler/compile.h ../compiler/parse.h shell.h machine.h dos.h res.h
	$(CC816) $(FLAGS) -Wl,-Map=sh.map -o $@ $(SH_SRC)
	python3 lint_absolute.py $(TC)/bin/llvm-objdump sh.bin.elf

clean:
	rm -f cc.bin cc.bin.elf cc.map sh.bin sh.bin.elf sh.map
.PHONY: all clean
