## $Id: Makefile.am,v 1.36 2004/03/22 13:31:06 kir Exp $
## Process this file with automake to produce Makefile.in
AUTOMAKE_OPTIONS = foreign no-installinfo
SUBDIRS = src doc etc include misc man
txt_DOCS = README INSTALL AUTHORS COPYING NEWS THANKS TODO FAQ README.rpm \
RELEASE-NOTES README.APACHE_MODULE

EXTRA_DIST = acconfig.h ${txt_DOCS}

# create /var
install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)
	@echo
	@echo " ***                                                                      ***"
	@echo " *** Now, after the installation, you need to create necessary SQL stuff. ***"
	@echo " *** So, read INSTALL file and your RDBMS manual.                         ***"
if MYSQL
	@echo " ***                                                                      ***"
	@echo " *** If you are using MySQL, you may have find useful script named        ***"
	@echo
	@echo "        $(DESTDIR)$(sbindir)/aspseek-mysql-postinstall"
	@echo
	@echo " *** It will (hopefully) create all needed SQL stuff for you.             ***"
endif
	@echo " ***                                                                      ***"
	@echo



# Remove dirs if empty
uninstall-local:
	-rmdir $(DESTDIR)$(localstatedir)
	-rmdir $(DESTDIR)$(bindir)
	-rmdir $(DESTDIR)$(sbindir)
	-rmdir $(DESTDIR)$(libdir)
	-rmdir $(DESTDIR)$(prefix)

# This is for making (S)RPM files.
rpms:	dist misc/aspseek.spec
	cp aspseek-$(VERSION).tar.gz `rpm --eval "%{_sourcedir}"`
	rpm -ba --nodeps misc/aspseek.spec

manual:
	make -C man manual
	@echo -e "\nManual is available as man/manual.pdf"

faq.html: $(srcdir)/FAQ
	$(srcdir)/misc/faq2html $< > $@
