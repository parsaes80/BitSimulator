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
#include <variant>

class CircuitScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit CircuitScene(QObject* parent = nullptr);

    void addGate(GType gateType, QPointF position);
    void addSource(QPointF position);
    void addRegister(RType RegType, QPointF position);
    void addMux(MType MuxType, QPointF position);
    void startWireConnection(QPointF startPoint);
    void updateWireConnection(QPointF currentPoint);
    void finishWireConnection(QPointF endPoint);

    void startSim();

    QList<bool> getSrcCycleValues() const {return m_nextSrcCycleValues;};
    void setSrcCycleValues(QList<bool>& input){m_nextSrcCycleValues = input;}

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override {
        painter->fillRect(rect, QColor(10, 100, 100));};// Draw background color

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
public slots:
    void setNextGateType(GType gatetype) {m_nextItem = gatetype;};
    void setNextSource() {m_nextItem = true;  };// Use bool to represent source
    void setNextRegister(RType regType) {m_nextItem = regType;};
    void setNextMux(MType muxType) {m_nextItem = muxType;};

    void receiveResult(SimResult result);
signals:
    void startSimSIG(ExportGraph graph);
private:
    void clearHighlights();

    bool m_connectingWire;
    QPointF m_wireStartPoint;
    WireItem* m_currWire = nullptr;
    PortItem* m_currWireStartPort = nullptr;
    PortItem* findNearestPort(const QPointF& scenePos, double threshold = 15.0);

    std::variant<GType, RType, MType,bool> m_nextItem = GType::AND;  // bool for source

    QList<bool> m_nextSrcCycleValues = { false,true };
};

class CircuitCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit CircuitCanvas(QWidget* parent = nullptr);

    void addGate(GType gateType, QPoint position) {m_scene->addGate(gateType, mapToScene(position));};
    void clearCanvas() {m_scene->clear();};
    
    CircuitScene* getScene() const { return m_scene; }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    CircuitScene* m_scene;
    bool m_ctrlPressed = false;
    bool m_shiftPressed = false;
    bool m_middleMousePressed = false;
    QPoint m_lastPanPoint;
};
