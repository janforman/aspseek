## $Id: Makefile.am,v 1.42 2004/01/14 11:13:01 kir Exp $

SUBDIRS = mysql oracle8i

bin_PROGRAMS = s.cgi
noinst_PROGRAMS = gen_rfc2396 @APACHE_MODULE@
EXTRA_PROGRAMS = mod_aspseek.so
EXTRA_DIST = mod_aspseek.c

sbin_PROGRAMS = index searchd
INCLUDES = -I$(top_srcdir)/include @INCLUDES@
lib_LTLIBRARIES = libaspseek.la
libaspseek_la_LIBADD = -lstdc++

s_cgi_SOURCES = sc.c
s_cgi_LDADD = libaspseek.la
# Uncomment the next line to have s.cgi statically linked with libaspseek
#s_cgi_LDFLAGS = -static

parse.cpp: rfc2396.h
gen_rfc2396_SOURCES = gen_rfc2396.c
rfc2396.h: gen_rfc2396
	./gen_rfc2396 >$@

libaspseek_la_SOURCES = aspseek.cpp logger.cpp datasource.cpp templates.cpp \
 protocol.cpp charsets.cpp sock.cpp datetime.cpp timer.cpp crc32.cpp \
 ucharset.cpp
libaspseek_la_CXXFLAGS = $(AM_CXXFLAGS)

index_SOURCES= filters.cpp resolve.cpp deltas.cpp wcache.cpp content.cpp \
 squeue.cpp hrefs.cpp index.cpp sqldb.cpp sqldbi.cpp parse.cpp config.cpp \
 md5.cpp charsets.cpp rank.cpp sock.cpp misc.cpp logger.cpp stopwords.cpp \
 datetime.cpp lastmod.cpp geo.cpp timer.cpp mimeconv.cpp ucharset.cpp \
 buddy.cpp delmap.cpp
#index_LDFLAGS = -export-dynamic
index_LDADD = @COMMON_LIBS@ @SSL_LIBS@

searchd_SOURCES = daemon.cpp excerpts.cpp qparser.cpp sqldb.cpp parser.cpp \
 scanner.cpp streams.cpp charsets.cpp search.cpp sock.cpp protocol.cpp \
 logger.cpp stopwords.cpp affix.cpp datetime.cpp lastmod.cpp urlfilter.cpp \
 misc.cpp acl.cpp timer.cpp cache.cpp crc32.cpp ucharset.cpp
#searchd_LDFLAGS = -export-dynamic
searchd_LDADD = @COMMON_LIBS@

if APACHEMODULE
mod_aspseek_so_SOURCES = mod_aspseek.c
mod_aspseek_so_LDADD = libaspseek.la

# Rules specific for Apache module
#APXS = @APXS@
mod_aspseek.so$(EXEEXT): $(mod_aspseek_so_SOURCES) libaspseek.la
	if test "x$(APACHE_MODULE)" != "x"; then \
		$(LIBTOOL) --mode=link --tag=CC $(APXS) $(INCLUDES) -c \
			-o $@ $(DEFS) $(mod_aspseek_so_SOURCES) \
			libaspseek.la $(COMMON_LIBS) $(LIBS); \
	fi

#APACHE_MODDIR = @APACHE_MODDIR@
install-exec-local: mod_aspseek.so$(EXEEXT)
	if test "x$(APACHE_MODULE)" != "x"; then \
		$(mkinstalldirs) $(DESTDIR)$(APACHE_MODDIR); \
		$(LIBTOOL) --mode=install $(INSTALL) mod_aspseek.so \
			$(DESTDIR)$(APACHE_MODDIR); \
	fi
endif

# Stuff below is to disable -Ox optimisations for some files
#noopt_CXXFLAGS=@NOOPT_CXXFLAGS@
#noopt_CXXCOMPILE=$(CXX) $(DEFS) $(INCLUDES) $(AM_CPPFLAGS) $(CPPFLAGS) \
# $(AM_CXXFLAGS) $(noopt_CXXFLAGS)

# Please enter rule for every source file you want to be compiled without -Ox
#parser.o: parser.cpp
#	$(noopt_CXXCOMPILE) -c $<

# This one removes ending whitespaces
fixwsp:
	for f in $(SOURCES); do \
	 echo -e ',s/[[:space:]]*$$//g\nwq' | ed $$f; \
	done

clean-local:
	rm -f rfc2396.h
