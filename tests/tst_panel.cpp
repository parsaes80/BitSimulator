// The Properties panel, driven through the real main window with simulated clicks and key presses.

#include "TestSupport.h"
#include <QFrame>
#include <QTabWidget>
#include <QScrollArea>
#include <QScrollBar>

using namespace Qt::StringLiterals;

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
        QCOMPARE(app.title(), u"Nothing selected"_s);
        QCOMPARE(app.nextComponent(), u"AND gate"_s);
        QVERIFY(labelWith(app.panel, u"NEXT COMPONENT"_s));
        QVERIFY(labelWith(app.panel, u"Middle-click"_s));
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
        QCOMPARE(app.title(), u"Nothing selected"_s);

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

        segment(app.panel, u"JK"_s)->click();
        segment(app.panel, u"Latch"_s)->click();
        QCOMPARE(std::get<RegisterProps>(app.scene->toolProps()), (RegisterProps{RType::JK, false, true}));
        QCOMPARE(app.nextComponent(), u"JK latch"_s);

        app.tool("muxButton")->click();
        app.tool("regButton")->click();                                  // coming back, it is still a JK latch
        QCOMPARE(app.nextComponent(), u"JK latch"_s);
        QVERIFY(segment(app.panel, u"JK"_s)->isChecked() && segment(app.panel, u"Latch"_s)->isChecked());
    }

    void multiplexerToolShowsItsSelectLines()
    {
        App app;
        app.tool("muxButton")->click();
        setSpin(app.editor_<QSpinBox>("muxInputs"), 5);
        QVERIFY(labelWith(app.panel, u"3 select lines"_s));
        setSpin(app.editor_<QSpinBox>("muxInputs"), 2);
        QVERIFY(labelWith(app.panel, u"1 select line "_s));
    }

    //===================== A selected component ========================

    void selectingAComponentShowsIt()
    {
        App app;
        setSpin(app.editor_<QSpinBox>("gateInputs"), 3);                 // the tool default, which must not leak into the selection
        auto* gate = add<GateItem>(app, GateProps{GType::NOR, 5});
        app.select(gate);

        QCOMPARE(app.title(), u"NOR gate"_s);
        QVERIFY(labelWith(app.panel, u"Selected component"_s));
        QVERIFY(app.nextComponent().isEmpty());
        QVERIFY(!labelWith(app.panel, u"Middle-click"_s));
        QVERIFY(app.editor_<QComboBox>("gateType")->isVisible());
        QCOMPARE(app.editor_<QComboBox>("gateType")->currentText(), u"NOR"_s);
        QCOMPARE(app.editor_<QSpinBox>("gateInputs")->value(), 5);

        app.scene->clearSelection();
        QCoreApplication::processEvents();
        QCOMPARE(app.title(), u"Nothing selected"_s);
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
        QVERIFY2(app.message().contains(u"Input 4 is wired"_s), qPrintable(app.message()));
        QCOMPARE(inputs->value(), 4);                                    // the editor snaps back

        setSpin(inputs, 6);
        QCOMPARE(gate->getInputPorts().size(), 6);
        QVERIFY(app.message().isEmpty());                                // a successful edit clears the message
        QCOMPARE(wire->getEndPort(), gate->getInputPort(3));

        choose(type, u"NOR"_s);
        QCOMPARE(gate->getGateType(), GType::NOR);
        QCOMPARE(app.title(), u"NOR gate"_s);

        choose(type, u"NOT"_s);                             // one input: refused while input 4 is wired
        QCOMPARE(gate->getGateType(), GType::NOR);
        QVERIFY(!app.message().isEmpty());
        QCOMPARE(type->currentText(), u"NOR"_s);
    }

    void registerEditing()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::list({2, 5}, 3), {-200, 0});
        auto* reg = add<RegisterItem>(app, RegisterProps{RType::D, true, true});
        QVERIFY(connectPorts(app.scene, source->getOutputPorts()[1], reg->getClkPort()));
        app.select(reg);
        QCOMPARE(app.title(), u"D flip-flop"_s);
        QVERIFY(labelWith(app.panel, u"D, reset, clock, enable"_s));

        segment(app.panel, u"Latch"_s)->click();            // the clock pin is wired
        QVERIFY(reg->getClkPort());
        QVERIFY2(app.message().contains(u"clock pin is wired"_s), qPrintable(app.message()));
        QVERIFY(segment(app.panel, u"Flip-flop"_s)->isChecked());

        app.editor_<QCheckBox>("registerEnable")->click();
        QVERIFY(!reg->getReadEnablePort());
        QVERIFY(app.message().isEmpty());
        QVERIFY(labelWith(app.panel, u"D, reset, clock"_s) && !labelWith(app.panel, u"enable"_s));

        segment(app.panel, u"T"_s)->click();
        QCOMPARE(reg->getRegType(), RType::T);
        QCOMPARE(app.title(), u"T flip-flop"_s);
        segment(app.panel, u"SR"_s)->click();
        QVERIFY(labelWith(app.panel, u"S, R, clock"_s));
    }

    void opensOnTheCircuitTab()
    {
        // Designer saves whichever tab was showing; the app must start on the circuit regardless
        App app;
        QCOMPARE(child<QTabWidget>(&app.window, "tabWidget")->currentWidget()->objectName(), u"CircuitTab"_s);
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
        QCOMPARE(app.title(), u"List source"_s);
        auto* values = app.editor_<QPlainTextEdit>("sourceValues");
        auto* bits = app.editor_<QSpinBox>("sourceBits");
        QCOMPARE(values->toPlainText(), u"1, 2"_s);
        QCOMPARE(bits->value(), 2);

        typeText(values, u"7, 300"_s);
        QCOMPARE(source->sourceProps().values, (QList<std::uint64_t>{7, 300}));
        QCOMPARE(source->getOutputPorts().size(), 9);                    // 300 needs 9 bits
        QCOMPARE(bits->value(), 9);
        QCOMPARE(bits->minimum(), 9);                                    // cannot go below what the values need
        QCOMPARE(values->toPlainText(), u"7, 300"_s);       // the text being typed is not rewritten

        QTest::keyClicks(values, u", x"_s);
        QVERIFY2(app.message().contains(u"not a whole number"_s), qPrintable(app.message()));
        QCOMPARE(source->sourceProps().values, (QList<std::uint64_t>{7, 300}));   // last valid values kept
        for (int i = 0; i < 3; i++) QTest::keyClick(values, Qt::Key_Backspace);
        QVERIFY(app.message().isEmpty());

        // Commas, spaces and new lines separate values; 0x is hex and 0b binary
        typeText(values, u"5 6\n0x1F;0b101"_s);
        QCOMPARE(source->sourceProps().values, (QList<std::uint64_t>{5, 6, 31, 5}));
        QVERIFY(labelWith(app.panel, u"4 values"_s));

        setSpin(bits, 12);                                               // more pins than needed is allowed
        QCOMPARE(source->getOutputPorts().size(), 12);

        typeText(values, u"-3"_s);                          // negative numbers are not values
        QVERIFY(!app.message().isEmpty());
        QCOMPARE(source->sourceProps().values, (QList<std::uint64_t>{5, 6, 31, 5}));

        typeText(values, u"18446744073709551615"_s);        // the largest 64-bit number
        QCOMPARE(source->sourceProps().values, QList<std::uint64_t>{~std::uint64_t{0}});
        QCOMPARE(source->getOutputPorts().size(), 64);
        QTest::keyClick(values, Qt::Key_6);                              // one digit more does not fit in 64 bits
        QVERIFY2(app.message().contains(u"64 bits"_s), qPrintable(app.message()));
        QCOMPARE(source->sourceProps().values, QList<std::uint64_t>{~std::uint64_t{0}});

        values->selectAll();
        QTest::keyClick(values, Qt::Key_Delete);                         // an empty field
        QVERIFY2(app.message().contains(u"at least one"_s), qPrintable(app.message()));
        QCOMPARE(source->sourceProps().values, QList<std::uint64_t>{~std::uint64_t{0}});
    }

    void counterSettingsAndPreview()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps::counter(8));
        app.select(source);
        QCOMPARE(app.title(), u"Counter source"_s);
        auto* start = app.editor_<QLineEdit>("sourceStart");
        auto* step = app.editor_<QLineEdit>("sourceStep");
        auto* every = app.editor_<QSpinBox>("sourceEvery");
        auto* preview = app.editor_<QLabel>("sourcePreview");
        QCOMPARE(preview->text(), u"Steps: 0, 1, 2, 3, 4, 5, 6, 7, ..."_s);

        typeInto(start, u"250"_s);
        typeInto(step, u"0x3"_s);
        setSpin(every, 2);
        QCOMPARE(source->sourceProps().start, std::uint64_t(250));
        QCOMPARE(source->sourceProps().step, std::uint64_t(3));
        QCOMPARE(source->sourceProps().ticksPerStep, 2);
        QCOMPARE(step->text(), u"0x3"_s);                   // what was typed stays
        QCOMPARE(preview->text(), u"Steps: 250, 253, 0, 3, 6, 9, 12, 15, ..."_s);   // wraps at 8 bits

        segment(app.panel, u"Down"_s)->click();
        QVERIFY(source->sourceProps().countDown);
        QCOMPARE(preview->text(), u"Steps: 250, 247, 244, 241, 238, 235, 232, 229, ..."_s);

        typeInto(start, u"12x"_s);
        QVERIFY2(app.message().contains(u"not a whole number"_s), qPrintable(app.message()));
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

        choose(mode, u"Gray code"_s);
        QCOMPARE(source->sourceProps().mode, SourceMode::GrayCode);
        QCOMPARE(app.title(), u"Gray code source"_s);
        QVERIFY(shown(start) && !shown(step));                           // Gray code always steps by one
        QCOMPARE(preview->text(), u"Steps: 0, 1, 3, 2, 6, 7, 5, 4, ..."_s);

        choose(mode, u"Walking one"_s);
        QVERIFY(!shown(start) && !shown(step));
        QCOMPARE(preview->text(), u"Steps: 1, 2, 4, 8, 1, 2, 4, 8, ..."_s);

        choose(mode, u"Random"_s);
        auto* seed = app.editor_<QSpinBox>("sourceSeed");
        QVERIFY(shown(seed));
        const QString firstNumbers = preview->text();
        setSpin(seed, 99);
        QCOMPARE(source->sourceProps().seed, std::uint32_t(99));
        QVERIFY(preview->text() != firstNumbers);

        choose(mode, u"Constant"_s);
        auto* constant = app.editor_<QLineEdit>("sourceConstant");
        QVERIFY(shown(constant) && !shown(every));                       // a constant has no steps
        typeInto(constant, u"0b1010_1010_1"_s);
        QCOMPARE(source->sourceProps().constant, std::uint64_t(0x155));
        QCOMPARE(source->getOutputPorts().size(), 9);                    // widened to fit
        QCOMPARE(preview->text(), u"Outputs 341"_s);

        // Every mode's settings are kept while another mode is shown
        choose(mode, u"Random"_s);
        QCOMPARE(seed->value(), 99);
        choose(mode, u"Constant"_s);
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
        QVERIFY(labelWith(app.panel, u"Steps: 1, 1, 0, 0, 0, 0, 0, 0"_s));

        auto* mode = app.editor_<QComboBox>("sourceMode");
        choose(mode, u"List"_s);
        QVERIFY(repeat->isVisible() && !repeat->isChecked());           // kept when switching modes
        choose(mode, u"Counter"_s);
        QVERIFY(!repeat->isVisible());                                   // a counter wraps at its bits instead
    }

    void waveformPerPin()
    {
        App app;
        SourceProps props = SourceProps::waveform({false, true});
        props.bits = 3;
        auto* source = add<SourceItem>(app, props);
        app.select(source);
        QCOMPARE(app.title(), u"Waveform source"_s);
        auto* waveform = app.editor_<WaveformEditor>("sourceWaveform");
        auto* pin1 = app.editor_<QLineEdit>("sourcePattern1");
        QCOMPARE(app.editor_<QLineEdit>("sourcePattern0")->text(), u"01"_s);
        QCOMPARE(pin1->text(), u"0"_s);                     // a new pin starts as a single 0
        QVERIFY(app.panel->findChild<QLineEdit*>(u"sourcePattern3"_s) == nullptr);

        typeInto(pin1, u"0011 0"_s);
        QCOMPARE(source->sourceProps().waves[1], (QList<bool>{0, 0, 1, 1, 0}));
        QCOMPARE(pin1->text(), u"0011 0"_s);                // spaces the user typed are kept
        QCOMPARE(waveform->values(), (QList<bool>{0, 0, 1, 1, 0}));       // the click editor shows the pin being typed in
        QVERIFY(app.editor_<QToolButton>("sourcePin1")->isChecked());

        QTest::mouseClick(waveform, Qt::LeftButton, {}, QPoint(24 * 4 + 5, 10));   // fifth step
        QCOMPARE(source->sourceProps().waves[1], (QList<bool>{0, 0, 1, 1, 1}));
        QCOMPARE(pin1->text(), u"00111"_s);

        app.editor_<QToolButton>("sourcePin0")->click();                 // the editor switches pins
        QCOMPARE(waveform->values(), (QList<bool>{0, 1}));
        QCOMPARE(sourceValueAt(source->sourceProps(), 4), std::uint64_t(0b010));   // pin 0 low, pin 1 high

        QTest::keyClick(pin1, Qt::Key_2);                                // not a bit
        QVERIFY2(app.message().contains(u"only contain 0 and 1"_s), qPrintable(app.message()));
        QCOMPARE(source->sourceProps().waves[1].size(), 5);

        // Fewer pins removes rows; the patterns come back with the pins
        setSpin(app.editor_<QSpinBox>("sourceBits"), 1);
        QVERIFY(app.panel->findChild<QLineEdit*>(u"sourcePattern1"_s) == nullptr);
        setSpin(app.editor_<QSpinBox>("sourceBits"), 2);
        QCOMPARE(app.editor_<QLineEdit>("sourcePattern1")->text(), u"00111"_s);
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
        QVERIFY2(app.message().contains(u"Output 2 is wired"_s), qPrintable(app.message()));
        QCOMPARE(source->getOutputPorts().size(), 2);
        QCOMPARE(bits->value(), 2);

        choose(app.editor_<QComboBox>("sourceMode"), u"Waveform"_s);   // switching modes keeps the pins
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
        QCOMPARE(app.title(), u"Multiplexer"_s);
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
        QCOMPARE(app.title(), u"List source"_s);

        QTest::mouseClick(app.canvas->viewport(), Qt::LeftButton, {}, app.viewPos(gate));
        QVERIFY(gate->isSelected() && !source->isSelected());
        QCOMPARE(app.title(), u"AND gate"_s);

        QTest::mouseClick(app.canvas->viewport(), Qt::LeftButton, {}, QPoint(30, app.canvas->viewport()->height() - 30));
        QVERIFY(app.scene->selectedItems().isEmpty());                   // a click on empty canvas
        QCOMPARE(app.title(), u"Nothing selected"_s);

        app.select(gate);
        app.tool("muxButton")->click();                                  // picking a tool deselects too
        QVERIFY(app.scene->selectedItems().isEmpty());
        QCOMPARE(app.nextComponent(), u"Multiplexer"_s);
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
        QCOMPARE(app.title(), u"2 items selected"_s);
        QVERIFY(app.nextComponent().isEmpty());

        app.select(wire);
        QCOMPARE(app.title(), u"Wire"_s);
        QVERIFY(labelWith(app.panel, u"no properties"_s));
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
        QCOMPARE(app.title(), u"NAND gate"_s);
        app.pressDelete();
        QVERIFY(itemsOfType<GateItem>(app.scene).isEmpty());
        QCOMPARE(app.title(), u"Nothing selected"_s);
        QCOMPARE(app.nextComponent(), u"NAND gate"_s);

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
        QCOMPARE(values->toPlainText(), u"12, "_s);
        QCOMPARE(source->sourceProps().values, QList<std::uint64_t>{12});
    }

    void clearingTheCanvasWhileSomethingIsSelected()
    {
        App app;
        auto* gate = add<GateItem>(app, GateProps{GType::OR, 3});
        app.select(gate);
        app.clear->click();
        QCoreApplication::processEvents();
        QCOMPARE(app.title(), u"Nothing selected"_s);
        setSpin(app.editor_<QSpinBox>("gateInputs"), 5);                 // must edit the tool, not the deleted gate
        QCOMPARE(std::get<GateProps>(app.scene->toolProps()).numInputs, 5);
    }
};

QTEST_MAIN(TestPanel)
#include "tst_panel.moc"
