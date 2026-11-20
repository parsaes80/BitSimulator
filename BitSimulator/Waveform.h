#pragma once
// The Waveforms tab: the recorded history, its model, the view and the signal tree

#include <QObject>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <expected>
#include <optional>
#include <span>
#include <vector>
#include "CircuitCanvas.h"   // WaveSignal
#include "general.h"

//===================== 1. WaveHistory ========================

// Ticks are kept per net; a net's value is its start value flipped once per change
class WaveHistory {
public:
    void start(u32 runId, const std::vector<u32>& recordedNets);
    // False (nothing added) for another run's report
    bool add(const WaveChanges& changes);

    u32 runId() const { return m_runId; }
    bool isEmpty() const { return !m_hasTicks; }
    u64 firstTick() const { return m_firstTick; }   // the oldest tick still kept
    u64 lastTick() const { return m_lastTick; }     // the newest tick recorded
    size_t changeCount() const { return m_changeCount; }

    // Net 0 and unrecorded nets read 0, WaveSignal::AlwaysOne reads 1
    bool bit(u32 net, u64 tick) const;
    // from..to inclusive
    size_t changesBetween(u32 net, u64 from, u64 to) const;
    std::optional<u64> nextChange(u32 net, u64 tick) const;
    std::span<const u64> changeTicks(u32 net) const;   // oldest first; empty if not recorded

    // 8 bytes each; beyond it the oldest quarter is dropped
    static constexpr size_t MaxChanges = size_t(32) << 20;
    void setMaxChanges(size_t changes) { m_maxChanges = changes; }   // for the tests

private:
    struct Net {
        bool startValue = false;         // at m_firstTick - 1
        std::vector<u64> ticks;          // oldest first
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

// `bits` lowest first, any width; hex and binary keep every digit ("0f" for 8 bits)
QString formatValue(const std::vector<bool>& bits, Radix radix);
QString radixName(Radix radix);

//===================== 3. WaveformModel ========================

// A signal, or one bit or word of an expanded bus
struct WaveRow {
    int signal = -1;         // in signalList()
    int bit = -1;            // -1: the whole signal; else the part's lowest bit
    int width = 1;           // the top word may be narrower
};

class WaveformModel : public QObject {
    Q_OBJECT
public:
    explicit WaveformModel(QObject* parent = nullptr) : QObject(parent) {}

    // Still-existing shown signals stay; the circuit's new inputs and outputs are added
    void startRun(u32 runId, const QList<WaveSignal>& runSignals, const std::vector<u32>& recordedNets, const QString& topName);
    void addChanges(const WaveChanges& changes);
    // A new circuit: forget it all; the next run shows its inputs and outputs
    void resetSignals();
    // The history stays until the next run
    void stopRun();
    // Forgets the history, and late reports of the run; shown signals stay
    void clearHistory();

    const QList<WaveSignal>& signalList() const { return m_signals; }
    const WaveHistory& history() const { return m_history; }
    const QString& topName() const { return m_topName; }
    bool isRunning() const { return m_running; }
    bool hasRun() const { return !m_signals.isEmpty() || !m_history.isEmpty(); }

    //----- What is shown -----
    QList<WaveRow> rows() const;                 // with the parts of expanded buses
    bool isShown(int signal) const;
    void show(int signal);                       // at the end
    void hide(int signal);
    void move(int signal, int by);               // -1 up, +1 down
    Radix radix(int signal) const;
    void setRadix(int signal, Radix radix);
    // Collapsing keeps the word size for the next expand
    bool isExpanded(int signal) const;
    void setExpanded(int signal, bool expanded);
    int wordBits(int signal) const;               // 1 = single bits
    void splitIntoWords(int signal, int bits);    // also expands it

    //----- Values -----
    // Lowest first
    std::vector<bool> bitsAt(const WaveRow& row, u64 tick) const;
    QString valueText(const WaveRow& row, u64 tick) const;
    // "a[3:0]", "u/h0/s" (block port), "a[2]" (bit or word)
    QString label(const WaveRow& row) const;
    std::vector<u32> netsOf(const WaveRow& row) const;

    // Identity across runs: scope and name
    static QString keyOf(const WaveSignal& signal);

signals:
    void signalsChanged();   // also when the shown signals or their settings change
    void historyGrew();

private:
    struct Shown {
        QString key;
        Radix radix = Radix::Hex;
        bool expanded = false;
        int wordBits = 1;
    };
    int shownIndex(int signal) const;            // or -1

