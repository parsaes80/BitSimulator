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
#include "Items.h"

class CircuitScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit CircuitScene(QObject* parent = nullptr);

    void addGate(GType gateType, QPointF position);
    void addSource(QPointF position);
    void startWireConnection(QPointF startPoint);
    void updateWireConnection(QPointF currentPoint);
    void finishWireConnection(QPointF endPoint);
    void startSim() { qDebug() << "startsim"; };

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
public slots:
    void setNextGateType(GType gatetype)
    {
        nextSource = false;
        nextGateType = gatetype;
    };
    void setNextSource() { nextSource = true; };

private:
    bool m_connectingWire;
    QPointF m_wireStartPoint;
    WireItem* m_currentWire;
    PortItem* m_currentWireStartPort = nullptr;
    PortItem* findNearestPort(const QPointF& scenePos, double threshold = 15.0);
    void clearHighlights(); // Add this method

    GType nextGateType = GType::AND;
    bool nextSource = false;
};

class CircuitCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit CircuitCanvas(QWidget* parent = nullptr);

    // Public interface
    void addGate(GType gateType, QPoint position) {m_scene->addGate(gateType, mapToScene(position));};
    void clearCanvas() {m_scene->clear();};
    
    CircuitScene* getScene() const { return m_scene; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    CircuitScene* m_scene;
    
    // Camera dragging
    bool m_middleMousePressed = false;
    QPoint m_lastPanPoint;
};
