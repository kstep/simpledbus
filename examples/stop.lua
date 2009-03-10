#!/usr/bin/env lua

-- import the module
local DBus = require 'simpledbus'

-- initialise and get a handle for the session bus
local bus = assert(DBus.SessionBus())

-- send the stop signal
assert(bus:send_signal(
      '/org/lua/SimpleDBus/Test',
      'org.lua.SimpleDBus.TestSignal',
      'Signal', 's', 'stop'))

-- vi: syntax=lua ts=3 sw=3 et:
