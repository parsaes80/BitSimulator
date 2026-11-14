#pragma once
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <QGraphicsSceneMouseEvent>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDebug>
#include <QKeyEvent>
#include <expected>
#include "general.h"
#include "ItemProps.h"
#include <QColor>
#include <algorithm>
#include <cmath>
#include "general.h"

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
