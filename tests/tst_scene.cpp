// The canvas alone: components, editing rules, the run state machine, the netlist it builds

#include "TestSupport.h"
#include <QSignalSpy>
#include "Theme.h"

using namespace Qt::StringLiterals;

using namespace TestSupport;

namespace {

// Everything the scene would send to the simulator, recorded instead.
struct SimLink {
    QList<ExportGraph> started;
    QList<bool> running;
    int stopped = 0;

    explicit SimLink(CircuitScene& scene)
    {
        QObject::connect(&scene, &CircuitScene::startSimSIG, &scene, [this](const ExportGraph& graph) { started.append(graph); });
        QObject::connect(&scene, &CircuitScene::setSimRunningSIG, &scene, [this](bool on) { running.append(on); });
        QObject::connect(&scene, &CircuitScene::stopSimSIG, &scene, [this] { stopped++; });
    }
};

template <typename Item, typename Props>
Item* add(CircuitScene& scene, const Props& props, QPointF position = {})
{
    return dynamic_cast<Item*>(scene.addComponent(props, position));
}

u32 netOf(const PortItem* port)
{
    const auto* scene = static_cast<const CircuitScene*>(port->scene());
    return port->getConnections().isEmpty() ? 0 : scene->wireMap().wire2net.at(port->getConnections().first());
}

} // namespace

class TestScene : public QObject {
    Q_OBJECT
private slots:
    void aSavedCircuitOpensExactlyAsItWas()
    {
        CircuitScene scene;
        SourceProps wide = SourceProps::list({(std::uint64_t(1) << 63) + 5, 7}, 64);   // past 2^53: JSON numbers would round it
        wide.repeat = false;
        auto* list = add<SourceItem>(scene, wide, QPointF(80000, 50000));
        SourceProps pattern = SourceProps::waveform({true, false, true});
        pattern.bits = 2;
        auto* waves = add<SourceItem>(scene, normalized(pattern), QPointF(80000, 50300));
        auto* gate = add<GateItem>(scene, GateProps{GType::NAND, 3}, QPointF(80300, 50000));
        auto* reg = add<RegisterItem>(scene, RegisterProps{RType::D, true, true, true, true}, QPointF(80300, 50300));
        auto* mux = add<MuxItem>(scene, MuxProps{MType::MUX, 4}, QPointF(80600, 50000));
        auto* out = add<DisplayItem>(scene, DisplayProps{2, 1}, QPointF(80900, 50000));
        QVERIFY(scene.renameComponent(list, u"data_in"_s));
        QVERIFY(connectPorts(&scene, list->getOutputPorts()[0], gate->getInputPort(0)));
        QVERIFY(connectPorts(&scene, waves->getOutputPorts()[1], gate->getInputPort(2)));
        QVERIFY(connectPorts(&scene, gate->getOutputPort(), reg->getInputPort()));
        QVERIFY(connectPorts(&scene, waves->getOutputPorts()[0], reg->getClkPort()));
        QVERIFY(connectPorts(&scene, reg->getOutputPort(), mux->getInputPorts()[3]));
        QVERIFY(connectPorts(&scene, mux->getOutputPort(), out->getInputPorts()[1]));
        QVERIFY(connectPorts(&scene, gate->getOutputPort(), mux->getAddressPorts()[0]));   // an output feeds two inputs

        const QByteArray json = circuitToJson(scene.circuitData());
        const auto data = circuitFromJson(json);
        QVERIFY2(data, qPrintable(data.error()));
        QCOMPARE(data->components.size(), 6);
        QCOMPARE(data->wires.size(), 7);
        QCOMPARE(std::get<SourceProps>(data->components[0].props), wide);
        QCOMPARE(data->components[0].name, u"data_in"_s);
        QCOMPARE(data->components[1].name, u"Source 2"_s);

        CircuitScene loaded;
        const auto result = loaded.loadCircuit(*data);
        QVERIFY2(result, qPrintable(result.error()));
        QCOMPARE(circuitToJson(loaded.circuitData()), json);                // the same circuit, byte for byte
        QCOMPARE(itemsOfType<WireItem>(&loaded).size(), 7);
        for (WireItem* wire : itemsOfType<WireItem>(&loaded)) QVERIFY(wire->isConnected());
    }

    void everySettingAndPositionIsSaved()
    {
        // Every field away from its default, so one the file forgot would come back different
        SourceProps source;
        source.mode = SourceMode::Random;     // the other modes' settings are kept too
        source.bits = 12;
        source.ticksPerStep = 3;
        source.countDown = true;
        source.start = 0xABCDEF0123456789;
        source.step = 7;
        source.seed = 4242;
        source.values = {1, 2, 3, std::uint64_t(-1)};
        source.waves = {{true, true, false}, {false, true}};
        source.constant = 0x8000000000000001;
        source.repeat = false;
        const QList<ItemProps> all{source, GateProps{GType::XNOR, 5}, RegisterProps{RType::T, false, false, true, true},
                                   MuxProps{MType::DEMUX, 8}, DisplayProps{3, 2}};
        for (const ItemProps& props : all) QVERIFY(props != ItemProps(std::visit([](auto p) -> ItemProps { return decltype(p){}; }, props)));

        CircuitScene scene;
        QList<QPointF> positions;
        for (qsizetype i = 0; i < all.size(); i++) {
            positions << QPointF(80000 + 137.5 * i, 50000 - 61.25 * i);
            QVERIFY(scene.addComponent(all[i], positions.last()));
        }
        const auto data = circuitFromJson(circuitToJson(scene.circuitData()));
        QVERIFY(data);
        CircuitScene loaded;
        QVERIFY(loaded.loadCircuit(*data));
        const CircuitData back = loaded.circuitData();
        QCOMPARE(back.components.size(), all.size());
        for (qsizetype i = 0; i < all.size(); i++) {
            const auto found = std::ranges::find(back.components, positions[i], &SavedComponent::position);
            QVERIFY2(found != back.components.end(), qPrintable(u"component %1 moved"_s.arg(i)));
            QVERIFY2(found->props == std::visit([](auto p) -> ItemProps { return normalized(p); }, all[i]), qPrintable(u"component %1 changed"_s.arg(i)));
        }
    }

    void aDamagedFileChangesNothing()
    {
        CircuitScene scene;
        auto* source = add<SourceItem>(scene, SourceProps::counter(1), QPointF(80000, 50000));
        auto* display = add<DisplayItem>(scene, DisplayProps{1, 0}, QPointF(80300, 50000));
        QVERIFY(connectPorts(&scene, source->getOutputPorts()[0], display->getInputPorts()[0]));
        const QByteArray good = circuitToJson(scene.circuitData());

        QVERIFY(!circuitFromJson("not json"));
        QVERIFY(!circuitFromJson(R"({"app": "Something else", "version": 1})"));
        QVERIFY(circuitFromJson(R"({"app": "BitSimulator", "version": 99})").error().contains(u"newer"_s));
        QVERIFY(circuitFromJson(R"({"app": "BitSimulator", "version": 1, "components": [{"kind": "toaster"}]})").error().contains(u"unknown kind"_s));

        CircuitData data = *circuitFromJson(good);
        CircuitScene other;
        add<GateItem>(other, GateProps{}, QPointF(80000, 50000));
        const auto refuse = [&other](const CircuitData& broken, const QString& why) {
            const auto result = other.loadCircuit(broken);
            QVERIFY(!result);
            QVERIFY2(result.error().contains(why), qPrintable(result.error()));
            QCOMPARE(itemsOfType<GateItem>(&other).size(), 1);                // the canvas it had
        };
        CircuitData missingPin = data;
        missingPin.wires[0].toPin = 5;
        refuse(missingPin, u"Wire 1"_s);
        CircuitData twice = data;
        twice.wires.append(data.wires[0]);                                 // a second wire into the same input
        refuse(twice, u"Wire 2"_s);
        CircuitData noModule = data;
        noModule.components.append({BlockProps{u"u"_s, u"adder"_s, {}}, {}, {}});
        refuse(noModule, u"adder"_s);
    }

