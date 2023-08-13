#include <stdint.h>

#define SAMPLE_MIN      2
#define SAMPLE_MAX      1032
#define SAMPLE_CENTER   (SAMPLE_MAX-SAMPLE_MIN)/2

#ifdef __32X__
#define SND_ATTR_SDRAM  __attribute__((section(".data"), aligned(16)))
#else
#define SND_ATTR_SDRAM 
#endif

void snddma_submit(void) SND_ATTR_SDRAM;
uint16_t* snddma_get_buf(int channels, int num_samples) SND_ATTR_SDRAM;
uint16_t* snddma_get_buf_mono(int num_samples) SND_ATTR_SDRAM;
uint16_t* snddma_get_buf_stereo(int num_samples) SND_ATTR_SDRAM;

static inline uint16_t s16pcm_to_u16pwm(int s) {
    return SAMPLE_MIN + ((unsigned)(s+32768) >> 6);
}

void snddma_sec_init(int sample_rate);
void snddma_init(int sample_rate);
void sec_dma_kickstart(void);
unsigned snddma_length(void)SND_ATTR_SDRAM;
void snddma_wait(void) SND_ATTR_SDRAM;
