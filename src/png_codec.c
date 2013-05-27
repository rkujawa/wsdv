/* $$
 *
 * png_codec.c
 *
 * Copyright (c) 1999-2012 Reinoud Zandijk <reinoud@13thmonkey.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTERS``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */


#include <sys/types.h>
#ifndef NO_STDINT
#	include <stdint.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include "png_codec.h"


#define BUFFER_SIZE	(32*1024)
#define ASSEMBLE_SIZE	(32*1024)
#define ZBUF_SIZE	(32*1024)


/* loader state machine */
#define LOADER_STATE_OFF		 0
#define LOADER_STATE_START		 1
#define LOADER_STATE_IDENTIFIED		 2
#define LOADER_STATE_IHDR		 3
#define LOADER_STATE_READ_IDATS		 4
#define LOADER_STATE_FINISHED		 5
#define LOADER_STATE_ERROR		99


/* block loader state machine */
#define BLOCK_LD_STATE_WAIT		 0
#define BLOCK_LD_STATE_START		 1
#define BLOCK_LD_STATE_READ_BLK		 2
#define BLOCK_LD_STATE_READ_CRC		 3
#define BLOCK_LD_STATE_FINISHED		 4
#define BLOCK_LD_STATE_ERROR		99


/* loader filter states */
#define FILTER_LD_STATE_WAIT		 0
#define FILTER_LD_STATE_START		 1
#define FILTER_LD_STATE_START_PASS	 2
#define FILTER_LD_STATE_STARTLINE	 3
#define FILTER_LD_STATE_FILTERMODE	 4
#define FILTER_LD_STATE_INLINE	 	 5


/* saver state machine */
#define SAVER_STATE_OFF			 0
#define SAVER_STATE_START		 1
#define SAVER_STATE_HEADER		 2
#define SAVER_STATE_IDENTIFIED		 3
#define SAVER_STATE_SEND_MISC_BLOCKS	 4
#define SAVER_STATE_START_SENDING_IDATS	 5
#define SAVER_STATE_SEND_IDATS		 6
#define SAVER_STATE_FINISHED		98
#define SAVER_STATE_ERROR		99


/* saver filter states */
#define FILTER_SA_STATE_WAIT		 0
#define FILTER_SA_STATE_START		 1
#define FILTER_SA_STATE_START_PASS	 2
#define FILTER_SA_STATE_STARTLINE	 3
#define FILTER_SA_STATE_WAIT_FOR_SPACE	 4
#define FILTER_SA_STATE_OUTPUT_LINE	 5
#define FILTER_SA_STATE_NEXT_LINE 	 6
#define FILTER_SA_STATE_NEXT_PASS	 7
#define FILTER_SA_STATE_FINISHED	 8


/* block types */
#define BLOCK_TYPE_IHDR		(0x52444849)
#define BLOCK_TYPE_IDAT		(0x54414449)
#define BLOCK_TYPE_IEND		(0x444e4549)
#define BLOCK_TYPE_PLTE		(0x45544c50)
#define BLOCK_TYPE_GAMA		(0x414d4147)
#define BLOCK_TYPE_BKGD		(0x44474b42)
#define BLOCK_TYPE_TIME		(0x454d4954)
#define BLOCK_TYPE_PHYS		(0x53594850)
#define BLOCK_TYPE_TRNS		(0x534e5254)


/* block flags */
#define BLOCK_ANCILLARY		 1
#define BLOCK_PRIVATE		 2
#define BLOCK_NON_CONFORMING	 4
#define BLOCK_SAFE_TO_COPY	 8


/* mixed endian macro's */
#define READ1_SIGNED(pos) ((signed int) (*( (signed char *) (pos) ) ))


/* LITTLE endian macro's */
#define READ2_LE(pos) ((uint32_t) *((pos)) + 256*(*((pos)+1)))
#define READ2_LE_SIGNED(pos) (( (int32_t) READ2_LE(pos) << 16) >>16)
#define READ4_LE(pos) (READ2_LE(pos) + (READ2_LE(pos+2)<<16))

#define WRITE2_LE(pos, value) *(pos) = (value) & 0xff; *((pos)+1) = ((value) >> 8) & 0xff;
#define WRITE4_LE(pos, value) WRITE2_LE(pos, ((value) & 0xffff)); WRITE2_LE((pos)+2, ((value) >> 16));

/* BIG endian macro's */
#define READ2_BE(pos) ((uint32_t) *((pos)+1) + 256*(*(pos)))
#define READ2_BE_SIGNED(pos) (( (int32_t) READ2_BE(pos) << 16) >>16)
#define READ4_BE(pos) (READ2_BE((pos)+2) + (READ2_BE((pos))<<16))

#define WRITE2_BE(pos, value) *(pos) = ((value) >> 8) & 0xff; *((pos)+1) = (value) & 0xff;
#define WRITE4_BE(pos, value) WRITE2_BE((pos), ((value) >> 16)); WRITE2_BE((pos)+2, ((value) & 0xffff));


/* progressive display help table - as specified for Adam7 interlace */
uint8_t starting_row[7]  = { 0, 0, 4, 0, 2, 0, 1 };
uint8_t starting_col[7]  = { 0, 4, 0, 2, 0, 1, 0 };
uint8_t row_increment[7] = { 8, 8, 8, 4, 4, 2, 2 };
uint8_t col_increment[7] = { 8, 8, 4, 4, 2, 2, 1 };
uint8_t block_height[7]  = { 8, 8, 4, 4, 2, 2, 1 };
uint8_t block_width[7]   = { 8, 4, 4, 2, 2, 1, 1 };


/* Samples per pixel indexed by colour type */
static int8_t samples_per_pixel[8] = {
	1,	/* 0 : grey		*/
	0,	/* 1 : illegal		*/
	3,	/* 2 : RGB colour	*/
	1,	/* 3 : indexed RGB	*/
	2,	/* 4 : grey+alpha	*/
	0,	/* 5 : illegal		*/
	4,	/* 6 : RGBA colour	*/
	0,	/* 7 : illegal		*/
};


struct png_private {
	uint32_t	 fhandle;

	/* state machines	*/
	uint32_t	 loader_state;
	uint32_t	 saver_state;
	uint32_t	 block_state;
	uint32_t	 filter_state;
	z_stream	 zlib_state;

	/* file loading/saving buffer	*/
	uint8_t		*buffer;
	uint32_t	 buf_length;

	/* block assembling for non IDAT's */
	uint8_t		*blk_cache;
	uint32_t	 blk_cache_pos;

	/* zoutput/zinput data cache */
	uint8_t		*z_buf;
	uint32_t	 z_buf_pos;

	/* block encoding / decoding	*/
	uint32_t	 cur_block_length;
	uint32_t	 cur_block_left;
	uint32_t	 cur_block_type;
	uint32_t	 cur_block_flags;
	uint32_t	 cur_running_crc;
	uint8_t		*cur_block_length_pos;

	/* scanline encoding / decoding	*/
	uint32_t	 col, row;
	uint32_t	 pass;
	uint32_t	 line_pos;
	uint8_t		 cur_filtermode;
	uint8_t		*this_line;		/* pointers to transmit/recieve buffers */
	uint8_t		*last_line;
	uint8_t		*lines[2];		/* pointers to max. transmit length buffer  */

	uint32_t	 packedline_length;
	uint8_t		*packed_line;		/* packed line with samples		    */

	uint8_t		*filter_result[5];	/* pointers to tryouts of different filters */
	int32_t		 filter_effect[5];	/* estimates of the filter efficiency       */
};


static struct png_info *
allocate_png_info(void) {
	return calloc(1, sizeof(struct png_info));
}


static void
free_png_info(struct png_info *png_info) {
	free(png_info);
}


static uint8_t *
allocate_image(uint32_t size) {
	return calloc(1, size);
}


static void
free_image(uint8_t *image) {
	free(image);
}


/*
 * CRC stuff following ISO 3309
 */
static uint32_t crc_table[256];

static void
make_crc_table(void) {
	uint32_t c;
	int n, k;

	for (n=0; n<256; n++) {
		c = (uint32_t) n;
		for (k=0; k < 8; k++) {
			if (c & 1)
				c = 0xedb88320 ^ (c >> 1);
			else
				c = (c >> 1);
			;
		}
		crc_table[n] = c;
	}
}


static uint32_t
update_crc(uint32_t crc, uint8_t *buf, int len) {
	uint32_t c = crc;
	int n;

	for(n=0; n < len; n++) {
		c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
	}
	return c;
}
/*
 * End of CRC stuff
 */


/*
 * PeathPredictor as specified in the specs
 */
static int8_t
PaethPredictor(uint8_t a, uint8_t b, uint8_t c) {
	int p, pa, pb, pc;
	
	/* a = left, b = above, c = above-left */
	p = a + b - c;
	pa = abs(p - a);
	pb = abs(p - b);
	pc = abs(p - c);
	/* return nearest of a,b,c */
	if ((pa <= pb) && (pa <= pc)) return a;
	if (pb <= pc) return b;
	return c;
}


/*
 * Provide two allocation functions to allow for special cached memory
 * management as found in the Artdraw library
 */
void
png_init(void) {
	make_crc_table();
}


struct png_info *
png_create_png_context(void) {
	struct png_info *info;
	struct png_private *png_private;
	int index;

	info = allocate_png_info();
	if (!info)
		return NULL;

	/* we got a png_info structure ! -> clear for sure */
	memset(info, 0, sizeof(struct png_info));

	png_private = info->png_private = calloc(1, sizeof(struct png_private));
	if (!png_private) {
		free_png_info(info);
		return NULL;
	}
	memset(png_private, 0, sizeof(struct png_private));

	/* initialise structure */
	png_private->buf_length = png_private->blk_cache_pos = png_private->z_buf_pos = 0;

	png_private->buffer	= malloc(BUFFER_SIZE+1);
	png_private->blk_cache	= malloc(ASSEMBLE_SIZE+1);
	png_private->z_buf	= malloc(ZBUF_SIZE+1);
	if (png_private->buffer && png_private->blk_cache && png_private->z_buf) {
		info->filestate = PNG_FILE_CLEAR;
		png_private->fhandle = -1;

		/* initialise state machines */
		png_private->loader_state = LOADER_STATE_OFF;
		png_private->saver_state  = SAVER_STATE_OFF;
		png_private->block_state  = BLOCK_LD_STATE_WAIT;
		png_private->filter_state = FILTER_LD_STATE_WAIT;

		/* initialise palette */
		for (index=0; index <= 255; index++)
			info->palette[index] = 0xff000000 + index*0x010101;

		return info;
	}
	/* ieeek ! */
	png_dispose_png(info);

	return NULL;
}


png_file_status
png_populate_with_image(struct png_info *info, int colourtype, void *blob, int bpp, int width, int height) {
	if (!info)
		return PNG_FILE_ERROR;
	if (info->filestate != PNG_FILE_CLEAR)
		return PNG_FILE_WOULD_DESTROY;

	info->blob = blob;
	info->colourtype = colourtype;
	info->width  = width;
	info->height = height;
	info->bpp    = bpp;

	info->sample_depth = bpp;
	if (info->colourtype == PNG_COLOUR_INDEXED)
		info->sample_depth = 8;

	if (colourtype & PNG_COLOURT_EI) {
		info->samples_per_pixel = 4;
	} else {
		info->samples_per_pixel = samples_per_pixel[info->colourtype];
	}

	info->strave = (info->width * info->bpp * info->samples_per_pixel+7)/8;

	if (info->blob)
		info->filestate = PNG_FILE_IS_DRAWABLE;
	return info->filestate;
}


png_file_status
png_populate_and_allocate_empty_image(struct png_info *info, int colourtype, int bpp, int width, int height) {
	if (!info)
		return PNG_FILE_ERROR;
	if (info->filestate != PNG_FILE_CLEAR)
		return PNG_FILE_WOULD_DESTROY;

	/* late allocate */
	png_populate_with_image(info, colourtype, NULL, bpp, width, height);
	if (info->filestate == PNG_FILE_CLEAR) {
		info->blob = allocate_image(info->strave * info->height);
		info->filestate = PNG_FILE_IS_DRAWABLE;
	}

	return info->filestate;
}


png_file_status
png_start_loading(struct png_info *info, int fhandle) {
	if (!fhandle)
		return PNG_FILE_ERROR | PNG_FILE_BAD_FILEHANDLE;
	if (info) {
		/* start loading engine */
		info->filestate = PNG_FILE_LOADING;
		info->png_private->fhandle = fhandle;
		info->png_private->loader_state = LOADER_STATE_START;
		return info->filestate;
	}

	return PNG_FILE_ERROR;
}


png_file_status
png_start_saving(struct png_info *info, int fhandle) {
	if (!fhandle)
		return PNG_FILE_ERROR | PNG_FILE_BAD_FILEHANDLE;
	if (info) {
		if (info->png_private) {
			/* start saver engine */
			info->filestate |= PNG_FILE_SAVING;
			info->png_private->fhandle = fhandle;
			info->png_private->saver_state = SAVER_STATE_START;
			info->png_private->filter_state = FILTER_SA_STATE_WAIT;
		}
	}

	return info->filestate;
}


png_file_status
png_dispose_png(struct png_info *info) {
	struct png_private *png_private;
	int index;

	if (info) {
		png_private = info->png_private;
		if (info->blob)
			free_image(info->blob);

		if (png_private) {
			if (png_private->buffer)	free(png_private->buffer);
			if (png_private->blk_cache)	free(png_private->blk_cache);
			if (png_private->z_buf)		free(png_private->z_buf);
			if (png_private->lines[0])	free(png_private->lines[0]);
			if (png_private->lines[1])	free(png_private->lines[1]);
			if (png_private->packed_line)	free(png_private->packed_line);
			for (index=0; index<5; index++) {
				if (png_private->filter_result[index])
					free(png_private->filter_result[index]);
			}
			free(info->png_private);
			info->png_private = NULL;
		}
		free_png_info(info);
	}
	return PNG_FILE_DISPOSED;
}


