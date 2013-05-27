/*-
 * Copyright (c) 2011, 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Radoslaw Kujawa and Reinoud Zandijk.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>

#include <sys/syslimits.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplay_usl_io.h>

#include "png_codec.h"
#include "keymap.h"

/* Debugging */
//#define WSDV_DEBUG

static const char* def_wsdisp = "/dev/ttyE0";
static const char* def_wsdispcfg = "/dev/ttyEcfg";
//static const char* def_wsdispstat = "/dev/ttyEstat";
static const char* def_wskbd = "/dev/wskbd0";

int wsdisp_fd;
int wsdispcfg_fd;
int wskbd_fd;

u_int fbinfo_strave;
struct wsdisplay_fbinfo fbinfo;
struct wsdisplay_cmap ocmap;
void *fb = NULL;

/* indicates if we want to use a translation file */
bool flag_use_keymap_file = false;
bool flag_specify_wsdisplay_device = false;

struct wskbd_map_data wskbd_map;

struct file_entry {
	char *path;	
	TAILQ_ENTRY(file_entry) entries;
};

TAILQ_HEAD(file_tailhead, file_entry) files_head;

void *
ws_mmap(int fd, size_t len)
{
	void *addr;
	void *mapped;

	addr = 0;	/* XXX: driver-dependent? */
	
	/* XXX: page size? */	
	mapped = mmap(addr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);	
	
	return mapped;
}


void
ws_cmap_get(int fd, struct wsdisplay_cmap *cmap, u_int size) 
{
	cmap->index = 0;
	cmap->count = size;
	cmap->red   = calloc(size, sizeof(u_int));
	cmap->green = calloc(size, sizeof(u_int));
	cmap->blue  = calloc(size, sizeof(u_int));

	if (ioctl(fd, WSDISPLAYIO_GETCMAP, cmap) == -1) 
		perror("WSDISPLAYIO_GETCMAP ioctl failed");

}


void
ws_cmap_set(int fd, struct wsdisplay_cmap *cmap)
{
	if (ioctl(fd, WSDISPLAYIO_PUTCMAP, cmap) == -1) 
		perror("WSDISPLAYIO_PUTCMAP ioctl failed");
}


void
png_cmap_to_ws_cmap(struct png_info *info, struct wsdisplay_cmap *cmap, int size)
{
	int i;

	cmap->index = 0;
	cmap->count = size;
	cmap->red   = calloc(size, sizeof(u_int));
	cmap->green = calloc(size, sizeof(u_int));
	cmap->blue  = calloc(size, sizeof(u_int));

	if (size > 256) {
		fprintf(stderr, "Indexed palette is %d entries, can't handle correctly\n", size);
		size = 256;
	}
	for (i = 0; i < size; i++) {
		cmap->red[i]   = (u_int) ((info->palette[i] >> 16) & 0xff);
		cmap->green[i] = (u_int) ((info->palette[i] >>  8) & 0xff);
		cmap->blue[i]  = (u_int) ((info->palette[i]      ) & 0xff);
	}
}


void
ws_get_display_geom(char *wsdisp)
{
	if (ioctl(wsdisp_fd, WSDISPLAYIO_GINFO, &fbinfo)) {
		perror("WSDISPLAYIO_GINFO ioctl failed during getting wsdisplay info");
		close(wsdisp_fd);
		exit(EXIT_FAILURE);
	}
	ioctl(wsdisp_fd, WSDISPLAYIO_LINEBYTES, &fbinfo_strave);
}


int
png_load(char *filename, struct png_info **info)
{
	int status;
	int fh;

	fh = open(filename, O_NONBLOCK | O_RDONLY);
	if (fh < 0) {
		fprintf(stderr, "Can't open input image %s\n", filename);
		return 0;
	}

	*info = png_create_png_context();
	status = png_start_loading(*info, fh);
	while (status & PNG_FILE_LOADING) {
		status = png_load_a_piece(*info);
	}
	close(fh);

	if (status & PNG_FILE_ERROR) {
		fprintf(stderr, "Error loading png file\n");
		return 0;
	}

	return 1;
}


int
png_convert2bpp(struct png_info *info, int bpp)
{
	int status;

	switch (bpp) {
	case 8: 
		/* only accept 8 bit images */
		if ((info->colourtype == PNG_COLOUR_INDEXED) && (info->sample_depth == 8))
			return 1;
		/* png_convert_to_8bit_indexed() */
		fprintf(stderr, "Can't convert to 8 bit indexed image yet\n");
		break;
	case 16:
		fprintf(stderr, "No support for 16 bit displays yet\n");
		break;
	case 32:
		status = png_convert_to_rgba32(info, 0);
		if (status & PNG_FILE_ERROR) {
			fprintf(stderr, "Error converting file to rgba32\n");
			return 0;
		}
		return 1;
	default:
		fprintf(stderr, "Can't convert from %d to %d bits yet\n",
			info->sample_depth * info->samples_per_pixel, bpp);
	}
	return 0;
}


bool
ws_prepare_keyboard(void)
{
	if (!wskbd_map.map) {
		wskbd_map.map = calloc(sizeof(struct wscons_keymap), WSKBDIO_MAXMAPLEN);
		if (wskbd_map.map == NULL) {
			wskbd_map.maplen = 0;
			perror("Can't allocate keyboard mapping");
			return false;
		}
	}
	wskbd_map.maplen = WSKBDIO_MAXMAPLEN;
	if (ioctl(wskbd_fd, WSKBDIO_GETMAP, &wskbd_map)) {
		perror("WSKBDIO_GETMAP failed");
		return false;
	}

#ifdef WSDV_DEBUG
	/* print keymapping for debug purposes */
	printf("keymap len = %d, mapping :", wskbd_map.maplen);
	struct wscons_keymap *km = wskbd_map.map;
	int i;
	for (i = 0; i < wskbd_map.maplen; i++) {
		printf("%d => %d, ", i, KS_VALUE(km[i].group1[0]));
	}
	printf("\n");
#endif

#if 0
	/* this fails... but why? */
	int ver = WSKBDIO_EVENT_VERSION;
	if (ioctl(wskbd_fd, WSKBDIO_SETVERSION, &ver) == -1) {
		perror("WSKBDIO_SETVERSION ioctl failed");
		return false;
	}
#endif

	return true;
}	


int
ws_kbd_translate(int kc)
{
	struct wscons_keymap *km;

	if (flag_use_keymap_file) 
		return wsdv_keycode_translate(KS_VALUE(kc) & 0xff);
	
	if (wskbd_map.maplen == 0)
		return WSDV_EXIT;

	km = wskbd_map.map;
	return KS_VALUE(km[kc].group1[0]);
}


void
ws_restore_screen(void) 
{
	int wsmode;

	if (fbinfo.depth == 8)
		ws_cmap_set(wsdisp_fd, &ocmap);

	/* TODO: munmap */

	wsmode = WSDISPLAYIO_MODE_EMUL;
	if (ioctl(wsdisp_fd, WSDISPLAYIO_SMODE, &wsmode) == -1) {
		perror("WSDISPLAYIO_SMODE ioctl failed during restore");
	}
}


bool
ws_prepare_screen(void)
{
	size_t display_size;
	int wsmode;

	/* imho this cannot work, descriptors will be closed at this moment */
	/*atexit(ws_restore_screen);*/

	wsmode = WSDISPLAYIO_MODE_MAPPED;
	if (ioctl(wsdisp_fd, WSDISPLAYIO_SMODE, &wsmode) == -1) {
		perror("WSDISPLAYIO_SMODE ioctl failed");
		return false;
	}

	if (fbinfo.depth == 8) 
		ws_cmap_get(wsdisp_fd, &ocmap, fbinfo.cmsize);

	display_size = fbinfo_strave * fbinfo.height;
	if (fb == NULL)
		fb = ws_mmap(wsdisp_fd, display_size);

	return true;
}	

void
ws_display_png(struct png_info *info, void *fb)
{
	struct wsdisplay_cmap cmap;
	u_int png_strave, skip_lines, skip_pixels;
	int pixel_bytes, y;
	char *opos, *ipos;

	/* check if it will fit the display */
	if ((info->width > fbinfo.width) || (info->height > fbinfo.height)) {
		fprintf(stderr, "PNG size (%d, %d) will not fit screen (%d, %d)\n",
			info->width, info->height, fbinfo.width, fbinfo.height);
		return;
	}

	if (!png_convert2bpp(info, fbinfo.depth))
		return;

	pixel_bytes = fbinfo.depth / 8;

	png_strave   = info->strave;

#ifdef WSDV_DEBUG
	printf("fbinfo.width   = %d\n", fbinfo.width);
	printf("fbinfo.height  = %d\n", fbinfo.height);
	printf("png_bpp        = %d\n", info->bpp);
	printf("png_sample_depth = %d\n", info->sample_depth);
	printf("png_samples_per_pixel = %d\n", info->samples_per_pixel);
	printf("fbinfo_strave  = %d\n", (int) fbinfo_strave);
	printf("png_strave     = %d\n", (int) png_strave);
	printf("display_size   = %d\n", (int) display_size);
	printf("pixel_bytes = %d\n", pixel_bytes);
#endif

	if (fbinfo.depth == 8) {
		png_cmap_to_ws_cmap(info, &cmap, fbinfo.cmsize);
		ws_cmap_set(wsdisp_fd, &cmap);
	}

	/* clear screen */
	opos = fb;
	for (y = 0; y < fbinfo.height; y++) {
		memset(opos, 0, fbinfo.width * pixel_bytes);
		opos += fbinfo_strave;
	}

	/* center image on screen */
	skip_lines = (fbinfo.height - info->height) / 2;
	skip_pixels = (fbinfo.width  - info->width) / 2;
	opos = (char *) fb + skip_lines*fbinfo_strave + skip_pixels * pixel_bytes;
	ipos = (char *) info->blob;
	for (y = 0; y < info->height; y++) {
		memcpy(opos, ipos, png_strave);
		ipos += png_strave;
		opos += fbinfo_strave;
	}
}

void
wsdv_usage(char *progname)
{
	fprintf(stderr, "usage: %s [-m wsdisplay] [-k wskbd] [-t keymap] file.png\n", 
	    progname);
}

void
wsdv_display_file(char *path) 
{
	struct png_info *png;

#ifdef WSDV_DEBUG
	printf("loading file %s\n", path);
#endif

	if (png_load(path, &png))
		ws_display_png(png, fb);

	png_dispose_png(png);
}

#if 0
/* Return number of currently active wsdispaly screen. */
int
wsdv_screen_get_active()
{
	int screen;

	if (ioctl(wsdispcfg_fd, VT_GETACTIVE, &screen) == -1) {
		perror("Can't get active screen! Forgot WSDISPLAY_COMPAT_USL?\n");
		return -1;
	}

	printf("active screen is %d\n", screen);
	
	return screen;
}

bool
wsdv_screen_switch(int screen)
{

	printf("going to switch to screen %d\n", screen);

	if (ioctl(wsdispcfg_fd, VT_ACTIVATE, screen) == -1) {
		perror("Can't switch screen");
	}
	
	return true;
}
#endif 

void
wsdv_process_file_list() 
{
	struct file_entry *file, *nfile;
	struct wscons_event event;

	file = TAILQ_FIRST(&files_head);

	if (file == NULL)
		fprintf(stderr, "file list empty, shouldn't happen\n");	

	wsdv_display_file(file->path);

	for (;;) {
		read(wskbd_fd, &event, sizeof(event));
		// printf("event type %x value %x\n", event.type, event.value);
		if (event.type == WSCONS_EVENT_KEY_DOWN)
			switch (ws_kbd_translate(event.value)) {
			case WSDV_EXIT:
				return;
				break;
			case WSDV_NEXTIMG:
				nfile = TAILQ_NEXT(file, entries);
				if (!nfile)
					break;
				file = nfile;
				wsdv_display_file(file->path);
				break;
			case WSDV_PREVIMG:
				nfile = TAILQ_PREV(file, file_tailhead, entries);
				if (!nfile)
					break;
				file = nfile;
				wsdv_display_file(file->path);
				break;
#if 0
			case WSDV_SWITCH_SCREEN_2:
				ws_restore_screen();
				close(wskbd_fd);
				close(wsdisp_fd);
				wsdv_screen_get_active();
				wsdv_screen_switch(2);
				close(wsdispcfg_fd);
				break;
#endif
			default:
				printf("unhandled key value %x\n", 
				    event.value);
			}
	}

}

/*
 * Try to determine default wsdisplay device, not hardcode it... 
 * This needs some cleanup and much more error checking. 
 */
