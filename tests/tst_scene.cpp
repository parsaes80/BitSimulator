// The canvas without the window around it: components and their pins, the editing rules,
// the simulation state machine, and the translation of the drawing into the netlist the
// simulator runs.

#include "TestSupport.h"

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
    return port->getConnections().isEmpty() ? 0 : map.wire2net.at(port->getConnections().first());
}

} // namespace

class TestScene : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { map.clear(); }

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
        QVERIFY2(refused.error().contains(QStringLiteral("Input 4")), qPrintable(refused.error()));
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
        QVERIFY(!refused.has_value() && refused.error().contains(QStringLiteral("clock")));
        refused = scene.editComponent(reg, RegisterProps{RType::D, true, false});
        QVERIFY(!refused.has_value() && refused.error().contains(QStringLiteral("enable")));
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
        QVERIFY(!refused.has_value() && refused.error().contains(QStringLiteral("Output 2")));
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
        QVERIFY(!refused.has_value() && refused.error().contains(QStringLiteral("Select line 3")));
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
        QCOMPARE(map.net2wire.empty(), endsRun);                         // the wire map belongs to the run
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
        QVERIFY(map.net2wire.empty() && map.wire2net.empty());

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
        QCOMPARE(map.net2wire.at(1).size(), std::size_t(3));             // all three wires colour together
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
        QCOMPARE(r.clkID, u32(0));                                       // which is what makes the simulator treat it as a latch
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
        QCOMPARE(netOf(gate->getInputPort(0)), netOf(display->getInputPorts()[0]));     // same net on both sides of the display
        QCOMPARE(netOf(gate2->getInputPort(0)), u32(0));
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
        QCOMPARE(link.started.first().numNets, u32(0));
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
        result.netValues[map.wire2net.at(w1)] = 1;                       // bit 1 high, bit 0 low: the display should read 2
        result.sourceTick = 1;
        result.registerValues = {true};
        scene.receiveResult(result);

        QVERIFY(!w0->getEndPort()->getValue() && w1->getEndPort()->getValue() && w1->getStartPort()->getValue());
        QCOMPARE(display->getValue(), 2);
        QCOMPARE(source->tick(), u64(1));
        QCOMPARE(source->value(), u64(2));                               // the list 1, 2, 3 at tick 1
        QVERIFY(reg->getValue());

        // The same result again changes nothing; a changed one updates only what differs
        result.netValues[map.wire2net.at(w0)] = 1;
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
