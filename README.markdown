SimpleDBus
==========


About
-----

SimpleDBus is a [Lua][1] library which enables you to write simple scripts to
act on signals, send signals, call methods on remote objects and export your own
objects to [DBus][2].

SimpleDBus implements a simple `poll()`-based main loop so scripts can
act asynchronously over several busses at the same time.

[1]: http://www.lua.org
[2]: http://dbus.freedesktop.org

Installation
------------

Get the sources and do

    make
    make PREFIX=/usr install

This will install the files `core.so` in `/usr/lib/lua/5.1/simpledbus` and
`simpledbus.lua` in `/usr/share/lua/5.1`. Have a look at the makefiles if this
isn't right for your system.

Instead of `make` you can use `make allinone` to compile all the code in one go.
This produces a slightly smaller library.

Alternatively you can use [luarocks][3] and just type `luarocks make` in the
source directory.

Make sure you have installed the dbus and expat libraries and their development
packages on systems where they are separate. The build process also requires
pkgconfig to set up paths for the dbus headers.

[3]: http://www.luarocks.org


Usage
-----

Here is a sample script:

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

Use the command `dbus-send --session --type=signal /org/lua/SimpleDBus/Test org.lua.SimpleDBus.TestSignal.Signal string:stop` or run `stop.lua` in the examples directory to stop this script in a nice way.

For more examples look in the examples directory in the source tree.


License
-------

SimpleDBus is free software. It is distributed under the terms of the
[GNU General Public License][4]

[4]: http://www.fsf.org/licensing/licenses/gpl.html


Contact
-------

Please send bug reports, patches, feature requests, praise and general gossip
to me, Emil Renner Berthing <esmil@mailme.dk>.