/* Process the decrunched idat piece in the blk_cache buffer */
static int
png_process_idat_in_blk_cache(struct png_info *info) {
	struct png_private *png_private;
	uint8_t		*recycled;
	uint8_t		*pos, *thislinepos, *lastlinepos, *screen, *screen_line;
	uint8_t		 this_byte, left_byte, top_byte, topleft_byte, result;
	uint32_t	 colour;
	uint32_t	 consumed, pixel_bit_offset, subpixel_mask;
	int		 leave, leave_line, bytes_per_pixel, pixels_per_byte;
	int		 bpp, subpixel, index, dcol, drow;

	/* shortcut */
	png_private = info->png_private;

	/* some used `constants' */
	bpp = info->bpp;
	subpixel_mask = (1 << bpp)-1;

	/* status variables */
	consumed = 0;
	leave = 0;
	while (!leave) {
		/* do the filter state machine */
		switch (png_private->filter_state) {
			case FILTER_LD_STATE_WAIT :
				fprintf(stderr, "Filter state: i shouldn't be here\n");
				break;
			case FILTER_LD_STATE_START :
				png_private->pass = 0;			/* specs start with 1 */
				png_private->filter_state = FILTER_LD_STATE_START_PASS;
				/* fall trough */
			case FILTER_LD_STATE_START_PASS :
				png_private->this_line = png_private->lines[0];
				png_private->last_line = png_private->lines[1];
				png_private->row = 0;
				if (info->interlace) png_private->row = starting_row[png_private->pass];
				/* clear the old memories */
				memset(png_private->this_line, 0, info->strave + 16);
				memset(png_private->last_line, 0, info->strave + 16);

				png_private->filter_state = FILTER_LD_STATE_STARTLINE;
				/* fall trough */
			case FILTER_LD_STATE_STARTLINE :
				recycled = png_private->last_line;
				png_private->last_line = png_private->this_line;
				png_private->this_line = recycled;
				png_private->col = 0;
				if (info->interlace) png_private->col = starting_col[png_private->pass];
				png_private->line_pos = 0;
				png_private->filter_state = FILTER_LD_STATE_FILTERMODE;
				/* fall trough */
			case FILTER_LD_STATE_FILTERMODE :
				if (png_private->z_buf_pos >= 1) {
					png_private->cur_filtermode = *png_private->z_buf;
					consumed++;
					png_private->filter_state = FILTER_LD_STATE_INLINE;
					if (png_private->cur_filtermode>4) {
						fprintf(stderr, "HAR! found undefined PNG filtermode %d\n", png_private->cur_filtermode);
						png_private->block_state  = BLOCK_LD_STATE_ERROR;
						info->filestate |=  PNG_FILE_OUT_OF_SPECS;
						leave = 1;
					}
				} else {
					leave = 1;
				}
				break;
			case FILTER_LD_STATE_INLINE :
				/* set some usefull variables */
				dcol = 1; drow = 1;
				if (info->interlace) {
					dcol = col_increment[png_private->pass];
					drow = row_increment[png_private->pass];
				}

				/* bytes_per_pixel is the number of bytes nessisary for at least one pixel */
				bytes_per_pixel = (info->sample_depth * info->samples_per_pixel)/8;
				if (bytes_per_pixel == 0)
					bytes_per_pixel = 1;
				pixels_per_byte = (8/bpp);

				/* update the pointers */
				pos = png_private->z_buf;
				thislinepos = png_private->this_line + png_private->line_pos + 8;
				lastlinepos = png_private->last_line + png_private->line_pos + 8;
/* XXX change me for pixel pusher XXX */
				screen_line = info->blob + info->strave*png_private->row;
				screen = screen_line + bytes_per_pixel*png_private->col;
				leave_line = 0;
				while ((png_private->z_buf_pos - consumed >= bytes_per_pixel) && !leave_line) {
					/* copy the raw bytes into the line */
					for(index=0; index < bytes_per_pixel; index++) {
						this_byte    = thislinepos[index] = pos[index];
						left_byte    = thislinepos[index-bytes_per_pixel];
						top_byte     = lastlinepos[index];
						topleft_byte = lastlinepos[index-bytes_per_pixel];
						/* apply the filters */
						result = this_byte;
						switch (png_private->cur_filtermode) {
							case 0 : /* None	*/
								break;
							case 1 : /* Sub		*/
								result += left_byte;
								break;
							case 2 : /* Up		*/
								result += top_byte;
								break;
							case 3 : /* Average	*/
								result += (left_byte + top_byte)/2;
								break;
							case 4 : /* Paeth	*/
								result += PaethPredictor(left_byte, top_byte, topleft_byte);
								break;
						}
						/*
						 * this is not well documented in the specs; the filter
						 * method is done on bytes but the packed bytes need to be
						 * distributed according to the interlace method
						 * anyway... in pixels !
						 */
						if (pixels_per_byte > 1) {
							for(subpixel=0; subpixel<pixels_per_byte; subpixel++) {
								pixel_bit_offset = bpp*png_private->col;
								screen = screen_line + (pixel_bit_offset>>3);
								colour = (result >> (8-bpp)) & subpixel_mask;

								/* call me paranoid */
								assert(screen < info->blob + (info->strave * info->height));

/* XXX change me for pixel pusher XXX */
								*screen |= (colour << (8-(pixel_bit_offset & 7)-bpp));
								result = result << bpp;
								png_private->col += dcol;
							}
						} else {
							/* call me paranoid */
							assert(screen+index < info->blob + (info->strave * info->height));

/* XXX change me for pixel pusher XXX */
							screen[index] = result;
						}
						thislinepos[index] = result;
					}
					thislinepos += bytes_per_pixel;
					lastlinepos += bytes_per_pixel;
					consumed += bytes_per_pixel;
					pos += bytes_per_pixel;
					png_private->line_pos += bytes_per_pixel;

					if (pixels_per_byte <= 1) {
						png_private->col += dcol;
						screen += dcol*bytes_per_pixel;
					}

					if (png_private->col >= info->width) {
						/* next line !!!! */
/* XXX change me for pixel pusher : we can do it here too! XXX */
						png_private->row += drow;
						if (png_private->row >= info->height) {
							png_private->pass++;	/* XXX why here ? XXX */
							png_private->filter_state = FILTER_LD_STATE_START_PASS;
						} else {
							png_private->filter_state = FILTER_LD_STATE_STARTLINE;
						}
						leave_line = 1;
					}
				}
				/* dont forget we have consumed as much as possible */
				if (!leave_line) leave = 1;
				break;
		}

		if (consumed) {
			/* fprintf(stderr, "line: consumed %d bytes\n", consumed); */
			if (consumed)
				memmove(png_private->z_buf, png_private->z_buf+consumed, png_private->z_buf_pos-consumed);
			png_private->z_buf_pos -= consumed;
			consumed = 0;
		}
		if (png_private->z_buf_pos == 0)
			leave=1;
	}

	return 0;
}


