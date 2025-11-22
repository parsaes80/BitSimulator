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
    void removeConnections(WireItem* wire);

    QList<WireItem*> getConnections() const { return m_connections; }
    QGraphicsObject* getParent() const { return dynamic_cast<QGraphicsObject*>(parentItem()); };
    void setHighlighted(bool highlighted) {m_highlighted = highlighted;update();};
    bool getValue() const { return m_value; };
    void setValue(bool value) { m_value = value; };

private:
    bool m_value = false;
    int m_pinIndex = 0;
    bool m_highlighted = false;
    PortType m_portType;
    QList<WireItem*> m_connections;
};

class GateItem : public QGraphicsObject{
    Q_OBJECT
public:
    explicit GateItem( GType gateType,int numInputs,QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override { return m_rect.adjusted(-2, -2, 2, 2); };
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QString gateTypeToString() const;

    GType getGateType() const { return m_gateType; };
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
    void createPorts();

    GType m_gateType;
    QRectF m_rect;

    Direction m_direction = Direction::RIGHT;

    int m_numInputs = 2;
    QVector<PortItem*> m_inputPorts;
    PortItem* m_outputPort = nullptr;
};

class SourceItem : public QGraphicsObject {
    Q_OBJECT
public:
    SourceItem(QGraphicsItem* parent = nullptr); // Change parameter type
    SourceItem(const QList<bool>& cycleValues, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override { return m_rect.adjusted(-2, -2, 2, 2); };
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QList<PortItem*> getOutputPorts() const { return m_outPorts; };
    u8 getIdx() const { return m_currIdx; };
    void setIdx(u8 value) { m_currIdx = value; };

    QList<bool> getValues() const { return m_cycleValues; };
    void setValues(QList<bool> values) { m_cycleValues = values; };
private:
    void addPorts();

    u8 m_currIdx;
    QList<bool> m_cycleValues;

    QList<PortItem*> m_outPorts;
    int m_numOutputs = 1;

    QRectF m_rect;
};

class WireItem : public QGraphicsObject
{
    Q_OBJECT
public:
    WireItem(QPointF startpos, QPointF endpos, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

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

class RegisterItem : public QGraphicsObject {
    //Q_OBJECT
public:
    explicit RegisterItem(RType RegType, QGraphicsItem* parent= nullptr);

    QRectF boundingRect() const override { return m_rect.adjusted(-2, -2, 2, 2); };
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    RType getRegType() const { return m_regType; };
    PortItem* getInputPort() const { return m_inputPortOne; };
    PortItem* getInputPortTwo() const { return m_inputPortTwo; };
    PortItem* getOutputPort() const { return m_outputPort; }
    PortItem* getReadEnablePort() const { return m_readEnbPort; }
    PortItem* getClkPort() const { return m_clkPort; }
    void setValue(bool value) { m_value = value;};
    //void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void createPorts();
    bool m_value = false;

    RType m_regType;
    QRectF m_rect;

    Direction m_direction = Direction::RIGHT;

    PortItem* m_inputPortOne = nullptr; // S,J,D,T
    PortItem* m_inputPortTwo = nullptr; // R,K,R,R

    PortItem* m_clkPort = nullptr;
    PortItem* m_readEnbPort = nullptr;

    PortItem* m_outputPort = nullptr;
};

class MuxItem : public QGraphicsObject {
public:
    explicit MuxItem(MType MuxType, QGraphicsItem* parent= nullptr);
    QRectF boundingRect() const override { return m_rect.adjusted(-2, -2, 2, 2); };
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<PortItem*> getInputPorts() const { return m_inputDataPorts; }
    QList<PortItem*> getAddressPorts() const { return m_inputAddressPorts; }
    PortItem* getOutputPort() const { return m_outputPort; }

private:
    void createPorts();

    MType m_muxType;
    QRectF m_rect;

    QList<PortItem*> m_inputDataPorts;
    QList<PortItem*> m_inputAddressPorts;
    PortItem* m_outputPort = nullptr;
};

class DisplayItem: public QGraphicsObject{
public:
    explicit DisplayItem(int numInputs,int numOutputs, QGraphicsItem* parent= nullptr);
    QRectF boundingRect() const override { return m_rect.adjusted(-2, -2, 2, 2); };
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<PortItem*> getInputPorts() const { return m_inputPorts; }
    QList<PortItem*> getOutputPorts() const { return m_outputPorts; }
    void updateValue();
private:
    void createPorts();

    QRectF m_rect;

    int m_value = 0;
    int m_numInputs;
    int m_numOutputs;
    QList<PortItem*> m_inputPorts;
    QList<PortItem*> m_outputPorts;
};
