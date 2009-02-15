/*
 * SimpleDBus - Simple DBus bindings for Lua
 * Copyright (C) 2008 Emil Renner Berthing <esmil@mailme.dk>
 *
 * SimpleDBus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleDBus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleDBus. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LUA_LIB
#include <lua.h>
#include <dbus/dbus.h>

#ifdef ALLINONE
#include <lauxlib.h>
#include <expat.h>

#define EXPORT static
#else
#include "add.h"
#include "push.h"
#include "parse.h"

#define EXPORT
#endif

#define BUS_CONNECTION        "connection"
#define BUS_SIGNAL_HANDLERS   "signal_handlers"
#define BUS_RUNNING_THREADS   "running_threads"

static DBusError err;
static lua_State *mainThread = NULL;
static unsigned int is_running = 0;

EXPORT int error(lua_State *L, const char *fmt, ...)
{
	va_list ap;

	lua_pushnil(L);
	va_start(ap, fmt);
	lua_pushvfstring(L, fmt, ap);
	va_end(ap);

	return 2;
}

#ifdef ALLINONE
#include "add.c"
#include "push.c"
#include "parse.c"
#endif

static void method_return_handler(DBusPendingCall *pending, lua_State *T)
{
	DBusMessage *msg = dbus_pending_call_steal_reply(pending);
	int nargs;

	dbus_pending_call_unref(pending);

	if (msg == NULL)
		nargs =	error(T, "Reply null");
	else {
		switch (dbus_message_get_type(msg)) {
		case DBUS_MESSAGE_TYPE_METHOD_RETURN:
			nargs = push_arguments(T, msg);
			break;
		case DBUS_MESSAGE_TYPE_ERROR:
			lua_pushnil(T);
			dbus_set_error_from_message(&err, msg);
			lua_pushstring(T, err.message);
			dbus_error_free(&err);
			nargs = 2;
			break;
		default:
			nargs = error(T, "Unknown reply");
		}
	}

	switch (lua_resume(T, nargs)) {
	case 0: /* thread finished */
		/* remove it from the running threads table */
		lua_pushthread(T);
		lua_xmove(T, mainThread, 1);
		lua_pushnil(mainThread);
		lua_settable(mainThread, -3); /* thread table */
		break;
	case LUA_YIELD: /* thread yielded again */
		break;
	default:
		/* move error message to main thread and error */
		lua_xmove(T, mainThread, 1);
		lua_error(mainThread);
	}
}

/*
 * argument 1: bus
 * argument 2: target
 * argument 3: object
 * argument 4: method
 * argument 5: interface
 * argument 6: signature
 * ...
 */
static int bus_call_method(lua_State *L)
{
	int argc = lua_gettop(L);
	DBusMessage *msg;
	const char *interface;
	DBusConnection *conn;
	DBusMessage *ret;

	/*
	printf("Calling:\n  %s\n  %s\n  %s\n  %s\n  %s\n",
				lua_tostring(L, 2),
				lua_tostring(L, 3),
				lua_tostring(L, 4),
				lua_tostring(L, 5),
				lua_tostring(L, 6));
	fflush(stdout);
	*/

	/* create a new method call and check for errors */
	interface = lua_tostring(L, 5);
	if (interface && *interface == '\0')
		interface = NULL;

	msg = dbus_message_new_method_call(
				lua_tostring(L, 2),
				lua_tostring(L, 3),
				interface,
				lua_tostring(L, 4));
	if (msg == NULL)
		return error(L, "Couldn't create message");

	/* get the signature and add arguments */
	if (lua_isstring(L, 6))
		add_arguments(L, 7, argc, lua_tostring(L, 6), msg);

	/* get the connection */
	lua_getfield(L, 1, BUS_CONNECTION);
	conn = lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (!conn)
		return error(L, "Connection isn't set");

	if (!lua_pushthread(L)) { /* L can be yielded */
		DBusPendingCall *pending;

		lua_pop(L, 1);

		if (!dbus_connection_send_with_reply(conn, msg, &pending, -1))
			return error(L, "Out of memory");

		if (!dbus_pending_call_set_notify(pending,
					(DBusPendingCallNotifyFunction)
					method_return_handler, L, NULL))
			return error(L, "Out of memory");

		return lua_yield(L, 0);
	}
	lua_pop(L, 1);

	/* L is the main thread, so we call the method synchronously */
	ret = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);

	/* free message */
	dbus_message_unref(msg);

	/* check reply */
	if (dbus_error_is_set(&err)) {
		lua_pushnil(L);
		lua_pushstring(L, err.message);
		dbus_error_free(&err);
		return 2;
	}
	if (ret == NULL)
		return error(L, "Reply null");
	switch (dbus_message_get_type(ret)) {
	case DBUS_MESSAGE_TYPE_METHOD_RETURN:
		/* read the parameters */
		return push_arguments(L, ret);
	case DBUS_MESSAGE_TYPE_ERROR:
		lua_pushnil(L);
		dbus_set_error_from_message(&err, ret);
		lua_pushstring(L, err.message);
		dbus_error_free(&err);
		return 2;
	}

	return error(L, "Unknown reply");
}