static png_file_status
png_loader_statemachine(struct png_info *info) {
	struct png_private *png_private;
	uint32_t consumed;
	uint32_t crc, rgb;
	uint8_t  *pos, A;
	int	  ok, leave, zresult, index, colourtype;

	/* shortcut */
	png_private = info->png_private;

	/* status variables */
	consumed = 0;
	leave = 0;
	while (!leave) {
		/*
		 * fprintf(stderr, "blockst = %d, loaderst = %d, filterst = %d\n",
		 *	png_private->block_state, png_private->loader_state, png_private->filter_state);
		 */
		/* do the block state machine */
		switch (png_private->block_state) {
			case BLOCK_LD_STATE_WAIT :		/* do nothing */
				break;
			case BLOCK_LD_STATE_START :	/* start reading */
				if (png_private->buf_length >= 8) {
					png_private->cur_block_length = READ4_BE(png_private->buffer);
					png_private->cur_block_left   = png_private->cur_block_length;
					png_private->blk_cache_pos    = 0;

					/* init running CRC to all `1's as specified */
					png_private->cur_running_crc = 0xffffffff;
					/* block type is included in the CRC ! */
					png_private->cur_running_crc = update_crc(png_private->cur_running_crc, png_private->buffer+4, 4);

					/* check for interesting flags */
					png_private->cur_block_flags  = 0;
					pos = png_private->buffer+4;	/* process block type */
					if (pos[0] && 32) png_private->cur_block_flags |= BLOCK_ANCILLARY;
					if (pos[1] && 32) png_private->cur_block_flags |= BLOCK_PRIVATE;
					if (pos[2] && 32) png_private->cur_block_flags |= BLOCK_NON_CONFORMING;
					if (pos[3] && 32) png_private->cur_block_flags |= BLOCK_SAFE_TO_COPY;

					pos[0] = pos[0] & 223;
					pos[1] = pos[1] & 223;
					pos[2] = pos[2] & 223;
					pos[3] = pos[3] & 223;

					png_private->cur_block_type   = READ4_LE(png_private->buffer+4);

					consumed = 8;

					png_private->block_state = BLOCK_LD_STATE_READ_BLK;
				} else {
					leave = 1;
				}
				break;
			case BLOCK_LD_STATE_READ_BLK :
				/* process block stuff */
				consumed = png_private->buf_length;
				if (consumed > png_private->cur_block_left)
					consumed = png_private->cur_block_left;
				png_private->cur_block_left -= consumed;

				png_private->cur_running_crc = update_crc(png_private->cur_running_crc, png_private->buffer, consumed);

				if (png_private->cur_block_type != BLOCK_TYPE_IDAT) {
					/* assemble block */
					if (png_private->blk_cache_pos + consumed < ASSEMBLE_SIZE) {
						memcpy(png_private->blk_cache+png_private->blk_cache_pos, png_private->buffer, consumed);
						png_private->blk_cache_pos+=consumed;
					} else {
						info->filestate |=  PNG_FILE_IMP_LIMIT;
						png_private->block_state = BLOCK_LD_STATE_ERROR;
					}
				} else {
					/*
					 * IDAT's are special in that they are to be fed to libz; they are
					 * to be decrunched in the blk_cache repeatably for decompressed
					 * stuff can explode in size and thus it needs to be processed right
					 * here .... not shining but streaming is priority 1; consequence is
					 * that the lib could give a zlib error when something is wrong in the
					 * datastream where it otherwise would give a CRC error right away.
					 */
					png_private->zlib_state.next_in   = png_private->buffer;
					png_private->zlib_state.avail_in  = consumed;
					/* process until all input is consumed */
					do {
						png_private->zlib_state.next_out  = png_private->z_buf + png_private->z_buf_pos;
						png_private->zlib_state.avail_out = ZBUF_SIZE - png_private->z_buf_pos;
						zresult = inflate(&png_private->zlib_state, Z_SYNC_FLUSH);
						if ((zresult == Z_OK) || (zresult == Z_STREAM_END)) {
							png_private->z_buf_pos = ZBUF_SIZE - png_private->zlib_state.avail_out;
							/* process the stuff in the output buffer */
							if (png_process_idat_in_blk_cache(info)) {
								info->filestate |= PNG_FILE_IDAT_ERR;
								png_private->block_state = BLOCK_LD_STATE_ERROR;
							}
						} else {
							info->filestate |= PNG_FILE_ZLIB_ERR;
							png_private->block_state = BLOCK_LD_STATE_ERROR;
						}
						ok  = (png_private->block_state != BLOCK_LD_STATE_ERROR);
						ok &= (png_private->zlib_state.avail_in > 0);
					} while (ok);
				}

				if ((png_private->cur_block_left == 0) && (png_private->block_state == BLOCK_LD_STATE_READ_BLK))
					png_private->block_state = BLOCK_LD_STATE_READ_CRC;

				if (errno == EAGAIN)
					leave = 1;		/* wait for more data */
				break;
			case BLOCK_LD_STATE_READ_CRC :
				if (png_private->buf_length >=4 ) {
					crc = READ4_BE(png_private->buffer);
					if (crc != (png_private->cur_running_crc ^ 0xffffffff)) {
						info->filestate |=  PNG_FILE_CRC_ERR;
						png_private->block_state = BLOCK_LD_STATE_ERROR;
					} else {
						consumed = 4;
						png_private->block_state = BLOCK_LD_STATE_FINISHED;
					}
				} else {
					leave = 1;
				}
				break;
			case BLOCK_LD_STATE_FINISHED :
				break;
			case BLOCK_LD_STATE_ERROR :
				info->filestate &= ~PNG_FILE_LOADING;
				info->filestate |=  PNG_FILE_ERROR;
				break;
		}

		/* do the loading state machine */
		switch (png_private->loader_state) {
			case LOADER_STATE_OFF :
				break;
			case LOADER_STATE_START :
				if (png_private->buf_length >= 8) {
					pos = png_private->buffer;
					ok = (*pos++ == 137);
					ok = ok && (*pos++ == 80);
					ok = ok && (*pos++ == 78);
					ok = ok && (*pos++ == 71);
					ok = ok && (*pos++ == 13);
					ok = ok && (*pos++ == 10);
					ok = ok && (*pos++ == 26);
					ok = ok && (*pos++ == 10);
					if (!ok) {
						info->filestate |= PNG_FILE_NO_PNG;
						png_private->loader_state = LOADER_STATE_ERROR;
						/* XXX cleanup XXX */
						leave = 1;
					}
					info->filestate = PNG_FILE_LOADING;
					png_private->loader_state = LOADER_STATE_IDENTIFIED;
					consumed = 8;
				}
				break;
			case LOADER_STATE_IDENTIFIED :
				/* initialise libz */
				png_private->zlib_state.zalloc = Z_NULL;
				png_private->zlib_state.zfree  = Z_NULL;
				if (inflateInit(&png_private->zlib_state) != Z_OK) {
					info->filestate |= PNG_FILE_ZLIB_ERR;
					png_private->loader_state = LOADER_STATE_ERROR;
					break;
				}

				png_private->block_state = BLOCK_LD_STATE_START;
				png_private->loader_state = LOADER_STATE_IHDR;
				break;
			case LOADER_STATE_IHDR :
				if (png_private->block_state == BLOCK_LD_STATE_FINISHED) {
					/* check for an IHDR block */
					if (png_private->cur_block_type == BLOCK_TYPE_IHDR) {
						/* read IHDR */
						pos = png_private->blk_cache;
						info->width 	  = READ4_BE(pos+0);
						info->height	  = READ4_BE(pos+4);
						info->bpp   	  = pos[ 8];
						info->colourtype  = pos[ 9];
						info->compression = pos[10];
						info->filter	  = pos[11];
						info->interlace	  = pos[12];

						/* libz is allready set up in init */
						png_private->block_state = BLOCK_LD_STATE_START;
						png_private->loader_state = LOADER_STATE_READ_IDATS;

						/* PNG specs police :) */
						info->sample_depth = info->bpp;
						if (info->colourtype == PNG_COLOUR_INDEXED)
							info->sample_depth = 8;

						if (info->compression || info->filter || (info->interlace>1)) {
							info->filestate |= PNG_FILE_OUT_OF_SPECS;
							png_private->loader_state = LOADER_STATE_ERROR;
						}

						/* please sync this code with the `png_populate and allocate_empty image' function! */
						/* calculate sizes */
						info->samples_per_pixel = samples_per_pixel[info->colourtype];
						/* round up strave */
						info->strave = (info->width * info->bpp * info->samples_per_pixel+7)/8;

						/* set up picture decoding vars */
						png_private->filter_state	= FILTER_LD_STATE_START;

						/* allocate blobs; strave has been normalised to bytes */
/* XXX change me ????? for pixel pusher XXX */
						info->blob = allocate_image(info->strave * info->height);	/* XXX 12345 XXX */
						ok = (info->blob != NULL);

						/* allocate some slack space 2*4*2 bytes extra for easy decoding */
						png_private->lines[0] = allocate_image(info->strave + 16);
						png_private->lines[1] = allocate_image(info->strave + 16);
						ok = ok && ((png_private->lines[0]!=NULL) && (png_private->lines[1]!=NULL));
						if (!ok) {
							info->filestate |= PNG_FILE_OUT_OF_MEM;
							png_private->loader_state = LOADER_STATE_ERROR;
						}

#ifndef NDEBUG
	fprintf(stderr, "PNG %d x %d, strave %d, %d bpp, sample %d bits, colt %d, comp %d, filterm %d, interl %d\n",
			info->width, info->height, info->strave, info->bpp, info->sample_depth, info->colourtype,
			info->compression, info->filter, info->interlace);
#endif

						info->filestate |= PNG_FILE_IS_DRAWABLE;

					} else {
						/* PNG specs police detected a file format error ! */
						png_private->loader_state = LOADER_STATE_ERROR;
					}
				}
				break;
			case LOADER_STATE_READ_IDATS :
				if (png_private->block_state == BLOCK_LD_STATE_FINISHED) {
					png_private->block_state = BLOCK_LD_STATE_START;

					if (png_private->cur_block_type == BLOCK_TYPE_IEND) {
						png_private->block_state = BLOCK_LD_STATE_FINISHED;
						png_private->loader_state = LOADER_STATE_FINISHED;
						break;
					}

					colourtype = info->colourtype;
					if (png_private->cur_block_type == BLOCK_TYPE_PLTE) {
						pos = png_private->blk_cache;
						for(index=0; index<png_private->cur_block_length/3; index++) {
							/* png specifies alpha=255 to be fully drawn */
							rgb  = 255 << 24;
							rgb |= (*pos++) << 16;
							rgb |= (*pos++) <<  8;
							rgb |= (*pos++);
							info->palette[index] = rgb;
						}
						break;
					}
					if (png_private->cur_block_type == BLOCK_TYPE_TRNS) {
						/* information is dependent on colour type */
						if (colourtype == PNG_COLOUR_INDEXED) {
							for(index=0; index<png_private->cur_block_length; index++) {
								A = png_private->blk_cache[index];
								rgb = info->palette[index] & 0x00ffffff;
								rgb |= (A<<24);
	
								info->palette[index] = rgb;
							}
						}
						if (colourtype == PNG_COLOUR_GREY_ONLY) {
							info->transparant_grey = READ2_BE(png_private->blk_cache + 0);
						}
						if (colourtype == PNG_COLOUR_RGB) {
							info->transparant_R = READ2_BE(png_private->blk_cache + 0);
							info->transparant_G = READ2_BE(png_private->blk_cache + 2);
							info->transparant_B = READ2_BE(png_private->blk_cache + 4);
						}
						info->has_transparancy = 1;
						break;
					}
					if (png_private->cur_block_type == BLOCK_TYPE_BKGD) {
						if (colourtype == PNG_COLOUR_INDEXED) {
							info->background_index = png_private->blk_cache[0];
						}
						if ((colourtype == PNG_COLOUR_GREY_ONLY) || (colourtype == PNG_COLOUR_GREY_ALPHA)) {
							info->background_grey = READ2_BE(png_private->blk_cache);
						}
						if (colourtype & PNG_COLOURT_COLOUR) {
							info->background_R = READ2_BE(png_private->blk_cache + 0);
							info->background_G = READ2_BE(png_private->blk_cache + 2);
							info->background_B = READ2_BE(png_private->blk_cache + 4);
						}
						info->has_backgroundcolour = 1;
						break;
					}

#ifndef NDEBUG
					if (png_private->cur_block_type != BLOCK_TYPE_IDAT) {
						fprintf(stderr, "PNG ignoring block %4s\n", (char *) &png_private->cur_block_type);
					}
#endif

				}
				break;
			case LOADER_STATE_FINISHED :
				inflateEnd(&png_private->zlib_state);

				info->filestate &= ~PNG_FILE_LOADING;
				info->filestate |=  PNG_FILE_FINISHED;
				leave = 1;
				break;
			case LOADER_STATE_ERROR :
				inflateEnd(&png_private->zlib_state);

				info->filestate &= ~PNG_FILE_LOADING;
				info->filestate |=  PNG_FILE_ERROR;
				leave = 1;
				break;
		}
		/* is something wrong with the block reader or loader itself ? */
		if (png_private->block_state == BLOCK_LD_STATE_ERROR)
			png_private->loader_state = LOADER_STATE_ERROR;
		if (png_private->loader_state == LOADER_STATE_ERROR)
			png_private->block_state = BLOCK_LD_STATE_ERROR;

		if (consumed) {
			if (consumed)
				memmove(png_private->buffer, png_private->buffer+consumed, png_private->buf_length-consumed);
			png_private->buf_length -= consumed;
			consumed = 0;
		}

		if (png_private->cur_block_left > png_private->buf_length)
			leave=1;
	}
	return info->filestate;
}


