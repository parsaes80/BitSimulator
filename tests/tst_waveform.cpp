// The waveforms: what the simulator records for them, the history built from it, and the view.

#include "TestSupport.h"
#include "Waveform.h"
#include "simulator.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTreeWidget>
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

    //===================== The view ========================

    void theViewMapsTicksToPixelsAndZooms()
    {
        WaveformModel model;
        model.startRun(1, {signalOf({}, u"a"_s, false, {1})}, {1}, {});
        model.addChanges(report(1, 999, {{10, 1}, {20, 1}}));
        WaveformView view;
        view.setModel(&model);
        view.resize(900, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const QRect waves = view.waveArea();

        // Following: the newest tick ends at the right edge
        QVERIFY(view.follows());
        QVERIFY(qAbs(view.xOf(1000) - waves.left() - waves.width() * 1.0) < 2);

        // Fit: all 1000 ticks across the waves
        view.zoomToFit();
        QCOMPARE(view.tickAt(waves.left()), 0.0);
        QVERIFY(qAbs(view.xOf(1000) - (waves.left() + waves.width())) < 2);

        // Zooming keeps the tick under the pointer where it is
        view.setFollow(false);
        const double x = waves.left() + 300;
        const double before = view.tickAt(x);
        view.zoomAround(x, 0.25);
        QVERIFY(qAbs(view.tickAt(x) - before) < 1e-6);
        QVERIFY(view.ticksPerPixel() < 1000.0 / waves.width());
        for (int i = 0; i < 40; i++) view.zoomAround(x, 0.5);           // not past 200 pixels per tick
        QCOMPARE(view.ticksPerPixel(), WaveformView::MinTicksPerPixel);
    }

    void clicksSetTheCursorsAndDragsPan()
    {
        WaveformModel model;
        model.startRun(1, {signalOf({}, u"a"_s, false, {1})}, {1}, {});
        model.addChanges(report(1, 199, {{10, 1}}));
        WaveformView view;
        view.setModel(&model);
        view.resize(900, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setFollow(false);
        view.zoomToFit();                                                   // all 200 ticks on screen
        const QRect waves = view.waveArea();
        const int y = WaveformView::RulerHeight + WaveformView::RowHeight / 2;

        // A click puts the cursor on the tick under it; Shift+click the second one
        const auto xMiddleOf = [&](u64 tick) { return int(view.xOf(double(tick) + 0.5)); };
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(xMiddleOf(30), y));
        QCOMPARE(view.cursorTick(), std::optional<u64>(30));
        QTest::mouseClick(&view, Qt::LeftButton, Qt::ShiftModifier, QPoint(xMiddleOf(42), y));
        QCOMPARE(view.secondCursorTick(), std::optional<u64>(42));
        QCOMPARE(view.cursorTick(), std::optional<u64>(30));

        // A drag pans instead, and stops following
        view.zoomAround(waves.center().x(), 0.25);                        // room to pan
        view.setFollow(true);
        const double left = view.leftTick();
        const QPoint start(waves.left() + 400, y);
        QTest::mousePress(&view, Qt::LeftButton, {}, start);
        QTest::mouseMove(&view, start + QPoint(120, 0));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, start + QPoint(120, 0));
        QVERIFY(!view.follows());
        QVERIFY(qAbs(view.leftTick() - (left - 120 * view.ticksPerPixel())) < 1e-6);
        QCOMPARE(view.cursorTick(), std::optional<u64>(30));             // a drag is not a click

        // The wheel over the waves zooms in
        const double zoom = view.ticksPerPixel();
        QWheelEvent wheel(QPointF(waves.left() + 200, y), view.mapToGlobal(QPointF(waves.left() + 200, y)), {}, QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &wheel);
        QVERIFY(view.ticksPerPixel() < zoom);
    }

    void aHighLevelIsDrawnWhereTheSignalIsOne()
    {
        WaveformModel model;
        model.startRun(1, {signalOf({}, u"a"_s, false, {1})}, {1}, {});
        model.addChanges(report(1, 39, {{10, 1}, {20, 1}}));             // 1 during ticks 10..19
        WaveformView view;
        view.setModel(&model);
        view.resize(900, 200);
        view.zoomToFit();
        QImage image(view.size(), QImage::Format_ARGB32);
        view.render(&image);

        const int high = WaveformView::RulerHeight + 5 + 1;               // just under the high level
        const QColor canvas = currentTheme().canvas;
        // Sampled between the ruler's grid lines
        QVERIFY(image.pixelColor(int(view.xOf(12.5)), high) != canvas);  // filled under the 1
        QCOMPARE(image.pixelColor(int(view.xOf(32.5)), high), canvas);  // nothing at 0
    }

    void drawingAMillionTicksIsQuick()
    {
        // A clock and a 32-bit counter over a million ticks, all on screen at once
        std::vector<u32> nets{1};
        for (u32 n = 2; n < 34; n++) nets.push_back(n);
        WaveformModel model;
        QList<WaveSignal> list{signalOf({}, u"clk"_s, false, {1}), signalOf({}, u"count"_s, false, std::vector<u32>(nets.begin() + 1, nets.end()))};
        model.startRun(1, list, nets, {});
        std::vector<WaveChange> changes;
        constexpr u64 Ticks = 1'000'000;
        for (u64 t = 0; t < Ticks; t++) {
            changes.push_back({t, 1});
            const u64 flipped = t ^ (t + 1);                              // the counter's bits that change after t
            for (u32 bit = 0; bit < 32; bit++) if ((flipped >> bit) & 1) changes.push_back({t + 1, 2 + bit});
        }
        std::ranges::stable_sort(changes, {}, &WaveChange::tick);
        changes.erase(std::ranges::find_if(changes, [](const WaveChange& c) { return c.tick >= Ticks; }), changes.end());
        model.addChanges(report(1, Ticks - 1, std::move(changes)));

        WaveformView view;
        view.setModel(&model);
        view.resize(1600, 300);
        view.zoomToFit();
        QImage image(view.size(), QImage::Format_ARGB32);
        QElapsedTimer timer;
        timer.start();
        view.render(&image);
        QVERIFY2(timer.elapsed() < 500, qPrintable(u"took %1 ms"_s.arg(timer.elapsed())));
    }

    //===================== The tab, through the window ========================

    void theTabFollowsTheRuns()
    {
        App app;
        auto* model = app.window.findChild<WaveformModel*>();
        auto* view = child<WaveformView>(&app.window, "waveView");
        auto* status = child<QLabel>(&app.window, "waveStatus");
        auto* tree = child<QTreeWidget>(&app.window, "waveSignalTree");
        QVERIFY(model && !model->hasRun());

        auto* source = dynamic_cast<SourceItem*>(app.scene->addComponent(SourceProps::counter(2), app.centre));
        auto* display = dynamic_cast<DisplayItem*>(app.scene->addComponent(DisplayProps{2, 0}, app.centre + QPointF(200, 0)));
        QVERIFY(connectPorts(app.scene, source->getOutputPorts()[0], display->getInputPorts()[0]));
        child<QSlider>(&app.window, "slider")->setValue(10000);          // the fastest speed

        // Start: the circuit's input and output are shown, and the ticks come in
        app.start->click();
        QCOMPARE(model->rows().size(), 2);
        QTRY_VERIFY(!model->history().isEmpty() && model->history().lastTick() > 20);
        QVERIFY(tree->topLevelItem(0)->childCount() == 2);
        QVERIFY(status->text().startsWith(u"Tick"_s));

        // Pause: the newest tick is reported and the status says so
        app.start->click();
        QTRY_VERIFY(status->text().startsWith(u"Paused at tick"_s));
        QTest::qWait(100);                                                  // ticks run before the pause reached the simulator
        const u64 paused = model->history().lastTick();
        QCOMPARE(status->text(), u"Paused at tick %1"_s.arg(paused));
        QTest::qWait(100);
        QCOMPARE(model->history().lastTick(), paused);                     // no ticks while paused

        // An edit ends the paused run; its history stays on screen
        QVERIFY(app.scene->renameComponent(source, u"clk"_s));
        QCOMPARE(app.scene->simState(), SimState::Stopped);
        QVERIFY(status->text().startsWith(u"Stopped at tick"_s));
        QCOMPARE(model->history().lastTick(), paused);
        QCOMPARE(view->selectedRow(), -1);

        // The next run starts again from tick 0; the renamed input is a new signal, shown too
        app.start->click();
        QTRY_VERIFY(!model->history().isEmpty());
        QStringList labels;
        for (const WaveRow& row : model->rows()) labels << model->label(row);
        QCOMPARE(labels, (QStringList{u"Display 1[1:0]"_s, u"clk[1:0]"_s}));

        // Clear: a new circuit, nothing to show
        app.clear->click();
        QVERIFY(!model->hasRun());
        QVERIFY(model->rows().isEmpty());
        QVERIFY(!(tree->topLevelItem(0)->flags() & Qt::ItemIsEnabled));   // the hint
    }

    void theTabsStartButtonIsTheSameAsTheCircuits()
    {
        App app;
        auto* waveStart = child<QPushButton>(&app.window, "waveStartButton");
        app.scene->addComponent(SourceProps::counter(2), app.centre);
        QCOMPARE(waveStart->text(), u"Start"_s);

        waveStart->click();                                                // starts the run
        QCOMPARE(app.scene->simState(), SimState::Running);
        QCOMPARE(waveStart->text(), u"Pause"_s);
        QCOMPARE(app.start->text(), u"Pause"_s);                          // both buttons say so
        QVERIFY(waveStart->property("running").toBool());                  // the amber Pause color

        app.start->click();                                                // the circuit's button pauses it
        QCOMPARE(app.scene->simState(), SimState::Paused);
        QCOMPARE(waveStart->text(), u"Start"_s);
        QVERIFY(!waveStart->property("running").toBool());
        waveStart->click();                                                // and this one continues it
        QCOMPARE(app.scene->simState(), SimState::Running);
        app.clear->click();
    }

    //===================== The signal tree ========================

    void theTreeChecksShowAndHideSignals()
    {
        WaveformModel model;
        QTreeWidget tree;
        WaveSignalTree filler(&tree, &model);
        QCOMPARE(tree.topLevelItemCount(), 1);                             // the hint
        QVERIFY(!(tree.topLevelItem(0)->flags() & Qt::ItemIsEnabled));

        const QList<WaveSignal> list{signalOf({}, u"a"_s, false, {1}), signalOf({}, u"y"_s, true, {2}),
                                     signalOf(u"u"_s, u"x"_s, false, {1}), signalOf(u"u"_s, u"z"_s, true, {2}),
                                     signalOf(u"u/h0"_s, u"s"_s, true, {2})};
        model.startRun(1, list, {1, 2}, u"top"_s);
        QTreeWidgetItem* top = tree.topLevelItem(0);
        QCOMPARE(top->text(0), u"top"_s);
        QCOMPARE(top->childCount(), 3);                                     // a, y, u
        QTreeWidgetItem* u = top->child(2);
        QCOMPARE(u->text(0), u"u (mod)"_s);
        QCOMPARE(u->childCount(), 3);                                       // x, z, h0
        QCOMPARE(u->child(2)->text(0), u"h0 (mod)"_s);
        QCOMPARE(top->child(0)->checkState(0), Qt::Checked);              // the circuit's own are shown
        QCOMPARE(u->child(0)->checkState(0), Qt::Unchecked);
        QCOMPARE(top->child(1)->text(1), u"out"_s);

        u->child(0)->setCheckState(0, Qt::Checked);                        // u/x
        QVERIFY(model.isShown(2));
        top->child(0)->setCheckState(0, Qt::Unchecked);                    // a
        QVERIFY(!model.isShown(0));

        // An instance's check box: all of its ports, and those of the instances in it
        u->setCheckState(0, Qt::Checked);
        QVERIFY(model.isShown(3) && model.isShown(4));
        model.hide(4);                                                      // and the other way: hidden elsewhere
        QCOMPARE(u->child(2)->child(0)->checkState(0), Qt::Unchecked);
    }
};

QTEST_MAIN(TestWaveform)
#include "tst_waveform.moc"
