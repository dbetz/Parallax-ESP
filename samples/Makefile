TARGET=robot-ws.elf

all:	robot.elf robot-ws.elf

CFLAGS=-mcmm -Os -m32bit-doubles -I include -L lib

%.elf:	%.c
	propeller-elf-gcc $(CFLAGS) -o $@ $< -lsimple-cmm -lm

run:	$(TARGET)
	proploader $< -t

clean:
	rm -f *.elf
