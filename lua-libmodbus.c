/*

  lua-libmodbus.c - Lua bindings to libmodbus

  Copyright (c) 2016 Karl Palsson <karlp@remake.is>

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <modbus/modbus.h>

#include "compat.h"

/* unique naming for userdata metatables */
#define MODBUS_META_CTX	"modbus.ctx"

typedef struct {
	lua_State *L;
	modbus_t *modbus;
	size_t max_len;
} ctx_t;

/**
 * Push either "true" or "nil, errormessage"
 * @param L
 * @param rc rc from modbus_xxxx function call
 * @param expected what rc was meant to be
 * @return the count of stack elements pushed
 */
static int libmodbus_rc_to_nil_error(lua_State *L, int rc, int expected)
{
	if (rc == expected) {
		lua_pushboolean(L, true);
		return 1;
	} else {
		lua_pushnil(L);
		// TODO - insert the integer errno code here too?
		lua_pushstring(L, modbus_strerror(errno));
		return 2;
	}
}

/**
 * Returns the runtime linked version of libmodbus as a string
 * @param L
 * @return eg "3.0.6"
 */
static int libmodbus_version(lua_State *L)
{
	char version[16];

	snprintf(version, sizeof(version) - 1, "%i.%i.%i",
		libmodbus_version_major, libmodbus_version_minor, libmodbus_version_micro);
	lua_pushstring(L, version);
	return 1;
}

static int libmodbus_new_rtu(lua_State *L)
{
	const char *device = luaL_checkstring(L, 1);
	int baud = luaL_optnumber(L, 2, 19200);
	const char *parityin = luaL_optstring(L, 3, "EVEN");
	int databits = luaL_optnumber(L, 4, 8);
	int stopbits = luaL_optnumber(L, 5, 1);

	/* just accept baud as is */
	/* parity must be one of a few things... */
	char parity;
	switch (parityin[0]) {
	case 'e':
	case 'E':
		parity = 'E';
		break;
	case 'n':
	case 'N':
		parity = 'N';
		break;
	case 'o':
	case 'O':
		parity = 'O';
		break;
	default:
		return luaL_argerror(L, 3, "Unrecognised parity");
	}

	ctx_t *ctx = (ctx_t *) lua_newuserdata(L, sizeof(ctx_t));

	ctx->modbus = modbus_new_rtu(device, baud, parity, databits, stopbits);
	ctx->max_len = MODBUS_RTU_MAX_ADU_LENGTH;

	if (ctx->modbus == NULL) {
		return luaL_error(L, modbus_strerror(errno));
	}

	ctx->L = L;

	luaL_getmetatable(L, MODBUS_META_CTX);
	// Can I put more functions in for rtu here? maybe?
	lua_setmetatable(L, -2);

	return 1;
}


static int libmodbus_new_tcp_pi(lua_State *L)
{
	const char *host = luaL_checkstring(L, 1);
	const char *service = luaL_checkstring(L, 2);

	// Do we really need any of this? no callbacks in libmodbus?
	ctx_t *ctx = (ctx_t *) lua_newuserdata(L, sizeof(ctx_t));

	ctx->modbus = modbus_new_tcp_pi(host, service);
	ctx->max_len = MODBUS_TCP_MAX_ADU_LENGTH;

	if (ctx->modbus == NULL) {
		return luaL_error(L, modbus_strerror(errno));
	}

	ctx->L = L;

	luaL_getmetatable(L, MODBUS_META_CTX);
	// Can I put more functions in for tcp here? maybe?
	lua_setmetatable(L, -2);

	return 1;
}

static ctx_t * ctx_check(lua_State *L, int i)
{
	return (ctx_t *) luaL_checkudata(L, i, MODBUS_META_CTX);
}

static int ctx_destroy(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	modbus_free(ctx->modbus);

	/* remove all methods operating on ctx */
	lua_newtable(L);
	lua_setmetatable(L, -2);

	/* Nothing to return on stack */
	return 0;
}

static int ctx_connect(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);

	int rc = modbus_connect(ctx->modbus);
	
	return libmodbus_rc_to_nil_error(L, rc, 0);
}

