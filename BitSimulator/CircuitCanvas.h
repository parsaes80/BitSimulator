#pragma once
#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <array>
#include <functional>
#include <unordered_map>
#include <vector>
#include "Items.h"
#include "hdlcompiler.h"

// A signal the waveforms can show: an input or output of the circuit (a source or display), or a
// port of a block instance. Internal wires are not signals.
struct WaveSignal {
    static constexpr u32 AlwaysOne = 0xFFFFFFFF;   // in `nets`: a bit tied to a constant 1

    QString scope;            // empty for the circuit itself, else the instance path, e.g. "core/alu"
    QString module;           // the module of that instance; empty for the circuit itself
    QString name;             // the source or display's name, or the port's name
    bool output = false;      // a display, or an output port of its module
    std::vector<u32> nets;    // the simulator net of each bit, lowest bit first; 0 = always 0

    int width() const { return int(nets.size()); }
};

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
    // Every source and display has a name: a compiled one the port's name from the code, one placed
    // by hand an automatic one ("Source 3", the lowest number no other source has) until renamed.
    // An empty name gives it an automatic one again. Refused for other components.
    ComponentItem::EditResult renameComponent(ComponentItem* item, const QString& name);
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
    // The signals of the current run, made when it starts: the circuit's sources and displays, then
    // every port of every block instance (blocks inside blocks too). Kept until the next run starts.
    const QList<WaveSignal>& waveSignals() const { return m_waveSignals; }
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
public:
    void startOrResumeSim();   // Stopped: build the circuit and run it from tick 0. Paused: continue.
    void pauseSim();           // freeze; values stay on screen
    void stopSim();            // end the run and return to the editing view

    void setToolProps(const ItemProps& props);   // makes that kind of component the active tool
    void selectTool(std::size_t kind);           // kind = itemKind<...>; keeps that tool's defaults
    void selectGateTool(GType gateType);
    void receiveResult(const SimResult& result);   // applies a result now
    // For the simulator's results: keeps only the newest and applies it at the next screen frame. At
    // fast speeds the simulator sends far more results than the screen can show; applying every one
    // would keep the GUI thread busy with values nobody sees, and leave painting and input waiting.
    void queueResult(const SimResult& result);
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
    // Fills m_waveSignals and graph.recordedNets, once the nets have their final numbers
    void buildWaveSignals(ExportGraph& graph, const QList<BlockItem*>& blocks);
    void setSimState(SimState state);
    void circuitEdited();              // call after every structural change
    QString automaticName(const ComponentItem* item) const;   // "Source 3" / "Display 1"; empty for other kinds
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
    QList<WaveSignal> m_waveSignals;
    std::vector<u8> m_lastNetValues; // last applied net values; only changed nets are pushed to wires
    std::vector<bool> m_lastRegisterValues;

    // Frame pacing for queueResult: the newest result not yet applied, and when the last one was
    SimResult m_pendingResult;
    bool m_hasPendingResult = false;
    QTimer m_frameTimer;               // single shot: fires at the next frame to apply m_pendingResult
    QElapsedTimer m_sinceLastFrame;
    void applyPendingResult();

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

    CircuitScene* m_scene;
    QList<Level> m_levels;            // opened blocks, outermost first
    QFrame* m_breadcrumb = nullptr;   // "top > core (RISCV) > alu (ALU)" over the top-left corner
    // Dragging empty canvas with the left button pans it
    bool m_panning = false;
    QPoint m_lastPanPoint;
    QPoint m_panStartPoint;   // to tell a click on empty canvas (deselect) from a drag (pan)
};
