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
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>

#define LUA_LIB
#include <lua.h>
#include <lauxlib.h>
#include <dbus/dbus.h>

#ifdef ALLINONE
#include <expat.h>

#define EXPORT static
#else
#include "add.h"
#include "push.h"
#include "parse.h"

#define EXPORT
#endif

static DBusError err;
static lua_State *mainThread = NULL;
static unsigned int stop;

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

#ifdef DEBUG
static void dump_watch(DBusWatch *watch)
{
	char *s;
	unsigned int flags = dbus_watch_get_flags(watch);

	if (flags & DBUS_WATCH_READABLE) {
		if (flags & DBUS_WATCH_WRITABLE)
			s = "READ|WRITE";
		else
			s = "READ";
	} else if (flags & DBUS_WATCH_WRITABLE)
		s = "WRITE";
	else
		s = "";

	printf("watch = %p, fd = %i, flags = %s, enabled = %s\n",
			(void *)watch,
			dbus_watch_get_unix_fd(watch),
			s,
			dbus_watch_get_enabled(watch) ? "true" : "false");
}
#endif

struct wlist_s;
typedef struct wlist_s *wlist;
struct wlist_s {
	DBusWatch *watch;
	wlist next;
};

typedef struct {
	DBusConnection *conn;
	unsigned int nwatches;
	unsigned int new_watch;
	wlist watches;
	int index;
} LCon;

static dbus_bool_t add_watch_cb(DBusWatch *watch, LCon *c)
{
	wlist n;
#ifdef DEBUG
	printf("Add watch: ");
	dump_watch(watch);
	fflush(stdout);
#endif
	if (dbus_watch_get_enabled(watch) == FALSE)
		return TRUE;

	n = malloc(sizeof(struct wlist_s));
	if (n == NULL)
		return FALSE;

	c->nwatches++;
	c->new_watch = 1;
	n->watch = watch;
	n->next = c->watches;
	c->watches = n;

	return TRUE;
}

static void remove_watch_cb(DBusWatch *watch, LCon *c)
{
	wlist *p;
#ifdef DEBUG
	printf("Remove watch: ");
	dump_watch(watch);
	fflush(stdout);
#endif
	p = &c->watches;
	while (*p && (*p)->watch != watch)
		p = &(*p)->next;

	if (*p) {
		wlist n = *p;

		*p = n->next;
		free(n);

		c->nwatches--;
		c->new_watch = 1;
	}
}

static void toggle_watch_cb(DBusWatch *watch, LCon *c)
{
#ifdef DEBUG
	printf("Toggle watch: ");
	dump_watch(watch);
	fflush(stdout);
#endif
	if (dbus_watch_get_enabled(watch))
		(void)add_watch_cb(watch, c);
	else
		remove_watch_cb(watch, c);
}

static LCon *bus_check(lua_State *L, int index)
{
	int r;

	if (lua_getmetatable(L, index) == 0)
		luaL_argerror(L, index,
				"expected a DBus connection");

	r = lua_equal(L, lua_upvalueindex(1), -1);
	lua_pop(L, 1);
	if (r == 0)
		luaL_argerror(L, index,
				"expected a DBus connection");

	return (LCon *)lua_touserdata(L, index);
}

