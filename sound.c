#include <stdint.h>
#include "32x.h"
#include "sound.h"
#include "mars_ringbuf.h"

marsrb_t soundbuf;
static unsigned samples[2][MAX_SAMPLES];
static unsigned snd_flip = 0;
static int snd_channels = 1;
static int snd_left, snd_right;
static short *snd_sqr_arr;

void sec_dma1_handler(void) SND_ATTR_SDRAM;
static void sec_dma_center(void) SND_ATTR_SDRAM;

int8_t* snddma_get_buf(int channels, int num_samples, int left, int right, short *snd_sqr_arr) {
    int16_t* p;

    p = (int16_t*)Mars_RB_GetWriteBuf(&soundbuf, 8, 1);
    if (!p)
        return NULL;
    *p++ = channels;
    *p++ = num_samples;
    *p++ = left;
    *p++ = right;
    *(uintptr_t *)p = (uintptr_t)snd_sqr_arr;
    Mars_RB_CommitWrite(&soundbuf);

    return (int8_t*)Mars_RB_GetWriteBuf(&soundbuf, (num_samples * channels + 1) / 2, 1);
}

int8_t* snddma_get_buf_mono(int num_samples, int left, short *snd_sqr_arr) {
    return snddma_get_buf(1, num_samples, left, left, snd_sqr_arr);
}

int8_t* snddma_get_buf_stereo(int num_samples, int left, int right, short *snd_sqr_arr) {
    return snddma_get_buf(2, num_samples, left, right, snd_sqr_arr);
}

void snddma_wait(void) {
    Mars_RB_WaitReader(&soundbuf, 0);
}

void snddma_submit(void) {
    Mars_RB_CommitWrite(&soundbuf);
}

unsigned snddma_length(void)
{
    return Mars_RB_Len(&soundbuf);
}

static void sec_dma_center(void)
{
    int i;
    uint16_t *s;

    s = (uint16_t *)samples[snd_flip];
    for (i = 0; i < MAX_SAMPLES; i++)
        *s++ = SAMPLE_CENTER;
    snd_channels = 1;
}

static void sec_dma_kickstart(void)
{
    snd_flip = 0;
    sec_dma_center();
    snd_flip = 1;

    SH2_DMA_SAR1 = (intptr_t)samples[0];
    SH2_DMA_TCR1 = MAX_SAMPLES;
    SH2_DMA_DAR1 = 0x20004038; // storing a word here will the MONO channel
    SH2_DMA_CHCR1 = 0x14e5; // dest fixed, src incr, size word, ext req, dack mem to dev, dack hi, dack edge, dreq rising edge, cycle-steal, dual addr, intr enabled, clear TE, dma enabled
}

