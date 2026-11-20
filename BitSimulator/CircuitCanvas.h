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
#include "CircuitFile.h"
#include "Items.h"
#include "hdlcompiler.h"

// An input or output of the circuit, or a port of a block instance
struct WaveSignal {
    static constexpr u32 AlwaysOne = 0xFFFFFFFF;   // a bit tied to 1

    QString scope;            // instance path, e.g. "core/alu"; empty at the top
    QString module;
    QString name;
    bool output = false;
    std::vector<u32> nets;    // simulator net per bit, lowest first; 0 = always 0

    int width() const { return int(nets.size()); }
};

// Paused: values frozen; any edit ends the run
enum class SimState { Stopped, Running, Paused };

// Per run: the net of each wire and the canvas item of each simulator register
struct WireMap {
    std::unordered_map<u32, std::vector<WireItem*>> net2wire;   // a net can be on many wires
    std::unordered_map<WireItem*, u32> wire2net;
    std::vector<RegisterItem*> registers;                       // by simulator register index

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
    bool isLocked() const { return m_simState == SimState::Running; }

    // All structural changes: nothing while running; an edit while paused ends the run

    // Null while running
    ComponentItem* addComponent(const ItemProps& props, QPointF position);
    // Refused with a reason if a wired pin would disappear
    ComponentItem::EditResult editComponent(ComponentItem* item, const ItemProps& props);
    // Sources and displays only; an empty name gives back the automatic one ("Source 3")
    ComponentItem::EditResult renameComponent(ComponentItem* item, const QString& name);
    // Selected components go only if they have no wires left
    void deleteSelection();
    // Stops the run and empties the canvas
    void resetCircuit();
    // Sources and displays whose name matches `pattern` (a case-insensitive regex, else literal text); inputs first
    QList<ComponentItem*> findInputsAndOutputs(const QString& pattern) const;
    void startWireConnection(QPointF startPoint);
    void updateWireConnection(QPointF currentPoint);
    void finishWireConnection(QPointF endPoint);

    // What a middle click places; each kind keeps its own defaults
    const ItemProps& toolProps() const { return m_toolDefaults[m_activeTool]; }

    //----- Compiled designs and their blocks -----

    // Empty for a drawn circuit; each submodule instance is a block
    const NetDesign& design() const { return m_design; }
    // Empty while stopped
    const WireMap& wireMap() const { return m_wireMap; }
    // Sources and displays, then every block port (nested too); kept until the next run
    const QList<WaveSignal>& waveSignals() const { return m_waveSignals; }
    // topLevel: ports become sources and displays; otherwise read-only port tags
    void buildModule(const NetDesign& design, const QString& moduleName, bool topLevel);
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    bool isReadOnly() const { return m_readOnly; }

    // Puts the run's values on `view`, a read-only scene showing the instance at `path`
    void showInstanceValues(const QString& path, CircuitScene* view) const;
    void clearValues() { resetValues(); }
protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;   // canvas color and dot grid

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
public:
    void startOrResumeSim();   // Stopped: from tick 0; Paused: continue
    void pauseSim();
    void stopSim();

    void setToolProps(const ItemProps& props);
    void selectTool(std::size_t kind);           // kind = itemKind<...>
    void selectGateTool(GType gateType);
    void receiveResult(const SimResult& result);
    // Keeps only the newest result and applies it at the next frame, so fast runs don't flood the GUI
    void queueResult(const SimResult& result);
    void receiveDesign(const NetDesign& design);
    CircuitData circuitData() const;
    // Replaces the circuit; refused with a reason if the data doesn't fit together (nothing changes then)
    std::expected<void, QString> loadCircuit(const CircuitData& data);

    //----- Undo: a snapshot per change; drags count once, typing in one field merges -----
    bool canUndo() const { return m_historyIndex > 0; }
    bool canRedo() const { return m_historyIndex + 1 < m_history.size(); }
    void undo();                       // nothing while running; ends a paused run
    void redo();
    void clearHistory();               // the circuit as it is becomes the only, saved state
    void markSaved();
    void markUnsaved();                // its file is gone: no state is saved any more
    bool isAtSavedState() const { return m_historyIndex == m_savedIndex; }
    static constexpr int MaxUndoSteps = 200;
signals:
    // To the simulator, queued across threads so they arrive in order
    void startSimSIG(ExportGraph graph);
    void setSimRunningSIG(bool running);
    void stopSimSIG();

