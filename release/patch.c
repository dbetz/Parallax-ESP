#include <stdio.h>
#include <stdlib.h>

#define MODE_QIO        0x00
#define MODE_QOUT       0x01
#define MODE_DIO        0x02
#define MODE_DOUT       0x03

#define SIZE_4M         0x00
#define SIZE_2M         0x10
#define SIZE_8M         0x20
#define SIZE_16M        0x30
#define SIZE_32M        0x40

#define FREQ_40M        0x00
#define FREQ_26M        0x01
#define FREQ_20M        0x02
#define FREQ_80M        0x0f

int main(int argc, char *argv[])
{
    FILE *ifp, *ofp;
    char *image;
    int size;

    if (argc != 3) {
        printf("usage: patch in-file out-file\n");
        return 1;
    }

    if (!(ifp = fopen(argv[1], "rb"))) {
        printf("error: can't open %s\n", argv[2]);
        return 1;
    }

    fseek(ifp, 0, SEEK_END);
    size = ftell(ifp);
    fseek(ifp, 0, SEEK_SET);

    if (!(image = malloc(size))) {
        printf("error: insufficient memory\n");
        return 1;
    }

    if (fread(image, 1, size, ifp) != size) {
        printf("error: reading %s\n", argv[1]);
        return 1;
    }

    fclose(ifp);

    image[2] = MODE_QIO;
    image[3] = SIZE_32M | FREQ_80M;

    if (!(ofp = fopen(argv[2], "wb"))) {
        printf("error: can't create %s\n", argv[3]);
        return 1;
    }

    if (fwrite(image, 1, size, ofp) != size) {
        printf("error: writing %s\n", argv[2]);
        return 1;
    }

    fclose(ofp);

    return 0;
}
