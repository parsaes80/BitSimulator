#pragma once
#include <utility>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

enum class RType : u8 { SR, JK, D, T };
enum class GType : u8 {NOT, AND, OR, XOR, NAND, NOR, XNOR };
enum class Direction : u8 { UP, RIGHT, DOWN, LEFT };
enum class PortType : u8 {IN,OUT};

struct Register {
	RType regType;
	u32 inID;   // index into a big inputs[] array
	u32 outID;  // index of output net
	bool nextValue;
	
	Register() = default;
	Register(RType t, u32 inId, u32 outId, bool next = false) : regType(t), inID(inId), outID(outId), nextValue(next) {}
};

struct Gate {
	GType gateType;
	u32 inID;     // index into a big inputs[] array
	u32 outID;    // index of output net
	u16 numInputs; // how many inputs this gate has
	
	Gate() = default;
	Gate(GType t, u32 inId, u32 outId, u16 inNum) : gateType(t), inID(inId), outID(outId), numInputs(inNum) {}
};

struct Net {
	bool value;   // later: make this u64 for parallel sim	
	Net() : value(false) {}
	Net(bool val) : value(val) {}
};

struct Source {
	bool value;   // later: make this u64 for parallel sim	
	Source() : value(false) {}
	Source(bool val) : value(val) {}
};
