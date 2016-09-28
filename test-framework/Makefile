MKDIR=mkdir
TOUCH=touch
RM=rm -r -f

ifeq ($(CROSS),)
  PREFIX=
else
  ifeq ($(CROSS),win32)
    PREFIX=x86_64-w64-mingw32-
    OS=msys
  else
    ifeq ($(CROSS),rpi)
      PREFIX=arm-linux-gnueabihf-
      OS=linux
    else
      echo "Unknown cross compilation selected"
    endif
  endif
endif

CC=$(PREFIX)gcc
CPP=$(PREFIX)g++

CFLAGS=-Wall

ifeq ($(OS),Windows_NT)
OS=msys
endif

ifeq ($(OS),linux)
CFLAGS+=-DLINUX
EXT=
OSINT=$(OBJDIR)/sock_posix.o $(OBJDIR)/serial_posix.o
LIBS=

else ifeq ($(OS),msys)
CFLAGS+=-DMINGW
EXT=.exe
OSINT=$(OBJDIR)/serial_mingw.o $(OBJDIR)/sock_posix.o
LIBS=-lws2_32 -liphlpapi -lsetupapi

else ifeq ($(OS),macosx)
CFLAGS+=-DMACOSX
EXT=
OSINT=$(OBJDIR)/serial_posix.o $(OBJDIR)/sock_posix.o
LIBS=

else ifeq ($(OS),)
$(error OS not set)

else
$(error Unknown OS $(OS))
endif

APP=test-framework

BUILD=$(realpath ..)/$(APP)-$(OS)-build
$(info Writing build to $(BUILD))

SRCDIR=src
OBJDIR=$(BUILD)/obj
BINDIR=$(BUILD)/bin

HDRS=\
$(SRCDIR)/cmd.h \
$(SRCDIR)/serial.h \
$(SRCDIR)/sock.h

OBJS=\
$(OBJDIR)/main.o \
$(OBJDIR)/tests.o \
$(OBJDIR)/cmd.o \
$(OBJDIR)/framework.o \
$(OSINT)

CFLAGS+=-I$(OBJDIR)
CPPFLAGS=$(CFLAGS)

all:	 $(BINDIR)/$(APP)$(EXT)

$(OBJS):	$(OBJDIR)/created $(HDRS) Makefile

$(BINDIR)/$(APP)$(EXT):	$(BINDIR)/created $(OBJS)
	$(CPP) -o $@ $(OBJS) $(LIBS) -lstdc++

$(OBJDIR)/%.o:	$(SRCDIR)/%.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o:	$(SRCDIR)/%.cpp $(HDRS)
	$(CPP) $(CPPFLAGS) -c $< -o $@

run:	$(BINDIR)/$(APP)$(EXT)
	$(BINDIR)/$(APP)$(EXT) -i 10.0.1.32

clean:
	$(RM) $(BUILD)

%/created:
	@$(MKDIR) -p $(@D)
	@$(TOUCH) $@
