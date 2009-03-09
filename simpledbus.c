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
static DBusObjectPathVTable vtable;
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

typedef struct {
	DBusConnection *conn;
	unsigned int watches_changed;
	unsigned int nactive;
	DBusWatch *active;
	int index;
} LCon;

static dbus_bool_t watch_list_insert(LCon *c, DBusWatch *watch)
{
	DBusWatch *prev, *next;

	if (c->active == NULL) {
		c->active = watch;
		dbus_watch_set_data(watch, NULL, NULL);
		c->nactive++;
		c->watches_changed = 1;
		return TRUE;
	}

	next = c->active;
	while (next != watch) {
		prev = next;
		next = dbus_watch_get_data(prev);

		if (next == NULL) { /* list ended */
			dbus_watch_set_data(prev, watch, NULL);
			dbus_watch_set_data(watch, NULL, NULL);
			c->nactive++;
			c->watches_changed = 1;
			return TRUE;
		}
	}

	/* Hmm.. watch was already in
	 * the list of active watches */
	return TRUE;
}

static dbus_bool_t add_watch_cb(DBusWatch *watch, LCon *c)
{
#ifdef DEBUG
	printf("Add watch: ");
	dump_watch(watch);
	fflush(stdout);
#endif
	if (dbus_watch_get_enabled(watch) == FALSE)
		return TRUE;

	return watch_list_insert(c, watch);
}

static void watch_list_remove(LCon *c, DBusWatch *watch)
{
	DBusWatch *prev, *next;

	if (watch == c->active) {
		c->active = dbus_watch_get_data(watch);
		dbus_watch_set_data(watch, NULL, NULL);
		c->nactive--;
		c->watches_changed = 1;
		return;
	}

	next = c->active;
	while (next) {
		prev = next;
		next = dbus_watch_get_data(prev);

		if (watch == next) {
			dbus_watch_set_data(prev,
					dbus_watch_get_data(watch), NULL);
			dbus_watch_set_data(watch, NULL, NULL);
			c->nactive--;
			c->watches_changed = 1;
			return;
		}
	}
	/* Watch wasn't found */
}

static void remove_watch_cb(DBusWatch *watch, LCon *c)
{
#ifdef DEBUG
	printf("Remove watch: ");
	dump_watch(watch);
	fflush(stdout);
#endif

	if (dbus_watch_get_enabled(watch) == FALSE)
		return;

	watch_list_remove(c, watch);
}