static uint8_t
*png_saver_start_block(uint8_t *pos, uint32_t blocktype, struct png_private *png_private, int flags) {
	/* reserve 4 bytes for the length */
	png_private->cur_block_length_pos = pos; pos+=4;

	/* block type */
	if (flags & BLOCK_ANCILLARY)		blocktype |= 32;
	if (flags & BLOCK_PRIVATE)		blocktype |= 32 <<  8;
	if (flags & BLOCK_NON_CONFORMING)	blocktype |= 32 << 16;
	if (flags & BLOCK_SAFE_TO_COPY)		blocktype |= 32 << 24;
	WRITE4_LE(pos, blocktype); pos+=4;

	/* init running CRC to all `1's as specified */
	png_private->cur_running_crc = 0xffffffff;

	return pos;
}


static uint8_t
*png_saver_end_block(uint8_t *pos, struct png_private *png_private) {
	uint32_t running_crc, length;

	/* put in the length ... only the length of the datablock */
	length = pos - png_private->cur_block_length_pos - 8;
	WRITE4_BE(png_private->cur_block_length_pos, length);

	/* run the CRC over the datablock and the block type */
	running_crc = update_crc(png_private->cur_running_crc, png_private->cur_block_length_pos+4, length+4);
	running_crc ^= 0xffffffff;		/* see specs */
	WRITE4_BE(pos, running_crc); pos+=4;

	return pos;
}


static void
png_saver_filter_fillup_zbuf(struct png_info *info, struct png_private *png_private) {
	uint8_t	*recycled, *packed_line, *pl_pos, *screen, *screen_line;
	uint8_t	*pos;
	int bpp, subpixel, index, dcol, drow;
	int ok, col, leave, bytes_per_pixel, pixels_per_byte;
	uint32_t result;
	uint32_t colour;
#if 0
	uint8_t *ourline, *thislinepos, *lastlinepos;
	uint8_t	 this_byte, left_byte, top_byte, topleft_byte, result;
#endif
	uint32_t pixel_bit_offset, subpixel_mask;
	uint32_t col32;
	uint64_t col64;

	/* data output starts here */
	pos = png_private->z_buf + png_private->z_buf_pos;
	packed_line = png_private->packed_line;

	bpp = info->bpp;
	subpixel_mask = (1 << bpp)-1;

	/*
	 * Note that this statemachine resembles a lot of the loader
	 * statemachine; thats due to the interleave mechanism.
	 */
	leave = 0;
	while (!leave) {
		switch (png_private->filter_state) {
			case FILTER_SA_STATE_WAIT :
				fprintf(stderr, "fillup : filter_sa state is WAIT ... or am i finished?\n");
				leave = 1;
				break;
			case FILTER_SA_STATE_START :
				png_private->pass = 0;
				/* allocate some slack space 2*4*2 bytes extra for easy decoding */
				ok = 1;
				if (png_private->lines[0] == NULL) {
					png_private->lines[0] = allocate_image(info->strave + 16);
					png_private->lines[1] = allocate_image(info->strave + 16);
					ok = ((png_private->lines[0]!=NULL) && (png_private->lines[1]!=NULL));
				}
				if (png_private->filter_result[0] == NULL) {
					for (index=0; index<5; index++) {
						png_private->filter_result[index] = allocate_image(info->strave + 16);
						ok = ok && (png_private->filter_result[index] != NULL);
					}
					packed_line = png_private->packed_line = allocate_image(info->strave + 16);
					ok = ok && (png_private->packed_line != NULL);
				}
				if (!ok) {
					info->filestate |= PNG_FILE_OUT_OF_MEM;
					png_private->loader_state = SAVER_STATE_ERROR;	/* XXX ? */
					leave = 1;
					break;
				}
				packed_line = png_private->packed_line;

				png_private->filter_state = FILTER_SA_STATE_START_PASS;
				break;
			case FILTER_SA_STATE_START_PASS :
				png_private->this_line = png_private->lines[0];
				png_private->last_line = png_private->lines[1];
				/* clear the old memories */
				memset(png_private->this_line, 0, info->strave + 16);
				memset(png_private->last_line, 0, info->strave + 16);

				png_private->row = 0;
				if (info->interlace)
					png_private->row = starting_row[png_private->pass];

				png_private->filter_state = FILTER_SA_STATE_STARTLINE;
				break;
			case FILTER_SA_STATE_STARTLINE :
				recycled = png_private->last_line;
				png_private->last_line = png_private->this_line;
				png_private->this_line = recycled;
				png_private->col = 0; if (info->interlace) png_private->col = starting_col[png_private->pass];
				png_private->packedline_length = 0;

				/* for each filter/compression method generate one line and
				 * choose the best filter method */
				bytes_per_pixel = (info->sample_depth * info->samples_per_pixel)/8;
				if (bytes_per_pixel == 0)
					bytes_per_pixel = 1;
				pixels_per_byte = (8/bpp);

				/* fill the packed_line */
				pl_pos = packed_line;

				dcol = 1;
				if (info->interlace)
					dcol = col_increment[png_private->pass];

				while (png_private->col < info->width) {
					/* XXX we allways do one complete line at a time XXX */
					screen_line = info->blob + info->strave*png_private->row;
/* XXX change me for pixel pusher XXX */
					screen = screen_line + bytes_per_pixel*png_private->col;
					if (pixels_per_byte > 1) {
						result = 0;
						for(subpixel = 0; subpixel < pixels_per_byte; subpixel++) {
							if (!info->interlace) {
								col = png_private->col + subpixel*dcol;
								pixel_bit_offset = bpp*col;

								screen = screen_line + (pixel_bit_offset>>3);
/* XXX change me for pixel pusher XXX */
								colour = (*screen >> (pixel_bit_offset & 7)) & subpixel_mask;

							} else {
								col = png_private->col + (pixels_per_byte-1-subpixel)*dcol;
								pixel_bit_offset = bpp*col;

								screen = screen_line + (pixel_bit_offset>>3);
/* XXX change me for pixel pusher XXX */
								colour = (*screen >> (8-(pixel_bit_offset & 7)-bpp)) & subpixel_mask;
							}
							result = result >> bpp;
							result |= colour << (8-bpp);
						}
						*pl_pos++ = result;
						png_private->col += pixels_per_byte*dcol;
					} else {
						if (info->colourtype & PNG_COLOURT_EI) {
							if (bytes_per_pixel == 4) {
								col32 = *((uint32_t *) screen);	/* 0xAaRrGgBb */
								*pl_pos++ = (col32 >> 16) & 0xff;
								*pl_pos++ = (col32 >>  8) & 0xff;
								*pl_pos++ = (col32      ) & 0xff;
								*pl_pos++ = (col32 >> 24) & 0xff;
							} else if (bytes_per_pixel == 8) {
								col64 = *((uint64_t *) screen); /* 0xAAaaRRrrGGggBBbb */
								WRITE2_BE(pl_pos, (col64 >> 32) & 0xffff); pl_pos += 2;
								WRITE2_BE(pl_pos, (col64 >> 16) & 0xffff); pl_pos += 2;
								WRITE2_BE(pl_pos, (col64      ) & 0xffff); pl_pos += 2;
								WRITE2_BE(pl_pos, (col64 >> 48) & 0xffff); pl_pos += 2;
							} else {
								/* this shouldn't happen */
								for(index=0; index < bytes_per_pixel; index++) {
									result = screen[index];
									*pl_pos++ = result;
								}
							}
						} else {
							/* default non EI PNG forms */
							for(index=0; index < bytes_per_pixel; index++) {
/* XXX change me for pixel pusher XXX */
								result = screen[index];
								*pl_pos++ = result;
							}
						}
						png_private->col += dcol;
					}
				}
				png_private->packedline_length = pl_pos - packed_line;

				png_private->filter_state = FILTER_SA_STATE_WAIT_FOR_SPACE;
				break;
			case FILTER_SA_STATE_WAIT_FOR_SPACE :
				leave = 1;
				if (ZBUF_SIZE - png_private->z_buf_pos >= png_private->packedline_length + 1) {
					leave = 0;
					png_private->filter_state = FILTER_SA_STATE_OUTPUT_LINE;
				}
				break;
			case FILTER_SA_STATE_OUTPUT_LINE :
				/* output filter type and packed line	*/
				/* now select best filter type		*/
				*pos++ = 0;	/* filter type 0 */
				memcpy(pos, packed_line, png_private->packedline_length);
				png_private->z_buf_pos += png_private->packedline_length+1;
				pos += png_private->packedline_length;
				png_private->filter_state = FILTER_SA_STATE_NEXT_LINE;
				png_private->packedline_length=0;
				break;
			case FILTER_SA_STATE_NEXT_LINE :
				drow = 1; if (info->interlace) drow = row_increment[png_private->pass];
				png_private->row += drow;
				png_private->filter_state = FILTER_SA_STATE_STARTLINE;
				if (png_private->row >= info->height) {
					png_private->filter_state = FILTER_SA_STATE_NEXT_PASS;
				}
				break;
			case FILTER_SA_STATE_NEXT_PASS :
				png_private->pass++;
				png_private->filter_state = FILTER_SA_STATE_START_PASS;
				if (!info->interlace || png_private->pass==7)
					png_private->filter_state = FILTER_SA_STATE_FINISHED;
				break;
			case FILTER_SA_STATE_FINISHED :
				leave = 1;
				break;
		}
	}
}