static int ctx_set_debug(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	bool opt;
	if (lua_isnil(L, -1)) {
		opt = true;
	} else {
		opt = lua_toboolean(L, -1);
	}
	modbus_set_debug(ctx->modbus, opt);
	return 0;
}

static int ctx_set_error_recovery(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int opt = luaL_checkinteger(L, 2);
	int opt2 = luaL_optinteger(L, 3, 0);
	
	int rc = modbus_set_error_recovery(ctx->modbus, opt | opt2);
	
	return libmodbus_rc_to_nil_error(L, rc, 0);
}

static int ctx_set_byte_timeout(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int opt = luaL_checkinteger(L, 2);
	int opt2 = luaL_optinteger(L, 3, 0);

#if LIBMODBUS_VERSION_CHECK(3,1,0)
	modbus_set_byte_timeout(ctx->modbus, opt, opt2);
#else
	struct timeval t = { opt, opt2 };
	modbus_set_byte_timeout(ctx->modbus, &t);
#endif

	return 0;
}

static int ctx_get_byte_timeout(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	uint32_t opt1, opt2;
	
#if LIBMODBUS_VERSION_CHECK(3,1,0)
	modbus_get_byte_timeout(ctx->modbus, &opt1, &opt2);
#else
	struct timeval t;
	modbus_get_byte_timeout(ctx->modbus, &t);
	opt1 = t.tv_sec;
	opt2 = t.tv_usec;
#endif
	lua_pushnumber(L, opt1);
	lua_pushnumber(L, opt2);

	return 2;
}

static int ctx_set_response_timeout(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int opt = luaL_checkinteger(L, 2);
	int opt2 = luaL_optinteger(L, 3, 0);

#if LIBMODBUS_VERSION_CHECK(3,1,0)
	modbus_set_response_timeout(ctx->modbus, opt, opt2);
#else
	struct timeval t = { opt, opt2 };
	modbus_set_response_timeout(ctx->modbus, &t);
#endif

	return 0;
}

static int ctx_get_response_timeout(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	uint32_t opt1, opt2;
	
#if LIBMODBUS_VERSION_CHECK(3,1,0)
	modbus_get_response_timeout(ctx->modbus, &opt1, &opt2);
#else
	struct timeval t;
	modbus_get_response_timeout(ctx->modbus, &t);
	opt1 = t.tv_sec;
	opt2 = t.tv_usec;
#endif
	lua_pushnumber(L, opt1);
	lua_pushnumber(L, opt2);

	return 2;
}

static int ctx_get_socket(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);

	lua_pushinteger(L, modbus_get_socket(ctx->modbus));

	return 1;
}

static int ctx_set_socket(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int newfd = luaL_checknumber(L, 2);

	modbus_set_socket(ctx->modbus, newfd);

	return 0;
}

static int ctx_get_header_length(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);

	lua_pushinteger(L, modbus_get_header_length(ctx->modbus));

	return 1;
}

static int ctx_set_slave(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int slave = luaL_checknumber(L, 2);

	int rc = modbus_set_slave(ctx->modbus, slave);
	
	return libmodbus_rc_to_nil_error(L, rc, 0);
}

static int _ctx_read_bits(lua_State *L, bool input)
{
	ctx_t *ctx = ctx_check(L, 1);
	int addr = luaL_checknumber(L, 2);
	int count = luaL_checknumber(L, 3);
	int rcount = 0;
	int rc;

	if (count > MODBUS_MAX_READ_BITS) {
		return luaL_argerror(L, 3, "requested too many bits");
	}

	uint8_t *buf = malloc(count * sizeof(uint8_t));
	assert(buf);
	if (input) {
		rc = modbus_read_input_bits(ctx->modbus, addr, count, buf);
	} else {
		rc = modbus_read_bits(ctx->modbus, addr, count, buf);
	}

	if (rc == count) {
		lua_newtable(L);
		/* nota bene, lua style offsets! */
		for (int i = 1; i <= rc; i++) {
			lua_pushnumber(L, i);
			/* TODO - push number or push bool? what's a better lua api? */
			lua_pushnumber(L, buf[i-1]);
			lua_settable(L, -3);
		}
		rcount = 1;
	} else {
		rcount = libmodbus_rc_to_nil_error(L, rc, count);
	}

	free(buf);
	return rcount;
}

