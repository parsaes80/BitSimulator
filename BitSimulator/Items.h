#pragma once
// The components of a circuit: their properties, the canvas items that draw them, and the colors
// they are drawn with.
//
//   1. Source properties   what a source outputs at each tick (the simulator uses these too)
//   2. Component properties the editable settings of every kind of component
//   3. Canvas items         the QGraphicsItems on the canvas
//   4. Canvas colors        CanvasTheme, filled from the style sheet in BitSimulator.ui
//
// Include order with general.h: part 1 needs nothing from general.h and comes before including
// it, because general.h includes this file in turn (its Source struct holds a SourceProps).

#include <QCoreApplication>
#include <QColor>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QList>
#include <QMimeData>
#include <QPainter>
#include <QString>
#include <QStyleOptionGraphicsItem>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <expected>
#include <variant>

//===================== 1. Source properties ========================

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

#include "general.h"   // after part 1, see the note at the top

//===================== 2. Component properties ========================

// The editable properties of each kind of component.
//
// The same types describe a component that is already on the canvas and the
// defaults of the tool that places new ones, so one editor serves both.

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

inline QString sourceModeName(SourceMode mode)
{
    switch (mode) {
    case SourceMode::Counter:    return QCoreApplication::translate("ItemProps", "Counter");
    case SourceMode::GrayCode:   return QCoreApplication::translate("ItemProps", "Gray code");
    case SourceMode::WalkingOne: return QCoreApplication::translate("ItemProps", "Walking one");
    case SourceMode::Random:     return QCoreApplication::translate("ItemProps", "Random");
    case SourceMode::List:       return QCoreApplication::translate("ItemProps", "List");
    case SourceMode::Waveforms:  return QCoreApplication::translate("ItemProps", "Waveform");
    case SourceMode::Constant:   return QCoreApplication::translate("ItemProps", "Constant");
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
            return QCoreApplication::translate("ItemProps", "%1 source").arg(sourceModeName(p.mode));
        }
        QString operator()(const DisplayProps&) const { return QCoreApplication::translate("ItemProps", "Display"); }
    };
    return std::visit(Visitor{}, props);
}

//===================== 3. Canvas items ========================

class WireItem;
class GateItem;

class PortItem : public QGraphicsEllipseItem{
public:
    PortItem(PortType portType, int pinIndex = 0, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    PortType getPortType() const { return m_portType;}

    bool canConnectTo(PortItem* otherPort) const;

    void addConnection(WireItem* wire);
    void removeConnection(WireItem* wire);

    QList<WireItem*> getConnections() const { return m_connections; }
    QGraphicsObject* getParent() const { return dynamic_cast<QGraphicsObject*>(parentItem()); };
    void setHighlighted(bool highlighted) {m_highlighted = highlighted;update();};
    bool getValue() const { return m_value; };
    void setValue(bool value) { m_value = value; };

private:
    bool m_value = false;
    int m_pinIndex;
    bool m_highlighted = false;
    PortType m_portType;
    QList<WireItem*> m_connections;
};

// Base of everything that can be placed on the canvas and wired up.
// Its properties can be read and changed after it has been placed.
class ComponentItem : public QGraphicsObject {
    Q_OBJECT
public:
    // Empty on success; otherwise the reason the edit was refused (nothing is changed in that case).
    using EditResult = std::expected<void, QString>;

    // The painted area leaves room for the selection outline; clicks only count on the body itself
    QRectF boundingRect() const override { return m_rect.adjusted(-PaintMargin, -PaintMargin, PaintMargin, PaintMargin); }
    QPainterPath shape() const override { QPainterPath body; body.addRect(m_rect.adjusted(-2, -2, 2, 2)); return body; }

    virtual ItemProps props() const = 0;
    // An edit that would remove a pin which still has a wire on it is refused.
    virtual EditResult setProps(const ItemProps& props) = 0;
    virtual QList<PortItem*> ports() const = 0;

    bool hasConnections() const;

protected:
    explicit ComponentItem(QGraphicsItem* parent = nullptr);

    // A dashed outline SelectionMargin outside the body while the item is selected
    void drawSelection(QPainter* painter, const QStyleOptionGraphicsItem* option) const;
    static constexpr qreal SelectionMargin = 5;
    static constexpr qreal PaintMargin = SelectionMargin + 1;

    // Refuses if any of ports[newCount..] is wired; `what` names the pin group, e.g. "Input".
    static EditResult checkRemovable(const QList<PortItem*>& ports, int newCount, const QString& what);
    static EditResult checkRemovable(const PortItem* port, bool stillWanted, const QString& what);
    // Grows or shrinks a pin list; existing pins (and the wires on them) are kept.
    void resizePorts(QList<PortItem*>& ports, int count, PortType type);
    void setOptionalPort(PortItem*& port, bool wanted, PortType type);
    void updateWires() const;                         // call after pins have moved

    QRectF m_rect;
};

class GateItem : public ComponentItem {
public:
    explicit GateItem(const GateProps& props, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    ItemProps props() const override { return m_props; }
    EditResult setProps(const ItemProps& props) override;
    QList<PortItem*> ports() const override { return m_inputPorts + QList<PortItem*>{m_outputPort}; }

    GType getGateType() const { return m_props.type; };
    PortItem* getInputPort(int index) const { return m_inputPorts[index]; };
    QList<PortItem*> getInputPorts() const { return m_inputPorts; }
    PortItem* getOutputPort() const { return m_outputPort; }

private:
    void drawGateShape(QPainter* painter);
    void drawAndGate(QPainter* painter);
    void drawOrGate(QPainter* painter);
    void drawXorGate(QPainter* painter);
    void drawNotGate(QPainter* painter);
    void drawNotBubble(QPainter* painter);
    void layoutPorts();

    GateProps m_props;
    QList<PortItem*> m_inputPorts;
    PortItem* m_outputPort = nullptr;
};

class SourceItem : public ComponentItem {
public:
    explicit SourceItem(const SourceProps& props, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    ItemProps props() const override { return m_props; }
    EditResult setProps(const ItemProps& props) override;
    QList<PortItem*> ports() const override { return m_outputPorts; }

    QList<PortItem*> getOutputPorts() const { return m_outputPorts; };
    const SourceProps& sourceProps() const { return m_props; }

    // The tick whose value the source shows (the simulator reports it; 0 when not running)
    u64 tick() const { return m_tick; }
    void setTick(u64 tick);
    u64 value() const { return sourceValueAt(m_props, m_tick); }

private:
    void layoutPorts();

    SourceProps m_props;
    u64 m_tick = 0;
    QList<PortItem*> m_outputPorts;
};

class WireItem : public QGraphicsObject{
    Q_OBJECT
public:
    WireItem(QPointF startpos, QPointF endpos, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QPainterPath shape() const override;

    void setStartPort(PortItem* port);
    void setEndPort(PortItem* port);
    PortItem* getStartPort() const { return m_startPort; }
    PortItem* getEndPort() const { return m_endPort; }
    bool isConnected() const { return m_startPort && m_endPort; }

    void setStartPos(QPointF& pos) { localStartPos = pos;}
    void setEndPos(QPointF& pos) { localEndPos = pos;}
    void setValue(bool val) { m_value = val;}
    QPen pen() const { return m_pen; }
    void setPen(const QPen& pen){ m_pen = pen;update();}

public slots:
    void updateWirePosition();

private:
    bool m_value = false;
    PortItem* m_startPort = nullptr;
    PortItem* m_endPort = nullptr;
    QPointF localStartPos;
    QPointF localEndPos;
    QPen m_pen;  //need pen because of outside access
};

class RegisterItem : public ComponentItem {
public:
    explicit RegisterItem(const RegisterProps& props, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    ItemProps props() const override { return m_props; }
    EditResult setProps(const ItemProps& props) override;
    QList<PortItem*> ports() const override;

    RType getRegType() const { return m_props.type; };
    PortItem* getInputPort() const { return m_inputPortOne; };
    PortItem* getInputPortTwo() const { return m_inputPortTwo; };
    PortItem* getOutputPort() const { return m_outputPort; }
    PortItem* getReadEnablePort() const { return m_readEnbPort; }
    PortItem* getClkPort() const { return m_clkPort; }
    void setValue(bool value) { m_value = value;};
    bool getValue() const { return m_value; }

private:
    void layoutPorts();

    RegisterProps m_props;
    bool m_value = false;

    PortItem* m_inputPortOne = nullptr; // S,J,D,T
    PortItem* m_inputPortTwo = nullptr; // R,K,R,R

    PortItem* m_clkPort = nullptr;      // only on flip-flops
    PortItem* m_readEnbPort = nullptr;  // only with an enable input

    PortItem* m_outputPort = nullptr;
};

class MuxItem : public ComponentItem {
public:
    explicit MuxItem(const MuxProps& props, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    ItemProps props() const override { return m_props; }
    EditResult setProps(const ItemProps& props) override;
    QList<PortItem*> ports() const override { return m_inputDataPorts + m_inputAddressPorts + QList<PortItem*>{m_outputPort}; }

    QList<PortItem*> getInputPorts() const { return m_inputDataPorts; }
    QList<PortItem*> getAddressPorts() const { return m_inputAddressPorts; }
    PortItem* getOutputPort() const { return m_outputPort; }

private:
    void layoutPorts();

    MuxProps m_props;
    QList<PortItem*> m_inputDataPorts;
    QList<PortItem*> m_inputAddressPorts;
    PortItem* m_outputPort = nullptr;
};

class DisplayItem : public ComponentItem {
public:
    explicit DisplayItem(const DisplayProps& props, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    ItemProps props() const override { return m_props; }
    EditResult setProps(const ItemProps& props) override;
    QList<PortItem*> ports() const override { return m_inputPorts + m_outputPorts; }

    QList<PortItem*> getInputPorts() const { return m_inputPorts; }
    QList<PortItem*> getOutputPorts() const { return m_outputPorts; }
    void updateValue();
    int getValue() const { return m_value; }     // the number currently shown

private:
    void layoutPorts();

    DisplayProps m_props;
    int m_value = 0;
    QList<PortItem*> m_inputPorts;
    QList<PortItem*> m_outputPorts;
};

//===================== 4. Canvas colors ========================

// The colors the canvas, its components and the toolbar icons are painted with.
//
// Edit them in BitSimulator.ui, not here: the main window's style sheet sets every one of them
// through the "CircuitCanvas { qproperty-... }" rule. The values below are only the fallback for
// code that runs without that style sheet (the tests, for example), and match it.
struct CanvasTheme {
    QColor background{0x0a, 0x0e, 0x11};
    QColor dot{0x1e, 0x28, 0x2e};            // the dot grid on the canvas

