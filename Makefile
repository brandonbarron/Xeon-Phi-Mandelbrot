# Makefile

CC = icpc
CFLAGS = --std=c++11 -qopenmp -qoffload-arch=mic -O3
FILES = main.cpp
MICDIR = /lib64
MICLIBS	= /opt/intel/compilers_and_libraries_2017.1.132/linux/compiler/lib/intel64_lin_mic

all: prog

prog:		$(FILES)
		$(CC) $(CFLAGS)  $(FILES)

miclibcopy:
	sudo scp $(MICLIBS)/libiomp5.so mic0:$(MICDIR)
	sudo scp $(MICLIBS)/libiomp5.so mic1:$(MICDIR)
	@echo ""
	@echo "REMEMBER TO export LD_LIBRARY_PATH=$(MICDIR) ON THE COPROCESSOR (if needed)"
	@echo ""
