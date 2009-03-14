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

function M.new_error(name)
   if name == nil or name == '' then
      name = 'org.freedesktop.DBus.Error.Failed'
   end
   return function(message)
      if message then
         return nil, name, message
      else
         return nil, name
      end
   end
end

do
   local Method = M.Method
   local function new_method(name, interface, signature, result)
      return setmetatable({
         name = name,
         interface = interface,
         signature = signature,
         result = result
      }, Method)
   end
   M.new_method = new_method

   function M.Proxy:add_method(name, interface, signature, result)
      self[name] = new_method(name, interface, signature, result)
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
   local Proxy = M.Proxy
   local function new_proxy(bus, target, object)
      return setmetatable({
         target = target,
         object = object,
         bus = bus
      }, Proxy)
   end
   M.Bus.new_proxy = new_proxy

   local Introspect = M.new_method('Introspect',
      M.INTERFACE_INTROSPECTABLE)
   M.Introspect = Introspect

   function M.Bus:auto_proxy(target, object)
      local proxy = new_proxy(self, target, object)

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

   function Bus:register_auto_signal(signal, f)
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

   function Bus:unregister_auto_signal(signal)
      return unregister_signal(self,
         signal.object,
         signal.interface,
         signal.name)
   end
end

do
   local EObject = {}
   EObject.__index = EObject

   M.EObject = EObject

   function M.Bus:register_object(o)
      assert(getmetatable(o) == EObject,
         'bad argument #2 (expected an EObject)')

      return self:register_object_path(o.path, o.lookup)
   end

   local sub, concat = string.sub, table.concat

   local function value_end(i, sig)
      local char = sub(sig, i, i)
      if char == 'a' then
         return value_end(i+1, sig)
      elseif char == '(' then
         i = i+1
         local n = #sig -- don't loop forever on broken signature
         repeat
            i = value_end(i, sig) + 1
         until i > n or sub(sig, i, i) == ')'
      elseif char == '{' then
         i = value_end(i+1, sig)
         i = value_end(i+1, sig) + 1
      end

      return i
   end

   local function method_xml(name, in_sig, out_sig)
      local t, l = { '<method name="', name, '">' }, 3

      local function write(s)
         l = l+1
         t[l] = s
         return write
      end

      local i, n = 1, #in_sig
      while i <= n do
         local j = value_end(i, in_sig)
         write '<arg direction="in" type = "' (sub(in_sig, i, j)) '" />';
         i = j+1
      end
      i, n = 1, #out_sig
      while i <= n do
         local j = value_end(i, out_sig)
         write '<arg direction="out" type = "' (sub(out_sig, i, j)) '" />';
         i = j+1
      end
      write '</method>';

      return concat(t)
   end

   function EObject:add_method(interface, name, in_sig, out_sig, f)
      if not in_sig  then in_sig  = '' end
      if not out_sig then out_sig = '' end

      self.lookup[interface..'.'..name] = {in_sig, out_sig, f}
      self.xml = nil

      local xml = method_xml(name, in_sig, out_sig)
      local interfaces = self.interfaces
      local methods = interfaces[interface]
      if methods then
         methods[name] = xml
      else
         interfaces[interface] = { [name] = xml }
      end
   end

   local function generate_xml(interfaces)
      local t, l = {[[
<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>]]}, 1

      local function write(s)
         l = l+1
         t[l] = s
      end

      for interface, methods in pairs(interfaces) do
         write '<interface name="'
         write(interface)
         write '">';
         for _, xml in pairs(methods) do
            write(xml);
         end
         write '</interface>'
      end

      write '</node>'

      return concat(t)
   end

   setmetatable(EObject, { __call = function(_, path)
      assert(path and path ~= '' and sub(path, 1, 1) == '/',
         'illegal object path')
      local t
      t = {
         path = path,
         lookup = {
            ['org.freedesktop.DBus.Introspectable.Introspect'] =
               {'', 's', function()
                  local xml = t.xml
                  if xml == nil then
                     xml = generate_xml(t.interfaces)
                     t.xml = xml
                  end

                  return xml
               end}
         },
         interfaces = {
            ['org.freedesktop.DBus.Introspectable'] = {
               ['Introspect'] = [[
<method name="Introspect"><arg name="data" direction="out" type="s" /></method>]]
            }
         }
      }
      return setmetatable(t, EObject)
   end})
end

return M

-- vi: syntax=lua ts=3 sw=3 et:
