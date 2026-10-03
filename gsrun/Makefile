# gsrun: AppleSoft programs under the IIgs ROM's own AppleSoft, or under
# applesoft816 (--vm), on emu816
EMU      = ../emu816
COMP     = ../compiler
ARES    ?= ../../ares/ares/component/processor/wdc65816
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wno-misleading-indentation
CFLAGS   ?= -std=c99 -O2 -g -Wall -Wextra
CPPFLAGS += -I$(EMU) -I$(ARES) -I$(COMP)

gsrun: gsrun.cpp compile.o $(EMU)/cpu816.o $(EMU)/cpu816.hpp $(COMP)/compile.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ gsrun.cpp compile.o $(EMU)/cpu816.o

compile.o: $(COMP)/compile.c $(COMP)/compile.h
	$(CC) $(CFLAGS) -c -o $@ $(COMP)/compile.c

$(EMU)/cpu816.o: $(EMU)/cpu816.cpp $(EMU)/cpu816.hpp
	$(MAKE) -C $(EMU) cpu816.o

clean:
	rm -f gsrun compile.o parsetest parse.o


# parsetest: compiler/parse.c against the ROM's PARSE.INPUT.LINE
parsetest: parsetest.cpp parse.o $(EMU)/cpu816.o $(EMU)/cpu816.hpp $(COMP)/parse.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ parsetest.cpp parse.o $(EMU)/cpu816.o

parse.o: $(COMP)/parse.c $(COMP)/parse.h
	$(CC) $(CFLAGS) -c -o $@ $(COMP)/parse.c
.PHONY: clean
