## $Id: Makefile.am,v 1.4 2002/06/10 16:22:37 kir Exp $

INCLUDES = -I$(top_srcdir)/include @MYSQL_INCLUDES@
EXTRA_DIST = mysqldb.cpp

libmysqldb_la_LDFLAGS = -module -release @VERSION@

if MYSQL
lib_LTLIBRARIES = libmysqldb.la
libmysqldb_la_SOURCES = mysqldb.cpp
# Adding -lstdc++ here fixes runtime linking on some platforms
libmysqldb_la_LIBADD = @MYSQL_LIBS@ -lstdc++
endif
