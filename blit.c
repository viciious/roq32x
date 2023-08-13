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

unsigned blit_roqframe_normal(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height, unsigned buf_incr, const short breakval)
{
    unsigned x, y;
    unsigned char* pb = ppb;
    const uint8_t *rlut = (uint8_t *)rgblut + 128;
    const uint8_t *glut = rlut + 128*32;
    const int8_t *blut = (const int8_t *)glut + 128*32;

    for (y = start_y; y < height; y += 2)
    {
        if (start_y == 0 && MARS_SYS_COMM4 == breakval) break;

        for (x = 0; x < width; x += 2)
        {
            unsigned u, v;
            int8_t uv;
            const uint8_t *r, *g;
            const int8_t *b;
            int32_t *d, pix;
            int8_t y, *py;

            u = pb[0];
            v = pb[1];

            // FIXME: why U and V are swapped here?
            uv = ((unsigned)(0.344136 * 0x2000/*0x10000>>3*/) * v + (unsigned)(0.714136 * 0x2000) * u) >> 16;
            u = u >> 3;
            v = v >> 3;

            r = &rlut[u*128];
            g = &glut[uv*128];
            b = &blut[v*128];

            d = (int32_t *)pbuf;
            py = (int8_t *)ppa;

            y = py[0];
            y >>= 1;
            pix = (r[y] << 8) | (g[y] << 2) | b[y];
            y = py[1];
            y >>= 1;
            pix = (pix << 16) | (r[y] << 8) | (g[y] << 2) | b[y];

            *d = pix;
            d += 160;
            py += width;

            y = py[0];
            y >>= 1;
            pix = (r[y] << 8) | (g[y] << 2) | b[y];
            y = py[1];
            y >>= 1;
            pix = (pix << 16) | (r[y] << 8) | (g[y] << 2) | b[y];
            *d = pix;

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
