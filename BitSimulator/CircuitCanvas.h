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

// Gate item for the scene
class GateItem : public QGraphicsItem {
public:
    explicit GateItem(GType gateType, QGraphicsItem* parent = nullptr);

    // Required virtual functions
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // Custom interaction
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

    // Gate-specific methods
    GType getGateType() const;
    QPointF getInputPin(int index) const;
    QPointF getOutputPin() const;

private:
    void drawPins(QPainter* painter);
    QString gateTypeToString() const;

    GType m_gateType;
    QRectF m_rect;
};

// Wire item for connections
class WireItem : public QGraphicsLineItem {
public:
    explicit WireItem(const QLineF& line, QGraphicsItem* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

// Custom scene for circuit editing
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

signals:
    void gateAdded(GateItem* gate);
    void wireAdded(WireItem* wire);
};

// Main widget combining scene and view
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