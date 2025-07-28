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
	$(GCC) $(ARGS) $(RELEASE_ARGS) $(LIBS) src/palette.cpp -o palette
	
grouper: src/grouper.cpp
	$(GCC) $(ARGS) $(RELEASE_ARGS) $(LIBS) src/grouper.cpp -o grouper

validator: src/validator.cpp
	$(GCC) $(ARGS) $(RELEASE_ARGS) $(LIBS) src/validator.cpp -o validator

clean:
	rm palette grouper validator

all: palette grouper validator


