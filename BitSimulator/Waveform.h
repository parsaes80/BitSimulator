#pragma once
// The Waveforms tab: the values of the circuit's inputs and outputs, and of its blocks' ports, over
// the ticks of a run, like the wave window of an HDL simulator.
//
//   1. WaveHistory    what the simulator recorded: for every recorded net, the ticks it changed at
//   2. formatValue    a value as text, in the radix chosen for its signal
//   3. WaveformModel  the run's signals, which of them are shown and how, and their history
//   4. WaveformView   draws the shown signals over the ticks: zoom, pan, cursors
//   5. WaveSignalTree the circuit and its block instances, with a check box per port to show it
//
// The simulator records the changes in its own thread (Simulator::sendWaveChanges); this side only
// keeps and reads them.

#include <QObject>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <optional>
#include <vector>
#include "CircuitCanvas.h"   // WaveSignal
#include "general.h"

//===================== 1. WaveHistory ========================

// A net's value at a tick is the value it started the history with, flipped once per change up to
// and including that tick. Ticks are kept per net, so a net that never changes costs nothing.
class WaveHistory {
public:
    // A new, empty history for the nets of a run
    void start(u32 runId, const std::vector<u32>& recordedNets);
    // Adds a report of the run; false (and nothing added) for a report of another run
    bool add(const WaveChanges& changes);

    u32 runId() const { return m_runId; }
    bool isEmpty() const { return !m_hasTicks; }
    u64 firstTick() const { return m_firstTick; }   // the oldest tick still kept
    u64 lastTick() const { return m_lastTick; }     // the newest tick recorded
    size_t changeCount() const { return m_changeCount; }

    // The value of a net after `tick`. Net 0 is always 0, WaveSignal::AlwaysOne always 1, and a net
    // that is not recorded reads 0.
    bool bit(u32 net, u64 tick) const;
    // How many times a net changes at ticks from..to (both included)
    size_t changesBetween(u32 net, u64 from, u64 to) const;
    // The first tick after `tick` at which the net changes, if it does
    std::optional<u64> nextChange(u32 net, u64 tick) const;

    // The most changes kept (8 bytes each). Beyond it the oldest quarter of the ticks is dropped.
    static constexpr size_t MaxChanges = size_t(32) << 20;
    void setMaxChanges(size_t changes) { m_maxChanges = changes; }   // for the tests

private:
    struct Net {
        bool startValue = false;         // its value at m_firstTick - 1, before the kept changes
        std::vector<u64> ticks;          // the ticks it changed at, oldest first
    };
    const Net* net(u32 id) const;
    void dropOldest();

    u32 m_runId = 0;
    bool m_hasTicks = false;
    u64 m_firstTick = 0;
    u64 m_lastTick = 0;
    size_t m_changeCount = 0;
    size_t m_maxChanges = MaxChanges;
    std::vector<u32> m_slot;             // by net id: index in m_nets + 1, 0 = not recorded
    std::vector<Net> m_nets;
};

//===================== 2. formatValue ========================

enum class Radix { Hex, Unsigned, Signed, Binary };

// `bits` lowest bit first, of any width. Hex and binary show every digit of the width ("0f" for 8
// bits); Signed reads the top bit as the sign (two's complement).
QString formatValue(const std::vector<bool>& bits, Radix radix);
QString radixName(Radix radix);

//===================== 3. WaveformModel ========================

// One row of the waveform: a signal, or a single bit of a bus that was expanded
struct WaveRow {
    int signal = -1;         // index in WaveformModel::signalList()
    int bit = -1;            // -1: the whole signal; otherwise this bit of it
};

class WaveformModel : public QObject {
    Q_OBJECT
public:
    explicit WaveformModel(QObject* parent = nullptr) : QObject(parent) {}

    // A run starts: its signals (CircuitScene::waveSignals) and the nets the simulator records.
    // `topName` names the circuit itself in the design tree (the compiled design's top module).
    // The shown signals that still exist stay shown; inputs and outputs of the circuit that the
    // last run did not have are shown too. After resetSignals(), all of the circuit's are shown.
    void startRun(u32 runId, const QList<WaveSignal>& runSignals, const std::vector<u32>& recordedNets, const QString& topName);
    // The simulator's reports
    void addChanges(const WaveChanges& changes);
    // A new circuit (a compile, or Clear): forget the run, and show the next run's inputs and outputs
    void resetSignals();
    // The run ended (the simulator reports its last ticks itself); its history stays until the next run
    void stopRun();

    const QList<WaveSignal>& signalList() const { return m_signals; }
    const WaveHistory& history() const { return m_history; }
    const QString& topName() const { return m_topName; }
    bool isRunning() const { return m_running; }
    bool hasRun() const { return !m_signals.isEmpty() || !m_history.isEmpty(); }

    //----- What is shown -----
    QList<WaveRow> rows() const;                 // the shown signals, with the bits of expanded buses
    bool isShown(int signal) const;
    void show(int signal);                       // added at the end; nothing if already shown
    void hide(int signal);
    void move(int signal, int by);               // up (-1) or down (+1) among the shown signals
    Radix radix(int signal) const;
    void setRadix(int signal, Radix radix);
    bool isExpanded(int signal) const;
    void setExpanded(int signal, bool expanded);

    //----- Values -----
    // The bits of a row after a tick, lowest first (one for a bit row)
    std::vector<bool> bitsAt(const WaveRow& row, u64 tick) const;
    // The text of a row's value after a tick, in its signal's radix
    QString valueText(const WaveRow& row, u64 tick) const;
    // The row's label: "a[3:0]" for the circuit's own, "u/h0/s" for a block's port, "a[2]" for a bit
    QString label(const WaveRow& row) const;
    // The nets a row is made of, lowest bit first
    std::vector<u32> netsOf(const WaveRow& row) const;

