#include <stdlib.h>
#include "32x.h"
#include "hw_32x.h"
#include "roq.h"
#include "roqbase.h"
#include "sound.h"
#include "blit.h"

roq_info* gri;
static roq_file grf;

static int blit_mode = 0;

int secondary_task(int cmd) RoQ_ATTR_SDRAM;
void secondary(void) RoQ_ATTR_SDRAM;

static void unswitch_ROMbanks(roq_file* fp) RoQ_ATTR_SDRAM;
static void switch_ROMbanks(roq_file* fp, int readhead) RoQ_ATTR_SDRAM;

// Slave SH2 support code ----------------------------------------------

int secondary_task(int cmd)
{
    switch (cmd) {
    case 1:
        snddma_sec_init(RoQ_SAMPLE_RATE);
        return 1;
    case 2:
        Hw32xScreenFlip(1);

        ClearCache();

        MARS_SYS_COMM6 = blit_roqframe(gri, blit_mode, 1, 0, gri->display_height, 0xff);
        return 1;
    case 3:
    {
        int y;
        int height;

        y = MARS_SYS_COMM6;
        //ClearCache();
        MARS_SYS_COMM4 = 0;

        height = (gri->display_height + y) / 2 + 1;
        height &= ~1;

        blit_roqframe(gri, blit_mode, 0, y, height, 0xfe);
    }
    return 0;

    case 0xff:
        return 1;

    default:
        break;
    }
 
    return 0;
}

void secondary(void)
{
    ClearCache();

    while (1) {
        int cmd;

        while ((cmd = MARS_SYS_COMM4) == 0) {}

        int res = secondary_task(cmd);
        if (res > 0) {
            MARS_SYS_COMM4 = 0;
        }
    }
}

void display(int framecount, int hudenable, int fpscount, int readtics, int totaltics, int bps, int maxbps, int clearhud)
{
    int y = 0;
    int height = 0;

    if (framecount == 0) {
        goto nextframe;
    }

    MARS_SYS_COMM4 = 0xff;
    while (MARS_SYS_COMM4 != 0);

    y = MARS_SYS_COMM6 - 2;
    if (y < 0) y = 0;
    if (y > gri->display_height) y = gri->display_height;

    if (y < gri->display_height)
    {
        MARS_SYS_COMM6 = y;
        MARS_SYS_COMM4 = 3;

        height = (gri->display_height - y) / 2 + 1;
        height &= ~1;
        blit_roqframe(gri, blit_mode, 0, y + height, gri->display_height, 0xfe);
    }

    if (clearhud) {
        Hw32xScreenClearLine(23);
    }

    Hw32xScreenSetXY(0, 23);

    switch (hudenable) {
    case 1:
        Hw32xScreenPrintf("fps:%02d r:%02d t:%02d sdma:%02dkb kbps:%03d",
            fpscount, readtics, totaltics,
            (snddma_length() * 2) >> 10, bps);
        break;
    case 2:
        Hw32xScreenPrintf("%dx%d maxbps:%03d 0x%x 0x%x",
            gri->width, gri->height, maxbps, gri->fp->pos, gri->fp->rover);
        break;
    default:
        break;
    }

    while (MARS_SYS_COMM4 != 0);

nextframe:
    MARS_SYS_COMM6 = 0;
    MARS_SYS_COMM4 = 2;
}

static void unswitch_ROMbanks(roq_file* fp)
{
    Hw32xSetBankPage(6, 6);
    Hw32xSetBankPage(7, 7);
    ClearCache();
}

static void switch_ROMbanks(roq_file* fp, int readhead)
{
    uintptr_t ptr = (uintptr_t)fp->pos;
    int page = (ptr - 0x02000000) >> 19;
    int nextpage = (ptr + readhead - 0x02000000) >> 19;

    if (nextpage > 7)
    {
        if (nextpage > fp->page)
        {
            Hw32xSetBankPage(6, page);
            Hw32xSetBankPage(7, nextpage);
            ClearCache();
            fp->rover = (uint8_t*)(0x02300000 + (ptr & 0x7FFFF));
        }
    }
    else
    {
        if (fp->page > 7)
        {
            unswitch_ROMbanks(fp);
            fp->rover = fp->pos;
        }
    }

    fp->page = page;
}

roq_file* open_ROMroq(void)
{
    roq_file* fp = &grf;

    fp->base = roqBase;
    fp->pos = roqBase;
    fp->rover = roqBase;
    fp->size = roqSize;
    fp->end = fp->base + roqSize;
    fp->page = 0;
    return &grf;
}

