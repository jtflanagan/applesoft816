# fp816: AppleSoft floating point for the 65816, checked against the ROM.
EMU      = ../emu816
ARES    ?= ../../ares/ares/component/processor/wdc65816
ROM     ?= ../../strudelworks-host/assets/apple2gs_rom3.bin
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wno-misleading-indentation
CPPFLAGS += -I$(EMU) -I$(ARES)
CASES   ?= 200000

all: fp.bin fptest

fp.bin: fp.s fp.cfg
	ca65 --cpu 65816 -l fp.lst -o fp.o fp.s
	ld65 -C fp.cfg -m fp.map -o $@ fp.o

$(EMU)/cpu816.o:
	$(MAKE) -C $(EMU) cpu816.o

fptest: fptest.cpp $(EMU)/cpu816.o $(EMU)/cpu816.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ fptest.cpp $(EMU)/cpu816.o

test: fp.bin fptest
	./fptest $(ROM) fp.bin $(CASES)

clean:
	rm -f fp.bin fp.o fp.lst fp.map fptest

.PHONY: all test clean
