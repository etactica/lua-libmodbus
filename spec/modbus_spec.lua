--[[
--You'll need a working modbus device of some description for these tests...
--that's.... out of scope....
--]]
local D = {
	slave = 0xb6,
	host = "192.168.255.74",
	service = 1502,
	base = 0x2000,
	count = 10,
}

require("busted")
mb = require("libmodbus")

describe("basic arg passing", function()

	it("should accept helpful new_ params", function()
		assert.truthy(mb.new_tcp_pi("192.168.255.74", "mbap"))
		assert.truthy(mb.new_tcp_pi("192.168.255.74", 1502))
		assert.has_error(function() mb.new_tcp_pi(true, "xxx") end)
		assert.has_error(function() mb.new_tcp_pi("xxx", {}) end)
	end)

	it("should get/set byte timeouts properly", function()
		x = mb.new_tcp_pi("blah", 123)
		local tv = {x:get_byte_timeout()}
		assert.is_same({0, 500*1000}, tv)
		x:set_byte_timeout(1, 250*1000)
		tv = {x:get_byte_timeout()}
		assert.is_same({1, 250*1000}, tv)
	end)
	it("should get/set response timeouts properly", function()
		x = mb.new_tcp_pi("blah", 123)
		local tv = {x:get_response_timeout()}
		assert.is_same({0, 500*1000}, tv)
		x:set_response_timeout(1, 250*1000)
		tv = {x:get_response_timeout()}
		assert.is_same({1, 250*1000}, tv)
	end)

end)

describe("functional tcp pi tests #real", function()
	local x
	setup(function()
		x = mb.new_tcp_pi(D.host, D.service)
	end)

	it("should connect ok", function()
		assert.is_truthy(x:connect())
		assert.is_truthy(x:set_slave(D.slave))
		local res, err = x:read_registers(D.base, D.count)
		assert.is_truthy(res, err)
	end)

	it("should fail well", function()
		assert.is_truthy(x:connect())
		assert.is_truthy(x:set_slave(D.slave))
		local res, err = x:read_registers(9999, 8)
		assert.falsy(res, "should have failed with illegal address")
	end)
		
	
end)