static void toggle_watch_cb(DBusWatch *watch, LCon *c)
{
#ifdef DEBUG
	printf("Toggle watch: ");
	dump_watch(watch);
	fflush(stdout);
#endif
	if (dbus_watch_get_enabled(watch))
		(void)watch_list_insert(c, watch);
	else
		watch_list_remove(c, watch);
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
			dbus_message_unref(msg);
			nargs = 2;
			break;
		default:
			dbus_message_unref(msg);
			nargs = error(T, "Unknown reply");
		}
	}

	switch (lua_resume(T, nargs)) {
	case 0: /* thread finished */
#ifdef DEBUG
		printf("Thread finished, lua_gettop(T) = %i, "
				"lua_type(T, 1) = %s\n",
				lua_gettop(T),
				lua_typename(T, lua_type(T, 1)));
#endif
		if (lua_iscfunction(T, 1) && lua_tocfunction(T, 1)(T)) {
			/* move error message to main thread and error */
			lua_xmove(T, mainThread, 1);
			lua_error(mainThread);
		}
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
 * Bus:call_method()
 *
 * argument 1: bus
 * argument 2: target
 * argument 3: object
 * argument 4: interface
 * argument 5: method
 * argument 6: signature
 * ...
 */
static int bus_call_method(lua_State *L)
{
	LCon *c = bus_check(L, 1);
	const char *interface;
	DBusMessage *msg;
	DBusMessage *ret;

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
	interface = lua_tostring(L, 4);
	if (interface && *interface == '\0')
		interface = NULL;

	msg = dbus_message_new_method_call(
				lua_tostring(L, 2),
				lua_tostring(L, 3),
				interface,
				lua_tostring(L, 5));
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
		dbus_message_unref(ret);
		return 2;
	}

	dbus_message_unref(ret);

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
#ifdef DEBUG
	printf("received \"%s\"\n", lua_tostring(mainThread, -1));
	fflush(stdout);
#endif
	lua_rawget(mainThread, c->index); /* signal handler table */
	if (lua_type(mainThread, -1) != LUA_TFUNCTION) {
		lua_pop(mainThread, 1);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	/* create new Lua thread and move the
	 * Lua signal handler there */
	T = lua_newthread(mainThread);
	lua_pushnil(T);
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
#ifdef DEBUG
	printf("added rule=\"%s\"\n", rule); fflush(stdout);
#endif
	/* add the rule */
	dbus_bus_add_match(conn, rule, &err);
}

static void remove_match(DBusConnection *conn,
		const char *object,
		const char *interface,
		const char *signal)
{
	char *rule = alloca(45 + strlen(object)
			+ strlen(interface) + strlen(signal));

	/* construct rule to catch the signal messages */
	sprintf(rule, "type='signal',path='%s',interface='%s',member='%s'",
			object, interface, signal);
#ifdef DEBUG
	printf("removed rule=\"%s\"\n", rule); fflush(stdout);
#endif
	/* add the rule */
	dbus_bus_remove_match(conn, rule, &err);
}

/*
 * Bus:register_signal()
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
	const char *object = luaL_checkstring(L, 2);
	const char *interface = luaL_checkstring(L, 3);
	const char *signal = luaL_checkstring(L, 4);

	luaL_checktype(L, 5, LUA_TFUNCTION);

	/* drop extra arguments */
	lua_settop(L, 5);

	/* get the signal handler table */
	lua_getfenv(L, 1);
	/* ..and insert it before the function */
	lua_insert(L, 5);

	/* push the signal string */
	push_signal_string(L, object, interface, signal);
	/* ..and insert it before the function */
	lua_insert(L, 6);

	/* check if signal is already set */
	lua_pushvalue(L, 6);
	lua_rawget(L, 5);
	if (lua_isnil(L, 8)) {
		/* if we didn't already set this signal
		   add the rule and check for errors */
		add_match(conn, object, interface, signal);
		if (dbus_error_is_set(&err)) {
			lua_pushnil(L);
			lua_pushstring(L, err.message);
			dbus_error_free(&err);
			return 2;
		}
	}
	lua_settop(L, 7);

	/* add the function to the signal handler table */
	lua_rawset(L, 5);

	/* return true */
	lua_pushboolean(L, 1);
	return 1;
}

/*
 * Bus:unregister_signal()
 *
 * argument 1: connection
 * argument 2: object
 * argument 3: interface
 * argument 4: signal
 */
static int bus_unregister_signal(lua_State *L)
{
	DBusConnection *conn = bus_check(L, 1)->conn;
	const char *object = luaL_checkstring(L, 2);
	const char *interface = luaL_checkstring(L, 3);
	const char *signal = luaL_checkstring(L, 4);

	/* drop extra arguments */
	lua_settop(L, 4);

	/* get the signal handler table */
	lua_getfenv(L, 1);

	/* push the signal string */
	push_signal_string(L, object, interface, signal);

	/* check if signal is set at all*/
	lua_pushvalue(L, 6);
	lua_rawget(L, 5);
	if (lua_isnil(L, 7))
		return luaL_error(L, "Signal not set");
	lua_settop(L, 6);

	remove_match(conn, object, interface, signal);

	/* set sh_table[sh_string] = nil */
	lua_pushnil(L);
	lua_rawset(L, 5);

	/* return true */
	lua_pushboolean(L, 1);
	return 1;
}

static int send_reply(lua_State *T)
{
	DBusConnection *conn = lua_touserdata(T, 2);
	DBusMessage *ret = lua_touserdata(T, 3);
	const char *signature = lua_tostring(T, 4);

	if (signature && *signature != '\0')
		add_arguments(T, 5, lua_gettop(T), signature, ret);

	if (!dbus_connection_send(conn, ret, NULL))
		luaL_error(mainThread, "Out of memory");

	dbus_message_unref(ret);

	return 0;
}