static png_file_status
png_saver_statemachine(struct png_info *info) {
	struct png_private *png_private;
	uint8_t *pos, flags;
	int	 leave, pal_entries, entry, transparencies, colourtype;
	int	 consumed, produced, result, zflags;

	/* shortcut */
	png_private = info->png_private;

	leave = 0;
	while (!leave) {
		switch (png_private->saver_state) {
			case SAVER_STATE_OFF :
				break;
			case SAVER_STATE_START :
				png_private->filter_state = FILTER_SA_STATE_WAIT;
				png_private->saver_state = SAVER_STATE_HEADER;
				break;
			case SAVER_STATE_HEADER :
				/* output the PNG identifier */
				pos = png_private->buffer;
				*pos++ = 137;		/* fixed char to check for 8 bit channel */
				*pos++ = 80;		/* 'P' */
				*pos++ = 78;		/* 'N' */
				*pos++ = 71;		/* 'G' */
				*pos++ = 13;		/*  CR */
				*pos++ = 10;		/*  LF */
				*pos++ = 26;		/*  ^Z to stop printing when catting on some systems */
				*pos++ = 10;		/*  LF */
				png_private->buf_length = 8;
				png_private->saver_state = SAVER_STATE_IDENTIFIED;
				break;
			case SAVER_STATE_IDENTIFIED :
				/* now output the IHDR block first as specified */
				pos = png_private->buffer + png_private->buf_length;
				pos = png_saver_start_block(pos, BLOCK_TYPE_IHDR, png_private, 0);

				WRITE4_BE(pos, info->width);  pos+=4;
				WRITE4_BE(pos, info->height); pos+=4;
				*pos++ = info->bpp;
				*pos++ = (info->colourtype) & 7;
				*pos++ = info->compression;
				*pos++ = info->filter;
				*pos++ = info->interlace;

				pos = png_saver_end_block(pos, png_private);
				png_private->buf_length = pos - png_private->buffer;
				
				png_private->saver_state = SAVER_STATE_SEND_MISC_BLOCKS;
				break;
			case SAVER_STATE_SEND_MISC_BLOCKS :
				if (BUFFER_SIZE - png_private->buf_length >= 4096) {
					/*
					 * we have at least a 4kb space for the blocks to build
					 * if we have a palette, send it straight away... also when we have
					 * a gamma setting or a background colour
					 */
					transparencies = 0;
					colourtype = info->colourtype;
					if (colourtype & PNG_COLOURT_PALETTE) {
						/* output palette */
						pal_entries = 1<<info->bpp;
						if (pal_entries > 256)
							pal_entries = 256;
						flags = BLOCK_ANCILLARY;
						if ((colourtype & PNG_COLOUR_INDEXED) == PNG_COLOUR_INDEXED)
							flags = 0;

						pos = png_private->buffer + png_private->buf_length;
						pos = png_saver_start_block(pos, BLOCK_TYPE_PLTE, png_private, flags);

						for(entry = 0; entry<pal_entries; entry++) {
							*pos++ = (info->palette[entry] >> 16) & 0xff;
							*pos++ = (info->palette[entry] >>  8) & 0xff;
							*pos++ = (info->palette[entry]      ) & 0xff;
							if (((info->palette[entry]>>24) & 0xff) != 255)
								transparencies=1;
						}

						pos = png_saver_end_block(pos, png_private);
						png_private->buf_length = pos - png_private->buffer;
					}
					if (info->has_backgroundcolour) {
						pos = png_private->buffer + png_private->buf_length;
						pos = png_saver_start_block(pos, BLOCK_TYPE_BKGD, png_private, BLOCK_ANCILLARY);

						if (colourtype & PNG_COLOURT_PALETTE) {
							*pos++ = info->background_index;
						}
						if ((colourtype == PNG_COLOUR_GREY_ONLY) || (colourtype == PNG_COLOUR_GREY_ALPHA)) {
							WRITE2_BE(pos, info->background_grey); pos += 2;
						}
						if ((colourtype & (PNG_COLOURT_COLOUR | PNG_COLOURT_PALETTE)) == PNG_COLOURT_COLOUR) {
							WRITE2_BE(pos, info->background_R); pos += 2;
							WRITE2_BE(pos, info->background_G); pos += 2;
							WRITE2_BE(pos, info->background_B); pos += 2;
						}

						pos = png_saver_end_block(pos, png_private);
						png_private->buf_length = pos - png_private->buffer;
					}

					/* transparencies */
					if ((colourtype == PNG_COLOUR_INDEXED) && transparencies) {
						/* indexed colours ... save palette alpha channel */
						pal_entries = 1<<info->bpp;
						if (pal_entries > 256)
							pal_entries = 256;
						pos = png_private->buffer + png_private->buf_length;
						pos = png_saver_start_block(pos, BLOCK_TYPE_TRNS, png_private, BLOCK_ANCILLARY);

						for(entry=0; entry<pal_entries; entry++) {
							*pos++ = (info->palette[entry] >> 24) & 0xff;
						}

						pos = png_saver_end_block(pos, png_private);
						png_private->buf_length = pos - png_private->buffer;
					}
					if ((colourtype == PNG_COLOUR_GREY_ONLY) || (colourtype == PNG_COLOUR_RGB)) {
						if (info->has_transparancy) {
							pos = png_private->buffer + png_private->buf_length;
							pos = png_saver_start_block(pos, BLOCK_TYPE_TRNS, png_private, BLOCK_ANCILLARY);
	
							if (colourtype == PNG_COLOUR_GREY_ONLY) {
								WRITE2_BE(pos, info->transparant_grey); pos+=2;
							} else if (colourtype == PNG_COLOUR_RGB) {
								WRITE2_BE(pos, info->transparant_R);    pos+=2;
								WRITE2_BE(pos, info->transparant_G);    pos+=2;
								WRITE2_BE(pos, info->transparant_B);    pos+=2;
							}
								
							pos = png_saver_end_block(pos, png_private);
							png_private->buf_length = pos - png_private->buffer;
						}
					}

				}
				/* nothing yet */
				png_private->saver_state = SAVER_STATE_START_SENDING_IDATS;
				break;
			case SAVER_STATE_START_SENDING_IDATS :
				/* initialise libz */
				png_private->zlib_state.zalloc = Z_NULL;
				png_private->zlib_state.zfree  = Z_NULL;
				if (deflateInit(&png_private->zlib_state, Z_DEFAULT_COMPRESSION) != Z_OK) {
					info->filestate |= PNG_FILE_ZLIB_ERR;
					png_private->saver_state = SAVER_STATE_ERROR;
					break;
				}
				png_private->z_buf_pos = 0;
				png_private->filter_state = FILTER_SA_STATE_START;
				png_private->saver_state = SAVER_STATE_SEND_IDATS;
				break;
			case SAVER_STATE_SEND_IDATS :
				if (BUFFER_SIZE - png_private->buf_length >= 8096+12) {
					/* mimimum 8kb output block */
					png_saver_filter_fillup_zbuf(info, png_private);
					if (png_private->filter_state == FILTER_SA_STATE_WAIT_FOR_SPACE) leave=1;

					if (png_private->z_buf_pos) {
						pos = png_private->buffer + png_private->buf_length;
						pos = png_saver_start_block(pos, BLOCK_TYPE_IDAT, png_private, 0);

						/* we reserve 4 bytes for sanity and for the CRC */
						png_private->zlib_state.next_in = png_private->z_buf;
						png_private->zlib_state.avail_in = png_private->z_buf_pos;
						png_private->zlib_state.next_out = pos;
						png_private->zlib_state.avail_out = BUFFER_SIZE - png_private->buf_length - 4;

						zflags = Z_SYNC_FLUSH;
						if (png_private->filter_state == FILTER_SA_STATE_FINISHED) {
							zflags = Z_FINISH;
						}
						result = deflate(&png_private->zlib_state, zflags);
						if ((result != Z_OK) && (result != Z_STREAM_END)) {
							info->filestate |= PNG_FILE_ZLIB_ERR;
							png_private->saver_state = SAVER_STATE_ERROR;
							break;
						}
						consumed = png_private->z_buf_pos - png_private->zlib_state.avail_in;
						produced = png_private->zlib_state.next_out-pos;
						memmove(png_private->z_buf, png_private->zlib_state.next_in, ZBUF_SIZE-consumed);

						png_private->z_buf_pos -= consumed;
						pos = png_private->zlib_state.next_out;

						pos = png_saver_end_block(pos, png_private);
						png_private->buf_length = pos - png_private->buffer;
					}

					if (png_private->filter_state == FILTER_SA_STATE_FINISHED) {
						/* we are done so please finish the stream with an IEND */
						pos = png_private->buffer + png_private->buf_length;
						pos = png_saver_start_block(pos, BLOCK_TYPE_IEND, png_private, 0);
						pos = png_saver_end_block(pos, png_private);
						png_private->buf_length = pos - png_private->buffer;

						png_private->saver_state = SAVER_STATE_FINISHED;
						break;
					}

				} else {
					leave = 1;
				}
				break;
			case SAVER_STATE_FINISHED :
				deflateEnd(&png_private->zlib_state);

				info->filestate &= ~PNG_FILE_SAVING;
				info->filestate |=  PNG_FILE_FINISHED;
				leave = 1;
				break;
			case SAVER_STATE_ERROR :
				deflateEnd(&png_private->zlib_state);

				info->filestate &= ~PNG_FILE_SAVING;
				info->filestate |=  PNG_FILE_ERROR;
				leave = 1;
				break;
		}
	}

	return info->filestate;
}

		
png_file_status
png_load_a_piece(struct png_info *info) {
	struct png_private *png_private;
	int32_t bytes_read;

	if (!info)
		return PNG_FILE_ERROR | PNG_FILE_OUT_OF_MEM;
	if (!(info->filestate & PNG_FILE_LOADING))
		return info->filestate;

	/* shortcut */
	png_private = info->png_private;

	do {
		/* read the buffer full */
		bytes_read = read(png_private->fhandle,
				png_private->buffer+png_private->buf_length, BUFFER_SIZE-png_private->buf_length-1);
		if (bytes_read < 0) {
			if (errno != EAGAIN) {
				/* serious error -> cleaning up */
				perror("Reading png file");
				info->filestate = PNG_FILE_ERROR;
				/* XXX cleanup XXX */
			}
			/*
			 * its the statemachine's responsibility to bail out
			 * when it needs more information
			 */
		}
		png_private->buf_length += bytes_read;
		png_loader_statemachine(info);
	} while (bytes_read>0);

	return info->filestate;
}


