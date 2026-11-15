// The Properties panel, driven through the real main window with simulated clicks and key presses.

#include "TestSupport.h"
#include <QFrame>
#include <QTabWidget>
#include <QScrollArea>
#include <QScrollBar>

using namespace TestSupport;

namespace {

// Like the spin box arrows: sets the value and lets the panel react.
// Replaces the text of a multi-line field the way a user does
void typeText(QPlainTextEdit* edit, const QString& text)
{
    edit->setFocus();
    edit->selectAll();
    const QStringList lines = text.split(u'\n');
    for (qsizetype i = 0; i < lines.size(); i++) {
        if (i > 0) QTest::keyClick(edit, Qt::Key_Return);              // keyClicks cannot type a new line
        if (!lines[i].isEmpty()) QTest::keyClicks(edit, lines[i]);
    }
}

void setSpin(QSpinBox* spin, int value)
{
    spin->setFocus();
    spin->setValue(value);
}

template <typename Item, typename Props>
Item* add(App& app, const Props& props, QPointF offset = {})
{
    return dynamic_cast<Item*>(app.scene->addComponent(props, app.centre + offset));
}

} // namespace

class TestPanel : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }

    //===================== Nothing selected ========================

    void nothingSelectedSaysSoAndOffersTheNextComponent()
    {
        App app;
        QCOMPARE(app.title(), QStringLiteral("Nothing selected"));
        QCOMPARE(app.nextComponent(), QStringLiteral("AND gate"));
        QVERIFY(labelWith(app.panel, QStringLiteral("NEXT COMPONENT")));
        QVERIFY(labelWith(app.panel, QStringLiteral("Middle-click")));
        // For a new gate the type comes from the toolbar, so the panel does not offer it
        QVERIFY(!app.editor_<QComboBox>("gateType")->isVisible());
        QCOMPARE(app.editor_<QSpinBox>("gateInputs")->value(), 2);
    }

    void toolbarButtonsChooseTheNextComponent_data()
    {
        QTest::addColumn<QString>("button");
        QTest::addColumn<QString>("title");
        QTest::newRow("AND") << "andButton" << "AND gate";
        QTest::newRow("NAND") << "nandButton" << "NAND gate";
        QTest::newRow("OR") << "orButton" << "OR gate";
        QTest::newRow("NOR") << "norButton" << "NOR gate";
        QTest::newRow("XOR") << "xorButton" << "XOR gate";
        QTest::newRow("XNOR") << "xnorButton" << "XNOR gate";
        QTest::newRow("NOT") << "notButton" << "NOT gate";
        QTest::newRow("MUX") << "muxButton" << "Multiplexer";
        QTest::newRow("register") << "regButton" << "D flip-flop";
        QTest::newRow("display") << "displayButton" << "Display";
        QTest::newRow("source") << "srcButton" << "Counter source";
    }
    void toolbarButtonsChooseTheNextComponent()
    {
        QFETCH(QString, button);
        QFETCH(QString, title);
        App app;
        app.tool(qPrintable(button))->click();
        QCOMPARE(app.nextComponent(), title);
        QCOMPARE(app.title(), QStringLiteral("Nothing selected"));

        // ...and a middle click places exactly that
        QTest::mouseClick(app.canvas->viewport(), Qt::MiddleButton, {}, app.canvas->viewport()->rect().center());
        const QList<ComponentItem*> placed = itemsOfType<ComponentItem>(app.scene);
        QCOMPARE(placed.size(), 1);
        QCOMPARE(itemTitle(placed.first()->props()), title);
    }

    void settingsOfTheNextComponentAreUsedWhenPlacing()
    {
        App app;
        setSpin(app.editor_<QSpinBox>("gateInputs"), 4);
        QCOMPARE(std::get<GateProps>(app.scene->toolProps()).numInputs, 4);

        app.tool("orButton")->click();                                   // another gate keeps the input count
        QCOMPARE(std::get<GateProps>(app.scene->toolProps()), (GateProps{GType::OR, 4}));
        QTest::mouseClick(app.canvas->viewport(), Qt::MiddleButton, {}, app.canvas->viewport()->rect().center());
        const auto gates = itemsOfType<GateItem>(app.scene);
        QCOMPARE(gates.size(), 1);
        QCOMPARE(gates.first()->getInputPorts().size(), 4);

        app.tool("notButton")->click();                                  // a NOT gate has one input and no choice
        QVERIFY(!app.editor_<QSpinBox>("gateInputs")->isEnabled());
        QCOMPARE(app.editor_<QSpinBox>("gateInputs")->value(), 1);
        app.tool("andButton")->click();
        QVERIFY(app.editor_<QSpinBox>("gateInputs")->isEnabled());
    }

    void registerToolKeepsItsSettings()
    {
        // The old overlay flipped "Enable" every time the register page was opened
        App app;
        app.tool("regButton")->click();
        for (int i = 0; i < 4; i++) {
            app.tool("andButton")->click();
            app.tool("regButton")->click();
            QVERIFY(std::get<RegisterProps>(app.scene->toolProps()).hasEnable);
            QVERIFY(app.editor_<QCheckBox>("registerEnable")->isChecked());
        }

        segment(app.panel, QStringLiteral("JK"))->click();
        segment(app.panel, QStringLiteral("Latch"))->click();
        QCOMPARE(std::get<RegisterProps>(app.scene->toolProps()), (RegisterProps{RType::JK, false, true}));
        QCOMPARE(app.nextComponent(), QStringLiteral("JK latch"));

        app.tool("muxButton")->click();
        app.tool("regButton")->click();                                  // coming back, it is still a JK latch
        QCOMPARE(app.nextComponent(), QStringLiteral("JK latch"));
        QVERIFY(segment(app.panel, QStringLiteral("JK"))->isChecked() && segment(app.panel, QStringLiteral("Latch"))->isChecked());
    }

    void multiplexerToolShowsItsSelectLines()
    {
        App app;
        app.tool("muxButton")->click();
        setSpin(app.editor_<QSpinBox>("muxInputs"), 5);
        QVERIFY(labelWith(app.panel, QStringLiteral("3 select lines")));
        setSpin(app.editor_<QSpinBox>("muxInputs"), 2);
        QVERIFY(labelWith(app.panel, QStringLiteral("1 select line ")));
    }

    //===================== A selected component ========================

    void selectingAComponentShowsIt()
    {
        App app;
        setSpin(app.editor_<QSpinBox>("gateInputs"), 3);                 // the tool default, which must not leak into the selection
        auto* gate = add<GateItem>(app, GateProps{GType::NOR, 5});
        app.select(gate);

        QCOMPARE(app.title(), QStringLiteral("NOR gate"));
        QVERIFY(labelWith(app.panel, QStringLiteral("Selected component")));
        QVERIFY(app.nextComponent().isEmpty());
        QVERIFY(!labelWith(app.panel, QStringLiteral("Middle-click")));
        QVERIFY(app.editor_<QComboBox>("gateType")->isVisible());
        QCOMPARE(app.editor_<QComboBox>("gateType")->currentText(), QStringLiteral("NOR"));
        QCOMPARE(app.editor_<QSpinBox>("gateInputs")->value(), 5);

        app.scene->clearSelection();
        QCoreApplication::processEvents();
        QCOMPARE(app.title(), QStringLiteral("Nothing selected"));
        QCOMPARE(app.editor_<QSpinBox>("gateInputs")->value(), 3);       // back to the tool default
    }

    void gateEditing()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::list({2, 5}, 3), {-200, 0});
        auto* gate = add<GateItem>(app, GateProps{GType::AND, 4});
        WireItem* wire = connectPorts(app.scene, source->getOutputPorts()[0], gate->getInputPort(3));
        QVERIFY(wire);
        app.select(gate);
        auto* inputs = app.editor_<QSpinBox>("gateInputs");
        auto* type = app.editor_<QComboBox>("gateType");

        setSpin(inputs, 2);                                              // would remove the wired fourth input
        QCOMPARE(gate->getInputPorts().size(), 4);
        QVERIFY2(app.message().contains(QStringLiteral("Input 4 is wired")), qPrintable(app.message()));
        QCOMPARE(inputs->value(), 4);                                    // the editor snaps back

        setSpin(inputs, 6);
        QCOMPARE(gate->getInputPorts().size(), 6);
        QVERIFY(app.message().isEmpty());                                // a successful edit clears the message
        QCOMPARE(wire->getEndPort(), gate->getInputPort(3));

        choose(type, QStringLiteral("NOR"));
        QCOMPARE(gate->getGateType(), GType::NOR);
        QCOMPARE(app.title(), QStringLiteral("NOR gate"));

        choose(type, QStringLiteral("NOT"));                             // one input: refused while input 4 is wired
        QCOMPARE(gate->getGateType(), GType::NOR);
        QVERIFY(!app.message().isEmpty());
        QCOMPARE(type->currentText(), QStringLiteral("NOR"));
    }

    void registerEditing()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::list({2, 5}, 3), {-200, 0});
        auto* reg = add<RegisterItem>(app, RegisterProps{RType::D, true, true});
        QVERIFY(connectPorts(app.scene, source->getOutputPorts()[1], reg->getClkPort()));
        app.select(reg);
        QCOMPARE(app.title(), QStringLiteral("D flip-flop"));
        QVERIFY(labelWith(app.panel, QStringLiteral("D, reset, clock, enable")));

        segment(app.panel, QStringLiteral("Latch"))->click();            // the clock pin is wired
        QVERIFY(reg->getClkPort());
        QVERIFY2(app.message().contains(QStringLiteral("clock pin is wired")), qPrintable(app.message()));
        QVERIFY(segment(app.panel, QStringLiteral("Flip-flop"))->isChecked());

        app.editor_<QCheckBox>("registerEnable")->click();
        QVERIFY(!reg->getReadEnablePort());
        QVERIFY(app.message().isEmpty());
        QVERIFY(labelWith(app.panel, QStringLiteral("D, reset, clock")) && !labelWith(app.panel, QStringLiteral("enable")));

        segment(app.panel, QStringLiteral("T"))->click();
        QCOMPARE(reg->getRegType(), RType::T);
        QCOMPARE(app.title(), QStringLiteral("T flip-flop"));
        segment(app.panel, QStringLiteral("SR"))->click();
        QVERIFY(labelWith(app.panel, QStringLiteral("S, R, clock")));
    }

    void opensOnTheCircuitTab()
    {
        // Designer saves whichever tab was showing; the app must start on the circuit regardless
        App app;
        QCOMPARE(child<QTabWidget>(&app.window, "tabWidget")->currentWidget()->objectName(), QStringLiteral("CircuitTab"));
        QVERIFY(app.canvas->isVisible());
    }

    void panelFloatsOverTheCanvas()
    {
        App app;
        auto* frame = child<QFrame>(&app.window, "propertiesFrame");
        QWidget* viewport = app.canvas->viewport();
        const QRect area = viewport->geometry();
        const auto checkPlace = [&] {
            const QRect box = frame->geometry();
            QVERIFY(qAbs(box.height() - area.height() * 2 / 3) <= 1);   // 2/3 of the canvas high
            QCOMPARE(box.right(), area.right() - 12);                     // in its top-right corner
            QCOMPARE(box.top(), area.top() + 12);
        };
        checkPlace();

        // Panning the canvas does not take the panel with it
        app.canvas->horizontalScrollBar()->setValue(app.canvas->horizontalScrollBar()->value() + 300);
        app.canvas->verticalScrollBar()->setValue(app.canvas->verticalScrollBar()->value() + 200);
        QCoreApplication::processEvents();
        checkPlace();

        // Neither does resizing the window
        app.window.resize(1500, 900);
        QTRY_VERIFY(viewport->geometry() != area);
        const QRect bigger = viewport->geometry();
        QTRY_VERIFY(qAbs(frame->height() - bigger.height() * 2 / 3) <= 1);
        QCOMPARE(frame->geometry().right(), bigger.right() - 12);

        // In a short window the settings are taller than the box: they scroll inside it
        app.window.resize(1280, 560);
        SourceProps tall = SourceProps::waveform({true});
        tall.bits = 8;
        auto* source = add<SourceItem>(app, tall);
        app.select(source);
        auto* scroll = child<QScrollArea>(&app.window, "propertiesScroll");
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(qAbs(frame->height() - viewport->height() * 2 / 3) <= 1);   // still 2/3, not stretched by the content
    }

    void listSourceEditing()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::list({1, 2}, 2));
        app.select(source);
        QCOMPARE(app.title(), QStringLiteral("List source"));
        auto* values = app.editor_<QPlainTextEdit>("sourceValues");
        auto* bits = app.editor_<QSpinBox>("sourceBits");
        QCOMPARE(values->toPlainText(), QStringLiteral("1, 2"));
        QCOMPARE(bits->value(), 2);

        typeText(values, QStringLiteral("7, 300"));
        QCOMPARE(source->sourceProps().values, (QList<std::uint64_t>{7, 300}));
        QCOMPARE(source->getOutputPorts().size(), 9);                    // 300 needs 9 bits
        QCOMPARE(bits->value(), 9);
        QCOMPARE(bits->minimum(), 9);                                    // cannot go below what the values need
        QCOMPARE(values->toPlainText(), QStringLiteral("7, 300"));       // the text being typed is not rewritten

        QTest::keyClicks(values, QStringLiteral(", x"));
        QVERIFY2(app.message().contains(QStringLiteral("not a whole number")), qPrintable(app.message()));
        QCOMPARE(source->sourceProps().values, (QList<std::uint64_t>{7, 300}));   // last valid values kept
        for (int i = 0; i < 3; i++) QTest::keyClick(values, Qt::Key_Backspace);
        QVERIFY(app.message().isEmpty());

        // Commas, spaces and new lines separate values; 0x is hex and 0b binary
        typeText(values, QStringLiteral("5 6\n0x1F;0b101"));
        QCOMPARE(source->sourceProps().values, (QList<std::uint64_t>{5, 6, 31, 5}));
        QVERIFY(labelWith(app.panel, QStringLiteral("4 values")));

        setSpin(bits, 12);                                               // more pins than needed is allowed
        QCOMPARE(source->getOutputPorts().size(), 12);

        typeText(values, QStringLiteral("-3"));                          // negative numbers are not values
        QVERIFY(!app.message().isEmpty());
        QCOMPARE(source->sourceProps().values, (QList<std::uint64_t>{5, 6, 31, 5}));

        typeText(values, QStringLiteral("18446744073709551615"));        // the largest 64-bit number
        QCOMPARE(source->sourceProps().values, QList<std::uint64_t>{~std::uint64_t{0}});
        QCOMPARE(source->getOutputPorts().size(), 64);
        QTest::keyClick(values, Qt::Key_6);                              // one digit more does not fit in 64 bits
        QVERIFY2(app.message().contains(QStringLiteral("64 bits")), qPrintable(app.message()));
        QCOMPARE(source->sourceProps().values, QList<std::uint64_t>{~std::uint64_t{0}});

        values->selectAll();
        QTest::keyClick(values, Qt::Key_Delete);                         // an empty field
        QVERIFY2(app.message().contains(QStringLiteral("at least one")), qPrintable(app.message()));
        QCOMPARE(source->sourceProps().values, QList<std::uint64_t>{~std::uint64_t{0}});
    }

    void counterSettingsAndPreview()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::counter(8));
        app.select(source);
        QCOMPARE(app.title(), QStringLiteral("Counter source"));
        auto* start = app.editor_<QLineEdit>("sourceStart");
        auto* step = app.editor_<QLineEdit>("sourceStep");
        auto* every = app.editor_<QSpinBox>("sourceEvery");
        auto* preview = app.editor_<QLabel>("sourcePreview");
        QCOMPARE(preview->text(), QStringLiteral("Steps: 0, 1, 2, 3, 4, 5, 6, 7, ..."));

        typeInto(start, QStringLiteral("250"));
        typeInto(step, QStringLiteral("0x3"));
        setSpin(every, 2);
        QCOMPARE(source->sourceProps().start, std::uint64_t(250));
        QCOMPARE(source->sourceProps().step, std::uint64_t(3));
        QCOMPARE(source->sourceProps().ticksPerStep, 2);
        QCOMPARE(step->text(), QStringLiteral("0x3"));                   // what was typed stays
        QCOMPARE(preview->text(), QStringLiteral("Steps: 250, 253, 0, 3, 6, 9, 12, 15, ..."));   // wraps at 8 bits

        segment(app.panel, QStringLiteral("Down"))->click();
        QVERIFY(source->sourceProps().countDown);
        QCOMPARE(preview->text(), QStringLiteral("Steps: 250, 247, 244, 241, 238, 235, 232, 229, ..."));

        typeInto(start, QStringLiteral("12x"));
        QVERIFY2(app.message().contains(QStringLiteral("not a whole number")), qPrintable(app.message()));
        QCOMPARE(source->sourceProps().start, std::uint64_t(12));        // what was valid on the way there
    }

    void eachModeShowsItsSettings()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::counter(4));
        app.select(source);
        auto* mode = app.editor_<QComboBox>("sourceMode");
        auto* start = app.editor_<QLineEdit>("sourceStart");
        auto* step = app.editor_<QLineEdit>("sourceStep");
        auto* every = app.editor_<QSpinBox>("sourceEvery");
        auto* preview = app.editor_<QLabel>("sourcePreview");
        const auto shown = [&app](QWidget* w) { return w->isVisibleTo(app.panel); };
        QVERIFY(shown(start) && shown(step) && shown(every));

        choose(mode, QStringLiteral("Gray code"));
        QCOMPARE(source->sourceProps().mode, SourceMode::GrayCode);
        QCOMPARE(app.title(), QStringLiteral("Gray code source"));
        QVERIFY(shown(start) && !shown(step));                           // Gray code always steps by one
        QCOMPARE(preview->text(), QStringLiteral("Steps: 0, 1, 3, 2, 6, 7, 5, 4, ..."));

        choose(mode, QStringLiteral("Walking one"));
        QVERIFY(!shown(start) && !shown(step));
        QCOMPARE(preview->text(), QStringLiteral("Steps: 1, 2, 4, 8, 1, 2, 4, 8, ..."));

        choose(mode, QStringLiteral("Random"));
        auto* seed = app.editor_<QSpinBox>("sourceSeed");
        QVERIFY(shown(seed));
        const QString firstNumbers = preview->text();
        setSpin(seed, 99);
        QCOMPARE(source->sourceProps().seed, std::uint32_t(99));
        QVERIFY(preview->text() != firstNumbers);

        choose(mode, QStringLiteral("Constant"));
        auto* constant = app.editor_<QLineEdit>("sourceConstant");
        QVERIFY(shown(constant) && !shown(every));                       // a constant has no steps
        typeInto(constant, QStringLiteral("0b1010_1010_1"));
        QCOMPARE(source->sourceProps().constant, std::uint64_t(0x155));
        QCOMPARE(source->getOutputPorts().size(), 9);                    // widened to fit
        QCOMPARE(preview->text(), QStringLiteral("Outputs 341"));

        // Every mode's settings are kept while another mode is shown
        choose(mode, QStringLiteral("Random"));
        QCOMPARE(seed->value(), 99);
        choose(mode, QStringLiteral("Constant"));
        QCOMPARE(source->sourceProps().constant, std::uint64_t(0x155));
    }

    void repeatIsOfferedForListsAndWaveforms()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::waveform({true, true, false}));
        app.select(source);
        auto* repeat = app.editor_<QCheckBox>("sourceRepeat");
        QVERIFY(repeat->isVisible() && repeat->isChecked());

        repeat->click();                                                 // a pulse that stays 0 afterwards
        QVERIFY(!source->sourceProps().repeat);
        QVERIFY(labelWith(app.panel, QStringLiteral("Steps: 1, 1, 0, 0, 0, 0, 0, 0")));

        auto* mode = app.editor_<QComboBox>("sourceMode");
        choose(mode, QStringLiteral("List"));
        QVERIFY(repeat->isVisible() && !repeat->isChecked());           // kept when switching modes
        choose(mode, QStringLiteral("Counter"));
        QVERIFY(!repeat->isVisible());                                   // a counter wraps at its bits instead
    }

    void waveformPerPin()
    {
        App app;
        SourceProps props = SourceProps::waveform({false, true});
        props.bits = 3;
        auto* source = add<SourceItem>(app, props);
        app.select(source);
        QCOMPARE(app.title(), QStringLiteral("Waveform source"));
        auto* waveform = app.editor_<WaveformEditor>("sourceWaveform");
        auto* pin1 = app.editor_<QLineEdit>("sourcePattern1");
        QCOMPARE(app.editor_<QLineEdit>("sourcePattern0")->text(), QStringLiteral("01"));
        QCOMPARE(pin1->text(), QStringLiteral("0"));                     // a new pin starts as a single 0
        QVERIFY(app.panel->findChild<QLineEdit*>(QStringLiteral("sourcePattern3")) == nullptr);

        typeInto(pin1, QStringLiteral("0011 0"));
        QCOMPARE(source->sourceProps().waves[1], (QList<bool>{0, 0, 1, 1, 0}));
        QCOMPARE(pin1->text(), QStringLiteral("0011 0"));                // spaces the user typed are kept
        QCOMPARE(waveform->values(), (QList<bool>{0, 0, 1, 1, 0}));       // the click editor shows the pin being typed in
        QVERIFY(app.editor_<QToolButton>("sourcePin1")->isChecked());

        QTest::mouseClick(waveform, Qt::LeftButton, {}, QPoint(24 * 4 + 5, 10));   // fifth step
        QCOMPARE(source->sourceProps().waves[1], (QList<bool>{0, 0, 1, 1, 1}));
        QCOMPARE(pin1->text(), QStringLiteral("00111"));

        app.editor_<QToolButton>("sourcePin0")->click();                 // the editor switches pins
        QCOMPARE(waveform->values(), (QList<bool>{0, 1}));
        QCOMPARE(sourceValueAt(source->sourceProps(), 4), std::uint64_t(0b010));   // pin 0 low, pin 1 high

        QTest::keyClick(pin1, Qt::Key_2);                                // not a bit
        QVERIFY2(app.message().contains(QStringLiteral("only contain 0 and 1")), qPrintable(app.message()));
        QCOMPARE(source->sourceProps().waves[1].size(), 5);

        // Fewer pins removes rows; the patterns come back with the pins
        setSpin(app.editor_<QSpinBox>("sourceBits"), 1);
        QVERIFY(app.panel->findChild<QLineEdit*>(QStringLiteral("sourcePattern1")) == nullptr);
        setSpin(app.editor_<QSpinBox>("sourceBits"), 2);
        QCOMPARE(app.editor_<QLineEdit>("sourcePattern1")->text(), QStringLiteral("00111"));
    }

    void sourcePinsWithWiresCannotBeRemoved()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::counter(2), {-200, 0});
        auto* display = add<DisplayItem>(app, DisplayProps{2, 0});
        QVERIFY(connectPorts(app.scene, source->getOutputPorts()[1], display->getInputPorts()[1]));
        app.select(source);

        auto* bits = app.editor_<QSpinBox>("sourceBits");
        setSpin(bits, 1);
        QVERIFY2(app.message().contains(QStringLiteral("Output 2 is wired")), qPrintable(app.message()));
        QCOMPARE(source->getOutputPorts().size(), 2);
        QCOMPARE(bits->value(), 2);

        choose(app.editor_<QComboBox>("sourceMode"), QStringLiteral("Waveform"));   // switching modes keeps the pins
        QCOMPARE(source->getOutputPorts().size(), 2);
        QVERIFY(app.message().isEmpty());
    }

    void displayAndMuxEditing()
    {
        App app;
        auto* display = add<DisplayItem>(app, DisplayProps{2, 2}, {150, 0});
        auto* mux = add<MuxItem>(app, MuxProps{MType::MUX, 2}, {-150, 0});

        app.select(display);
        setSpin(app.editor_<QSpinBox>("displayInputs"), 4);
        setSpin(app.editor_<QSpinBox>("displayOutputs"), 0);
        QCOMPARE(display->getInputPorts().size(), 4);
        QVERIFY(display->getOutputPorts().isEmpty());

        app.select(mux);
        QCOMPARE(app.title(), QStringLiteral("Multiplexer"));
        setSpin(app.editor_<QSpinBox>("muxInputs"), 8);
        QCOMPARE(mux->getInputPorts().size(), 8);
        QCOMPARE(mux->getAddressPorts().size(), 3);
        setSpin(app.editor_<QSpinBox>("muxInputs"), 2);
        QCOMPARE(mux->getAddressPorts().size(), 1);
    }

    void aComponentWiderThanTheLimitsCanStillBeShown()
    {
        // The HDL compiler can create displays and sources wider than the panel's normal range
        App app;
        auto* display = add<DisplayItem>(app, DisplayProps{16, 0});
        app.select(display);
        QCOMPARE(app.editor_<QSpinBox>("displayInputs")->value(), 16);
        QCOMPARE(display->getInputPorts().size(), 16);                   // showing it did not shrink it
    }

    //===================== Selection ========================

    void mouseSelectsAndDeselects()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::list({2, 5}, 3), {-150, 0});
        auto* gate = add<GateItem>(app, GateProps{}, {150, 0});

        QTest::mouseClick(app.canvas->viewport(), Qt::LeftButton, {}, app.viewPos(source));
        QVERIFY(source->isSelected());                                   // a source used to open the overlay instead
        QCOMPARE(app.title(), QStringLiteral("List source"));

        QTest::mouseClick(app.canvas->viewport(), Qt::LeftButton, {}, app.viewPos(gate));
        QVERIFY(gate->isSelected() && !source->isSelected());
        QCOMPARE(app.title(), QStringLiteral("AND gate"));

        QTest::mouseClick(app.canvas->viewport(), Qt::LeftButton, {}, QPoint(30, app.canvas->viewport()->height() - 30));
        QVERIFY(app.scene->selectedItems().isEmpty());                   // a click on empty canvas
        QCOMPARE(app.title(), QStringLiteral("Nothing selected"));

        app.select(gate);
        app.tool("muxButton")->click();                                  // picking a tool deselects too
        QVERIFY(app.scene->selectedItems().isEmpty());
        QCOMPARE(app.nextComponent(), QStringLiteral("Multiplexer"));
    }

    void draggingEmptyCanvasPansWithoutDeselecting()
    {
        App app;
        auto* gate = add<GateItem>(app, GateProps{});
        app.select(gate);

        auto* viewport = app.canvas->viewport();
        const QPoint from(40, viewport->height() - 40);
        const QPointF before = app.canvas->mapToScene(viewport->rect().center());
        QTest::mousePress(viewport, Qt::LeftButton, {}, from);
        QTest::mouseMove(viewport, from + QPoint(-80, -60));
        QTest::mouseRelease(viewport, Qt::LeftButton, {}, from + QPoint(-80, -60));

        QVERIFY(gate->isSelected());                                     // a drag is not a click
        QVERIFY(app.canvas->mapToScene(viewport->rect().center()) != before);
    }

    void severalItemsOrAWire()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::waveform({true}), {-150, 0});
        auto* gate = add<GateItem>(app, GateProps{GType::NOT, 1}, {150, 0});
        WireItem* wire = connectPorts(app.scene, source->getOutputPorts()[0], gate->getInputPort(0));

        source->setSelected(true); gate->setSelected(true);
        QCoreApplication::processEvents();
        QCOMPARE(app.title(), QStringLiteral("2 items selected"));
        QVERIFY(app.nextComponent().isEmpty());

        app.select(wire);
        QCOMPARE(app.title(), QStringLiteral("Wire"));
        QVERIFY(labelWith(app.panel, QStringLiteral("no properties")));
    }

    void deleteKeyRemovesTheSelection()
    {
        App app;
        app.tool("nandButton")->click();
        setSpin(app.editor_<QSpinBox>("gateInputs"), 3);
        QTest::mouseClick(app.canvas->viewport(), Qt::MiddleButton, {}, app.canvas->viewport()->rect().center());
        const auto gates = itemsOfType<GateItem>(app.scene);
        QCOMPARE(gates.size(), 1);
        QCOMPARE(gates.first()->props(), ItemProps(GateProps{GType::NAND, 3}));

        QTest::mouseClick(app.canvas->viewport(), Qt::LeftButton, {}, app.viewPos(gates.first()));
        QCOMPARE(app.title(), QStringLiteral("NAND gate"));
        app.pressDelete();
        QVERIFY(itemsOfType<GateItem>(app.scene).isEmpty());
        QCOMPARE(app.title(), QStringLiteral("Nothing selected"));
        QCOMPARE(app.nextComponent(), QStringLiteral("NAND gate"));

        // Backspace works like Delete
        QTest::mouseClick(app.canvas->viewport(), Qt::MiddleButton, {}, app.canvas->viewport()->rect().center());
        app.select(itemsOfType<GateItem>(app.scene).first());
        app.canvas->setFocus();
        QTest::keyClick(app.canvas, Qt::Key_Backspace);
        QVERIFY(itemsOfType<GateItem>(app.scene).isEmpty());
    }

    void backspaceInThePanelEditsTextNotTheCircuit()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::list({12, 3}, 4));
        app.select(source);
        auto* values = app.editor_<QPlainTextEdit>("sourceValues");
        values->setFocus();
        values->moveCursor(QTextCursor::End);
        QTest::keyClick(values, Qt::Key_Backspace);                      // deletes the "3", not the source
        QVERIFY(app.scene->items().contains(source));
        QCOMPARE(values->toPlainText(), QStringLiteral("12, "));
        QCOMPARE(source->sourceProps().values, QList<std::uint64_t>{12});
    }

    void clearingTheCanvasWhileSomethingIsSelected()
    {
        App app;
        auto* gate = add<GateItem>(app, GateProps{GType::OR, 3});
        app.select(gate);
        app.clear->click();
        QCoreApplication::processEvents();
        QCOMPARE(app.title(), QStringLiteral("Nothing selected"));
        setSpin(app.editor_<QSpinBox>("gateInputs"), 5);                 // must edit the tool, not the deleted gate
        QCOMPARE(std::get<GateProps>(app.scene->toolProps()).numInputs, 5);
    }
};

QTEST_MAIN(TestPanel)
#include "tst_panel.moc"
