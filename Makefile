# Sample Makefile for a GKrellM plugin

VERSION = "0.2.0"
REL_DATE = "2005-07-04"
#GTK_INCLUDE = `pkg-config gtk+-2.0 --cflags`
#GTK_LIB = `pkg-config gtk+-2.0 --libs`
#
#
#FLAGS = -O2 -Wall -fPIC $(GTK_INCLUDE) -g
##LIBS = $(GTK_LIB) $(IMLIB_LIB) -lefence
#LIBS = $(GTK_LIB) $(IMLIB_LIB)

CFLAGS += $(shell pkg-config --cflags gtk+-2.0) -DVERSION='$(VERSION)' -DREL_DATE='$(REL_DATE)' $(CFLAGS_DEBUG) 
LDFLAGS += $(shell pkg-config --libs gtk+-2.0) -shared -W1

OBJS = gkrellmpager.o

PLUGIN_DIR ?= /usr/local/lib/gkrellm2/plugins

gkrellmpager.so: $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -f *.o core *.so* *.bak *~

gkrellmpager.o: gkrellmpager.c

install: gkrellmpager.so
	install -s -m 755 $^ $(PLUGIN_DIR)

test: gkrellmpager.so
	gkrellm -p $^

dist:
	mkdir gkrellmpager-$(VERSION)
	ln *.c Makefile README COPYING ChangeLog LICENSE gkrellmpager-$(VERSION)
	tar czf gkrellmpager-$(VERSION).tar.gz gkrellmpager-$(VERSION)
	rm -rf gkrellmpager-$(VERSION)
