#!/usr/bin/env lua

-- import the module
local DBus = require 'simpledbus'

-- initialise and get handles for the busses
local sessionbus = assert(DBus.SessionBus())
local systembus = assert(DBus.SystemBus())

-- connect to the client connected at org.freedesktop.DBus
-- and get a proxy for the object /org/freedesktop/DBus
sessionDBus = assert(sessionbus:auto_proxy(
   'org.freedesktop.DBus', '/org/freedesktop/DBus'))
systemDBus = assert(systembus:auto_proxy(
   'org.freedesktop.DBus', '/org/freedesktop/DBus'))

-- print connections to busses
print 'Connections to the session bus:'
for i, s in ipairs(assert(sessionDBus:ListNames())) do
   print(('%4i: %s'):format(i, s))
end

print 'Connections to the system bus:'
for i, s in ipairs(assert(systemDBus:ListNames())) do
   print(('%4i: %s'):format(i, s))
end

-- register interest in the signals from the
-- object /org/lua/SimpleDBus/Test implementing
-- the interface org.lua.SimpleDBus.TestSignal
-- called Signal

local function signal_handler(prefix)
   return function(...)
      local t = { ... }
      for i = 1, #t do
         t[i] = tostring(t[i])
      end

      print(prefix..': '..table.concat(t, ', '))

      if t[1] == 'stop' then
         DBus.stop()
      end
   end
end

sessionbus:register_signal(
   '/org/lua/SimpleDBus/Test',
   'org.lua.SimpleDBus.TestSignal',
   'Signal',
   signal_handler('Session'))

systembus:register_signal(
   '/org/lua/SimpleDBus/Test',
   'org.lua.SimpleDBus.TestSignal',
   'Signal',
   signal_handler('System'))

-- now run the main loop and wait for signals to arrive
assert(DBus.mainloop(sessionbus, systembus))

-- vi: syntax=lua ts=3 sw=3 et:
