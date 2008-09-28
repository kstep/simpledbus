CC	= gcc
INSTALL = install

CFLAGS  = -march=native -O2 -Wall -fpic
#CFLAGS  =-O2 -Wall
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

override CFLAGS += $(DEFINES) $(shell pkg-config --cflags dbus-1) $(shell pkg-config --cflags lua)
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

programs = simpledbus.so

.PHONY: all indent clean install uninstall

all: $(programs)

%.o : %.c %.h
	$(CC) $(CFLAGS) -c $<

simpledbus.so: $(objects)
	$(CC) $(CFLAGS) $(LDFLAGS) -lexpat $(shell pkg-config --libs dbus-1) $(objects) -o $@
ifdef STRIP
	strip $@
endif

allinone: CFLAGS+=-DALLINONE
allinone:
	$(CC) $(CFLAGS) $(LDFLAGS) -lexpat $(shell pkg-config --libs dbus-1) simpledbus.c -o simpledbus.so
ifdef STRIP
	strip simpledbus.so
endif

simpledbus:
	@echo "LuaRocks is silly..."

indent:
	indent -kr -i8 *.c *.h

clean:
	rm -f *.so *.o *.c~ *.h~ $(programs)

install: simpledbus
	$(INSTALL) -m755 -D simpledbus.so $(DESTDIR)$(LUA_LIBDIR)/simpledbus.so
	$(INSTALL) -m644 -D SimpleDBus.lua $(DESTDIR)$(LUA_SHAREDIR)/SimpleDBus.lua

uninstall:
	rm -f $(DESTDIR)$(LUA_LIBDIR)/simpledbus.so
	rm -f $(DESTDIR)$(LUA_SHAREDIR)/simpledbus.so

