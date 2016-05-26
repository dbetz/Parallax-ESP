$(info HOME $(HOME))

TARGET=robot-ws.elf

all:	robot.elf robot-ws.elf tcp-test.elf

CFLAGS=-mcmm -Os -m32bit-doubles -I $(HOME)/simple/include -L $(HOME)/simple/lib

%.elf:	%.c
	propeller-elf-gcc $(CFLAGS) -o $@ $< -lsimple-cmm -lm

run:	$(TARGET)
	proploader $< -t

clean:
	rm -f *.elf
