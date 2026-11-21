// Start, Pause, Reset, Clear and Compile on the real window, simulator in its own thread

#include "TestSupport.h"
#include <QAbstractItemView>
#include <QCompleter>
#include <QDir>
#include <functional>
#include <QInputDialog>
#include <QTreeWidget>
#include <QLineEdit>
#include <QFileInfo>
#include <QTabWidget>
#include <QListWidget>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QStatusBar>

using namespace Qt::StringLiterals;

using namespace TestSupport;

namespace {

const QString goodVerilog = u"module top(input wire clk, input wire [1:0] a, input wire [1:0] b, output reg [1:0] r);\n"
    "always @(posedge clk) r <= a ^ b;\n"
    "endmodule\n"_s;
const QString brokenVerilog = u"module top(input wire a, output wire y);\n"
    "assign y = a &&& ;\n"
    "endmodule\n"_s;

// clock -> NOT -> display; 16 clock steps tell "continues" from "starts over"
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
        clock = dynamic_cast<SourceItem*>(app.scene->addComponent(SourceProps::waveform(pattern), app.centre + QPointF(-150, 0)));
        inv = dynamic_cast<GateItem*>(app.scene->addComponent(GateProps{GType::NOT, 1}, app.centre));
        out = dynamic_cast<DisplayItem*>(app.scene->addComponent(DisplayProps{1, 0}, app.centre + QPointF(150, 0)));
        inWire = connectPorts(app.scene, clock->getOutputPorts()[0], inv->getInputPort(0));
        outWire = connectPorts(app.scene, inv->getOutputPort(), out->getInputPorts()[0]);
        if (!clock || !inv || !out || !inWire || !outWire) qFatal("Test setup: could not build the clock circuit");
    }

    bool in() const { return inv->getInputPort(0)->getValue(); }
    bool notOut() const { return inv->getOutputPort()->getValue(); }
    int step() const { return int(clock->tick() % u64(pattern.size())); }   // position in the pattern
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
        QVERIFY(app.window.findChild<QPushButton*>(u"pauseButton"_s) == nullptr);
        QCOMPARE(app.start->text(), u"Start"_s);
        QCOMPARE(app.scene->simState(), SimState::Stopped);
        QCOMPARE(app.start->property("running").toBool(), false);         // the style sheet colors it by this

        app.start->click();
        QCOMPARE(app.scene->simState(), SimState::Running);
        QCOMPARE(app.start->text(), u"Pause"_s);
        QCOMPARE(app.start->property("running").toBool(), true);          // another colour

        app.start->click();
        QCOMPARE(app.scene->simState(), SimState::Paused);
        QCOMPARE(app.start->text(), u"Start"_s);
        QCOMPARE(app.start->property("running").toBool(), false);
        QVERIFY(app.start->toolTip().contains(u"Continue"_s));

        app.clear->click();
        QCOMPARE(app.scene->simState(), SimState::Stopped);
        QVERIFY(app.start->toolTip().contains(u"first tick"_s));
    }

    void pressingItManyTimesNeverGetsStuck()
    {
        App app;
        ClockCircuit circuit(app);
        for (int i = 0; i < 41; i++) {
            app.start->click();
            QTest::qWait(i % 5);
            QCOMPARE(app.start->text() == u"Pause"_s, app.scene->simState() == SimState::Running);
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
        QVERIFY(labelWith(app.panel, u"simulation is running"_s));

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
        QVERIFY(labelWith(app.panel, u"simulation is paused"_s));
        circuit.inv->setPos(circuit.inv->pos() + QPointF(0, 20));        // moving is not an edit
        QCOMPARE(app.scene->simState(), SimState::Paused);

        app.start->click();                                              // Start again
        QTest::qWait(130);
        const int resumed = circuit.step();
        QVERIFY2(resumed > frozenStep && resumed <= frozenStep + 3,
                 qPrintable(u"paused at step %1, 130 ms after Start it is at step %2 (a restart would be at 0-2)"_s.arg(frozenStep).arg(resumed)));
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
        QCOMPARE(app.start->text(), u"Start"_s);
        QVERIFY(!labelWith(app.panel, u"simulation is"_s));

        // Nothing of the old run is left on the canvas
        QCOMPARE(circuit.step(), 0);
        for (PortItem* port : itemsOfType<PortItem>(app.scene)) QVERIFY(!port->getValue());
        QCOMPARE(circuit.out->getValue(), 0);
        QVERIFY(app.scene->wireMap().net2wire.empty());

        QTest::qWait(250);                                               // results still on their way are ignored
        for (PortItem* port : itemsOfType<PortItem>(app.scene)) QVERIFY(!port->getValue());

        app.start->click();                                              // the next Start is a fresh run
        QTest::qWait(130);
        QCOMPARE(app.scene->simState(), SimState::Running);
        QVERIFY2(circuit.step() <= 2, qPrintable(u"step %1"_s.arg(circuit.step())));
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

    //===================== Reset ========================

    void resetEndsTheRunAndKeepsTheCircuit()
    {
        App app;
        ClockCircuit circuit(app);
        auto* reset = app.window.findChild<QPushButton*>(u"resetButton"_s);
        QVERIFY(reset);
        QVERIFY(!reset->isEnabled());                                    // nothing to reset before a run
        const int itemCount = int(app.scene->items().size());

        // While running: back to the editing view, the circuit untouched, no values left
        app.start->click();
        QVERIFY(reset->isEnabled());
        QVERIFY(QTest::qWaitFor([&] { return circuit.step() >= 3; }, 5000));
        reset->click();
        QCOMPARE(app.scene->simState(), SimState::Stopped);
        QCOMPARE(app.start->text(), u"Start"_s);
        QVERIFY(!reset->isEnabled());
        QCOMPARE(int(app.scene->items().size()), itemCount);
        QCOMPARE(circuit.step(), 0);
        QTest::qWait(250);                                               // results still on their way are ignored
        for (PortItem* port : itemsOfType<PortItem>(app.scene)) QVERIFY(!port->getValue());
        QCOMPARE(circuit.out->getValue(), 0);

        // While paused, the same; the next Start is a fresh run from the first tick
        app.start->click();
        QVERIFY(QTest::qWaitFor([&] { return circuit.step() >= 3; }, 5000));
        app.start->click();
        QCOMPARE(app.scene->simState(), SimState::Paused);
        QVERIFY(reset->isEnabled());
        reset->click();
        QCOMPARE(app.scene->simState(), SimState::Stopped);
        app.start->click();
        QTest::qWait(130);
        QCOMPARE(app.scene->simState(), SimState::Running);
        QVERIFY2(circuit.step() <= 2, qPrintable(u"step %1"_s.arg(circuit.step())));
    }

    //===================== Files ========================

    void savingAndOpeningOnTheFilesTab()
    {
        App app;
        auto* tabs = child<QTabWidget>(&app.window, "tabWidget");
        auto* list = child<QTreeWidget>(&app.window, "savesList");
        auto* fileName = child<QLabel>(&app.window, "currentFileName");
        auto* source = dynamic_cast<SourceItem*>(app.scene->addComponent(SourceProps::counter(2), app.centre));
        auto* display = dynamic_cast<DisplayItem*>(app.scene->addComponent(DisplayProps{2, 0}, app.centre + QPointF(200, 0)));
        QVERIFY(connectPorts(app.scene, source->getOutputPorts()[0], display->getInputPorts()[0]));
        QVERIFY(app.window.isWindowModified());
        QCOMPARE(fileName->text(), u"Untitled (unsaved changes)"_s);

        // Save on an untitled circuit asks for a name and saves into the saves folder
        whenDialogOpens([](QWidget* dialog) { typeName(dialog, u"half adder"_s); });
        child<QPushButton>(&app.window, "saveButton")->click();
        const QString path = QDir(app.saves.path()).filePath(u"half adder.bitsim"_s);
        QVERIFY(QFileInfo::exists(path));
        QCOMPARE(app.window.currentFile(), QFileInfo(path).absoluteFilePath());
        QVERIFY(!app.window.isWindowModified());
        QCOMPARE(app.window.windowTitle(), u"half adder[*] - BitSimulator"_s);
        QCOMPARE(list->topLevelItemCount(), 1);
        QCOMPARE(list->topLevelItem(0)->text(0), u"half adder"_s);

        // A move counts as a change; with the prompt on, New asks, and Cancel keeps everything
        app.window.setAskToSave(true);
        const QPointF savedPos = source->pos();
        source->setPos(savedPos + QPointF(400, 0));
        QVERIFY(app.window.isWindowModified());
        whenDialogOpens([](QWidget* dialog) { pressButton(dialog, QMessageBox::Cancel); });
        child<QPushButton>(&app.window, "newButton")->click();
        QVERIFY(app.scene->items().contains(source));

        // Discard: an empty, untitled canvas
        whenDialogOpens([](QWidget* dialog) { pressButton(dialog, QMessageBox::Discard); });
        child<QPushButton>(&app.window, "newButton")->click();
        QVERIFY(itemsOfType<ComponentItem>(app.scene).isEmpty());
        QVERIFY(app.window.currentFile().isEmpty());
        QVERIFY(!app.window.isWindowModified());

        // Opening from the list brings back the saved circuit, as it was saved
        tabs->setCurrentWidget(child<QWidget>(&app.window, "filesTab"));
        emit list->itemActivated(list->topLevelItem(0), 0);
        QCOMPARE(tabs->currentWidget(), child<QWidget>(&app.window, "CircuitTab"));
        const QList<SourceItem*> sources = itemsOfType<SourceItem>(app.scene);
        QCOMPARE(sources.size(), 1);
        QCOMPARE(sources[0]->pos(), savedPos);
        QCOMPARE(itemsOfType<WireItem>(app.scene).size(), 1);
        QVERIFY(!app.window.isWindowModified());
        QCOMPARE(app.window.currentFile(), QFileInfo(path).absoluteFilePath());

        // Ctrl+S writes the file again without asking for a name
        sources[0]->setPos(savedPos + QPointF(0, 300));
        app.window.activateWindow();                                     // shortcuts need the active window
        QVERIFY(QTest::qWaitForWindowActive(&app.window));
        QTest::keyClick(app.canvas->viewport(), Qt::Key_S, Qt::ControlModifier);
        QVERIFY(!app.window.isWindowModified());
        app.window.setAskToSave(false);
        app.clear->click();
        QVERIFY(app.window.openCircuit(path));
        QCOMPARE(itemsOfType<SourceItem>(app.scene)[0]->pos(), savedPos + QPointF(0, 300));

        // The run can go on while saving; a compile's result is untitled
        app.start->click();
        QVERIFY(app.window.saveCircuit(path));
        QCOMPARE(app.scene->simState(), SimState::Running);
        app.clear->click();
    }

    void ctrlZAndCtrlYUndoAndRedo()
    {
        App app;
        app.window.activateWindow();                                     // shortcuts need the active window
        QVERIFY(QTest::qWaitForWindowActive(&app.window));
        auto* source = dynamic_cast<SourceItem*>(app.scene->addComponent(SourceProps::counter(1), app.centre));
        QVERIFY(app.window.saveCircuit(QDir(app.saves.path()).filePath(u"one.bitsim"_s)));
        QVERIFY(!app.window.isWindowModified());
        const auto key = [&](Qt::Key k) { QTest::keyClick(app.canvas->viewport(), k, Qt::ControlModifier); };

        // A drag is one step, and undoing it is back at the saved state
        const QPointF start = source->pos();
        app.canvas->centerOn(source);
        const QPoint grab = app.canvas->mapFromScene(source->sceneBoundingRect().center());
        QTest::mousePress(app.canvas->viewport(), Qt::LeftButton, {}, grab);
        for (int x = 10; x <= 80; x += 10) QTest::mouseMove(app.canvas->viewport(), grab + QPoint(x, 0));
        QTest::mouseRelease(app.canvas->viewport(), Qt::LeftButton, {}, grab + QPoint(80, 0));
        QVERIFY(source->pos() != start);
        QVERIFY(app.window.isWindowModified());
        key(Qt::Key_Z);
        QCOMPARE(itemsOfType<SourceItem>(app.scene)[0]->pos(), start);
        QVERIFY(!app.window.isWindowModified());
        key(Qt::Key_Y);
        QVERIFY(itemsOfType<SourceItem>(app.scene)[0]->pos() != start);
        QVERIFY(app.window.isWindowModified());

        // Clear can be undone too
        app.clear->click();
        QVERIFY(itemsOfType<SourceItem>(app.scene).isEmpty());
        key(Qt::Key_Z);
        QCOMPARE(itemsOfType<SourceItem>(app.scene).size(), 1);

        // In a text field, Ctrl+Z is the field's own; while running, nothing changes
        auto* bar = child<QLineEdit>(&app.window, "searchBar");
        bar->setFocus();
        QTest::keyClicks(bar, u"ab"_s);
        QTest::keyClick(bar, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(itemsOfType<SourceItem>(app.scene).size(), 1);
        app.canvas->setFocus();
        app.start->click();
        key(Qt::Key_Z);
        QCOMPARE(itemsOfType<SourceItem>(app.scene).size(), 1);
        QVERIFY(app.window.statusBar()->currentMessage().contains(u"Pause"_s));
        app.clear->click();
    }

    void ctrlCXAndVCopyCutAndPasteAtTheMouse()
    {
        App app;
        auto* gate = app.scene->addComponent(GateProps{GType::OR, 2}, app.centre);
        app.select(gate);
        app.canvas->setFocus();
        const auto key = [&](Qt::Key k) { QTest::keyClick(app.canvas, k, Qt::ControlModifier); };

        key(Qt::Key_C);
        QCOMPARE(app.window.statusBar()->currentMessage(), u"Copied 1 component."_s);

        // At the mouse when it is over the canvas
        const QPointF spot = app.centre + QPointF(200, 100);
        QCursor::setPos(app.canvas->viewport()->mapToGlobal(app.viewPos(spot)));
        key(Qt::Key_V);
        QList<GateItem*> gates = itemsOfType<GateItem>(app.scene);
        QCOMPARE(gates.size(), 2);
        GateItem* pasted = gates[0] == gate ? gates[1] : gates[0];
        QVERIFY2((pasted->pos() - spot).manhattanLength() <= 2 * CircuitScene::GridSpacing, qPrintable(u"pasted at %1, %2"_s.arg(pasted->pos().x()).arg(pasted->pos().y())));
        QVERIFY(pasted->isSelected() && !gate->isSelected());

        key(Qt::Key_X);
        QCOMPARE(itemsOfType<GateItem>(app.scene).size(), 1);
        QCOMPARE(app.window.statusBar()->currentMessage(), u"Cut 1 component."_s);

        // In a text field, Ctrl+C and Ctrl+V are the field's own
        auto* bar = child<QLineEdit>(&app.window, "searchBar");
        bar->setFocus();
        QTest::keyClick(bar, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(itemsOfType<GateItem>(app.scene).size(), 1);

        // While running nothing is pasted, and the status bar says why
        app.canvas->setFocus();
        app.start->click();
        key(Qt::Key_V);
        QCOMPARE(itemsOfType<GateItem>(app.scene).size(), 1);
        QVERIFY(app.window.statusBar()->currentMessage().contains(u"Pause"_s));
        app.clear->click();
    }

    void aDamagedFileIsReportedAndTheCircuitKept()
    {
        App app;
        app.scene->addComponent(GateProps{}, app.centre);
        QFile file(QDir(app.saves.path()).filePath(u"broken.bitsim"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"app": "BitSimulator", "version": 1, "components": [{"kind": "toaster"}]})");
        file.close();
        QString message;
        whenDialogOpens([&message](QWidget* dialog) {
            message = qobject_cast<QMessageBox*>(dialog)->text();
            pressButton(dialog, QMessageBox::Ok);
        });
        QVERIFY(!app.window.openCircuit(file.fileName()));
        QVERIFY2(message.contains(u"unknown kind"_s), qPrintable(message));
        QCOMPARE(itemsOfType<GateItem>(app.scene).size(), 1);
        QVERIFY(app.window.currentFile().isEmpty());
    }

    //===================== Search ========================

    void theSearchBarListsMatchesAndJumpsToThem()
    {
        App app;
        auto* bar = child<QLineEdit>(&app.window, "searchBar");
        auto* completer = app.window.findChild<QCompleter*>();
        QVERIFY(completer);
        auto* near = dynamic_cast<SourceItem*>(app.scene->addComponent(SourceProps::counter(1), app.centre));
        auto* far = dynamic_cast<SourceItem*>(app.scene->addComponent(SourceProps::counter(1), app.centre + QPointF(5000, 3000)));
        auto* other = dynamic_cast<DisplayItem*>(app.scene->addComponent(DisplayProps{1, 0}, app.centre + QPointF(300, 0)));
        QVERIFY(app.scene->renameComponent(near, u"alu_a"_s));
        QVERIFY(app.scene->renameComponent(far, u"alu_b"_s));
        QVERIFY(app.scene->renameComponent(other, u"result"_s));
        const auto viewCentre = [&] { return app.canvas->mapToScene(app.canvas->viewport()->rect().center()); };
        const auto shows = [&](ComponentItem* item) {
            const double pixels = QLineF(viewCentre(), item->sceneBoundingRect().center()).length() * app.canvas->transform().m11();
            return pixels < 3 && item->isSelected();                     // centred, to the pixel
        };

        // Typing lists the matches below the bar, none highlighted
        bar->setFocus();
        QTest::keyClicks(bar, u"alu"_s);
        QTRY_VERIFY(completer->popup()->isVisible());
        QCOMPARE(completer->popup()->model()->rowCount(), 2);
        QVERIFY(completer->popup()->model()->index(0, 0).data().toString().startsWith(u"alu_a"_s));
        QVERIFY(!completer->popup()->currentIndex().isValid());

        // Enter with two matches does not jump
        QTest::keyClick(completer->popup(), Qt::Key_Return);
        QVERIFY(!far->isSelected() && !near->isSelected());

        // Clicking a row jumps to it, zooms in and selects it
        QTest::keyClicks(bar, u"_"_s);
        QTRY_VERIFY(completer->popup()->isVisible());
        QAbstractItemView* list = completer->popup();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualRect(list->model()->index(1, 0)).center());
        QVERIFY(shows(far));
        QVERIFY(!completer->popup()->isVisible());
        QVERIFY(app.canvas->transform().m11() > 1);

        // One match: Enter jumps to it, also from inside the run
        app.start->click();
        bar->clear();
        QTest::keyClicks(bar, u"^res"_s);
        QTRY_VERIFY(completer->popup()->isVisible());
        QCOMPARE(completer->popup()->model()->rowCount(), 1);
        QTest::keyClick(completer->popup(), Qt::Key_Return);
        QVERIFY(shows(other));
        QCOMPARE(app.scene->simState(), SimState::Running);              // the search leaves the run alone

        // No match: no list
        bar->clear();
        QTest::keyClicks(bar, u"zzz"_s);
        QTest::qWait(50);
        QVERIFY(!completer->popup()->isVisible());
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
        QCOMPARE(app.start->text(), u"Start"_s);
        QVERIFY(app.pages->isEnabled());

        QTest::qWait(400);                                               // late results from the cleared run
        QVERIFY(app.scene->items().isEmpty() && app.scene->wireMap().net2wire.empty());

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
        // Values show once per frame, so count the ticks the clock got through
        const u64 before = circuit.clock->tick();
        QTest::qWait(200);
        const u64 ticks = circuit.clock->tick() - before;
        QVERIFY2(ticks >= 50, qPrintable(u"only %1 ticks in 200 ms at full speed"_s.arg(ticks)));
    }

    //===================== Compile ========================

    void compileStopsTheRunAndReplacesTheCircuit()
    {
        if (!hdlToolsAvailable()) QSKIP(MissingHdlTools);
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
        QVERIFY(app.window.statusBar()->currentMessage().contains(u"Compiled"_s));

        app.start->click();                                              // and the compiled circuit runs
        QTRY_VERIFY(std::ranges::any_of(itemsOfType<PortItem>(app.scene), [](PortItem* port) { return port->getValue(); }));
    }

    void compileDirListsChecksAndCompilesAFolder()
    {
        if (!hdlToolsAvailable()) QSKIP(MissingHdlTools);
        // chip (the top) uses and2 from a subfolder; tb is a testbench that would otherwise be the top
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto write = [&dir](const QString& name, const QString& text) {
            QDir().mkpath(QFileInfo(dir.filePath(name)).absolutePath());
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
            file.write(text.toUtf8());
        };
        write("cells/and2.v", "module and2(input wire a, b, output wire y);\n  assign y = a & b;\nendmodule\n");
        write("chip.sv", "module chip(input logic p, q, output logic z);\n  and2 g(.a(p), .b(q), .y(z));\nendmodule\n");
        write("tb/tb.v", "module tb;\n  reg p, q; wire z;\n  chip dut(.p(p), .q(q), .z(z));\n  initial $display(\"hi\");\nendmodule\n");
        write("cells/defs.vh", "`define UNUSED 1\n");
        write("notes.txt", "not Verilog\n");

        App app;
        auto* tabs = child<QTabWidget>(&app.window, "tabWidget");
        auto* tab = child<QWidget>(&app.window, "CompileDirTab");
        QCOMPARE(tabs->tabText(tabs->indexOf(tab)), u"Compile Dir"_s);
        auto* path = child<QLineEdit>(&app.window, "compileDirPath");
        auto* files = child<QListWidget>(&app.window, "compileFilesDisplayer");
        auto* count = child<QLabel>(&app.window, "compileDirCount");
        auto* compileDir = child<QPushButton>(&app.window, "compileDirButton");
        QVERIFY(!compileDir->isEnabled());                              // nothing chosen yet

        path->setText(dir.path());
        path->editingFinished();                                    // like pressing Enter
        QStringList listed;
        for (int row = 0; row < files->count(); row++) {
            listed << files->item(row)->text();
            QCOMPARE(files->item(row)->checkState(), Qt::Checked);
        }
        QCOMPARE(listed, (QStringList{QDir::toNativeSeparators("cells/and2.v"), "chip.sv", QDir::toNativeSeparators("tb/tb.v")}));
        QCOMPARE(count->text(), u"3 of 3 files selected"_s);
        QVERIFY(compileDir->isEnabled());

        files->item(2)->setCheckState(Qt::Unchecked);                    // leave the testbench out
        QCOMPARE(count->text(), u"2 of 3 files selected"_s);

        // The run stops, both compile buttons wait, the result shows on the canvas
        ClockCircuit circuit(app);
        app.start->click();
        tabs->setCurrentWidget(tab);
        compileDir->click();
        QCOMPARE(app.scene->simState(), SimState::Stopped);
        QVERIFY(!compileDir->isEnabled() && !app.compile->isEnabled());
        QVERIFY(waitForCompile(app));
        QCoreApplication::processEvents();
        QVERIFY2(app.window.statusBar()->currentMessage().contains(u"Compiled"_s), qPrintable(app.window.statusBar()->currentMessage()));
        // and2, from another file, is a block in chip; its AND gate is inside it
        QCOMPARE(itemsOfType<GateItem>(app.scene).size(), 0);
        const QList<BlockItem*> blocks = itemsOfType<BlockItem>(app.scene);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks.first()->blockProps().module, u"and2"_s);
        app.canvas->openBlock(blocks.first());
        QCOMPARE(itemsOfType<GateItem>(app.canvas->currentScene()).size(), 1);
        QCOMPARE(itemsOfType<GateItem>(app.canvas->currentScene()).first()->getGateType(), GType::AND);
        QVERIFY(!child<QFrame>(&app.window, "propertiesFrame")->isVisible());   // nothing to edit inside a block
        app.canvas->showLevel(0);
        QVERIFY(child<QFrame>(&app.window, "propertiesFrame")->isVisible());
        QCOMPARE(tabs->currentWidget(), app.window.findChild<QWidget*>(u"CircuitTab"_s));
        QVERIFY(compileDir->isEnabled());

        // Unchecking everything, or a folder that does not exist, leaves nothing to compile
        for (int row = 0; row < files->count(); row++) files->item(row)->setCheckState(Qt::Unchecked);
        QVERIFY(!compileDir->isEnabled());
        path->setText(dir.filePath("missing"));
        path->editingFinished();
        QCOMPARE(files->count(), 0);
        QCOMPARE(count->text(), u"This folder does not exist."_s);
    }

    void aFailedCompileKeepsTheCircuitAndShowsTheError()
    {
        if (!hdlToolsAvailable()) QSKIP(MissingHdlTools);
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
        QVERIFY(app.window.statusBar()->currentMessage().contains(u"failed"_s));

        auto* box = app.window.findChild<QMessageBox*>(u"compileErrorBox"_s);
        QVERIFY(box && box->isVisible());
        QVERIFY2(box->informativeText().contains(u"Parse error"_s), qPrintable(box->informativeText()));
        box->close();

        app.start->click();                                              // the kept circuit still works
        QTRY_VERIFY(circuit.notOut());
    }

    void startWhileCompilingRunsTheOldCircuitUntilTheNewOneArrives()
    {
        if (!hdlToolsAvailable()) QSKIP(MissingHdlTools);
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
        if (!hdlToolsAvailable()) QSKIP(MissingHdlTools);
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
                app.scene->addComponent(i % 2 ? ItemProps{GateProps{GType::NOT, 1}} : ItemProps{SourceProps::waveform(pattern)},
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
            QCOMPARE(app.start->text() == u"Pause"_s, running);
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