png_file_status
png_save_a_piece(struct png_info *info) {
	struct png_private *png_private;
	int32_t bytes_written;

	if (!info)
		return PNG_FILE_ERROR;
	if (!(info->filestate & PNG_FILE_SAVING))
		return info->filestate;

	/* shortcut */
	png_private = info->png_private;

	do {
		png_saver_statemachine(info);

		bytes_written = write(png_private->fhandle, png_private->buffer, png_private->buf_length);
		/* fprintf(stderr, "png: bytes_written = %d\n", bytes_written); */

		if (bytes_written < 0) {
			if (errno != EAGAIN) {
				/* serious error -> cleaning up */
				perror("Writing png file");
				info->filestate = PNG_FILE_ERROR;
				/* XXX cleanup XXX */
			}
			/*
			 * state machine will bail out when it its finished
			 * writing or when the buffer is full
			 */
		}
		if (bytes_written > 0) {
			memcpy(png_private->buffer, png_private->buffer+bytes_written, BUFFER_SIZE-bytes_written);
			png_private->buf_length -= bytes_written;
		}
	} while (bytes_written>0);

	return info->filestate;
}


/*
 * Converter routines ... seperate one day ?
 * XXX THEY BOTH SUCK for 16 bit samples.... redo this please !!! XXX
 */

/* XXX change me for pixel pusher XXX */
png_file_status
png_convert_to_rgba32(struct png_info *info, int inverse_alpha) {
	uint8_t *pos;
	uint32_t xp, yp, A, R, G, B;
	uint32_t *outpos, *outblob;
	uint32_t rgb, val, bpp, num_subpixels, subpixel, subpixel_mask, colour;
	uint32_t width, height, colourtype;

	if (!info)
		return PNG_FILE_ERROR | PNG_FILE_OUT_OF_MEM;

	bpp = info->bpp;
	num_subpixels = 8/bpp;
	subpixel_mask = (1<<bpp)-1;

	width = info->width;
	height = info->height;
	colourtype = info->colourtype;

	/* if its allready in the correct form do nothing */
	if (colourtype == PNG_COLOUR_RGBA_EI)
		return info->filestate;

	outblob = malloc((width+1) * (height+1) * sizeof(uint32_t));

	if (!outblob) {
		info->filestate = PNG_FILE_ERROR | PNG_FILE_OUT_OF_MEM;
		return info->filestate;
	}

	outpos = outblob;

	/* the checks for (bpp>8) are to acommodate 16 bit samples */
	pos = info->blob;
	for(yp=0; yp < height; yp++) {
		for(xp=0; xp < width; xp++) {
			switch (colourtype) {
				case PNG_COLOUR_GREY_ONLY :
					if (bpp <= 8) {
						val = *pos++;
						for(subpixel=0; subpixel<num_subpixels; subpixel++) {
							colour = (val >> (8-bpp)) & subpixel_mask;
							colour = ((colour * 255)/subpixel_mask) & 0xff;

							A = 255;
							if (info->has_transparancy && (colour == info->transparant_grey))
								A = 0;
							if (inverse_alpha)
								A = 255-A;
	
							rgb = (A<<24) | (colour << 16 | colour << 8 | colour);
							*outpos++ = rgb;
	
							val = val << bpp;
							xp += 1;
							if (xp >= width)
								break;
						}
						xp -= 1;
					} else {
						val = READ2_BE(pos); pos += 2;
						A = 255;
						if (info->has_transparancy && (val == info->transparant_grey))
							A = 0;
						if (inverse_alpha)
							A = 255-A;

						val >>= 8; /* XXX */
						rgb = (A<<24) | (val << 16 | val << 8 | val);
						*outpos++ = rgb;
					}
					break;
				case PNG_COLOUR_GREY_ALPHA:
					if (bpp <= 8) {
						val = *pos++;
						A   = *pos++;

						for(subpixel=0; subpixel<num_subpixels; subpixel++) {
							colour = (val >> (8-bpp)) & subpixel_mask;
							colour = ((colour * 255)/subpixel_mask) & 0xff;
	
							if (inverse_alpha)
								A = 255-A;
							rgb = (A<<24) | (colour << 16 | colour << 8 | colour);
							*outpos++ = rgb;
	
							val = val << bpp;
							xp += 1;
							if (xp >= width)
								break;
						}
						xp -= 1;
					} else {
						val = READ2_BE(pos); pos += 2; val >>= 8;  /* XXX */
						A   = READ2_BE(pos); pos += 2; A   >>= 8;  /* XXX */

						if (inverse_alpha)
							A = 255-A;
						rgb = (A<<24) | (val << 16 | val << 8 | val);
						*outpos++ = rgb;
					}
					break;
				case PNG_COLOUR_RGB       :
				case PNG_COLOUR_RGBA      :
					if (bpp == 8) {
						R = *pos++;
						G = *pos++;
						B = *pos++;

						A = 255;
						if (colourtype == PNG_COLOUR_RGBA)
							A = *pos++;
						if (info->has_transparancy) {
							if ( (R == info->transparant_R) &&
							     (G == info->transparant_G) &&
							     (B == info->transparant_B)    ) {
								A = 0;
							}
						}
					} else {
						R = READ2_BE(pos); pos += 2;
						G = READ2_BE(pos); pos += 2;
						B = READ2_BE(pos); pos += 2;

						A = 0xffff;
						if (colourtype == PNG_COLOUR_RGBA) {
							A = READ2_BE(pos); pos += 2;
						}
						if (info->has_transparancy) {
							if ( (R == info->transparant_R) &&
							     (G == info->transparant_G) &&
							     (B == info->transparant_B)    ) {
								A = 0;
							}
						}
						A >>= 8; R >>= 8; G >>= 8; B >>= 8; /* XXX */
					}

					if (inverse_alpha)
						A = 255-A;
					rgb = (A<<24) | (R << 16) | (G << 8) | B;
					*outpos++ = rgb;
					break;
				case PNG_COLOUR_INDEXED   :
					val = *pos++;
					for(subpixel=0; subpixel<num_subpixels; subpixel++) {
						colour = (val >> (8-bpp)) & subpixel_mask;

						rgb = info->palette[colour];
						A = (rgb >> 24) & 0xff;
						if (inverse_alpha)
							A = 0xff-A;
						rgb = (rgb & 0x00ffffff) | (A<<24);

						*outpos++ = rgb;

						val = val << bpp;
						xp += 1;
						if (xp >= width)
							break;
					}
					xp -= 1;
					break;
				default :
					fprintf(stderr, "PNG codec rgba32 convert : the colourtype field is invalid !\n");
					exit(1);
			}
		}
	}
	if (info->blob != (uint8_t *) outblob) {
		free(info->blob);
		info->blob = (uint8_t *) outblob;
		info->bpp = 32;
		info->samples_per_pixel = 4;
		info->strave = 4*width;			/* we have RGBA/pixel now	*/
		info->colourtype = PNG_COLOUR_RGBA_EI;	/* extension			*/
	}

	return info->filestate;
}


