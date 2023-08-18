#include <stdlib.h>
#include "32x.h"
#include "roq.h"
#include "blit.h"

#define RMASK ((1<<5)-1)
#define GMASK (((1<<10)-1) & ~RMASK)
#define BMASK (((1<<15)-1) & ~(RMASK|GMASK))

#define YUVClip8(v) (__builtin_expect((v) & ~YUV_MASK2, 0) ? (__builtin_expect((int)(v) < 0, 0) ? 0 : YUV_MASK2) : (v))
#define YUVRGB555(r,g,b) ((((((r)) >> (10+(YUV_FIX2-7))))) | (((((g)) >> (5+(YUV_FIX2-7)))) & GMASK) | (((((b)) >> (0+(YUV_FIX2-7)))) & BMASK))

const int YUV_FIX2 = 8;                   // fixed-point precision for YUV->RGB
const int YUV_MUL2 = (1 << YUV_FIX2);
const int YUV_NUDGE2 = /*(1 << (YUV_FIX2 - 1))*/0;
const int YUV_MASK2 = (256 << YUV_FIX2) - 1;

const int v1402C_ = 1.402000 * YUV_MUL2;
const int v0714C_ = 0.714136 * YUV_MUL2;

const int u0344C_ = 0.344136 * YUV_MUL2;
const int u1772C_ = 1.772000 * YUV_MUL2;

unsigned blit_roqframe_normal(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height, 
    unsigned pitch, const short breakval)
{
    unsigned x, y;
    unsigned char* pb = ppb;

    pitch /= 2;

    for (y = start_y; y < height; y += 2)
    {
        if (start_y == 0 && MARS_SYS_COMM4 == breakval) break;

        int* d = (int*)pbuf;

        for (x = 0; x < width; x += 2)
        {
            int u, v;

            u = pb[0] - 128;
            v = pb[1] - 128;

            int v1436_ = v1402C_ * v + YUV_NUDGE2;
            int u1815_ = u1772C_ * u + YUV_NUDGE2;

            int v731_ = v0714C_ * v;
            int u352_ = u0344C_ * u;
            int uv_ = u352_ + v731_ - YUV_NUDGE2;

            unsigned char* py = ppa;

            int pix;
            unsigned yy;
            unsigned ymul, r, g, b;

            yy = *(uint16_t *)py;
            ymul = (yy&0xff) << YUV_FIX2;
            r = YUVClip8(ymul + v1436_), g = YUVClip8(ymul - uv_), b = YUVClip8(ymul + u1815_);
            pix = YUVRGB555(r, g, b);

#if YUV_FIX2 <= 8
            ymul = (yy & ~0xff)>>(8-YUV_FIX2);
#else
            ymul = (yy & ~0xff)<<(YUV_FIX2-8);
#endif
            r = YUVClip8(ymul + v1436_), g = YUVClip8(ymul - uv_), b = YUVClip8(ymul + u1815_);
            pix |= YUVRGB555(r, g, b) << 16;
            d[0] = pix;

            yy = *((uint16_t *)(py + width));
            ymul = (yy&0xff) << YUV_FIX2;
            r = YUVClip8(ymul + v1436_), g = YUVClip8(ymul - uv_), b = YUVClip8(ymul + u1815_);
            pix = YUVRGB555(r, g, b);

#if YUV_FIX2 <= 8
            ymul = (yy & ~0xff)>>(8-YUV_FIX2);
#else
            ymul = (yy & ~0xff)<<(YUV_FIX2-8);
#endif
            r = YUVClip8(ymul + v1436_), g = YUVClip8(ymul - uv_), b = YUVClip8(ymul + u1815_);
            pix |= YUVRGB555(r, g, b) << 16;
            d[pitch] = pix;

            ppa += 2;
            d++;
            pb += 2;
        }

        ppa += width;
        pbuf += pitch*4;
    }

    return y + 2;
}

