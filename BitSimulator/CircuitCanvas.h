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
#include <array>
#include <variant>
#include <vector>
#include <QPointer>
#include "general.h"
#include "Items.h"
#include "hdlcompiler.h"

// Stopped: editing view, no values shown.
// Running: live values; the circuit is read-only.
// Paused:  values frozen on screen. The circuit can be edited, but any edit ends the paused run.
enum class SimState { Stopped, Running, Paused };

class CircuitScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit CircuitScene(QObject* parent = nullptr);
    ~CircuitScene() override;

    SimState simState() const { return m_simState; }
    bool isLocked() const { return m_simState == SimState::Running; }   // no structural edits while running

    // Every change to the circuit's structure goes through the functions below. They do nothing
    // while the simulation is running, and they end a paused run (its values no longer apply).

    // Creates the component described by `props` and places it on the canvas (null while running).
    ComponentItem* addComponent(const ItemProps& props, QPointF position);
    // Changes a placed component; refused with a reason if a wired pin would disappear.
    ComponentItem::EditResult editComponent(ComponentItem* item, const ItemProps& props);
    // Removes the selected wires, and the selected components that have no wires left.
    void deleteSelection();
    // Stops the simulation and empties the canvas.
    void resetCircuit();
    void startWireConnection(QPointF startPoint);
    void updateWireConnection(QPointF currentPoint);
    void finishWireConnection(QPointF endPoint);

    // The active tool: what a middle click places next, and with which properties.
    // Each kind of component remembers its own defaults while another tool is active.
    const ItemProps& toolProps() const { return m_toolDefaults[m_activeTool]; }
protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override {
        painter->fillRect(rect, QColor(10, 100, 100));};

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
public slots:
    void startOrResumeSim();   // Stopped: build the circuit and run it from tick 0. Paused: continue.
    void pauseSim();           // freeze; values stay on screen
    void stopSim();            // end the run and return to the editing view

    void setToolProps(const ItemProps& props);   // makes that kind of component the active tool
    void selectTool(std::size_t kind);           // kind = itemKind<...>; keeps that tool's defaults
    void selectGateTool(GType gateType);
    void receiveResult(const SimResult& result);
    void receiveGraph(QHash<QString,Node> graph);
signals:
    // To the simulator (queued across threads, so they arrive in this order)
    void startSimSIG(ExportGraph graph);
    void setSimRunningSIG(bool running);
    void stopSimSIG();

    void simStateChanged(SimState state);
    void toolChanged();
private:
    ExportGraph buildGraph();          // numbers the nets and fills the global wire map
    void setSimState(SimState state);
    void circuitEdited();              // call after every structural change
    void cancelWireConnection();       // drop a wire that is still being dragged
    void resetValues();                // clear the values the last run left on the items

    void clearHighlights();
    PortItem* getPortFromItem(QGraphicsObject* item, PortType type);

    SimState m_simState = SimState::Stopped;
    u32 m_runId = 0;                   // bumped for every new run and every stop; older results are ignored

    bool m_connectingWire;
    QPointF m_wireStartPoint;
    WireItem* m_currWire = nullptr;
    PortItem* m_currWireStartPort = nullptr;
    PortItem* findNearestPort(const QPointF& scenePos, double threshold = 15.0);

    // One entry per alternative of ItemProps, in the same order (m_toolDefaults[i].index() == i)
    std::array<ItemProps, itemKindCount> m_toolDefaults{GateProps{}, RegisterProps{}, MuxProps{}, SourceProps{}, DisplayProps{}};
    std::size_t m_activeTool = itemKind<GateProps>;

    // Cached per simulation run (rebuilt in startSim) so receiveResult avoids scanning items()
    QList<QPointer<DisplayItem>> m_displays;
    std::vector<u8> m_lastNetValues; // last applied net values; only changed nets are pushed to wires
};

class CircuitCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit CircuitCanvas(QWidget* parent = nullptr);
    void clearCanvas() {m_scene->resetCircuit();};
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
    QPoint m_panStartPoint;   // to tell a click on empty canvas (deselect) from a drag (pan)
};
