#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

static uint8_t uvlut[32*32];
static uint8_t rgblut[256*32*3];

int binit = 0;
void init(void)
{
    int i, j;
    uint8_t *rlut = rgblut;
    uint8_t *glut = rlut + 256*32;
    int8_t *blut = (int8_t *)glut + 256*32;

    if (binit)
        return;
    binit = 1;

    for (i = 0; i < 32; i++) {
        uint8_t *row = &uvlut[i*32];
        for (j = 0; j < 32; j++) {
            int u = i<<3;
            int v = j<<3;
            int p = 0.344136 * u + 0.714136 * v;
            if (p < 0) p = 0;
            if (p > 255) p = 255;
            row[j] = p>>3;
        }
    }

    for (j = 0; j < 32; j++) {
        uint8_t *row = &rlut[j*256] + 128;
        for (i = 0; i < 256; i++) {
            int v = j << 3;
            v -= 128;
            int p = i + 1.140 * v;
            if (p < 0) p = 0;
            if (p > 255) p = 255;
            row[(int8_t)i] = ((p >> 3) & 31) * 4;
        }
    }

    for (j = 0; j < 32; j++) {
        uint8_t *row = &glut[j*256] + 128;
        for (i = 0; i < 256; i++) {
            int uv = (j<<3);
            int p = i - uv + 128;
            if (p < 0) p = 0;
            if (p > 255) p = 255;
            row[(int8_t)i] = ((p >> 3) & 31) * 2;
        }
    }

    for (j = 0; j < 32; j++) {
        int8_t *row = &blut[j*256] + 128;
        for (i = 0; i < 256; i++) {
            int u = j << 3;
            u -= 128;
            int p = i + 1.772000 * u;
            if (p < 0) p = 0;
            if (p > 255) p = 255;
            row[(int8_t)i] = (p >> 3) & 31;
        }
    }
}

int main(int argc, char **argv) {
    int i, j, k;

    init();

    printf("#include <stdint.h>\n");
    printf("\n");
    printf("static const uint8_t uvlut[32*32] = {\n");
    for (i = 0; i < 32; i++) {
        uint8_t *row = &uvlut[i*32];

        for (j = 0; j < 32; j++) {
            printf("0x%02x%s", row[j], (i == 31 && j == 31 ? "" : ","));
        }
        printf("\n");
    }
    printf("};\n");

    printf("static const uint8_t rgblut[256*32*3] = {\n");
    for (k = 0; k < 3; k++) {
        uint8_t *lut = &rgblut[256*32*k];

        for (i = 0; i < 32; i++) {
            uint8_t *row = &lut[i*256];

            for (j = 0; j < 256; j++) {
                printf("0x%02x%s", row[j], (i == 31 && j == 255 && k == 2 ? "" : ","));
                if (j % 32 == 31) {
                    printf("\n");
                }
            }
            //printf("\n");
        }
    }
    printf("};\n");
}