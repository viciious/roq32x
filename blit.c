#include <stdlib.h>
#include "32x.h"
#include "roq.h"
#include "blit.h"

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
    unsigned short* buf[2] = { pbuf, NULL };
    unsigned char* pa[2] = { ppa, NULL };
    unsigned char* pb = ppb;

    pa[1] = pa[0] + width;
    buf[1] = buf[0] + 320;

    for (y = start_y; y < height; y += 2)
    {
        if (start_y == 0 && MARS_SYS_COMM4 == breakval) break;

        for (x = 0; x < width; x += 2)
        {
            unsigned i, j;
            int u, v;

            u = pb[0] - 128;
            v = pb[1] - 128;

            int v1436_ = v1402C_ * v + YUV_NUDGE2;
            int u1815_ = u1772C_ * u + YUV_NUDGE2;

            int v731_ = v0714C_ * v;
            int u352_ = u0344C_ * u;
            int uv_ = u352_ + v731_ - YUV_NUDGE2;

            for (i = 0; i < 2; i++)
            {
                unsigned char* py = pa[i];
                unsigned short* d = buf[i];

                for (j = 0; j < 2; j++)
                {
                    unsigned t;
                    unsigned ymul = py[j] << YUV_FIX2;

                    t = ymul + v1436_;
                    unsigned r = YUVClip8(t);

                    t = ymul - uv_;
                    unsigned g = YUVClip8(t);

                    t = ymul + u1815_;
                    unsigned b = YUVClip8(t);

                    d[j] = YUVRGB555(r, g, b);
                }
            }

            pa[0] += 2;
            pa[1] += 2;
            buf[0] += 2;
            buf[1] += 2;
            pb += 2;
        }

        pa[0] += width;
        pa[1] += width;

        buf[0] += buf_incr;
        buf[1] += buf_incr;
    }

    return y + 2;
}

unsigned blit_roqframe_stretch_x2(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height, unsigned buf_incr, const short breakval)
{
    unsigned x, y;
    unsigned short* buf[2] = { pbuf, NULL };
    unsigned char* pa[2] = { ppa, NULL };
    unsigned char* pb = ppb;

    pa[1] = pa[0] + width;
    buf[1] = buf[0] + 320;

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

            for (i = 0; i < 2; i++)
            {
                unsigned char* py = pa[i];
                unsigned short* d = buf[i];

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
            }

            pa[0] += 2;
            pa[1] += 2;
            buf[0] += 4;
            buf[1] += 4;
            pb += 2;
        }

        pa[0] += width;
        pa[1] += width;

        buf[0] += buf_incr;
        buf[1] += buf_incr;
    }

    return y + 2;
}

unsigned blit_roqframe_downsampled(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height, unsigned buf_incr, const short breakval)
{
    unsigned x, y;
    unsigned short* buf[2] = { pbuf, NULL };
    unsigned char* pa[2] = { ppa, NULL };
    unsigned char* pb = ppb;

    pa[1] = pa[0] + width;
    buf[1] = buf[0] + 320;

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
            buf[0][0] = rgb;
            buf[0][1] = rgb;
            buf[1][0] = rgb;
            buf[1][1] = rgb;

            pa[0] += 2;
            pa[1] += 2;
            buf[0] += 2;
            buf[1] += 2;
            pb += 2;
        }

        pa[0] += width;
        pa[1] += width;

        buf[0] += buf_incr;
        buf[1] += buf_incr;
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