static int ctx_read_input_bits(lua_State *L)
{
	return _ctx_read_bits(L, true);
}

static int ctx_read_bits(lua_State *L)
{
	return _ctx_read_bits(L, false);
}

static int _ctx_read_regs(lua_State *L, bool input)
{
	ctx_t *ctx = ctx_check(L, 1);
	int addr = luaL_checknumber(L, 2);
	int count = luaL_checknumber(L, 3);
	int rcount = 0;
	int rc;
	
	if (count > MODBUS_MAX_READ_REGISTERS) {
		return luaL_argerror(L, 3, "requested too many registers");
	}
	
	// better malloc as much space as we need to return data here...
	uint16_t *buf = malloc(count * sizeof(uint16_t));
	assert(buf);
	if (input) {
		rc = modbus_read_input_registers(ctx->modbus, addr, count, buf);
	} else {
		rc = modbus_read_registers(ctx->modbus, addr, count, buf);
	}
	if (rc == count) {
		lua_newtable(L);
		/* nota bene, lua style offsets! */
		for (int i = 1; i <= rc; i++) {
			lua_pushnumber(L, i);
			lua_pushnumber(L, buf[i-1]);
			lua_settable(L, -3);
		}
		rcount = 1;
	} else {
		rcount = libmodbus_rc_to_nil_error(L, rc, count);
	}
	
	free(buf);
	return rcount;
}

static int ctx_read_input_registers(lua_State *L)
{
	return _ctx_read_regs(L, true);
}

static int ctx_read_registers(lua_State *L)
{
	return _ctx_read_regs(L, false);
}

static int ctx_report_slave_id(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);

	uint8_t *buf = malloc(ctx->max_len);
	assert(buf);
#if LIBMODBUS_VERSION_CHECK(3,1,0)
	int rc = modbus_report_slave_id(ctx->modbus, ctx->max_len, buf);
#else
	int rc = modbus_report_slave_id(ctx->modbus, buf);
#endif
	if (rc < 0) {
		return libmodbus_rc_to_nil_error(L, rc, 0);
	}

	lua_pushlstring(L, (char *)buf, rc);
	return 1;
}

static int ctx_write_bit(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int addr = luaL_checknumber(L, 2);
	int val;

	if (lua_type(L, 3) == LUA_TNUMBER) {
		val = lua_tonumber(L, 3);
	} else if (lua_type(L, 3) == LUA_TBOOLEAN) {
		val = lua_toboolean(L, 3);
	} else {
		return luaL_argerror(L, 3, "bit must be numeric or boolean");
	}

	int rc = modbus_write_bit(ctx->modbus, addr, val);

	return libmodbus_rc_to_nil_error(L, rc, 1);
}

static int ctx_write_register(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int addr = luaL_checknumber(L, 2);
	int val = luaL_checknumber(L, 3);

	int rc = modbus_write_register(ctx->modbus, addr, val);
	
	return libmodbus_rc_to_nil_error(L, rc, 1);
}


static int ctx_write_bits(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int addr = luaL_checknumber(L, 2);
	int rc;
	int rcount;

	/*
	 * TODO - could allow just a series of arguments too? easier for
	 * smaller sets?"?)
	 */
	luaL_checktype(L, 3, LUA_TTABLE);
	/* array style table only! */
	int count = lua_objlen(L, 3);

	if (count > MODBUS_MAX_WRITE_BITS) {
		return luaL_argerror(L, 3, "requested too many bits");
	}

	/* Convert table to uint8_t array */
	uint8_t *buf = malloc(count * sizeof(uint8_t));
	assert(buf);
	for (int i = 1; i <= count; i++) {
		bool ok = false;
		lua_rawgeti(L, 3, i);
		if (lua_type(L, -1) == LUA_TNUMBER) {
			buf[i-1] = lua_tonumber(L, -1);
			ok = true;
		}
		if (lua_type(L, -1) == LUA_TBOOLEAN) {
			buf[i-1] = lua_toboolean(L, -1);
			ok = true;
		}

		if (ok) {
			lua_pop(L, 1);
		} else {
			free(buf);
			return luaL_argerror(L, 3, "table values must be numeric or bool");
		}
	}
	rc = modbus_write_bits(ctx->modbus, addr, count, buf);
	if (rc == count) {
		rcount = 1;
		lua_pushboolean(L, true);
	} else {
		rcount = libmodbus_rc_to_nil_error(L, rc, count);
	}

	free(buf);
	return rcount;
}


