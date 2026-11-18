#pragma once
// The Waveforms tab: the values of the circuit's inputs and outputs, and of its blocks' ports, over
// the ticks of a run, like the wave window of an HDL simulator.
//
//   1. WaveHistory    what the simulator recorded: for every recorded net, the ticks it changed at
//   2. formatValue    a value as text, in the radix chosen for its signal
//   3. WaveformModel  the run's signals, which of them are shown and how, and their history
//
// The simulator records the changes in its own thread (Simulator::sendWaveChanges); this side only
// keeps and reads them.

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
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
