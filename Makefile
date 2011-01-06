CC	= gcc
INSTALL = install

CFLAGS  = -march=native -O2 -Wall -fpic -pedantic
#CFLAGS  =-O2 -Wall -fPIC -pedantic
LIBFLAG = -shared

PREFIX = /usr/local

LUA_DIR = $(PREFIX)
LUA_LIBDIR=$(LUA_DIR)/lib/lua/5.1
LUA_SHAREDIR=$(LUA_DIR)/share/lua/5.1

#EXPAT_DIR = $(PREFIX)
ifdef EXPAT_DIR
	EXPAT_INCDIR = $(EXPAT_DIR)/include
	EXPAT_LIBDIR = $(EXPAT_DIR)/lib
endif

override CFLAGS += $(DEFINES) $(shell pkg-config --cflags dbus-1) $(shell pkg-config --cflags lua5.1)
override LDFLAGS += $(LIBFLAG) 

ifdef EXPAT_INCDIR
override CFLAGS += -I$(EXPAT_INCDIR)
endif
ifdef EXPAT_LIBDIR
override LDFLAGS += -L$(EXPAT_LIBDIR)
endif

sources = add.c push.c parse.c simpledbus.c
headers = $(sources:.c=.h)
objects = $(sources:.c=.o)

programs = core.so

.PHONY: all strip indent clean install uninstall

all: $(programs)

%.o : %.c %.h
	$(CC) $(CFLAGS) -c $<

core.so: $(objects)
	$(CC) $(CFLAGS) $(LDFLAGS) -lexpat $(shell pkg-config --libs dbus-1) $(objects) -o $@

allinone: CFLAGS+=-DALLINONE
allinone:
	$(CC) $(CFLAGS) $(LDFLAGS) -lexpat $(shell pkg-config --libs dbus-1) simpledbus.c -o core.so

simpledbus:
	@echo "LuaRocks is silly..."

strip:
	@for i in $(programs); do echo strip $$i; strip "$$i"; done

indent:
	indent -kr -i8 *.c *.h

clean:
	rm -f *.so *.o *.c~ *.h~ $(programs)

install: core.so
	$(INSTALL) -m755 -D core.so $(DESTDIR)$(LUA_LIBDIR)/simpledbus/core.so
	$(INSTALL) -m644 -D simpledbus.lua $(DESTDIR)$(LUA_SHAREDIR)/simpledbus.lua

uninstall:
	rm -rf $(DESTDIR)$(LUA_LIBDIR)/simpledbus
	rm -f $(DESTDIR)$(LUA_SHAREDIR)/simpledbus.lua

