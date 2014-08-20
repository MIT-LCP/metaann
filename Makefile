## Programs and flags

CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Os

PKG_CONFIG = $(CROSS_COMPILE)pkg-config

GTK_CFLAGS  = `$(PKG_CONFIG) --cflags gtk+-2.0`
GTK_LIBS    = `$(PKG_CONFIG) --libs gtk+-2.0`
CURL_CFLAGS = `$(PKG_CONFIG) --cflags libcurl`
CURL_LIBS   = `$(PKG_CONFIG) --libs libcurl`

WFDB_CONFIG = $(CROSS_COMPILE)wfdb-config

WFDB_CFLAGS = `$(WFDB_CONFIG) --cflags`
WFDB_LIBS   = `$(WFDB_CONFIG) --libs`

## Compilation options

cflags1 = $(CFLAGS) $(CPPFLAGS) -DMETAANN_VERSION=\"$(PACKAGE_VERSION)\" $(DEFS) \
	  $(GTK_CFLAGS) $(CURL_CFLAGS) $(WFDB_CFLAGS)

cflags2 = $(cflags1) -W -Wall -Wformat=2 -Wwrite-strings -Wmissing-prototypes

libs = $(GTK_LIBS) $(WFDB_LIBS) $(CURL_LIBS) $(LIBS)

objs = metaann.o conf.o url.o annot.o grid.o init.o modepan.o sig.o wave_widget.o wave_window.o

## Package information

PACKAGE_TARNAME = metaann
PACKAGE_VERSION = 0.1
SRCPACKAGE = $(PACKAGE_TARNAME)-$(PACKAGE_VERSION)
W32PACKAGE = $(PACKAGE_TARNAME)-w32

srcfiles = Makefile *.c *.h
datafiles = *.ui metaann.conf default.conf

## W32 binary package information

DLLPREFIX = `$(PKG_CONFIG) --variable=exec_prefix gtk+-2.0`
sysdlls = msvcrt.dll kernel32.dll gdi32.dll imm32.dll ole32.dll \
          shell32.dll user32.dll advapi32.dll ws2_32.dll comctl32.dll \
          comdlg32.dll winspool.drv msimg32.dll gdiplus.dll dnsapi.dll \
	  shlwapi.dll usp10.dll crypt32.dll

#w32extrafiles = etc/fonts \
#	         lib/gtk-2.0/2.10.0/engines \
#	         lib/gtk-2.0/modules

##

all: metaann$(EXEEXT)

all-w32:
	$(MAKE) all CROSS_COMPILE=i686-w64-mingw32- EXEEXT=.exe LDFLAGS=-mwindows

metaann$(EXEEXT): $(objs)
	$(CC) $(LDFLAGS) $(objs) -o metaann$(EXEEXT) $(libs)

$(objs): *.h

metaann.o: metaann.c
	$(CC) $(cflags2) -c metaann.c
conf.o: conf.c
	$(CC) $(cflags2) -c conf.c
url.o: url.c
	$(CC) $(cflags2) -c url.c
wave_window.o: wave_window.c
	$(CC) $(cflags2) -c wave_window.c

annot.o: annot.c
	$(CC) $(cflags1) -c annot.c
grid.o: grid.c
	$(CC) $(cflags1) -c grid.c
init.o: init.c
	$(CC) $(cflags1) -c init.c
modepan.o: modepan.c
	$(CC) $(cflags1) -c modepan.c
sig.o: sig.c
	$(CC) $(cflags1) -c sig.c
wave_widget.o: wave_widget.c
	$(CC) $(cflags1) -c wave_widget.c

dist:
	rm -rf $(SRCPACKAGE)
	mkdir $(SRCPACKAGE)
	cp -p $(srcfiles) $(SRCPACKAGE)
	cp -p $(datafiles) $(SRCPACKAGE)
	tar cfvj $(SRCPACKAGE).tar.bz2 $(SRCPACKAGE)

dist-w32:
	$(MAKE) dist-binary CROSS_COMPILE=i686-w64-mingw32- EXEEXT=.exe LDFLAGS=-mwindows

dist-binary: all
	rm -rf $(W32PACKAGE)
	mkdir $(W32PACKAGE)
	set -e; export LC_ALL=C; \
	cd $(W32PACKAGE); \
	ln -s ../metaann.exe; \
	for f in $(w32extrafiles); do \
	  if ! [ -e $(DLLPREFIX)/$$f ]; then \
	    echo; \
	    echo " *** File not found: $$f ***"; \
	    echo " *** DLLPREFIX = $(DLLPREFIX) ***"; \
	    echo; \
	    exit 1; \
	  fi; \
	  mkdir -p `dirname $$f`; \
	  ln -s $(DLLPREFIX)/$$f $$f; \
	done; \
	newfiles=`find -L . -name '*.exe' -o -name '*.dll'`; \
	while [ -n "$$newfiles" ]; do \
	  oldfiles=$$newfiles; \
	  newfiles=''; \
	  for f in $$oldfiles; do \
	    dlls=`objdump -p $$f | sed -n '/^\tDLL Name: /{s/.*: //;p}'`; \
	    for dll in $$dlls; do \
	      if ! [ -f $$dll ] && ! echo $(sysdlls) | grep -qwi $$dll; then \
	        if [ -f $(DLLPREFIX)/bin/$$dll ]; then \
	          ln -s $(DLLPREFIX)/bin/$$dll; \
	          newfiles="$$newfiles $$dll"; \
	        else \
	          echo; \
	          echo " *** Library not found: $$dll ***"; \
	          echo " *** DLLPREFIX = $(DLLPREFIX) ***"; \
	          echo; \
	          exit 1; \
	        fi; \
	      fi; \
	    done; \
	  done; \
	done
	cp -p $(datafiles) $(W32PACKAGE)
	zip -9 -r $(PACKAGE_TARNAME)-$(PACKAGE_VERSION)-w32.zip $(W32PACKAGE)

clean:
	rm -f metaann metaann.exe
	rm -f *.o

.PHONY: all all-w32 clean dist dist-w32 dist-binary
