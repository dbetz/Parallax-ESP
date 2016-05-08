CFLAGS=-mcmm -Os -m32bit-doubles -I include -L lib

robot.elf:	robot.c
	propeller-elf-gcc $(CFLAGS) -o $@ $< -lsimple-cmm -lm

run:	robot.elf
	proploader robot.elf -t

clean:
	rm -f robot.elf
