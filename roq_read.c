/* ------------------------------------------------------------------------
 * Id Software's RoQ video file format decoder
 *
 * Dr. Tim Ferguson, 2001.
 * For more details on the algorithm:
 *         http://www.csse.monash.edu.au/~timf/videocodec.html
 *
 * This is a simple decoder for the Id Software RoQ video format.  In
 * this format, audio samples are DPCM coded and the video frames are
 * coded using motion blocks and vector quantisation.
 *
 * Note: All information on the RoQ file format has been obtained through
 *   pure reverse engineering.  This was achieved by giving known input
 *   audio and video frames to the roq.exe encoder and analysing the
 *   resulting output text and RoQ file.  No decompiling of the Quake III
 *   Arena game was required.
 *
 * You may freely use this source code.  I only ask that you reference its
 * source in your projects documentation:
 *       Tim Ferguson: http://www.csse.monash.edu.au/~timf/
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "roq.h"
#include "roqbase.h"
#include "sound.h"

#define DBUG	0

/* -------------------------------------------------------------------------- */

static void roq_bufferdata_dummy(roq_file* fp, int readhead) RoQ_ATTR_SDRAM;
static void roq_on_first_frame(roq_info* ri);

static void roq_bufferdata_dummy(roq_file* fp, int readhead)
{
	fp->rover = fp->pos;
}

static inline int roq_fgetc(roq_file* fp) {
	fp->pos++;
	return *fp->rover++;
}

static inline int roq_fgetsc(roq_file* fp) {
	fp->pos++;
	return *(int8_t *)fp->rover++;
}

static inline int roq_ftell(roq_file* fp) {
	return fp->pos - fp->base;
}

static inline int roq_feof(roq_file* fp) {
	return fp->pos >= fp->end;
}

static int roq_fseek(roq_file* fp, intptr_t whence, int mode) {
	switch (mode) {
	case SEEK_CUR:
		fp->pos += whence;
		break;
	case SEEK_SET:
		fp->pos = fp->base + whence;
		break;
	case SEEK_END:
		fp->pos = fp->end;
		break;
	}
	return 0;
}

static inline unsigned short get_word(roq_file* fp)
{
	unsigned short ret;
	ret = (fp->rover[0]);
	ret |= (fp->rover[1]) << 8;
	fp->rover += 2;
	fp->pos += 2;
	return ret;
}


/* -------------------------------------------------------------------------- */
static inline unsigned int get_long(roq_file* fp)
{
	unsigned int ret;
	ret = (fp->rover[0]);
	ret |= (fp->rover[1]) << 8;
	ret |= (fp->rover[2]) << 16;
	ret |= (fp->rover[3]) << 24;
	fp->rover += 4;
	fp->pos += 4;
	return ret;
}


/* -------------------------------------------------------------------------- */
static int roq_parse_file(roq_file* fp, roq_info* ri, int refresh_rate)
{
	int i;
	unsigned int head1, head2;
	unsigned framerate;
	unsigned chunk_id, chunk_size;
	uint8_t *start_rover;

	head1 = get_word(fp);
	head2 = get_long(fp);
	framerate = get_word(fp);
	if (head1 != 0x1084 && head2 != 0xffffffff)
	{
		//printf("Not an RoQ file.\n");
		return 1;
	}

	ri->snd_sqr_arr = ri->snd_sqr_arr_ + 128;
	for (i = 0; i < 128; i++)
	{
		ri->snd_sqr_arr[(int8_t)i] = i * i;
		ri->snd_sqr_arr[(int8_t)(i + 128)] = -(i * i);
	}

	ri->framerate = (refresh_rate * 0x10000 / framerate) >> 16;
	ri->roq_start = roq_ftell(fp);
	start_rover = fp->rover;
	while (!roq_feof(fp))
	{
		unsigned next_chunk;
		uint8_t *buf;

		ri->buffer(fp, 2 + 4 + 2);

		chunk_id = get_word(fp);
		chunk_size = get_long(fp);
		roq_fgetc(fp);
		roq_fgetc(fp);
		next_chunk = roq_ftell(fp) + chunk_size;

		ri->buffer(fp, chunk_size);
		ri->frame_bytes += chunk_size;

		buf = fp->rover;

		if (chunk_id == RoQ_INFO)		/* video info */
		{
			ri->buffer(fp, chunk_size);

			ri->width = get_word(fp);
			ri->height = get_word(fp);
			ri->display_height = ri->height;
			if (ri->width*ri->display_height > RoQ_MAX_WIDTH*RoQ_MAX_HEIGHT)
				ri->display_height = (RoQ_MAX_WIDTH*RoQ_MAX_HEIGHT/ri->width) & ~15;
		}

		fp->rover = buf + chunk_size;
		roq_fseek(fp, next_chunk, SEEK_CUR);

		if (chunk_id == RoQ_INFO)
			break;
	}

	if (roq_feof(fp))
	{
		// no info chunk
		return 1;
	}

	fp->rover = start_rover;
	roq_fseek(fp, ri->roq_start, SEEK_SET);

	return 0;
}

