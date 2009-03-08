#!/usr/bin/env lua

-- import the module
local DBus = require 'simpledbus'

-- initialise and get handle for the session bus
local bus = assert(DBus.SessionBus())

local function stringify(t)
   for i = 1, #t do
      local ti = t[i]
      if type(ti) == 'string' then
         t[i] = ('%q'):format(ti)
      else
         t[i] = tostring(ti)
      end
   end

   return table.concat(t, ', ')
end

-- listen for Signal from the object /org/lua/SimpleDBus/Test,
-- interface org.lua.SimpleDBus.TestSignal, as a backdoor to
-- ending this script in a nice way. Use:
-- dbus-send --session --type=signal /org/lua/SimpleDBus/Test \
--   org.lua.SimpleDBus.TestSignal.Signal string:'stop'

assert(bus:register_signal(
   '/org/lua/SimpleDBus/Test',
   'org.lua.SimpleDBus.TestSignal',
   'Signal',
   function(arg1)
      if arg1 == 'stop' then
         print 'Got signal to exit'
         DBus.stop()
      end
   end))

-- create table for methods
local methods = {}

-- register the object path
assert(bus:register_object_path('/org/lua/SimpleDBus/Test', methods))

-- create methods
methods['org.lua.SimpleDBus.Test.Test'] = {'v', 's', function(...)
   local s = 'You called Test('..stringify{ ... }..')'

   print(s)
   return s
end}

methods['org.lua.SimpleDBus.Test.Unregister'] = {'', 's', function()
   local r, msg = bus:unregister_object_path('/org/lua/SimpleDBus/Test')
   if r then
      return 'OK'
   else
      return msg
   end
end}

methods['org.lua.SimpleDBus.Test.Exit'] = {'', '', function()
   print 'Exit() method called'
   DBus.stop()
end}

methods['org.freedesktop.DBus.Introspectable.Introspect'] = {'', 's', function()
   return [[
<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="org.freedesktop.DBus.Introspectable">
    <method name="Introspect">
      <arg name="data" direction="out" type="s"/>
    </method>
  </interface>
  <interface name="org.lua.SimpleDBus.Test">
    <method name="Test">
      <arg direction="in" type="v"/>
      <arg direction="out" type="s"/>
    </method>
    <method name="Unregister">
      <arg direction="out" type="s"/>
    </method>
    <method name="Exit"></method>
  </interface>
</node>
]]
end}

-- now run the main loop and wait for signals
-- and method calls to arrive
assert(DBus.mainloop(bus))

-- vi: syntax=lua ts=3 sw=3 et:
