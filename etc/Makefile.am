## $Id: Makefile.am,v 1.29 2002/07/01 13:06:11 kir Exp $

sysconf_DATA = aspseek.conf-dist searchd.conf-dist s.htm-dist \
 charsets.conf-dist stopwords.conf-dist ucharset.conf-dist
sysconf_DIST = aspseek.conf-dist.in searchd.conf-dist.in s.htm-dist \
 charsets.conf-dist stopwords.conf-dist ucharset.conf-dist

charset_TABLES = charsets/cp866 charsets/koi8r charsets/cp1251 charsets/iso88595 \
 charsets/mac charsets/iso88591 charsets/cp1250 charsets/cp1253 charsets/iso885913 \
 charsets/iso88592 charsets/iso88594 charsets/iso88597 charsets/iso88599

ucharset_TABLES =        tables/iso8859-1.txt    tables/iso8859-10.txt \
 tables/iso8859-13.txt   tables/iso8859-14.txt   tables/iso8859-15.txt \
 tables/iso8859-2.txt    tables/iso8859-3.txt    tables/iso8859-4.txt \
 tables/iso8859-5.txt    tables/iso8859-6.txt    tables/iso8859-7.txt \
 tables/iso8859-8.txt    tables/iso8859-9.txt    tables/koi8r.txt \
 tables/koi8u.txt        tables/windows-1250.txt tables/windows-1251.txt \
 tables/windows-1252.txt tables/windows-1253.txt tables/windows-1254.txt \
 tables/windows-1255.txt tables/windows-1256.txt tables/windows-1257.txt \
 tables/windows-1258.txt tables/windows-874.txt  tables/README \
 tables/cp852.txt        tables/keybcs2.txt      tables/koi8cs.txt \
 tables/macce.txt        tables/big5.txt         tables/gb2312.txt \
 tables/chinese.txt


stopword_TABLES = stopwords/README stopwords/czech stopwords/danish \
 stopwords/dutch stopwords/english stopwords/french stopwords/german \
 stopwords/italian stopwords/italian-small stopwords/norwegian \
 stopwords/polish stopwords/portuguese \
 stopwords/russian stopwords/slovak stopwords/spanish stopwords/turkish \
 stopwords/ukrainian stopwords/czech.iso88592 stopwords/hungarian \
 stopwords/catalan

langmap_TABLES = langmap/arabic-iso8859_6.lm langmap/arabic-windows1256.lm \
 langmap/greek-iso8859-7.lm \
 langmap/hebrew-iso8859_8.lm langmap/russian-cp1251.lm \
 langmap/russian-cp866.lm langmap/russian-iso88595.lm \
 langmap/russian-koi8r.lm langmap/slovak-windows1250.lm \
 langmap/slovenian-iso8859_2.lm langmap/ukrainian-koi8_r.lm \
 langmap/german-iso8859-1.lm \
 langmap/czech-cp852.lm langmap/czech-iso8859_1.lm \
 langmap/czech-iso8859_2.lm langmap/czech-keybcs2.lm \
 langmap/czech-koi8cs.lm langmap/czech-macce.lm \
 langmap/czech-utf_8.lm

# Needed for uninstall
sql_TABLES=sql/mysql/tables.sql sql/orcl/tables.sql

img_DIST = img/aspseek-big.png img/asps.png img/e_.png img/e.png img/k.png

EXTRA_DIST = ${sysconf_DIST} ${charset_TABLES} ${ucharset_TABLES} \
 ${img_DIST} ${stopword_TABLES} ${langmap_TABLES} \
 sql/mysql/tables.sql.in sql/orcl/tables.sql.in

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)/sql
if MYSQL
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)/sql/mysql
	@for i in sql/mysql/tables.sql; do \
	  if test -f $(srcdir)/$$i; then file=$(srcdir)/$$i; \
	  else file=$$i; fi; \
	  echo " $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/sql/mysql/"; \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/sql/mysql/; \
	done
endif
if ORACLE8
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)/sql/oracle8
	@for i in sql/orcl/tables.sql; do \
	  if test -f $(srcdir)/$$i; then file=$(srcdir)/$$i; \
	  else file=$$i; fi; \
	  echo " $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/sql/oracle8/"; \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/sql/oracle8/; \
	done
endif
if UNICODE
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)/tables
	@list='$(ucharset_TABLES)'; \
	for i in $$list; do \
	  if test -f $(srcdir)/$$i; then file=$(srcdir)/$$i; \
	  else file=$$i; fi; \
	  echo " $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/tables/"; \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/tables/; \
	done
else
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)/charsets
	@list='$(charset_TABLES)'; \
	for i in $$list; do \
	  if test -f $(srcdir)/$$i; then file=$(srcdir)/$$i; \
	  else file=$$i; fi; \
	  echo " $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/charsets/"; \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/charsets/; \
	done
endif
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)/stopwords
	@list='$(stopword_TABLES)'; \
	for i in $$list; do \
	  if test -f $(srcdir)/$$i; then file=$(srcdir)/$$i; \
	  else file=$$i; fi; \
	  echo " $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/stopwords/"; \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/stopwords/; \
	done
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)/langmap
	@list='$(langmap_TABLES)'; \
	for i in $$list; do \
	  if test -f $(srcdir)/$$i; then file=$(srcdir)/$$i; \
	  else file=$$i; fi; \
	  echo " $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/langmap/"; \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/langmap/; \
	done
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)/img
	@list='$(img_DIST)'; \
	for i in $$list; do \
	  if test -f $(srcdir)/$$i; then file=$(srcdir)/$$i; \
	  else file=$$i; fi; \
	  echo " $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/img/"; \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(sysconfdir)/img/; \
	done
	@(cd $(DESTDIR)$(sysconfdir)/; \
	for p in *-dist; do \
	  np=`echo $$p | sed "s/-dist//"`; \
	  if test -f $$np; then \
	    echo "Preserving existing configuration file $$np"; \
	  else \
	      if test $$np = "aspseek.conf" -o $$np = "searchd.conf"; then \
	        grep -v "@CONFIG_LINE_TO_REMOVE@" $$p > $$np; \
	      else \
	        cp $$p $$np; \
	      fi \
	  fi; \
	done \
	)

uninstall-local:
	list='$(sql_TABLES) $(charset_TABLES) $(ucharset_TABLES) $(img_DIST) $(stopword_TABLES) $(langmap_TABLES)'; \
	for p in $$list; do \
	  rm -f $(DESTDIR)$(sysconfdir)/$$p; \
	done
	-rmdir $(DESTDIR)$(sysconfdir)/img
	-rmdir $(DESTDIR)$(sysconfdir)/sql/mysql
	-rmdir $(DESTDIR)$(sysconfdir)/sql/oracle8
	-rmdir $(DESTDIR)$(sysconfdir)/sql
	-rmdir $(DESTDIR)$(sysconfdir)/charsets
	-rmdir $(DESTDIR)$(sysconfdir)/tables
	-rmdir $(DESTDIR)$(sysconfdir)/stopwords
	-rmdir $(DESTDIR)$(sysconfdir)/langmap
	-rmdir $(DESTDIR)$(sysconfdir)
