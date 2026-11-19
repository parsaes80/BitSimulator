#pragma once
// Types shared by the simulator and the GUI: enums, source properties, the netlist and its results.

#include <QList>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

enum class RType : u8 { SR, JK, D, T };
enum class GType : u8 {NOT, AND, OR, XOR, NAND, NOR, XNOR};
enum class Direction : u8 { UP, RIGHT, DOWN, LEFT };
enum class PortType : u8 {IN,OUT};
enum class MType : u8 {MUX,DEMUX};
enum class IOType : u8 {IN,OUT};

//===================== 1. Source properties ========================

// A source's output is a pure function of the tick (sourceValueAt), so nothing is stored per step.

enum class SourceMode : std::uint8_t {
    Counter,      // wraps at the bit width
    GrayCode,     // a counter in Gray code: one pin changes per step
    WalkingOne,   // 0001, 0010, 0100, 1000, 0001, ...
    Random,       // the same sequence on every run
    List,
    Waveforms,    // one 0/1 pattern per pin
    Constant,
};
inline constexpr SourceMode allSourceModes[] = {
    SourceMode::Counter, SourceMode::GrayCode, SourceMode::WalkingOne, SourceMode::Random,
    SourceMode::List, SourceMode::Waveforms, SourceMode::Constant,
};

namespace SourceLimits {
inline constexpr int maxBits = 64;
inline constexpr int maxSteps = 65536;              // entries in a list, or steps in one pin's waveform
inline constexpr int maxTicksPerStep = 1000000;
}

struct SourceProps {
    SourceMode mode = SourceMode::Counter;
    int bits = 3;                                   // output pins
    int ticksPerStep = 1;                           // a new value every this many ticks (not for Constant)

    // Every mode's settings are kept, so switching modes loses nothing
    bool countDown = false;                         // Counter, Gray code, Walking one
    std::uint64_t start = 0;                        // Counter, Gray code
    std::uint64_t step = 1;                         // Counter
    std::uint32_t seed = 1;                         // Random
    QList<std::uint64_t> values{2, 5};              // List
    QList<QList<bool>> waves{{false, true}};        // Waveforms: one pattern per pin, lowest first
    std::uint64_t constant = 0;                     // Constant
    bool repeat = true;                             // List, Waveforms; false: hold the last value

    // Shorthands, mostly for tests and the HDL compiler
    static SourceProps waveform(const QList<bool>& pattern)
    {
        SourceProps p;
        p.mode = SourceMode::Waveforms;
        p.bits = 1;
        p.waves = {pattern};
        return p;
    }
    static SourceProps list(const QList<std::uint64_t>& numbers, int bits)
    {
        SourceProps p;
        p.mode = SourceMode::List;
        p.values = numbers;
        p.bits = bits;
        return p;
    }
    static SourceProps counter(int bits, int ticksPerStep = 1)
    {
        SourceProps p;
        p.mode = SourceMode::Counter;
        p.bits = bits;
        p.ticksPerStep = ticksPerStep;
        return p;
    }
    static SourceProps constantOf(std::uint64_t value, int bits)
    {
        SourceProps p;
        p.mode = SourceMode::Constant;
        p.bits = bits;
        p.constant = value;
        return p;
    }

    bool operator==(const SourceProps&) const = default;
};

// All ones in the lowest `bits` bits.
inline std::uint64_t sourceMask(int bits)
{
    return bits >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << std::max(bits, 0)) - 1;
}

// Smallest number of pins that can show `value`.
inline int bitsNeeded(std::uint64_t value)
{
    return std::max(1, static_cast<int>(std::bit_width(value)));
}

inline int bitsNeeded(const QList<std::uint64_t>& values)
{
    return values.isEmpty() ? 1 : bitsNeeded(*std::ranges::max_element(values));
}

