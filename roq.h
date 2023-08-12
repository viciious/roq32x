#ifndef _av_roq_h
#define _av_roq_h

#include <stdint.h>

#define RoQ_INFO				0x1001
#define RoQ_QUAD_CODEBOOK		0x1002
#define RoQ_QUAD_VQ				0x1011
#define RoQ_SOUND_MONO			0x1020
#define RoQ_SOUND_STEREO		0x1021

#define RoQ_ID_MOT		0x00
#define RoQ_ID_FCC		0x01
#define RoQ_ID_SLD		0x02
#define RoQ_ID_CCC		0x03

#define RoQ_SAMPLE_RATE    22050

#define RoQ_MAX_WIDTH	320
#define RoQ_MAX_HEIGHT	192

#ifdef __32X__
#define RoQ_ATTR_SDRAM  __attribute__((section(".data"), aligned(16)))
#else
#define RoQ_ATTR_SDRAM 
#endif

typedef struct {
	union {
		short y02[2];
		char y0123[4];
	};
	short uv;
} roq_cell;

typedef struct {
	unsigned char idx[4];
} roq_qcell;

typedef struct {
	unsigned char *pos;
	unsigned char* rover;
	intptr_t size;
	unsigned char *base;
	unsigned char* end;
	int page;
} roq_file;

typedef void (*roq_bufferdata_t)(roq_file*, int readahead);

typedef struct {
	roq_file *fp;
	int buf_size;
	roq_cell cells[256];
	roq_qcell qcells[256];
	short snd_sqr_arr_[260];
	short *snd_sqr_arr;
	long roq_start;
	unsigned width, height, frame_num;
	unsigned display_height;
	unsigned char *y[2], *uv[2];
	unsigned char y256[2][RoQ_MAX_WIDTH * RoQ_MAX_HEIGHT] __attribute__((aligned(16)));
	unsigned char uv256[2][RoQ_MAX_WIDTH * RoQ_MAX_HEIGHT / 4 * 2] __attribute__((aligned(16)));
	unsigned int frame_bytes;
	roq_bufferdata_t buffer;

	unsigned chunk_arg0, chunk_arg1;
	unsigned vqflg;
	unsigned vqflg_pos;
	unsigned framerate;
} roq_info;

/* -------------------------------------------------------------------------- */

void roq_init(void);
void roq_cleanup(void);
roq_info *roq_open(roq_file *fp, int max_height, roq_bufferdata_t buf, int refresh_rate);
void roq_close(roq_info *ri);
int roq_read_video(roq_info *ri, char loop);
int roq_read_audio(roq_info *ri, char loop);

int roq_read_frame(roq_info* ri, char loop)
	RoQ_ATTR_SDRAM
	;

#endif

