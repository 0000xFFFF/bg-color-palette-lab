GCC = clang++
ARGS = 
DEBUG_ARGS = -D DEBUG -g -fno-omit-frame-pointer
RELEASE_ARGS = -Wall -Wextra -s -march=native
LIBS = `pkg-config --cflags --libs opencv4`

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
LIBDIR = $(PREFIX)/lib
INCLUDEDIR = $(PREFIX)/include

palette: src/palette.cpp
	$(GCC) $(ARGS) $(RELEASE_ARGS) $(LIBS) src/palette.cpp -o bgcpl-palette
	
grouper: src/grouper.cpp
	$(GCC) $(ARGS) $(RELEASE_ARGS) $(LIBS) src/grouper.cpp -o bgcpl-grouper

validator: src/validator.cpp
	$(GCC) $(ARGS) $(RELEASE_ARGS) $(LIBS) src/validator.cpp -o bgcpl-validator


install:
	install -m 755 bgcpl-palette $(BINDIR)
	install -m 755 bgcpl-grouper $(BINDIR)
	install -m 755 bgcpl-validator $(BINDIR)


clean:
	rm bgcpl-palette bgcpl-grouper bgcpl-validator

all: palette grouper validator


