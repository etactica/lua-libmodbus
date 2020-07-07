/*** lua bindings to libmodbus

@module libmodbus
@author Karl Palsson <karlp@etactica.com> 2016-2020

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

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/time.h>

#if defined(WIN32)
#include <winsock2.h>
#endif

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

	/* only used for making tostring */
	char *dev_host;
	char *service;
	int baud;
	char parity;
	int databits;
	int stopbits;
} ctx_t;

/*
 * Pushes either "true" or "nil, errormessage"
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
 * Returns the runtime linked version of libmodbus as a string.
 * The compile time version is available as a constant VERSION.
 * @function version
 * @return eg "3.0.6"
 * @see other_constants
 */
static int libmodbus_version(lua_State *L)
{
	char version[16];

	snprintf(version, sizeof(version) - 1, "%i.%i.%i",
		libmodbus_version_major, libmodbus_version_minor, libmodbus_version_micro);
	lua_pushstring(L, version);
	return 1;
}

/**
 * Create a Modbus/RTU context
 * @function new_rtu
 * @param device (required)
 * @param baud rate, defaults to 19200
 * @param parity defaults to EVEN
 * @param databits defaults to 8
 * @param stopbits defaults to 1
 * @return a modbus context ready for use
 */
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

	/* save data for nice string representations */
	ctx->baud = baud;
	ctx->databits = databits;
	ctx->dev_host = strdup(device);
	ctx->parity = parity;
	ctx->stopbits = stopbits;

	/* Make sure unused fields are zeroed */
	ctx->service = NULL;

	luaL_getmetatable(L, MODBUS_META_CTX);
	// Can I put more functions in for rtu here? maybe?
	lua_setmetatable(L, -2);

	return 1;
}


/**
 * Create a Modbus/TCP context
 * @function new_tcp_pi
 * @param host eg "192.168.1.100" or "modbus.example.org"
 * @param service eg "502" or "mbap"
 * @return a modbus context ready for use
 */
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

	/* save data for nice string representations */
	ctx->dev_host = strdup(host);
	ctx->service = strdup(service);
	ctx->databits = 0;

	luaL_getmetatable(L, MODBUS_META_CTX);
	// Can I put more functions in for tcp here? maybe?
	lua_setmetatable(L, -2);

	return 1;
}

/** Write a 32bit (u)int to 2x16bit registers
 * @function set_s32
 * @param num 32bit number
 * @return reg1 upper 16bits
 * @return reg2 lower 16bits
 */
static int helper_set_s32(lua_State *L)
{
	/* truncate as necessary */
	const uint32_t toval = (uint32_t)luaL_checknumber(L, 1);
	lua_pushinteger(L, toval >> 16);
	lua_pushinteger(L, toval & 0xffff);
	return 2;
}

/** Write a bit float to 2x16bit registers
 * @function set_f32
 * @param num floating point number
 * @return reg1 upper 16bits of a 32bit float
 * @return reg2 lower 16bits of a 32bit float
 */
static int helper_set_f32(lua_State *L)
{
	/* truncate to float if necessary */
	const float in = (float)luaL_checknumber(L, 1);
	uint32_t out;
	memcpy(&out, &in, sizeof(out));
	lua_pushinteger(L, out >> 16);
	lua_pushinteger(L, out & 0xffff);
	return 2;
}


/**
 * 16bit register as number to signed 16bit
 * @function get_s16
 * @param 1 16bit register
 * @return signed 16bit number
 */
static int helper_get_s16(lua_State *L)
{
	const int16_t in = luaL_checknumber(L, 1);
	lua_pushinteger(L, in);
	return 1;
}

/**
 * 2x16bit registers as number to signed 32 bit
 * @function get_s32
 * @param 1 16bit register
 * @param 2 16bit register
 * @return 32bit number
 */
static int helper_get_s32(lua_State *L)
{
	const uint16_t in1 = luaL_checknumber(L, 1);
	const uint16_t in2 = luaL_checknumber(L, 2);
	int32_t out = in1 << 16 | in2;
	lua_pushinteger(L, out);
	return 1;
}