static DBusHandlerResult method_call_handler(DBusConnection *conn,
		DBusMessage *msg, lua_State *T)
{
	lua_State *N;
	DBusMessage *ret;

#ifdef DEBUG
	printf("Received message: path = %s,"
			" interface = %s, member = %s\n",
			dbus_message_get_path(msg),
			dbus_message_get_interface(msg),
			dbus_message_get_member(msg));
	fflush(stdout);
#endif

	lua_pushfstring(T, "%s.%s",
			dbus_message_get_interface(msg),
			dbus_message_get_member(msg));

	lua_rawget(T, 2);
	if (!lua_istable(T, 3)) {
		lua_settop(T, 2);
#ifdef DEBUG
		printf("..not handled\n"); fflush(stdout);
#endif
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	/* create a new thread to run the method in */
	N = lua_newthread(T);
	/* ..and insert it before the function table */
	lua_insert(T, 3);

	/* push the send_reply function */
	lua_pushcclosure(N, send_reply, 0);

	/* push the connection */
	lua_pushlightuserdata(N, conn);

	/* create new message return */
	ret = dbus_message_new_method_return(msg);
	/* ..and push it to N */
	lua_pushlightuserdata(N, ret);

	/* move the return signature and the function to N */
	lua_rawgeti(T, 4, 2);
	lua_rawgeti(T, 4, 3);
	lua_xmove(T, N, 2);

	/* forget about the function table */
	lua_settop(T, 3);

	dbus_message_ref(msg);
	switch (lua_resume(N, push_arguments(N, msg))) {
	case 0: /* thread finished */
		if (send_reply(N)) {
			/* move error message to main thread and error */
			lua_xmove(N, mainThread, 1);
			lua_error(mainThread);
		}
		/* forget about the thread */
		lua_settop(T, 2);
		break;
	case LUA_YIELD:	/* thread yielded */
		/* save it in the running threads table */
		lua_pushboolean(T, 1);
		lua_rawset(T, 1); /* thread table */
		break;
	default:
		/* move error message to main thread and error */
		lua_xmove(N, mainThread, 1);
		lua_error(mainThread);
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}

/*
 * Bus:register_object_path()
 *
 * argument 1: connection
 * argument 2: path
 * argument 3: method table
 */
static int bus_register_object_path(lua_State *L)
{
	LCon *c = bus_check(L, 1);
	const char *path = luaL_checkstring(L, 2);
	lua_State *T;

	luaL_checktype(L, 3, LUA_TTABLE);

	/* drop extra arguments */
	lua_settop(L, 3);

	/* move method table before path */
	lua_insert(L, 2);

	/* get the signal/thread table of the conection */
	lua_getfenv(L, 1);
	/* ..and move it before the method table */
	lua_insert(L, 2);

	/* create thread for storing object data
	 * PS. yes, this is a bad hack >.< */
	T = lua_newthread(L);
	if (T == NULL) {
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}

	/*  register the object path */
	if (!dbus_connection_register_object_path(c->conn, path, &vtable, T)) {
		lua_pushnil(L);
		lua_pushliteral(L, "Error registering object path");
		return 2;
	}

	/* push the thread table to the thread */
	lua_pushvalue(L, 2);
	lua_xmove(L, T, 1);

	/* save the thread in the thread table */
	lua_rawset(L, 2);

	/* move method table to the thread*/
	lua_xmove(L, T, 1);

	/* return true */
	lua_pushboolean(L, 1);
	return 1;
}

/*
 * Bus:unregister_object_path()
 *
 * argument 1: connection
 * argument 2: path
 */
static int bus_unregister_object_path(lua_State *L)
{
	LCon *c = bus_check(L, 1);
	const char *path = luaL_checkstring(L, 2);

	/* drop extra arguments */
	lua_settop(L, 2);

	/* get the signal/thread table of the conection */
	lua_getfenv(L, 1);
	/* ..and move it before the path */
	lua_insert(L, 2);

	lua_pushvalue(L, 3);
	lua_rawget(L, 2);
	if (!lua_isthread(L, 4))
		return luaL_error(L, "Object path not registered");
	lua_settop(L, 3);

	lua_pushnil(L);
	if (!dbus_connection_unregister_object_path(c->conn, path)) {
		lua_pushliteral(L, "Out of memory");
		return 2;
	}

	lua_rawset(L, 2);

	/* return true */
	lua_pushboolean(L, 1);
	return 1;
}

/*
 * DBus.__gc()
 */
static int bus_gc(lua_State *L)
{
	LCon *c = lua_touserdata(L, 1);
	dbus_connection_unref(c->conn);

	return 0;
}

/*
 * mainloop()
 */
static struct pollfd *make_poll_struct(int n, LCon **c, nfds_t *nfds)
{
	struct pollfd *p;
	struct pollfd *fds;
	nfds_t total = 0;
	int i;
	DBusWatch *watch;

	for (i = 0; i < n; i++)
		total += c[i]->nactive;
	*nfds = total;

	fds = malloc(total * sizeof(struct pollfd));
	if (fds == NULL)
		return NULL;

	p = fds;
	for (i = 0; i < n; i++) {
		for (watch = c[i]->active; watch;
				watch = dbus_watch_get_data(watch)) {
			unsigned int flags = dbus_watch_get_flags(watch);

			p->fd = dbus_watch_get_unix_fd(watch);
			p->events = POLLERR | POLLHUP;
			p->revents = 0;

			if (flags & DBUS_WATCH_READABLE)
				p->events |= POLLIN;
			if (flags & DBUS_WATCH_WRITABLE)
				p->events |= POLLOUT;

			p++;
		}

		c[i]->watches_changed = 0;
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

		r |= c[i]->watches_changed;
	}

	return r;
}

static inline void handleall(int n, LCon **c, struct pollfd *p)
{
	int i;
	DBusWatch *watch;

	for (i = 0; i < n; i++) {
		for (watch = c[i]->active; watch;
				watch = dbus_watch_get_data(watch)) {
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

				(void)dbus_watch_handle(watch, flags);
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

	/* make sure the stack can hold all the "fenv"'s of
	 * the connections (and a bit more) */
	if (!lua_checkstack(L, n+3))
		return error(L, "Out of memory");

	c = malloc(n * sizeof(LCon *));
	if (c == NULL)
		return error(L, "Out of memory");

	for (i = 0; i < n; i++) {
		c[i] = bus_check(L, i+1);
		lua_getfenv(L, i+1);
		c[i]->index = lua_gettop(L);
	}

	fds = make_poll_struct(n, c, &nfds);
	if (fds == NULL) {
		free(c);
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

	lua_pushlightuserdata(L, conn);
	lua_rawget(L, lua_upvalueindex(2)); /* connection table */
	if (lua_type(L, -1) == LUA_TUSERDATA) {
		dbus_connection_unref(conn);
		return 1;
	} else
		lua_settop(L, 0);

	/* create new userdata for the bus */
	c = lua_newuserdata(L, sizeof(LCon));
	if (c == NULL)
		return error(L, "Out of memory");
	c->conn = conn;
	c->nactive = 0;
	c->active = NULL;

	/* set watch functions */
	if (!dbus_connection_set_watch_functions(conn,
				(DBusAddWatchFunction)add_watch_cb,
				(DBusRemoveWatchFunction)remove_watch_cb,
				(DBusWatchToggledFunction)toggle_watch_cb,
				c, NULL)) {
		dbus_connection_unref(conn);
		return error(L, "Error setting watch functions");
	}

	/* set the signal handler */
	if (!dbus_connection_add_filter(conn,
				(DBusHandleMessageFunction)signal_handler,
				c, NULL)) {
		dbus_connection_unref(conn);
		return error(L, "Error adding filter");
	}

	/* set the metatable */
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_setmetatable(L, 1);

	/* create new environment table for
	 * signal handlers and running threads */
	lua_newtable(L);
	lua_setfenv(L, 1);

	/* insert the connection in the connection table */
	lua_pushlightuserdata(L, conn);
	lua_pushvalue(L, 1);
	lua_rawset(L, lua_upvalueindex(2));

	return 1;
}

/*
 * SessionBus()
 */
static int simpledbus_session_bus(lua_State *L)
{
	return new_connection(L, dbus_bus_get(DBUS_BUS_SESSION, &err));
}

/*
 * SystemBus()
 */
static int simpledbus_system_bus(lua_State *L)
{
	return new_connection(L, dbus_bus_get(DBUS_BUS_SYSTEM, &err));
}

/*
 * StarterBus()
 */
static int simpledbus_starter_bus(lua_State *L)
{
	return new_connection(L, dbus_bus_get(DBUS_BUS_STARTER, &err));
}

/*
 * open()
 */
static int simpledbus_open(lua_State *L)
{
	return new_connection(L,
			dbus_connection_open(luaL_checkstring(L, 1), &err));
}

#define set_dbus_string_constant(L, i, name) \
	lua_pushliteral(L, #name); \
	lua_pushliteral(L, DBUS_##name); \
	lua_rawset(L, i)
#define set_dbus_number_constant(L, i, name) \
	lua_pushliteral(L, #name); \
	lua_pushnumber(L, (lua_Number)DBUS_##name); \
	lua_rawset(L, i)

/*
 * It starts...
 */
LUALIB_API int luaopen_simpledbus_core(lua_State *L)
{
	luaL_Reg bus_funcs[] = {
		{"call_method", bus_call_method},
		{"register_signal", bus_register_signal},
		{"unregister_signal", bus_unregister_signal},
		{"register_object_path", bus_register_object_path},
		{"unregister_object_path", bus_unregister_object_path},
		{NULL, NULL}
	};
	luaL_Reg *p;

	/* initialise the errors */
	dbus_error_init(&err);

	/* initialise the vtable */
	vtable.unregister_function = NULL;
	vtable.message_function =
		(DBusObjectPathMessageFunction)method_call_handler;


	/* make a table for this module */
	lua_newtable(L);

	/* insert the stop() function*/
	lua_pushcclosure(L, simpledbus_stop, 0);
	lua_setfield(L, 2, "stop");

	/* make the Bus metatable */
	lua_newtable(L);

	/* DBus.__index = Bus */
	lua_pushvalue(L, 3);
	lua_setfield(L, 3, "__index");

	/* insert the mainloop() function*/
	lua_pushvalue(L, 3); /* upvalue 1: Bus */
	lua_pushcclosure(L, simpledbus_mainloop, 1);
	lua_setfield(L, 2, "mainloop");

	/* create table for connections and let
	 * the values be weak references */
	lua_newtable(L);
	lua_createtable(L, 0, 1);
	lua_pushliteral(L, "v");
	lua_setfield(L, 5, "__mode");
	lua_setmetatable(L, 4);

	/* insert the SessionBus() function */
	lua_pushvalue(L, 3); /* upvalue 1: Bus */
	lua_pushvalue(L, 4); /* upvalue 2: connection table */
	lua_pushcclosure(L, simpledbus_session_bus, 2);
	lua_setfield(L, 2, "SessionBus");

	/* insert the SystemBus() function */
	lua_pushvalue(L, 3); /* upvalue 1: Bus */
	lua_pushvalue(L, 4); /* upvalue 2: connection table */
	lua_pushcclosure(L, simpledbus_system_bus, 2);
	lua_setfield(L, 2, "SystemBus");

	/* insert the StarterBus() function */
	lua_pushvalue(L, 3); /* upvalue 1: Bus */
	lua_pushvalue(L, 4); /* upvalue 2: connection table */
	lua_pushcclosure(L, simpledbus_starter_bus, 2);
	lua_setfield(L, 2, "StarterBus");

	/* insert the open() function */
	lua_pushvalue(L, 3); /* upvalue 1: Bus */
	lua_pushvalue(L, 4); /* upvalue 2: connection table */
	lua_pushcclosure(L, simpledbus_open, 2);
	lua_setfield(L, 2, "open");

	/* pop connection table */
	lua_settop(L, 3);

	/* insert Bus methods */
	for (p = bus_funcs; p->name; p++) {
		lua_pushvalue(L, 3); /* upvalue 1: Bus */
		lua_pushcclosure(L, p->func, 1);
		lua_setfield(L, 3, p->name);
	}

	/* insert the garbage collection metafunction */
	lua_pushcclosure(L, bus_gc, 0);
	lua_setfield(L, 3, "__gc");

	/* insert the Bus class and metatable */
	lua_setfield(L, 2, "Bus");

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

	/* insert constants */
	set_dbus_string_constant(L, 2, SERVICE_DBUS);
	set_dbus_string_constant(L, 2, PATH_DBUS);
	set_dbus_string_constant(L, 2, INTERFACE_DBUS);
	set_dbus_string_constant(L, 2, INTERFACE_PROPERTIES);
	set_dbus_string_constant(L, 2, INTERFACE_PEER);
	set_dbus_string_constant(L, 2, INTERFACE_LOCAL);

	set_dbus_number_constant(L, 2, NAME_FLAG_ALLOW_REPLACEMENT);
	set_dbus_number_constant(L, 2, NAME_FLAG_REPLACE_EXISTING);
	set_dbus_number_constant(L, 2, NAME_FLAG_DO_NOT_QUEUE);

	set_dbus_number_constant(L, 2, REQUEST_NAME_REPLY_PRIMARY_OWNER);
	set_dbus_number_constant(L, 2, REQUEST_NAME_REPLY_IN_QUEUE);
	set_dbus_number_constant(L, 2, REQUEST_NAME_REPLY_EXISTS);
	set_dbus_number_constant(L, 2, REQUEST_NAME_REPLY_ALREADY_OWNER);

	set_dbus_number_constant(L, 2, RELEASE_NAME_REPLY_RELEASED);
	set_dbus_number_constant(L, 2, RELEASE_NAME_REPLY_NON_EXISTENT);
	set_dbus_number_constant(L, 2, RELEASE_NAME_REPLY_NOT_OWNER);

	set_dbus_number_constant(L, 2, START_REPLY_SUCCESS);
	set_dbus_number_constant(L, 2, START_REPLY_ALREADY_RUNNING);

	return 1;
}
