#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    int flashSize, size;
    FILE *ifp, *ofp;
    char *image;

    if (argc < 3 || argc > 4) {
        printf("usage: patch in-file out-file [1M|2M|4M]\n");
        return 1;
    }

    if (argc < 4)
        flashSize = SIZE_16M;
    else {
        if (strcmp(argv[3], "1M") == 0)
            flashSize = SIZE_8M;
        else if (strcmp(argv[3], "2M") == 0)
            flashSize = SIZE_16M;
        else if (strcmp(argv[3], "4M") == 0)
            flashSize = SIZE_32M;
        else {
            printf("error: unsupported flash size '%s'\n", argv[3]);
            return 1;
        }
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
    image[3] = flashSize | FREQ_80M;

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
