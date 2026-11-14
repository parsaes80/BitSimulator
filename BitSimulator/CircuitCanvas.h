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
    void drawBackground(QPainter* painter, const QRectF& rect) override;   // the canvas color and a dot grid

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
    QList<QPointer<SourceItem>> m_sources;   // they show their value at the tick the simulator reports
    std::vector<u8> m_lastNetValues; // last applied net values; only changed nets are pushed to wires
};

class CircuitCanvas : public QGraphicsView {
    Q_OBJECT
    // The canvas colors. BitSimulator.ui sets them in its style sheet, e.g.
    //   CircuitCanvas { qproperty-wireColor: #c3ced4; }
    // They are shared by every canvas and by the toolbar icons (see CanvasTheme.h).
    Q_PROPERTY(QColor canvasColor READ canvasColor WRITE setCanvasColor)
    Q_PROPERTY(QColor dotColor READ dotColor WRITE setDotColor)
    Q_PROPERTY(double componentTint READ componentTint WRITE setComponentTint)
    Q_PROPERTY(QColor componentFill READ componentFill WRITE setComponentFill)
    Q_PROPERTY(QColor componentText READ componentText WRITE setComponentText)
    Q_PROPERTY(QColor andColor READ andColor WRITE setAndColor)
    Q_PROPERTY(QColor orColor READ orColor WRITE setOrColor)
    Q_PROPERTY(QColor xorColor READ xorColor WRITE setXorColor)
    Q_PROPERTY(QColor notColor READ notColor WRITE setNotColor)
    Q_PROPERTY(QColor muxColor READ muxColor WRITE setMuxColor)
    Q_PROPERTY(QColor registerColor READ registerColor WRITE setRegisterColor)
    Q_PROPERTY(QColor sourceColor READ sourceColor WRITE setSourceColor)
    Q_PROPERTY(QColor displayColor READ displayColor WRITE setDisplayColor)
    Q_PROPERTY(QColor wireColor READ wireColor WRITE setWireColor)
    Q_PROPERTY(QColor wireHighColor READ wireHighColor WRITE setWireHighColor)
    Q_PROPERTY(QColor wireLowColor READ wireLowColor WRITE setWireLowColor)
    Q_PROPERTY(QColor pinColor READ pinColor WRITE setPinColor)
    Q_PROPERTY(QColor pinWiredColor READ pinWiredColor WRITE setPinWiredColor)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor)
    Q_PROPERTY(QColor dragColor READ dragColor WRITE setDragColor)
    Q_PROPERTY(QColor snapColor READ snapColor WRITE setSnapColor)
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
    void setThemeColor(QColor CanvasTheme::* field, const QColor& color);
    QColor canvasColor() const { return canvasTheme().background; }
    void setCanvasColor(const QColor& color) { setThemeColor(&CanvasTheme::background, color); }
    double componentTint() const { return canvasTheme().componentTint; }
    void setComponentTint(double tint) { canvasTheme().componentTint = tint; m_scene->update(); }
    QColor dotColor() const { return canvasTheme().dot; }
    void setDotColor(const QColor& color) { setThemeColor(&CanvasTheme::dot, color); }
    QColor componentFill() const { return canvasTheme().componentFill; }
    void setComponentFill(const QColor& color) { setThemeColor(&CanvasTheme::componentFill, color); }
    QColor componentText() const { return canvasTheme().componentText; }
    void setComponentText(const QColor& color) { setThemeColor(&CanvasTheme::componentText, color); }
    QColor andColor() const { return canvasTheme().andGate; }
    void setAndColor(const QColor& color) { setThemeColor(&CanvasTheme::andGate, color); }
    QColor orColor() const { return canvasTheme().orGate; }
    void setOrColor(const QColor& color) { setThemeColor(&CanvasTheme::orGate, color); }
    QColor xorColor() const { return canvasTheme().xorGate; }
    void setXorColor(const QColor& color) { setThemeColor(&CanvasTheme::xorGate, color); }
    QColor notColor() const { return canvasTheme().notGate; }
    void setNotColor(const QColor& color) { setThemeColor(&CanvasTheme::notGate, color); }
    QColor muxColor() const { return canvasTheme().mux; }
    void setMuxColor(const QColor& color) { setThemeColor(&CanvasTheme::mux, color); }
    QColor registerColor() const { return canvasTheme().reg; }
    void setRegisterColor(const QColor& color) { setThemeColor(&CanvasTheme::reg, color); }
    QColor sourceColor() const { return canvasTheme().source; }
    void setSourceColor(const QColor& color) { setThemeColor(&CanvasTheme::source, color); }
    QColor displayColor() const { return canvasTheme().display; }
    void setDisplayColor(const QColor& color) { setThemeColor(&CanvasTheme::display, color); }
    QColor wireColor() const { return canvasTheme().wire; }
    void setWireColor(const QColor& color) { setThemeColor(&CanvasTheme::wire, color); }
    QColor wireHighColor() const { return canvasTheme().wireHigh; }
    void setWireHighColor(const QColor& color) { setThemeColor(&CanvasTheme::wireHigh, color); }
    QColor wireLowColor() const { return canvasTheme().wireLow; }
    void setWireLowColor(const QColor& color) { setThemeColor(&CanvasTheme::wireLow, color); }
    QColor pinColor() const { return canvasTheme().pin; }
    void setPinColor(const QColor& color) { setThemeColor(&CanvasTheme::pin, color); }
    QColor pinWiredColor() const { return canvasTheme().pinWired; }
    void setPinWiredColor(const QColor& color) { setThemeColor(&CanvasTheme::pinWired, color); }
    QColor selectionColor() const { return canvasTheme().selection; }
    void setSelectionColor(const QColor& color) { setThemeColor(&CanvasTheme::selection, color); }
    QColor dragColor() const { return canvasTheme().drag; }
    void setDragColor(const QColor& color) { setThemeColor(&CanvasTheme::drag, color); }
    QColor snapColor() const { return canvasTheme().snap; }
    void setSnapColor(const QColor& color) { setThemeColor(&CanvasTheme::snap, color); }

    CircuitScene* m_scene;
    bool m_ctrlPressed = false;
    bool m_shiftPressed = false;
    bool m_middleMousePressed = false;
    QPoint m_lastPanPoint;
    QPoint m_panStartPoint;   // to tell a click on empty canvas (deselect) from a drag (pan)
};
