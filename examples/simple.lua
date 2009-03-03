#!/usr/bin/env lua

-- import the module
SimpleDBus = require 'simpledbus'

-- initialise and get a handle for the session bus
bus = assert(SimpleDBus.SessionBus())

-- connect to the client connected at org.freedesktop.DBus
-- and get a proxy for the object /org/freedesktop/DBus
DBus = assert(bus:auto_proxy('org.freedesktop.DBus', '/org/freedesktop/DBus'))

-- call a method and print the results nicely
print 'Connections to the session bus:'
for i, s in ipairs(assert(DBus:ListNames())) do
   print(('%4i: %s'):format(i, s))
end

-- register interest in the signal from the
-- object /org/lua/SimpleDBus/Test implementing
-- the interface org.lua.SimpleDBus.TestSignal
-- called Signal
bus:register_signal(
   '/org/lua/SimpleDBus/Test',
   'org.lua.SimpleDBus.TestSignal',
   'Signal',
   function(arg1)
      print('Signal: ' .. tostring(arg1))

      if arg1 == 'stop' then
         SimpleDBus.stop()
      end
   end)

-- now run the main loop and wait for signals to arrive
assert(SimpleDBus.mainloop(bus))

-- vi: syntax=lua ts=3 sw=3 et:
