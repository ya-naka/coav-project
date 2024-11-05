EXE=libplugin.so

all: $(EXE)

GCC_ROOT=/home/patrick.carribault/LOCAL/GCC/gcc1220_install

CXX=$(GCC_ROOT)/bin/g++_1220
CC=$(GCC_ROOT)/bin/gcc_1220

MPICC=mpicc

PLUGIN_FLAGS=-I`$(CC) -print-file-name=plugin`/include -g -Wall -fno-rtti -fPIC

CFLAGS=-g -O3

OBJS=plugin.o

%.o: %.cpp
	$(CXX) $(PLUGIN_FLAGS) -c -o $@ $<

libplugin.so: $(OBJS)
	$(CXX) -shared $(GMP_CFLAGS) -o $@ $^

test_%: tests/test_%.c libplugin.so
	OMPI_MPICC=$(CC) $(MPICC) $< $(CFLAGS) -o $@ -fplugin=./libplugin.so


.PHONY: clean cleanall
clean:
	rm -rf $(EXE)
	rm -rf test_*
	rm -rf *.o

cleanall: clean
	rm -rf libplugin*.so *.dot