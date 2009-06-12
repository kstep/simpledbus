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

enum add_return {
	ADD_OK = 0,
	ADD_ERROR
};

typedef enum add_return (*add_function)(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args);

static add_function get_addfunc(DBusSignatureIter *type);

static enum add_return add_error(lua_State *L, int index, int expected)
{
	lua_pushfstring(L, "(%s expected, got %s)",
			lua_typename(L, expected),
			lua_typename(L, lua_type(L, index)));

	return ADD_ERROR;
}

static enum add_return add_not_implemented(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	lua_pushfstring(L, "(adding type '%s' not implemented yet)",
			dbus_signature_iter_get_signature(type));

	return ADD_ERROR;
}

static enum add_return add_byte(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	unsigned char n;
	if (!lua_isnumber(L, index))
		return add_error(L, index, LUA_TNUMBER);
	n = (unsigned char)lua_tonumber(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_BYTE, &n);
	return ADD_OK;
}

static enum add_return add_boolean(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	dbus_bool_t b;
	if (!lua_isboolean(L, index))
		return add_error(L, index, LUA_TBOOLEAN);
	b = lua_toboolean(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_BOOLEAN, &b);
	return ADD_OK;
}

static enum add_return add_int16(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	dbus_int16_t n;
	if (!lua_isnumber(L, index))
		return add_error(L, index, LUA_TNUMBER);
	n = (dbus_int16_t)lua_tonumber(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_INT16, &n);
	return ADD_OK;
}

static enum add_return add_uint16(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	dbus_uint16_t n;
	if (!lua_isnumber(L, index))
		return add_error(L, index, LUA_TNUMBER);
	n = (dbus_uint16_t)lua_tonumber(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_UINT16, &n);
	return ADD_OK;
}

static enum add_return add_int32(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	dbus_int32_t n;
	if (!lua_isnumber(L, index))
		return add_error(L, index, LUA_TNUMBER);
	n = (dbus_int32_t)lua_tonumber(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_INT32, &n);
	return ADD_OK;
}

static enum add_return add_uint32(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	dbus_uint32_t n;
	if (!lua_isnumber(L, index))
		return add_error(L, index, LUA_TNUMBER);
	n = (dbus_uint32_t)lua_tonumber(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_UINT32, &n);
	return ADD_OK;
}

static enum add_return add_string(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	const char *s;
	if (!lua_isstring(L, index))
		return add_error(L, index, LUA_TSTRING);
	s = lua_tostring(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_STRING, &s);
	return ADD_OK;
}

static enum add_return add_object_path(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	const char *s;
	if (!lua_isstring(L, index))
		return add_error(L, index, LUA_TSTRING);
	s = lua_tostring(L, index);
	dbus_message_iter_append_basic(args, DBUS_TYPE_OBJECT_PATH, &s);
	return ADD_OK;
}

static enum add_return add_array(lua_State *L, int index,
		DBusSignatureIter *type, DBusMessageIter *args)
{
	DBusSignatureIter array_type;
	DBusMessageIter array_args;
	char *signature;
	add_function af;
	int i;

	if (!lua_istable(L, index))
		return add_error(L, index, LUA_TTABLE);

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

		if (af(L, -1, &array_type, &array_args) != ADD_OK) {
			lua_insert(L, -2);
			lua_pop(L, 1);
			return ADD_ERROR;
		}

		lua_pop(L, 1);

		i++;
	}

	lua_pop(L, 1);

	dbus_free(signature);

	dbus_message_iter_close_container(args, &array_args);

	return ADD_OK;
}

static add_function get_addfunc(DBusSignatureIter *type)
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

	return add_not_implemented;
}

EXPORT unsigned int add_arguments(lua_State *L, int start, int argc,
		const char *signature, DBusMessage *msg)
{
	DBusMessageIter args;
	DBusSignatureIter type;
	int i = start;

	dbus_message_iter_init_append(msg, &args);
	dbus_signature_iter_init(&type, signature);

	do {
		if (i > argc) {
			lua_pushfstring(L, "type error adding value #%d "
					"of '%s' (too few arguments)",
					i - start + 1, signature);
			return 1;
		}

		if ((get_addfunc(&type))(L, i, &type, &args) != ADD_OK) {
			lua_pushfstring(L, "type error adding value #%d of '%s' ",
					i - start + 1, signature);
			lua_insert(L, -2);
			lua_concat(L, 2);
			return 1;
		}

		i++;
	} while (dbus_signature_iter_next(&type));

	return 0;
}