    void undoAndRedoWalkThroughEveryEdit()
    {
        CircuitScene scene;
        QList<QByteArray> states{circuitToJson(scene.circuitData())};
        const auto step = [&] { states << circuitToJson(scene.circuitData()); };
        auto* source = add<SourceItem>(scene, SourceProps::counter(2), QPointF(80000, 50000));
        step();
        auto* display = add<DisplayItem>(scene, DisplayProps{2, 0}, QPointF(80300, 50000));
        step();
        QVERIFY(connectPorts(&scene, source->getOutputPorts()[0], display->getInputPorts()[0]));
        step();
        QVERIFY(scene.editComponent(display, DisplayProps{4, 0}));
        step();
        QVERIFY(scene.renameComponent(source, u"clk"_s));
        step();
        scene.clearSelection();
        display->setSelected(true);
        itemsOfType<WireItem>(&scene)[0]->setSelected(true);
        scene.deleteSelection();                                          // the wire and the display, one step
        QVERIFY(itemsOfType<DisplayItem>(&scene).isEmpty());
        step();

        // Back through every step, then forward again
        for (qsizetype i = states.size() - 2; i >= 0; i--) {
            QVERIFY(scene.canUndo());
            scene.undo();
            QCOMPARE(circuitToJson(scene.circuitData()), states[i]);
        }
        QVERIFY(!scene.canUndo());
        for (qsizetype i = 1; i < states.size(); i++) {
            QVERIFY(scene.canRedo());
            scene.redo();
            QCOMPARE(circuitToJson(scene.circuitData()), states[i]);
        }
        QVERIFY(!scene.canRedo());

        // A new change after undoing drops what could be redone
        scene.undo();
        scene.undo();
        add<GateItem>(scene, GateProps{}, QPointF(80000, 50300));
        QVERIFY(!scene.canRedo());
        scene.undo();
        QCOMPARE(circuitToJson(scene.circuitData()), states[states.size() - 3]);
    }

    void typingIntoOneFieldIsOneStep()
    {
        CircuitScene scene;
        auto* source = add<SourceItem>(scene, SourceProps::list({1}, 8), QPointF(80000, 50000));
        const QByteArray before = circuitToJson(scene.circuitData());
        for (const QList<std::uint64_t>& typed : {QList<std::uint64_t>{1}, {12}, {12, 3}, {12, 30}})   // "1", "12", "12, 3", "12, 30"
            QVERIFY(scene.editComponent(source, SourceProps::list(typed, 8)));
        scene.undo();
        QCOMPARE(circuitToJson(scene.circuitData()), before);

        // Another component, or a save in between, starts a new step
        auto* other = add<SourceItem>(scene, SourceProps::list({1}, 8), QPointF(80000, 50300));
        QVERIFY(scene.editComponent(source, SourceProps::list({5}, 8)));
        QVERIFY(scene.editComponent(other, SourceProps::list({6}, 8)));
        scene.markSaved();
        QVERIFY(scene.editComponent(other, SourceProps::list({7}, 8)));
        const auto otherValues = [&scene] {
            for (SourceItem* item : itemsOfType<SourceItem>(&scene))
                if (item->pos() == QPointF(80000, 50300)) return std::get<SourceProps>(item->props()).values;
            return QList<std::uint64_t>{};
        };
        scene.undo();
        QVERIFY(scene.isAtSavedState());
        QCOMPARE(otherValues(), QList<std::uint64_t>{6});
        scene.undo();
        QCOMPARE(otherValues(), QList<std::uint64_t>{1});
        QVERIFY(!scene.isAtSavedState());
    }

    void undoKeepsTheSelectionAndRespectsTheRun()
    {
        CircuitScene scene;
        SimLink link(scene);
        auto* gate = add<GateItem>(scene, GateProps{GType::AND, 2}, QPointF(80000, 50000));
        add<GateItem>(scene, GateProps{GType::OR, 2}, QPointF(80000, 50300));
        QVERIFY(scene.editComponent(gate, GateProps{GType::AND, 4}));
        scene.clearSelection();
        gate->setSelected(true);
        scene.undo();
        QCOMPARE(scene.selectedItems().size(), 1);                      // the same gate, back to 2 inputs
        QCOMPARE(std::get<GateProps>(dynamic_cast<ComponentItem*>(scene.selectedItems()[0])->props()).numInputs, 2);

        scene.startOrResumeSim();                                        // running: the circuit is read-only
        const QByteArray running = circuitToJson(scene.circuitData());
        scene.undo();
        scene.redo();
        QCOMPARE(circuitToJson(scene.circuitData()), running);
        scene.pauseSim();
        scene.redo();                                                    // paused: undo and redo end the run, like an edit
        QCOMPARE(scene.simState(), SimState::Stopped);
        QCOMPARE(std::get<GateProps>(dynamic_cast<ComponentItem*>(scene.selectedItems()[0])->props()).numInputs, 4);
    }

    void theHistoryHasALimit()
    {
        CircuitScene scene;
        for (int i = 0; i < CircuitScene::MaxUndoSteps + 10; i++) add<GateItem>(scene, GateProps{}, QPointF(80000 + i * 100, 50000));
        int undone = 0;
        while (scene.canUndo()) {
            scene.undo();
            undone++;
        }
        QCOMPARE(undone, CircuitScene::MaxUndoSteps);
        QCOMPARE(itemsOfType<GateItem>(&scene).size(), 10);             // the oldest steps were dropped
    }

    void displaysShowNumbersOfAnyWidth()
    {
        const auto show = [](int width, const QList<int>& ones) {
            DisplayItem display(DisplayProps{width, 0});
            for (int bit : ones) display.getInputPorts()[bit]->setValue(true);   // the top pin is bit 0
            display.updateValue();
            return std::pair{display.valueText(), display.getValue()};
        };
        QCOMPARE(show(4, {0, 2}), std::pair(u"5"_s, u64(5)));
        QCOMPARE(show(32, {31}), std::pair(u"2147483648"_s, u64(1) << 31));          // an int made this negative
        QCOMPARE(show(64, {63, 0}), std::pair(u"9223372036854775809"_s, (u64(1) << 63) + 1));
        QCOMPARE(show(100, {99}).first, u"633825300114114700748351602688"_s);         // 2^99
        QCOMPARE(show(100, {99, 3}).second, u64(8));                                  // getValue: the lowest 64 bits
    }

    void onlyNumbersOfFourOrMoreDigitsTurn()
    {
        QFont huge;
        huge.setPointSize(40);                                          // nothing fits across this box
        const QRectF tall(0, 0, 30, 200), wide(0, 0, 200, 30), square(0, 0, 30, 30);
        QVERIFY(!numberRunsAlong(u"999"_s, huge, tall));                 // 3 digits stay across, shrunk instead
        QVERIFY(numberRunsAlong(u"1000"_s, huge, tall));
        QVERIFY(!numberRunsAlong(u"1000"_s, huge, square));              // turning only helps in a tall box
        QVERIFY(!numberRunsAlong(u"1000"_s, QFont(), wide));             // it fits
    }

    void zoomedOutWiresAreDrawnByTheSceneAndPinsHide()
    {
        CircuitCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        CircuitScene* scene = canvas.getScene();
        auto* source = add<SourceItem>(*scene, SourceProps::counter(1), QPointF(80000, 50000));
        auto* display = add<DisplayItem>(*scene, DisplayProps{1, 0}, QPointF(83000, 52000));
        WireItem* wire = connectPorts(scene, source->getOutputPorts()[0], display->getInputPorts()[0]);
        QVERIFY(wire);
        const QLineF vertical = wire->sceneSegments()[1];
        const auto colorOnWire = [&] {
            const QImage image = canvas.viewport()->grab().toImage();
            return image.pixelColor(canvas.mapFromScene(vertical.center()));
        };
        // Zoomed out, a wire is its own color blended with the canvas, however its floor is tuned
        const auto dimmed = [](const QColor& pixel, const QColor& color) {
            const QColor canvas = currentTheme().canvas;
            const int span = color.green() - canvas.green();
            const double alpha = span ? double(pixel.green() - canvas.green()) / span : 1.0;
            const QColor expected = Theme::mix(canvas, color, alpha);
            return alpha > 0.05 && alpha < 1.01 && std::abs(pixel.red() - expected.red()) <= 3
                   && std::abs(pixel.green() - expected.green()) <= 3 && std::abs(pixel.blue() - expected.blue()) <= 3;
        };

        // Zoomed out: drawn by the scene in one batch, pins hidden
        canvas.setTransform(QTransform::fromScale(0.1, 0.1));
        canvas.centerOn(vertical.center());
        QVERIFY(WireItem::drawnByScene(0.1) && !PortItem::drawnAt(0.1));
        QVERIFY(dimmed(colorOnWire(), currentTheme().wire));
        QVERIFY(!source->getOutputPorts()[0]->isVisible());
        sim_values_visible = true;                                       // as while a run is shown
        wire->setValue(true);
        QVERIFY(dimmed(colorOnWire(), currentTheme().wireHigh));
        wire->setValue(false);
        QVERIFY(dimmed(colorOnWire(), currentTheme().wireLow));
        sim_values_visible = false;
        wire->setSelected(true);                                         // selected: its own color, dimmed and thin too
        QVERIFY(dimmed(colorOnWire(), currentTheme().wireSelected));
        const QPoint centre = canvas.mapFromScene(vertical.center());
        const QImage image = canvas.viewport()->grab().toImage();
        QCOMPARE(image.pixelColor(centre + QPoint(1, 0)), image.pixelColor(centre + QPoint(8, 0)));   // 1 px: background beside it
        wire->setSelected(false);

        // A wire can still be drawn from a hidden pin
        auto* gate = add<GateItem>(*scene, GateProps{GType::NOT, 1}, QPointF(80000, 54000));
        canvas.viewport()->grab();                                       // a paint: the new gate's pin hides too
        QVERIFY(!gate->getInputPort(0)->isVisible());
        scene->startWireConnection(source->getOutputPorts()[0]->scenePos());
        scene->finishWireConnection(gate->getInputPort(0)->scenePos());
        QCOMPARE(gate->getInputPort(0)->getConnections().size(), 1);

        // Zoomed in: pins come back, and each wire draws itself again
        canvas.setTransform(QTransform::fromScale(1, 1));
        canvas.centerOn(vertical.center());
        QCOMPARE(colorOnWire(), currentTheme().wire);
        QVERIFY(source->getOutputPorts()[0]->isVisible() && gate->getInputPort(0)->isVisible());
    }

    void searchFindsInputsAndOutputsByName()
    {
        CircuitScene scene;
        auto* clk = add<SourceItem>(scene, SourceProps::counter(1), QPointF(80000, 50000));
        auto* rst = add<SourceItem>(scene, SourceProps::counter(1), QPointF(80000, 50200));
        auto* s10 = add<SourceItem>(scene, SourceProps::counter(1), QPointF(80000, 50400));
        auto* s2 = add<SourceItem>(scene, SourceProps::counter(1), QPointF(80000, 50600));
        auto* out = add<DisplayItem>(scene, DisplayProps{1, 0}, QPointF(80300, 50000));
        add<GateItem>(scene, GateProps{}, QPointF(80300, 50300));       // not an input or output
        QVERIFY(scene.renameComponent(clk, u"CLK_main"_s));
        QVERIFY(scene.renameComponent(rst, u"rst_n"_s));
        QVERIFY(scene.renameComponent(s10, u"data10"_s));
        QVERIFY(scene.renameComponent(s2, u"data2"_s));
        QVERIFY(scene.renameComponent(out, u"data_out"_s));
        const auto find = [&scene](const QString& pattern) {
            QStringList names;
            for (ComponentItem* item : scene.findInputsAndOutputs(pattern)) {
                if (auto* source = dynamic_cast<SourceItem*>(item)) names << source->name();
                else names << dynamic_cast<DisplayItem*>(item)->name();
            }
            return names;
        };

        QCOMPARE(find(u"data"_s), (QStringList{u"data2"_s, u"data10"_s, u"data_out"_s}));   // inputs first, numbers in order
        QCOMPARE(find(u"clk"_s), QStringList{u"CLK_main"_s});                              // any case
        QCOMPARE(find(u"^data\\d+$"_s), (QStringList{u"data2"_s, u"data10"_s}));         // a regex
        QCOMPARE(find(u"rst|clk"_s), (QStringList{u"CLK_main"_s, u"rst_n"_s}));
        QCOMPARE(find(u"data["_s), QStringList{});                                         // invalid: literal text, no match
        QCOMPARE(find(u"nothing"_s), QStringList{});
    }

    void sourcesAndDisplaysGetNames()
    {
        CircuitScene scene;
        auto* s1 = add<SourceItem>(scene, SourceProps::counter(2), QPointF(80000, 50000));
        auto* s2 = add<SourceItem>(scene, SourceProps::counter(2), QPointF(80000, 50200));
        auto* s3 = add<SourceItem>(scene, SourceProps::counter(2), QPointF(80000, 50400));
        auto* d1 = add<DisplayItem>(scene, DisplayProps{2, 0}, QPointF(80300, 50000));
        QCOMPARE(s1->name(), u"Source 1"_s);
        QCOMPARE(s2->name(), u"Source 2"_s);
        QCOMPARE(s3->name(), u"Source 3"_s);
        QCOMPARE(d1->name(), u"Display 1"_s);                            // numbered on their own

        // Numbers are kept: deleting Source 2 renames nobody, and the next source gets the free 2
        scene.clearSelection();
        s2->setSelected(true);
        scene.deleteSelection();
        QCOMPARE(s3->name(), u"Source 3"_s);
        auto* again = add<SourceItem>(scene, SourceProps::counter(2), QPointF(80000, 50600));
        QCOMPARE(again->name(), u"Source 2"_s);

        // Renaming; an empty name gives an automatic one again, with a free number
        QVERIFY(scene.renameComponent(s1, u"  clk  "_s));
        QCOMPARE(s1->name(), u"clk"_s);                                   // trimmed
        QVERIFY(s1->boundingRect().width() > 0);
        QVERIFY(scene.renameComponent(s1, QString()));
        QCOMPARE(s1->name(), u"Source 1"_s);
        QVERIFY(scene.renameComponent(d1, u"result"_s));
        QCOMPARE(d1->name(), u"result"_s);

        auto* gate = add<GateItem>(scene, GateProps{}, QPointF(80600, 50000));
        QVERIFY(!scene.renameComponent(gate, u"g"_s));                    // only sources and displays

        scene.startOrResumeSim();
        QVERIFY(!scene.renameComponent(s1, u"x"_s));                      // not while running
        QCOMPARE(s1->name(), u"Source 1"_s);
        scene.stopSim();
    }

    void repaintsOnlyWhatChanges()
    {
        CircuitCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        CircuitScene* scene = canvas.getScene();
        auto* source = add<SourceItem>(*scene, SourceProps::counter(1), QPointF(79900, 50000));
        auto* display = add<DisplayItem>(*scene, DisplayProps{1, 0}, QPointF(80100, 50000));
        WireItem* wire = connectPorts(scene, source->getOutputPorts()[0], display->getInputPorts()[0]);
        QVERIFY(wire);
        canvas.centerOn(80000, 50000);
        QTest::qWait(200);

        // An idle canvas paints nothing (it used to repaint hundreds of times a second)
        int paints = 0;
        struct PaintCounter : QObject {
            int* count = nullptr;
            bool eventFilter(QObject*, QEvent* event) override { if (event->type() == QEvent::Paint) ++*count; return false; }
        } counter;
        counter.count = &paints;
        canvas.viewport()->installEventFilter(&counter);
        QTest::qWait(500);
        QCOMPARE(paints, 0);

        // A wire that changes value repaints itself, and only itself
        QSignalSpy changed(scene, &QGraphicsScene::changed);
        wire->setValue(true);
        QTRY_VERIFY(!changed.isEmpty());
        QRectF dirty;
        for (const QRectF& rect : changed.first().first().value<QList<QRectF>>()) dirty |= rect;
        QVERIFY(dirty.intersects(wire->sceneBoundingRect()));
        QVERIFY(dirty.width() < 1000 && dirty.height() < 1000);
    }
    //===================== Components and their pins ========================

    void componentsStartWithTheirProperties()
    {
        CircuitScene scene;
        auto* gate = add<GateItem>(scene, GateProps{GType::NOR, 5});
        QVERIFY(gate);
        QCOMPARE(gate->getInputPorts().size(), 5);
        QCOMPARE(gate->props(), ItemProps(GateProps{GType::NOR, 5}));

        auto* notGate = add<GateItem>(scene, GateProps{GType::NOT, 4});
        QCOMPARE(notGate->getInputPorts().size(), 1);                    // a NOT gate has one input whatever was asked

        auto* flipFlop = add<RegisterItem>(scene, RegisterProps{RType::JK, true, false});
        QVERIFY(flipFlop->getClkPort() && !flipFlop->getReadEnablePort());
        auto* latch = add<RegisterItem>(scene, RegisterProps{RType::D, false, true});
        QVERIFY(!latch->getClkPort() && latch->getReadEnablePort());

        SourceProps fivePins = SourceProps::waveform({true, false});
        fivePins.bits = 5;
        auto* wave = add<SourceItem>(scene, fivePins);
        QCOMPARE(wave->getOutputPorts().size(), 5);                      // a waveform per pin
        auto* counter = add<SourceItem>(scene, SourceProps::counter(32));
        QCOMPARE(counter->getOutputPorts().size(), 32);
        auto* numbers = add<SourceItem>(scene, SourceProps::list({9}, 1));
        QCOMPARE(numbers->getOutputPorts().size(), 4);                   // 9 needs 4 bits

        auto* display = add<DisplayItem>(scene, DisplayProps{3, 1});
        QCOMPARE(display->getInputPorts().size(), 3);
        QCOMPARE(display->getOutputPorts().size(), 1);
    }

    void multiplexerSelectLines_data()
    {
        QTest::addColumn<int>("inputs");
        QTest::addColumn<int>("lines");
        const int expected[] = {1, 2, 2, 3, 3, 3, 3};
        for (int inputs = 2; inputs <= 8; inputs++) QTest::addRow("%d inputs", inputs) << inputs << expected[inputs - 2];
    }
    void multiplexerSelectLines()
    {
        QFETCH(int, inputs);
        QFETCH(int, lines);
        CircuitScene scene;
        auto* mux = add<MuxItem>(scene, MuxProps{MType::MUX, inputs});
        QCOMPARE(mux->getInputPorts().size(), inputs);
        QCOMPARE(mux->getAddressPorts().size(), lines);
    }

    void gateCanGrowAndShrink()
    {
        CircuitScene scene;
        auto* gate = add<GateItem>(scene, GateProps{GType::AND, 2});
        const qreal smallHeight = gate->boundingRect().height();

        QVERIFY(scene.editComponent(gate, GateProps{GType::AND, 6}).has_value());
        QCOMPARE(gate->getInputPorts().size(), 6);
        QVERIFY(gate->boundingRect().height() > smallHeight);            // the body grows with the pins

        QVERIFY(scene.editComponent(gate, GateProps{GType::AND, 3}).has_value());
        QCOMPARE(gate->getInputPorts().size(), 3);

        // The pins are spread evenly and stay inside the body
        const QList<PortItem*> pins = gate->getInputPorts();
        for (int i = 1; i < pins.size(); i++) QVERIFY(pins[i]->pos().y() > pins[i - 1]->pos().y());
        QVERIFY(gate->boundingRect().contains(pins.first()->pos()) && gate->boundingRect().contains(pins.last()->pos()));
    }

    void aWiredPinCannotBeRemoved()
    {
        CircuitScene scene;
        auto* source = add<SourceItem>(scene, SourceProps::waveform({false, true}), {-200, 0});
        auto* gate = add<GateItem>(scene, GateProps{GType::AND, 4});
        WireItem* wire = connectPorts(&scene, source->getOutputPorts()[0], gate->getInputPort(3));
        QVERIFY(wire);

        const auto refused = scene.editComponent(gate, GateProps{GType::AND, 2});
        QVERIFY(!refused.has_value());
        QVERIFY2(refused.error().contains(u"Input 4"_s), qPrintable(refused.error()));
        QCOMPARE(gate->getInputPorts().size(), 4);                       // nothing changed
        QCOMPARE(gate->props(), ItemProps(GateProps{GType::AND, 4}));

        QVERIFY(!scene.editComponent(gate, GateProps{GType::NOT, 1}).has_value());      // NOT would drop inputs 2-4
        QCOMPARE(gate->getGateType(), GType::AND);

        // Shrinking to exactly the wired pin is fine, and so is growing
        QVERIFY(scene.editComponent(gate, GateProps{GType::AND, 4}).has_value());
        QVERIFY(scene.editComponent(gate, GateProps{GType::OR, 7}).has_value());
        QCOMPARE(gate->getInputPort(3)->getConnections(), QList<WireItem*>{wire});
        QCOMPARE(wire->getEndPort(), gate->getInputPort(3));
    }

    void wiresFollowPinsThatMove()
    {
        CircuitScene scene;
        auto* source = add<SourceItem>(scene, SourceProps::waveform({false, true}), {-200, 0});
        auto* gate = add<GateItem>(scene, GateProps{GType::AND, 2});
        WireItem* wire = connectPorts(&scene, source->getOutputPorts()[0], gate->getInputPort(1));
        QVERIFY(wire);

        auto wireReaches = [&](const PortItem* pin) {
            return wire->mapRectToScene(wire->boundingRect()).adjusted(-1, -1, 1, 1).contains(pin->mapToScene(QPointF(0, 0)));
        };
        const QPointF before = gate->getInputPort(1)->mapToScene(QPointF(0, 0));
        QVERIFY(scene.editComponent(gate, GateProps{GType::AND, 8}).has_value());
        QVERIFY(gate->getInputPort(1)->mapToScene(QPointF(0, 0)) != before);    // adding pins moved this one
        QVERIFY(wireReaches(gate->getInputPort(1)) && wireReaches(source->getOutputPorts()[0]));

        gate->setPos(120, 90);                                                 // dragging the component
        QVERIFY(wireReaches(gate->getInputPort(1)));
    }

