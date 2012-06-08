/*
 * Copyright © 2011 Texas Instruments, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Soft-
 * ware"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, provided that the above copyright
 * notice(s) and this permission notice appear in all copies of the Soft-
 * ware and that both the above copyright notice(s) and this permission
 * notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABIL-
 * ITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY
 * RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN
 * THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSE-
 * QUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFOR-
 * MANCE OF THIS SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization of
 * the copyright holder.
 *
 * Authors:
 *   Rob Clark (rob@ti.com)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>

#include "dri2util.h"

#define NFRAMES 300
#define WIN_WIDTH  500
#define WIN_HEIGHT 500
#define VID_WIDTH  1920
#define VID_HEIGHT 1080

#define FOURCC(a, b, c, d) ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24 ))
#define FOURCC_STR(str)    FOURCC(str[0], str[1], str[2], str[3])

/* swap these for big endian.. */
#define RED   2
#define GREEN 1
#define BLUE  0

static void fill420(unsigned char *y, unsigned char *u, unsigned char *v,
		int cs /*chroma pixel stride */,
		int n, int width, int height, int stride)
{
	int i, j;

	/* paint the buffer with colored tiles, in blocks of 2x2 */
	for (j = 0; j < height; j+=2) {
		unsigned char *y1p = y + j * stride;
		unsigned char *y2p = y1p + stride;
		unsigned char *up = u + (j/2) * stride * cs / 2;
		unsigned char *vp = v + (j/2) * stride * cs / 2;

		for (i = 0; i < width; i+=2) {
			div_t d = div(n+i+j, width);
			uint32_t rgb = 0x00130502 * (d.quot >> 6) + 0x000a1120 * (d.rem >> 6);
			unsigned char *rgbp = &rgb;
			unsigned char y = (0.299 * rgbp[RED]) + (0.587 * rgbp[GREEN]) + (0.114 * rgbp[BLUE]);

			*(y2p++) = *(y1p++) = y;
			*(y2p++) = *(y1p++) = y;

			*up = (rgbp[BLUE] - y) * 0.565 + 128;
			*vp = (rgbp[RED] - y) * 0.713 + 128;
			up += cs;
			vp += cs;
		}
	}
}

static void fill422(unsigned char *virtual, int n, int width, int height, int stride)
{
	int i, j;
	/* paint the buffer with colored tiles */
	for (j = 0; j < height; j++) {
		uint8_t *ptr = (uint32_t*)((char*)virtual + j * stride);
		for (i = 0; i < width; i+=2) {
			div_t d = div(n+i+j, width);
			uint32_t rgb = 0x00130502 * (d.quot >> 6) + 0x000a1120 * (d.rem >> 6);
			unsigned char *rgbp = &rgb;
			unsigned char y = (0.299 * rgbp[RED]) + (0.587 * rgbp[GREEN]) + (0.114 * rgbp[BLUE]);

			*(ptr++) = y;
			*(ptr++) = (rgbp[BLUE] - y) * 0.565 + 128;
			*(ptr++) = y;
			*(ptr++) = (rgbp[RED] - y) * 0.713 + 128;
		}
	}
}

/* stolen from modetest.c */
static void fill(unsigned char *virtual, int n, int width, int height, int stride)
{
	int i, j;
    /* paint the buffer with colored tiles */
    for (j = 0; j < height; j++) {
            uint32_t *fb_ptr = (uint32_t*)((char*)virtual + j * stride);
            for (i = 0; i < width; i++) {
                    div_t d = div(n+i+j, width);
                    fb_ptr[i] =
                            0x00130502 * (d.quot >> 6) +
                            0x000a1120 * (d.rem >> 6);
            }
    }
}


/* move this somewhere common?  It does seem useful.. */
static Bool is_fourcc(unsigned int val)
{
	char *str = (char *)&val;
	return isalnum(str[0]) && isalnum(str[1]) && isalnum(str[2]) && isalnum(str[3]);
}

#define ATOM(name) XInternAtom(dpy, name, False)

