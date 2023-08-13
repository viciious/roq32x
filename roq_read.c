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
static void roq_on_first_frame(roq_info* ri) RoQ_ATTR_SDRAM;

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
static int roq_parse_file(roq_file* fp, roq_info* ri, int max_height, int refresh_rate)
{
	int i;
	unsigned int head1, head2;
	unsigned framerate;
	unsigned chunk_id, chunk_size;

	head1 = get_word(fp);
	head2 = get_long(fp);
	framerate = get_word(fp);
	if (head1 != 0x1084 && head2 != 0xffffffff)
	{
		//printf("Not an RoQ file.\n");
		return 1;
	}

	ri->cells = ri->cells_ + 128;
	ri->qcells = ri->qcells_ + 128;

	ri->snd_sqr_arr = ri->snd_sqr_arr_ + 128;
	for (i = 0; i < 128; i++)
	{
		ri->snd_sqr_arr[(int8_t)i] = i * i;
		ri->snd_sqr_arr[(int8_t)(i + 128)] = -(i * i);
	}

	ri->framerate = (refresh_rate * 0x10000 / framerate) >> 16;
	ri->roq_start = roq_ftell(fp);
	while (!roq_feof(fp))
	{
		ri->buffer(fp, 2 + 4 + 2);

		chunk_id = get_word(fp);
		chunk_size = get_long(fp);
		get_word(fp);
		if (roq_feof(fp)) break;

		if (chunk_id == RoQ_INFO)		/* video info */
		{
			ri->buffer(fp, chunk_size);

			ri->width = get_word(fp);
			ri->height = get_word(fp);
			ri->halfwidth = ri->width >> 1;
			ri->halfheight = ri->height >> 1;
			ri->display_height = ri->height;
			if (ri->display_height > max_height) ri->display_height = max_height;
			get_word(fp);
			get_word(fp);
			break;
		}
		else
		{
			roq_fseek(fp, chunk_size, SEEK_CUR);
		}
	}

	if (roq_feof(fp))
	{
		// no info chunk
		return 1;
	}

	return 0;
}

/* -------------------------------------------------------------------------- */

static inline void apply_vector_2x2(roq_info* ri, unsigned x, unsigned y, roq_cell* cell)
{
	short *yptr;
	short *uvptr;
	unsigned hw = y * ri->width;

	yptr = (short *)((char *)ri->y[0] + hw + x);
	*yptr++ = cell->y02[0];

	yptr += (ri->halfwidth - 1);
	*yptr++ = cell->y02[1];

	uvptr = (short *)&ri->uv[0][((hw>>1) + x)];
	*uvptr++ = cell->uv;
}


/* -------------------------------------------------------------------------- */
static inline void apply_vector_4x4(roq_info* ri, unsigned x, unsigned y, roq_cell* cell)
{
	unsigned row_inc, c_row_inc;
	short y0, y1, * yptr;
	short uv, * uvptr;
	unsigned yw = y * ri->width;

	yptr = (short*)(ri->y[0] + yw + x);
	uvptr = (short*)ri->uv[0] + (yw >> 2) + (x >> 1);

	row_inc = (ri->width - 4) >> 1;
	c_row_inc = ri->halfwidth - 2;

	y0 = (uint8_t)cell->y0123[0];
	y0 |= (y0 << 8);

	uv = cell->uv;

	*yptr++ = y0, * uvptr++ = uv;

	y1 = (uint8_t)cell->y0123[1];
	y1 |= (y1 << 8);
	*yptr++ = y1, * uvptr++ = uv;

	yptr += row_inc;

	*yptr++ = y0;
	*yptr++ = y1;

	yptr += row_inc, uvptr += c_row_inc;

	y0 = (uint8_t)cell->y0123[2];
	y0 |= (y0 << 8);
	*yptr++ = y0, * uvptr++ = uv;

	y1 = (uint8_t)cell->y0123[3];
	y1 |= (y1 << 8);
	*yptr++ = y1, * uvptr++ = uv;

	yptr += row_inc;

	*yptr++ = y0;
	*yptr++ = y1;
}

