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
#include "32x.h"

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

	ri->viewport = ri->framebuffer;

	ri->display_height = ri->height;
	if (ri->viewport_pitch * ri->display_height < RoQ_MAX_VIEWPORT_SIZE)
	{
		if (ri->width < 320)
			ri->viewport += (320 - ri->width) / 2;
		ri->viewport = (void *)((uintptr_t)ri->viewport & ~15);
		ri->viewport_pitch = (160 + ri->width / 2 + 15) & ~15;
	}
	else
	{
		ri->viewport_pitch = ri->width;
	}
	while (ri->viewport_pitch * ri->display_height > RoQ_MAX_VIEWPORT_SIZE || ri->display_height > 224)
		ri->display_height -= 16;

	fp->rover = start_rover;
	roq_fseek(fp, ri->roq_start, SEEK_SET);

	return 0;
}

/* -------------------------------------------------------------------------- */
static inline void apply_motion_4x4(roq_parse_ctx* ctx, unsigned x, unsigned y, unsigned char mv, char mean_x, char mean_y)
{
	int mx, my, i;
	short *src, *dst;
	roq_info *ri = ctx->ri;

	mx = x + 8 - (mv / 16) - mean_x;
	my = y + 8 - (mv & 0xf) - mean_y;

	dst = ri->viewport + y * ri->viewport_pitch + x;
	src = ri->viewportcopy + my * ri->viewport_pitch + mx;

	for (i = 0; i < 4; i++)
	{
		int j;
		for (j = 0; j < 4; j++)
			dst[j] = src[j];
		src += ri->viewport_pitch;
		dst += ri->viewport_pitch;
	}
}

/* -------------------------------------------------------------------------- */
static inline void apply_motion_8x8(roq_parse_ctx* ctx, unsigned x, unsigned y, unsigned char mv, char mean_x, char mean_y)
{
	int mx, my, i;
	short *src, *dst;
	roq_info *ri = ctx->ri;

	mx = x + 8 - (mv / 16) - mean_x;
	my = y + 8 - (mv & 0xf) - mean_y;
	
	dst = ri->viewport + y * ri->viewport_pitch + x;
	src = ri->viewportcopy + my * ri->viewport_pitch + mx;

	for (i = 0; i < 8; i++)
	{
		int j;
		for (j = 0; j < 8; j++)
			dst[j] = src[j];
		src += ri->viewport_pitch;
		dst += ri->viewport_pitch;
	}
}

static void roq_on_first_frame(roq_info* ri)
{
	ri->frame_num = 0;
}

/* -------------------------------------------------------------------------- */
roq_info* roq_open(roq_file* fp, roq_bufferdata_t buf, int refresh_rate, short *framebuffer)
{
	roq_info* ri;
	static roq_info ris;

	ri = &ris;
	ri->fp = fp;
	ri->buffer = buf ? buf : roq_bufferdata_dummy;
	ri->framebuffer = framebuffer;

	if (roq_parse_file(fp, ri, refresh_rate)) return NULL;	
	roq_on_first_frame(ri);

	return ri;
}


/* -------------------------------------------------------------------------- */

#define RMASK ((1<<5)-1)
#define GMASK (((1<<10)-1) & ~RMASK)
#define BMASK (((1<<15)-1) & ~(RMASK|GMASK))

#define YUVClip8(v) (__builtin_expect((v) & ~YUV_MASK2, 0) ? (__builtin_expect((int)(v) < 0, 0) ? 0 : YUV_MASK2) : (v))
#define YUVRGB555(r,g,b) ((((((r)) >> (10+(YUV_FIX2-7))))) | (((((g)) >> (5+(YUV_FIX2-7)))) & GMASK) | (((((b)) >> (0+(YUV_FIX2-7)))) & BMASK))

