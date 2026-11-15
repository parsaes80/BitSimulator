#pragma once
#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPointer>
#include <array>
#include <functional>
#include <unordered_map>
#include <vector>
#include "Items.h"
#include "hdlcompiler.h"

// Stopped: editing view, no values shown.
// Running: live values; the circuit is read-only.
// Paused:  values frozen on screen. The circuit can be edited, but any edit ends the paused run.
enum class SimState { Stopped, Running, Paused };

// For one simulation run: which simulator net each wire carries, and which flip-flop on the canvas
// each simulator register is, to put the run's values back on the canvas
struct WireMap {
    std::unordered_map<u32, std::vector<WireItem*>> net2wire;   // a net can be on many wires
    std::unordered_map<WireItem*, u32> wire2net;
    std::vector<RegisterItem*> registers;                       // index = the simulator's register index

    void clear()
    {
        net2wire.clear();
        wire2net.clear();
        registers.clear();
    }
};

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

    //----- Compiled designs and their blocks -----

    // The compiled design on the canvas (empty for a drawn circuit). The canvas shows its top module;
    // each submodule instance is a block, opened in a read-only scene of its own.
    const NetDesign& design() const { return m_design; }
    // The nets of the current run (empty while stopped)
    const WireMap& wireMap() const { return m_wireMap; }
    // Builds a module of `design` as components and wires. topLevel: the module's ports become sources
    // and displays, and everything can be edited and simulated. Otherwise they become port terminals,
    // for a read-only view of the inside of a block.
    void buildModule(const NetDesign& design, const QString& moduleName, bool topLevel);
    // A scene showing the inside of a block: nothing in it can be edited or moved
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    bool isReadOnly() const { return m_readOnly; }

    // While a run is shown: the values of the instance at `path` (e.g. "core/alu"), for a read-only
    // scene showing it (`view`). Its wires and flip-flops take the values the run gives them.
    void showInstanceValues(const QString& path, CircuitScene* view) const;
    void clearValues() { resetValues(); }
protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;   // the canvas color and a dot grid

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;   // opens a block
public slots:
    void startOrResumeSim();   // Stopped: build the circuit and run it from tick 0. Paused: continue.
    void pauseSim();           // freeze; values stay on screen
    void stopSim();            // end the run and return to the editing view

    void setToolProps(const ItemProps& props);   // makes that kind of component the active tool
    void selectTool(std::size_t kind);           // kind = itemKind<...>; keeps that tool's defaults
    void selectGateTool(GType gateType);
    void receiveResult(const SimResult& result);
    void receiveDesign(const NetDesign& design);   // a compiled design replaces the circuit
signals:
    // To the simulator (queued across threads, so they arrive in this order)
    void startSimSIG(ExportGraph graph);
    void setSimRunningSIG(bool running);
    void stopSimSIG();

    void simStateChanged(SimState state);
    void toolChanged();
    void openBlock(BlockItem* block);  // a block was double-clicked
    void designChanged();              // a new design, or the circuit was cleared: the view returns to the top
    void valuesChanged();              // a run's result was applied (block views follow it)
private:
    ExportGraph buildGraph();          // numbers the nets and fills the wire map
    WireItem* addWire(PortItem* from, PortItem* to);
    // Adds the contents of every block to `graph` (see BlockExpander); pinNet gives the net on a block pin
    void expandBlocks(ExportGraph& graph, const QList<BlockItem*>& blocks, const std::function<u32(PortItem*)>& pinNet);
    void setSimState(SimState state);
    void circuitEdited();              // call after every structural change
    void cancelWireConnection();       // drop a wire that is still being dragged
    void resetValues();                // clear the values the last run left on the items

    void clearHighlights();            // of the pins a dragged wire starts at and snaps to

    SimState m_simState = SimState::Stopped;
    u32 m_runId = 0;                   // bumped for every new run and every stop; older results are ignored

    // A wire being dragged with the right mouse button
    bool m_connectingWire = false;
    QPointF m_wireStartPoint;
    WireItem* m_currWire = nullptr;
    PortItem* m_currWireStartPort = nullptr;
    PortItem* m_snapPort = nullptr;    // the pin it would connect to if released now
    PortItem* findNearestPort(const QPointF& scenePos, double threshold = 15.0);

    WireMap m_wireMap;                 // filled by buildGraph, cleared when the run ends

    // One entry per alternative of ItemProps, in the same order (m_toolDefaults[i].index() == i)
    std::array<ItemProps, itemKindCount> m_toolDefaults{GateProps{}, RegisterProps{}, MuxProps{}, SourceProps{}, DisplayProps{}, BlockProps{}};
    std::size_t m_activeTool = itemKind<GateProps>;

    // Cached per simulation run (rebuilt in startSim) so receiveResult avoids scanning items()
    QList<QPointer<DisplayItem>> m_displays;
    QList<QPointer<SourceItem>> m_sources;   // they show their value at the tick the simulator reports
    std::vector<u8> m_lastNetValues; // last applied net values; only changed nets are pushed to wires
    std::vector<bool> m_lastRegisterValues;

    // Compiled designs
    NetDesign m_design;
    bool m_readOnly = false;
    // Per simulation run, for every block instance path ("core", "core/alu"): which simulator net
    // each bit of its module became, and which simulator register each of its flip-flops is
    QHash<QString, QHash<int, u32>> m_instanceNets;
    QHash<QString, QHash<QString, u32>> m_instanceRegisters;
    // In a scene built from a module: the module bit of every wire, and the node of every flip-flop
    QHash<WireItem*, int> m_wireBits;
    QHash<RegisterItem*, QString> m_registerNodes;
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
    Q_PROPERTY(QColor blockColor READ blockColor WRITE setBlockColor)
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
    CircuitScene* getScene() const { return m_scene; }   // the circuit itself (the top level)

    //----- Blocks: the inside of a block is shown on this canvas in place of the circuit -----

    // The scene on screen: the circuit, or the inside of the innermost opened block
    CircuitScene* currentScene() const;
    int depth() const { return int(m_levels.size()); }   // opened blocks; 0 = the circuit is shown
    QString currentPath() const;                         // e.g. "core/alu"; empty at the top
    void openBlock(BlockItem* block);                    // shows its inside (a block of the scene on screen)
    void showLevel(int depth);                           // goes back out to that level (0 = the circuit)
signals:
    void depthChanged(int depth);
public:

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // An opened block: a read-only scene with the inside of its module
    struct Level {
        CircuitScene* scene = nullptr;
        QString path;                 // instance path from the top, e.g. "core/alu"
        QString title;                // e.g. "alu (ALU)"
        QTransform outerTransform;    // the zoom and position of the level outside it, to go back to
        QPointF outerCentre;
    };
    void showScene(CircuitScene* scene);
    void updateBreadcrumb();
    void refreshBlockValues();
    QPointF viewCentre() const { return mapToScene(viewport()->rect().center()); }

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
    QColor blockColor() const { return canvasTheme().block; }
    void setBlockColor(const QColor& color) { setThemeColor(&CanvasTheme::block, color); }
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
    QList<Level> m_levels;            // opened blocks, outermost first
    QFrame* m_breadcrumb = nullptr;   // "top > core (RISCV) > alu (ALU)" over the top-left corner
    // Dragging empty canvas with the left button pans it
    bool m_panning = false;
    QPoint m_lastPanPoint;
    QPoint m_panStartPoint;   // to tell a click on empty canvas (deselect) from a drag (pan)
};
