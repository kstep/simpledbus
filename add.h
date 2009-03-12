#ifndef _ADD_H
#define _ADD_H

unsigned int add_arguments(lua_State *L, int start, int argc,
		const char *signature, DBusMessage *msg);

#endif
