if HAVE_PYTHON
if HAVE_GJS
if HAVE_INTROSPECTION
# List of tests.
TESTS = \
	test-gegl.js \

EXTRA_DIST = $(TESTS)

check-TESTS: $(TESTS)
	LD_LIBRARY_PATH=$(top_builddir)/gegl/.libs:$(LD_LIBRARY_PATH) \
	GI_TYPELIB_PATH=$(top_builddir)/gegl:$(GI_TYPELIB_PATH) \
	$(PYTHON) $(srcdir)/../run-tests.py \
	  --build-dir=$(top_builddir) --src-dir=$(top_srcdir) \
	  $(TESTS)

endif # HAVE_INTROSPECTION
endif # HAVE_GJS
endif # HAVE_PYTHON
