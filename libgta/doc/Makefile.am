# Copyright (C) 2010, 2011, 2014
# Martin Lambers <marlam@marlam.de>
#
# Copying and distribution of this file, with or without modification, are
# permitted in any medium without royalty provided the copyright notice and this
# notice are preserved. This file is offered as-is, without any warranty.

EXTRA_DIST = doxyfile.in

dist_doc_DATA = \
	example-basic.c example-basic.cpp \
	example-tags.c example-tags.cpp \
	example-stream-io.c example-stream-io.cpp \
	example-block-io.c example-block-io.cpp

if BUILD_REF

BUILT_SOURCES = reference/index.html
nobase_nodist_doc_DATA = $(wildcard reference/*)
CLEANFILES = $(wildcard reference/*)

reference/index.html: doxyfile $(top_srcdir)/src/gta/gta.h $(top_srcdir)/src/gta/gta.hpp
	$(DOXYGEN) doxyfile

endif
