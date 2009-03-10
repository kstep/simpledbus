--[[
   SimpleDBus - Simple DBus bindings for Lua
   Copyright (C) 2008 Emil Renner Berthing <esmil@mailme.dk>

   SimpleDBus is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   SimpleDBus is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with SimpleDBus. If not, see <http://www.gnu.org/licenses/>.
--]]

local M = require 'simpledbus.core'

do
   local Proxy = M.Proxy
   function M.Bus:new_proxy(target, object)
      return setmetatable({
         target = target,
         object = object,
         bus = self
      }, Proxy)
   end
end

do
   local Method = M.Method
   function M.new_method(name, interface, signature, result)
      return setmetatable({
         name = name,
         interface = interface,
         signature = signature,
         result = result
      }, Method)
   end

   function M.Proxy:add_method(name, interface, signature, result)
      self[name] = setmetatable({
         name = name,
         interface = interface,
         signature = signature,
         result = result
      }, Method)
   end
end

do
   local Introspect = M.new_method('Introspect',
      M.INTERFACE_INTROSPECTABLE)
   M.Introspect = Introspect

   local Proxy = M.Proxy
   function M.Bus:auto_proxy(target, object)
      local proxy = setmetatable({
         target = target,
         object = object,
         bus = self
      }, Proxy)

      local r, msg = Introspect(proxy)
      if not r then
         return nil, msg
      end

      r, msg = proxy:parse(r)
      if not r then
         return nil, msg
      end

      return proxy
   end
end

do
   local call_method = M.Bus.call_method
   function M.Method.__call(method, proxy, ...)
      return call_method(
         proxy.bus, proxy.target, proxy.object,
         method.interface, method.name,
         method.signature, ...)
   end
end

do
   local call_method = M.Bus.call_method
   local target, object, interface =
      M.SERVICE_DBUS, M.PATH_DBUS, M.INTERFACE_DBUS

   function M.Bus:request_name(name, flags)
      return call_method(self, target, object, interface,
            'RequestName', 'su', name, flags or 0)
   end

   function M.Bus:release_name(name)
      return call_method(self, target, object, interface,
            'ReleaseName', 's', name)
   end

   function M.Bus:add_match(rule)
      return call_method(self, target, object, interface,
            'AddMatch', 's', rule)
   end

   function M.Bus:remove_match(rule)
      return call_method(self, target, object, interface,
            'RemoveMatch', 's', rule)
   end

end

do
   local assert, getmetatable, type = assert, getmetatable, type
   local format = string.format
   local Bus = M.Bus
   local add_match = M.Bus.add_match

   local function register_signal(bus, object, interface, name, f)
      assert(getmetatable(bus) == Bus,
         'bad argument #1 (expected a DBus connection)')
      assert(type(object) == 'string',
         'bad argument #2 (string expected, got '..type(object))
      assert(type(interface) == 'string',
         'bad argument #3 (string expected, got '..type(interface))
      assert(type(name) == 'string',
         'bad argument #4 (string expected, got '..type(name))
      assert(type(f) == 'function',
         'bad argument #5 (function expected, got '..type(f))

      local t = bus:get_signal_table()

      -- this magic string representation of an inconming
      -- signal must match the one in the C code
      local s = format('%s\n%s\n%s', object, interface, name)

      if t[s] == nil then
         local r, msg = add_match(bus,
               format("type='signal',path='%s',interface='%s',member='%s'",
                     object, interface, name))
         if msg then return nil, msg end
      end

      t[s] = f

      return true
   end
   Bus.register_signal = register_signal

   function M.Bus:register_auto_signal(signal, f)
      return register_signal(self,
         signal.object,
         signal.interface,
         signal.name,
         f)
   end
end

do
   local assert, getmetatable, type = assert, getmetatable, type
   local format = string.format
   local Bus = M.Bus
   local remove_match = M.Bus.remove_match

   local function unregister_signal(bus, object, interface, name)
      assert(getmetatable(bus) == Bus,
         'bad argument #1 (expected a DBus connection)')
      assert(type(object) == 'string',
         'bad argument #2 (string expected, got '..type(object))
      assert(type(interface) == 'string',
         'bad argument #3 (string expected, got '..type(interface))
      assert(type(name) == 'string',
         'bad argument #4 (string expected, got '..type(name))

      local t = bus:get_signal_table()

      -- this magic string representation of an inconming
      -- signal must match the one in the C code
      local s = format('%s\n%s\n%s', object, interface, name)

      assert(t[s] ~= nil, 'signal not set')

      local r, msg = remove_match(bus,
            format("type='signal',path='%s',interface='%s',member='%s'",
                  object, interface, name))

      if msg then return nil, msg end

      t[s] = nil

      return true
   end
   Bus.unregister_signal = unregister_signal

   function M.Bus:unregister_auto_signal(signal)
      return unregister_signal(self,
         signal.object,
         signal.interface,
         signal.name)
   end
end

return M

-- vi: syntax=lua ts=3 sw=3 et:
