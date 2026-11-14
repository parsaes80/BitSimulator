#pragma once

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <algorithm>
#include <bit>
#include <variant>
#include "general.h"

// The editable properties of each kind of component.
//
// The same types describe a component that is already on the canvas and the
// defaults of the tool that places new ones, so one editor serves both.

using SourceValues = std::variant<QList<bool>, QList<int>>;   // waveform (1 bit) or a list of numbers

struct GateProps {
    GType type = GType::AND;
    int numInputs = 2;                 // always 1 for NOT
    bool operator==(const GateProps&) const = default;
};

struct RegisterProps {
    RType type = RType::D;
    bool edgeTriggered = true;         // true: flip-flop with a clock pin, false: latch
    bool hasEnable = true;
    bool operator==(const RegisterProps&) const = default;
};

struct MuxProps {
    MType type = MType::MUX;
    int numInputs = 2;
    bool operator==(const MuxProps&) const = default;
};

struct SourceProps {
    SourceValues values = QList<int>{2, 5};
    int bits = 3;                      // output pins; always 1 for a waveform
    bool operator==(const SourceProps&) const = default;
};

struct DisplayProps {
    int numInputs = 2;
    int numOutputs = 2;
    bool operator==(const DisplayProps&) const = default;
};

using ItemProps = std::variant<GateProps, RegisterProps, MuxProps, SourceProps, DisplayProps>;

// Position of T inside ItemProps; used instead of hard-coded page/tool numbers.
namespace detail {
template <typename T, typename Variant> struct VariantIndex;
template <typename T, typename... Ts>
struct VariantIndex<T, std::variant<Ts...>> {
    static constexpr std::size_t value = [] {
        std::size_t index = 0;
        (void)((std::is_same_v<T, Ts> ? true : (++index, false)) || ...);
        return index;
    }();
};
}
template <typename T>
inline constexpr std::size_t itemKind = detail::VariantIndex<T, ItemProps>::value;
inline constexpr std::size_t itemKindCount = std::variant_size_v<ItemProps>;

namespace ItemLimits {
inline constexpr int minGateInputs = 2, maxGateInputs = 8;
inline constexpr int minMuxInputs = 2, maxMuxInputs = 8;
inline constexpr int minDisplayInputs = 1, maxDisplayInputs = 8;
inline constexpr int minDisplayOutputs = 0, maxDisplayOutputs = 8;
inline constexpr int maxSourceBits = 16;
inline constexpr int maxSourceSteps = 255;     // the simulator indexes steps with a u8
}

// Smallest number of output pins that can show every value in the list.
inline int bitsNeeded(const QList<int>& values)
{
    const int maxValue = values.isEmpty() ? 0 : *std::ranges::max_element(values);
    return std::max(1, static_cast<int>(std::bit_width(static_cast<unsigned>(std::max(maxValue, 0)))));
}

// Select lines a multiplexer needs to address `numInputs` data inputs.
inline int muxSelectLines(int numInputs)
{
    return std::max(1, static_cast<int>(std::bit_width(static_cast<unsigned>(std::max(numInputs, 2) - 1))));
}

// Bring properties into a consistent state (a NOT gate has one input, a waveform has one pin, ...).
inline GateProps normalized(GateProps p)
{
    p.numInputs = (p.type == GType::NOT) ? 1 : std::max(p.numInputs, ItemLimits::minGateInputs);
    return p;
}
inline MuxProps normalized(MuxProps p)
{
    p.numInputs = std::max(p.numInputs, ItemLimits::minMuxInputs);
    return p;
}
inline SourceProps normalized(SourceProps p)
{
    if (auto* wave = std::get_if<QList<bool>>(&p.values)) {
        if (wave->isEmpty()) wave->append(false);
        p.bits = 1;
    } else {
        auto& numbers = std::get<QList<int>>(p.values);
        if (numbers.isEmpty()) numbers.append(0);
        p.bits = std::max(p.bits, bitsNeeded(numbers));
    }
    return p;
}
inline DisplayProps normalized(DisplayProps p)
{
    p.numInputs = std::max(p.numInputs, ItemLimits::minDisplayInputs);
    p.numOutputs = std::max(p.numOutputs, ItemLimits::minDisplayOutputs);
    return p;
}
inline RegisterProps normalized(RegisterProps p) { return p; }

inline QString gateTypeName(GType type)
{
    switch (type) {
    case GType::AND:  return QStringLiteral("AND");
    case GType::OR:   return QStringLiteral("OR");
    case GType::XOR:  return QStringLiteral("XOR");
    case GType::NAND: return QStringLiteral("NAND");
    case GType::NOR:  return QStringLiteral("NOR");
    case GType::XNOR: return QStringLiteral("XNOR");
    case GType::NOT:  return QStringLiteral("NOT");
    }
    return {};
}

inline QString registerTypeName(RType type)
{
    switch (type) {
    case RType::SR: return QStringLiteral("SR");
    case RType::JK: return QStringLiteral("JK");
    case RType::D:  return QStringLiteral("D");
    case RType::T:  return QStringLiteral("T");
    }
    return {};
}

// Human-readable name of a component, e.g. "NAND gate" or "D flip-flop".
inline QString itemTitle(const ItemProps& props)
{
    struct Visitor {
        QString operator()(const GateProps& p) const { return QCoreApplication::translate("ItemProps", "%1 gate").arg(gateTypeName(p.type)); }
        QString operator()(const RegisterProps& p) const {
            return (p.edgeTriggered ? QCoreApplication::translate("ItemProps", "%1 flip-flop")
                                    : QCoreApplication::translate("ItemProps", "%1 latch")).arg(registerTypeName(p.type));
        }
        QString operator()(const MuxProps&) const { return QCoreApplication::translate("ItemProps", "Multiplexer"); }
        QString operator()(const SourceProps& p) const {
            return std::holds_alternative<QList<bool>>(p.values) ? QCoreApplication::translate("ItemProps", "Waveform source")
                                                                 : QCoreApplication::translate("ItemProps", "Value source");
        }
        QString operator()(const DisplayProps&) const { return QCoreApplication::translate("ItemProps", "Display"); }
    };
    return std::visit(Visitor{}, props);
}