// splitmix64: a stateless hash that looks random
inline std::uint64_t sourceHash(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// The entry used at step n: wraps around, or without repeat holds the last one
inline qsizetype stepIndex(std::uint64_t n, qsizetype size, bool repeat)
{
    const auto count = static_cast<std::uint64_t>(size);
    return static_cast<qsizetype>(repeat ? n % count : std::min(n, count - 1));
}
// The value at `tick` (0 = first tick of the run); bit i drives pin i

inline std::uint64_t sourceValueAt(const SourceProps& p, std::uint64_t tick)
{
    const std::uint64_t mask = sourceMask(p.bits);
    const std::uint64_t n = tick / static_cast<std::uint64_t>(std::max(p.ticksPerStep, 1));   // which step

    switch (p.mode) {
    case SourceMode::Counter:
        // Unsigned arithmetic wraps at 2^64; the mask then wraps it at the bit width
        return (p.countDown ? p.start - n * p.step : p.start + n * p.step) & mask;
    case SourceMode::GrayCode: {
        const std::uint64_t count = (p.countDown ? p.start - n : p.start + n) & mask;
        return (count ^ (count >> 1)) & mask;
    }
    case SourceMode::WalkingOne: {
        const int bits = std::clamp(p.bits, 1, 64);
        const int pin = static_cast<int>(n % static_cast<std::uint64_t>(bits));
        return std::uint64_t{1} << (p.countDown ? bits - 1 - pin : pin);
    }
    case SourceMode::Random:
        return sourceHash((std::uint64_t{p.seed} << 32) ^ sourceHash(n)) & mask;
    case SourceMode::List:
        if (p.values.isEmpty()) return 0;
        return p.values[stepIndex(n, p.values.size(), p.repeat)] & mask;
    case SourceMode::Waveforms: {
        std::uint64_t value = 0;
        const int pins = std::min<int>(std::min<int>(p.bits, 64), static_cast<int>(p.waves.size()));
        for (int pin = 0; pin < pins; pin++) {
            const QList<bool>& wave = p.waves[pin];
            if (!wave.isEmpty() && wave[stepIndex(n, wave.size(), p.repeat)]) value |= std::uint64_t{1} << pin;
        }
        return value;
    }
    case SourceMode::Constant:
        return p.constant & mask;
    }
    return 0;
}

// Clamps the limits; enough pins for a list or constant, one pattern per pin
inline SourceProps normalized(SourceProps p)
{
    p.ticksPerStep = std::clamp(p.ticksPerStep, 1, SourceLimits::maxTicksPerStep);

    if (p.values.isEmpty()) p.values.append(0);
    if (p.values.size() > SourceLimits::maxSteps) p.values.resize(SourceLimits::maxSteps);

    int bits = std::clamp(p.bits, 1, SourceLimits::maxBits);
    if (p.mode == SourceMode::List) bits = std::max(bits, bitsNeeded(p.values));
    if (p.mode == SourceMode::Constant) bits = std::max(bits, bitsNeeded(p.constant));
    p.bits = bits;

    // Patterns of removed pins are kept for when they come back
    if (p.mode == SourceMode::Waveforms) {
        while (p.waves.size() < p.bits) p.waves.append(QList<bool>{false});
        for (QList<bool>& wave : p.waves) {
            if (wave.isEmpty()) wave.append(false);
            if (wave.size() > SourceLimits::maxSteps) wave.resize(SourceLimits::maxSteps);
        }
    }
    return p;
}

//===================== The netlist the simulator runs ========================

struct Register {
	u32 inID;
    u32 InID2; // R of SR, K of JK, reset of D and T
	u32 outID;  
	u32 clkID;
	u32 enableID;
	RType regType;
	bool asyncReset = false;    // D and T: InID2 resets without waiting for the clock
	bool resetValue = false;    // D and T: what InID2 sets the register to
	bool storedValue = false;
	bool prevClkState = false;
	bool toggledThisTick = false;   // a JK latch with J = K = 1 toggles at most once per tick
	Register() = default;
    Register(RType t, u32 inId, u32 inId2, u32 outId, u32 clkId, u32 enId, bool asyncReset = false, bool resetValue = false)
        : inID(inId), InID2(inId2), outID(outId), clkID(clkId), enableID(enId), regType(t), asyncReset(asyncReset), resetValue(resetValue) {}
};

struct Gate {
    std::vector<u32> inIDs;
	u32 outID;    
	GType gateType;
	Gate() = default;
    Gate(GType t, std::vector<u32> inIds, u32 outId) : inIDs(std::move(inIds)), outID(outId), gateType(t) {}
};

// Drives sourceValueAt(props, tick) onto outIDs every tick
struct Source {
    std::vector<u32> outIDs;   // lowest bit first; 0 = not connected
    SourceProps props;
    Source() = default;
    Source(std::vector<u32> outIDs, SourceProps props) : outIDs(std::move(outIDs)), props(std::move(props)) {}
};

struct Mux{
    std::vector<u32> inData;
    std::vector<u32> inAddress;
    u32 outID;
    Mux()= default;
    Mux(std::vector<u32> data, std::vector<u32> address, u32 out)
        : inData(std::move(data)), inAddress(std::move(address)), outID(out) {}
};

struct ExportGraph
{
    std::vector<Gate> gates;
    std::vector<Source> sources;
	std::vector<Register> registers;
    std::vector<Mux> muxes;

    u32 numNets = 0;   // highest net id used by the components above
    u32 runId = 0;     // copied into every result

    // The nets the waveforms show; sorted, unique, no 0
    std::vector<u32> recordedNets;
};

struct WaveChange {
    u64 tick = 0;
    u32 net = 0;
};

// Recorded nets start at 0: a net's value is the parity of its changes so far
struct WaveChanges {
    u32 runId = 0;
    u64 lastTick = 0;                  // the newest tick these changes cover
    bool hasTicks = false;             // false: no tick since the last report
    std::vector<WaveChange> changes;   // in tick order
};

struct SimResult
{
    u32 runId = 0;     // the canvas ignores results of older runs
    std::vector<u8> netValues;
    u64 sourceTick = 0;   // the tick the sources drove
	std::vector<bool> registerValues;
};


