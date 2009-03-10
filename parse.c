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
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <expat.h>

#define EXPORT
#endif

/*
 * Maximum length of a signature string, see
 * http://dbus.freedesktop.org/doc/dbus-specification.html
 */
#define SIG_MAXLENGTH 256

struct parsedata {
	lua_State *L;
	unsigned int level;
	unsigned int interface;
	enum {
		TAG_NONE   = 0,
		TAG_METHOD = 1,
		TAG_SIGNAL = 2
	} type;
	char signature[SIG_MAXLENGTH];
	char *sig_next;
	char result[SIG_MAXLENGTH];
	char *res_next;
};

static void start_element_handler(struct parsedata *data,
		const XML_Char *name,
		const XML_Char **atts)
{
	data->level++;

	switch (data->level) {
	case 2:
		if (strcmp(name, "interface"))
			return;

		if (!*atts)
			return;
		while (strcmp(*atts, "name")) {
			atts += 2;
			if (!*atts)
				return;
		}

		/* push the interface name */
		atts++;
		lua_pushstring(data->L, *atts);

		data->interface = 1;
		break;
	case 3:
		if (!data->interface)
			return;
		if (!strcmp(name, "method"))
			data->type = TAG_METHOD;
		else if (!strcmp(name, "signal"))
			data->type = TAG_SIGNAL;
		else
			return;

		if (!*atts)
			return;
		while (strcmp(*atts, "name")) {
			atts += 2;
			if (!*atts)
				return;
		}

		/* push the method name */
		atts++;
		lua_pushstring(data->L, *atts);

		/* check if the field is already set */
		lua_pushvalue(data->L, 5);
		lua_gettable(data->L, 1);
		if (!lua_isnil(data->L, 6)) {
			/* if it is, don't add this method/signal */
			lua_settop(data->L, 4);
			data->type = TAG_NONE;
			return;
		}
		lua_settop(data->L, 5);

		/* create a new method/signal table */
		lua_createtable(data->L, 0, 4);

		/* ..and set the metatable */
		lua_pushvalue(data->L, lua_upvalueindex(data->type));
		lua_setmetatable(data->L, 6);

		break;
	case 4:
		if (data->type == TAG_NONE || strcmp(name, "arg"))
			return;

		{
			unsigned int out = 0;
			const char *type = NULL;

			while (*atts) {
				if (!strcmp(*atts, "type")) {
					atts++;
					type = *atts;
					atts++;
				} else if (!strcmp(*atts, "direction")) {
					atts++;
					if (strcmp(*atts, "in"))
						out = 1;
					atts++;
				} else
					atts += 2;
			}

			if (!type)
				return;

			if (out) {
				while (*type)
					*data->res_next++ = *type++;
			} else {
				while (*type)
					*data->sig_next++ = *type++;
			}
		}
	}
}

static void end_element_handler(struct parsedata *data,
		const XML_Char *name)
{
	data->level--;

	switch (data->level) {
	case 1:
		if (!data->interface || strcmp(name, "interface"))
			return;

		lua_settop(data->L, 3);

		data->interface = 0;
		break;
	case 2:
		if (data->type == TAG_NONE)
			return;

		*data->sig_next = *data->res_next = '\0';

		lua_pushvalue(data->L, 5); /* method/signal name */
		lua_setfield(data->L, 6, "name");
		lua_pushvalue(data->L, 4); /* interface */
		lua_setfield(data->L, 6, "interface");
		lua_pushlstring(data->L, data->signature,
				data->sig_next - data->signature);
		lua_setfield(data->L, 6, "signature");

		switch (data->type) {
		case TAG_METHOD:
			lua_pushlstring(data->L, data->result,
					data->res_next - data->result);
			lua_setfield(data->L, 6, "result");
			break;
		/* case TAG_SIGNAL:, but make gcc -Wall happy */
		default:
			lua_pushvalue(data->L, 3); /* object name */
			lua_setfield(data->L, 6, "object");
			break;
		}

		lua_settable(data->L, 1);
		data->res_next = data->result;
		data->sig_next = data->signature;
		data->type = TAG_NONE;
	}
}

/*
 * Proxy:parse()
 *
 * upvalue 1: Method
 * upvalue 2: Signal
 *
 * argument 1: proxy
 * argument 2: xml string
 */
EXPORT int proxy_parse(lua_State *L)
{
	XML_Parser p;
	struct parsedata data;
	const char *xml;

	/* drop extra arguments */
	lua_settop(L, 2);

	/* get the xml string */
	xml = luaL_checkstring(L, 2);

	/* put the object name on the stack */
	lua_getfield(L, 1, "object");
	if (lua_isnil(L, 3))
		return luaL_argerror(L, 2, "no object set in the proxy");

	/* create parser and initialise it */
	p = XML_ParserCreate("UTF-8");
	if (!p) {
		lua_pushnil(L);
		lua_pushliteral(L, "Out of memory");
		return 2;
	}

	data.L = L;
	data.level = 0;
	data.interface = 0;
	data.type = 0;
	*data.signature = '\0';
	*data.result = '\0';
	data.sig_next = data.signature;
	data.res_next = data.result;

	XML_SetUserData(p, &data);
	XML_SetElementHandler(p,
			(XML_StartElementHandler)start_element_handler,
			(XML_EndElementHandler)end_element_handler);

	/* now parse the xml document inserting methods as we go */
	if (!XML_Parse(p, xml, strlen(xml), 1)) {
#ifdef DEBUG
		fprintf(stderr, "Parse error at line %d:\n%s\n",
				(int)XML_GetCurrentLineNumber(p),
				XML_ErrorString(XML_GetErrorCode(p)));
#endif
		lua_pushnil(L);
		lua_pushliteral(L, "Error parsing introspection data");
		return 2;
	}

	/* free the parser */
	XML_ParserFree(p);

	/* return true */
	lua_pushboolean(L, 1);
	return 1;
}
