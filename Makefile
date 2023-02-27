INCLUDE_DIRS=include

NBITS ?= 128
$(info NBITS: $(NBITS))

ifeq ($(NBITS), 512)
   SIMD=-mavx512f -mavx512dq
else ifeq ($(NBITS), 256)
   SIMD=-mavx2
else ifeq ($(NBITS), 128)
   SIMD=-msse4.2
endif

BINDIR=bin-$(NBITS)

COMMONFLAGS = -c -O3 $(SIMD) -I$(INCLUDE_DIRS)

SFMT_FLAGS = -DSFMT_MEXP=19937

CFLAGS += $(COMMONFLAGS)
CPPFLAGS += $(COMMONFLAGS) -O3 $(SIMD)


HEADERS := $(shell find . -name "*.h")
$(info HEADERS: $(HEADERS))


CPP_SRC=$(wildcard src/*.cpp)
$(info C++ files: $(CPP_SRC))

CPP_OBJ=$(patsubst src/%.cpp,$(BINDIR)/%.cpp.obj,$(CPP_SRC))
$(info C++ obj: $(CPP_OBJ))

MT_OBJ = $(BINDIR)/mt19937ar.c.obj
SFMT_OBJ = $(BINDIR)/sfmt.c.obj

TARGETS := $(patsubst src/%.cpp,$(BINDIR)/%.exe,$(CPP_SRC))
$(info TARGETS: $(TARGETS))

all: $(TARGETS)

$(BINDIR)/perf.cpp.obj : CPPFLAGS += $(SFMT_FLAGS)

$(BINDIR)/%.cpp.obj : src/%.cpp $(HEADERS) Makefile $(BINDIR)
	g++ $(CPPFLAGS) -o $@ $<

$(MT_OBJ) : mt19937-original/mt19937ar.c $(HEADERS) Makefile $(BINDIR)
	gcc $(CFLAGS) -o $@ $<

$(SFMT_OBJ) : SFMT-src-1.5.1/sfmt.c $(HEADERS) Makefile $(BINDIR)
	gcc $(CFLAGS) $(SFMT_FLAGS) -o $@ $<

$(BINDIR)/%.exe : $(BINDIR)/%.cpp.obj $(HEADERS) Makefile $(MT_OBJ) $(SFMT_OBJ) $(BINDIR)
	g++ $(LFLAGS) -o $@ $(MT_OBJ) $(SFMT_OBJ) $<


.PHONY: clean
clean:
	rm -rf bin-*

# use -p for multithreading
$(BINDIR):
	mkdir -p $(BINDIR)