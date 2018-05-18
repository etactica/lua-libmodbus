require("busted")
--local rmep = require("remake.plugin_support")
local mb = require("libmodbus")

function noit()
end

function nodescribe()
end

describe("signed vs unsigned", function()
    it("should decode unsigned", function()
        regs = { 0x0020, 0x3456 }
        val = mb.get_s32(unpack(regs))
        valu = mb.get_u32(unpack(regs))
        assert.are.equal(val, valu)
        assert.are.equal(val, 2110550)
    end)
    it("should decode signed negative and large positive", function()
        regs = { 0xffff, 0xfc44 }
        val = mb.get_s32(unpack(regs))
        valu = mb.get_u32(unpack(regs))
        assert.are.equal(val, -956)
        assert.are.equal(valu, 0xfffffc44)
        assert.are.equal(valu, 4294966340)
    end)
    it("should do near edges", function()
        regs = { 0xffff, 0xfffe }
        assert.are.equal(-2, mb.get_s32(unpack(regs)))
        valu = mb.get_u32(unpack(regs))
        assert.are.equal(0xfffffffe, valu)
        assert.are.equal(4294967294, valu)
    end)
    it("should do edges_high", function()
        regs = { 0xffff, 0xffff }
        assert.are.equal(mb.get_s32(unpack(regs)), -1)
        assert.are.equal(mb.get_u32(unpack(regs)), 4294967295)
    end)
    it("should do edges_low", function()
        regs = { 0x0, 0x0 }
        assert.are.equal(mb.get_s32(unpack(regs)), 0)
        assert.are.equal(mb.get_u32(unpack(regs)), 0)
    end)
end)

describe("regswap", function()
    it("should work normally", function()
        regs = {0x1122, 0x3344}
        assert.are_equal(860098850, mb.get_s32le(unpack(regs)))
        assert.are_equal(287454020, mb.get_s32(unpack(regs)))
        assert.are_equal(860098850, mb.get_u32le(unpack(regs)))
        assert.are_equal(287454020, mb.get_u32(unpack(regs)))
    end)
    it("should handle negatives too", function()
        regs = { 0xfffa, 0xfffe }
        assert.are.equal(0xfffefffa, mb.get_u32le(unpack(regs)))
        assert.are.equal(4294901754, mb.get_u32le(unpack(regs)))
        assert.are.equal(-65542, mb.get_s32le(unpack(regs)))
    end)
end)

describe("64bit ops", function()
    it("should handle basic stuff", function()
        regs = {0xffff, 0xffff, 0xffff, 0xfffe}
        local valu = mb.get_u64(unpack(regs))
        assert.are.equal(18446744073709551614, mb.get_u64(unpack(regs)))
        assert.are.equal(-2, mb.get_s64(unpack(regs)))
    end)
    it("should handle small stuff too", function()
        regs = {0x0001, 0x0001, 0xffff, 0xfffe}
        assert.are.equal(281483566645246, mb.get_s64(unpack(regs)))
        assert.are.equal(281483566645246, mb.get_u64(unpack(regs)))
    end)
    it("should handle zero", function()
        regs = {0x0000, 0x0000, 0x0000, 0x0000}
        assert.are.equal(0, mb.get_s64(unpack(regs)))
        assert.are.equal(0, mb.get_u64(unpack(regs)))
    end)
    it("should handle one", function()
        regs = {0x0000, 0x0000, 0x0000, 0x0001}
        assert.are.equal(1, mb.get_s64(unpack(regs)))
        assert.are.equal(1, mb.get_u64(unpack(regs)))
    end)
    it ("should handle minus one", function()
        regs = {0xffff, 0xffff, 0xffff, 0xffff}
        assert.are.equal(-1, mb.get_s64(unpack(regs)))
    end)
    it ("should handle giant number", function()
        regs = {0xffff, 0xffff, 0xffff, 0xffff}
        local result = mb.get_u64(unpack(regs))
        assert.is_true(result >= 1.84467440737E19 and result <= 1.844674407372E19)
    end)
    it ("should handle giant negative number", function()
        regs = {0x8000, 0x0000, 0x0000, 0x0000}
        local result = mb.get_s64(unpack(regs))
        assert.is_true(result >= -9.2233720368549E18 and result <= -9.2233720368547E18)
    end)
end)
