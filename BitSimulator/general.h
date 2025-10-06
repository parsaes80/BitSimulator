#pragma once
#include <QMetaType>
#include <vector>
#include <unordered_map>

class WireItem;

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
	u32 inID;
	u32 outID;  
	bool nextValue;
	
	Register() = default;
	Register(RType t, u32 inId, u32 outId, bool next = false) : regType(t), inID(inId), outID(outId), nextValue(next) {}
};

struct Gate {
	GType gateType;
	u32 inID;    
	u32 outID;    
	u16 numInputs; 
	
	Gate() = default;
	Gate(GType t, u32 inId, u32 outId, u16 inNum) : gateType(t), inID(inId), outID(outId), numInputs(inNum) {}
};

struct Net {
    bool value;  
    Net() : value(false) {}  
    Net(bool val, u32 netId) : value(val){}  
};

struct Source {
	bool value;
	u32 outID;  // Output net ID
	Source() : value(false), outID(0) {}
	Source(bool val, u32 outId = 0) : value(val), outID(outId) {}
};

struct ExportGraph
{
    // Circuit components
    std::vector<Gate> gates;
    std::vector<Source> sources;

    std::vector<u32> gateInputs;        // Flat array: all gate inputs concatenated

    std::unordered_map <u32, WireItem*> net2wire;    // net2wire[netId] = wireItem pointer
    std::unordered_map <WireItem*, u32> wire2net;          // wire2net[wireIndex] = netId

    // Helper methods
    void clear()
    {
        gates.clear();
        sources.clear();
        gateInputs.clear();
        net2wire.clear();
        wire2net.clear();
    }
};

struct SimResult
{
    std::vector<bool> netValues;        // netValues[netId] = true/false
    std::vector<u32> netIds;            // List of net IDs
    u32 simulationStep;
    bool simulationComplete;

    SimResult() : simulationStep(0), simulationComplete(false) {}
};

