#include <stdlib.h>
#include "32x.h"
#include "roq.h"
#include "blit.h"
#include "luts.h"

#define RMASK ((1<<5)-1)
#define GMASK (((1<<10)-1) & ~RMASK)
#define BMASK (((1<<15)-1) & ~(RMASK|GMASK))

#define YUVClip8(v) (__builtin_expect(v & ~YUV_MASK2, 0) ? (__builtin_expect((int)v < 0, 0) ? 0 : YUV_MASK2) : v)
#define YUVRGB555(r,g,b) ((((((r)) >> (10+(YUV_FIX2-7))))) | (((((g)) >> (5+(YUV_FIX2-7)))) & GMASK) | (((((b)) >> (0+(YUV_FIX2-7)))) & BMASK))

const int YUV_FIX2 = 7;                   // fixed-point precision for YUV->RGB
const int YUV_MUL2 = (1 << YUV_FIX2);
const int YUV_NUDGE2 = (1 << (YUV_FIX2 - 1));
const int YUV_MASK2 = (256 << YUV_FIX2) - 1;

const int v1402C_ = 1.402000 * YUV_MUL2;
const int v0714C_ = 0.714136 * YUV_MUL2;

const int u0344C_ = 0.344136 * YUV_MUL2;
const int u1772C_ = 1.772000 * YUV_MUL2;

