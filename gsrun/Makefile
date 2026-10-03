# gsrun: AppleSoft programs under the IIgs ROM's own AppleSoft, on emu816
EMU      = ../emu816
ARES    ?= ../../ares/ares/component/processor/wdc65816
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wno-misleading-indentation
CPPFLAGS += -I$(EMU) -I$(ARES)

gsrun: gsrun.cpp $(EMU)/cpu816.o $(EMU)/cpu816.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ gsrun.cpp $(EMU)/cpu816.o

$(EMU)/cpu816.o: $(EMU)/cpu816.cpp $(EMU)/cpu816.hpp
	$(MAKE) -C $(EMU) cpu816.o

clean:
	rm -f gsrun
.PHONY: clean
