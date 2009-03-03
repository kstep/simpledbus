package = 'simpledbus'
version = 'dev-1'
source = {
   url = ''
}

description = {
   summary = 'Simple DBus bindings for Lua',
   --detailed = [[]],
   homepage = 'http://github.com/esmil/simpledbus',
   license = 'GPL'
}

external_dependencies = {
   DBUS = {
        header  = 'dbus-1.0/dbus/dbus.h',
        library = 'libdbus-1.so'
     },
   EXPAT = {
	header  = 'expat.h',
        library = 'libexpat.so'
     }
}

local build_linux = {
   type = 'make',
   build_target = 'allinone',
   build_variables = {
--      CFLAGS  = '$(CFLAGS)',
      LIBFLAG = '$(LIBFLAG)'
   },
   install_pass = false,
   install = {
      lua = { simpledbus = 'simpledbus.lua' },
      lib = { ['simpledbus.core'] = 'core.so' }
   }
}

local build_separate = {
   sources = {'add.c', 'push.c', 'parse.c', 'simpledbus.c'},
   libraries = { 'expat', 'dbus-1' },
   incdirs = {'/usr/include/dbus-1.0', '/usr/lib/dbus-1.0/include'}
}

local build_allinone = {
   sources = { 'simpledbus.c' },
   defines = { 'ALLINONE' },
   libraries = { 'expat', 'dbus-1' },
   incdirs = {'/usr/include/dbus-1.0', '/usr/lib/dbus-1.0/include'}
}

build = {
   platforms = { linux = build_linux },
   type = 'builtin',
   modules = {
      simpledbus = 'simpledbus.lua',
      ['simpledbus.core'] = build_separate,
   }
}

-- vi: syntax=lua ts=3 sw=3 et:
