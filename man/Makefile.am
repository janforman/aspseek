## $Id: Makefile.am,v 1.30 2002/12/25 14:01:18 kir Exp $
# Author: Kir Kolyshkin <kir@asplinux.ru>

man_MANS = aspseek.7 index.1 aspseek.conf.5 \
 searchd.1 searchd.conf.5 s.cgi.1 s.htm.5 aspseek-sql.5
man_in = $(patsubst %, %.ep.in, $(man_MANS))
man_ps = $(patsubst %, %.ps, $(man_MANS))
man_txt = $(patsubst %, %.txt, $(man_MANS))
man_html = $(patsubst %, %.php, $(man_MANS))
man_pdf = $(patsubst %, %.pdf, $(man_MANS))
man_pcl = $(patsubst %, %.pcl, $(man_MANS))

# for TOC generation
TOC_TMPL=toc.man.in
TOC_SH=toc.sh

# For HTML generation
FIXHTML=fix-html

# A nice logo
#ASEEK_LOGO=aspseek.eps
#
#EXTRA_DIST = $(man_in) $(TOC_TMPL) $(TOC_SH) $(ASEEK_LOGO) $(FIXHTML)
EXTRA_DIST = $(man_in) $(TOC_TMPL) $(TOC_SH) $(FIXHTML)

EP = sh $(top_builddir)/misc/expand-prefix
GROFF = groff
# this is to produce one big page
GROFF_4ALL = -rC1 -rD1 -rS11
# for postscript
GROFF_OPTIONS_PS = -Tps -t -ma4 -man -msafer
GROFF_OPTIONS_PS4ALL = $(GROFF_OPTIONS_PS) $(GROFF_4ALL)
# for text
GROFF_OPTIONS_TEXT = -Tascii -t -man -msafer
GROFF_OPTIONS_TEXT4ALL = $(GROFF_OPTIONS_TEXT) $(GROFF_4ALL)
# for PCL5 (HP LaserJet4 and up) output
PCL_DUPLEX_PRINTING = -P-d1
GROFF_OPTIONS_PCL = -Tlj4 -t -ma4 -man -msafer -P-pa4 $(PCL_DUPLEX_PRINTING)
GROFF_OPTIONS_PCL4ALL = $(GROFF_OPTIONS_PCL) $(GROFF_4ALL)

# What is needed for www.aspseek.org site
WEB_CONTENTS = faq.php $(man_MANS) $(man_html) $(man_txt) $(man_ps) \
 $(man_pdf) $(man_pcl) manual.ps manual.txt manual.pdf manual.pcl

# FAQ in HTML format
FAQ=$(top_builddir)/FAQ
FAQ2HTML=$(top_builddir)/misc/faq2html

# How to produce faq.php
faq.php: $(FAQ) $(FAQ2HTML)
	$(FAQ2HTML) $(FAQ) > $@

# to make postscript man page
%.1.ps: %.1
	$(GROFF) $(GROFF_OPTIONS_PS) $< > $@
%.5.ps: %.5
	$(GROFF) $(GROFF_OPTIONS_PS) $< > $@
%.7.ps: %.7
	$(GROFF) $(GROFF_OPTIONS_PS) $< > $@

# to create PDF from PS
%.pdf: %.ps
	ps2pdf $< $@

# to make text man page
%.1.txt.os: %.1
	$(GROFF) $(GROFF_OPTIONS_TEXT) $< > $@
%.5.txt.os: %.5
	$(GROFF) $(GROFF_OPTIONS_TEXT) $< > $@
%.7.txt.os: %.7
	$(GROFF) $(GROFF_OPTIONS_TEXT) $< > $@

# to make PCL man page
%.1.pcl: %.1
	$(GROFF) $(GROFF_OPTIONS_PCL) $< > $@
%.5.pcl: %.5
	$(GROFF) $(GROFF_OPTIONS_PCL) $< > $@
%.7.pcl: %.7
	$(GROFF) $(GROFF_OPTIONS_PCL) $< > $@

# remove overstriking produced by groff
%.txt: %.txt.os
	@sed 's/.//g' < $< > $@ && rm -f $<

# to finally substitute all the needed paths, also beautify CVS Date keyword
%.1: %.1.ep
	$(EP) $< && sed 's|"[$$]Date: \([0-9]*/[0-9]*/[0-9]*\) [0-9:]* [$$]"|"\1"|' < $< >$@
%.5: %.5.ep
	$(EP) $< && sed 's|"[$$]Date: \([0-9]*/[0-9]*/[0-9]*\) [0-9:]* [$$]"|"\1"|' < $< >$@
%.7: %.7.ep
	$(EP) $< && sed 's|"[$$]Date: \([0-9]*/[0-9]*/[0-9]*\) [0-9:]* [$$]"|"\1"|' < $< >$@

# to make HTML man page
%.1.php: %.1
	man2html $< > $@
	./$(FIXHTML) $@

%.5.php: %.5
	man2html $< > $@
	./$(FIXHTML) $@

%.7.php: %.7
	man2html $< > $@
	./$(FIXHTML) $@


# to create html file for every man page
html: $(man_html)
php: html

# to create postscript file for every man page
ps: $(man_ps)

# to create one postscript file with all man pages
all.ps: $(man_MANS)
	$(GROFF) $(GROFF_OPTIONS_PS4ALL) $^ > $@

# to create one big text file with all man pages
manual.txt.os: $(man_MANS)
	$(GROFF) $(GROFF_OPTIONS_TEXT4ALL) $^ > $@

# to create text file for every man page
txt: $(man_txt)

# to create archive of files for www.aspseek.org
www.tgz: $(WEB_CONTENTS)
	tar czf $@ $^

www.tbz2: $(WEB_CONTENTS)
	tar cjf $@ $^

## Probably we shouldn't define clean target here...
clean:
	-rm -f *.ps *.1 *.5 *.7 *.ep *.txt *.txt.os *.html *.pdf *.pcl \
	 www.tgz toc.man

# toc.man: $(TOC_TMPL) all.ps Makefile $(ASEEK_LOGO)
toc.man: $(TOC_TMPL) all.ps Makefile
	sed -ne '1,/__TOC__/p' < $(TOC_TMPL) | sed '/__TOC__/d' > $@
	./$(TOC_SH) all.ps >> $@
	sed -ne '/__TOC__/,$$p' < $(TOC_TMPL) | sed '/__TOC__/d' >> $@

toc.ps: toc.man
	$(GROFF) -Tps -t -ma4 -msafer $< > $@

manual.ps: toc.man $(man_MANS)
	$(GROFF) $(GROFF_OPTIONS_PS4ALL) $^ > $@

manual.pcl: toc.man $(man_MANS)
	$(GROFF) $(GROFF_OPTIONS_PCL4ALL) $^ > $@

manual: manual.ps manual.pdf

uninstall-local:
	-rmdir $(DESTDIR)$(man1dir)
	-rmdir $(DESTDIR)$(man5dir)
	-rmdir $(DESTDIR)$(man7dir)
	-rmdir $(DESTDIR)$(mandir)

.PHONY: clean