void close_ROMroq(void)
{
    if (grf.page > 7)
    {
        unswitch_ROMbanks(&grf);
    }
}

int main(void)
{
    int i, j;
    int framecount;
    int fpscount;
    int prevsec;
    int prevsecframe;
    int inputticcount;
    int readtics;
    int totaltics;
    char paused = 0, hud = 0, clearhud = 0;
    unsigned bytesread, bps, maxbps;
    int refresh_rate;

    SetSH2SR(1);

    SH2_WDT_WTCSR_TCNT = 0x5A00; /* WDT TCNT = 0 */
    SH2_WDT_WTCSR_TCNT = 0xA53E; /* WDT TCSR = clr OVF, IT mode, timer on, clksel = Fs/4096 */

/* init hires timer system */
    SH2_WDT_VCR = (65 << 8) | (SH2_WDT_VCR & 0x00FF); // set exception vector for WDT
    SH2_INT_IPRA = (SH2_INT_IPRA & 0xFF0F) | 0x0020; // set WDT INT to priority 2

    Hw32xInit(MARS_VDP_MODE_32K, 0);

    Hw32xScreenClear();

    snddma_init(RoQ_SAMPLE_RATE);

    // init sound on slave
    MARS_SYS_COMM4 = 1;
    MARS_SYS_COMM6 = 0;
    while (MARS_SYS_COMM4 != 0) {}

    refresh_rate = MARS_VDP_DISPMODE & MARS_NTSC_FORMAT ? 60 : 50;

    if ((gri = roq_open(open_ROMroq(), 200, switch_ROMbanks, refresh_rate)) == NULL) return -1;

    blit_mode = 0;

    totaltics = 0;
    inputticcount = 0;

    while (1)
    {
start:
        fpscount = 0;
        framecount = 0;

        prevsec = 0;
        prevsecframe = 0;

        bytesread = 0;
        bps = 0;
        maxbps = 0;

        if (gri->width <= BLIT_STRETCH_WIDTH_X2)
        {
            volatile unsigned short* lines = &MARS_FRAMEBUFFER;

            for (i = 0; i < 2; i++)
            {
                for (j = 0; j < 256; j++)
                {
                    if (j < 180)
                        lines[j] = (j / 2) * 320 + 0x100;
                    else if (j < 200)
                        lines[j] = j * 320 + 0x100;
                    else
                        lines[j] = 199 * 320 + 0x100;
                }

                Hw32xScreenClear();
            }
        }

        while (1) {
            int sec;
            int ret = 1;
            int starttics;
            int waittics;

            starttics = Hw32xGetTicks();

            sec = starttics / (MARS_VDP_DISPMODE & MARS_NTSC_FORMAT ? 60 : 50); // FIXME: add proper NTSC vs PAL rate detection
            if (sec != prevsec) {
                fpscount = (framecount - prevsecframe) / (sec - prevsec);
                prevsec = sec;
                prevsecframe = framecount;
                bps = bytesread >> 10;
                bytesread = 0;
                if (bps > maxbps)
                    maxbps = bps;
            }

            if (starttics > inputticcount + 7) {
                if (MARS_SYS_COMM8 & SEGA_CTRL_A) {
                    blit_mode ^= 1;
                }
                if (MARS_SYS_COMM8 & SEGA_CTRL_B) {
                    hud = (hud + 1) % 3;
                    clearhud = 2;
                }
                if (MARS_SYS_COMM8 & SEGA_CTRL_START) {
                    paused ^= 1;
                }
                inputticcount = starttics;
            }

            readtics = starttics;

            if (framecount == 0 || !paused)
            {
                ret = roq_read_frame(gri, 0);

                if (ret == 0) {
                    while (MARS_SYS_COMM4 != 0);
                    close_ROMroq();
                    if ((gri = roq_open(open_ROMroq(), 200, switch_ROMbanks, refresh_rate)) == NULL)
                        return -1;
                    goto start;
                }

                if (ret < 0)
                    return -1;
                bytesread += gri->frame_bytes;
            }

            readtics = Hw32xGetTicks() - readtics;

            display(framecount, hud, fpscount, readtics, totaltics, bps, maxbps, clearhud);

            clearhud--;
            if (clearhud < 0)
                clearhud = 0;

            totaltics = Hw32xGetTicks() - starttics;

            waittics = totaltics;
            while (waittics < gri->framerate) {
                waittics = Hw32xGetTicks() - starttics;
            }

            framecount++;
        }
    }

    return 0;
}


