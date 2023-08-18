#include "roq.h"

#define BLIT_STRETCH_WIDTH_X2 160

unsigned blit_roqframe_normal(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height, unsigned pitch, const short breakval)
    RoQ_ATTR_SDRAM
    ;

unsigned blit_roqframe_stretch_x2(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height, unsigned pitch, const short breakval)
    RoQ_ATTR_SDRAM
    ;

unsigned blit_roqframe_downsampled(unsigned start_y, unsigned short* pbuf,
    unsigned char* ppa, unsigned char* ppb, unsigned width, unsigned height, unsigned pitch, const short breakval)
    RoQ_ATTR_SDRAM
    ;

unsigned blit_roqframe(roq_info* ri, int blit_mode, unsigned frame, unsigned y_start, unsigned height, 
    unsigned breakval, unsigned stretch)
    RoQ_ATTR_SDRAM
    ;