/**
 * 2x16bit registers as number to signed 32 bit (reverse order)
 * @function get_s32le
 * @param 1 16bit register
 * @param 2 16bit register
 * @return 32bit number
 */
static int helper_get_s32le(lua_State *L)
{
	const uint16_t in2 = luaL_checknumber(L, 1);
	const uint16_t in1 = luaL_checknumber(L, 2);
	int32_t out = in1 << 16 | in2;
	lua_pushinteger(L, out);
	return 1;
}


/**
 * 2x16bit registers as number to unsigned 32 bit
 * @function get_u32
 * @param 1 16bit register
 * @param 2 16bit register
 * @return 32bit number
 */
static int helper_get_u32(lua_State *L)
{
	const uint16_t in1 = luaL_checknumber(L, 1);
	const uint16_t in2 = luaL_checknumber(L, 2);
	uint32_t out = in1 << 16 | in2;
	lua_pushnumber(L, out);
	return 1;
}

/**
 * 2x16bit registers as number to unsigned 32 bit (reversed order)
 * @function get_u32le
 * @param 1 16bit register
 * @param 2 16bit register
 * @return 32bit number
 */
static int helper_get_u32le(lua_State *L)
{
	const uint16_t in2 = luaL_checknumber(L, 1);
	const uint16_t in1 = luaL_checknumber(L, 2);
	uint32_t out = in1 << 16 | in2;
	lua_pushnumber(L, out);
	return 1;
}

/**
 * 2x16bit registers as number to 32 bit float
 * @function get_f32
 * @param 1 16bit register
 * @param 2 16bit register
 * @return 32bit float
 */
static int helper_get_f32(lua_State *L)
{
	const uint16_t in1 = luaL_checknumber(L, 1);
	const uint16_t in2 = luaL_checknumber(L, 2);

	uint32_t inval = in1<<16 | in2;
	float f;
	memcpy(&f, &inval, sizeof(f));

	lua_pushnumber(L, f);
	return 1;
}

/**
 * 2x16bit registers as number to 32 bit float (Reversed order)
 * @function get_f32le
 * @param 1 16bit register
 * @param 2 16bit register
 * @return 32bit float
 */
static int helper_get_f32le(lua_State *L)
{
	const uint16_t in2 = luaL_checknumber(L, 1);
	const uint16_t in1 = luaL_checknumber(L, 2);

	uint32_t inval = in1<<16 | in2;
	float f;
	memcpy(&f, &inval, sizeof(f));

	lua_pushnumber(L, f);
	return 1;
}

/**
 * 4x16bit registers as number to signed 64 bit
 * @function get_s64
 * @param 1 16bit register
 * @param 2 16bit register
 * @param 3 16bit register
 * @param 4 16bit register
 * @return 64bit number
 */
static int helper_get_s64(lua_State *L)
{
	const uint16_t in1 = luaL_checknumber(L, 1);
	const uint16_t in2 = luaL_checknumber(L, 2);
	const uint16_t in3 = luaL_checknumber(L, 3);
	const uint16_t in4 = luaL_checknumber(L, 4);
	int64_t out = in1;
	out = out << 16 | in2;
	out = out << 16 | in3;
	out = out << 16 | in4;
	lua_pushnumber(L, out);
	return 1;
}

/**
 * 4x16bit registers as number to unsigned 64 bit
 * @function get_u64
 * @param 1 16bit register
 * @param 2 16bit register
 * @param 3 16bit register
 * @param 4 16bit register
 * @return 64bit number
 */
static int helper_get_u64(lua_State *L)
{
	const uint16_t in1 = luaL_checknumber(L, 1);
	const uint16_t in2 = luaL_checknumber(L, 2);
	const uint16_t in3 = luaL_checknumber(L, 3);
	const uint16_t in4 = luaL_checknumber(L, 4);
	uint64_t out = (uint64_t)in1 << 48 | (uint64_t)in2 << 32 | (uint64_t)in3 << 16 | in4;
	lua_pushnumber(L, out);
	return 1;
}


