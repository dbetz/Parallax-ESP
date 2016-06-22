ifeq ($(CROSS),win32)
PREFIX=i586-mingw32msvc-
EXT=.exe
endif

CC=$(PREFIX)gcc

GZIP_COMPRESSION ?= no

CFLAGS=-I.. -std=gnu99
ifeq ("$(GZIP_COMPRESSION)","yes")
CFLAGS		+= -DESPFS_GZIP
endif

OBJS=mkroffsimage.o
TARGET=mkroffsimage$(EXT)

$(TARGET): $(OBJS)
ifeq ("$(GZIP_COMPRESSION)","yes")
	$(CC) -o $@ $^ -lz
else
	$(CC) -o $@ $^
endif

clean:
	rm -f $(TARGET) $(OBJS)
