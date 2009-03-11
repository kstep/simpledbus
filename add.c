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

#ifndef ALLINONE
#define LUA_LIB
#include <lua.h>
#include <lauxlib.h>
#include <dbus/dbus.h>

#define EXPORT
#endif

typedef void (*addfunc)(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args);

static addfunc get_addfunc(DBusSignatureIter *type);

static void add_error(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	(void)luaL_error(L, "Adding type '%s' is not implemented yet",
			dbus_signature_iter_get_signature(type));
}

static void add_byte(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	unsigned char n;
	if (!lua_isnumber(L, index))
		(void)luaL_error(L, "Expected number");
	n = (unsigned char)lua_tonumber(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_BYTE, &n);
}

static void add_boolean(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	dbus_bool_t b;
	if (!lua_isboolean(L, index))
		(void)luaL_error(L, "Expected boolean");
	b = lua_toboolean(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_BOOLEAN, &b);
}

static void add_int16(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	dbus_int16_t n;
	if (!lua_isnumber(L, index))
		(void)luaL_error(L, "Expected number");
	n = (dbus_int16_t)lua_tonumber(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_INT16, &n);
}

static void add_uint16(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	dbus_uint16_t n;
	if (!lua_isnumber(L, index))
		(void)luaL_error(L, "Expected number");
	n = (dbus_uint16_t)lua_tonumber(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_UINT16, &n);
}

static void add_int32(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	dbus_int32_t n;
	if (!lua_isnumber(L, index))
		(void)luaL_error(L, "Expected number");
	n = (dbus_int32_t)lua_tonumber(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_INT32, &n);
}

static void add_uint32(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	dbus_uint32_t n;
	if (!lua_isnumber(L, index))
		(void)luaL_error(L, "Expected number");
	n = (dbus_uint32_t)lua_tonumber(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_UINT32, &n);
}

static void add_string(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	const char *s;
	if (!lua_isstring(L, index))
		(void)luaL_error(L, "Expected string");
	s = lua_tostring(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_STRING, &s);
}

static void add_object_path(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	const char *s;
	if (!lua_isstring(L, index))
		(void)luaL_error(L, "Expected string");
	s = lua_tostring(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_OBJECT_PATH, &s);
}

static void add_array(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	DBusSignatureIter array_type;
	DBusMessageIter array_args;
	char *signature;
	addfunc af;
	int i;

	if (!lua_istable(L, index))
		(void)luaL_error(L, "Expected table (array)");

	dbus_signature_iter_recurse(type, &array_type);

	signature = dbus_signature_iter_get_signature(&array_type);

	dbus_message_iter_open_container(args, DBUS_TYPE_ARRAY,
			signature, &array_args);

	af = get_addfunc(&array_type);

	i = 1;
	while (1) {
		lua_rawgeti(L, index, i);
		if (lua_isnil(L, -1))
			break;

		af(L, -1, &array_type, &array_args);

		lua_pop(L, 1);

		i++;
	}

	lua_pop(L, 1);

	dbus_free(signature);

	dbus_message_iter_close_container(args, &array_args);
}

static addfunc get_addfunc(DBusSignatureIter *type)
{
	switch (dbus_signature_iter_get_current_type(type)) {
	case DBUS_TYPE_BOOLEAN:
		return add_boolean;
	case DBUS_TYPE_BYTE:
		return add_byte;
	case DBUS_TYPE_INT16:
		return add_int16;
	case DBUS_TYPE_UINT16:
		return add_uint16;
	case DBUS_TYPE_INT32:
		return add_int32;
	case DBUS_TYPE_UINT32:
		return add_uint32;
	case DBUS_TYPE_STRING:
		return add_string;
	case DBUS_TYPE_OBJECT_PATH:
		return add_object_path;
	case DBUS_TYPE_ARRAY:
		return add_array;
	}

	return add_error;
}

EXPORT void add_arguments(lua_State *L, int i, int argc, const char *signature,
		DBusMessage *msg)
{
	DBusMessageIter args;
	DBusSignatureIter type;

	dbus_message_iter_init_append(msg, &args);
	dbus_signature_iter_init(&type, signature);

	do {
		if (i > argc)
			(void)luaL_error(L, "Too few arguments");

		(get_addfunc(&type))(L, i, &type, &args);

		i++;
	} while (dbus_signature_iter_next(&type));
}