/* XXX change me for pixel pusher XXX */
png_file_status
png_convert_to_rgba64(struct png_info *info, uint16_t *r_trans, uint16_t *g_trans, uint16_t *b_trans, int inverse_alpha) {
	uint8_t *pos;
	uint32_t xp, yp;
	uint32_t val, bpp, num_subpixels, subpixel, subpixel_mask;
	uint32_t width, height, colourtype;
	uint64_t *outpos, *outblob;
	uint64_t rgb, A, R, G, B, colour;

	if (!info)
		return PNG_FILE_ERROR | PNG_FILE_OUT_OF_MEM;

	bpp = info->bpp;
	num_subpixels = 8/bpp;
	subpixel_mask = (1<<bpp)-1;

	width = info->width;
	height = info->height;
	colourtype = info->colourtype;

	/* if its allready in the correct form do nothing */
	if (colourtype == PNG_COLOUR_RGBA_EI)
		return info->filestate;

	outblob = malloc((width+1) * (height+1) * sizeof(u_int64_t));

	if (!outblob) {
		info->filestate = PNG_FILE_ERROR | PNG_FILE_OUT_OF_MEM;
		return info->filestate;
	}

	outpos = outblob;

	pos = info->blob;
	for(yp=0; yp < height; yp++) {
		for(xp=0; xp < width; xp++) {
			switch (colourtype) {
				case PNG_COLOUR_GREY_ONLY :
					if (bpp <= 8) {
						val = *pos++;
						for(subpixel=0; subpixel<num_subpixels; subpixel++) {
							colour = (val >> (8-bpp)) & subpixel_mask;
							colour = ((colour * 255)/subpixel_mask) & 0xff;
	
							A = 255;
							if (info->has_transparancy && (colour == info->transparant_grey))
								A = 0;
							if (inverse_alpha)
								A = 255-A;
	
							R = r_trans[colour];
							G = g_trans[colour];
							B = b_trans[colour];
							A = 0x101*A;
	
							rgb = (A << 48) | (R << 32) | (G << 16) | (B);
							*outpos++ = rgb;
	
							val = val << bpp;
							xp += 1;
							if (xp >= width)
								break;
						}
						xp -= 1;
					} else {
						colour = READ2_BE(pos); pos += 2;
						A = 0xffff;
						if (info->has_transparancy && (colour == info->transparant_grey))
							A = 0;
						if (inverse_alpha)
							A = 0xffff-A;

						rgb = (A << 48) | (colour << 32) | (colour << 16) | (colour);
						*outpos++ = rgb;
					}
					break;
				case PNG_COLOUR_GREY_ALPHA:
					if (bpp <= 8) {
						val = *pos++;
						A   = *pos++;
	
						for(subpixel=0; subpixel<num_subpixels; subpixel++) {
							colour = (val >> (8-bpp)) & subpixel_mask;
	
							if (inverse_alpha)
								A = 255-A;
							colour = ((colour * 255)/subpixel_mask) & 0xff;
	
							R = r_trans[colour];
							G = g_trans[colour];
							B = b_trans[colour];
							A = 0x101*A;

							rgb = (A << 48) | (R << 32) | (G << 16) | (B);
							*outpos++ = rgb;

							val = val << bpp;
							xp += 1;
							if (xp >= width)
								break;
						}
						xp -= 1;
					} else {
						colour = READ2_BE(pos); pos += 2;
						A      = READ2_BE(pos); pos += 2;

						if (info->has_transparancy && (colour == info->transparant_grey))
							A = 0;
						if (inverse_alpha)
							A = 0xffff-A;

						rgb = (A << 48) | (colour << 32) | (colour << 16) | (colour);
						*outpos++ = rgb;
					}
					break;
				case PNG_COLOUR_RGB       :
				case PNG_COLOUR_RGBA      :
					if (bpp == 8) {
						R = *pos++;
						G = *pos++;
						B = *pos++;

						A = 255;
						if (colourtype == PNG_COLOUR_RGBA) A = *pos++;
						if (info->has_transparancy) {
							if ( (R == info->transparant_R) &&
							     (G == info->transparant_G) &&
							     (B == info->transparant_B) )
							{
								A = 0;
							}
						}

						R = r_trans[R];
						G = g_trans[G];
						B = b_trans[B];
						A = 0x101*A;
					} else {
						R = READ2_BE(pos); pos += 2;
						G = READ2_BE(pos); pos += 2;
						B = READ2_BE(pos); pos += 2;

						A = 0xffff;
						if (colourtype == PNG_COLOUR_RGBA) {
							A = READ2_BE(pos); pos += 2;
						}

						if (info->has_transparancy) {
							if ( (R == info->transparant_R) &&
							     (G == info->transparant_G) &&
							     (B == info->transparant_B) )
							{
								A = 0;
							}
						}
					}

					if (inverse_alpha)
						A = 0xffff-A;
					rgb = (A << 48) | (R << 32) | (G << 16) | (B);
					*outpos++ = rgb;
					break;
				case PNG_COLOUR_INDEXED   :
					val = *pos++;
					for(subpixel=0; subpixel<num_subpixels; subpixel++) {
						colour = (val >> (8-bpp)) & subpixel_mask;

						rgb = info->palette[colour];
						A = (rgb >> 24) & 0xff;
						if (inverse_alpha)
							A = 0xff-A;

						R = r_trans[(rgb >> 16) & 0xff];
						G = g_trans[(rgb >>  8) & 0xff];
						B = b_trans[(rgb      ) & 0xff];
						A = 0x101*A;

						rgb = (A << 48) | (R << 32) | (G << 16) | (B);
						*outpos++ = rgb;

						val = val << bpp;
						xp += 1;
						if (xp >= width)
							break;
					}
					xp -= 1;
					break;
				default :
					fprintf(stderr, "PNG codec rgba64 convert : the colourtype field is invalid !\n");
					exit(1);
			}
		}
	}
	if (info->blob != (uint8_t *) outblob) {
		free(info->blob);
		info->blob = (uint8_t *) outblob;
		info->bpp = 64;
		info->samples_per_pixel = 4;
		info->strave = 8*width;			/* we have RGBA/pixel now	*/
		info->colourtype = PNG_COLOUR_RGBA_EI;	/* extension			*/
	}

	return info->filestate;
}