    // A signal's identity across runs: its scope and name
    static QString keyOf(const WaveSignal& signal);

signals:
    void signalsChanged();   // a run started, or the shown signals or their settings changed
    void historyGrew();      // new ticks arrived

private:
    struct Shown {
        QString key;
        Radix radix = Radix::Hex;
        bool expanded = false;
    };
    int shownIndex(int signal) const;            // index in m_shown, or -1

    QList<WaveSignal> m_signals;
    QStringList m_keys;                          // keyOf() of each signal
    QList<Shown> m_shown;                        // in the order they are shown
    QSet<QString> m_known;                       // the keys of the last run's signals
    bool m_reset = true;                         // show all of the circuit's inputs and outputs at the next start
    QString m_topName;
    WaveHistory m_history;
    bool m_running = false;
};

//===================== 4. WaveformView ========================

class QScrollBar;
class QTreeWidget;
class QTreeWidgetItem;

// The shown signals, one row each: name | value at the cursor | the waves. A tick t covers the
// pixels from xOf(t) to xOf(t + 1); its value is the value after that tick.
//   Wheel over the waves: zoom around the pointer. Wheel over the names: scroll the rows.
//   Left-drag: pan. Click: the cursor. Shift+click: a second cursor; the ruler shows the ticks between.
//   Right-click a row: radix, expand or collapse a bus, move, remove. Delete removes the selected row.
class WaveformView : public QWidget {
    Q_OBJECT
public:
    explicit WaveformView(QWidget* parent = nullptr);
    void setModel(WaveformModel* model);

    // The waves' area, and how ticks map to it
    QRect waveArea() const;
    double tickAt(double x) const;
    double xOf(double tick) const;
    double ticksPerPixel() const { return m_ticksPerPixel; }
    double leftTick() const { return m_leftTick; }

    std::optional<u64> cursorTick() const { return m_cursor; }
    std::optional<u64> secondCursorTick() const { return m_cursor2; }
    void setCursorTick(std::optional<u64> tick);
    void setSecondCursorTick(std::optional<u64> tick);

    // Follow: while the run goes on, keep its newest tick at the right edge. Panning turns it off.
    bool follows() const { return m_follow; }
    void setFollow(bool follow);

    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomAround(double x, double factor);   // factor < 1 zooms in; the tick at x stays at x

    int selectedRow() const { return m_selected; }   // index in the model's rows(), or -1
    int rowAt(int y) const;                           // the row under a y, or -1

    static constexpr int RowHeight = 26;
    static constexpr int RulerHeight = 26;
    static constexpr double MinTicksPerPixel = 1.0 / 200;   // zoomed in to 200 pixels per tick

signals:
    void followChanged(bool follow);
    void cursorsChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void signalsChanged();
    void historyGrew();
    void layoutParts();                    // the columns' widths and the scroll bars' places
    void clampView();                      // keeps the view on the recorded ticks
    void updateScrollBars();
    void scrollRows(int pixels);
    int rowsHeight() const { return int(m_rows.size()) * RowHeight; }
    std::optional<u64> nextChange(const std::vector<u32>& nets, u64 tick) const;   // of any of the nets
    void paintRuler(QPainter& painter);
    void paintWaves(QPainter& painter, const WaveRow& row, int top);
    void paintCursor(QPainter& painter, u64 tick, const QColor& color);
    QString rulerText(u64 tick) const;
    std::optional<QRect> measureLabel() const;   // "Δ 16 ticks" on the ruler, between the two cursors
    void showRowMenu(int row, const QPoint& globalPos);

    WaveformModel* m_model = nullptr;
    QList<WaveRow> m_rows;
    int m_nameWidth = 140;
    int m_valueWidth = 96;
    double m_leftTick = 0;                 // the tick at the waves' left edge (may be fractional)
    double m_ticksPerPixel = 1.0 / 20;     // 20 pixels per tick to start with
    int m_scrollY = 0;                     // rows scrolled out at the top, in pixels
    bool m_follow = true;
    std::optional<u64> m_cursor;
    std::optional<u64> m_cursor2;
    int m_selected = -1;

    // A left press in the waves: a click (cursor) unless the mouse moves; then a pan
    bool m_pressed = false;
    bool m_panning = false;
    QPoint m_pressPos;
    double m_pressLeftTick = 0;
    int m_pressScrollY = 0;

    QScrollBar* m_hbar = nullptr;
    QScrollBar* m_vbar = nullptr;
    double m_hbarScale = 1;                // pixels per scroll bar step, so long runs fit its int range
    bool m_updatingBars = false;
};

//===================== 5. WaveSignalTree ========================

// Fills a QTreeWidget (two columns: name, in/out) with the circuit, its block instances and their
// ports. A port's check box shows or hides it in the waveforms; an instance's, all of its ports.
class WaveSignalTree : public QObject {
    Q_OBJECT
public:
    WaveSignalTree(QTreeWidget* tree, WaveformModel* model, QObject* parent = nullptr);

private:
    void refresh();                        // rebuilds the tree for a new run, else updates the checks
    void rebuild();
    void itemChanged(QTreeWidgetItem* item, int column);

    QTreeWidget* m_tree;
    WaveformModel* m_model;
    QStringList m_builtFor;                // the keys of the signals the tree shows
    bool m_filling = false;
};