static ctx_t * ctx_check(lua_State *L, int i)
{
	return (ctx_t *) luaL_checkudata(L, i, MODBUS_META_CTX);
}

static int ctx_destroy(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	modbus_close(ctx->modbus);
	modbus_free(ctx->modbus);
	if (ctx->dev_host) {
		free(ctx->dev_host);
	}
	if (ctx->service) {
		free(ctx->service);
	}

	/* remove all methods operating on ctx */
	lua_newtable(L);
	lua_setmetatable(L, -2);

	/* Nothing to return on stack */
	return 0;
}

static int ctx_tostring(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);

	if (ctx->databits) {
		lua_pushfstring(L, "ModbusRTU<%s@%d/%c%d>", ctx->dev_host, ctx->databits, ctx->parity, ctx->stopbits);
	} else {
		lua_pushfstring(L, "ModbusTCP<%s@%s>", ctx->dev_host, ctx->service);
	}
	return 1;
}

/** Context Methods.
 * These functions are members of a modbus context, from either new_rtu() or new_tcp_pi()
 * @section context_methods
 */

/**
 * Connect, see modbus_connect()
 * @function ctx:connect
 */
static int ctx_connect(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);

	int rc = modbus_connect(ctx->modbus);
	
	return libmodbus_rc_to_nil_error(L, rc, 0);
}

/**
 * Close, see modbus_close()
 * @function ctx:close
 */
static int ctx_close(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);

	modbus_close(ctx->modbus);

	return 0;
}

/**
 * Set debug
 * @function ctx:set_debug
 * @param enable optional bool, defaults to true
 */
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

/**
 * Set error recovery, see modbus_set_error_recovery.
 * The arguments will be or'd together.
 * @function ctx:set_error_recovery
 * @param a one of @{error_recovery_constants}
 * @param b one of @{error_recovery_constants}
 */
static int ctx_set_error_recovery(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int opt = luaL_checkinteger(L, 2);
	int opt2 = luaL_optinteger(L, 3, 0);
	
	int rc = modbus_set_error_recovery(ctx->modbus, opt | opt2);
	
	return libmodbus_rc_to_nil_error(L, rc, 0);
}

/**
 * See also modbus_set_byte_timeout
 * @function ctx:set_byte_timeout
 * @param seconds (required)
 * @param microseconds (optional, defaults to 0)
 */
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

/**
 * @function ctx:get_byte_timeout
 * @return[1] seconds
 * @return[1] microseconds
 */
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

/**
 * @function ctx:set_response_timeout
 * @param seconds (required)
 * @param microseconds (optional, defaults to 0)
 */
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

/**
 * @function ctx:get_response_timeout
 * @return[1] seconds
 * @return[1] microseconds
 */
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

/**
 * @function ctx:get_socket
 * @return the socket number
 */
static int ctx_get_socket(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);

	lua_pushinteger(L, modbus_get_socket(ctx->modbus));

	return 1;
}

/**
 * @function ctx:set_socket
 * @param sock integer socket number to set
 */
static int ctx_set_socket(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int newfd = luaL_checknumber(L, 2);

	modbus_set_socket(ctx->modbus, newfd);

	return 0;
}

/**
 * @function ctx:rtu_get_serial_mode
 * @return @{rtu_constants} the serial mode, either RTU_RS232 or RTU_RS485
 */
static int ctx_rtu_get_serial_mode(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);

	lua_pushinteger(L, modbus_rtu_get_serial_mode(ctx->modbus));

	return 1;
}

/**
 * Sets the mode of a serial port.
 * Remember, this is only required if your kernel is handling rs485 natively.
 * If you are using a USB adapter, you do NOT need this.
 * @function ctx:rtu_set_serial_mode
 * @param mode The selected serial mode from @{rtu_constants}, either RTU_RS232 or RTU_RS485.
 */
