lib_LTLIBRARIES = libdri2test.la

libdri2test_la_SOURCES = dri2test.c
libdri2test_la_LIBADD = @DRI2_LIBS@ 
libdri2test_la_CFLAGS = $(CWARNFLAGS) -I$(top_srcdir)/include @DRI2_CFLAGS@


if ENABLE_NOUVEAU
libdri2test_la_SOURCES += dri2test-nouveau.c
libdri2test_la_LIBADD  += @NOUVEAU_LIBS@
libdri2test_la_CFLAGS  += @NOUVEAU_CFLAGS@
endif

if ENABLE_OMAP
libdri2test_la_SOURCES += dri2test-omap.c
libdri2test_la_LIBADD  += @OMAP_LIBS@
libdri2test_la_CFLAGS  += @OMAP_CFLAGS@
endif