/* -------------------------------------------------------------------------- */
static inline void apply_motion_4x4(roq_info* ri, unsigned x, unsigned y, unsigned char mv, char mean_x, char mean_y)
{
	int i, mx, my;
	short* pa;
	unsigned char *pb;
	short* pc, * pd;
	unsigned w = ri->width, hw = w /2;

	mx = x + 8 - (mv / 16) - mean_x;
	my = y + 8 - (mv & 0xf) - mean_y;

	pa = (short*)(ri->y[0] + y * w + x);
	pb = ri->y[1] + my * w + mx;

	if (mx & 1)
	{
		for (i = 0; i < 4; i++)
		{
			*pa = (pb[0] << 8) | pb[1];
			*(pa + 1) = (pb[2] << 8) | pb[3];
			pa = (short *)((char *)pa + w);
			pb = pb + w;
		}
	}
	else 
	{
		for (i = 0; i < 4; i++)
		{
			*pa = *((short *)pb);
			*(pa + 1) = *((short *)pb + 1);
			pa = (short *)((char *)pa + w);
			pb = pb + w;
		}
	}

	pc = (short*)(ri->uv[0] + y * hw + x);
	pd = (short*)ri->uv[1] + (my / 2) * hw + (mx + 1) / 2;
	for (i = 0; i < 2; i++)
	{
		*pc = *pd;
		*(pc + 1) = *(pd + 1);
		pc += hw;
		pd += hw;
	}

}

/* -------------------------------------------------------------------------- */
static inline void apply_motion_8x8(roq_info* ri, unsigned x, unsigned y, unsigned char mv, char mean_x, char mean_y)
{
	int mx, my, i;
	short* pa;
	
	short* pc, * pd;
	unsigned w = ri->width, hw = w / 2;

	mx = x + 8 - (mv / 16) - mean_x;
	my = y + 8 - (mv & 0xf) - mean_y;

	pa = (short*)(ri->y[0] + y * w + x);

	if (mx & 1) {
		unsigned char *pb = ri->y[1] + (my * w) + mx;
		for (i = 0; i < 8; i++)
		{
			*pa = (pb[0] << 8) | pb[1];
			*(pa + 1) = (pb[2] << 8) | pb[3];
			*(pa + 2) = (pb[4] << 8) | pb[5];
			*(pa + 3) = (pb[6] << 8) | pb[7];
			pa = (short *)((char *)pa + w);
			pb = pb + w;
		}
	} else {
		short *pb = (short*)(ri->y[1] + (my * w) + mx);
		for (i = 0; i < 8; i++)
		{
			*pa = *(pb);
			*(pa + 1) = *(pb + 1);
			*(pa + 2) = *(pb + 2);
			*(pa + 3) = *(pb + 3);
			pa = (short *)((char *)pa + w);
			pb = (short *)((char *)pb + w);
		}
	}

	pc = (short*)((char *)ri->uv[0] + y * hw + x);
	pd = (short*)ri->uv[1] + (my / 2) * hw + (mx + 1) / 2;
	for (i = 0; i < 4; i++)
	{
		*pc = *pd;
		*(pc + 1) = *(pd + 1);
		*(pc + 2) = *(pd + 2);
		*(pc + 3) = *(pd + 3);
		pc += hw;
		pd += hw;
	}
}

static void roq_on_first_frame(roq_info* ri)
{
	int i;

	for (i = 0; i < 2; i++)
	{
		ri->y[i] = ri->y256[i];
		ri->uv[i] = ri->uv256[i];

		{
			memset(ri->y[i], 0, RoQ_MAX_WIDTH * RoQ_MAX_HEIGHT);
			memset(ri->uv[i], 0, RoQ_MAX_WIDTH * RoQ_MAX_HEIGHT / 4 * 2);

#if 0
			int j;
			int* pi;

			pi = (int*)ri->y[i];
			for (j = 0; j < RoQ_MAX_WIDTH * RoQ_MAX_HEIGHT / 4; j++) {
				pi[j] = 0;
			}

			pi = (int*)ri->uv[i];
			for (j = 0; j < RoQ_MAX_WIDTH * RoQ_MAX_HEIGHT / 4 * 2 / 4; j++) {
				pi[j] = 0;
			}
#endif
		}
	}

	ri->frame_num = 0;
}

