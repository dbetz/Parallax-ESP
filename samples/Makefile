#TARGET=robot
TARGET=robot-ws

CFLAGS=-mcmm -Os -m32bit-doubles -I include -L lib

$(TARGET).elf:	$(TARGET).c
	propeller-elf-gcc $(CFLAGS) -o $@ $< -lsimple-cmm -lm

run:	$(TARGET).elf
	proploader -v $< -t

clean:
	rm -f $(TARGET).elf
