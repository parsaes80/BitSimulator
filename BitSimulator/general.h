#pragma once
#include <QMetaType>
#include <vector>
#include <unordered_map>

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
enum class GType : u8 {NOT, AND, OR, XOR, NAND, NOR, XNOR };
enum class Direction : u8 { UP, RIGHT, DOWN, LEFT };
enum class PortType : u8 {IN,OUT};

struct Register {
	u32 inID;
	u32 outID;  
	RType regType;
	bool storedValue = false;

	Register() = default;
	Register(RType t, u32 inId, u32 outId) : regType(t), inID(inId), outID(outId) {}
};

struct Gate {
	u32 inID;    
	u32 outID;    
	u16 numInputs; 
	GType gateType;
	Gate() = default;
	Gate(GType t, u32 inId, u32 outId, u16 inNum) : gateType(t), inID(inId), outID(outId), numInputs(inNum) {}
};

struct Source {
	
	u32 outID;
	std::vector<bool> cycleValues;
	u8 currentIndex = -1;
	Source() = default;
	Source(u32 outID,std::vector<bool> cycleValues) :outID(outID), cycleValues(cycleValues){}
};

struct ExportGraph
{
    std::vector<Gate> gates;
    std::vector<Source> sources;
	std::vector<Register> registers;

    std::vector<u32> gateInputs;   
    //std::vector<u32> sourceCycleValues;

    void clear()
    {
        gates.clear();
        sources.clear();
        gateInputs.clear();
    }
};

struct SimResult
{
    std::vector<bool> netValues;  // netValues[netId] = true/false
	std::vector<u8> sourcesCurrIdx;
};

struct GlobalMap {
	std::unordered_map <u32, std::vector<WireItem*>> net2wire;
	std::unordered_map <WireItem*, u32> wire2net;    

	std::unordered_map <GateItem*, u32> gate2Idx;
	std::unordered_map <u32, GateItem*> Idx2gate;

	std::unordered_map <SourceItem*, u32> source2Idx; 
	std::unordered_map <u32, SourceItem*> Idx2source;

	std::unordered_map <RegisterItem*, u32> reg2Idx;
	std::unordered_map <u32, RegisterItem*> Idx2reg;
};