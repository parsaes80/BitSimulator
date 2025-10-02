#pragma once
#include <QMetaType>
#include <vector>

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
    u32 id;
    Net() : value(false), id(0) {}  // Default constructor
    Net(bool val, u32 netId) : value(val), id(netId) {}  // Full constructor
};

struct Source {
	bool value;   // later: make this u64 for parallel sim	
	Source() : value(false) {}
	Source(bool val) : value(val) {}
};

// In general.h - replace the empty ExportGraph with this:
struct ExportGraph
{
    // Circuit components
    std::vector<Gate> gates;
    std::vector<Source> sources;
    std::vector<Net> nets;

    // Connection mappings
    std::vector<std::vector<u32>> gateInputs; // gateInputs[gateIndex] = {netIndex1, netIndex2, ...}
    std::vector<u32> gateOutputs;             // gateOutputs[gateIndex] = netIndex
    std::vector<u32> sourceOutputs;           // sourceOutputs[sourceIndex] = netIndex

    std::vector<u32> wireUItoSimMap;    // wireUItoSimMap[wireIndex] = netId
    std::vector<WireItem*> simToUIMap;  // simToUIMap[netId] = wireItem pointer

    // Metadata
    u32 totalGates;
    u32 totalSources;
    u32 totalNets;

    // Constructor
    ExportGraph() : totalGates(0), totalSources(0), totalNets(0) {}

    // Helper methods
    void clear()
    {
        gates.clear();
        sources.clear();
        nets.clear();
        gateInputs.clear();
        gateOutputs.clear();
        sourceOutputs.clear();
        totalGates = totalSources = totalNets = 0;
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
