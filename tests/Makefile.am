SUBDIRS = buffer

# Make the tests run against the build and not the installation
TESTS_ENVIRONMENT = \
	GEGL_PATH=$(top_builddir)/operations/common:$(top_builddir)/operations/core:$(top_builddir)/operations/external:$(top_builddir)/operations/affine:$(top_builddir)/operations/generated

# The tests
TESTS = \
	test-change-processor-rect	\
	test-proxynop-processing	\
	test-color-op			\
	test-gegl-rectangle 

if HAVE_GPU
TESTS += \
	test-gegl-gpu-texture-clear \
	test-gegl-gpu-texture-clear-subrect \
	test-gegl-gpu-texture-set \
	test-gegl-gpu-texture-set-subrect \
	test-gegl-gpu-texture-copy \
	test-gegl-gpu-texture-copy-subrect \
	test-gegl-gpu-texture-dup  \
	test-gegl-tile-lock-mode-write-then-read \
	test-gegl-tile-lock-mode-gpu-write-then-read \
	test-gegl-tile-lock-mode-write-then-gpu-read \
	test-gegl-tile-lock-mode-gpu-write-then-gpu-read \
	test-gegl-buffer-gpu-scale-normal \
	test-gegl-tile-cow \
	test-gegl-tile-cow-consistency 
endif

noinst_PROGRAMS = $(TESTS)

# Common CPPFLAGS
AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir)/gegl \
	-I$(top_srcdir)/gegl \
	-I$(top_builddir)/gegl/buffer \
	-I$(top_srcdir)/gegl/buffer \
	-I$(top_builddir)/gegl/property-types \
	-I$(top_srcdir)/gegl/property-types \
	-I$(top_builddir)/gegl/operation \
	-I$(top_srcdir)/gegl/operation \
	-I$(top_builddir)/gegl/gpu \
	-I$(top_srcdir)/gegl/gpu

AM_CFLAGS = $(DEP_CFLAGS) $(BABL_CFLAGS)

# Common libs
LIBS = $(top_builddir)/gegl/libgegl-$(GEGL_API_VERSION).la	\
	$(DEP_LIBS) $(BABL_LIBS)
