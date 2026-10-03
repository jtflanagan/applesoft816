# AS816.SYSTEM, the ProDOS 8 system file
AS816.SYSTEM: sys.s res.inc sys.cfg
	ca65 --cpu 65816 -l sys.lst -o sys.o sys.s
	ld65 -C sys.cfg -m sys.map -o $@ sys.o
clean:
	rm -f AS816.SYSTEM sys.o sys.lst sys.map
.PHONY: clean
