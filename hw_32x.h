#ifndef HW_32X_H
#define HW_32X_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "32x.h"

#define HW32X_ATTR_SDRAM  __attribute__((section(".data"), aligned(16), optimize("O1")))

#ifdef __cplusplus
extern "C" {
#endif

extern void Hw32xSetFGColor(int s, int r, int g, int b);
extern void Hw32xSetBGColor(int s, int r, int g, int b);
extern void Hw32xInit(int vmode, int lineskip);
extern int Hw32xScreenGetX();
extern int Hw32xScreenGetY();
extern void Hw32xScreenSetXY(int x, int y);
extern void Hw32xScreenClear();
extern void Hw32xScreenPutChar(int x, int y, unsigned char ch);
extern void Hw32xScreenClearLine(int Y);
extern int Hw32xScreenPrintData(const char *buff, int size);
extern int Hw32xScreenPutsn(const char *str, int len);
extern void Hw32xScreenPrintf(const char *format, ...);
extern void Hw32xDelay(int ticks);
extern void Hw32xScreenFlip(int wait) HW32X_ATTR_SDRAM;
extern void Hw32xFlipWait(void);

extern unsigned short HwMdReadPad(int port);
extern unsigned char HwMdReadSram(unsigned short offset);
extern void HwMdWriteSram(unsigned char byte, unsigned short offset);
extern int HwMdReadMouse(int port);
extern void HwMdClearScreen(void);
extern void HwMdSetOffset(unsigned short offset);
extern void HwMdSetNTable(unsigned short word);
extern void HwMdSetVram(unsigned short word);
extern void HwMdPuts(char *str, int color, int x, int y);
extern void HwMdPutc(char chr, int color, int x, int y);
extern void HwMdPutsf(int x, int y, int color, const char* format, ...);

unsigned Hw32xGetTicks(void) HW32X_ATTR_SDRAM;
void Hw32xSetBankPage(int bank, int page) HW32X_ATTR_SDRAM;

void pri_vbi_handler(void) HW32X_ATTR_SDRAM;

#ifdef __cplusplus
}
#endif

#endif