/* -------------------------------------------------------------------------- */
static inline void apply_motion_4x4(roq_info* ri, unsigned x, unsigned y, unsigned char mv, char mean_x, char mean_y)
{
	int i, mx, my;
	short* pa;
	short* pc, * pd;
	unsigned w, hw;
	unsigned x2, y2;
	unsigned mx2, my2;

	mx = x + 8 - (mv / 16) - mean_x;
	my = y + 8 - (mv & 0xf) - mean_y;

	w = ri->width;
	hw = ri->halfwidth;

	y2 = y * hw;
	x2 = (x >> 1);

	mx2 = (mx + 1) / 2;
	my2 = (my / 2) * hw;

	pa = (short*)ri->y[0] + y2 + x2;
	if (mx & 1) {
		unsigned char*pb = ri->y[1] + (my * w) + mx;
		for (i = 0; i < 4; i++)
		{
			*pa = (pb[0] << 8) | pb[1];
			*(pa + 1) = (pb[2] << 8) | pb[3];
			pa += hw;
			pb += w;
		}
	} else {
		short *pb = (short *)(ri->y[1] + (my * w) + mx);
		for (i = 0; i < 4; i++)
		{
			*pa = *pb;
			*(pa + 1) = *(pb + 1);
			pa += hw;
			pb += hw;
		}
	}

	pc = (short*)ri->uv[0] + (y2 >> 1) + x2;
	pd = (short*)ri->uv[1] + my2 + mx2;
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
	unsigned w, hw;
	unsigned x2, y2;
	unsigned mx2, my2;

	mx = x + 8 - (mv / 16) - mean_x;
	my = y + 8 - (mv & 0xf) - mean_y;

	w = ri->width;
	hw = ri->halfwidth;

	y2 = y * hw;
	x2 = (x >> 1);

	mx2 = (mx + 1) / 2;
	my2 = (my / 2) * (hw);

	pa = (short*)ri->y[0] + y2 + x2;

	if (mx & 1) {
		unsigned char *pb = ri->y[1] + (my * w) + mx;
		for (i = 0; i < 8; i++)
		{
			*pa = (pb[0] << 8) | pb[1];
			*(pa + 1) = (pb[2] << 8) | pb[3];
			*(pa + 2) = (pb[4] << 8) | pb[5];
			*(pa + 3) = (pb[6] << 8) | pb[7];
			pa += hw;
			pb += w;
		}
	} else {
		short *pb = (short*)(ri->y[1] + (my * w) + mx);
		for (i = 0; i < 8; i++)
		{
			*pa = *pb;
			*(pa + 1) = *(pb + 1);
			*(pa + 2) = *(pb + 2);
			*(pa + 3) = *(pb + 3);
			pa += hw;
			pb += hw;
		}
	}

	pc = (short*)ri->uv[0] + (y2 >> 1) + x2;
	pd = (short*)ri->uv[1] + my2 + mx2;
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
			int j;
			int* pi;

			//memset(ri->y[i], 0, RoQ_MAX_WIDTH * RoQ_MAX_HEIGHT);
			//memset(ri->uv[i], 0, RoQ_MAX_WIDTH * RoQ_MAX_HEIGHT / 4 * 2);

			pi = (int*)ri->y[i];
			for (j = 0; j < RoQ_MAX_WIDTH * RoQ_MAX_HEIGHT / 4; j++) {
				pi[j] = 0;
			}

			pi = (int*)ri->uv[i];
			for (j = 0; j < RoQ_MAX_WIDTH * RoQ_MAX_HEIGHT / 4 * 2 / 4; j++) {
				pi[j] = 0;
			}
		}
	}

	ri->frame_num = 0;
}

