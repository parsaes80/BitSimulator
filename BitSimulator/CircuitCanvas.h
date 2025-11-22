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
    void addDisplay(int numIn,int numOut, QPointF position);
    void startWireConnection(QPointF startPoint);
    void updateWireConnection(QPointF currentPoint);
    void finishWireConnection(QPointF endPoint);

    void startSim();

    QList<bool> getSrcValuesBool() const {
        return std::holds_alternative<QList<bool>>(m_srcValues)?std::get<QList<bool>>(m_srcValues):QList<bool>{false, true};}

    QList<int> getSrcValuesInt() const {
        return std::holds_alternative<QList<int>>(m_srcValues)?std::get<QList<int>>(m_srcValues): QList<int>{0, 1}; }

    void setSrcValues(const QList<bool>& values){m_srcValues = values;}
    void setSrcValues(const QList<int>& values){m_srcValues = values;}

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override {
        painter->fillRect(rect, QColor(10, 100, 100));};// Draw background color

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
public slots:
    void setNextGateType(GType gatetype) {m_nextItem = gatetype;};
    void setNextSource() {m_nextItem = true;  };// Use true to represent source
    void setNextDisplay() {m_nextItem = false;  };// Use false to represent Display
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

    std::variant<GType, RType, MType,bool> m_nextItem = GType::AND;  // bool true for source false for Display
    int m_numInputs = 2,m_numOutputs = 2;
    bool m_hasEnable = true, m_isFlipFlop = true;
    std::variant<QList<bool>,QList<int>> m_srcValues = QList<bool>{ false, true };
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
