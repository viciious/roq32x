#include <stdlib.h>
#include "32x.h"
#include "hw_32x.h"
#include "roq.h"
#include "roqbase.h"
#include "sound.h"
#include "blit.h"

roq_info* gri;

static int blit_mode = 0;

// Slave SH2 support code ----------------------------------------------

int slave_task(int cmd)
{
    switch (cmd) {
    case 1:
        snddma_slave_init(RoQ_SAMPLE_RATE);
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
        MARS_SYS_COMM4 = 4;

        height = (gri->display_height + y) / 2 + 1;
        height &= ~1;

        blit_roqframe(gri, blit_mode, 0, y, height, 0xfe);

        while (MARS_SYS_COMM4 != 2) {}
    }
    return 0;

    case 0xff:
        return 1;

    default:
        break;
    }
 
    return 0;
}

void slave(void)
{
    ClearCache();

    while (1) {
        int cmd;

        while ((cmd = MARS_SYS_COMM4) == 0) {}

        int res = slave_task(cmd);
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
    while (MARS_SYS_COMM4 != 0) {}

    ClearCache();

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
        Hw32xScreenPrintf("%dx%d maxbps:%03d",
            gri->width, gri->height, maxbps);
        break;
    default:
        break;
    }

    while (MARS_SYS_COMM4 == 3) {}

nextframe:
    MARS_SYS_COMM6 = 0;
    MARS_SYS_COMM4 = 2;
}

roq_file* open_ROMroq(void)
{
    static roq_file grf;
    roq_file* fp = &grf;

    fp->base = roqBase;
    fp->pos = roqBase;
    fp->size = roqSize;
    fp->end = fp->base + roqSize;
    return &grf;
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
    int ticksperframe;

    Hw32xInit(MARS_VDP_MODE_32K, 0);

    Hw32xScreenClear();

    snddma_init(RoQ_SAMPLE_RATE);

    // init sound on slave
    MARS_SYS_COMM4 = 1;
    MARS_SYS_COMM6 = 0;
    while (MARS_SYS_COMM4 != 0) {}

    if ((gri = roq_open(open_ROMroq(), 200)) == NULL) return -1;

    blit_mode = 0;
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

    if (gri->width <= BLIT_STRETCH_WIDTH_X2) {
        ticksperframe = 2; // 30/25 fps
    }
    else {
        ticksperframe = (MARS_VDP_DISPMODE & MARS_NTSC_FORMAT ? 4 : 3); // 15 / 16-17fps
    }

    fpscount = 0;
    framecount = 0;

    prevsec = 0;
    prevsecframe = 0;

    bytesread = 0;
    bps = 0;
    maxbps = 0;

    totaltics = 0;
    inputticcount = 0;

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
            ret = roq_read_frame(gri, 1);
            if (ret <= 0) {
                return 1;
            }
            bytesread += gri->frame_bytes;
        }

        readtics = Hw32xGetTicks() - readtics;

        display(framecount, hud, fpscount, readtics, totaltics, bps, maxbps, clearhud);

        clearhud--;
        if (clearhud < 0)
            clearhud = 0;

        totaltics = Hw32xGetTicks() - starttics;

        waittics = totaltics;
        while (waittics < ticksperframe) {
            waittics = Hw32xGetTicks() - starttics;
        }

        framecount++;
    }

    return 0;
}


