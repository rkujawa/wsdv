/* $$
 *
 * png_codec.h
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


#ifndef _PNG_CODEC_H
#define _PNG_CODEC_H

#include <sys/types.h>
#ifndef NO_STDINT
#	include <stdint.h>
#endif

#include <stdio.h>
#include <zlib.h>

/* png colour type flags */
#define PNG_COLOURT_PALETTE	(0x01)
#define PNG_COLOURT_COLOUR	(0x02)
#define PNG_COLOURT_ALPHA	(0x04)
#define PNG_COLOURT_EI		(0x80)		/* own extension */

/* the legal png colour sample type flags */
#define PNG_COLOUR_GREY_ONLY	(0x00)
#define PNG_COLOUR_RGB		(PNG_COLOURT_COLOUR)
#define PNG_COLOUR_INDEXED	(PNG_COLOURT_PALETTE | PNG_COLOURT_COLOUR)
#define PNG_COLOUR_GREY_ALPHA	(PNG_COLOURT_ALPHA)
#define PNG_COLOUR_RGBA		(PNG_COLOURT_COLOUR | PNG_COLOURT_ALPHA)
#define PNG_COLOUR_RGBA_EI	(PNG_COLOUR_RGBA | PNG_COLOURT_EI)


typedef int png_file_status;
#define PNG_FILE_CLEAR		((png_file_status) (0x0000))
#define PNG_FILE_ERROR		((png_file_status) (0x0001))
#define	PNG_FILE_LOADING	((png_file_status) (0x0002))
#define	PNG_FILE_SAVING		((png_file_status) (0x0004))
#define PNG_FILE_IS_DRAWABLE	((png_file_status) (0x0008))
#define PNG_FILE_FINISHED	((png_file_status) (0x0010))
#define PNG_FILE_NO_PNG		((png_file_status) (0x0020))
#define PNG_FILE_OUT_OF_SPECS	((png_file_status) (0x0040))
#define PNG_FILE_IMP_LIMIT	((png_file_status) (0x0080))
#define PNG_FILE_CRC_ERR	((png_file_status) (0x0100))
#define PNG_FILE_ZLIB_ERR	((png_file_status) (0x0200))
#define PNG_FILE_IDAT_ERR	((png_file_status) (0x0400))
#define PNG_FILE_OUT_OF_MEM	((png_file_status) (0x0800))
#define PNG_FILE_DISPOSED	((png_file_status) (0x1000))
#define PNG_FILE_WOULD_DESTROY	((png_file_status) (0x2000))
#define PNG_FILE_BAD_FILEHANDLE ((png_file_status) (0x4000))


struct png_private;


/*
 * Contains all information about a png file being loaded/saved
 */
struct png_info {
	/* stuff filled in by the png_codec for information purposes */
	uint8_t		*blob;

	uint32_t	 width, height;
	uint32_t	 strave;
	uint8_t		 bpp;
	uint8_t		 sample_depth;
	uint8_t		 samples_per_pixel;

	uint8_t		 colourtype;
	uint8_t		 compression;
	uint8_t		 filter;
	uint8_t		 interlace;

	uint8_t		 has_transparancy;
	uint32_t	 transparant_grey;
	uint32_t	 transparant_R;
	uint32_t	 transparant_G;
	uint32_t	 transparant_B;

	uint8_t		 has_backgroundcolour;
	uint32_t	 background_grey;
	uint8_t		 background_index;
	uint32_t	 background_R;
	uint32_t	 background_G;
	uint32_t	 background_B;

	uint32_t	 palette[256];
	png_file_status	 filestate;

	struct png_private *png_private;
};


/* implemented functions; all args are preserved */
extern void png_init(void);


extern struct png_info *png_create_png_context(void);
extern png_file_status png_populate_and_allocate_empty_image(struct png_info *info, int colourtype, int bpp, int width, int height);
extern png_file_status png_populate_with_image(struct png_info *info, int colourtype, void *blob, int bpp, int width, int height);


extern png_file_status png_start_loading(struct png_info *info, int fhandle);
extern png_file_status png_load_a_piece(struct png_info *info);

extern png_file_status png_start_saving(struct png_info *info, int fhandle);
extern png_file_status png_save_a_piece(struct png_info *info);


extern png_file_status png_dispose_png(struct png_info *info);

/* converters */
extern png_file_status png_convert_to_rgba32(struct png_info *info, int inverse_alpha);
extern png_file_status png_convert_to_rgba64(struct png_info *info, uint16_t *r_trans, uint16_t *g_trans, uint16_t *b_trans, int inverse_alpha);


#endif	/* _PNG_CODEC_H */

