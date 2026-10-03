# emu816: the ares WDC65816 core (sibling clone ~/repos/ares, ISC), standalone.
ARES    ?= ../../ares/ares/component/processor/wdc65816
SAMPLES ?= ../../strudelworks-firmware/sim/cpu816_work/samples
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wno-misleading-indentation
CPPFLAGS += -I. -I$(ARES)

all: sst_run

cpu816.o: cpu816.cpp cpu816.hpp nall_shim.hpp $(wildcard $(ARES)/*.cpp $(ARES)/*.hpp)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ cpu816.cpp

sst_run: sst_run.cpp cpu816.o
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ sst_run.cpp cpu816.o

# SingleStepTests/65816: the firmware's sampled set (sim/fetch_65816_tests.sh)
out/sst.bin: sst_pack.py
	mkdir -p out && python3 sst_pack.py $@ $(SAMPLES)/*.json

sst: sst_run out/sst.bin
	./sst_run out/sst.bin -v | grep -v '^  '

clean:
	rm -rf sst_run *.o out

.PHONY: all sst clean
