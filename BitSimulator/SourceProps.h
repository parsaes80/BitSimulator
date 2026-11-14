#pragma once

#include <QList>
#include <algorithm>
#include <bit>
#include <cstdint>

// What a source component drives, tick by tick.
//
// A source has `bits` output pins (lowest bit on the top pin) and a mode that decides which
// number it outputs at each simulation tick. The value is a pure function of the tick number
// (sourceValueAt), so the simulator and the canvas compute the same thing independently, and a
// counter or random source needs no stored list however wide it is.

enum class SourceMode : std::uint8_t {
    Counter,      // start, start+step, start+2*step, ... (or downwards); wraps at the bit width
    GrayCode,     // a counter in Gray code: one pin changes per step
    WalkingOne,   // a single 1 moving across the pins: 0001, 0010, 0100, 1000, 0001, ...
    Random,       // pseudo-random numbers over the full width; the same sequence on every run
    List,         // a list of numbers, then it repeats
    Waveforms,    // one 0/1 pattern per pin, each repeating on its own
    Constant,     // always the same number
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

    // The settings of every mode are kept, so switching modes and back does not lose them
    bool countDown = false;                         // Counter, Gray code; Walking one: from the top pin down
    std::uint64_t start = 0;                        // Counter, Gray code
    std::uint64_t step = 1;                         // Counter
    std::uint32_t seed = 1;                         // Random
    QList<std::uint64_t> values{2, 5};              // List
    QList<QList<bool>> waves{{false, true}};        // Waveforms: one pattern per pin, lowest pin first
    std::uint64_t constant = 0;                     // Constant

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

// Mixes a 64-bit number into one that looks random (splitmix64). The same input always gives the same output.
inline std::uint64_t sourceHash(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// The number the source outputs at `tick` (0 is the first tick of a run). Bit i drives pin i.
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
        return p.values[static_cast<qsizetype>(n % static_cast<std::uint64_t>(p.values.size()))] & mask;
    case SourceMode::Waveforms: {
        std::uint64_t value = 0;
        const int pins = std::min<int>(std::min<int>(p.bits, 64), static_cast<int>(p.waves.size()));
        for (int pin = 0; pin < pins; pin++) {
            const QList<bool>& wave = p.waves[pin];
            if (!wave.isEmpty() && wave[static_cast<qsizetype>(n % static_cast<std::uint64_t>(wave.size()))])
                value |= std::uint64_t{1} << pin;
        }
        return value;
    }
    case SourceMode::Constant:
        return p.constant & mask;
    }
    return 0;
}

// Brings the properties into a consistent state: limits, enough pins for a list or constant,
// one waveform per pin.
inline SourceProps normalized(SourceProps p)
{
    p.ticksPerStep = std::clamp(p.ticksPerStep, 1, SourceLimits::maxTicksPerStep);

    if (p.values.isEmpty()) p.values.append(0);
    if (p.values.size() > SourceLimits::maxSteps) p.values.resize(SourceLimits::maxSteps);

    int bits = std::clamp(p.bits, 1, SourceLimits::maxBits);
    if (p.mode == SourceMode::List) bits = std::max(bits, bitsNeeded(p.values));
    if (p.mode == SourceMode::Constant) bits = std::max(bits, bitsNeeded(p.constant));
    p.bits = bits;

    // Waveforms: a pattern for every pin; patterns of pins that were removed are kept for when they come back
    if (p.mode == SourceMode::Waveforms) {
        while (p.waves.size() < p.bits) p.waves.append(QList<bool>{false});
        for (QList<bool>& wave : p.waves) {
            if (wave.isEmpty()) wave.append(false);
            if (wave.size() > SourceLimits::maxSteps) wave.resize(SourceLimits::maxSteps);
        }
    }
    return p;
}