#define push_signal_string(L, object, interface, signal) \
	lua_pushfstring(L, "%s\n%s\n%s", object, interface, signal)

static DBusHandlerResult signal_handler(DBusConnection *conn,
		DBusMessage *msg, void *data)
{
	lua_State *T;

	if (!msg || dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	push_signal_string(mainThread,
			dbus_message_get_path(msg),
			dbus_message_get_interface(msg),
			dbus_message_get_member(msg));
	/*
	printf("received \"%s\"\n", lua_tostring(mainThread, -1));
	fflush(stdout);
	*/

	lua_gettable(mainThread, -3); /* signal handler table */

	if (lua_type(mainThread, -1) != LUA_TFUNCTION) {
		lua_pop(mainThread, 1);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	T = lua_newthread(mainThread);
	lua_insert(mainThread, -2);
	lua_xmove(mainThread, T, 1);

	dbus_message_ref(msg);
	switch (lua_resume(T, push_arguments(T, msg))) {
	case 0: /* thread finished */
		/* just forget about it */
		lua_pop(mainThread, 1);
		break;
	case LUA_YIELD:	/* thread yielded */
		/* save it in the running threads table */
		lua_pushboolean(mainThread, 1);
		lua_settable(mainThread, -3); /* thread table */
		break;
	default:
		/* move error message to main thread and error */
		lua_xmove(T, mainThread, 1);
		lua_error(mainThread);
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}

static void add_match(DBusConnection *conn,
		const char *object,
		const char *interface,
		const char *signal)
{
	char *rule = alloca(45 + strlen(object)
			+ strlen(interface) + strlen(signal));

	/* construct rule to catch the signal messages */
	sprintf(rule, "type='signal',path='%s',interface='%s',member='%s'",
			object, interface, signal);

	/*
	printf("added rule=\"%s\"\n", rule); fflush(stdout);
	*/

	/* add the rule */
	dbus_bus_add_match(conn, rule, &err);
}

/*
 * DBus.register_signal()
 *
 * argument 1: connection
 * argument 2: object
 * argument 3: interface
 * argument 4: signal
 * argument 5: function
 */
static int bus_register_signal(lua_State *L)
{
	DBusConnection *conn;

	/* get the connection */
	lua_getfield(L, 1, BUS_CONNECTION);
	conn = lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (lua_gettop(L) < 5)
		return error(L, "Too few arguments");

	/* get the signal handler table */
	lua_getfield(L, 1, BUS_SIGNAL_HANDLERS);

	{
		const char *object = lua_tostring(L, 2);
		const char *interface = lua_tostring(L, 3);
		const char *signal = lua_tostring(L, 4);

		/* push the signal string */
		push_signal_string(L, object, interface, signal);

		/* check if signal is already set */
		lua_pushvalue(L, -1);
		lua_gettable(L, -3);

		/* if we didn't already set this signal
		 * tell dbus we're interested */
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);

			/* add the rule and check for errors */
			add_match(conn, object, interface, signal);
			if (dbus_error_is_set(&err)) {
				lua_pop(L, 2);
				lua_pushnil(L);
				lua_pushstring(L, err.message);
				dbus_error_free(&err);
				return 2;
			}
		} else
			lua_pop(L, 1);
	}

	/* add the function to the signal handler table */
	lua_pushvalue(L, 5);
	lua_settable(L, -3); /* signal handler table */
	lua_pop(L, 1);

	lua_pushboolean(L, 1);
	return 1;
}

/*
 * DBus.run()
 *
 * argument 1: connection
 */
static int bus_run(lua_State *L)
{
	DBusConnection *conn;

	if (is_running)
		return error(L, "Another main loop is already running");

	/* get the connection */
	lua_getfield(L, 1, BUS_CONNECTION);
	conn = lua_touserdata(L, -1);
	lua_pop(L, 1);

	/* push the signal handler and running threads tables on the stack */
	lua_getfield(L, 1, BUS_SIGNAL_HANDLERS);
	lua_getfield(L, 1, BUS_RUNNING_THREADS);

	is_running = 1;
	mainThread = L;

	while (is_running && dbus_connection_read_write_dispatch(conn, -1));

	/* remove the signal handler and running threads tables */
	lua_pop(L, 2);

	mainThread = NULL;

	if (is_running) {
		is_running = 0;
		return error(L, "Main loop ended unexpectedly");
	}

	lua_pushboolean(L, 1);
	return 1;
}

/*
 * DBus.stop()
 *
 * argument 1: connection
 */
static int bus_stop(lua_State *L)
{
	is_running = 0;

	return 0;
}

/*
 * DBus.__gc()
 *
 * argument 1: connection
 */
static int bus_gc(lua_State *L)
{
	DBusConnection *conn;

	lua_getfield(L, 1, BUS_CONNECTION);
	conn = lua_touserdata(L, -1);
	lua_pop(L, 1);

	dbus_connection_unref(conn);

	return 0;
}

static int new_connection(lua_State *L, DBusConnection *conn)
{
	if (dbus_error_is_set(&err)) {
		lua_pushnil(L);
		lua_pushstring(L, err.message);
		dbus_error_free(&err);
		return 2;
	}

	if (conn == NULL)
		return error(L, "Couldn't create connection");

	/* create new table for the bus */
	lua_newtable(L);

	/* ..and set the metatable */
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_setmetatable(L, -2);

	/* insert light userdata for the connection */
	lua_pushliteral(L, BUS_CONNECTION);
	lua_pushlightuserdata(L, conn);
	lua_rawset(L, -3);

	/* insert table for signal handlers */
	lua_pushliteral(L, BUS_SIGNAL_HANDLERS);
	lua_newtable(L);
	lua_rawset(L, -3);

	/* insert table for running threads */
	lua_pushliteral(L, BUS_RUNNING_THREADS);
	lua_newtable(L);
	lua_rawset(L, -3);

	/* set the signal handler */
	if (!dbus_connection_add_filter(conn, 
				(DBusHandleMessageFunction)signal_handler,
				NULL, NULL))
		return error(L, "Not enough memory to add filter");

	return 1;
}

/*
 * system_bus()
 *
 * upvalue 1: DBus
 */
static int simpledbus_system_bus(lua_State *L)
{
	return new_connection(L, dbus_bus_get(DBUS_BUS_SYSTEM, &err));
}

/*
 * session_bus()
 *
 * upvalue 1: DBus
 */
static int simpledbus_session_bus(lua_State *L)
{
	return new_connection(L, dbus_bus_get(DBUS_BUS_SESSION, &err));
}

/*
 * It starts...
 */
LUALIB_API int luaopen_simpledbus(lua_State *L)
{
	/* initialise the errors */
	dbus_error_init(&err);

	/* make a table for this module */
	lua_newtable(L);

	/* make the DBus metatable */
	lua_newtable(L);

	/* DBus.__index = DBus */
	lua_pushvalue(L, 3);
	lua_setfield(L, 3, "__index");

	/* insert the system_bus function */
	lua_pushvalue(L, 3); /* upvalue 1: DBus */
	lua_pushcclosure(L, simpledbus_system_bus, 1);
	lua_setfield(L, 2, "system_bus");

	/* insert the session_bus function */
	lua_pushvalue(L, 3); /* upvalue 1: DBus */
	lua_pushcclosure(L, simpledbus_session_bus, 1);
	lua_setfield(L, 2, "session_bus");

	/* insert the call_method function */
	lua_pushcclosure(L, bus_call_method, 0);
	lua_setfield(L, 3, "call_method");

	/* insert the register_signal function */
	lua_pushcclosure(L, bus_register_signal, 0);
	lua_setfield(L, 3, "register_signal");

	/* insert the run function */
	lua_pushcclosure(L, bus_run, 0);
	lua_setfield(L, 3, "run");

	/* insert the stop function */
	lua_pushcclosure(L, bus_stop, 0);
	lua_setfield(L, 3, "stop");

	/* insert the garbage collection metafunction */
	lua_pushcclosure(L, bus_gc, 0);
	lua_setfield(L, 3, "__gc");

	/* insert the DBus class and metatable */
	lua_setfield(L, 2, "DBus");

	/* make the Proxy class and metatable */
	lua_newtable(L);

	/* Proxy.__index = Proxy */
	lua_pushvalue(L, 3);
	lua_setfield(L, 3, "__index");


	/* make the Method metatable */
	lua_newtable(L);

	/* Method.__index = Method */
	lua_pushvalue(L, 4);
	lua_setfield(L, 4, "__index");

	/* make the Signal metatable */
	lua_newtable(L);

	/* Signal.__index = Signal */
	lua_pushvalue(L, 5);
	lua_setfield(L, 5, "__index");

	/* insert the parse function */
	lua_pushvalue(L, 4); /* upvalue 1: Method */
	lua_pushvalue(L, 5); /* upvalue 2: Signal */
	lua_pushcclosure(L, proxy_parse, 2);
	lua_setfield(L, 3, "parse");

	/* insert the Signal metatable */
	lua_setfield(L, 2, "Signal");

	/* insert the Method metatable */
	lua_setfield(L, 2, "Method");

	/* insert the Proxy metatable */
	lua_setfield(L, 2, "Proxy");

	return 1;
}
