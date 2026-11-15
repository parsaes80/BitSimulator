#pragma once
// The components of a circuit: their properties, the canvas items that draw them, and the colors
// they are drawn with.
//
//   1. Component properties the editable settings of every kind of component
//   2. Canvas items         the QGraphicsItems on the canvas
//   3. Canvas colors        CanvasTheme, filled from the style sheet in BitSimulator.ui
//
// A source's properties (SourceProps) are in general.h: the simulator uses them too.

#include <QCoreApplication>
#include <QColor>
#include <QGraphicsItem>
#include <QList>
#include <QPainter>
#include <QString>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <expected>
#include <variant>
#include "general.h"

//===================== 1. Component properties ========================

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
    // The reset input of a D or T flip-flop (its second pin): it sets the flip-flop to resetValue on the
    // clock edge, or with asyncReset at once, without waiting for the clock. Compiled Verilog uses
    // both kinds (e.g. "always @(posedge clk or posedge rst)" is asynchronous) and both values.
    bool asyncReset = false;
    bool resetValue = false;
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

// One port of a block: its pins are consecutive, lowest bit first
struct BlockPort {
    QString name;
    bool output = false;
    int width = 1;
    bool operator==(const BlockPort&) const = default;
};

// A submodule instance in a compiled design, drawn as one block. Double-clicking it opens the
// module's contents; the block itself has nothing to edit.
struct BlockProps {
    QString instance;               // e.g. "alu"
    QString module;                 // e.g. "ALU"
    QList<BlockPort> ports;         // sorted by name; inputs on the left, outputs on the right
    bool operator==(const BlockProps&) const = default;
};

using ItemProps = std::variant<GateProps, RegisterProps, MuxProps, SourceProps, DisplayProps, BlockProps>;

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
inline BlockProps normalized(BlockProps p) { return p; }

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
        QString operator()(const BlockProps& p) const {
            return QCoreApplication::translate("ItemProps", "%1 (%2)").arg(p.instance, p.module);
        }
    };
    return std::visit(Visitor{}, props);
}

// The size of a component's body on the canvas (its selection outline not included), and of a port
// tag inside a block. The HDL compiler gives these sizes to dot, so a layout leaves room for every
// component as it is drawn. `name`: the port name a source or display of compiled code shows.
QSizeF componentSize(const ItemProps& props, const QString& name = {});
QSizeF terminalSize(const QString& name, int width);

// A gate's symbol filling `body`, with the painter's pen and brush: the AND, OR, XOR or NOT shape,
// and for NAND, NOR and XNOR a bubble just right of it. Used on the canvas and on the toolbar.
void drawGateSymbol(QPainter* painter, GType type, const QRectF& body);

//===================== 2. Canvas items ========================

// True while the items draw simulation values (running or paused): wires and pins in their 0/1
// colors, a 1-pin source lit when it outputs 1. Set by CircuitScene; only the GUI thread uses it.
extern bool sim_values_visible;

class WireItem;