/* -------------------------------------------------------------------------- */
roq_info* roq_open(roq_file* fp, int max_height, roq_bufferdata_t buf, int refresh_rate)
{
	roq_info* ri;
	static roq_info ris;

	ri = &ris;
	ri->fp = fp;
	ri->buffer = buf ? buf : roq_bufferdata_dummy;

	if (max_height > RoQ_MAX_HEIGHT) max_height = RoQ_MAX_HEIGHT;
	if (roq_parse_file(fp, ri, max_height, refresh_rate)) return NULL;
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

static inline int roq_read_vqid(roq_info* ri, unsigned char* buf, unsigned* vqid)
{
	int adv = 0;

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

	*vqid = ri->vqflg & 0x3;
	ri->vqflg >>= 2;
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
	roq_qcell* qcell;
	qcell = ri->qcells + buf[0];
	apply_vector_4x4(ri, x, y, ri->cells + qcell->idx[0]);
	apply_vector_4x4(ri, x + 4, y, ri->cells + qcell->idx[1]);
	apply_vector_4x4(ri, x, y + 4, ri->cells + qcell->idx[2]);
	apply_vector_4x4(ri, x + 4, y + 4, ri->cells + qcell->idx[3]);
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
	roq_qcell* qcell;
	qcell = ri->qcells + buf[0];
	apply_vector_2x2(ri, x, y, ri->cells + qcell->idx[0]);
	apply_vector_2x2(ri, x + 2, y, ri->cells + qcell->idx[1]);
	apply_vector_2x2(ri, x, y + 2, ri->cells + qcell->idx[2]);
	apply_vector_2x2(ri, x + 2, y + 2, ri->cells + qcell->idx[3]);
	return 1;
}

static int roq_apply_cc2(roq_info* ri, unsigned x, unsigned y, char* buf)
{
	apply_vector_2x2(ri, x, y, ri->cells + (int8_t)buf[0]);
	apply_vector_2x2(ri, x + 2, y, ri->cells + (int8_t)buf[1]);
	apply_vector_2x2(ri, x, y + 2, ri->cells + (int8_t)buf[2]);
	apply_vector_2x2(ri, x + 2, y + 2, ri->cells + (int8_t)buf[3]);
	return 4;
}

/* -------------------------------------------------------------------------- */
int roq_read_frame(roq_info* ri, char loop)
{
	int i, j, nv1, nv2;
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

			for (i = 0; i < nv1; i++)
			{
				j = (int8_t)i;
				ri->cells[j].y0123[0] = roq_fgetsc(fp);
				ri->cells[j].y0123[1] = roq_fgetsc(fp);
				ri->cells[j].y0123[2] = roq_fgetsc(fp);
				ri->cells[j].y0123[3] = roq_fgetsc(fp);
				ri->cells[j].uv = (roq_fgetc(fp)<<8) | roq_fgetc(fp);
			}

			for (i = 0; i < nv2; i++)
			{
				for (j = 0; j < 4; j++)
					ri->qcells[(int8_t)i].idx[j] = roq_fgetsc(fp);
			}
			break;
		case RoQ_SOUND_MONO:
		{
			int j = 0;
			uint16_t* p;
			int16_t snd_left;
			int total_samples = chunk_size;

			snd_left = (chunk_arg1 << 8) | chunk_arg0;

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
			int16_t snd_left, snd_right;
			int total_samples = chunk_size / 2;

			snd_left = chunk_arg0 << 8;
			snd_right = chunk_arg1;

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
					snd_right += ri->snd_sqr_arr[roq_fgetsc(fp)];
					*p++ = s16pcm_to_u16pwm(snd_left);
					*p++ = s16pcm_to_u16pwm(snd_left);
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
		//memcpy(ri->y[1], ri->y[0], ri->width * ri->height);
		//memcpy(ri->uv[1], ri->uv[0], (ri->width * ri->height) / 4 * 2);

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
