# The VM image: vm.s and fp816 linked into one bank image
FP = ../fp816
vm.bin: vm.s $(FP)/fp.s vm.cfg
	ca65 --cpu 65816 -l vm.lst -o vm.o vm.s
	ca65 --cpu 65816 -o fp.o $(FP)/fp.s
	ld65 -C vm.cfg -m vm.map -o $@ vm.o fp.o
clean:
	rm -f vm.bin vm.o fp.o vm.lst vm.map
.PHONY: clean