char *
wsdv_display_get_default() 
{
	int i;
	char *ttypath, *ttydevname;
	struct stat s;
	dev_t ttydev;
	devmajor_t wsdisp_major;
	size_t miblen;
	struct kinfo_drivers *kern_drivers;
	static int mib[2] = {CTL_KERN, KERN_DRIVERS};

	wsdisp_major = 0;
	ttypath = (char*) malloc(sizeof(char)*PATH_MAX);
	memset(ttypath, 0, PATH_MAX);
	ttydev = 0;

	sysctl(mib, 2, NULL, &miblen, NULL, 0);
	kern_drivers = malloc(miblen);
	sysctl(mib, 2, kern_drivers, &miblen, NULL, 0);

	/* obtain device major number for wsdisplay driver */
	for (i = 0; i < miblen / sizeof(*kern_drivers); kern_drivers++, i++) {
		if (strcmp("wsdisplay", kern_drivers->d_name))
			continue;
		wsdisp_major = kern_drivers->d_cmajor;
		break;
	}

	if (wsdisp_major == 0)
		err(EXIT_FAILURE, 
		    "could not deterine wsdisplay device major number\n");
			
	if (isatty(0)) {
		ttypath = ttyname(0);
	
		if (stat(ttypath, &s) == -1)
			perror("error stating stdin");

		ttydev = s.st_dev; 
	}

	/* if stdin is not a tty or is a tty that is not a wsdisplay */
	if (ttydev == 0 || (major(ttydev) != wsdisp_major)) {
		ttypath = strcpy(ttypath, "/dev/");
		ttydevname = devname(makedev(wsdisp_major, 0), S_IFCHR);
		strncat(ttypath, ttydevname, strlen(ttydevname));
	}

	if (stat(ttypath, &s) == -1) {
		perror("error stating wsdisplay device file");
		/* fall back to hard coded default */
		strncpy(ttypath, def_wsdisp, strlen(def_wsdisp));
	}

	return ttypath;
}

int
main(int argc, char *argv[]) 
{
	int ch;
	char wsdisp[PATH_MAX];
	char wsdispcfg[PATH_MAX];
	char wskbd[PATH_MAX];
	char keymap[PATH_MAX];
	char progname[PATH_MAX];
	struct file_entry *file;

	fb = NULL;

	TAILQ_INIT(&files_head); 

	strncpy(progname, argv[0], sizeof(progname));
	strncpy(wsdispcfg, def_wsdispcfg, sizeof(wsdispcfg));
	strncpy(wskbd, def_wskbd, sizeof(wskbd));

	flag_use_keymap_file = false;
	while ((ch = getopt(argc, argv, "m:k:t:")) != -1) {

		switch (ch) {
		case 'm':
			flag_specify_wsdisplay_device = true;
			strncpy(wsdisp, optarg, sizeof(wsdisp));	
			break;
		case 'k':
			strncpy(wskbd, optarg, sizeof(wskbd));	
			break;
		case 't':
			strncpy(keymap, optarg, sizeof(keymap));	
			flag_use_keymap_file = true;
			break;
		default:
			wsdv_usage(progname);
			return EXIT_FAILURE;
		}
	}
	argc -= optind;
	argv += optind;

	if (*argv == NULL) {
		wsdv_usage(progname);
		return EXIT_FAILURE;
	}

	if (flag_use_keymap_file)
		wsdv_keymap_load(keymap);

	while (*argv != NULL) {
		printf("got filename arg %s\n", *argv);
		file = (struct file_entry*) malloc(sizeof(struct file_entry));
		file->path = *argv;
		TAILQ_INSERT_TAIL(&files_head, file, entries);
		argv++;
	}
	
	if (!flag_specify_wsdisplay_device) {
		strncpy(wsdisp, wsdv_display_get_default(), sizeof(wsdisp));	
	}

	printf("using wsdisplay dev %s, wskdb dev %s\n", 
	    wsdisp, wskbd);

	wskbd_fd = open(wskbd, O_RDWR);
	wsdisp_fd = open(wsdisp, O_RDWR);
	wsdispcfg_fd = open(wsdisp, O_RDWR);

	if (wsdisp_fd < 0) {
		perror("Can't open wsdisplay dev");
		return EXIT_FAILURE;
	}
	if (wskbd_fd < 0) {
		perror("Can't open wskbd dev");
		return EXIT_FAILURE;
	}

	png_init();
	ws_get_display_geom(wsdisp);

	if (!ws_prepare_keyboard())
		return EXIT_FAILURE;

	if (!ws_prepare_screen()) {
		ws_restore_screen();
		return EXIT_FAILURE;
	}

	wsdv_process_file_list();

	ws_restore_screen();

	close(wsdisp_fd);
	close(wskbd_fd);

	return EXIT_SUCCESS;
}