unsigned blit_roqframe_stretch_x2(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height,
    unsigned pitch, const short breakval)
{
    unsigned x, y;
    unsigned char* pb = ppb;

    pitch /= 2;

    for (y = start_y; y < height; y += 2)
    {
        if (start_y == 0 && MARS_SYS_COMM4 == breakval) break;

        int* d = (int*)pbuf;

        for (x = 0; x < width; x += 2)
        {
            int u, v;

            u = pb[0] - 128;
            v = pb[1] - 128;

            int v1436_ = v1402C_ * v + YUV_NUDGE2;
            int u1815_ = u1772C_ * u + YUV_NUDGE2;

            int v731_ = v0714C_ * v;
            int u352_ = u0344C_ * u;
            int uv_ = u352_ + v731_ - YUV_NUDGE2;

            unsigned char* py = ppa;

            unsigned pix;
            unsigned yy;
            unsigned ymul, r, g, b;

            yy = *(uint16_t *)py;
            ymul = (yy&0xff) << YUV_FIX2;
            r = YUVClip8(ymul + v1436_), g = YUVClip8(ymul - uv_), b = YUVClip8(ymul + u1815_);
            pix = YUVRGB555(r, g, b);
            pix |= (pix << 16);
            d[1] = pix;

#if YUV_FIX2 <= 8
            ymul = (yy & ~0xff)>>(8-YUV_FIX2);
#else
            ymul = (yy & ~0xff)<<(YUV_FIX2-8);
#endif
            r = YUVClip8(ymul + v1436_), g = YUVClip8(ymul - uv_), b = YUVClip8(ymul + u1815_);
            pix = YUVRGB555(r, g, b);
            pix |= (pix << 16);
            d[0] = pix;

            yy = *((uint16_t *)(py + width));
            ymul = (yy&0xff) << YUV_FIX2;
            r = YUVClip8(ymul + v1436_), g = YUVClip8(ymul - uv_), b = YUVClip8(ymul + u1815_);
            pix = YUVRGB555(r, g, b);
            pix |= (pix << 16);
            d[pitch+1] = pix;

#if YUV_FIX2 <= 8
            ymul = (yy & ~0xff)>>(8-YUV_FIX2);
#else
            ymul = (yy & ~0xff)<<(YUV_FIX2-8);
#endif
            r = YUVClip8(ymul + v1436_), g = YUVClip8(ymul - uv_), b = YUVClip8(ymul + u1815_);
            pix = YUVRGB555(r, g, b);
            pix |= (pix << 16);
            d[pitch+0] = pix;

            ppa += 2;
            d += 2;
            pb += 2;
        }

        ppa += width;
        pbuf += pitch*4;
    }

    return y + 2;
}

unsigned blit_roqframe_downsampled(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height,
    unsigned pitch, const short breakval)
{
    unsigned x, y;
    unsigned char* pa[2] = { ppa, NULL };
    unsigned char* pb = ppb;

    pa[1] = pa[0] + width;

    for (y = start_y; y < height; y += 2)
    {
        if (start_y == 0 && MARS_SYS_COMM4 == breakval) break;

        unsigned short* d = (unsigned short*)pbuf;

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
            d[0] = rgb;
            d[1] = rgb;
            d[pitch+0] = rgb;
            d[pitch+1] = rgb;

            pa[0] += 2;
            pa[1] += 2;
            d += 2;
            pb += 2;
        }

        pa[0] += width;
        pa[1] += width;
        pbuf += pitch*2;
    }

    return y + 2;
}

unsigned blit_roqframe(roq_info* ri, int blit_mode, unsigned frame, unsigned y_start, unsigned height, 
    unsigned breakval, unsigned stretch)
{
    unsigned short* buf;
    unsigned y;
    unsigned char* pa = ri->y[frame];
    unsigned char* pb = ri->uv[frame];
    unsigned width = ri->width;
    unsigned pitch;

    pitch = 160 + width * stretch / 2;

    buf = (unsigned short *)&MARS_FRAMEBUFFER;
    buf += 0x100;

    y = y_start;
    pa += y_start * width;
    pb += (y_start >> 1) * width;

    buf += y_start * pitch;
    if (width < 320)
        buf += (320 - width * stretch) / 2;

    if (stretch == 2)
        return blit_roqframe_stretch_x2(y, buf, pa, pb, width, height, pitch, breakval);

    switch (blit_mode)
    {
    case 1:
        return blit_roqframe_downsampled(y, buf, pa, pb, width, height, pitch, breakval);
    default:
        return blit_roqframe_normal(y, buf, pa, pb, width, height, pitch, breakval);
    }
}
