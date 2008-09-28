package = "SimpleDBus"
version = "dev-1"
source = {
   url = ""
}
description = {
   summary = "Simple DBus bindings for Lua",
   --detailed = [[]],
   homepage = "http://github.com/esmil/simpledbus",
   license = "GPL"
}
external_dependencies = {
   DBUS = {
        header  = "dbus-1.0/dbus/dbus.h",
        library = "libdbus-1.so"
     },
   EXPAT = {
	header  = "expat.h",
        library = "libexpat.so"
     }
}
build = {
   type = "make",
   build_target = "allinone",
   build_variables = {
--      CFLAGS  = "$(CFLAGS)",
      LIBFLAG = "$(LIBFLAG)"
   },
--[[
   install_variables = {
      LUA_LIBDIR = "$(LIBDIR)",
      LUA_SHAREDIR = "$(LUADIR)"
   }
--]]
--![[
   install_pass = false,
   install = {
      lib = { "simpledbus.so" },
      lua = { "SimpleDBus.lua" }
   }
--]]
}
