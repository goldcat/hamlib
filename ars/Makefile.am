AM_CFLAGS = @AM_CFLAGS@ $(PTHREAD_CFLAGS)

pkglib_LTLIBRARIES = hamlib-ars.la
hamlib_ars_la_SOURCES = ars.c
hamlib_ars_la_LDFLAGS = -no-undefined -module -avoid-version $(PTHREAD_LIBS)
hamlib_ars_la_LIBADD = $(top_builddir)/lib/libmisc.la \
				$(top_builddir)/src/libhamlib.la

noinst_HEADERS = ars.h

EXTRA_DIST = Android.mk