    // Components: an outline in the component's own color, and a body tinted with it
    QColor componentFill{0x14, 0x1b, 0x1f};  // the body before tinting
    double componentTint = 0.5;              // 0 = plain body, 1 = body as strong as the outline
    QColor componentText{0xc3, 0xce, 0xd4};
    QColor andGate{0xe8, 0xb0, 0x5a};        // also NAND
    QColor orGate{0x5f, 0xd3, 0xa5};         // also NOR
    QColor xorGate{0xa7, 0x8b, 0xfa};        // also XNOR
    QColor notGate{0xf2, 0x7c, 0x7c};
    QColor mux{0xf5, 0x9e, 0x5b};
    QColor reg{0x7a, 0xa2, 0xf7};
    QColor source{0xef, 0x44, 0x44};
    QColor display{0x67, 0xe8, 0xf9};

    // Wires: one color while editing; while the simulation runs, a 1 lights up and a 0 dims
    QColor wire{0xc3, 0xce, 0xd4};
    QColor wireHigh{0x2d, 0xd4, 0xbf};
    QColor wireLow{0x3b, 0x4a, 0x52};

    // Pins, selection, and the wire being drawn
    QColor pin{0x4a, 0x59, 0x60};            // nothing connected
    QColor pinWired{0x14, 0xb8, 0xa6};
    QColor selection{0x2d, 0xd4, 0xbf};
    QColor drag{0x7a, 0x88, 0x90};           // a wire being dragged, not over a pin it can connect to
    QColor snap{0xfd, 0xe6, 0x8a};           // ... over a pin it can connect to, and that pin

    // The body of a component outlined in `outline`: the plain body moved `componentTint` of the way to it
    QColor fillFor(const QColor& outline) const
    {
        const double t = std::clamp(componentTint, 0.0, 1.0);
        const auto mix = [t](int from, int to) { return static_cast<int>(std::lround(from + (to - from) * t)); };
        return QColor(mix(componentFill.red(), outline.red()), mix(componentFill.green(), outline.green()),
                       mix(componentFill.blue(), outline.blue()));
    }

    QColor gateColor(GType type) const
    {
        switch (type) {
        case GType::AND: case GType::NAND: return andGate;
        case GType::OR:  case GType::NOR:  return orGate;
        case GType::XOR: case GType::XNOR: return xorGate;
        case GType::NOT: return notGate;
        }
        return componentText;
    }
};

// The one theme everything paints with. CircuitCanvas's properties write into it.
inline CanvasTheme& canvasTheme()
{
    static CanvasTheme theme;
    return theme;
}
