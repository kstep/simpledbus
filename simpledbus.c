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

#include "add.c"
#include "push.c"
#include "parse.c"

#else /* ALLINONE */

#include "add.h"
#include "push.h"
#include "parse.h"

#endif /* ALLINONE */

static DBusError err;
static DBusObjectPathVTable vtable;
static lua_State *mainThread = NULL;
static int stop;

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

/*
 * Bus:get_signal_table()
 *
 * argument 1: bus
 */
static int bus_get_signal_table(lua_State *L)
{
	(void)bus_check(L, 1);

	lua_getfenv(L, 1);
	lua_rawgeti(L, 2, 2);

	return 1;
}

static void method_return_handler(DBusPendingCall *pending, lua_State *T)
{
	DBusMessage *msg = dbus_pending_call_steal_reply(pending);
	int nargs;

	dbus_pending_call_unref(pending);

	/* remove the thread from the threads table */
	lua_pushthread(T);
	lua_pushnil(T);
	lua_rawset(T, -3);
	/* pop threads table from the thread */
	lua_pop(T, 1);

	if (msg == NULL) {
		lua_pushnil(T);
		lua_pushliteral(T, "Reply null");
		nargs = 2;
	} else {
		switch (dbus_message_get_type(msg)) {
		case DBUS_MESSAGE_TYPE_METHOD_RETURN:
			nargs = push_arguments(T, msg);
			dbus_message_unref(msg);
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
			lua_pushnil(T);
			lua_pushliteral(T, "Unknown reply");
			nargs = 2;
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
		if (lua_iscfunction(T, 1) && lua_tocfunction(T, 1)(T)
				&& stop == 0) {
			/* move error message to main thread */
			lua_xmove(T, mainThread, 1);
			stop = -1;
		}
	case LUA_YIELD: /* thread yielded again */
		break;
	default:
		if (stop == 0) {
			/* move error message to main */
			lua_xmove(T, mainThread, 1);
			stop = -1;
		}
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
 * argument 6: signature (optional)
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
	if (msg == NULL) {
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}

	/* get the signature and add arguments */
	if (lua_isstring(L, 6)) {
		const char *signature = lua_tostring(L, 6);
		if (*signature &&
				add_arguments(L, 7, lua_gettop(L), signature, msg))
			return lua_error(L);
	}

	/* if (!lua_pushthread(L)) { / * L can be yielded */
	if (mainThread) { /* main loop is running */
		DBusPendingCall *pending;

		if (!dbus_connection_send_with_reply(c->conn, msg, &pending, -1)) {
			lua_pushnil(L);
			lua_pushliteral(L, "Out of memory");
			return 2;
		}

		if (!dbus_pending_call_set_notify(pending,
					(DBusPendingCallNotifyFunction)
					method_return_handler, L, NULL)) {
			lua_pushnil(L);
			lua_pushliteral(L, "Out of memory");
			return 2;
		}

		/* get the threads table */
		lua_settop(L, 1);
		lua_getfenv(L, 1);
		/* save the thread there */
		lua_pushthread(L);
		lua_pushboolean(L, 1);
		lua_rawset(L, 2);
		/* yield the threads table */
		return lua_yield(L, 1);
	}
	/* lua_pop(L, 1); */

	/* L is the main thread, so we call the method synchronously */
	ret = dbus_connection_send_with_reply_and_block(c->conn, msg, -1, &err);

	/* free message */
	dbus_message_unref(msg);

	/* check reply */
	if (ret == NULL) {
		lua_pushnil(L);
		if (dbus_error_is_set(&err)) {
			lua_pushstring(L, err.message);
			dbus_error_free(&err);
		} else
			lua_pushliteral(L, "Reply null");
		return 2;
	}

	switch (dbus_message_get_type(ret)) {
	case DBUS_MESSAGE_TYPE_METHOD_RETURN:
		{
			/* read the parameters */
			int nargs = push_arguments(L, ret);
			dbus_message_unref(ret);

			return nargs;
		}
	case DBUS_MESSAGE_TYPE_ERROR:
		lua_pushnil(L);
		dbus_set_error_from_message(&err, ret);
		lua_pushstring(L, err.message);
		dbus_error_free(&err);
		dbus_message_unref(ret);
		return 2;
	}

	dbus_message_unref(ret);

	lua_pushnil(L);
	lua_pushliteral(L, "Unknown reply");
	return 2;
}

/* this magic string representation of an incoming
 * signal must match the one in the Lua code */
#define push_signal_string(L, object, interface, signal) \
	lua_pushfstring(L, "%s\n%s\n%s", object, interface, signal)

static DBusHandlerResult signal_handler(DBusConnection *conn,
		DBusMessage *msg, lua_State *S)
{
	lua_State *T;

	if (msg == NULL || dbus_message_get_type(msg)
			!= DBUS_MESSAGE_TYPE_SIGNAL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	push_signal_string(S,
			dbus_message_get_path(msg),
			dbus_message_get_interface(msg),
			dbus_message_get_member(msg));
#ifdef DEBUG
	printf("received \"%s\"\n", lua_tostring(S, 2));
	fflush(stdout);
#endif
	lua_rawget(S, 1); /* signal handler table */
	if (lua_type(S, 2) != LUA_TFUNCTION) {
		lua_settop(S, 1);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	/* create new Lua thread */
	T = lua_newthread(S);
	lua_insert(S, 2);
	/* push nil to let whoever sees the end of this thread
	 * know that nothing further needs to be done */
	lua_pushnil(T);
	/* move the Lua signal handler there */
	lua_xmove(S, T, 1);

	switch (lua_resume(T, push_arguments(T, msg))) {
	case 0: /* thread finished */
	case LUA_YIELD:	/* thread yielded */
		/* just forget about it */
		lua_settop(S, 1);
		break;
	default: /* thread errored */
		lua_settop(S, 1);
		if (stop == 0) {
			/* move error message to main */
			lua_xmove(T, mainThread, 1);
			stop = -1;
		}
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}

/*
 * Bus:send_signal()
 *
 * argument 1: connection
 * argument 2: path
 * argument 3: interface
 * argument 4: name
 * argument 5: signature (optional)
 * ...
 */
static int bus_send_signal(lua_State *L)
{
	DBusConnection *conn = bus_check(L, 1)->conn;
	const char *path = luaL_checkstring(L, 2);
	const char *interface = luaL_checkstring(L, 3);
	const char *name = luaL_checkstring(L, 4);
	DBusMessage *msg;
	dbus_bool_t r;

	if (interface && *interface == '\0')
		interface = NULL;

	msg = dbus_message_new_signal(path, interface, name);
	if (msg == NULL) {
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}

	if (lua_isstring(L, 5)) {
		const char *signature = lua_tostring(L, 5);
		if (*signature &&
				add_arguments(L, 6, lua_gettop(L), signature, msg))
			return lua_error(L);
	}

	r = dbus_connection_send(conn, msg, NULL);
	dbus_message_unref(msg);

	if (r == FALSE) {
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}

	/* return true */
	lua_pushboolean(L, 1);
	return 1;
}

static int send_reply(lua_State *T)
{
	DBusConnection *conn = lua_touserdata(T, 2);
	DBusMessage *msg = lua_touserdata(T, 3);
	DBusMessage *reply;
	int top = lua_gettop(T);

	/* check if the method returned an error */
	if (top >= 6 && lua_isnil(T, 5)) {
		const char *name = lua_tostring(T, 6);
		const char *message = (top >= 7) ? lua_tostring(T, 7) : NULL;

		if (name == NULL) {
			dbus_message_unref(msg);
			lua_pushliteral(T, "Return #1 nil, "
					"expected error name as #2");
			return 1;
		}
		if (message && *message == '\0')
			message = NULL;

		reply = dbus_message_new_error(msg, name, message);
		dbus_message_unref(msg);
		if (reply == NULL) {
			lua_pushliteral(T, "Out of memory");
			return 1;
		}
	} else {
		const char *signature;

		reply = dbus_message_new_method_return(msg);
		dbus_message_unref(msg);
		if (reply == NULL) {
			lua_pushliteral(T, "Out of memory");
			return 1;
		}

		signature = lua_tostring(T, 4);
		if (signature && *signature &&
				add_arguments(T, 5, top, signature, reply)) {
			/* add_arguments() pushes its own error message */
			dbus_message_unref(reply);
			return 1;
		}
	}

	if (!dbus_connection_send(conn, reply, NULL)) {
		lua_pushliteral(T, "Out of memory");
		return 1;
	}

	dbus_message_unref(reply);

	return 0;
}

static DBusHandlerResult method_call_handler(DBusConnection *conn,
		DBusMessage *msg, lua_State *O)
{
	lua_State *T;

#ifdef DEBUG
	printf("Received message: path = %s,"
			" interface = %s, member = %s\n",
			dbus_message_get_path(msg),
			dbus_message_get_interface(msg),
			dbus_message_get_member(msg));
	fflush(stdout);
#endif

	lua_pushfstring(O, "%s.%s",
			dbus_message_get_interface(msg),
			dbus_message_get_member(msg));

	lua_rawget(O, 1);
	if (!lua_istable(O, 2)) {
		lua_settop(O, 1);
#ifdef DEBUG
		printf("..not handled\n"); fflush(stdout);
#endif
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	/* create a new thread to run the method in */
	T = lua_newthread(O);
	/* ..and insert it before the function table */
	lua_insert(O, 2);

	/* push the send_reply function */
	lua_pushcclosure(T, send_reply, 0);

	/* push the connection */
	lua_pushlightuserdata(T, conn);

	/* push the message */
	dbus_message_ref(msg);
	lua_pushlightuserdata(T, msg);

	/* move the return signature and the function to T */
	lua_rawgeti(O, 3, 2);
	lua_rawgeti(O, 3, 3);
	lua_xmove(O, T, 2);

	/* forget about the function table */
	lua_settop(O, 2);

	switch (lua_resume(T, push_arguments(T, msg))) {
	case 0: /* thread finished */
		if (send_reply(T) && stop == 0) {
			/* move error message to main thread and error */
			lua_xmove(T, mainThread, 1);
			stop = -1;
		}
	case LUA_YIELD:	/* thread yielded */
		/* forget about the thread */
		lua_settop(O, 1);
		break;
	default: /* thread errored */
		lua_settop(O, 1);
		if (stop == 0) {
			/* move error message to main */
			lua_xmove(T, mainThread, 1);
			stop = -1;
		}
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
	lua_State *O;

	luaL_checktype(L, 3, LUA_TTABLE);

	/* drop extra arguments */
	lua_settop(L, 3);

	/* get the signal/thread table of the conection */
	lua_getfenv(L, 1);
	/* ..and move it before the path */
	lua_insert(L, 2);

	/* check if we already registered this object path */
	lua_pushvalue(L, 3);
	lua_rawget(L, 2);
	if (lua_isthread(L, 5)) {
		/* just replace the method table */
		O = lua_tothread(L, 5);
		lua_xmove(L, O, 1);
		lua_replace(O, 2);
		/* return true */
		lua_pushboolean(L, 1);
		return 1;
	}
	/* clear result of lua_rawget() */
	lua_settop(L, 4);

	/* move method table before path */
	lua_insert(L, 3);

	/* create thread for storing object data */
	O = lua_newthread(L);
	if (O == NULL) {
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}

	/* register the object path */
	if (!dbus_connection_register_object_path(c->conn, path, &vtable, O)) {
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}

	/* save the thread in the thread table */
	lua_rawset(L, 2);

	/* move method table to the thread*/
	lua_xmove(L, O, 1);

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

	if (mainThread)
		return luaL_error(L, "Another main loop is already running");

	if (lua_isfunction(L, n))
		n--;

	if (n < 1)
		return luaL_error(L, "At least 1 DBus connection required");

	c = malloc(n * sizeof(LCon *));
	if (c == NULL) {
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}

	for (i = 0; i < n; i++) {
		/* don't use bus_check() so we can free c
		 * before erroring
		c[i] = bus_check(L, i+1);
		*/
		int r;

		if (lua_getmetatable(L, i+1) == 0) {
			free(c);
			return luaL_argerror(L, i+1,
					"expected a DBus connection");
		}

		r = lua_equal(L, lua_upvalueindex(1), -1);
		lua_pop(L, 1);
		if (r == 0) {
			free(c);
			return luaL_argerror(L, i+1,
					"expected a DBus connection");
		}

		c[i] = lua_touserdata(L, i+1);
	}

	fds = make_poll_struct(n, c, &nfds);
	if (fds == NULL) {
		free(c);
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}

	stop = 0;
	mainThread = L;

	/* read, write, dispatch until we get a break */
	while (1) {
		unsigned int watches_changed = dispatchall(n, c);
		int r;

		if (stop)
			goto exit;

		if (watches_changed) {
			free(fds);
			fds = make_poll_struct(n, c, &nfds);
			if (fds == NULL) {
				free(c);
				mainThread = NULL;
				lua_pushnil(L);
				lua_pushliteral(L, "Out of memory");
				return 2;
			}
		}
		r = poll(fds, nfds, 0);
		if (r < 0) {
			lua_pushnil(L);
			lua_pushfstring(L, "Error polling DBus: %s",
					strerror(errno));
			stop = 2;
			goto exit;
		}
		if (r == 0)
			break;

		handleall(n, c, fds);
	}

	/* if the last argument was a function,
	 * start it in a new thread */
	if (n < lua_gettop(L)) {
		lua_State *T = lua_newthread(L);
		lua_insert(L, n+1);
		lua_xmove(L, T, 1);

		switch (lua_resume(T, 0)) {
		case 0: /* thread finished */
		case LUA_YIELD:	/* thread yielded */
			/* forget about the thread */
			lua_settop(L, n);
			break;
		default: /* thread errored */
			/* move error message to main thread */
			lua_xmove(T, L, 1);
			stop = -1;
			goto exit;
		}
	}

	/* now run the real main loop */
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
				lua_pushnil(L);
				lua_pushliteral(L, "Out of memory");
				return 2;
			}
		}
		if (poll(fds, nfds, -1) < 0) {
			lua_pushnil(L);
			lua_pushfstring(L, "Error polling DBus: %s",
					strerror(errno));
			stop = 2;
			break;
		}
		handleall(n, c, fds);
	}

exit:
	free(c);
	free(fds);

	mainThread = NULL;

	if (stop < 0)
		return lua_error(L);

	return stop;
}

/*
 * stop()
 */
static int simpledbus_stop(lua_State *L)
{
	if (mainThread == NULL)
		return luaL_error(L, "Main loop not running");

	stop = lua_gettop(L);

	if (stop == 0) {
		lua_pushboolean(L, 1);
		stop = 1;
	}

	lua_checkstack(mainThread, stop);

	lua_xmove(L, mainThread, stop);

	return 0;
}

static int new_connection(lua_State *L, DBusConnection *conn)
{
	LCon *c;
	lua_State *S;

	if (dbus_error_is_set(&err)) {
		lua_pushnil(L);
		lua_pushstring(L, err.message);
		dbus_error_free(&err);
		return 2;
	}

	if (conn == NULL) {
		lua_pushnil(L);
		lua_pushliteral(L, "Couldn't create connection");
		return 2;
	}

	dbus_connection_set_exit_on_disconnect(conn, FALSE);

	lua_pushlightuserdata(L, conn);
	lua_rawget(L, lua_upvalueindex(2)); /* connection table */
	if (lua_type(L, -1) == LUA_TUSERDATA) {
		dbus_connection_unref(conn);
		return 1;
	} else
		lua_settop(L, 0);

	/* create new userdata for the bus */
	c = lua_newuserdata(L, sizeof(LCon));
	if (c == NULL) {
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}
	c->conn = conn;
	c->nactive = 0;
	c->active = NULL;

	/* set the metatable */
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_setmetatable(L, 1);

	/* create new environment table for
	 * signal handlers and running threads */
	lua_createtable(L, 2, 0);
	lua_pushvalue(L, 2);
	lua_setfenv(L, 1);

	/* create thread for signal handler */
	S = lua_newthread(L);
	if (S == NULL) {
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}
	/* ..and save it */
	lua_rawseti(L, 2, 1);

	/* create signal table */
	lua_newtable(L);
	/* ..save it */
	lua_pushvalue(L, 3);
	lua_rawseti(L, 2, 2);
	/* ..and move it to the thread */
	lua_xmove(L, S, 1);

	/* set watch functions */
	if (!dbus_connection_set_watch_functions(conn,
				(DBusAddWatchFunction)add_watch_cb,
				(DBusRemoveWatchFunction)remove_watch_cb,
				(DBusWatchToggledFunction)toggle_watch_cb,
				c, NULL)) {
		dbus_connection_unref(conn);
		lua_pushnil(L);
		lua_pushliteral(L, "Error setting watch functions");
		return 2;
	}

	/* set the signal handler */
	if (!dbus_connection_add_filter(conn,
				(DBusHandleMessageFunction)signal_handler,
				S, NULL)) {
		dbus_connection_unref(conn);
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}

	/* insert the connection in the connection table */
	lua_pushlightuserdata(L, conn);
	lua_pushvalue(L, 1);
	lua_rawset(L, lua_upvalueindex(2));

	/* return the connection */
	lua_settop(L, 1);
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
		{"get_signal_table", bus_get_signal_table},
		{"call_method", bus_call_method},
		{"send_signal", bus_send_signal},
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

	/* insert the Bus metatable */
	lua_setfield(L, 2, "Bus");

	/* make the Proxy metatable */
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
	set_dbus_string_constant(L, 2, INTERFACE_INTROSPECTABLE);
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