    void gateTypeChangesInPlace()
    {
        CircuitScene scene;
        auto* gate = add<GateItem>(scene, GateProps{GType::AND, 3});
        PortItem* output = gate->getOutputPort();
        const qreal plain = output->pos().x();

        QVERIFY(scene.editComponent(gate, GateProps{GType::NAND, 3}).has_value());
        QCOMPARE(gate->getOutputPort(), output);                         // same pin object, so its wires survive
        QVERIFY(output->pos().x() > plain);                              // moved out to make room for the bubble

        QVERIFY(scene.editComponent(gate, GateProps{GType::NOT, 3}).has_value());
        QCOMPARE(gate->getInputPorts().size(), 1);
        QVERIFY(scene.editComponent(gate, GateProps{GType::XOR, 1}).has_value());
        QCOMPARE(gate->getInputPorts().size(), 2);                       // back to at least two inputs
    }

    void registerPinsComeAndGo()
    {
        CircuitScene scene;
        auto* source = add<SourceItem>(scene, SourceProps::list({3}, 2), {-200, 0});
        auto* reg = add<RegisterItem>(scene, RegisterProps{RType::D, true, true});
        QVERIFY(connectPorts(&scene, source->getOutputPorts()[0], reg->getClkPort()));
        QVERIFY(connectPorts(&scene, source->getOutputPorts()[1], reg->getReadEnablePort()));

        auto refused = scene.editComponent(reg, RegisterProps{RType::D, false, true});
        QVERIFY(!refused.has_value() && refused.error().contains(u"clock"_s));
        refused = scene.editComponent(reg, RegisterProps{RType::D, true, false});
        QVERIFY(!refused.has_value() && refused.error().contains(u"enable"_s));
        QVERIFY(reg->getClkPort() && reg->getReadEnablePort());

        // The type can always change: it does not touch any pin
        PortItem* in1 = reg->getInputPort();
        QVERIFY(scene.editComponent(reg, RegisterProps{RType::SR, true, true}).has_value());
        QCOMPARE(reg->getRegType(), RType::SR);
        QCOMPARE(reg->getInputPort(), in1);

        auto* free = add<RegisterItem>(scene, RegisterProps{RType::T, true, true}, {200, 0});
        QVERIFY(scene.editComponent(free, RegisterProps{RType::T, false, false}).has_value());
        QVERIFY(!free->getClkPort() && !free->getReadEnablePort());
        QCOMPARE(free->ports().size(), 3);
        QVERIFY(scene.editComponent(free, RegisterProps{RType::T, true, true}).has_value());
        QCOMPARE(free->ports().size(), 5);
    }

    void sourceKindAndWidth()
    {
        CircuitScene scene;
        auto* source = add<SourceItem>(scene, SourceProps::list({1, 2}, 2), {-200, 0});
        auto* display = add<DisplayItem>(scene, DisplayProps{4, 0});
        QVERIFY(connectPorts(&scene, source->getOutputPorts()[1], display->getInputPorts()[1]));

        QVERIFY(scene.editComponent(source, SourceProps::list({1, 2, 12}, 2)).has_value());
        QCOMPARE(source->getOutputPorts().size(), 4);                    // widened to fit 12

        // Becoming a waveform would leave one pin, but the second pin is wired
        const auto refused = scene.editComponent(source, SourceProps::waveform({true}));
        QVERIFY(!refused.has_value() && refused.error().contains(u"Output 2"_s));
        QCOMPARE(source->sourceProps().mode, SourceMode::List);

        QVERIFY(scene.editComponent(source, SourceProps::list({0}, 2)).has_value());   // down to the wired pin is fine
        QCOMPARE(source->getOutputPorts().size(), 2);
    }

    void displayAndMuxPins()
    {
        CircuitScene scene;
        auto* display = add<DisplayItem>(scene, DisplayProps{2, 2});
        QVERIFY(scene.editComponent(display, DisplayProps{6, 0}).has_value());
        QCOMPARE(display->getInputPorts().size(), 6);
        QVERIFY(display->getOutputPorts().isEmpty());

        auto* source = add<SourceItem>(scene, SourceProps::waveform({true}), {-200, 0});
        auto* mux = add<MuxItem>(scene, MuxProps{MType::MUX, 8});
        QVERIFY(connectPorts(&scene, source->getOutputPorts()[0], mux->getAddressPorts()[2]));
        const auto refused = scene.editComponent(mux, MuxProps{MType::MUX, 4});      // 4 inputs need only 2 select lines
        QVERIFY(!refused.has_value() && refused.error().contains(u"Select line 3"_s));
        QVERIFY(scene.editComponent(mux, MuxProps{MType::MUX, 5}).has_value());      // 5 still needs 3
        QCOMPARE(mux->getInputPorts().size(), 5);
    }

    void propertiesOfTheWrongKindAreRefused()
    {
        CircuitScene scene;
        auto* gate = add<GateItem>(scene, GateProps{});
        QVERIFY(!scene.editComponent(gate, RegisterProps{}).has_value());
        QVERIFY(!scene.editComponent(gate, DisplayProps{}).has_value());
        QCOMPARE(gate->props(), ItemProps(GateProps{}));
    }

    //===================== Wiring and deleting ========================

    void wiringRules()
    {
        CircuitScene scene;
        auto* a = add<SourceItem>(scene, SourceProps::waveform({true}), {-200, -60});
        auto* b = add<SourceItem>(scene, SourceProps::waveform({true}), {-200, 60});
        auto* gate = add<GateItem>(scene, GateProps{GType::AND, 2});
        auto* other = add<GateItem>(scene, GateProps{GType::OR, 2}, {0, 150});

        QVERIFY(connectPorts(&scene, a->getOutputPorts()[0], gate->getInputPort(0)));
        QVERIFY(!connectPorts(&scene, b->getOutputPorts()[0], gate->getInputPort(0)));             // an input takes one wire
        QCOMPARE(gate->getInputPort(0)->getConnections().size(), 1);
        QVERIFY(!connectPorts(&scene, gate->getInputPort(1), other->getInputPort(0)));             // input to input
        QVERIFY(!connectPorts(&scene, a->getOutputPorts()[0], b->getOutputPorts()[0]));            // output to output
        QVERIFY(connectPorts(&scene, a->getOutputPorts()[0], other->getInputPort(0)));             // an output can feed many
        QCOMPARE(a->getOutputPorts()[0]->getConnections().size(), 2);

        // A drag that ends in empty space leaves nothing behind
        const int before = scene.items().size();
        scene.startWireConnection(a->getOutputPorts()[0]->mapToScene(QPointF(0, 0)));
        scene.updateWireConnection(QPointF(400, 400));
        scene.finishWireConnection(QPointF(400, 400));
        QCOMPARE(scene.items().size(), before);
    }

