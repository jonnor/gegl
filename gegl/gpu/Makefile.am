AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir)/gegl \
	-I$(top_srcdir)/gegl \
	-I$(top_builddir)/gegl/property-types \
	-I$(top_srcdir)/gegl/property-types \
	-I$(top_builddir)/gegl/buffer \
	-I$(top_srcdir)/gegl/buffer \
	-I$(top_builddir)/gegl/gpu \
	-I$(top_srcdir)/gegl/gpu

AM_CFLAGS = $(DEP_CFLAGS) $(BABL_CFLAGS) $(GPU_CFLAGS)

LIBS = $(GPU_LIBS)

noinst_LTLIBRARIES = libgpu.la

#libgpu_publicdir = $(includedir)/gegl-$(GEGL_API_VERSION)/gegl/gpu
#libgpu_public_HEADERS = #

libgpu_la_SOURCES = \
	gegl-gpu-init.c \
	gegl-gpu-texture.c \
	\
	gegl-gpu-init.h \
	gegl-gpu-texture.h \
	gegl-gpu-types.h

#libgpu_la_SOURCES = $(libgpu_sources) $(libgpu_public_HEADERS)

