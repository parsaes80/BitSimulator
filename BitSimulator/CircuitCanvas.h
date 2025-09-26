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
#include "components.h"
class WireItem;
class GateItem;

class PortItem : public QGraphicsEllipseItem
{
public:
    PortItem(PortType portType, int pinIndex = 0, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // Port connection methods
    PortType getPortType() const { return m_portType; }
    int getPinIndex() const { return m_pinIndex; }
    bool canConnectTo(PortItem* otherPort) const;

    // Connection tracking
    void addConnection(WireItem* wire);
    void removeConnection(WireItem* wire);
    QList<WireItem*> getConnections() const { return m_connections; }

    // Visual feedback
    void setHighlighted(bool highlighted);

private:
    PortType m_portType;
    int m_pinIndex; // Which input pin this is (0, 1, etc.) or -1 for output
    QList<WireItem*> m_connections;
    bool m_highlighted = false;
};

class GateItem : public QGraphicsItem {
public:
    explicit GateItem(GType gateType, QGraphicsItem* parent = nullptr);

    // Required virtual functions
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // Custom interaction
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    QString gateTypeToString() const;

    // Getters
    GType getGateType() const;
    QPointF getInputPin(int index) const;
    QPointF getOutputPin() const;

    QList<PortItem*> getInputPorts() const { return m_inputPorts; }
    PortItem* getOutputPort() const { return m_outputPort; }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
private:
    void drawPins(QPainter* painter);
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
    double padding = 0;

    QList<PortItem*> m_inputPorts;
    PortItem* m_outputPort = nullptr;
};

class WireItem : public QGraphicsLineItem {
public:
    WireItem(const QLineF& line, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    //QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

    // Port connection methods
    void setStartPort(PortItem* port);
    void setEndPort(PortItem* port);
    PortItem* getStartPort() const { return m_startPort; }
    PortItem* getEndPort() const { return m_endPort; }

    bool isConnected() const { return m_startPort && m_endPort; }

private:
    PortItem* m_startPort = nullptr;
    PortItem* m_endPort = nullptr;
};

class CircuitScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit CircuitScene(QObject* parent = nullptr);

    // Add components
    void addGate(GType gateType, QPointF position);
    void startWireConnection(QPointF startPoint);
    void updateWireConnection(QPointF currentPoint);
    void finishWireConnection(QPointF endPoint);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    bool m_connectingWire;
    QPointF m_wireStartPoint;
    WireItem* m_currentWire;
    PortItem* m_currentWireStartPort = nullptr;
    PortItem* findNearestPort(const QPointF& scenePos, double threshold = 15.0);
signals:
    void gateAdded(GateItem* gate);
    void wireAdded(WireItem* wire);
};

class CircuitCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit CircuitCanvas(QWidget* parent = nullptr);

    // Public interface
    void addGate(GType gateType, QPoint position);
    void clearCanvas();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onGateAdded(GateItem* gate);
    void onWireAdded(WireItem* wire);

private:
    CircuitScene* m_scene;

signals:
    void circuitChanged();
};
