// Start, Pause, Clear and Compile on the real main window, with the simulator running in its own
// thread exactly as in the application.
//
// The tests that press Compile need yosys and Graphviz dot; they are skipped if those are missing.

#include "TestSupport.h"
#include <QMessageBox>
#include <QRandomGenerator>
#include <QStatusBar>

using namespace TestSupport;

namespace {

const QString goodVerilog = QStringLiteral(
    "module top(input wire clk, input wire [1:0] a, input wire [1:0] b, output reg [1:0] r);\n"
    "always @(posedge clk) r <= a ^ b;\n"
    "endmodule\n");
const QString brokenVerilog = QStringLiteral(
    "module top(input wire a, output wire y);\n"
    "assign y = a &&& ;\n"
    "endmodule\n");

// clock -> NOT -> display. The clock has 16 steps so "continues" and "starts over" can be told apart.
struct ClockCircuit {
    QList<bool> pattern;
    SourceItem* clock = nullptr;
    GateItem* inv = nullptr;
    DisplayItem* out = nullptr;
    WireItem* inWire = nullptr;
    WireItem* outWire = nullptr;

    explicit ClockCircuit(App& app)
    {
        for (int i = 0; i < 16; i++) pattern << (i % 2 == 1);
        clock = dynamic_cast<SourceItem*>(app.scene->addComponent(SourceProps{pattern, 1}, app.centre + QPointF(-150, 0)));
        inv = dynamic_cast<GateItem*>(app.scene->addComponent(GateProps{GType::NOT, 1}, app.centre));
        out = dynamic_cast<DisplayItem*>(app.scene->addComponent(DisplayProps{1, 0}, app.centre + QPointF(150, 0)));
        inWire = connectPorts(app.scene, clock->getOutputPorts()[0], inv->getInputPort(0));
        outWire = connectPorts(app.scene, inv->getOutputPort(), out->getInputPorts()[0]);
        if (!clock || !inv || !out || !inWire || !outWire) qFatal("Test setup: could not build the clock circuit");
    }

    bool in() const { return inv->getInputPort(0)->getValue(); }
    bool notOut() const { return inv->getOutputPort()->getValue(); }
    int step() const { return clock->getIdx(); }
};

bool waitForCompile(App& app)
{
    return QTest::qWaitFor([&app] { return app.compile->isEnabled(); }, 90000);
}

} // namespace