/* -------------------------------------------------------------------------- */
roq_info* roq_open(roq_file* fp, roq_bufferdata_t buf, int refresh_rate)
{
	roq_info* ri;
	static roq_info ris;

	ri = &ris;
	ri->fp = fp;
	ri->buffer = buf ? buf : roq_bufferdata_dummy;

	if (roq_parse_file(fp, ri, refresh_rate)) return NULL;	
	roq_on_first_frame(ri);

	return ri;
}


/* -------------------------------------------------------------------------- */

typedef int (*roq_applier)(roq_info* ri, unsigned x, unsigned y, char* buf);

static int roq_apply_mot(roq_info* ri, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;
static int roq_apply_fcc(roq_info* ri, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;
static int roq_apply_sld(roq_info* ri, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;
static int roq_apply_cc(roq_info* ri, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;

static int roq_apply_fcc2(roq_info* ri, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;
static int roq_apply_sld2(roq_info* ri, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;
static int roq_apply_cc2(roq_info* ri, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;

roq_applier appliers[] = { &roq_apply_mot, &roq_apply_fcc, &roq_apply_sld, &roq_apply_cc };
roq_applier appliers2[] = { &roq_apply_mot, &roq_apply_fcc2, &roq_apply_sld2, &roq_apply_cc2 };

void roq_close(roq_info* ri)
{
}

static inline int roq_read_vqid(roq_info* ri, unsigned char* buf, unsigned* pvqid)
{
	int adv = 0;
	unsigned vqid;

#ifdef MARS
	if (ri->vqflg_pos == 0)
	{	
		ri->vqflg = ((buf[1] << 8) | buf[0]) << 16;
		ri->vqflg_pos = 8;
		adv = 2;
	}

	__asm volatile(
		"shll %1\n\t"
		"movt r0\n\t"
		"shll %1\n\t"
		"movt %0\n\t"
		"add r0, r0\n\t"
		"add r0, %0\n\t"
		: "=r"(vqid), "+r"(ri->vqflg) : : "r0"
	);
#else
	if (ri->vqflg_pos == 0)
	{
		unsigned shf;
		unsigned qfl = buf[0] | (buf[1] << 8);

		ri->vqflg = 0;
		for (shf = 0; shf < 16; shf += 2) {
			ri->vqflg <<= 2;
			ri->vqflg |= (qfl >> shf) & 0x3;
		}

		ri->vqflg_pos = 8;
		adv = 2;
	}

	vqid = ri->vqflg & 0x3;
	ri->vqflg >>= 2;
#endif

	*pvqid = vqid;
	ri->vqflg_pos--;

	return adv;
}

static int roq_apply_mot(roq_info* ri, unsigned x, unsigned y, char* buf)
{
	return 0;
}

static int roq_apply_fcc(roq_info* ri, unsigned x, unsigned y, char* buf)
{
	apply_motion_8x8(ri, x, y, buf[0], ri->chunk_arg1, ri->chunk_arg0);
	return 1;
}

static int roq_apply_sld(roq_info* ri, unsigned x, unsigned y, char* buf)
{
	int i, j;
	int *yptr;
	int * uvptr;
	unsigned width = ri->width;
	roq_qcell *qcell = ri->qcells + (uint8_t)buf[0];

	yptr = (int*)(ri->y[0] + y*width + x);
	uvptr = (int*)(ri->uv[0] + y*width/2 + x);

	for (i = 0; i < 4; i += 2)
	{
		roq_cell *cell0 = ri->cells + qcell->idx[i];
		roq_cell *cell1 = ri->cells + qcell->idx[i+1];

		for (j = 0; j < 4; j += 2)
		{
			int y0 = ((uint8_t)cell0->y0123[j] << 8) | (uint8_t)cell0->y0123[j];
			y0 = (y0 << 16) | ((uint8_t)cell0->y0123[j+1] << 8) | (uint8_t)cell0->y0123[j+1];
			int y1 = ((uint8_t)cell1->y0123[j] << 8) | (uint8_t)cell1->y0123[j];
			y1 = (y1 << 16) | ((uint8_t)cell1->y0123[j+1] << 8) | (uint8_t)cell1->y0123[j+1];
			int uv0 = (cell0->uv << 16) | (uint16_t)cell0->uv;
			int uv1 = (cell1->uv << 16) | (uint16_t)cell1->uv;

			yptr[0] = y0, yptr[1] = y1, yptr = (void*)((char *)yptr + width);
			yptr[0] = y1, yptr[1] = y1, yptr = (void*)((char *)yptr + width);
			uvptr[0] = uv0, uvptr[1] = uv1, uvptr = (void*)((char *)uvptr + width);
		}
	}

	return 1;
}

static int roq_apply_cc(roq_info* ri, unsigned xp, unsigned yp, char* buf)
{
	int k;
	unsigned x, y;
	int bpos = 0;

	for (k = 0; k < 4; k++)
	{
		unsigned vqid;

		x = xp; y = yp;
		if (k & 0x01) x += 4;
		if (k & 0x02) y += 4;

		bpos += roq_read_vqid(ri, (uint8_t *)&buf[bpos], &vqid);

		bpos += appliers2[vqid](ri, x, y, &buf[bpos]);
	}

	return bpos;
}

static int roq_apply_fcc2(roq_info* ri, unsigned x, unsigned y, char* buf)
{
	apply_motion_4x4(ri, x, y, buf[0], ri->chunk_arg1, ri->chunk_arg0);
	return 1;
}

static int roq_apply_sld2(roq_info* ri, unsigned x, unsigned y, char* buf)
{
	roq_apply_cc2(ri, x, y, (char *)ri->qcells[(uint8_t)buf[0]].idx);
	return 1;
}

static int roq_apply_cc2(roq_info* ri, unsigned x, unsigned y, char* buf)
{
	int i;
	int *yptr;
	int *uvptr;
	unsigned width = ri->width;

	yptr = (int *)((char *)ri->y[0] + y*width + x);
	uvptr = (int*)(ri->uv[0] + y*width/2 + x);

	for (i = 0; i < 4; i += 2)
	{
		roq_cell *cell0 = ri->cells + (uint8_t)buf[i];
		roq_cell *cell1 = ri->cells + (uint8_t)buf[i+1];
		int y0 = (cell0->y02[0] << 16) | (uint16_t)cell1->y02[0];
		int y1 = (cell0->y02[1] << 16) | (uint16_t)cell1->y02[1];
		int uv = (cell0->uv << 16) | (uint16_t)cell1->uv;

		yptr[0] = y0, yptr = (void*)((char *)yptr + width);
		yptr[0] = y1, yptr = (void*)((char *)yptr + width);
		uvptr[0] = uv, uvptr = (void*)((char *)uvptr + width);
	}

	return 4;
}

/* -------------------------------------------------------------------------- */
int roq_read_frame(roq_info* ri, char loop)
{
	int i, nv1, nv2;
	roq_file* fp = ri->fp;
	unsigned int chunk_id = 0, chunk_arg0 = 0, chunk_arg1 = 0;
	unsigned long chunk_size = 0;
	unsigned long next_chunk;
	unsigned vqid, bpos, xpos, ypos, xp, yp;
	unsigned char* tp, * buf;

	ri->frame_bytes = 0;

loop_start:
	while (!roq_feof(fp))
	{
		ri->buffer(fp, 2 + 4 + 2);

		chunk_id = get_word(fp);
		chunk_size = get_long(fp);
		chunk_arg0 = roq_fgetc(fp);
		chunk_arg1 = roq_fgetc(fp);
		next_chunk = roq_ftell(fp) + chunk_size;

		ri->buffer(fp, chunk_size);
		ri->frame_bytes += chunk_size;

		buf = fp->rover;

		if (chunk_id == RoQ_QUAD_VQ)
			break;

		switch (chunk_id)
		{
		case RoQ_QUAD_CODEBOOK:
			if ((nv1 = chunk_arg1) == 0) nv1 = 256;
			if ((nv2 = chunk_arg0) == 0 && nv1 * 6 < chunk_size) nv2 = 256;

			memcpy(ri->cells, fp->rover, nv1*6);
			fp->rover += nv1*6;

			memcpy(ri->qcells, fp->rover, nv2*4);
			fp->rover += nv2*4;
			break;

		case RoQ_SOUND_MONO:
		{
			int j = 0;
			uint16_t* p;
			int snd_left;
			int total_samples = chunk_size;

			snd_left = (int16_t)((chunk_arg1 << 8) | chunk_arg0);

			for (i = 0; i < total_samples; )
			{
				int num_samples = total_samples - i;
				if (num_samples > 256)
					num_samples = 256;

				p = snddma_get_buf_mono(num_samples);
				if (!p)
					break;

				for (j = 0; j < num_samples; j++)
				{
					snd_left += ri->snd_sqr_arr[roq_fgetsc(fp)];
					if (snd_left < -32768) snd_left = -32768;
					else if (snd_left >  32767) snd_left =  32767;

					*p++ = s16pcm_to_u16pwm(snd_left);
				}

				snddma_submit();

				i += num_samples;
			}
		}
			break;

		case RoQ_SOUND_STEREO:
		{
			int j = 0;
			uint16_t *p;
			int snd_left, snd_right;
			int total_samples = chunk_size / 2;

			snd_left = (int16_t)(chunk_arg0 << 8);
			snd_right = (int16_t)(chunk_arg1 << 8);

			for (i = 0; i < total_samples; )
			{
				int num_samples = total_samples - i;
				if (num_samples > 256)
					num_samples = 256;

				p = snddma_get_buf_stereo(num_samples);
				if (!p)
					break;

				for (j = 0; j < num_samples; j++)
				{
					snd_left += ri->snd_sqr_arr[roq_fgetsc(fp)];
					if (snd_left < -32768) snd_left = -32768;
					else if (snd_left >  32767) snd_left =  32767;

					snd_right += ri->snd_sqr_arr[roq_fgetsc(fp)];
					if (snd_right < -32768) snd_right = -32768;
					else if (snd_right >  32767) snd_right =  32767;

					*p++ = s16pcm_to_u16pwm(snd_left);
					*p++ = s16pcm_to_u16pwm(snd_right);
				}

				snddma_submit();

				i += num_samples;
			}
		}
		break;
		}

		fp->rover = buf + chunk_size;
		roq_fseek(fp, next_chunk, SEEK_SET);
	}

	if (roq_feof(fp))
	{
		snddma_wait();
		if (loop) {
			roq_fseek(fp, ri->roq_start, SEEK_SET);
			roq_on_first_frame(ri);
			goto loop_start;
		}
		return 0;
	}

	if (chunk_id != RoQ_QUAD_VQ)
	{
		return 0;
	}

	ri->frame_num++;

	buf = fp->rover;

	ri->chunk_arg0 = chunk_arg0;
	ri->chunk_arg1 = chunk_arg1;

	ri->vqflg = 0;
	ri->vqflg_pos = 0;

	vqid = RoQ_ID_MOT;

	bpos = xpos = ypos = 0;
	while (bpos < chunk_size)
	{
		for (yp = ypos; yp < ypos + 16; yp += 8)
		{
			for (xp = xpos; xp < xpos + 16; xp += 8)
			{
				bpos += roq_read_vqid(ri, &buf[bpos], &vqid);
				bpos += appliers[vqid](ri, xp, yp, (char *)&buf[bpos]);
				if (bpos >= chunk_size)
					break;
			}
		}

		xpos += 16;
		if (xpos >= ri->width)
		{
			xpos -= ri->width;
			ypos += 16;
		}

		if (ypos >= ri->height) break;
	}

	if (ri->frame_num == 1)
	{
		memcpy(ri->y[1], ri->y[0], ri->width * ri->height);
		memcpy(ri->uv[1], ri->uv[0], (ri->width * ri->height) / 4 * 2);
/*
		{
			unsigned j, l;
			int* di, *si;

			di = (int*)ri->y[1];
			si = (int*)ri->y[0];
			l = (ri->width * ri->height) / 4;
			for (j = 0; j < l; j++) {
				di[j] = si[j];
			}

			di = (int*)ri->uv[1];
			si = (int*)ri->uv[0];
			l = ((ri->width * ri->height) / 4) * 2 / 4;
			for (j = 0; j < l; j++) {
				di[j] = si[j];
			}
		}
*/
	}
	else
	{
		tp = ri->y[0]; ri->y[0] = ri->y[1]; ri->y[1] = tp;
		tp = ri->uv[0]; ri->uv[0] = ri->uv[1]; ri->uv[1] = tp;
	}

	fp->rover = buf + chunk_size;
	roq_fseek(fp, next_chunk, SEEK_SET);
	return 1;
}
