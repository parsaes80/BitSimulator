// The Properties panel, driven through the real main window with simulated clicks and key presses.

#include "TestSupport.h"

using namespace TestSupport;

namespace {

// Like the spin box arrows: sets the value and lets the panel react.
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
        QTest::newRow("source") << "srcButton" << "Value source";
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
        auto* source = add<SourceItem>(app, SourceProps{QList<int>{2, 5}, 3}, {-200, 0});
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
        auto* source = add<SourceItem>(app, SourceProps{QList<int>{2, 5}, 3}, {-200, 0});
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

    void valueSourceEditing()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps{QList<int>{1, 2}, 2});
        app.select(source);
        auto* values = app.editor_<QLineEdit>("sourceValues");
        auto* bits = app.editor_<QSpinBox>("sourceBits");
        QCOMPARE(values->text(), QStringLiteral("1, 2"));
        QCOMPARE(bits->value(), 2);

        typeInto(values, QStringLiteral("7, 300"));
        QCOMPARE(std::get<QList<int>>(source->getValues()), (QList<int>{7, 300}));
        QCOMPARE(source->getOutputPorts().size(), 9);                    // 300 needs 9 bits
        QCOMPARE(bits->value(), 9);
        QCOMPARE(bits->minimum(), 9);                                    // cannot go below what the values need
        QCOMPARE(values->text(), QStringLiteral("7, 300"));              // the text being typed is not rewritten

        QTest::keyClicks(values, QStringLiteral(", x"));
        QVERIFY2(app.message().contains(QStringLiteral("not a whole number")), qPrintable(app.message()));
        QCOMPARE(std::get<QList<int>>(source->getValues()), (QList<int>{7, 300}));     // last valid values kept
        for (int i = 0; i < 3; i++) QTest::keyClick(values, Qt::Key_Backspace);
        QVERIFY(app.message().isEmpty());

        typeInto(values, QStringLiteral("5 6,7"));                       // spaces separate values as well as commas
        QCOMPARE(std::get<QList<int>>(source->getValues()), (QList<int>{5, 6, 7}));

        setSpin(bits, 12);                                               // more pins than needed is allowed
        QCOMPARE(source->getOutputPorts().size(), 12);

        typeInto(values, QStringLiteral("-3"));                          // negative numbers are not values
        QVERIFY(!app.message().isEmpty());
        QCOMPARE(std::get<QList<int>>(source->getValues()), (QList<int>{5, 6, 7}));

        typeInto(values, QStringLiteral("70000"));                       // needs more than 16 bits
        QVERIFY2(app.message().contains(QStringLiteral("not supported")), qPrintable(app.message()));
        QCOMPARE(std::get<QList<int>>(source->getValues()), QList<int>{7000});         // what was valid on the way there

        values->selectAll();
        QTest::keyClick(values, Qt::Key_Delete);                         // an empty field
        QVERIFY2(app.message().contains(QStringLiteral("at least one")), qPrintable(app.message()));
        QCOMPARE(std::get<QList<int>>(source->getValues()), QList<int>{7000});
    }

    void waveformEditing()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps{QList<int>{7, 300}, 9});
        app.select(source);

        segment(app.panel, QStringLiteral("Waveform"))->click();
        QVERIFY(std::holds_alternative<QList<bool>>(source->getValues()));
        QCOMPARE(source->getOutputPorts().size(), 1);
        QCOMPARE(app.title(), QStringLiteral("Waveform source"));

        auto* pattern = app.editor_<QLineEdit>("sourcePattern");
        auto* waveform = app.editor_<WaveformEditor>("sourceWaveform");
        typeInto(pattern, QStringLiteral("1100 1010 11"));
        QCOMPARE(std::get<QList<bool>>(source->getValues()), (QList<bool>{1, 1, 0, 0, 1, 0, 1, 0, 1, 1}));
        QCOMPARE(pattern->text(), QStringLiteral("1100 1010 11"));       // spaces the user typed are kept
        QCOMPARE(waveform->values().size(), 10);

        QTest::mouseClick(waveform, Qt::LeftButton, {}, QPoint(24 * 2 + 5, 10));        // third step, first row
        QVERIFY(std::get<QList<bool>>(source->getValues())[2]);
        QCOMPARE(pattern->text(), QStringLiteral("1110101011"));
        QTest::mouseClick(waveform, Qt::LeftButton, {}, QPoint(24 * 1 + 5, 40 + 10));   // tenth step, second row
        QVERIFY(!std::get<QList<bool>>(source->getValues())[9]);
        QTest::mouseClick(waveform, Qt::LeftButton, {}, QPoint(24 * 5 + 5, 40 + 10));   // past the last step: nothing
        QCOMPARE(std::get<QList<bool>>(source->getValues()).size(), 10);

        pattern->setFocus();
        pattern->end(false);
        QTest::keyClick(pattern, Qt::Key_2);                             // not a bit
        QVERIFY2(app.message().contains(QStringLiteral("only contain 0 and 1")), qPrintable(app.message()));
        QCOMPARE(std::get<QList<bool>>(source->getValues()).size(), 10);
        QTest::keyClick(pattern, Qt::Key_Backspace);
        QVERIFY(app.message().isEmpty());

        segment(app.panel, QStringLiteral("Values"))->click();           // the earlier numbers come back
        QCOMPARE(std::get<QList<int>>(source->getValues()), (QList<int>{7, 300}));
        segment(app.panel, QStringLiteral("Waveform"))->click();         // and so does the waveform
        QCOMPARE(std::get<QList<bool>>(source->getValues()).size(), 10);
    }

    void sourceKindChangeIsRefusedWhenPinsAreWired()
    {
        App app;
        auto* source = add<SourceItem>(app, SourceProps{QList<int>{1, 2}, 2}, {-200, 0});
        auto* display = add<DisplayItem>(app, DisplayProps{2, 0});
        QVERIFY(connectPorts(app.scene, source->getOutputPorts()[1], display->getInputPorts()[1]));
        app.select(source);

        segment(app.panel, QStringLiteral("Waveform"))->click();
        QVERIFY(std::holds_alternative<QList<int>>(source->getValues()));
        QVERIFY2(app.message().contains(QStringLiteral("Output 2 is wired")), qPrintable(app.message()));
        QVERIFY(segment(app.panel, QStringLiteral("Values"))->isChecked());
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
        auto* source = add<SourceItem>(app, SourceProps{QList<int>{2, 5}, 3}, {-150, 0});
        auto* gate = add<GateItem>(app, GateProps{}, {150, 0});

        QTest::mouseClick(app.canvas->viewport(), Qt::LeftButton, {}, app.viewPos(source));
        QVERIFY(source->isSelected());                                   // a source used to open the overlay instead
        QCOMPARE(app.title(), QStringLiteral("Value source"));

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
        auto* source = add<SourceItem>(app, SourceProps{QList<bool>{true}, 1}, {-150, 0});
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
        auto* source = add<SourceItem>(app, SourceProps{QList<int>{12, 3}, 4});
        app.select(source);
        auto* values = app.editor_<QLineEdit>("sourceValues");
        values->setFocus();
        values->end(false);
        QTest::keyClick(values, Qt::Key_Backspace);                      // deletes the "3", not the source
        QVERIFY(app.scene->items().contains(source));
        QCOMPARE(values->text(), QStringLiteral("12, "));
        QCOMPARE(std::get<QList<int>>(source->getValues()), QList<int>{12});
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