class TestLifecycle : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }

    //===================== The Start/Pause button ========================

    void oneButtonSaysStartOrPause()
    {
        App app;
        QVERIFY(app.window.findChild<QPushButton*>(QStringLiteral("pauseButton")) == nullptr);
        QCOMPARE(app.start->text(), QStringLiteral("Start"));
        QCOMPARE(app.scene->simState(), SimState::Stopped);
        QCOMPARE(app.start->property("running").toBool(), false);         // the .ui style sheet colors it by this

        app.start->click();
        QCOMPARE(app.scene->simState(), SimState::Running);
        QCOMPARE(app.start->text(), QStringLiteral("Pause"));
        QCOMPARE(app.start->property("running").toBool(), true);          // another colour

        app.start->click();
        QCOMPARE(app.scene->simState(), SimState::Paused);
        QCOMPARE(app.start->text(), QStringLiteral("Start"));
        QCOMPARE(app.start->property("running").toBool(), false);
        QVERIFY(app.start->toolTip().contains(QStringLiteral("Continue")));

        app.clear->click();
        QCOMPARE(app.scene->simState(), SimState::Stopped);
        QVERIFY(app.start->toolTip().contains(QStringLiteral("first tick")));
    }

    void pressingItManyTimesNeverGetsStuck()
    {
        App app;
        ClockCircuit circuit(app);
        for (int i = 0; i < 41; i++) {
            app.start->click();
            QTest::qWait(i % 5);
            QCOMPARE(app.start->text() == QStringLiteral("Pause"), app.scene->simState() == SimState::Running);
        }
        QCOMPARE(app.scene->simState(), SimState::Running);              // an odd number of presses
        QTRY_VERIFY(circuit.in() != circuit.notOut());
    }

    //===================== Start ========================

    void startShowsValuesAtOnce()
    {
        App app;
        ClockCircuit circuit(app);
        app.start->click();
        // Well inside the first 100 ms tick period: the first values must not wait for the timer
        QTRY_VERIFY_WITH_TIMEOUT(circuit.notOut(), 60);
        QVERIFY(!circuit.in());
        QCOMPARE(circuit.out->getValue(), 1);
    }

    void valuesFlowWhileRunning()
    {
        App app;
        ClockCircuit circuit(app);
        app.start->click();
        QTRY_VERIFY_WITH_TIMEOUT(circuit.notOut(), 60);

        int low = 0, high = 0;
        for (int i = 0; i < 20; i++) {
            QTest::qWait(53);
            QVERIFY2(circuit.in() != circuit.notOut(), "the NOT output must always be the inverse of its input");
            QCOMPARE(circuit.in(), circuit.pattern[circuit.step()]);     // and agree with the clock step shown
            QCOMPARE(circuit.out->getValue(), circuit.notOut() ? 1 : 0);
            (circuit.notOut() ? high : low)++;
        }
        QVERIFY2(low > 0 && high > 0, "the output should toggle");
    }

    void theCircuitIsReadOnlyWhileRunning()
    {
        App app;
        ClockCircuit circuit(app);
        auto* loose = dynamic_cast<GateItem*>(app.scene->addComponent(GateProps{}, app.centre + QPointF(0, 140)));
        app.start->click();
        const int before = app.scene->items().size();
        auto* viewport = app.canvas->viewport();

        QVERIFY(!app.pages->isEnabled());
        QVERIFY(labelWith(app.panel, QStringLiteral("simulation is running")));

        QTest::mouseClick(viewport, Qt::MiddleButton, {}, app.viewPos(app.centre + QPointF(0, -160)));
        QCOMPARE(app.scene->items().size(), before);                     // nothing placed

        const QPoint from = app.viewPos(circuit.clock->getOutputPorts()[0]->mapToScene(QPointF(0, 0)));
        const QPoint to = app.viewPos(loose->getInputPort(0)->mapToScene(QPointF(0, 0)));
        QTest::mousePress(viewport, Qt::RightButton, {}, from);
        QTest::mouseMove(viewport, to);
        QTest::mouseRelease(viewport, Qt::RightButton, {}, to);
        QCOMPARE(app.scene->items().size(), before);                     // no wire drawn
        QVERIFY(loose->getInputPort(0)->getConnections().isEmpty());

        app.select(loose);
        app.pressDelete();
        app.select(circuit.outWire);
        app.pressDelete();
        app.canvas->setFocus();
        QTest::keyClick(app.canvas, Qt::Key_Backspace);
        QCOMPARE(app.scene->items().size(), before);                     // nothing deleted
        QVERIFY(circuit.outWire->isConnected());

        // Moving things around is still allowed, and the simulation carries on
        loose->setPos(loose->pos() + QPointF(40, 0));
        QCOMPARE(app.scene->simState(), SimState::Running);
        const int step = circuit.step();
        QTRY_VERIFY(circuit.step() != step);
    }

    //===================== Pause and continue ========================

    void pauseFreezesAndStartContinues()
    {
        App app;
        ClockCircuit circuit(app);
        app.start->click();
        QVERIFY(QTest::qWaitFor([&] { return circuit.step() >= 6 && circuit.step() <= 11; }, 5000));

        app.start->click();                                              // Pause
        QTest::qWait(150);                                               // let results already on their way arrive
        const int frozenStep = circuit.step();
        const bool frozenIn = circuit.in(), frozenOut = circuit.notOut();
        QCOMPARE(app.scene->simState(), SimState::Paused);

        QTest::qWait(600);                                               // six tick periods
        QCOMPARE(circuit.step(), frozenStep);
        QCOMPARE(circuit.in(), frozenIn);
        QCOMPARE(circuit.notOut(), frozenOut);
        QVERIFY(frozenIn != frozenOut);                                  // the frozen picture is a consistent one
        QCOMPARE(frozenIn, circuit.pattern[frozenStep]);

        QVERIFY(app.pages->isEnabled());                                 // editing is possible while paused
        QVERIFY(labelWith(app.panel, QStringLiteral("simulation is paused")));
        circuit.inv->setPos(circuit.inv->pos() + QPointF(0, 20));        // moving is not an edit
        QCOMPARE(app.scene->simState(), SimState::Paused);

        app.start->click();                                              // Start again
        QTest::qWait(130);
        const int resumed = circuit.step();
        QVERIFY2(resumed > frozenStep && resumed <= frozenStep + 3,
                 qPrintable(QStringLiteral("paused at step %1, 130 ms after Start it is at step %2 (a restart would be at 0-2)").arg(frozenStep).arg(resumed)));
        QVERIFY(circuit.in() != circuit.notOut());
    }

    void editingWhilePausedEndsTheRun()
    {
        App app;
        ClockCircuit circuit(app);
        auto* loose = app.scene->addComponent(GateProps{}, app.centre + QPointF(0, 140));
        app.start->click();
        QVERIFY(QTest::qWaitFor([&] { return circuit.step() >= 3; }, 5000));
        app.start->click();
        QTest::qWait(120);
        QCOMPARE(app.scene->simState(), SimState::Paused);

        app.select(loose);
        app.pressDelete();                                               // an edit
        QVERIFY(!app.scene->items().contains(loose));
        QCOMPARE(app.scene->simState(), SimState::Stopped);
        QCOMPARE(app.start->text(), QStringLiteral("Start"));
        QVERIFY(!labelWith(app.panel, QStringLiteral("simulation is")));

        // Nothing of the old run is left on the canvas
        QCOMPARE(circuit.step(), 0);
        for (PortItem* port : itemsOfType<PortItem>(app.scene)) QVERIFY(!port->getValue());
        QCOMPARE(circuit.out->getValue(), 0);
        QVERIFY(map.net2wire.empty());

        QTest::qWait(250);                                               // results still on their way are ignored
        for (PortItem* port : itemsOfType<PortItem>(app.scene)) QVERIFY(!port->getValue());

        app.start->click();                                              // the next Start is a fresh run
        QTest::qWait(130);
        QCOMPARE(app.scene->simState(), SimState::Running);
        QVERIFY2(circuit.step() <= 2, qPrintable(QStringLiteral("step %1").arg(circuit.step())));
    }

    void aPropertyEditWhilePausedAlsoEndsTheRun()
    {
        App app;
        ClockCircuit circuit(app);
        app.start->click();
        app.start->click();
        QCOMPARE(app.scene->simState(), SimState::Paused);

        app.select(circuit.out);
        app.editor_<QSpinBox>("displayInputs")->setValue(3);
        QCOMPARE(circuit.out->getInputPorts().size(), 3);
        QCOMPARE(app.scene->simState(), SimState::Stopped);
    }

    //===================== Clear ========================

    void clearStopsAndEmpties()
    {
        App app;
        ClockCircuit circuit(app);
        app.start->click();
        QTRY_VERIFY(circuit.notOut());

        app.clear->click();
        QCOMPARE(app.scene->simState(), SimState::Stopped);
        QVERIFY(app.scene->items().isEmpty());
        QCOMPARE(app.start->text(), QStringLiteral("Start"));
        QVERIFY(app.pages->isEnabled());

        QTest::qWait(400);                                               // late results from the cleared run
        QVERIFY(app.scene->items().isEmpty() && map.net2wire.empty());

        // The window is fully usable afterwards
        ClockCircuit again(app);
        app.start->click();
        QTRY_VERIFY(again.notOut());
        QVERIFY(again.step() <= 2);
    }

    void clearInEveryState()
    {
        App app;
        app.clear->click();                                              // empty and stopped
        app.start->click();                                              // running an empty canvas
        QCOMPARE(app.scene->simState(), SimState::Running);
        app.clear->click();
        app.start->click();
        app.start->click();                                              // paused
        app.clear->click();
        QCOMPARE(app.scene->simState(), SimState::Stopped);
    }

    //===================== Speed ========================

    void speedSlider()
    {
        App app;
        auto* slider = child<QSlider>(&app.window, "slider");
        QSignalSpy period(&app.window, &MainWindow::sendTimerPeriod);

        slider->setValue(1);
        QCOMPARE(period.last().at(0).toInt(), 1000);                     // slowest: one tick per second
        slider->setValue(10000);
        QCOMPARE(period.last().at(0).toInt(), 1);                        // fastest: 1 ms
        slider->setValue(3350);
        QVERIFY(qAbs(period.last().at(0).toInt() - 100) <= 2);           // the starting position is 100 ms

        int previous = 1001;                                             // moving right never slows it down
        for (int value = 1; value <= 10000; value += 487) {
            slider->setValue(value);
            const int now = period.last().at(0).toInt();
            QVERIFY(now <= previous && now >= 1);
            previous = now;
        }
    }

    void speedChangeTakesEffectWhileRunning()
    {
        App app;
        ClockCircuit circuit(app);
        auto* slider = child<QSlider>(&app.window, "slider");
        slider->setValue(1);                                             // 1 s per tick
        app.start->click();
        QTRY_VERIFY_WITH_TIMEOUT(circuit.notOut(), 80);                  // first values still come at once
        QTest::qWait(300);
        QCOMPARE(circuit.step(), 0);                                     // and then nothing for a second

        slider->setValue(10000);                                         // 1 ms per tick
        int changes = 0, last = circuit.step();
        for (int i = 0; i < 40; i++) { QTest::qWait(5); if (circuit.step() != last) { changes++; last = circuit.step(); } }
        QVERIFY2(changes >= 10, qPrintable(QStringLiteral("only %1 step changes seen in 200 ms at full speed").arg(changes)));
    }

    //===================== Compile ========================

    void compileStopsTheRunAndReplacesTheCircuit()
    {
        REQUIRE_HDL_TOOLS();
        App app;
        ClockCircuit circuit(app);
        app.start->click();
        QTRY_VERIFY(circuit.notOut());

        app.editor->setPlainText(goodVerilog);
        app.compile->click();
        QCOMPARE(app.scene->simState(), SimState::Stopped);              // at once, not when the compiler is done
        QVERIFY(!app.compile->isEnabled());
        QVERIFY(app.scene->items().contains(circuit.inv));               // the old circuit stays for now
        // A raw pointer cannot tell "deleted" from "a new item allocated at the same address"
        const QPointer<GateItem> oldGate = circuit.inv;

        QVERIFY(waitForCompile(app));
        QCoreApplication::processEvents();
        QVERIFY(oldGate.isNull());                                       // this is where the app used to crash
        QCOMPARE(itemsOfType<RegisterItem>(app.scene).size(), 2);
        QVERIFY(!itemsOfType<WireItem>(app.scene).isEmpty());
        for (WireItem* wire : itemsOfType<WireItem>(app.scene)) QVERIFY(wire->isConnected());
        QCOMPARE(app.scene->simState(), SimState::Stopped);
        QVERIFY(app.window.statusBar()->currentMessage().contains(QStringLiteral("Compiled")));

        app.start->click();                                              // and the compiled circuit runs
        QTRY_VERIFY(std::ranges::any_of(itemsOfType<PortItem>(app.scene), [](PortItem* port) { return port->getValue(); }));
    }

    void aFailedCompileKeepsTheCircuitAndShowsTheError()
    {
        REQUIRE_HDL_TOOLS();
        App app;
        ClockCircuit circuit(app);
        app.start->click();
        const int before = app.scene->items().size();

        app.editor->setPlainText(brokenVerilog);
        app.compile->click();
        QVERIFY(waitForCompile(app));
        QCoreApplication::processEvents();

        QCOMPARE(app.scene->simState(), SimState::Stopped);              // the simulation was stopped all the same
        QCOMPARE(app.scene->items().size(), before);
        QVERIFY(app.scene->items().contains(circuit.inv));
        QVERIFY(app.window.statusBar()->currentMessage().contains(QStringLiteral("failed")));

        auto* box = app.window.findChild<QMessageBox*>(QStringLiteral("compileErrorBox"));
        QVERIFY(box && box->isVisible());
        QVERIFY2(box->informativeText().contains(QStringLiteral("ERROR")), qPrintable(box->informativeText()));
        box->close();

        app.start->click();                                              // the kept circuit still works
        QTRY_VERIFY(circuit.notOut());
    }

    void startWhileCompilingRunsTheOldCircuitUntilTheNewOneArrives()
    {
        REQUIRE_HDL_TOOLS();
        App app;
        ClockCircuit circuit(app);
        app.editor->setPlainText(goodVerilog);
        app.compile->click();
        app.start->click();
        QCOMPARE(app.scene->simState(), SimState::Running);

        QVERIFY(waitForCompile(app));
        QCoreApplication::processEvents();
        QCOMPARE(app.scene->simState(), SimState::Stopped);              // stopped before the canvas was replaced
        QCOMPARE(itemsOfType<RegisterItem>(app.scene).size(), 2);
    }

    void compilingTwiceGivesTheSameCircuit()
    {
        REQUIRE_HDL_TOOLS();
        App app;
        app.editor->setPlainText(goodVerilog);
        app.compile->click();
        QVERIFY(waitForCompile(app));
        QCoreApplication::processEvents();
        const int components = itemsOfType<ComponentItem>(app.scene).size();
        const int wires = itemsOfType<WireItem>(app.scene).size();
        QVERIFY(components > 0);

        app.compile->click();
        QVERIFY(waitForCompile(app));
        QCoreApplication::processEvents();
        QCOMPARE(itemsOfType<ComponentItem>(app.scene).size(), components);
        QCOMPARE(itemsOfType<WireItem>(app.scene).size(), wires);
    }

    //===================== Everything at once ========================

    void randomActionsAtFullSpeed()
    {
        App app;
        child<QSlider>(&app.window, "slider")->setValue(10000);          // 1 ms per tick
        const bool canCompile = hdlToolsAvailable();
        const QList<bool> pattern{false, true, true, false};
        QRandomGenerator random(20261113);                               // fixed seed: the same sequence every run
        int compiles = 0;

        for (int i = 0; i < 400; i++) {
            switch (random.bounded(12)) {
            case 0: case 1: case 2: case 3:
                app.start->click();
                break;
            case 4:
                app.clear->click();
                break;
            case 5: case 6:
                app.scene->addComponent(i % 2 ? ItemProps{GateProps{GType::NOT, 1}} : ItemProps{SourceProps{pattern, 1}},
                                        app.centre + QPointF(random.bounded(400) - 200, random.bounded(300) - 150));
                break;
            case 7: {   // wire a source to a free gate input
                PortItem* output = nullptr; PortItem* input = nullptr;
                for (SourceItem* source : itemsOfType<SourceItem>(app.scene)) output = source->getOutputPorts()[0];
                for (GateItem* gate : itemsOfType<GateItem>(app.scene)) if (gate->getInputPort(0)->getConnections().isEmpty()) input = gate->getInputPort(0);
                if (output && input) connectPorts(app.scene, output, input);
                break; }
            case 8: {   // select something at random and press Delete
                const QList<QGraphicsItem*> all = app.scene->items();
                if (!all.isEmpty()) { app.select(all[random.bounded(all.size())]); app.pressDelete(); }
                break; }
            case 9:
                if (canCompile && app.compile->isEnabled() && compiles < 3) { app.editor->setPlainText(goodVerilog); app.compile->click(); compiles++; }
                break;
            default:
                break;
            }
            QTest::qWait(random.bounded(10));

            // Whatever happened, the button, the panel and the real state must agree
            const bool running = app.scene->simState() == SimState::Running;
            QCOMPARE(app.start->text() == QStringLiteral("Pause"), running);
            QCOMPARE(!app.pages->isEnabled(), running);
        }
        if (canCompile) QVERIFY(waitForCompile(app));
        QTest::qWait(200);
        app.clear->click();
        QVERIFY(app.scene->items().isEmpty());
    }

    //===================== Closing ========================

    void closingWhileRunning()
    {
        App app;
        ClockCircuit circuit(app);
        app.select(circuit.inv);                                         // a selection, so the panel is watching an item
        app.start->click();
        QTRY_VERIFY(circuit.notOut());
        app.window.close();
    }

    void closingWhilePaused()
    {
        App app;
        ClockCircuit circuit(app);
        app.start->click();
        app.start->click();
        app.window.close();
    }
};

QTEST_MAIN(TestLifecycle)
#include "tst_lifecycle.moc"