static int ctx_rtu_set_serial_mode(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int mode = luaL_checknumber(L, 2);

	int rc = modbus_rtu_set_serial_mode(ctx->modbus, mode);

	return libmodbus_rc_to_nil_error(L, rc, 0);
}

static int ctx_get_header_length(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);

	lua_pushinteger(L, modbus_get_header_length(ctx->modbus));

	return 1;
}

/**
 * @function ctx:set_slave
 * @param unitid the unit address / slave id to use
 */
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

/**
 * @function ctx:read_input_bits
 * @param address
 * @param count
 * @return an array of results
 */
static int ctx_read_input_bits(lua_State *L)
{
	return _ctx_read_bits(L, true);
}

/**
 * @function ctx:read_bits
 * @param address
 * @param count
 * @return an array of results
 */
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

/**
 * @function ctx:read_input_registers
 * @param address
 * @param count
 * @return an array of results
 */
static int ctx_read_input_registers(lua_State *L)
{
	return _ctx_read_regs(L, true);
}

/**
 * @function ctx:read_registers
 * @param address
 * @param count
 * @return an array of results
 */
static int ctx_read_registers(lua_State *L)
{
	return _ctx_read_regs(L, false);
}

/**
 * @function ctx:report_slave_id
 * @return a luastring with the raw result (lua strings can contain nulls)
 */
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

/**
 * @function ctx:write_bit
 * @param address
 * @param value either a number or a boolean
 */
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

/**
 * @function ctx:write_register
 * @param address
 * @param value
 */
static int ctx_write_register(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int addr = luaL_checknumber(L, 2);
	int val = luaL_checknumber(L, 3);

	int rc = modbus_write_register(ctx->modbus, addr, val);
	
	return libmodbus_rc_to_nil_error(L, rc, 1);
}


/**
 * @function ctx:write_bits
 * @param address
 * @param value as a lua array table
 */
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
	int count = lua_rawlen(L, 3);

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


/**
 * @function ctx:write_registers
 * @param address
 * @param value as a lua array table, or a sequence of values.
 * @usage either
 *  ctx:write_registers(0x2000, {1,2,3})
 *  ctx:write_registers(0x2000, 1, 2, 3)
 */
