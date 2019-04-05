## $Id: Makefile.am,v 1.4 2002/06/10 16:22:37 kir Exp $

INCLUDES = -I$(top_srcdir)/include @ORACLE8_INCLUDES@
## Put it here because of conditional compilation
EXTRA_DIST = orcl8sqldb.cpp

liboracle8db_la_LDFLAGS = -module -release @VERSION@

if ORACLE8
lib_LTLIBRARIES = liboracle8db.la
liboracle8db_la_SOURCES = orcl8sqldb.cpp
# Adding -lstdc++ here fixes runtime linking on some platforms
liboracle8db_la_LIBADD = @ORACLE8_LIBS@ -lstdc++
endif
