# Makefile to install the files 
DESTDIR=
MANDIR=/usr/share/man
LIBDIR=/usr/lib
DOCDIR=/usr/share/doc


install:
	# Library
	mkdir -p $(DESTDIR)$(LIBDIR)/process-getopt/
	install -m 755 src/process-getopt $(DESTDIR)$(LIBDIR)/process-getopt/
	# Man page
	gzip --best -c doc/process-getopt.1 > process-getopt.1.gz
	mkdir -p $(DESTDIR)$(MANDIR)/man1/
	install -m 644 process-getopt.1.gz $(DESTDIR)$(MANDIR)/man1/
	rm process-getopt.1.gz
	# Readme and examples
	mkdir -p $(DESTDIR)$(DOCDIR)/process-getopt/
	install -m 644 README.md $(DESTDIR)$(DOCDIR)/process-getopt/
	mkdir -p $(DESTDIR)$(DOCDIR)/process-getopt/examples/
	install -m 755 doc/examples/boilerplate $(DESTDIR)$(DOCDIR)/process-getopt/examples/
	install -m 755 doc/examples/boilerplate-awk $(DESTDIR)$(DOCDIR)/process-getopt/examples/
	install -m 644 doc/examples/example-script $(DESTDIR)$(DOCDIR)/process-getopt/examples/
	install -m 755 doc/examples/testecho $(DESTDIR)$(DOCDIR)/process-getopt/examples/
	install -m 644 doc/examples/tiny $(DESTDIR)$(DOCDIR)/process-getopt/examples/