static int ctx_write_registers(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int addr = luaL_checknumber(L, 2);
	int rc;
	int rcount;
	uint16_t *buf;
	int count;

	if (lua_type(L, 3) == LUA_TTABLE) {
		/* array style table only! */
		count = lua_rawlen(L, 3);

		if (count > MODBUS_MAX_WRITE_REGISTERS) {
			return luaL_argerror(L, 3, "requested too many registers");
		}

		/* Convert table to uint16_t array */
		buf = malloc(count * sizeof(uint16_t));
		assert(buf);
		for (int i = 1; i <= count; i++) {
			lua_rawgeti(L, 3, i);
			/* user beware! we're not range checking your values */
			if (lua_type(L, -1) != LUA_TNUMBER) {
				free(buf);
				return luaL_argerror(L, 3, "table values must be numeric yo");
			}
			/* This preserves sign and fractions better than tointeger() */
			lua_Number n = lua_tonumber(L, -1);
			buf[i-1] = (int16_t)n;
			lua_pop(L, 1);
		}
	} else {
		/* Assume sequence of values then... */
		int total_args = lua_gettop(L);
		if (total_args < 3) {
			return luaL_argerror(L, 3, "No values provided to write!");
		}
		count = total_args - 2;
		buf = malloc(count * sizeof(uint16_t));
		assert(buf);
		for (int i = 0; i < count; i++) {
			buf[i] = (int16_t)lua_tonumber(L, i + 3);
		}
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

static int ctx_send_raw_request(lua_State *L)
{
	ctx_t *ctx = ctx_check(L, 1);
	int rc;
	int rcount;
	int count = lua_rawlen(L, 2);
	luaL_checktype(L, 2, LUA_TTABLE);
	/* array style table only! */

	/* Convert table to uint8_t array */
	uint8_t *buf = malloc(lua_rawlen(L, 2) * sizeof(uint8_t));
	assert(buf);
	for (int i = 1; i <= count; i++) {
		lua_rawgeti(L, 2, i);
		/* user beware! we're not range checking your values */
		if (lua_type(L, -1) != LUA_TNUMBER) {
			free(buf);
			return luaL_argerror(L, 2, "table values must be numeric");
		}
		buf[i-1] = lua_tonumber(L, -1);
		lua_pop(L, 1);
	};
	rc = modbus_send_raw_request(ctx->modbus, buf, count);

	if (rc < 0) {
		lua_pushnil(L);
		lua_pushstring(L, modbus_strerror(errno));
		rcount = 2;
	} else {
                // wait
		lua_pushboolean(L, true);
		rcount = 1;
	}
	long wait = lua_tonumber(L,3);
	if (wait > 0) {
		static struct timeval tim;
		tim.tv_sec = 0;
		tim.tv_usec = wait;
		if (select(0, NULL, NULL, NULL, &tim) < 0) {
			lua_pushnil(L);
			return 2;
		};
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

/** Constants provided for use.
 * Constants used in various functions, either as arguments or returns
 * @section constants
 */

/**
 * RTU Mode Constants
 * @see rtu_get_serial_mode
 * @see rtu_set_serial_mode
 * @table rtu_constants
 * @field RTU_RS232
 * @field RTU_RS485
 */

/** Error Recovery Constants
 * @see set_error_recovery
 * @table error_recovery_constants
 * @field ERROR_RECOVERY_NONE
 * @field ERROR_RECOVERY_LINK
 * @field ERROR_RECOVERY_PROTOCOL
 */

/** Exception codes
 * These are all MODBUS_xxxx upstream in libmodbus.
 * @table exception_codes
 * @field EXCEPTION_ILLEGAL_FUNCTION
 * @field EXCEPTION_ILLEGAL_DATA_ADDRESS
 * @field EXCEPTION_ILLEGAL_DATA_VALUE
 * @field EXCEPTION_SLAVE_OR_SERVER_FAILURE
 * @field EXCEPTION_ACKNOWLEDGE
 * @field EXCEPTION_SLAVE_OR_SERVER_BUSY
 * @field EXCEPTION_NEGATIVE_ACKNOWLEDGE
 * @field EXCEPTION_MEMORY_PARITY
 * @field EXCEPTION_NOT_DEFINED
 * @field EXCEPTION_GATEWAY_PATH
 */
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

/** Other constants
 * @table other_constants
 * @field VERSION_STRING the <em>compile time</em> version, see also @{version}
 * @field BROADCAST_ADDRESS used in @{set_slave}
 * @field TCP_SLAVE used in @{set_slave}
 */
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

	{"set_s32",	helper_set_s32},
	{"set_f32",	helper_set_f32},

	{"get_s16",	helper_get_s16},
	{"get_s32",	helper_get_s32},
	{"get_s32le",	helper_get_s32le},
	{"get_u32",	helper_get_u32},
	{"get_u32le",	helper_get_u32le},
	{"get_f32",	helper_get_f32},
	{"get_f32le",	helper_get_f32le},
	{"get_s64",	helper_get_s64},
	{"get_u64",	helper_get_u64},
	/* {"get_u16",	helper_get_u16}, Not normally useful, just use the number as it was returned */
	
	{NULL, NULL}
};

static const struct luaL_Reg ctx_M[] = {
	{"connect",		ctx_connect},
	{"close",		ctx_close},
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
	{"rtu_get_serial_mode",	ctx_rtu_get_serial_mode},
	{"rtu_set_serial_mode",	ctx_rtu_set_serial_mode},
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
	{"send_raw_request",	ctx_send_raw_request},
	{"__gc",		ctx_destroy},
	{"__tostring",		ctx_tostring},
	
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
