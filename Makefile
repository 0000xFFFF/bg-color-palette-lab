GCC = clang++
ARGS = 
DEBUG_ARGS = -D DEBUG -g -fno-omit-frame-pointer
RELEASE_ARGS = -Wall -Wextra -s -march=native
LIBS = `pkg-config --cflags --libs opencv4`

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
LIBDIR = $(PREFIX)/lib
INCLUDEDIR = $(PREFIX)/include

PALETTE_FILES = src/palette.cpp
GROUPER_FILES = src/grouper.cpp src/utils.cpp
VALIDATOR_FILES = src/validator.cpp src/utils.cpp
DARKSCORE_FILES = src/darkscore.cpp src/utils.cpp

palette: $(PALETTE_FILES)
	$(GCC) $(ARGS) $(RELEASE_ARGS) $(LIBS) $(PALETTE_FILES) -o bgcpl-palette
	
grouper: $(GROUPER_FILES)
	$(GCC) $(ARGS) $(RELEASE_ARGS) $(LIBS) $(GROUPER_FILES) -o bgcpl-grouper

validator: $(VALIDATOR_FILES)
	$(GCC) $(ARGS) $(RELEASE_ARGS) $(LIBS) $(VALIDATOR_FILES) -o bgcpl-validator

darkscore: $(DARKSCORE_FILES)
	$(GCC) $(ARGS) $(RELEASE_ARGS) $(LIBS) $(DARKSCORE_FILES) -o bgcpl-darkscore

install:
	install -m 755 bgcpl-palette $(BINDIR)
	install -m 755 bgcpl-grouper $(BINDIR)
	install -m 755 bgcpl-validator $(BINDIR)
	install -m 755 bgcpl-darkscore $(BINDIR)


clean:
	rm bgcpl-palette bgcpl-grouper bgcpl-validator bgcpl-darkscore

all: palette grouper validator darkscore