    void simStateChanged(SimState state);
    void toolChanged();
    void openBlock(BlockItem* block);
    void designChanged();              // also when cleared: the view returns to the top
    void valuesChanged();              // block views follow it
    void circuitModified();            // a change worth saving: an edit or a move
    void historyChanged();             // a step recorded, undone or redone, or the saved state moved
private:
    ExportGraph buildGraph();          // also fills the wire map
    WireItem* addWire(PortItem* from, PortItem* to);
    // See BlockExpander; pinNet gives the net on a block pin
    void expandBlocks(ExportGraph& graph, const QList<BlockItem*>& blocks, const std::function<u32(PortItem*)>& pinNet);
    // Needs the final net numbers
    void buildWaveSignals(ExportGraph& graph, const QList<BlockItem*>& blocks);
    void setSimState(SimState state);
    void circuitEdited();              // after every structural change
    void watchMoves(QGraphicsObject* item);
    // designId: which design the data belongs to; a different one restarts the views and waveforms
    std::expected<void, QString> replaceCircuit(const CircuitData& data, int designId);
    void recordStep();
    struct Snapshot {
        CircuitData data;
        int designId = 0;
        bool operator==(const Snapshot& o) const { return designId == o.designId && data.components == o.data.components && data.wires == o.data.wires; }
    };
    void restore(const Snapshot& snapshot);
    QString automaticName(const ComponentItem* item) const;   // "Source 3"; empty for other kinds
    void cancelWireConnection();
    void resetValues();

    void clearHighlights();            // of a dragged wire's start and snap pins

    SimState m_simState = SimState::Stopped;
    u32 m_runId = 0;                   // bumped on every start and stop; older results are ignored

    // A wire being dragged with the right button
    bool m_connectingWire = false;
    QPointF m_wireStartPoint;
    WireItem* m_currWire = nullptr;
    PortItem* m_currWireStartPort = nullptr;
    PortItem* m_snapPort = nullptr;    // where it would connect if released now
    PortItem* findNearestPort(const QPointF& scenePos, double threshold = 15.0);

    WireMap m_wireMap;                 // cleared when the run ends

    QList<Snapshot> m_history{Snapshot{}};
    qsizetype m_historyIndex = 0;
    qsizetype m_savedIndex = 0;        // -1: no state in the history is saved
    int m_designId = 0;                // changes with m_design (the netlist itself is shared, never compared)
    int m_lastDesignId = 0;
    bool m_restoring = false;
    int m_lastEdited = -1;             // the component the last step only changed the settings of
    QElapsedTimer m_sinceEdit;

    // m_toolDefaults[i].index() == i
    std::array<ItemProps, itemKindCount> m_toolDefaults{GateProps{}, RegisterProps{}, MuxProps{}, SourceProps{}, DisplayProps{}, BlockProps{}};
    std::size_t m_activeTool = itemKind<GateProps>;

    // Per run, so receiveResult needn't scan items()
    QList<QPointer<DisplayItem>> m_displays;
    QList<QPointer<SourceItem>> m_sources;
    QList<WaveSignal> m_waveSignals;
    std::vector<u8> m_lastNetValues; // only changed nets are pushed to wires
    std::vector<bool> m_lastRegisterValues;

    // For queueResult
    SimResult m_pendingResult;
    bool m_hasPendingResult = false;
    QTimer m_frameTimer;               // fires at the next frame
    QElapsedTimer m_sinceLastFrame;
    void applyPendingResult();

    NetDesign m_design;
    bool m_readOnly = false;
    // Per run, by instance path ("core/alu"): its module bits' nets and its flip-flops' registers
    QHash<QString, QHash<int, u32>> m_instanceNets;
    QHash<QString, QHash<QString, u32>> m_instanceRegisters;
    // In a module's scene: each wire's module bit, each flip-flop's node
    QHash<WireItem*, int> m_wireBits;
    QHash<RegisterItem*, QString> m_registerNodes;
};

class CircuitCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit CircuitCanvas(QWidget* parent = nullptr);
    CircuitScene* getScene() const { return m_scene; }   // the top level

    //----- Blocks: shown on this canvas in place of the circuit -----

    // The circuit, or the innermost opened block
    CircuitScene* currentScene() const;
    int depth() const { return int(m_levels.size()); }   // 0 = the circuit
    QString currentPath() const;                         // e.g. "core/alu"
    void openBlock(BlockItem* block);
    void showLevel(int depth);                           // 0 = the circuit
    void focusOn(ComponentItem* item);
    void showWholeCircuit();                             // everything on screen, never magnified                   // jumps and zooms to a component of the circuit, and selects it
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
    struct Level {
        CircuitScene* scene = nullptr;
        QString path;                 // e.g. "core/alu"
        QString title;                // e.g. "alu (ALU)"
        QTransform outerTransform;    // of the level outside, to go back to
        QPointF outerCentre;
    };
    void showScene(CircuitScene* scene);
    void updateBreadcrumb();
    void refreshBlockValues();
    QPointF viewCentre() const { return mapToScene(viewport()->rect().center()); }

    CircuitScene* m_scene;
    QList<Level> m_levels;            // outermost first
    QFrame* m_breadcrumb = nullptr;   // "top > core (RISCV) > alu (ALU)"
    // Left-dragging empty canvas pans
    bool m_panning = false;
    QPoint m_lastPanPoint;
    QPoint m_panStartPoint;   // a click deselects, a drag pans
};
