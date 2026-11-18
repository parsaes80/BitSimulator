// The waveforms: what the simulator records for them, the history built from it, and the view.

#include "TestSupport.h"
#include "Waveform.h"
#include "simulator.h"
#include <QEventLoop>
#include <QTimer>

using namespace Qt::StringLiterals;

using namespace TestSupport;

namespace {

template <typename Item, typename Props>
Item* add(CircuitScene& scene, const Props& props, QPointF position = {})
{
    return dynamic_cast<Item*>(scene.addComponent(props, position));
}

// A run of the simulator in this thread: every result, and every report of recorded changes
struct Run {
    QList<SimResult> results;
    QList<WaveChanges> reports;
};

// Runs `graph` for `ticks` ticks as fast as the simulator goes, then stops it as the canvas would
Run simulate(const ExportGraph& graph, int ticks, bool pauseFirst = false)
{
    Simulator sim;
    Run run;
    QEventLoop loop;
    QObject::connect(&sim, &Simulator::sendResult, &sim, [&](SimResult result) {
        run.results.append(std::move(result));
        if (run.results.size() >= ticks) {
            sim.setRunning(false);           // no more ticks; a pause reports what is left
            loop.quit();
        }
    });
    QObject::connect(&sim, &Simulator::sendWaveChanges, &sim, [&](WaveChanges changes) { run.reports.append(std::move(changes)); });
    sim.SimController();
    sim.setTimerPeriod(0);
    sim.receiveCircuit(graph);
    if (run.results.size() < ticks) {
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    if (!pauseFirst) sim.unloadCircuit();
    return run;
}

// The value of a recorded net after `tick`, from the reports: whether it changed an odd number of times
bool recordedValue(const Run& run, u32 net, u64 tick)
{
    bool value = false;
    for (const WaveChanges& report : run.reports) {
        for (const WaveChange& change : report.changes) {
            if (change.net == net && change.tick <= tick) value = !value;
        }
    }
    return value;
}

// `width` bits of `value`, lowest first
std::vector<bool> bitsOf(u64 value, int width)
{
    std::vector<bool> bits(static_cast<size_t>(width));
    for (int i = 0; i < width; i++) bits[size_t(i)] = i < 64 && ((value >> i) & 1);
    return bits;
}

WaveChanges report(u32 runId, u64 lastTick, std::vector<WaveChange> changes)
{
    WaveChanges r;
    r.runId = runId;
    r.lastTick = lastTick;
    r.hasTicks = true;
    r.changes = std::move(changes);
    return r;
}

WaveSignal signalOf(const QString& scope, const QString& name, bool output, std::vector<u32> nets)
{
    return WaveSignal{scope, scope.isEmpty() ? QString() : u"mod"_s, name, output, std::move(nets)};
}

ExportGraph startAndCapture(CircuitScene& scene)
{
    ExportGraph graph;
    const auto connection = QObject::connect(&scene, &CircuitScene::startSimSIG, &scene, [&graph](const ExportGraph& g) { graph = g; });
    scene.startOrResumeSim();
    QObject::disconnect(connection);
    return graph;
}

} // namespace

class TestWaveform : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }

    //===================== Recording, in the simulator ========================

    void theSignalsAreTheCircuitsInputsAndOutputs()
    {
        // A 3-bit counter, one bit inverted onto a display; a second source with no wires at all
        CircuitScene scene;
        auto* counter = add<SourceItem>(scene, SourceProps::counter(3), QPointF(80000, 50000));
        auto* idle = add<SourceItem>(scene, SourceProps::counter(2), QPointF(80000, 50300));
        auto* inverter = add<GateItem>(scene, GateProps{GType::NOT, 1}, QPointF(80200, 50000));
        auto* display = add<DisplayItem>(scene, DisplayProps{2, 0}, QPointF(80400, 50000));
        QVERIFY(connectPorts(&scene, counter->getOutputPorts()[0], inverter->getInputPorts()[0]));
        QVERIFY(connectPorts(&scene, inverter->getOutputPort(), display->getInputPorts()[0]));
        QVERIFY(scene.renameComponent(counter, u"count"_s));

        const ExportGraph graph = startAndCapture(scene);
        const QList<WaveSignal>& signals_ = scene.waveSignals();
        QCOMPARE(signals_.size(), 3);
        // Inputs, then outputs, by name: "Source 2" (the idle one) comes after "count"
        QCOMPARE(signals_[0].name, u"count"_s);
        QCOMPARE(signals_[0].width(), 3);
        QVERIFY(!signals_[0].output);
        QVERIFY(signals_[0].scope.isEmpty());
        QCOMPARE(signals_[1].name, idle->name());
        QCOMPARE(signals_[2].name, display->name());
        QCOMPARE(signals_[2].width(), 2);
        QVERIFY(signals_[2].output);
        QCOMPARE(signals_[2].nets[1], u32(0));                          // its second input has no wire

        // Every bit of every source has a net, wired or not, and the simulator records exactly these
        for (const WaveSignal& signal : {signals_[0], signals_[1]}) {
            for (u32 net : signal.nets) QVERIFY(net != 0);
        }
        QVERIFY(std::ranges::is_sorted(graph.recordedNets));
        QCOMPARE(graph.recordedNets.size(), std::size_t(3 + 2 + 1));   // the display's bit 0 is the inverter's net
        scene.stopSim();
    }

    void theRecordedHistoryMatchesEveryTick()
    {
        CircuitScene scene;
        auto* counter = add<SourceItem>(scene, SourceProps::counter(3), QPointF(80000, 50000));
        add<SourceItem>(scene, SourceProps::list({1, 2, 3}, 2), QPointF(80000, 50300));   // no wires
        auto* gate = add<GateItem>(scene, GateProps{GType::AND, 2}, QPointF(80200, 50000));
        auto* display = add<DisplayItem>(scene, DisplayProps{1, 0}, QPointF(80400, 50000));
        QVERIFY(connectPorts(&scene, counter->getOutputPorts()[0], gate->getInputPorts()[0]));
        QVERIFY(connectPorts(&scene, counter->getOutputPorts()[1], gate->getInputPorts()[1]));
        QVERIFY(connectPorts(&scene, gate->getOutputPort(), display->getInputPorts()[0]));

        const ExportGraph graph = startAndCapture(scene);
        const Run run = simulate(graph, 200);
        scene.stopSim();
        QCOMPARE(run.results.size(), 200);
        QVERIFY(run.reports.size() >= 1);

        // Reports cover every tick in order, and the last one reaches the last tick
        u64 previous = 0;
        for (const WaveChanges& report : run.reports) {
            QCOMPARE(report.runId, graph.runId);
            for (const WaveChange& change : report.changes) {
                QVERIFY(change.tick >= previous);
                previous = change.tick;
            }
        }
        QCOMPARE(run.reports.last().lastTick, run.results.last().sourceTick);

        // Every recorded net, at every tick, has the value the simulator had
        for (const SimResult& result : run.results) {
            for (u32 net : graph.recordedNets) {
                QVERIFY2(recordedValue(run, net, result.sourceTick) == (result.netValues[net] != 0),
                         qPrintable(u"net %1 at tick %2"_s.arg(net).arg(result.sourceTick)));
            }
        }

        // An unwired source is recorded too: its bits are its values
        const WaveSignal& listSignal = scene.waveSignals()[1];             // "Source 1", "Source 2", then the display
        QCOMPARE(listSignal.name, u"Source 2"_s);
        for (const SimResult& result : run.results) {
            const u64 expected = sourceValueAt(SourceProps::list({1, 2, 3}, 2), result.sourceTick);
            for (int bit = 0; bit < listSignal.width(); bit++) {
                QCOMPARE(recordedValue(run, listSignal.nets[size_t(bit)], result.sourceTick), ((expected >> bit) & 1) != 0);
            }
        }
    }

    void onlyChangesAreRecorded()
    {
        // A constant never changes: one change at tick 0 for each 1 bit, nothing after
        CircuitScene scene;
        add<SourceItem>(scene, SourceProps::constantOf(5, 3), QPointF(80000, 50000));
        const ExportGraph graph = startAndCapture(scene);
        const Run run = simulate(graph, 100);
        scene.stopSim();
        int changes = 0;
        for (const WaveChanges& report : run.reports) {
            for (const WaveChange& change : report.changes) {
                QCOMPARE(change.tick, u64(0));
                changes++;
            }
        }
        QCOMPARE(changes, 2);                                             // 5 = 101
    }

    void pausingReportsUpToTheNewestTick()
    {
        CircuitScene scene;
        add<SourceItem>(scene, SourceProps::counter(4), QPointF(80000, 50000));
        const ExportGraph graph = startAndCapture(scene);
        const Run run = simulate(graph, 37, /*pauseFirst*/ true);       // paused, not unloaded
        scene.stopSim();
        QVERIFY(!run.reports.isEmpty());
        QCOMPARE(run.reports.last().lastTick, run.results.last().sourceTick);
    }

    //===================== The history ========================

    void historyValuesAndChanges()
    {
        WaveHistory history;
        history.start(5, {3, 7});
        QVERIFY(history.isEmpty());
        QVERIFY(history.add(report(5, 6, {{0, 3}, {2, 7}, {4, 3}})));
        QVERIFY(history.add(report(5, 10, {{9, 3}})));
        QVERIFY(!history.add(report(4, 12, {{11, 3}})));                // another run's report is ignored
        QCOMPARE(history.lastTick(), u64(10));
        QCOMPARE(history.changeCount(), size_t(4));

        QCOMPARE(history.bit(3, 0), true);                               // changed at tick 0
        QCOMPARE(history.bit(3, 3), true);
        QCOMPARE(history.bit(3, 4), false);
        QCOMPARE(history.bit(3, 9), true);
        QCOMPARE(history.bit(7, 1), false);
        QCOMPARE(history.bit(7, 2), true);
        QCOMPARE(history.bit(0, 2), false);                              // net 0 is always 0
        QCOMPARE(history.bit(WaveSignal::AlwaysOne, 2), true);
        QCOMPARE(history.bit(5, 2), false);                              // not recorded

        QCOMPARE(history.changesBetween(3, 1, 9), size_t(2));
        QCOMPARE(history.changesBetween(3, 0, 0), size_t(1));
        QCOMPARE(history.nextChange(3, 4), std::optional<u64>(9));
        QCOMPARE(history.nextChange(3, 9), std::optional<u64>());
    }

    void droppingTheOldestKeepsTheRestRight()
    {
        WaveHistory history;
        history.start(1, {2, 3});
        history.setMaxChanges(100);
        for (u64 tick = 0; tick < 400; tick += 10) {
            std::vector<WaveChange> changes;
            for (u64 t = tick; t < tick + 10; t++) {
                changes.push_back({t, 2});                               // net 2 flips every tick
                if (t % 7 == 0) changes.push_back({t, 3});               // net 3 every 7th
            }
            QVERIFY(history.add(report(1, tick + 9, changes)));
        }
        QVERIFY(history.changeCount() <= 100);
        QVERIFY(history.firstTick() > 0);
        QCOMPARE(history.lastTick(), u64(399));
        for (u64 t = history.firstTick(); t <= 399; t++) {
            QCOMPARE(history.bit(2, t), (t + 1) % 2 == 1);              // 1 after ticks 0, 2, 4, ...
            QCOMPARE(history.bit(3, t), (t / 7 + 1) % 2 == 1);
        }
    }

    //===================== Values as text ========================

    void formatValueInEveryRadix()
    {
        QCOMPARE(formatValue(bitsOf(0x0F, 8), Radix::Hex), u"0f"_s);
        QCOMPARE(formatValue(bitsOf(0x0F, 8), Radix::Binary), u"00001111"_s);
        QCOMPARE(formatValue(bitsOf(0x0F, 8), Radix::Unsigned), u"15"_s);
        QCOMPARE(formatValue(bitsOf(0x0F, 8), Radix::Signed), u"15"_s);
        QCOMPARE(formatValue(bitsOf(0xF0, 8), Radix::Unsigned), u"240"_s);
        QCOMPARE(formatValue(bitsOf(0xF0, 8), Radix::Signed), u"-16"_s);
        QCOMPARE(formatValue(bitsOf(0b1000, 4), Radix::Signed), u"-8"_s);   // the most negative
        QCOMPARE(formatValue(bitsOf(0x5, 3), Radix::Hex), u"5"_s);
        QCOMPARE(formatValue(bitsOf(0, 12), Radix::Unsigned), u"0"_s);
        QCOMPARE(formatValue(bitsOf(0x123, 10), Radix::Hex), u"123"_s);

        // Wider than 64 bits: 70 ones
        const std::vector<bool> ones(70, true);
        QCOMPARE(formatValue(ones, Radix::Hex), u"3fffffffffffffffff"_s);
        QCOMPARE(formatValue(ones, Radix::Unsigned), u"1180591620717411303423"_s);   // 2^70 - 1
        QCOMPARE(formatValue(ones, Radix::Signed), u"-1"_s);
        std::vector<bool> top(70, false);
        top[69] = true;
        QCOMPARE(formatValue(top, Radix::Signed), u"-590295810358705651712"_s);       // -2^69
    }

    //===================== Which signals are shown ========================

    void theCircuitsInputsAndOutputsAreShownAndChoicesKept()
    {
        WaveformModel model;
        const QList<WaveSignal> first{signalOf({}, u"a"_s, false, {1, 2}), signalOf({}, u"y"_s, true, {3}),
                                      signalOf(u"u"_s, u"x"_s, false, {1})};
        model.startRun(1, first, {1, 2, 3}, u"top"_s);
        QCOMPARE(model.rows().size(), 2);                                  // a and y; u's port only when added
        QCOMPARE(model.label(model.rows()[0]), u"a[1:0]"_s);
        QCOMPARE(model.label(model.rows()[1]), u"y"_s);

        model.show(2);
        model.setRadix(0, Radix::Binary);
        model.setExpanded(0, true);
        QList<WaveRow> rows = model.rows();
        QCOMPARE(rows.size(), 5);                                          // a, a[1], a[0], y, u/x
        QCOMPARE(model.label(rows[1]), u"a[1]"_s);
        QCOMPARE(model.label(rows[2]), u"a[0]"_s);
        QCOMPARE(model.label(rows[4]), u"u/x"_s);

        // Values: a = 2 then 3; a bit row is that bit
        model.addChanges(report(1, 5, {{0, 2}, {3, 1}}));
        QCOMPARE(model.valueText(rows[0], 0), u"10"_s);                    // binary, as chosen
        QCOMPARE(model.valueText(rows[0], 4), u"11"_s);
        QCOMPARE(model.valueText(rows[2], 4), u"1"_s);
        QCOMPARE(model.valueText(rows[4], 2), u"0"_s);

        // The next run of the same circuit, with a new input: the choices stay, the new input is added
        model.hide(1);                                                     // y removed by hand
        QList<WaveSignal> second = first;
        second.append(signalOf({}, u"b"_s, false, {4}));
        model.startRun(2, second, {1, 2, 3, 4}, u"top"_s);
        rows = model.rows();
        QCOMPARE(rows.size(), 5);                                          // a (expanded), u/x, b; not y
        QCOMPARE(model.radix(0), Radix::Binary);
        QCOMPARE(model.label(rows.last()), u"b"_s);
        QVERIFY(!model.isShown(1));
        QVERIFY(model.history().isEmpty());                                // a new run, a new history

        model.move(3, -1);                                                 // b above u/x
        QCOMPARE(model.label(model.rows().last()), u"u/x"_s);

        // A new circuit: back to its inputs and outputs only
        model.resetSignals();
        QVERIFY(model.rows().isEmpty());
        model.startRun(3, second, {1, 2, 3, 4}, u"top"_s);
        QCOMPARE(model.rows().size(), 3);                                  // a, y, b
        QCOMPARE(model.radix(0), Radix::Hex);
    }
};

QTEST_MAIN(TestWaveform)
#include "tst_waveform.moc"
