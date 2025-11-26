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
#include <variant>
#include "general.h"
#include "Items.h"
#include "hdlcompiler.h"

class CircuitScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit CircuitScene(QObject* parent = nullptr);

    QGraphicsObject* addGate(GType gateType,int numIn, QPointF position);
    QGraphicsObject* addSource(QPointF position);
    QGraphicsObject* addSource(std::variant<QList<bool>,QList<int>> srcValues,int numOut,QPointF position);
    QGraphicsObject* addRegister(RType RegType, QPointF position);
    QGraphicsObject* addRegister(RType RegType,bool hasEnable, QPointF position);
    QGraphicsObject* addMux(MType MuxType,int numIn, QPointF position);
    QGraphicsObject* addDisplay(int numIn,int numOut, QPointF position);
    void startWireConnection(QPointF startPoint);
    void updateWireConnection(QPointF currentPoint);
    void finishWireConnection(QPointF endPoint);

    void startSim();

    std::variant<QList<bool>,QList<int>> getSrcValues() const {return m_srcValues;}
    int getNumInputs() const { return m_numInputs; }
    int getNumOutputs() const { return m_numOutputs; }
    bool getHasEnable() const { return m_hasEnable; }
    bool getIsFlipFlop() const { return m_isFlipFlop; }
    bool getSrcIsWave() const { return std::holds_alternative<QList<bool>>(m_srcValues);}
    RType getNextRegisterType() const {
        if (std::holds_alternative<RType>(m_nextItem)) {
            return std::get<RType>(m_nextItem);
        }
        return RType::SR;
    }
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
    void setSrcValues(const QList<bool>& values){m_srcValues = values;}
    void setSrcValues(const QList<int>& values){m_srcValues = values;}
    void setNumInputs(const int value){m_numInputs = value;}
    void setNumOutputs(const int value){m_numOutputs = value;}
    void setHasEnable(const bool value){m_hasEnable = value;}
    void setIsFlipFlop(const bool value){m_isFlipFlop = value;}
    void receiveResult(SimResult result);
    void receiveGraph(QHash<QString,Node> graph);
signals:
    void startSimSIG(ExportGraph graph);
private:
    void clearHighlights();
    PortItem* getPortFromItem(QGraphicsObject* item, PortType type);

    bool m_connectingWire;
    QPointF m_wireStartPoint;
    WireItem* m_currWire = nullptr;
    PortItem* m_currWireStartPort = nullptr;
    PortItem* findNearestPort(const QPointF& scenePos, double threshold = 15.0);

    std::variant<GType, RType, MType,bool> m_nextItem = GType::AND;  // bool true for source false for Display
    std::variant<QList<bool>,QList<int>> m_srcValues = QList<int>{ 2, 5 };
    int m_numInputs = 2,m_numOutputs = 2;
    bool m_hasEnable = true, m_isFlipFlop = true;
};

class CircuitCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit CircuitCanvas(QWidget* parent = nullptr);
    void clearCanvas() {m_scene->clear();};
    CircuitScene* getScene() const { return m_scene; }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    CircuitScene* m_scene;
    bool m_ctrlPressed = false;
    bool m_shiftPressed = false;
    bool m_middleMousePressed = false;
    QPoint m_lastPanPoint;
};
