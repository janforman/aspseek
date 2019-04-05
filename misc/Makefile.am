## $Id: Makefile.am,v 1.8 2002/12/18 16:43:19 kir Exp $

EXTRA_DIST = \
 aspseek.spec.in config.guess config.sub expand-prefix.in install-sh \
 missing mkinstalldirs net.m4 utils.m4 stl-ext.m4 \
 aspseek-mysql-postinstall.in aspseek.in robotstxt-entry

# NOTE: do not include faq2html into EXTRA_DIST since it is needed
# only by webmaster of www.aspseek.org

if MYSQL
sbin_SCRIPTS = aspseek-mysql-postinstall
endif