#define YUV_FIX2 8                   // fixed-point precision for YUV->RGB
const int YUV_MUL2 = (1 << YUV_FIX2);
const int YUV_NUDGE2 = /*(1 << (YUV_FIX2 - 1))*/0;
const int YUV_MASK2 = (256 << YUV_FIX2) - 1;

const int v1402C_ = 1.402000 * YUV_MUL2;
const int v0714C_ = 0.714136 * YUV_MUL2;

const int u0344C_ = 0.344136 * YUV_MUL2;
const int u1772C_ = 1.772000 * YUV_MUL2;

#define yuv2rgb555(y,u,v) \
	YUVRGB555( \
		YUVClip8(((unsigned)y << YUV_FIX2) + (v1402C_ * (v-128) + YUV_NUDGE2)), \
		YUVClip8(((unsigned)y << YUV_FIX2) - (u0344C_ * (u-128) + v0714C_ * (v-128) - YUV_NUDGE2)), \
		YUVClip8(((unsigned)y << YUV_FIX2) + (u1772C_ * (u-128) + YUV_NUDGE2)) \
	)

/* -------------------------------------------------------------------------- */

typedef int (*roq_applier)(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf);

static int roq_apply_mot(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;
static int roq_apply_fcc(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;
static int roq_apply_sld(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;
static int roq_apply_cc(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;

static int roq_apply_fcc2(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;
static int roq_apply_sld2(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;
static int roq_apply_cc2(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf) RoQ_ATTR_SDRAM;

roq_applier appliers[] = { &roq_apply_mot, &roq_apply_fcc, &roq_apply_sld, &roq_apply_cc };
roq_applier appliers2[] = { &roq_apply_mot, &roq_apply_fcc2, &roq_apply_sld2, &roq_apply_cc2 };

void roq_close(roq_info* ri)
{
}

static inline int roq_read_vqid(roq_parse_ctx* ctx, unsigned char* buf, unsigned* pvqid)
{
	int adv = 0;
	unsigned vqid;

#ifdef MARS
	if (ctx->vqflg_pos == 0)
	{	
		ctx->vqflg = ((buf[1] << 8) | buf[0]) << 16;
		ctx->vqflg_pos = 8;
		adv = 2;
	}

	__asm volatile(
		"shll %1\n\t"
		"movt r0\n\t"
		"shll %1\n\t"
		"movt %0\n\t"
		"add r0, r0\n\t"
		"add r0, %0\n\t"
		: "=r"(vqid), "+r"(ctx->vqflg) : : "r0"
	);
#else
	if (ctx->vqflg_pos == 0)
	{
		unsigned shf;
		unsigned qfl = buf[0] | (buf[1] << 8);

		ctx->vqflg = 0;
		for (shf = 0; shf < 16; shf += 2) {
			ctx->vqflg <<= 2;
			ctx->vqflg |= (qfl >> shf) & 0x3;
		}

		ctx->vqflg_pos = 8;
		adv = 2;
	}

	vqid = ctx->vqflg & 0x3;
	ctx->vqflg >>= 2;
#endif

	*pvqid = vqid;
	ctx->vqflg_pos--;

	return adv;
}

static int roq_apply_mot(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf)
{
	return 0;
}

static int roq_apply_fcc(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf)
{
	apply_motion_8x8(ctx, x, y, buf[0], ctx->chunk_arg1, ctx->chunk_arg0);
	return 1;
}

static int roq_apply_sld(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf)
{
	int i;
	roq_info* ri = ctx->ri;
	unsigned pitch = ri->viewport_pitch;
	roq_qcell *qcell = ri->qcells + (uint8_t)buf[0];
	short *dst = ri->viewport + y * pitch + x;
	short *dst2 = dst + pitch;

	pitch *= 2;
	for (i = 0; i < 4; i += 2)
	{
		roq_cell *cell0 = ri->cells + qcell->idx[i];
		roq_cell *cell1 = ri->cells + qcell->idx[i+1];

		dst[0] = dst[1] = dst2[0] = dst2[1] = cell0->rgb555[0];
		dst[2] = dst[3] = dst2[2] = dst2[3] = cell1->rgb555[0];
		dst[4] = dst[5] = dst2[4] = dst2[5] = cell0->rgb555[1];
		dst[6] = dst[7] = dst2[6] = dst2[7] = cell1->rgb555[1];
		dst += pitch;
		dst2 += pitch;

		dst[0] = dst[1] = dst2[0] = dst2[1] = cell0->rgb555[2];
		dst[2] = dst[3] = dst2[2] = dst2[3] = cell1->rgb555[2];
		dst[4] = dst[5] = dst2[4] = dst2[5] = cell0->rgb555[3];
		dst[6] = dst[7] = dst2[6] = dst2[7] = cell1->rgb555[3];
		dst += pitch;
		dst2 += pitch;
	}

	return 1;
}

static int roq_apply_cc(roq_parse_ctx* ctx, unsigned xp, unsigned yp, char* buf)
{
	unsigned k;
	unsigned x, y;
	int bpos = 0;

	for (k = 0; k < 4; k++)
	{
		unsigned vqid;

		x = (k & 1) * 4;
		x += xp;

		y = (k & 2) * 2;
		y += yp;

		bpos += roq_read_vqid(ctx, (uint8_t *)&buf[bpos], &vqid);

		bpos += appliers2[vqid](ctx, x, y, &buf[bpos]);
	}

	return bpos;
}

static int roq_apply_fcc2(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf)
{
	apply_motion_4x4(ctx, x, y, buf[0], ctx->chunk_arg1, ctx->chunk_arg0);
	return 1;
}

static int roq_apply_sld2(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf)
{
	roq_apply_cc2(ctx, x, y, (char *)ctx->ri->qcells[(uint8_t)buf[0]].idx);
	return 1;
}

static int roq_apply_cc2(roq_parse_ctx* ctx, unsigned x, unsigned y, char* buf)
{
	int i;
	roq_info* ri = ctx->ri;
	unsigned pitch = ri->viewport_pitch;
	int *dst = (int *)(ri->viewport + y * pitch + x);

	pitch /= 2;
	for (i = 0; i < 4; i += 2)
	{
		roq_cell *cell0 = ri->cells + (uint8_t)buf[i];
		roq_cell *cell1 = ri->cells + (uint8_t)buf[i+1];

		dst[0] = cell0->rgb555x2[0];
		dst[1] = cell1->rgb555x2[0];
		dst += pitch;

		dst[0] = cell0->rgb555x2[1];
		dst[1] = cell1->rgb555x2[1];
		dst += pitch;
	}

	return 4;
}

/* -------------------------------------------------------------------------- */

void roq_read_vq(roq_parse_ctx *ctx, unsigned char *buf, int chunk_size, int dodma)
{
	int bpos = 0;
	int xpos, ypos;
	roq_info *ri = ctx->ri;
	int width = ri->width;
	int dmafrom = 0, dmato = 0;
	int finalxfer = 0;

	xpos = ypos = 0;
	for (bpos = 0; bpos < chunk_size; )
	{
		int xp, yp;

		for (yp = ypos; yp < ypos + 16; yp += 8)
		{
			for (xp = xpos; xp < xpos + 16; xp += 8)
			{
				bpos += roq_read_vqid(ctx, &buf[bpos], &ctx->vqid);
				bpos += appliers[ctx->vqid](ctx, xp, yp, (char *)&buf[bpos]);
				if (bpos >= chunk_size)
					goto checknext;
			}
		}

		xpos += 16;
		if (xpos < width)
			continue;

checknext:
		if (dodma)
		{
			int othery;

xfer:
			othery = ypos;

			if (othery > dmato)
			{
				if (dmato > 0) 
				{
					if (!finalxfer)
					{
						if (!(SH2_DMA_CHCR1 & 2))
							goto next; // do not wait on TE
					}
					else
					{
						while (!(SH2_DMA_CHCR1 & 2)) ; // wait on TE
					}
					SH2_DMA_CHCR1 = 0; // clear TE
				}

				dmafrom = dmato;
				dmato = othery;

				// start DMA
				SH2_DMA_SAR1 = (uint32_t)(ri->viewport + ri->viewport_pitch*dmafrom/2*sizeof(short));
				SH2_DMA_DAR1 = (uint32_t)(ri->viewportcopy + ri->viewport_pitch*dmafrom/2*sizeof(short));
				SH2_DMA_TCR1 = ((((dmato - dmafrom)*ri->viewport_pitch*sizeof(short)) >> 4) << 2); // xfer count (4 * # of 16 byte units)
				SH2_DMA_CHCR1 = 0b0101111011100001; // dest incr, src incr, size 16B, auto req, cycle-steal, dual addr, intr disabled, clear TE, dma enabled
			}

			if (finalxfer)
			{
				while (!(SH2_DMA_CHCR1 & 2)) ; // wait on TE
				return;
			}
		}

next:
		xpos = 0;
		ypos += 16;
		if (ypos >= ri->display_height)
			break;
	}

	if (dodma && dmato < ri->display_height)
	{
		finalxfer = 1;
		goto xfer;
	}
}

int roq_read_frame(roq_info* ri, char loop, void (*finish)(void))
{
	int i, nv1, nv2;
	roq_file* fp = ri->fp;
	unsigned int chunk_id = 0, chunk_arg0 = 0, chunk_arg1 = 0;
	unsigned long chunk_size = 0;
	unsigned long next_chunk;
	unsigned char *buf;
	roq_parse_ctx ctx;

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
		{
			roq_yuvcell *yuvcell;

			if ((nv1 = chunk_arg1) == 0) nv1 = 256;
			if ((nv2 = chunk_arg0) == 0 && nv1 * 6 < chunk_size) nv2 = 256;

			yuvcell = (void *)fp->rover;
			fp->rover += nv1*6;
			for (i = 0; i < nv1; i++)
			{
				ri->cells[i].rgb555[0] = yuv2rgb555(yuvcell->y0123[0], yuvcell->uvb[0], yuvcell->uvb[1]);
				ri->cells[i].rgb555[1] = yuv2rgb555(yuvcell->y0123[1], yuvcell->uvb[0], yuvcell->uvb[1]);
				ri->cells[i].rgb555[2] = yuv2rgb555(yuvcell->y0123[2], yuvcell->uvb[0], yuvcell->uvb[1]);
				ri->cells[i].rgb555[3] = yuv2rgb555(yuvcell->y0123[3], yuvcell->uvb[0], yuvcell->uvb[1]);
				yuvcell++;
			}

			memcpy(ri->qcells, fp->rover, nv2*4);
			fp->rover += nv2*4;
		}
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
				if (num_samples > MAX_SAMPLES) 
					num_samples = MAX_SAMPLES;

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

			snd_left = (int16_t)(chunk_arg1 << 8);
			snd_right = (int16_t)(chunk_arg0 << 8);

			for (i = 0; i < total_samples; )
			{
				int num_samples = total_samples - i;
				if (num_samples > MAX_SAMPLES)
					num_samples = MAX_SAMPLES;

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

	finish();

	ri->frame_num++;

	buf = fp->rover;

	ctx.ri = ri;
	ctx.chunk_arg0 = chunk_arg0;
	ctx.chunk_arg1 = chunk_arg1;
	ctx.vqflg = 0;
	ctx.vqflg_pos = 0;
	ctx.vqid = RoQ_ID_MOT;

	roq_read_vq(&ctx, buf, chunk_size, 1);

	fp->rover = buf + chunk_size;
	roq_fseek(fp, next_chunk, SEEK_SET);

	return 1;
}
