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
      'org.freedesktop.DBus.Introspectable')
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
   local register_signal = M.Bus.register_signal
   function M.Bus:register_auto_signal(signal, f)
      return register_signal(self,
         signal.object,
         signal.interface,
         signal.name,
         f)
   end
end

do
   local unregister_signal = M.Bus.unregister_signal
   function M.Bus:unregister_auto_signal(signal)
      return unregister_signal(self,
         signal.object,
         signal.interface,
         signal.name)
   end
end

do
   local call_method = M.Bus.call_method
   function M.Bus:request_name(name, flags)
      return call_method(self,
            'org.freedesktop.DBus',
            '/org/freedesktop/DBus',
            'org.freedesktop.DBus',
            'RequestName',
            'su', name, flags or 0)
   end
end

return M

-- vi: syntax=lua ts=3 sw=3 et:
