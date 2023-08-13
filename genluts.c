#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

static uint8_t rgblut[128*32*3];

int binit = 0;
void init(void)
{
    int i, j;
    uint8_t *rlut = rgblut;
    uint8_t *glut = rlut + 128*32;
    int8_t *blut = (int8_t *)glut + 128*32;

    if (binit)
        return;
    binit = 1;

    for (j = 0; j < 32; j++) {
        uint8_t *row = &rlut[j*128] + 128;
        for (i = 0; i < 128; i++) {
            int y = i << 1;
            int v = j << 3;
            v -= 128;
            int p = y + 1.140 * v;
            p += 3;            
            if (p < 0) p = 0;
            if (p > 255) p = 255;
            row[(int8_t)i] = (p >> 3) * 4;
        }
    }

    for (j = 0; j < 32; j++) {
        uint8_t *row = &glut[j*128] + 128;
        for (i = 0; i < 128; i++) {
            int y = i << 1;
            int uv = (j<<3);
            int p = y - uv + 128;
            p += 3;
            if (p < 0) p = 0;
            if (p > 255) p = 255;
            row[(int8_t)i] = (p >> 3) * 8;
        }
    }

    for (j = 0; j < 32; j++) {
        int8_t *row = &blut[j*128] + 128;
        for (i = 0; i < 128; i++) {
            int y = i << 1;
            int u = j << 3;
            u -= 128;
            int p = y + 1.772000 * u;
            p += 3;
            if (p < 0) p = 0;
            if (p > 255) p = 255;
            row[(int8_t)i] = (p >> 3);
        }
    }
}

int main(int argc, char **argv) {
    int i, j, k;

    init();

    printf("#include <stdint.h>\n");
    printf("\n");
    printf("static const uint8_t rgblut[128*32*3] = {\n");
    for (k = 0; k < 3; k++) {
        uint8_t *lut = &rgblut[128*32*k];

        for (i = 0; i < 32; i++) {
            uint8_t *row = &lut[i*128];

            for (j = 0; j < 128; j++) {
                printf("0x%02x%s", row[j], (i == 32-1 && j == 128-1 && k == 2 ? "" : ","));
                if (j % 32 == 31) {
                    printf("\n");
                }
            }
            //printf("\n");
        }
    }
    printf("};\n");
}