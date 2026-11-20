#pragma once
// Component properties and the canvas items that draw them (SourceProps is in general.h)

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
#include "Theme.h"
#include "general.h"

//===================== 1. Component properties ========================

// Also the defaults of the tool that places new components, so one editor serves both

struct GateProps {
    GType type = GType::AND;
    int numInputs = 2;                 // always 1 for NOT
    bool operator==(const GateProps&) const = default;
};

struct RegisterProps {
    RType type = RType::D;
    bool edgeTriggered = true;         // false: latch
    bool hasEnable = true;
    // D and T: the second pin resets to resetValue on the clock edge, or at once with asyncReset
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

// Pins are consecutive, lowest bit first
struct BlockPort {
    QString name;
    bool output = false;
    int width = 1;
    bool operator==(const BlockPort&) const = default;
};

// A submodule instance; double-click opens it, nothing to edit
struct BlockProps {
    QString instance;               // e.g. "alu"
    QString module;                 // e.g. "ALU"
    QList<BlockPort> ports;         // by name; inputs left, outputs right
    bool operator==(const BlockProps&) const = default;
};

using ItemProps = std::variant<GateProps, RegisterProps, MuxProps, SourceProps, DisplayProps, BlockProps>;

// Index of T in ItemProps
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

inline int muxSelectLines(int numInputs)
{
    return std::max(1, static_cast<int>(std::bit_width(static_cast<unsigned>(std::max(numInputs, 2) - 1))));
}

// A consistent state: a NOT has one input, a waveform has one pin, ...
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
    using namespace Qt::StringLiterals;
    switch (type) {
    case GType::AND:  return u"AND"_s;
    case GType::OR:   return u"OR"_s;
    case GType::XOR:  return u"XOR"_s;
    case GType::NAND: return u"NAND"_s;
    case GType::NOR:  return u"NOR"_s;
    case GType::XNOR: return u"XNOR"_s;
    case GType::NOT:  return u"NOT"_s;
    }
    return {};
}

inline QString registerTypeName(RType type)
{
    using namespace Qt::StringLiterals;
    switch (type) {
    case RType::SR: return u"SR"_s;
    case RType::JK: return u"JK"_s;
    case RType::D:  return u"D"_s;
    case RType::T:  return u"T"_s;
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

// e.g. "NAND gate", "D flip-flop"
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

// Body size (selection outline excluded); dot gets these so layouts fit. name: a compiled port's
QSizeF componentSize(const ItemProps& props, const QString& name = {});
QSizeF terminalSize(const QString& name, int width);

// Fills `body` with the gate's shape (and bubble); used on the canvas and the toolbar
void drawGateSymbol(QPainter* painter, GType type, const QRectF& body);

//===================== 2. Canvas items ========================

// Items draw run values (running or paused); GUI thread only
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
    void setValue(bool value);

private:
    bool m_value = false;
    bool m_highlighted = false;
    PortType m_portType;
    QList<WireItem*> m_connections;
};

// Base of everything placed and wired on the canvas
class ComponentItem : public QGraphicsObject {
    Q_OBJECT
public:
    // Error: why the edit was refused (nothing changed)
    using EditResult = std::expected<void, QString>;

    // Room for the selection outline; clicks only count on the body
    QRectF boundingRect() const override { return m_rect.adjusted(-PaintMargin, -PaintMargin, PaintMargin, PaintMargin); }
    QPainterPath shape() const override { QPainterPath body; body.addRect(m_rect.adjusted(-2, -2, 2, 2)); return body; }

    virtual ItemProps props() const = 0;
    // Refused if it would remove a wired pin
    virtual EditResult setProps(const ItemProps& props) = 0;
    virtual QList<PortItem*> ports() const = 0;

    bool hasConnections() const;

protected:
    explicit ComponentItem(QGraphicsItem* parent = nullptr);

    void drawSelection(QPainter* painter, const QStyleOptionGraphicsItem* option) const;
    static constexpr qreal SelectionMargin = 5;
    static constexpr qreal PaintMargin = SelectionMargin + 1;

    // Refuses if any of ports[newCount..] is wired; `what` names them, e.g. "Input"
    static EditResult checkRemovable(const QList<PortItem*>& ports, int newCount, const QString& what);
    static EditResult checkRemovable(const PortItem* port, bool stillWanted, const QString& what);
    // Existing pins and their wires are kept
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

    // The tick whose value it shows; 0 when not running
    u64 tick() const { return m_tick; }
    void setTick(u64 tick);
    u64 value() const { return sourceValueAt(m_props, m_tick); }

    // Compiled inputs show their port name; hand-placed ones have none
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
    QPainterPath shape() const override;   // widened, so a click near it selects it

    void setStartPort(PortItem* port);
    void setEndPort(PortItem* port);
    PortItem* getStartPort() const { return m_startPort; }
    PortItem* getEndPort() const { return m_endPort; }
    bool isConnected() const { return m_startPort && m_endPort; }

    void setEnds(QPointF start, QPointF end);   // while dragged, in scene coordinates
    void setValue(bool value);
    QPen pen() const { return m_pen; }
    void setPen(const QPen& pen){ m_pen = pen;update();}

public:
    void updateWirePosition();

private:
    bool m_value = false;
    PortItem* m_startPort = nullptr;
    PortItem* m_endPort = nullptr;
    QPainterPath path() const;   // horizontal, vertical, horizontal

    QPointF localStartPos;
    QPointF localEndPos;
    QPen m_pen;
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
    void setValue(bool value);
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
    u64 getValue() const;                                  // the lowest 64 bits
    const QString& valueText() const { return m_text; }   // decimal, any width

    // See SourceItem::name
    const QString& name() const { return m_name; }
    void setName(const QString& name);

private:
    void layoutPorts();

    DisplayProps m_props;
    QString m_name;
    std::vector<bool> m_bits;          // lowest first; the top pin is bit 0
    QString m_text{QChar(u'0')};
    QList<PortItem*> m_inputPorts;
    QList<PortItem*> m_outputPorts;
};

// Instance and module name, inputs left, outputs right; double-click opens it
class BlockItem : public ComponentItem {
public:
    explicit BlockItem(const BlockProps& props, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    ItemProps props() const override { return m_props; }
    EditResult setProps(const ItemProps& props) override;
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

// A port of the module shown inside a block: inputs on the left, outputs on the right
class TerminalItem : public QGraphicsObject {
public:
    TerminalItem(const QString& name, bool output, int width, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override { return m_rect.adjusted(-2, -2, 2, 2); }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<PortItem*> pins() const { return m_pins; }   // outputs for an input port, inputs for an output
    const QString& name() const { return m_name; }

private:
    QString m_name;
    bool m_output;
    QRectF m_rect;
    QList<PortItem*> m_pins;
};