int main(int argc, char **argv)
{
	Display *dpy;
	Window win;
	Backend *backend = NULL;
	DRI2Buffer *dri2bufs;
	Buffer *bufs;
	char *driver;
	unsigned int nformats, *formats, format = 0;
	int fd, nbufs, i;
	CARD32 *pval;

	dpy = XOpenDisplay(NULL);
	win = XCreateSimpleWindow(dpy, RootWindow(dpy, 0), 1, 1,
			WIN_WIDTH, WIN_HEIGHT, 0, BlackPixel (dpy, 0), BlackPixel(dpy, 0));
	XMapWindow(dpy, win);
	XFlush(dpy);

	if ((fd = dri2_connect(dpy, DRI2DriverXV, &driver)) < 0) {
		return -1;
	}

	if (!DRI2GetFormats(dpy, RootWindow(dpy, DefaultScreen(dpy)),
			&nformats, &formats)) {
		ERROR_MSG("DRI2GetFormats failed");
		return -1;
	}

	if (nformats == 0) {
		ERROR_MSG("no formats!");
		return -1;
	}

	/* print out supported formats */
	MSG("Found %d supported formats:", nformats);
	for (i = 0; i < nformats; i++) {
		if (is_fourcc(formats[i])) {
			MSG("  %d: %08x (\"%.4s\")", i, formats[i], (char *)&formats[i]);
		} else {
			MSG("  %d: %08x (device dependent)", i, formats[i]);
		}
	}

	// XXX pick something we understand!
//	format = FOURCC_STR("I420");
	format = FOURCC_STR("YUY2");
//	format = FOURCC_STR("RGB4");

	free(formats);

	backend = get_backend(driver);
	if (!backend) {
		return -1;
	}

	backend->setup(fd);

	DRI2CreateDrawable(dpy, win);

	/* check some attribute.. just to exercise the code-path: */
	if (!DRI2GetAttribute(dpy, win, ATOM("XV_CSC_MATRIX"), &i, &pval)) {
		ERROR_MSG("DRI2GetAttribute failed");
		return -1;
	}

	MSG("Got CSC matrix:");
	print_hex(i*4, (const unsigned char *)pval);

	free(pval);

	unsigned attachments[] = {
			DRI2BufferFrontLeft, 32, /* always requested, never returned */
			1, format, 2, format, 3, format, 4, format,
	};
	dri2bufs = DRI2GetBuffersVid(dpy, win, VID_WIDTH, VID_HEIGHT, attachments, 4, &nbufs);
	if (!dri2bufs) {
		ERROR_MSG("DRI2GetBuffersVid failed");
		return -1;
	}

	MSG("DRI2GetBuffers: nbufs=%d", nbufs);

	bufs = calloc(nbufs, sizeof(Buffer));

	for (i = 0; i < nbufs; i++) {
		bufs[i].dri2buf = &dri2bufs[i];
		bufs[i].hdls[0] = backend->init(bufs[i].dri2buf, 0);
		if (format == FOURCC_STR("I420")) {
			bufs[i].hdls[1] = backend->init(bufs[i].dri2buf, 1);
			bufs[i].hdls[2] = backend->init(bufs[i].dri2buf, 2);
		} else if (format == FOURCC_STR("NV12")) {
			bufs[i].hdls[1] = backend->init(bufs[i].dri2buf, 1);
		}
	}

	for (i = 0; i < NFRAMES; i++) {
		BoxRec b = {
				// TODO change this dynamically..  fill appropriately so
				// the cropped region has different color, or something,
				// so we can see visually if cropping is incorrect..
				.x1 = 0,
				.y1 = 0,
				.x2 = VID_WIDTH,
				.y2 = VID_HEIGHT,
		};
		CARD64 count;

		Buffer *buf = &bufs[i % nbufs];
		int pitch = buf->dri2buf->pitch[0];
		unsigned char *ptr = backend->prep(buf->hdls[0]);
		if (format == FOURCC_STR("I420")) {
			unsigned char *y = ptr;
			unsigned char *u = backend->prep(buf->hdls[1]);
			unsigned char *v = backend->prep(buf->hdls[2]);
			fill420(y, u, v, 1, i, VID_WIDTH, VID_HEIGHT, pitch);
			backend->fini(buf->hdls[2]);
			backend->fini(buf->hdls[1]);
		} else if (format == FOURCC_STR("NV12")) {
			unsigned char *y = ptr;
			unsigned char *u = backend->prep(buf->hdls[1]);
			unsigned char *v = u + 1;
			fill420(y, u, v, 2, i, VID_WIDTH, VID_HEIGHT, pitch);
			backend->fini(buf->hdls[1]);
		} else if (format == FOURCC_STR("YUY2")) {
			fill422(ptr, i, VID_WIDTH, VID_HEIGHT, pitch);
		} else if (format == FOURCC_STR("RGB4")) {
			fill(ptr, i, VID_WIDTH, VID_HEIGHT, pitch);
		}
		backend->fini(buf->hdls[0]);
		DRI2SwapBuffersVid(dpy, win, 0, 0, 0, &count, (i % nbufs) + 1, &b);
		MSG("DRI2SwapBuffersVid: count=%lu", count);
		if (i > 0) {
			/* XXX wait.. */
		}
	}

	return 0;
}
