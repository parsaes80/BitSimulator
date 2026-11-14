#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include "SourceProps.h"

class WireItem;
class GateItem;
class SourceItem;
class RegisterItem;

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

enum class RType : u8 { SR, JK, D, T };
enum class GType : u8 {NOT, AND, OR, XOR, NAND, NOR, XNOR};
enum class Direction : u8 { UP, RIGHT, DOWN, LEFT };
enum class PortType : u8 {IN,OUT};
enum class MType : u8 {MUX,DEMUX};
enum class IOType : u8 {IN,OUT};

struct Register {
	u32 inID;
    u32 InID2; // R input in SR D T and K in JK case
	u32 outID;  
	u32 clkID;
	u32 enableID;
	RType regType;
	bool storedValue = false;
	bool prevClkState = false;
	Register() = default;
	Register(RType t, u32 inId, u32 outId, u32 clkId, u32 enId)
        : regType(t), inID(inId), outID(outId), clkID(clkId), enableID(enId) {InID2=0;}
    Register(RType t, u32 inId,u32 inId2, u32 outId, u32 clkId, u32 enId)
        : regType(t), inID(inId), InID2(inId2), outID(outId), clkID(clkId), enableID(enId) {}
};

struct Gate {
    std::vector<u32> inIDs;
	u32 outID;    
	GType gateType;
	Gate() = default;
    Gate(GType t, std::vector<u32> inIds, u32 outId) : gateType(t), inIDs(inIds), outID(outId) {}
};

// A source component: at every tick it computes sourceValueAt(props, tick) and drives bit i onto outIDs[i]
struct Source {
    std::vector<u32> outIDs;   // the net of each pin, lowest bit first; 0 = pin not connected
    SourceProps props;
    Source() = default;
    Source(std::vector<u32> outIDs, SourceProps props) : outIDs(std::move(outIDs)), props(std::move(props)) {}
};

struct Mux{
    std::vector<u32> inData;
    std::vector<u32> inAddress;
    u32 outID;
    Mux()= default;
    Mux(std::vector<u32> InData, std::vector<u32> InAddress,u32 outData):
        inData(InData), inAddress(InAddress), outID(outData){}
};

struct ExportGraph
{
    std::vector<Gate> gates;
    std::vector<Source> sources;
	std::vector<Register> registers;
    std::vector<Mux> muxes;

    u32 numNets = 0;   // highest net id used by the components above
    u32 runId = 0;     // identifies this run; the simulator copies it into every result

    void clear()
    {
        gates.clear();
        sources.clear();
        registers.clear();
        muxes.clear();
    }
};

struct SimResult
{
    u32 runId = 0;     // which run produced this result; the canvas ignores results of older runs
    std::vector<u8> netValues;
    u64 sourceTick = 0;   // the tick whose values the sources drove; the canvas shows each source's value at it
	std::vector<bool> registerValues;
};

struct GlobalMap {
	std::unordered_map <u32, std::vector<WireItem*>> net2wire; // One net can be mapped to multiple WireItems
	std::unordered_map <WireItem*, u32> wire2net;  // Multiple WireItems can be mapped to the same net

	std::unordered_map <GateItem*, u32> gate2Idx;
	std::unordered_map <u32, GateItem*> Idx2gate;

	std::unordered_map <RegisterItem*, u32> reg2Idx;
	std::unordered_map <u32, RegisterItem*> Idx2reg;
	
	void clear() {
		net2wire.clear();
		wire2net.clear();
		gate2Idx.clear();
		Idx2gate.clear();
		reg2Idx.clear();
		Idx2reg.clear();
    }
};