class PortItem : public QGraphicsEllipseItem{
public:
    explicit PortItem(PortType portType, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    PortType getPortType() const { return m_portType;}

    bool canConnectTo(PortItem* otherPort) const;

    void addConnection(WireItem* wire);
    void removeConnection(WireItem* wire);

    QList<WireItem*> getConnections() const { return m_connections; }
    QGraphicsObject* getParent() const { return dynamic_cast<QGraphicsObject*>(parentItem()); }
    void setHighlighted(bool highlighted) { m_highlighted = highlighted; update(); }
    bool getValue() const { return m_value; }
    void setValue(bool value);              // repaints when the value changes

private:
    bool m_value = false;
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

    GType getGateType() const { return m_props.type; }
    PortItem* getInputPort(int index) const { return m_inputPorts[index]; }
    QList<PortItem*> getInputPorts() const { return m_inputPorts; }
    PortItem* getOutputPort() const { return m_outputPort; }

private:
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

    QList<PortItem*> getOutputPorts() const { return m_outputPorts; }
    const SourceProps& sourceProps() const { return m_props; }

    // The tick whose value the source shows (the simulator reports it; 0 when not running)
    u64 tick() const { return m_tick; }
    void setTick(u64 tick);
    u64 value() const { return sourceValueAt(m_props, m_tick); }

    // A source made for an input port of compiled code shows the port's name at its top, and is
    // made wide enough for it. Placed by hand, it has none.
    const QString& name() const { return m_name; }
    void setName(const QString& name);

private:
    void layoutPorts();

    SourceProps m_props;
    QString m_name;
    u64 m_tick = 0;
    QList<PortItem*> m_outputPorts;
};

class WireItem : public QGraphicsObject{
    Q_OBJECT
public:
    WireItem(QPointF startpos, QPointF endpos, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QPainterPath shape() const override;   // the path, widened so a click near it selects it

    void setStartPort(PortItem* port);
    void setEndPort(PortItem* port);
    PortItem* getStartPort() const { return m_startPort; }
    PortItem* getEndPort() const { return m_endPort; }
    bool isConnected() const { return m_startPort && m_endPort; }

    void setEnds(QPointF start, QPointF end);   // a wire still being dragged: its two ends, in scene coordinates
    void setValue(bool value);              // repaints when the value changes
    QPen pen() const { return m_pen; }
    void setPen(const QPen& pen){ m_pen = pen;update();}

public slots:
    void updateWirePosition();

private:
    bool m_value = false;
    PortItem* m_startPort = nullptr;
    PortItem* m_endPort = nullptr;
    QPainterPath path() const;   // horizontal, vertical, horizontal between the two ends

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

    RType getRegType() const { return m_props.type; }
    PortItem* getInputPort() const { return m_inputPortOne; }
    PortItem* getInputPortTwo() const { return m_inputPortTwo; }
    PortItem* getOutputPort() const { return m_outputPort; }
    PortItem* getReadEnablePort() const { return m_readEnbPort; }
    PortItem* getClkPort() const { return m_clkPort; }
    void setValue(bool value);              // repaints when the value changes
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

    // A display made for an output port of compiled code shows the port's name (see SourceItem::name)
    const QString& name() const { return m_name; }
    void setName(const QString& name);

private:
    void layoutPorts();

    DisplayProps m_props;
    QString m_name;
    int m_value = 0;
    QList<PortItem*> m_inputPorts;
    QList<PortItem*> m_outputPorts;
};

// A submodule instance: a box with the instance and module name, input port pins on the left and
// output port pins on the right, each port labelled. Double-click opens it (see CircuitScene).
class BlockItem : public ComponentItem {
public:
    explicit BlockItem(const BlockProps& props, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    ItemProps props() const override { return m_props; }
    EditResult setProps(const ItemProps& props) override;     // always refused
    QList<PortItem*> ports() const override { return m_inputPorts + m_outputPorts; }

    const BlockProps& blockProps() const { return m_props; }
    QList<PortItem*> getInputPorts() const { return m_inputPorts; }
    QList<PortItem*> getOutputPorts() const { return m_outputPorts; }

    static constexpr double PinSpacing = 12;

private:
    BlockProps m_props;
    QList<PortItem*> m_inputPorts;
    QList<PortItem*> m_outputPorts;
};

// A port of the module whose contents are shown (inside a block): an input port drives its bits
// into the module from the left, an output port collects them on the right. Read-only.
class TerminalItem : public QGraphicsObject {
public:
    TerminalItem(const QString& name, bool output, int width, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override { return m_rect.adjusted(-2, -2, 2, 2); }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<PortItem*> pins() const { return m_pins; }   // outputs for an input port, inputs for an output port
    const QString& name() const { return m_name; }      // the module port's name

private:
    QString m_name;
    bool m_output;
    QRectF m_rect;
    QList<PortItem*> m_pins;
};

//===================== 3. Canvas colors ========================

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
    QColor block{0xf0, 0xab, 0xfc};          // submodule blocks and the module ports inside them

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