void sec_dma1_handler(void)
{
    int i;

    SH2_DMA_CHCR1; // read TE
    SH2_DMA_CHCR1 = 0; // clear TE

    SH2_DMA_SAR1 = (uintptr_t)samples[snd_flip];
    SH2_DMA_TCR1 = MAX_SAMPLES;

    if (snd_channels == 2)
    {
        SH2_DMA_DAR1 = 0x20004034; // storing a long here will set left and right
        SH2_DMA_CHCR1 = 0x18e5; // dest fixed, src incr, size long, ext req, dack mem to dev, dack hi, dack edge, dreq rising edge, cycle-steal, dual addr, intr enabled, clear TE, dma enabled
    }
    else
    {
        SH2_DMA_DAR1 = 0x20004038; // storing a word here will set the MONO channel
        SH2_DMA_CHCR1 = 0x14e5; // dest fixed, src incr, size word, ext req, dack mem to dev, dack hi, dack edge, dreq rising edge, cycle-steal, dual addr, intr enabled, clear TE, dma enabled
    }

    snd_flip = (snd_flip + 1) % (sizeof(samples) / sizeof(samples[0]));

    if (Mars_RB_Len(&soundbuf) <= 8) {
        sec_dma_center();
        return;
    }

    int16_t *p = (int16_t *)Mars_RB_GetReadBuf(&soundbuf, 8, 1);
    int num_channels = *p++;
    int num_samples = *p++;
    int new_snd_left = *p++;
    int new_snd_right = *p++;
    short *new_snd_sqr_arr = (short *)(*((uintptr_t *)p));
    Mars_RB_CommitRead(&soundbuf);

    if (new_snd_sqr_arr)
    {
        snd_sqr_arr = new_snd_sqr_arr;
        snd_left = new_snd_left;
        snd_right = new_snd_right;
    }
    snd_channels = num_channels;

    int8_t *b = (int8_t *)Mars_RB_GetReadBuf(&soundbuf, (num_samples * num_channels + 1) / 2, 1);
    uint16_t *s = (uint16_t *)samples[snd_flip];

    if (num_channels == 1)
    {
        for (i = 0; i < num_samples; i++)
        {
            snd_left += snd_sqr_arr[*b++];
            if (snd_left < -32768) snd_left = -32768;
            else if (snd_left >  32767) snd_left =  32767;
            *s++ = s16pcm_to_u16pwm(snd_left);
        }
    }
    else
    {
        for (i = 0; i < num_samples; i++)
        {
            snd_left += snd_sqr_arr[*b++];
            if (snd_left < -32768) snd_left = -32768;
            else if (snd_left >  32767) snd_left =  32767;
            *s++ = s16pcm_to_u16pwm(snd_left);

            snd_right += snd_sqr_arr[*b++];
            if (snd_right < -32768) snd_right = -32768;
            else if (snd_right >  32767) snd_right =  32767;
            *s++ = s16pcm_to_u16pwm(snd_right);
        }

    }

    Mars_RB_CommitRead(&soundbuf);

    if (num_channels == 1)
    {
        for (; i < MAX_SAMPLES; i++)
            *s++ = SAMPLE_CENTER;
    }
    else
    {
        for (; i < MAX_SAMPLES; i++)
        {
            *s++ = SAMPLE_CENTER;
            *s++ = SAMPLE_CENTER;
        }
    }
}

void snddma_sec_init(int sample_rate)
{
    uint16_t sample, ix;

    Mars_RB_ResetRead(&soundbuf);

    // init DMA
    SH2_DMA_SAR0 = 0;
    SH2_DMA_DAR0 = 0;
    SH2_DMA_TCR0 = 0;
    SH2_DMA_CHCR0 = 0;
    SH2_DMA_DRCR0 = 0;
    SH2_DMA_SAR1 = 0;
    SH2_DMA_DAR1 = 0x20004038; // storing a word here will the MONO channel
    SH2_DMA_TCR1 = 0;
    SH2_DMA_CHCR1 = 0;
    SH2_DMA_DRCR1 = 0;
    SH2_DMA_DMAOR = 1; // enable DMA

    SH2_DMA_VCR1 = 66; 	// set exception vector for DMA channel 1
    SH2_INT_IPRA = (SH2_INT_IPRA & 0xF0FF) | 0x0400; // set DMA INT to priority 4

    // init the sound hardware
    MARS_PWM_MONO = 1;
    MARS_PWM_MONO = 1;
    MARS_PWM_MONO = 1;
    if (MARS_VDP_DISPMODE & MARS_NTSC_FORMAT)
        MARS_PWM_CYCLE = (((23011361 << 1) / (sample_rate) + 1) >> 1) + 1; // for NTSC clock
    else
        MARS_PWM_CYCLE = (((22801467 << 1) / (sample_rate) + 1) >> 1) + 1; // for PAL clock
    MARS_PWM_CTRL = 0x0185; // TM = 1, RTP, RMD = right, LMD = left

    sample = SAMPLE_MIN;

    // ramp up to SAMPLE_CENTER to avoid click in audio (real 32X)
    while (sample < SAMPLE_CENTER)
    {
        for (ix = 0; ix < (sample_rate * 2) / (SAMPLE_CENTER - SAMPLE_MIN); ix++)
        {
            while (MARS_PWM_MONO & 0x8000); // wait while full
            MARS_PWM_MONO = sample;
        }
        sample++;
    }

    sec_dma_kickstart();
}

void snddma_init(int sample_rate)
{
    Mars_RB_ResetAll(&soundbuf);
}