    void deletingFollowsTheRules()
    {
        CircuitScene scene;
        auto* source = add<SourceItem>(scene, SourceProps::waveform({true}), {-200, 0});
        auto* gate = add<GateItem>(scene, GateProps{GType::NOT, 1});
        auto* loose = add<GateItem>(scene, GateProps{}, {0, 150});
        WireItem* wire = connectPorts(&scene, source->getOutputPorts()[0], gate->getInputPort(0));

        gate->setSelected(true);
        scene.deleteSelection();
        QVERIFY(scene.items().contains(gate));                           // still wired, so it stays

        scene.clearSelection(); wire->setSelected(true);
        scene.deleteSelection();
        QVERIFY(!scene.items().contains(wire));
        QVERIFY(gate->getInputPort(0)->getConnections().isEmpty() && source->getOutputPorts()[0]->getConnections().isEmpty());

        gate->setSelected(true); loose->setSelected(true);
        scene.deleteSelection();
        QVERIFY(!scene.items().contains(gate) && !scene.items().contains(loose));

        // A component selected together with its wires goes in one step
        auto* gate2 = add<GateItem>(scene, GateProps{GType::NOT, 1});
        WireItem* wire2 = connectPorts(&scene, source->getOutputPorts()[0], gate2->getInputPort(0));
        scene.clearSelection(); gate2->setSelected(true); wire2->setSelected(true);
        scene.deleteSelection();
        QVERIFY(!scene.items().contains(gate2) && !scene.items().contains(wire2));
        QVERIFY(scene.items().contains(source));

        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    //===================== Tools ========================

    void toolsRememberTheirOwnDefaults()
    {
        CircuitScene scene;
        QSignalSpy changed(&scene, &CircuitScene::toolChanged);
        QCOMPARE(scene.toolProps(), ItemProps(GateProps{GType::AND, 2}));

        scene.setToolProps(GateProps{GType::AND, 5});
        scene.selectGateTool(GType::XNOR);
        QCOMPARE(scene.toolProps(), ItemProps(GateProps{GType::XNOR, 5}));          // the type changed, the inputs stayed

        scene.selectTool(itemKind<RegisterProps>);
        scene.setToolProps(RegisterProps{RType::T, false, false});
        scene.selectTool(itemKind<DisplayProps>);
        QCOMPARE(scene.toolProps(), ItemProps(DisplayProps{}));
        scene.selectTool(itemKind<RegisterProps>);
        QCOMPARE(scene.toolProps(), ItemProps(RegisterProps{RType::T, false, false}));
        scene.selectGateTool(GType::NOT);
        QCOMPARE(scene.toolProps(), ItemProps(GateProps{GType::NOT, 1}));
        scene.selectGateTool(GType::AND);
        QCOMPARE(std::get<GateProps>(scene.toolProps()).numInputs, 2);

        QCOMPARE(changed.count(), 8);
        scene.selectTool(99);                                            // nonsense is ignored
        QCOMPARE(changed.count(), 8);
    }

    void pickingAToolDeselects()
    {
        CircuitScene scene;
        auto* gate = add<GateItem>(scene, GateProps{});
        gate->setSelected(true);
        scene.selectTool(itemKind<MuxProps>);
        QVERIFY(scene.selectedItems().isEmpty());
    }

    //===================== Simulation state ========================

    void startPauseContinueStop()
    {
        CircuitScene scene;
        SimLink link(scene);
        QSignalSpy states(&scene, &CircuitScene::simStateChanged);
        QCOMPARE(scene.simState(), SimState::Stopped);
        QVERIFY(!scene.isLocked());

        scene.pauseSim();                                                // nothing to pause
        scene.stopSim();                                                 // nothing to stop
        QCOMPARE(states.count(), 0);

        scene.startOrResumeSim();
        QCOMPARE(scene.simState(), SimState::Running);
        QVERIFY(scene.isLocked());
        QCOMPARE(link.started.size(), 1);
        scene.startOrResumeSim();                                        // already running
        QCOMPARE(link.started.size(), 1);

        scene.pauseSim();
        QCOMPARE(scene.simState(), SimState::Paused);
        QVERIFY(!scene.isLocked());
        QCOMPARE(link.running, QList<bool>{false});

        scene.startOrResumeSim();                                        // continue: no new circuit is sent
        QCOMPARE(scene.simState(), SimState::Running);
        QCOMPARE(link.started.size(), 1);
        QCOMPARE(link.running, (QList<bool>{false, true}));

        scene.stopSim();
        QCOMPARE(scene.simState(), SimState::Stopped);
        QCOMPARE(link.stopped, 1);

        scene.startOrResumeSim();                                        // a fresh run gets a new id
        QCOMPARE(link.started.size(), 2);
        QVERIFY(link.started[1].runId > link.started[0].runId);
        QCOMPARE(states.count(), 5);
    }

    void theCircuitIsReadOnlyWhileRunning()
    {
        CircuitScene scene;
        auto* source = add<SourceItem>(scene, SourceProps::waveform({true}), {-200, 0});
        auto* gate = add<GateItem>(scene, GateProps{GType::AND, 2});
        auto* loose = add<GateItem>(scene, GateProps{}, {0, 150});
        WireItem* wire = connectPorts(&scene, source->getOutputPorts()[0], gate->getInputPort(0));
        const int before = scene.items().size();

        scene.startOrResumeSim();
        QVERIFY(scene.addComponent(GateProps{}, {300, 0}) == nullptr);
        QVERIFY(!scene.editComponent(gate, GateProps{GType::OR, 2}).has_value());
        QVERIFY(!connectPorts(&scene, source->getOutputPorts()[0], gate->getInputPort(1)));
        loose->setSelected(true); wire->setSelected(true);
        scene.deleteSelection();
        QCOMPARE(scene.items().size(), before);
        QCOMPARE(gate->getGateType(), GType::AND);
        QVERIFY(wire->isConnected());

        scene.pauseSim();                                                // paused: editing works again
        QVERIFY(scene.editComponent(gate, GateProps{GType::OR, 2}).has_value());
    }

    void anEditEndsAPausedRun_data()
    {
        QTest::addColumn<QString>("edit");
        QTest::addColumn<bool>("endsRun");
        QTest::newRow("placing a component") << "place" << true;
        QTest::newRow("drawing a wire") << "wire" << true;
        QTest::newRow("deleting a wire") << "delete" << true;
        QTest::newRow("changing a property") << "edit" << true;
        QTest::newRow("an edit that is refused") << "refused" << false;
        QTest::newRow("moving a component") << "move" << false;
        QTest::newRow("selecting") << "select" << false;
        QTest::newRow("changing the tool") << "tool" << false;
    }
    void anEditEndsAPausedRun()
    {
        QFETCH(QString, edit);
        QFETCH(bool, endsRun);

        CircuitScene scene;
        SimLink link(scene);
        auto* source = add<SourceItem>(scene, SourceProps::list({3}, 2), {-200, 0});
        auto* gate = add<GateItem>(scene, GateProps{GType::AND, 2});
        WireItem* wire = connectPorts(&scene, source->getOutputPorts()[0], gate->getInputPort(1));
        scene.startOrResumeSim();
        scene.pauseSim();

        if (edit == "place") scene.addComponent(GateProps{}, {300, 0});
        else if (edit == "wire") QVERIFY(connectPorts(&scene, source->getOutputPorts()[1], gate->getInputPort(0)));
        else if (edit == "delete") { wire->setSelected(true); scene.deleteSelection(); }
        else if (edit == "edit") QVERIFY(scene.editComponent(gate, GateProps{GType::NOR, 2}).has_value());
        else if (edit == "refused") QVERIFY(!scene.editComponent(gate, GateProps{GType::NOT, 1}).has_value());
        else if (edit == "move") gate->setPos(50, 50);
        else if (edit == "select") gate->setSelected(true);
        else if (edit == "tool") scene.selectGateTool(GType::XOR);

        QCOMPARE(scene.simState(), endsRun ? SimState::Stopped : SimState::Paused);
        QCOMPARE(link.stopped, endsRun ? 1 : 0);
        QCOMPARE(scene.wireMap().net2wire.empty(), endsRun);                         // the wire map belongs to the run
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void resetEmptiesEverything()
    {
        CircuitScene scene;
        SimLink link(scene);
        auto* source = add<SourceItem>(scene, SourceProps::waveform({true}), {-200, 0});
        auto* gate = add<GateItem>(scene, GateProps{GType::NOT, 1});
        connectPorts(&scene, source->getOutputPorts()[0], gate->getInputPort(0));
        scene.startOrResumeSim();

        scene.startWireConnection(QPointF(500, 500));                    // refused while running, leaves nothing half-drawn
        scene.resetCircuit();
        QCOMPARE(scene.simState(), SimState::Stopped);
        QCOMPARE(link.stopped, 1);
        QVERIFY(scene.items().isEmpty());
        QVERIFY(scene.wireMap().net2wire.empty() && scene.wireMap().wire2net.empty());

        scene.resetCircuit();                                            // twice is fine
        QCOMPARE(link.stopped, 1);
    }

    //===================== From drawing to netlist ========================

    void netlistOfASmallCircuit()
    {
        CircuitScene scene;
        SimLink link(scene);
        auto* a = add<SourceItem>(scene, SourceProps::waveform({false, true}), {-200, -60});
        auto* b = add<SourceItem>(scene, SourceProps::waveform({false, false, true}), {-200, 60});
        auto* gate = add<GateItem>(scene, GateProps{GType::NAND, 3});
        auto* display = add<DisplayItem>(scene, DisplayProps{1, 0}, {200, 0});
        QVERIFY(connectPorts(&scene, a->getOutputPorts()[0], gate->getInputPort(0)));
        QVERIFY(connectPorts(&scene, b->getOutputPorts()[0], gate->getInputPort(2)));
        QVERIFY(connectPorts(&scene, gate->getOutputPort(), display->getInputPorts()[0]));

        scene.startOrResumeSim();
        QCOMPARE(link.started.size(), 1);
        const ExportGraph& graph = link.started.first();

        QCOMPARE(graph.gates.size(), std::size_t(1));
        QCOMPARE(graph.sources.size(), std::size_t(2));
        QVERIFY(graph.registers.empty() && graph.muxes.empty());
        QCOMPARE(graph.numNets, u32(3));

        const Gate& g = graph.gates.front();
        QCOMPARE(g.gateType, GType::NAND);
        QCOMPARE(g.inIDs.size(), std::size_t(3));
        QCOMPARE(g.inIDs[1], u32(0));                                    // the middle input is not wired
        const QSet<u32> nets{g.inIDs[0], g.inIDs[2], g.outID};
        QCOMPARE(nets, (QSet<u32>{1, 2, 3}));                            // three different nets, numbered from 1

        // Each pin in the netlist is the net of the wire drawn on it
        QCOMPARE(g.inIDs[0], netOf(gate->getInputPort(0)));
        QCOMPARE(g.inIDs[2], netOf(gate->getInputPort(2)));
        QCOMPARE(g.outID, netOf(gate->getOutputPort()));
        const u32 netA = netOf(a->getOutputPorts()[0]), netB = netOf(b->getOutputPorts()[0]);
        for (const Source& source : graph.sources) {
            QCOMPARE(source.outIDs.size(), std::size_t(1));
            QVERIFY(source.outIDs[0] == netA || source.outIDs[0] == netB);
            QCOMPARE(source.props.waves[0].size(), source.outIDs[0] == netA ? 2 : 3);
        }
    }

    void oneOutputToSeveralInputsIsOneNet()
    {
        CircuitScene scene;
        SimLink link(scene);
        auto* source = add<SourceItem>(scene, SourceProps::waveform({true}), {-200, 0});
        auto* g1 = add<GateItem>(scene, GateProps{GType::NOT, 1}, {0, -80});
        auto* g2 = add<GateItem>(scene, GateProps{GType::AND, 2}, {0, 80});
        connectPorts(&scene, source->getOutputPorts()[0], g1->getInputPort(0));
        connectPorts(&scene, source->getOutputPorts()[0], g2->getInputPort(0));
        connectPorts(&scene, source->getOutputPorts()[0], g2->getInputPort(1));

        scene.startOrResumeSim();
        const ExportGraph& graph = link.started.first();
        QCOMPARE(graph.numNets, u32(1));
        QCOMPARE(graph.sources.front().outIDs, std::vector<u32>{1});
        for (const Gate& gate : graph.gates) {
            for (const u32 net : gate.inIDs) QCOMPARE(net, u32(1));
            QCOMPARE(gate.outID, u32(0));                                // outputs not wired
        }
        QCOMPARE(scene.wireMap().net2wire.at(1).size(), std::size_t(3));             // all three wires colour together
    }

    void aSourceDrivesOneNetPerPin()
    {
        CircuitScene scene;
        SimLink link(scene);
        auto* source = add<SourceItem>(scene, SourceProps::list({5, 2, 7}, 3), {-200, 0});
        auto* display = add<DisplayItem>(scene, DisplayProps{3, 0});
        for (int bit = 0; bit < 3; bit++) connectPorts(&scene, source->getOutputPorts()[bit], display->getInputPorts()[bit]);

        scene.startOrResumeSim();
        const ExportGraph& graph = link.started.first();
        QCOMPARE(graph.sources.size(), std::size_t(1));
        const Source& s = graph.sources.front();
        QCOMPARE(s.outIDs.size(), std::size_t(3));
        for (int bit = 0; bit < 3; bit++) QCOMPARE(s.outIDs[bit], netOf(source->getOutputPorts()[bit]));
        QCOMPARE(s.props, source->sourceProps());                        // the simulator computes the values from these
        QCOMPARE(sourceValueAt(s.props, 1), u64(2));
    }

    void registerAndMuxPinsLandOnTheRightNets()
    {
        CircuitScene scene;
        SimLink link(scene);
        auto* source = add<SourceItem>(scene, SourceProps::list({0}, 7), {-300, 0});
        auto* reg = add<RegisterItem>(scene, RegisterProps{RType::JK, true, true}, {0, -100});
        auto* mux = add<MuxItem>(scene, MuxProps{MType::MUX, 3}, {0, 100});
        const QList<PortItem*> out = source->getOutputPorts();
        connectPorts(&scene, out[0], reg->getInputPort());
        connectPorts(&scene, out[1], reg->getInputPortTwo());
        connectPorts(&scene, out[2], reg->getClkPort());
        connectPorts(&scene, out[3], reg->getReadEnablePort());
        connectPorts(&scene, out[4], mux->getInputPorts()[0]);
        connectPorts(&scene, out[5], mux->getInputPorts()[2]);
        connectPorts(&scene, out[6], mux->getAddressPorts()[1]);
        connectPorts(&scene, reg->getOutputPort(), mux->getInputPorts()[1]);

        scene.startOrResumeSim();
        const ExportGraph& graph = link.started.first();
        QCOMPARE(graph.registers.size(), std::size_t(1));
        QCOMPARE(graph.muxes.size(), std::size_t(1));
        QCOMPARE(graph.numNets, u32(8));                                 // 7 source bits + the register output

        const Register& r = graph.registers.front();
        QCOMPARE(r.regType, RType::JK);
        QCOMPARE(r.inID, netOf(out[0]));
        QCOMPARE(r.InID2, netOf(out[1]));
        QCOMPARE(r.clkID, netOf(out[2]));
        QCOMPARE(r.enableID, netOf(out[3]));
        QCOMPARE(r.outID, netOf(reg->getOutputPort()));

        const Mux& m = graph.muxes.front();
        QCOMPARE(m.inData, (std::vector<u32>{netOf(out[4]), r.outID, netOf(out[5])}));
        QCOMPARE(m.inAddress, (std::vector<u32>{0, netOf(out[6])}));     // first select line not wired
        QCOMPARE(m.outID, u32(0));
    }

    void aLatchHasNoClockNet()
    {
        CircuitScene scene;
        SimLink link(scene);
        add<RegisterItem>(scene, RegisterProps{RType::D, false, false});
        scene.startOrResumeSim();
        const Register& r = link.started.first().registers.front();
        QCOMPARE(r.clkID, u32(0));                                       // so the simulator treats it as a latch
        QCOMPARE(r.enableID, u32(0));
    }

    void displayOutputsPassTheirInputThrough()
    {
        CircuitScene scene;
        SimLink link(scene);
        auto* source = add<SourceItem>(scene, SourceProps::waveform({true}), {-200, 0});
        auto* display = add<DisplayItem>(scene, DisplayProps{2, 2});
        auto* gate = add<GateItem>(scene, GateProps{GType::NOT, 1}, {200, 0});
        auto* gate2 = add<GateItem>(scene, GateProps{GType::NOT, 1}, {200, 80});
        connectPorts(&scene, source->getOutputPorts()[0], display->getInputPorts()[0]);
        connectPorts(&scene, display->getOutputPorts()[0], gate->getInputPort(0));
        connectPorts(&scene, display->getOutputPorts()[1], gate2->getInputPort(0));     // its input is not wired

        scene.startOrResumeSim();
        const ExportGraph& graph = link.started.first();
        QCOMPARE(graph.numNets, u32(1));
        QCOMPARE(netOf(gate->getInputPort(0)), netOf(display->getInputPorts()[0]));     // same net on both sides
        QCOMPARE(netOf(gate2->getInputPort(0)), u32(0));
    }

    void aChainOfDisplaysIsOneNet()
    {
        // source -> display A -> display B -> NOT, wired in the order that once split B's net
        CircuitScene scene;
        SimLink link(scene);
        auto* source = add<SourceItem>(scene, SourceProps::waveform({true}), {-300, 0});
        auto* a = add<DisplayItem>(scene, DisplayProps{1, 1}, {-100, 0});
        auto* b = add<DisplayItem>(scene, DisplayProps{1, 1}, {100, 0});
        auto* gate = add<GateItem>(scene, GateProps{GType::NOT, 1}, {300, 0});
        connectPorts(&scene, b->getOutputPorts()[0], gate->getInputPort(0));
        connectPorts(&scene, a->getOutputPorts()[0], b->getInputPorts()[0]);
        connectPorts(&scene, source->getOutputPorts()[0], a->getInputPorts()[0]);

        scene.startOrResumeSim();
        QCOMPARE(link.started.first().numNets, u32(1));
        QCOMPARE(netOf(gate->getInputPort(0)), netOf(a->getInputPorts()[0]));
        QCOMPARE(netOf(b->getInputPorts()[0]), netOf(a->getInputPorts()[0]));
    }

    void aHalfDrawnWireIsLeftOutAndRemoved()
    {
        CircuitScene scene;
        SimLink link(scene);
        auto* source = add<SourceItem>(scene, SourceProps::waveform({true}), {-200, 0});
        scene.startWireConnection(source->getOutputPorts()[0]->mapToScene(QPointF(0, 0)));
        scene.updateWireConnection(QPointF(50, 50));
        QCOMPARE(itemsOfType<WireItem>(&scene).size(), 1);

        scene.startOrResumeSim();                                        // used to dereference the wire's missing end
        // The wire carries nothing; the one net is the source pin's own, which the waveforms record
        QCOMPARE(link.started.first().numNets, u32(1));
        QVERIFY(scene.wireMap().wire2net.empty());
        QVERIFY(itemsOfType<WireItem>(&scene).isEmpty());
    }

    //===================== Results coming back ========================

    void resultsAreShownOnTheCanvas()
    {
        CircuitScene scene;
        SimLink link(scene);
        auto* source = add<SourceItem>(scene, SourceProps::list({1, 2, 3}, 2), {-200, 0});
        auto* reg = add<RegisterItem>(scene, RegisterProps{RType::D, true, false}, {0, -100});
        auto* display = add<DisplayItem>(scene, DisplayProps{2, 0}, {200, 0});
        WireItem* w0 = connectPorts(&scene, source->getOutputPorts()[0], display->getInputPorts()[0]);
        WireItem* w1 = connectPorts(&scene, source->getOutputPorts()[1], display->getInputPorts()[1]);
        scene.startOrResumeSim();
        const ExportGraph& graph = link.started.first();

        SimResult result;
        result.runId = graph.runId;
        result.netValues.assign(graph.numNets + 1, 0);
        result.netValues[scene.wireMap().wire2net.at(w1)] = 1;                       // bit 1 high: the display reads 2
        result.sourceTick = 1;
        result.registerValues = {true};
        scene.receiveResult(result);

        QVERIFY(!w0->getEndPort()->getValue() && w1->getEndPort()->getValue() && w1->getStartPort()->getValue());
        QCOMPARE(display->getValue(), 2);
        QCOMPARE(source->tick(), u64(1));
        QCOMPARE(source->value(), u64(2));                               // the list 1, 2, 3 at tick 1
        QVERIFY(reg->getValue());

        // The same result again changes nothing; a changed one updates only what differs
        result.netValues[scene.wireMap().wire2net.at(w0)] = 1;
        scene.receiveResult(result);
        QCOMPARE(display->getValue(), 3);
    }

    void resultsOfAnOtherRunAreIgnored()
    {
        CircuitScene scene;
        SimLink link(scene);
        auto* source = add<SourceItem>(scene, SourceProps::waveform({false, true}), {-200, 0});
        auto* display = add<DisplayItem>(scene, DisplayProps{1, 0});
        connectPorts(&scene, source->getOutputPorts()[0], display->getInputPorts()[0]);
        scene.startOrResumeSim();

        SimResult result;
        result.netValues = {0, 1};
        result.sourceTick = 1;
        result.runId = link.started.first().runId + 5;                   // not this run
        scene.receiveResult(result);
        QCOMPARE(display->getValue(), 0);

        result.runId = link.started.first().runId;
        scene.pauseSim();
        scene.receiveResult(result);                                     // a paused run still takes its own last results
        QCOMPARE(display->getValue(), 1);

        scene.stopSim();
        QCOMPARE(display->getValue(), 0);                                // stopping clears what the run left behind
        QCOMPARE(source->tick(), u64(0));
        scene.receiveResult(result);                                     // and anything still arriving is dropped
        QCOMPARE(display->getValue(), 0);

        // A result bigger than the circuit (from some other circuit) must not crash either
        scene.startOrResumeSim();
        SimResult wrongSize;
        wrongSize.runId = link.started.last().runId;
        wrongSize.netValues.assign(50, 1);
        wrongSize.sourceTick = 3;
        wrongSize.registerValues.assign(9, true);
        scene.receiveResult(wrongSize);
        QCOMPARE(display->getValue(), 1);
    }
};

QTEST_MAIN(TestScene)
#include "tst_scene.moc"