#if 0
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

    for (j = 0; j < 32; j++) {
        uint8_t *row = &rlut[j*256] + 128;
        for (i = 0; i < 256; i++) {
            int v = j << 3;
            v -= 128;
            int p = i + 1.140 * v;
            if (p < 0) p = 0;
            if (p > 255) p = 255;
            row[(int8_t)i] = (p >> 3) & 31;
        }
    }

    for (i = 0; i < 32; i++) {
        uint8_t *row = &uvlut[i*32];
        for (j = 0; j < 32; j++) {
            int u = i<<3;
            int v = j<<3;
            int p = 0.344136 * u + 0.714136 * v;
            if (p < 0) p = 0;
            if (p > 255) p = 255;
            row[j] = (p+7)>>3;
        }
    }

    for (j = 0; j < 32; j++) {
        uint8_t *row = &glut[j*256] + 128;
        for (i = 0; i < 256; i++) {
            int uv = (j<<3);
            int p = i - uv + 128;
            if (p < 0) p = 0;
            if (p > 255) p = 255;
            row[(int8_t)i] = (p >> 3) & 31;
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

#endif

unsigned blit_roqframe_normal(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height, unsigned buf_incr, const short breakval)
{
    unsigned x, y;
    unsigned char* pb = ppb;
    const uint8_t *rlut = (uint8_t *)rgblut + 128;
    const uint8_t *glut = rlut + 256*32;
    const int8_t *blut = (const int8_t *)glut + 256*32;

    for (y = start_y; y < height; y += 2)
    {
        if (start_y == 0 && MARS_SYS_COMM4 == breakval) break;

        for (x = 0; x < width; x += 2)
        {
            unsigned u, v;
            int8_t uv;
            const uint8_t *r, *g;
            const int8_t *b;
            int16_t *d;
            int8_t y, *py;

            u = pb[0];
            v = pb[1];

            u = u >> 3;
            v = v >> 3;
            uv = uvlut[u*32+v];

            r = &rlut[u*256];
            g = &glut[uv*256];
            b = &blut[v*256];

            d = (int16_t *)pbuf;
            py = (int8_t *)ppa;

            y = py[0];
            d[0] = (r[y] << 8) | (g[y] << 2) | b[y];
            y = py[1];
            d[1] = (r[y] << 8) | (g[y] << 2) | b[y];

            d += 320;
            py += width;

            y = py[0];
            d[0] = (r[y] << 8) | (g[y] << 2) | b[y];
            y = py[1];
            d[1] = (r[y] << 8) | (g[y] << 2) | b[y];

            ppa += 2;
            pbuf += 2;
            pb += 2;
        }

        ppa += width;
        pbuf += buf_incr;
    }

    return y + 2;
}

unsigned blit_roqframe_stretch_x2(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height, unsigned buf_incr, const short breakval)
{
    unsigned x, y;
    unsigned char* pa[2] = { ppa, NULL };
    unsigned char* pb = ppb;

    pa[1] = pa[0] + width;

    for (y = start_y; y < height; y += 2)
    {
        if (start_y == 0 && MARS_SYS_COMM4 == breakval) break;

        for (x = 0; x < width; x += 2)
        {
            unsigned i, j, k;
            int u, v;

            u = pb[0] - 128;
            v = pb[1] - 128;

            unsigned v1436_ = v1402C_ * v + YUV_NUDGE2;
            unsigned v731_ = v0714C_ * v + YUV_NUDGE2;

            unsigned u352_ = u0344C_ * u;
            unsigned u1815_ = u1772C_ * u;
            unsigned uv_ = u352_ + v731_ - YUV_NUDGE2;

            unsigned short* d = pbuf;

            for (i = 0; i < 2; i++)
            {
                unsigned char* py = pa[i];

                for (j = 0, k = 0; j < 2; j++, k += 2)
                {
                    unsigned t;
                    unsigned ymul = py[j] << YUV_FIX2;

                    t = ymul + v1436_;
                    unsigned r = YUVClip8(t);

                    t = ymul - uv_;
                    unsigned g = YUVClip8(t);

                    t = ymul + u1815_;
                    unsigned b = YUVClip8(t);

                    unsigned val = YUVRGB555(r, g, b);

                    d[k + 0] = val;
                    d[k + 1] = val;
                }

                d += 320;
            }

            pa[0] += 2;
            pa[1] += 2;
            pbuf += 4;
            pb += 2;
        }

        pa[0] += width;
        pa[1] += width;
        pbuf += buf_incr;
    }

    return y + 2;
}

unsigned blit_roqframe_downsampled(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height, unsigned buf_incr, const short breakval)
{
    unsigned x, y;
    unsigned char* pa[2] = { ppa, NULL };
    unsigned char* pb = ppb;

    pa[1] = pa[0] + width;

    for (y = start_y; y < height; y += 2)
    {
        if (start_y == 0 && MARS_SYS_COMM4 == breakval) break;

        for (x = 0; x < width; x += 2)
        {
            int u, v;
            unsigned int avgy;

            u = pb[0] - 128;
            v = pb[1] - 128;

            unsigned v1436_ = v1402C_ * v;
            unsigned v731_ = v0714C_ * v;

            unsigned u352_ = u0344C_ * u;
            unsigned u1815_ = u1772C_ * u;

            unsigned uv_ = u352_ + v731_;

            avgy = pa[0][0] + pa[0][1] + pa[1][0] + pa[1][1];
            avgy >>= 2;

            unsigned t;
            unsigned ymul = avgy * YUV_MUL2 + YUV_NUDGE2;

            t = ymul + v1436_;
            unsigned r = YUVClip8(t);

            t = ymul - uv_;
            unsigned g = YUVClip8(t);

            t = ymul + u1815_;
            unsigned b = YUVClip8(t);

            unsigned rgb = YUVRGB555(r, g, b);
            pbuf[0] = rgb;
            pbuf[1] = rgb;
            pbuf[320+0] = rgb;
            pbuf[320+1] = rgb;

            pa[0] += 2;
            pa[1] += 2;
            pbuf += 2;
            pb += 2;
        }

        pa[0] += width;
        pa[1] += width;
        pbuf += buf_incr;
    }

    return y + 2;
}

unsigned blit_roqframe(roq_info* ri, int blit_mode, unsigned frame, unsigned y_start, unsigned height, short breakval)
{
    unsigned short* buf;
    unsigned y;
    unsigned stretch;
    unsigned char* pa = ri->y[frame];
    unsigned char* pb = ri->uv[frame];
    unsigned width = ri->width;
    unsigned buf_incr;

    stretch = 1;
    if (width <= BLIT_STRETCH_WIDTH_X2)
        stretch = 2;

    buf = (unsigned short *)&MARS_FRAMEBUFFER;
    buf += 0x100;

    y = y_start;
    pa += y_start * width;
    pb += (y_start >> 1) * width;

    buf_incr = 640 - width * stretch;

    buf += y_start * 320;
    if (ri->display_height < 200)
        buf += (200 - ri->display_height * stretch) / 2 * 320;
    if (width < 320)
        buf += (320 - width * stretch) / 2;

    if (stretch == 2)
        return blit_roqframe_stretch_x2(y, buf, pa, pb, width, height, buf_incr, breakval);

    switch (blit_mode)
    {
    case 1:
        return blit_roqframe_downsampled(y, buf, pa, pb, width, height, buf_incr, breakval);
    default:
        return blit_roqframe_normal(y, buf, pa, pb, width, height, buf_incr, breakval);
    }
}