static void method_return_handler(DBusPendingCall *pending, lua_State *T)
{
	DBusMessage *msg = dbus_pending_call_steal_reply(pending);
	LCon *c;
	int nargs;

	dbus_pending_call_unref(pending);

	/* pop the connection from the thread */
	c = lua_touserdata(T, -1);
	lua_pop(T, 1);

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
		lua_rawset(mainThread, c->index); /* thread table */
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
 * DBus:call_method()
 *
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
	const char *interface;
	DBusMessage *msg;
	DBusMessage *ret;
	LCon *c = bus_check(L, 1);

#ifdef DEBUG
	printf("Calling:\n  %s\n  %s\n  %s\n  %s\n  %s\n",
				lua_tostring(L, 2),
				lua_tostring(L, 3),
				lua_tostring(L, 4),
				lua_tostring(L, 5),
				lua_tostring(L, 6));
	fflush(stdout);
#endif

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
		add_arguments(L, 7, lua_gettop(L), lua_tostring(L, 6), msg);

	if (!lua_pushthread(L)) { /* L can be yielded */
		DBusPendingCall *pending;

		if (!dbus_connection_send_with_reply(c->conn, msg, &pending, -1))
			return error(L, "Out of memory");

		if (!dbus_pending_call_set_notify(pending,
					(DBusPendingCallNotifyFunction)
					method_return_handler, L, NULL))
			return error(L, "Out of memory");

		/* yield the connection */
		lua_settop(L, 1);
		return lua_yield(L, 1);
	}
	lua_pop(L, 1);

	/* L is the main thread, so we call the method synchronously */
	ret = dbus_connection_send_with_reply_and_block(c->conn, msg, -1, &err);

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
		DBusMessage *msg, LCon *c)
{
	lua_State *T;

	if (msg == NULL || dbus_message_get_type(msg)
			!= DBUS_MESSAGE_TYPE_SIGNAL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	push_signal_string(mainThread,
			dbus_message_get_path(msg),
			dbus_message_get_interface(msg),
			dbus_message_get_member(msg));
	/*
	printf("received \"%s\"\n", lua_tostring(mainThread, -1));
	fflush(stdout);
	*/

	lua_rawget(mainThread, c->index); /* signal handler table */

	if (lua_type(mainThread, -1) != LUA_TFUNCTION) {
		lua_pop(mainThread, 1);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	/* create new Lua thread and move the
	 * Lua signal handler there */
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
		lua_rawset(mainThread, c->index); /* thread table */
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
 * DBus:register_signal()
 *
 * argument 1: connection
 * argument 2: object
 * argument 3: interface
 * argument 4: signal
 * argument 5: function
 */
static int bus_register_signal(lua_State *L)
{
	DBusConnection *conn = bus_check(L, 1)->conn;

	if (lua_gettop(L) < 5)
		return error(L, "Too few arguments");

	/* get the signal handler table */
	lua_getfenv(L, 1);

	{
		const char *object = lua_tostring(L, 2);
		const char *interface = lua_tostring(L, 3);
		const char *signal = lua_tostring(L, 4);
		int unset;

		/* push the signal string */
		push_signal_string(L, object, interface, signal);

		/* check if signal is already set */
		lua_pushvalue(L, -1);
		lua_rawget(L, -3);
		unset = lua_isnil(L, -1);
		lua_pop(L, 1);

		/* if we didn't already set this signal
		 * tell dbus we're interested */
		if (unset) {
			/* add the rule and check for errors */
			add_match(conn, object, interface, signal);
			if (dbus_error_is_set(&err)) {
				lua_pop(L, 2);
				lua_pushnil(L);
				lua_pushstring(L, err.message);
				dbus_error_free(&err);
				return 2;
			}
		}
	}

	/* add the function to the signal handler table */
	lua_pushvalue(L, 5);
	lua_rawset(L, -3); /* signal handler table */
	lua_pop(L, 1);

	/* return true */
	lua_pushboolean(L, 1);
	return 1;
}

/*
 * DBus.__gc()
 */
static int bus_gc(lua_State *L)
{
	wlist w;

	LCon *c = lua_touserdata(L, 1);
	dbus_connection_unref(c->conn);

	w = c->watches;
	while (w) {
		wlist tmp = w;
		w = w->next;
		free(tmp);
	}

	return 0;
}

/*
 * mainloop()
 */
static struct pollfd *make_poll_struct(int n, LCon **c, nfds_t *nfds)
{
	wlist w;
	struct pollfd *p;
	struct pollfd *fds;
	nfds_t total = 0;
	int i;

	for (i = 0; i < n; i++)
		total += c[i]->nwatches;
	*nfds = total;

	fds = malloc(total * sizeof(struct pollfd));
	if (fds == NULL)
		return NULL;

	p = fds;
	for (i = 0; i < n; i++) {
		for (w = c[i]->watches; w; w = w->next) {
			unsigned int flags = dbus_watch_get_flags(w->watch);

			p->fd = dbus_watch_get_unix_fd(w->watch);
			p->events = POLLERR | POLLHUP;
			p->revents = 0;

			if (flags & DBUS_WATCH_READABLE)
				p->events |= POLLIN;
			if (flags & DBUS_WATCH_WRITABLE)
				p->events |= POLLOUT;

			p++;
		}

		c[i]->new_watch = 0;
	}

	return fds;
}

static inline unsigned int dispatchall(int n, LCon **c)
{
	unsigned int r;
	int i;

	for (i = 0; i < n; i++) {
		DBusConnection *conn = c[i]->conn;

		if (dbus_connection_get_dispatch_status(conn)
				== DBUS_DISPATCH_DATA_REMAINS) {
			while (dbus_connection_dispatch(conn)
					== DBUS_DISPATCH_DATA_REMAINS);
		}

		r |= c[i]->new_watch;
	}

	return r;
}

static inline void handleall(int n, LCon **c, struct pollfd *p)
{
	int i;

	for (i = 0; i < n; i++) {
		wlist w;

		for (w = c[i]->watches; w; w = w->next) {
			if (p->revents) {
				unsigned int flags = 0;

				if (p->revents & POLLIN)
					flags |= DBUS_WATCH_READABLE;
				if (p->revents & POLLOUT)
					flags |= DBUS_WATCH_WRITABLE;
				if (p->revents & POLLERR)
					flags |= DBUS_WATCH_ERROR;
				if (p->revents & POLLHUP)
					flags |= DBUS_WATCH_HANGUP;

				(void)dbus_watch_handle(w->watch, flags);
				p->revents = 0;
			}
			p++;
		}
	}
}

static int simpledbus_mainloop(lua_State *L)
{
	LCon **c;
	struct pollfd *fds;
	nfds_t nfds;
	int i;
	int n = lua_gettop(L);

	if (n < 1)
		return luaL_error(L, "Too few arguments");

	if (mainThread)
		return luaL_error(L, "Another main loop is already running");

	c = malloc(n * sizeof(LCon *));
	if (c == NULL)
		return error(L, "Out of memory");

	for (i = 1; i <= n; i++) {
		c[i-1] = bus_check(L, i);
		lua_getfenv(L, i);
		lua_replace(L, i);
		c[i-1]->index = i;
	}

	fds = make_poll_struct(n, c, &nfds);
	if (fds == NULL) {
		free(c);
		lua_settop(L, 0);
		return error(L, "Out of memory");
	}

	stop = 0;
	mainThread = L;

	while (1) {
		unsigned int watches_changed = dispatchall(n, c);

		if (stop)
			break;

		if (watches_changed) {
			free(fds);
			fds = make_poll_struct(n, c, &nfds);
			if (fds == NULL) {
				free(c);
				lua_settop(L, 0);
				mainThread = NULL;
				return error(L, "Out of memory");
			}
		}

#ifdef DEBUG
		printf("("); fflush(stdout);
#endif
		if (poll(fds, nfds, -1) < 0) {
			free(c);
			free(fds);
			lua_settop(L, 0);
			mainThread = NULL;
#if 1
			lua_pushnil(L);
			lua_pushfstring(L, "Error polling DBus: %s",
					strerror(errno));
			return 2;
#else
			return error(L, "Error polling DBus: %s",
					strerror(errno));
#endif
		}
#ifdef DEBUG
		printf(")"); fflush(stdout);
#endif
		handleall(n, c, fds);
	}

	free(c);
	free(fds);
	lua_settop(L, 0);

	mainThread = NULL;

	/* return true */
	lua_pushboolean(L, 1);
	return 1;
}

/*
 * stop()
 */
static int simpledbus_stop(lua_State *L)
{
	stop = 1;

	return 0;
}

static int new_connection(lua_State *L, DBusConnection *conn)
{
	LCon *c;

	if (dbus_error_is_set(&err)) {
		lua_pushnil(L);
		lua_pushstring(L, err.message);
		dbus_error_free(&err);
		return 2;
	}

	if (conn == NULL)
		return error(L, "Couldn't create connection");

	/* create new userdata for the bus */
	c = lua_newuserdata(L, sizeof(LCon));
	if (c == NULL)
		return error(L, "Out of memory");
	c->conn = conn;
	c->nwatches = 0;
	c->watches = NULL;

	/* set watch functions */
	if (!dbus_connection_set_watch_functions(conn,
				(DBusAddWatchFunction)add_watch_cb,
				(DBusRemoveWatchFunction)remove_watch_cb,
				(DBusWatchToggledFunction)toggle_watch_cb,
				c, NULL)) {
		dbus_connection_unref(conn);
		lua_pop(L, 1);
		return error(L, "Error setting watch functions");
	}

	/* set the signal handler */
	if (!dbus_connection_add_filter(conn,
				(DBusHandleMessageFunction)signal_handler,
				c, NULL)) {
		dbus_connection_unref(conn);
		lua_pop(L, 1);
		return error(L, "Error adding filter");
	}

	/* set the metatable */
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_setmetatable(L, -2);

	/* create new environment table for
	 * signal handlers and running threads */
	lua_newtable(L);
	lua_setfenv(L, -2);

	return 1;
}

/*
 * SystemBus()
 */
static int simpledbus_system_bus(lua_State *L)
{
	return new_connection(L, dbus_bus_get(DBUS_BUS_SYSTEM, &err));
}

/*
 * SessionBus()
 */
static int simpledbus_session_bus(lua_State *L)
{
	return new_connection(L, dbus_bus_get(DBUS_BUS_SESSION, &err));
}

/*
 * It starts...
 */
LUALIB_API int luaopen_simpledbus_core(lua_State *L)
{
	luaL_Reg bus_funcs[] = {
		{"call_method", bus_call_method},
		{"register_signal", bus_register_signal},
		{NULL, NULL}
	};
	luaL_Reg *p;

	/* initialise the errors */
	dbus_error_init(&err);

	/* make a table for this module */
	lua_newtable(L);

	/* insert the stop() function*/
	lua_pushcclosure(L, simpledbus_stop, 0);
	lua_setfield(L, 2, "stop");

	/* make the DBus metatable */
	lua_newtable(L);

	/* DBus.__index = DBus */
	lua_pushvalue(L, 3);
	lua_setfield(L, 3, "__index");

	/* insert the system_bus function */
	lua_pushvalue(L, 3); /* upvalue 1: DBus */
	lua_pushcclosure(L, simpledbus_system_bus, 1);
	lua_setfield(L, 2, "SystemBus");

	/* insert the session_bus function */
	lua_pushvalue(L, 3); /* upvalue 1: DBus */
	lua_pushcclosure(L, simpledbus_session_bus, 1);
	lua_setfield(L, 2, "SessionBus");

	/* insert the mainloop() function*/
	lua_pushvalue(L, 3); /* upvalue 1: DBus */
	lua_pushcclosure(L, simpledbus_mainloop, 1);
	lua_setfield(L, 2, "mainloop");

	/* insert DBus methods */
	for (p = bus_funcs; p->name; p++) {
		lua_pushvalue(L, 3); /* upvalue 1: DBus */
		lua_pushcclosure(L, p->func, 1);
		lua_setfield(L, 3, p->name);
	}

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
