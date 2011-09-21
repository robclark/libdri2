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

#include "dri2test.h"

static Bool WireToEvent(Display *dpy, XExtDisplayInfo *info,
		XEvent *event, xEvent *wire)
{
	switch ((wire->u.u.type & 0x7f) - info->codes->first_event) {

	case DRI2_BufferSwapComplete:
	{
//		xDRI2BufferSwapComplete *awire = (xDRI2BufferSwapComplete *)wire;
		MSG("BufferSwapComplete");
		return True;
	}
	case DRI2_InvalidateBuffers:
	{
//		xDRI2InvalidateBuffers *awire = (xDRI2InvalidateBuffers *)wire;
		MSG("InvalidateBuffers");
//		dri2InvalidateBuffers(dpy, awire->drawable);
		return False;
	}
	default:
		/* client doesn't support server event */
		break;
	}

	return False;
}

static Status EventToWire(Display *dpy, XExtDisplayInfo *info,
		XEvent *event, xEvent *wire)
{
   switch (event->type) {
   default:
      /* client doesn't support server event */
      break;
   }

   return Success;
}

static const DRI2EventOps ops = {
		.WireToEvent = WireToEvent,
		.EventToWire = EventToWire,
};

static int dri2_connect(Display *dpy, char **driver)
{
	int eventBase, errorBase, major, minor;
	char *device;
	drm_magic_t magic;
	Window root;
	int fd;

	if (!DRI2InitDisplay(dpy, &ops)) {
		ERROR_MSG("DRI2InitDisplay failed");
		return -1;
	}

	if (!DRI2QueryExtension(dpy, &eventBase, &errorBase)) {
		ERROR_MSG("DRI2QueryExtension failed");
		return -1;
	}

	MSG("DRI2QueryExtension: eventBase=%d, errorBase=%d", eventBase, errorBase);

	if (!DRI2QueryVersion(dpy, &major, &minor)) {
		ERROR_MSG("DRI2QueryVersion failed");
		return -1;
	}

	MSG("DRI2QueryVersion: major=%d, minor=%d", major, minor);

	root = RootWindow(dpy, DefaultScreen(dpy));

	if (!DRI2Connect(dpy, root, driver, &device)) {
		ERROR_MSG("DRI2Connect failed");
		return -1;
	}

	MSG("DRI2Connect: driver=%s, device=%s", *driver, device);

	fd = open(device, O_RDWR);
	if (fd < 0) {
		ERROR_MSG("open failed");
		return fd;
	}

	if (drmGetMagic(fd, &magic)) {
		ERROR_MSG("drmGetMagic failed");
		return -1;
	}

	if (!DRI2Authenticate(dpy, root, magic)) {
		ERROR_MSG("DRI2Authenticate failed");
		return -1;
	}

	return fd;
}

#ifdef HAVE_NOUVEAU
extern Backend nouveau_backend;
#endif

#ifdef HAVE_OMAP
extern Backend omap_backend;
#endif

/* stolen from modetest.c */
static void fill(char *virtual, int n, int width, int height, int stride)
{
	int i, j;
    /* paint the buffer with colored tiles */
    for (j = 0; j < height; j++) {
            uint32_t *fb_ptr = (uint32_t*)((char*)virtual + j * stride);
            for (i = 0; i < width; i++) {
                    div_t d = div(n+i, width);
                    fb_ptr[i] =
                            0x00130502 * (d.quot >> 6) +
                            0x000a1120 * (d.rem >> 6);
            }
    }
}


int main(int argc, char **argv)
{
	static unsigned attachments[] = {
			DRI2BufferFrontLeft,
			DRI2BufferBackLeft,
	};
	Display *dpy;
	Window win;
	Backend *backend = NULL;
	DRI2Buffer *dri2bufs;
	Buffer *bufs;
	char *driver;
	int fd, nbufs, i, w, h;

	dpy = XOpenDisplay(NULL);
	win = XCreateSimpleWindow(dpy, RootWindow(dpy, 0),
			1, 1, WIDTH, HEIGHT, 0, BlackPixel (dpy, 0), BlackPixel(dpy, 0));
	XMapWindow(dpy, win);
	XFlush(dpy);

	if ((fd = dri2_connect(dpy, &driver)) < 0) {
		return -1;
	}

#ifdef HAVE_NOUVEAU
	if (!strcmp(driver, "nouveau")) {
		backend = &nouveau_backend;
	}
#endif

#ifdef HAVE_OMAP
	if (!strcmp(driver, "omap")) {
		backend = &omap_backend;
	}
#endif

	if (!backend) {
		ERROR_MSG("no suitable backend DRM driver found");
		return -1;
	}

	backend->setup(fd);

	DRI2CreateDrawable(dpy, win);

	dri2bufs = DRI2GetBuffers(dpy, win, &w, &h, attachments, 2, &nbufs);
	if (!dri2bufs) {
		ERROR_MSG("DRI2GetBuffers failed");
		return -1;
	}

	MSG("DRI2GetBuffers: w=%d, h=%d, nbufs=%d", w, h, nbufs);

	bufs = calloc(nbufs, sizeof(Buffer));

	for (i = 0; i < nbufs; i++) {
		bufs[i].dri2buf = &dri2bufs[i];
		bufs[i].hdl = backend->init(bufs[i].dri2buf);
	}

	for (i = 0; i < NFRAMES; i++) {
		CARD64 count;

		char *buf = backend->prep(bufs[i % nbufs].hdl);
		fill(buf, i, w, h, bufs[i % nbufs].dri2buf->pitch);
		backend->fini(bufs[i % nbufs].hdl);
		DRI2SwapBuffers(dpy, win, 0, 0, 0, &count);
		MSG("DRI2SwapBuffers: count=%lu", count);
		if (i > 0) {
			/* XXX wait.. */
		}
	}

	return 0;
}
