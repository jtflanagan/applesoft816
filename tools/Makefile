# tools/Makefile - prodisk, over the strudelworks host's ProDOS volume code
HOST = ../../strudelworks-host/src

prodisk: prodisk.cpp $(HOST)/prodos_fs.cpp $(HOST)/prodos_fs.hpp
	g++ -std=c++20 -O1 -Wall -I$(HOST) -o $@ prodisk.cpp $(HOST)/prodos_fs.cpp

clean:
	rm -f prodisk