    QList<WaveSignal> m_signals;
    QStringList m_keys;
    QList<Shown> m_shown;
    QSet<QString> m_known;                       // the last run's signals
    bool m_reset = true;                         // show all inputs and outputs at the next start
    QString m_topName;
    WaveHistory m_history;
    bool m_running = false;
};

//===================== 4. WaveformView ========================

class QScrollBar;
class QTreeWidget;
class QTreeWidgetItem;

// Rows of name | value at the cursor | waves; tick t covers xOf(t)..xOf(t + 1)
class WaveformView : public QWidget {
    Q_OBJECT
public:
    explicit WaveformView(QWidget* parent = nullptr);
    void setModel(WaveformModel* model);

    QRect waveArea() const;
    double tickAt(double x) const;
    double xOf(double tick) const;
    double ticksPerPixel() const { return m_ticksPerPixel; }
    double leftTick() const { return m_leftTick; }

    std::optional<u64> cursorTick() const { return m_cursor; }
    std::optional<u64> secondCursorTick() const { return m_cursor2; }
    void setCursorTick(std::optional<u64> tick);
    void setSecondCursorTick(std::optional<u64> tick);

    // Keeps the newest tick at the right edge while running; panning turns it off
    bool follows() const { return m_follow; }
    void setFollow(bool follow);

    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomAround(double x, double factor);   // factor < 1 zooms in; the tick at x stays put

    int selectedRow() const { return m_selected; }   // in rows(), or -1
    int rowAt(int y) const;                           // or -1

    static constexpr int RowHeight = 26;
    static constexpr int RulerHeight = 26;
    static constexpr double MinTicksPerPixel = 1.0 / 200;   // 200 pixels per tick

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
    void layoutParts();                    // column widths, scroll bar places
    void clampView();                      // on the recorded ticks
    void updateScrollBars();
    void scrollTime(double pixels);        // positive: forward
    void scrollRows(int pixels);
    int rowsHeight() const { return int(m_rows.size()) * RowHeight; }
    std::optional<u64> nextChange(const std::vector<u32>& nets, u64 tick) const;   // of any of the nets
    void paintRuler(QPainter& painter);
    void paintWaves(QPainter& painter, const WaveRow& row, int top);
    void paintCursor(QPainter& painter, u64 tick, const QColor& color);
    QString rulerText(u64 tick) const;
    std::optional<QRect> measureLabel() const;   // "Δ 16 ticks" between the cursors
    void showRowMenu(int row, const QPoint& globalPos);

    WaveformModel* m_model = nullptr;
    QList<WaveRow> m_rows;
    int m_nameWidth = 140;
    int m_valueWidth = 96;
    double m_leftTick = 0;                 // may be fractional
    double m_ticksPerPixel = 1.0 / 20;
    int m_scrollY = 0;                     // pixels
    bool m_follow = true;
    std::optional<u64> m_cursor;
    std::optional<u64> m_cursor2;
    int m_selected = -1;

    // A press in the waves is a click (cursor) unless the mouse moves (pan)
    bool m_pressed = false;
    bool m_panning = false;
    QPoint m_pressPos;
    double m_pressLeftTick = 0;
    int m_pressScrollY = 0;

    QScrollBar* m_hbar = nullptr;
    QScrollBar* m_vbar = nullptr;
    double m_hbarScale = 1;                // so long runs fit the bar's int range
    bool m_updatingBars = false;
};

//===================== 5. WaveSignalTree ========================

// Checkable tree of the circuit, its instances and their ports; checks show signals
class WaveSignalTree : public QObject {
    Q_OBJECT
public:
    WaveSignalTree(QTreeWidget* tree, WaveformModel* model, QObject* parent = nullptr);

private:
    void refresh();                        // rebuilds for a new run, else updates the checks
    void rebuild();
    void itemChanged(QTreeWidgetItem* item, int column);

    QTreeWidget* m_tree;
    WaveformModel* m_model;
    QStringList m_builtFor;
    bool m_filling = false;
};

//===================== 6. VCD export ========================

class QIODevice;
// Every recorded signal of the run as a Value Change Dump: a scope per block instance, 1 tick = 1 ns
std::expected<void, QString> writeVcd(QIODevice& out, const WaveformModel& model);