static int ctx_write_registers(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int addr = luaL_checknumber(L, 2);
	int rc;
	int rcount;
	
	/*
	 * TODO - could allow just a series of arguments too? easier for
	 * smaller sets? (more compatible with "write_register"?)
	 */
	luaL_checktype(L, 3, LUA_TTABLE);
	/* array style table only! */
	int count = lua_objlen(L, 3);

	if (count > MODBUS_MAX_WRITE_REGISTERS) {
		return luaL_argerror(L, 3, "requested too many registers");
	}

	/* Convert table to uint16_t array */
	uint16_t *buf = malloc(count * sizeof(uint16_t));
	assert(buf);
	for (int i = 1; i <= count; i++) {
		lua_rawgeti(L, 3, i);
		/* user beware! we're not range checking your values */
		if (lua_type(L, -1) != LUA_TNUMBER) {
			free(buf);
			return luaL_argerror(L, 3, "table values must be numeric yo");
		}
		buf[i-1] = lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
	rc = modbus_write_registers(ctx->modbus, addr, count, buf);
	if (rc == count) {
		rcount = 1;
		lua_pushboolean(L, true);
	} else {
		rcount = libmodbus_rc_to_nil_error(L, rc, count);
	}
	
	free(buf);
	return rcount;
}

static int ctx_tcp_pi_listen(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int conns = luaL_optinteger(L, 2, 1);

	int sock = modbus_tcp_pi_listen(ctx->modbus, conns);
	if (sock == -1) {
		return libmodbus_rc_to_nil_error(L, 0, 1);
	}
	lua_pushnumber(L, sock);

	return 1;
}

static int ctx_tcp_pi_accept(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int sock = luaL_checknumber(L, 2);

	sock = modbus_tcp_pi_accept(ctx->modbus, &sock);
	if (sock == -1) {
		return libmodbus_rc_to_nil_error(L, 0, 1);
	}
	lua_pushnumber(L, sock);

	return 1;
}

static int ctx_receive(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int rcount;

	uint8_t *req = malloc(ctx->max_len);
	int rc = modbus_receive(ctx->modbus, req);
	if (rc > 0) {
		lua_pushnumber(L, rc);
		lua_pushlstring(L, (char *)req, rc);
		rcount = 2;
	} else if (rc == 0) {
		printf("Special case for rc = 0, can't remember\n");
		rcount = 0;
	} else {
		rcount = libmodbus_rc_to_nil_error(L, rc, 0);
	}
	return rcount;
}

static int ctx_reply(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	size_t req_len;
	const char *req = luaL_checklstring(L, 2, &req_len);

	luaL_checktype(L, 3, LUA_TTABLE);

	// FIXME - oh boy, probably need a whole lot of wrappers on the mappings?
	//modbus_reply(ctx->modbus, (uint8_t*)req, req_len, mapping);
	(void)ctx;
	(void)req;
	return luaL_error(L, "reply is simply unimplemented my friend!");
}

static int ctx_reply_exception(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	const char *req = luaL_checklstring(L, 2, NULL);
	int exception = luaL_checknumber(L, 3);

	int rc = modbus_reply_exception(ctx->modbus, (uint8_t*)req, exception);
	if (rc == -1) {
		return libmodbus_rc_to_nil_error(L, 0, 1);
	} else {
		return libmodbus_rc_to_nil_error(L, rc, rc);
	}
}


struct definei {
        const char* name;
        int value;
};

struct defines {
        const char* name;
        const char* value;
};

static const struct definei D[] = {
        {"RTU_RS232", MODBUS_RTU_RS232},
        {"RTU_RS485", MODBUS_RTU_RS485},
	{"TCP_SLAVE", MODBUS_TCP_SLAVE},
	{"BROADCAST_ADDRESS", MODBUS_BROADCAST_ADDRESS},
	{"ERROR_RECOVERY_NONE", MODBUS_ERROR_RECOVERY_NONE},
	{"ERROR_RECOVERY_LINK", MODBUS_ERROR_RECOVERY_LINK},
	{"ERROR_RECOVERY_PROTOCOL", MODBUS_ERROR_RECOVERY_PROTOCOL},
	{"EXCEPTION_ILLEGAL_FUNCTION", MODBUS_EXCEPTION_ILLEGAL_FUNCTION},
	{"EXCEPTION_ILLEGAL_DATA_ADDRESS", MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS},
	{"EXCEPTION_ILLEGAL_DATA_VALUE", MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE},
	{"EXCEPTION_SLAVE_OR_SERVER_FAILURE", MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE},
	{"EXCEPTION_ACKNOWLEDGE", MODBUS_EXCEPTION_ACKNOWLEDGE},
	{"EXCEPTION_SLAVE_OR_SERVER_BUSY", MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY},
	{"EXCEPTION_NEGATIVE_ACKNOWLEDGE", MODBUS_EXCEPTION_NEGATIVE_ACKNOWLEDGE},
	{"EXCEPTION_MEMORY_PARITY", MODBUS_EXCEPTION_MEMORY_PARITY},
	{"EXCEPTION_NOT_DEFINED", MODBUS_EXCEPTION_NOT_DEFINED},
	{"EXCEPTION_GATEWAY_PATH", MODBUS_EXCEPTION_GATEWAY_PATH},
	{"EXCEPTION_GATEWAY_TARGET", MODBUS_EXCEPTION_GATEWAY_TARGET},
        {NULL, 0}
};

static const struct defines S[] = {
	{"VERSION_STRING", LIBMODBUS_VERSION_STRING},
        {NULL, NULL}
};

static void modbus_register_defs(lua_State *L, const struct definei *D, const struct defines *S)
{
        while (D->name != NULL) {
                lua_pushinteger(L, D->value);
                lua_setfield(L, -2, D->name);
                D++;
        }
        while (S->name != NULL) {
                lua_pushstring(L, S->value);
                lua_setfield(L, -2, S->name);
                S++;
        }
}


static const struct luaL_Reg R[] = {
	{"new_rtu",	libmodbus_new_rtu},
	{"new_tcp_pi",	libmodbus_new_tcp_pi},
	{"version",	libmodbus_version},
	
	{NULL, NULL}
};

static const struct luaL_Reg ctx_M[] = {
	{"connect",		ctx_connect},
	{"destroy",		ctx_destroy},
	{"get_socket",		ctx_get_socket},
	{"get_byte_timeout",	ctx_get_byte_timeout},
	{"get_header_length",	ctx_get_header_length},
	{"get_response_timeout",ctx_get_response_timeout},
	{"read_bits",		ctx_read_bits},
	{"read_input_bits",	ctx_read_input_bits},
	{"read_input_registers",ctx_read_input_registers},
	{"read_registers",	ctx_read_registers},
	{"report_slave_id",	ctx_report_slave_id},
	{"set_debug",		ctx_set_debug},
	{"set_byte_timeout",	ctx_set_byte_timeout},
	{"set_error_recovery",	ctx_set_error_recovery},
	{"set_response_timeout",ctx_set_response_timeout},
	{"set_slave",		ctx_set_slave},
	{"set_socket",		ctx_set_socket},
	{"write_bit",		ctx_write_bit},
	{"write_bits",		ctx_write_bits},
	{"write_register",	ctx_write_register},
	{"write_registers",	ctx_write_registers},
	{"__gc",		ctx_destroy},
	
	// FIXME - should really add these funcs only to contexts with tcp_pi!
	{"tcp_pi_listen",	ctx_tcp_pi_listen},
	{"tcp_pi_accept",	ctx_tcp_pi_accept},

	{"receive",		ctx_receive},
	{"reply",		ctx_reply}, /* Totally busted */
	{"reply_exception",	ctx_reply_exception},

	{NULL, NULL}
};

int luaopen_libmodbus(lua_State *L)
{

#ifdef LUA_ENVIRONINDEX
	/* set private environment for this module */
	lua_newtable(L);
	lua_replace(L, LUA_ENVIRONINDEX);
#endif

	/* metatable.__index = metatable */
	luaL_newmetatable(L, MODBUS_META_CTX);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_setfuncs(L, ctx_M, 0);

	luaL_newlib(L, R);

	modbus_register_defs(L, D, S);

	return 1;
}
	