#SUBDIRS = testsuite

EXTRA_DIST = \
	xml_insert.sh \
	create-reference.rb \
	gobj2dot.rb

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir)/gegl \
	-I$(top_srcdir)/gegl \
	-I$(top_builddir)/gegl/buffer \
	-I$(top_srcdir)/gegl/buffer \
	-I$(top_builddir)/gegl/module \
	-I$(top_srcdir)/gegl/module \
	-I$(top_builddir)/gegl/property-types \
	-I$(top_srcdir)/gegl/property-types \
	-I$(top_builddir)/gegl/gpu \
	-I$(top_srcdir)/gegl/gpu

AM_CFLAGS = $(DEP_CFLAGS) $(BABL_CFLAGS)

AM_LDFLAGS = \
	$(top_builddir)/gegl/libgegl-$(GEGL_API_VERSION).la \
	$(DEP_LIBS) $(BABL_LIBS)

noinst_PROGRAMS = introspect operation_reference
