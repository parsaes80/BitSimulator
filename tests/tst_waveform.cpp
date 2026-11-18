// The waveforms: what the simulator records for them, the history built from it, and the view.

#include "TestSupport.h"
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
};

QTEST_MAIN(TestWaveform)
#include "tst_waveform.moc"
