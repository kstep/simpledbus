/*
 * SimpleDBus - Simple DBus bindings for Lua
 * Copyright (C) 2008 Emil Renner Berthing <esmil@mailme.dk>
 *
 * SimpleDBus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleDBus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with SimpleDBus. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ALLINONE
#define LUA_LIB
#include <lua.h>
#include <dbus/dbus.h>

#define EXPORT
#endif

typedef void (*pushfunc)(lua_State *L, DBusMessageIter *args);

static pushfunc get_pushfunc(DBusMessageIter *args);

static void push_byte(lua_State *L, DBusMessageIter *args)
{
	unsigned char n;
	dbus_message_iter_get_basic(args, &n);
	lua_pushnumber(L, (lua_Number) n);
}

static void push_boolean(lua_State *L, DBusMessageIter *args)
{
	int b;
	dbus_message_iter_get_basic(args, &b);
	lua_pushboolean(L, b);
}

static void push_int16(lua_State *L, DBusMessageIter *args)
{
	dbus_int16_t n;
	dbus_message_iter_get_basic(args, &n);
	lua_pushnumber(L, (lua_Number) n);
}

static void push_uint16(lua_State *L, DBusMessageIter *args)
{
	dbus_uint16_t n;
	dbus_message_iter_get_basic(args, &n);
	lua_pushnumber(L, (lua_Number) n);
}

static void push_int32(lua_State *L, DBusMessageIter *args)
{
	dbus_int32_t n;
	dbus_message_iter_get_basic(args, &n);
	lua_pushnumber(L, (lua_Number) n);
}

static void push_uint32(lua_State *L, DBusMessageIter *args)
{
	dbus_uint32_t n;
	dbus_message_iter_get_basic(args, &n);
	lua_pushnumber(L, (lua_Number) n);
}

static void push_int64(lua_State *L, DBusMessageIter *args)
{
	dbus_int64_t n;
	dbus_message_iter_get_basic(args, &n);
	lua_pushnumber(L, (lua_Number) n);
}

static void push_uint64(lua_State *L, DBusMessageIter *args)
{
	dbus_uint64_t n;
	dbus_message_iter_get_basic(args, &n);
	lua_pushnumber(L, (lua_Number) n);
}

static void push_double(lua_State *L, DBusMessageIter *args)
{
	double d;
	dbus_message_iter_get_basic(args, &d);
	lua_pushnumber(L, (lua_Number) d);
}

static void push_string(lua_State *L, DBusMessageIter *args)
{
	char *s;
	dbus_message_iter_get_basic(args, &s);
	lua_pushstring(L, s);
}

static void push_variant(lua_State *L, DBusMessageIter *args)
{
	DBusMessageIter variant;
	dbus_message_iter_recurse(args, &variant);

	get_pushfunc(&variant)(L, &variant);
}

static void push_dict(lua_State *L, DBusMessageIter *args)
{
	DBusMessageIter array_args;
	DBusMessageIter dict_args;
	pushfunc kf;
	pushfunc vf;

	dbus_message_iter_recurse(args, &array_args);

	/* check if dictionary is empty */
	if (dbus_message_iter_get_arg_type(&array_args) != DBUS_TYPE_DICT_ENTRY)
		return;

	dbus_message_iter_recurse(&array_args, &dict_args);

	/* get push functions for key and value and push first entry */
	kf = get_pushfunc(&dict_args);
	if (!kf)
		return;

	kf(L, &dict_args);

	dbus_message_iter_next(&dict_args);

	vf = get_pushfunc(&dict_args);
	if (!vf) {
		lua_pop(L, 1);
		return;
	}

	vf(L, &dict_args);

	lua_rawset(L, -3);

	/* now push the rest */
	while (dbus_message_iter_next(&array_args)) {
		dbus_message_iter_recurse(&array_args, &dict_args);
		kf(L, &dict_args);
		dbus_message_iter_next(&dict_args);
		vf(L, &dict_args);
		lua_rawset(L, -3);
	}
}

static void push_array(lua_State *L, DBusMessageIter *args)
{
	DBusMessageIter array_args;
	pushfunc pf;
	unsigned int i;

	lua_newtable(L);

	if (dbus_message_iter_get_element_type(args) ==
			DBUS_TYPE_DICT_ENTRY) {
		push_dict(L, args);
		return;
	}

	dbus_message_iter_recurse(args, &array_args);

	pf = get_pushfunc(&array_args);
	if (!pf)
		return;

	i = 0;
	do {
		i++;
		pf(L, &array_args);
		lua_rawseti(L, -2, i);
	} while (dbus_message_iter_next(&array_args));
}

static void push_struct(lua_State *L, DBusMessageIter *args)
{
	DBusMessageIter struct_args;
	unsigned int i;

	lua_newtable(L);

	dbus_message_iter_recurse(args, &struct_args);

	i = 0;
	do {
		i++;
		(get_pushfunc(&struct_args))(L, &struct_args);
		lua_rawseti(L, -2, i);
	} while (dbus_message_iter_next(&struct_args));
}

static pushfunc get_pushfunc(DBusMessageIter *args)
{
	switch (dbus_message_iter_get_arg_type(args)) {
	case DBUS_TYPE_BYTE:
		return push_byte;
	case DBUS_TYPE_BOOLEAN:
		return push_boolean;
	case DBUS_TYPE_INT16:
		return push_int16;
	case DBUS_TYPE_UINT16:
		return push_uint16;
	case DBUS_TYPE_INT32:
		return push_int32;
	case DBUS_TYPE_UINT32:
		return push_uint32;
	case DBUS_TYPE_INT64:
		return push_int64;
	case DBUS_TYPE_UINT64:
		return push_uint64;
	case DBUS_TYPE_DOUBLE:
		return push_double;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
	case DBUS_TYPE_SIGNATURE:
		return push_string;
	case DBUS_TYPE_ARRAY:
		return push_array;
	case DBUS_TYPE_STRUCT:
		return push_struct;
	case DBUS_TYPE_VARIANT:
		return push_variant;
	}

	return NULL;
}

EXPORT int push_arguments(lua_State *L, DBusMessage *msg)
{
	DBusMessageIter args;
	unsigned int argc = 0;

	if (!dbus_message_iter_init(msg, &args))
		return 0;

	do {
		argc++;
		(get_pushfunc(&args))(L, &args);
	} while (dbus_message_iter_next(&args));

	return argc;
}
