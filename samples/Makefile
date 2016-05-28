$(info HOME $(HOME))

TARGET=robot.elf

all:	robot.elf robot-ws.elf tcp-test.elf ifttt.elf

CFLAGS=-mcmm -Os -m32bit-doubles -I $(HOME)/simple/include -L $(HOME)/simple/lib

%.elf:	%.c sscp-client.o
	propeller-elf-gcc $(CFLAGS) -o $@ $< sscp-client.o -lsimple-cmm -lm

%.o:	%.c
	propeller-elf-gcc -c $(CFLAGS) -o $@ $<

run:	$(TARGET)
	proploader $< -t

clean:
	rm -f *.o *.elf
