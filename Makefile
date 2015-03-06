ifeq ($(shell uname -s),Darwin)
 INCLUDES = -I/Library/Frameworks/SDL2.framework/Headers -F/Library/Frameworks
 SDL =  -F/Library/Frameworks -framework SDL2
 MIDIDEP = coremidi.o
 MIDILIB = -framework CoreMIDI -framework CoreFoundation
else
 INCLUDES = $(shell sdl2-config --cflags)
 SDL = $(shell sdl2-config --libs)
 MIDIDEP = alsamidi.o
 MIDILIB = -lasound
endif
CFLAGS = -O2 -Wall -Wno-multichar -g $(INCLUDES)
TFLAGS = -DTEST_TARGET
LDFLAGS = -lm -lncurses $(SDL) $(MIDILIB)
CC = gcc

PROG = playmidi

DEPS = playmidi.o
DEPS += loadsf2.o
DEPS += emumidi.o
DEPS += playmidi.o
DEPS += readmidi.o
DEPS += playevents.o
DEPS += io_ncurses.o
DEPS += $(MIDIDEP)

TESTS = loadsf2-test patchdump-test

all: $(PROG) $(TESTS)

#emumidi-test: loadsf2.o

%-test: %.c
	$(CC) $(TFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS) 

$(PROG): $(DEPS)
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(PROG) $(TESTS) $(DEPS)
