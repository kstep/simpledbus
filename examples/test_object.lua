#!/usr/bin/env lua

-- import the module
local DBus = require 'simpledbus'

-- initialise and get handle for the session bus
local bus = assert(DBus.SessionBus())

-- set the connection name
if assert(bus:request_name('org.lua.TestScript',
         DBus.NAME_FLAG_DO_NOT_QUEUE))
      ~= DBus.REQUEST_NAME_REPLY_PRIMARY_OWNER then
   print "Couldn't get the name org.lua.TestScript."
   print "Perhaps another instance is running?"
   os.exit(1)
end

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

-- create object to export
local o = DBus.EObject('/org/lua/SimpleDBus/Test')

o:add_method('org.lua.SimpleDBus.Test', 'Test', 'v', 's',
function(...)
   local s = 'You called Test('..stringify{ ... }..')'

   print(s)
   return s
end)

do
   local e = DBus.new_error()
   o:add_method('org.lua.SimpleDBus.Test', 'Error', '', '(ss)',
   function()
      print 'Error() method called'
      return e('Hello World!')
   end)
end

do
   local DBusProxy = assert(bus:auto_proxy(
      'org.freedesktop.DBus', '/org/freedesktop/DBus'))

   o:add_method('org.lua.SimpleDBus.Test', 'ListNames', '', 'as',
   function()
      print 'ListNames() method called..'
      local list = assert(DBusProxy:ListNames())
      print ' ..got list. Returning it.'
      return list
   end)
end

o:add_method('org.lua.SimpleDBus.Test', 'Unregister', '', 's',
function()
   local r, msg = bus:unregister_object_path('/org/lua/SimpleDBus/Test')
   if r then
      return 'OK'
   else
      return msg
   end
end)

o:add_method('org.lua.SimpleDBus.Test', 'Exit', '', '',
function()
   print 'Exit() method called'
   DBus.stop()
end)

-- register our object with DBus
assert(bus:register_object(o))

-- now run the main loop and wait for signals
-- and method calls to arrive
assert(DBus.mainloop(bus))

-- vi: syntax=lua ts=3 sw=3 et:
