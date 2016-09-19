$(info HOME $(HOME))

TARGET=robot.elf

all:	robot.elf # robot-ws.elf tcp-test.elf ifttt.elf blink.elf test.elf

CFLAGS=-mcmm -Os -Wall -m32bit-doubles -I $(HOME)/simple/include -L $(HOME)/simple/lib

%.elf:	%.c cmd.o
	propeller-elf-gcc $(CFLAGS) -o $@ $< cmd.o -lsimple-cmm -lm

%.o:	%.c
	propeller-elf-gcc -c $(CFLAGS) -o $@ $<

run:	$(TARGET)
	proploader $< -t

clean:
	rm -f *.o *.elf
