// The simulator alone, in the test's thread, so every tick's result is exact

#include <QtTest>
#include <numeric>
#include "simulator.h"

using namespace Qt::StringLiterals;

namespace {

// Fewer than `ticks` if the simulator stalls
QList<SimResult> run(const ExportGraph& graph, int ticks)
{
    Simulator sim;
    QList<SimResult> results;
    QEventLoop loop;
    QObject::connect(&sim, &Simulator::sendResult, &sim, [&](SimResult result) {
        results.append(std::move(result));
        if (results.size() >= ticks) loop.quit();
    });

    sim.SimController();          // creates the tick timer
    sim.setTimerPeriod(0);        // as fast as the event loop goes
    sim.receiveCircuit(graph);    // also computes the first tick
    if (results.size() < ticks) {
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);    // give up instead of hanging
        loop.exec();
    }
    sim.unloadCircuit();

    if (results.size() > ticks) results.resize(ticks);
    return results;
}

// Outputs bit `bit` of the tick: `width` of them walk through every combination
Source counterBit(u32 net, int bit, int width)
{
    QList<bool> cycle(qsizetype(1) << width);
    for (qsizetype step = 0; step < cycle.size(); step++) cycle[step] = (step >> bit) & 1;
    return Source({net}, SourceProps::waveform(cycle));
}

Source pattern(u32 net, std::initializer_list<int> bits)
{
    QList<bool> cycle;
    for (const int bit : bits) cycle.append(bit != 0);
    return Source({net}, SourceProps::waveform(cycle));
}

bool gateOutput(GType type, const QList<bool>& inputs)
{
    const bool all = std::ranges::all_of(inputs, [](bool v) { return v; });
    const bool any = std::ranges::any_of(inputs, [](bool v) { return v; });
    const bool odd = std::accumulate(inputs.begin(), inputs.end(), 0) % 2 == 1;
    switch (type) {
    case GType::AND:  return all;
    case GType::NAND: return !all;
    case GType::OR:   return any;
    case GType::NOR:  return !any;
    case GType::XOR:  return odd;
    case GType::XNOR: return !odd;
    case GType::NOT:  return !inputs.value(0);
    }
    return false;
}

const char* name(GType type)
{
    switch (type) {
    case GType::AND: return "AND"; case GType::NAND: return "NAND"; case GType::OR: return "OR"; case GType::NOR: return "NOR";
    case GType::XOR: return "XOR"; case GType::XNOR: return "XNOR"; case GType::NOT: return "NOT";
    }
    return "?";
}

const char* name(RType type)
{
    switch (type) { case RType::SR: return "SR"; case RType::JK: return "JK"; case RType::D: return "D"; case RType::T: return "T"; }
    return "?";
}

}

class TestSimulator : public QObject {
    Q_OBJECT
private slots:
    void asynchronousResetDoesNotWaitForTheClock()
    {
        // Nets: 1 clock (never ticks), 2 D = 0, 3 reset (high at tick 2 only), 4 Q
        for (const bool async : {true, false}) {
            ExportGraph graph;
            graph.numNets = 4;
            graph.sources.emplace_back(std::vector<u32>{1}, SourceProps::waveform({false}));
            graph.sources.emplace_back(std::vector<u32>{2}, SourceProps::waveform({false}));
            graph.sources.emplace_back(std::vector<u32>{3}, SourceProps::waveform({false, false, true, false, false, false, false, false}));
            graph.registers.emplace_back(RType::D, 2, 3, 4, 1, 0, async, true);   // resets to 1
            const QList<SimResult> results = run(graph, 6);
            QCOMPARE(results.size(), 6);
            for (int tick = 0; tick < results.size(); tick++) {
                // Asynchronous: 1 from the reset's tick; synchronous: waits for an edge that never comes
                QCOMPARE(bool(results[tick].netValues[4]), async && tick >= 2);
            }
        }
    }

    //===================== Gates ========================

    void gateTruthTables_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<int>("numInputs");
        for (const GType type : {GType::AND, GType::NAND, GType::OR, GType::NOR, GType::XOR, GType::XNOR}) {
            for (int inputs = 2; inputs <= 4; inputs++) {
                QTest::addRow("%s with %d inputs", name(type), inputs) << static_cast<int>(type) << inputs;
            }
        }
        QTest::newRow("NOT") << static_cast<int>(GType::NOT) << 1;
    }
    void gateTruthTables()
    {
        QFETCH(int, type);
        QFETCH(int, numInputs);
        const GType gateType = static_cast<GType>(type);
        const u32 out = numInputs + 1;

        ExportGraph graph;
        std::vector<u32> inputNets;
        for (int i = 0; i < numInputs; i++) {
            graph.sources.push_back(counterBit(i + 1, i, numInputs));
            inputNets.push_back(i + 1);
        }
        graph.gates.emplace_back(gateType, inputNets, out);
        graph.numNets = out;

        const int combinations = 1 << numInputs;
        const QList<SimResult> results = run(graph, combinations + 2);     // +2: wrap around once
        QCOMPARE(results.size(), combinations + 2);

        for (int tick = 0; tick < results.size(); tick++) {
            const auto& nets = results[tick].netValues;
            QList<bool> inputs;
            for (int i = 0; i < numInputs; i++) {
                inputs.append(nets[i + 1]);
                QCOMPARE(bool(nets[i + 1]), bool(((tick % combinations) >> i) & 1));    // the inputs are what we fed in
            }
            if (bool(nets[out]) != gateOutput(gateType, inputs)) {
                QFAIL(qPrintable(u"tick %1: inputs %2 gave %3"_s.arg(tick).arg(tick % combinations, numInputs, 2, QLatin1Char('0')).arg(nets[out])));
            }
        }
    }

    void unconnectedInputReadsAsZero()
    {
        // Net 0 = not connected; always reads 0
        ExportGraph graph;
        graph.sources.push_back(pattern(1, {1, 1, 0, 1}));
        graph.gates.emplace_back(GType::AND, std::vector<u32>{1, 0}, 2);
        graph.gates.emplace_back(GType::OR, std::vector<u32>{1, 0}, 3);
        graph.gates.emplace_back(GType::NOT, std::vector<u32>{0}, 4);
        graph.numNets = 4;

        for (const SimResult& result : run(graph, 8)) {
            QCOMPARE(result.netValues[0], u8(0));
            QCOMPARE(result.netValues[2], u8(0));                       // AND with a missing input is never 1
            QCOMPARE(result.netValues[3], result.netValues[1]);         // OR with a missing input follows the other
            QCOMPARE(result.netValues[4], u8(1));                       // NOT of nothing is 1
        }
    }

    void gateWithoutOutputWireIsHarmless()
    {
        ExportGraph graph;
        graph.sources.push_back(pattern(1, {0, 1}));
        graph.gates.emplace_back(GType::NOT, std::vector<u32>{1}, 0);   // output not wired
        graph.numNets = 1;
        const auto results = run(graph, 4);
        QCOMPARE(results.size(), 4);
        for (const SimResult& result : results) QCOMPARE(result.netValues[0], u8(0));
    }

    void changesTravelThroughAChainInOneTick()
    {
        // All five outputs settled in every result
        ExportGraph graph;
        graph.sources.push_back(pattern(1, {0, 1, 1, 0}));
        for (u32 i = 1; i <= 5; i++) graph.gates.emplace_back(GType::NOT, std::vector<u32>{i}, i + 1);
        graph.numNets = 6;

        const auto results = run(graph, 8);
        QCOMPARE(results.size(), 8);
        for (const SimResult& result : results) {
            for (u32 i = 1; i <= 5; i++) QVERIFY(result.netValues[i + 1] != result.netValues[i]);
        }
    }

    void oneOutputFeedsSeveralGates()
    {
        ExportGraph graph;
        graph.sources.push_back(pattern(1, {0, 1}));
        graph.sources.push_back(pattern(2, {0, 0, 1, 1}));
        graph.gates.emplace_back(GType::AND, std::vector<u32>{1, 2}, 3);
        graph.gates.emplace_back(GType::XOR, std::vector<u32>{1, 2}, 4);
        graph.gates.emplace_back(GType::OR, std::vector<u32>{3, 4}, 5);
        graph.numNets = 5;

        for (const SimResult& result : run(graph, 8)) {
            const bool a = result.netValues[1], b = result.netValues[2];
            QCOMPARE(bool(result.netValues[3]), a && b);
            QCOMPARE(bool(result.netValues[4]), a != b);
            QCOMPARE(bool(result.netValues[5]), a || b);
        }
    }

    void aLoopOfGatesDoesNotHang()
    {
        // A NOT wired to itself never settles; the simulator gives up on the tick and goes on
        ExportGraph graph;
        graph.gates.emplace_back(GType::NOT, std::vector<u32>{1}, 1);
        graph.sources.push_back(pattern(2, {0, 1}));
        graph.numNets = 2;

        const auto results = run(graph, 5);
        QCOMPARE(results.size(), 5);
        QCOMPARE(results[0].netValues[2], u8(0));       // the rest of the circuit still works
        QCOMPARE(results[1].netValues[2], u8(1));
    }

    //===================== Multiplexer ========================

    void multiplexer_data()
    {
        QTest::addColumn<int>("numInputs");
        QTest::newRow("2 inputs") << 2;
        QTest::newRow("3 inputs") << 3;
        QTest::newRow("4 inputs") << 4;
        QTest::newRow("5 inputs") << 5;
    }
    void multiplexer()
    {
        QFETCH(int, numInputs);
        int selectLines = 1;
        while ((1 << selectLines) < numInputs) selectLines++;
        const int width = numInputs + selectLines;
        const u32 out = width + 1;

        ExportGraph graph;
        std::vector<u32> data, address;
        for (int i = 0; i < numInputs; i++) { graph.sources.push_back(counterBit(i + 1, i, width)); data.push_back(i + 1); }
        for (int i = 0; i < selectLines; i++) { graph.sources.push_back(counterBit(numInputs + i + 1, numInputs + i, width)); address.push_back(numInputs + i + 1); }
        graph.muxes.emplace_back(data, address, out);
        graph.numNets = out;

        const int combinations = 1 << width;
        const auto results = run(graph, combinations);
        QCOMPARE(results.size(), combinations);

        for (const SimResult& result : results) {
            int selected = 0;
            for (int i = 0; i < selectLines; i++) selected |= result.netValues[numInputs + i + 1] << i;
            selected = std::min(selected, numInputs - 1);      // past the last input picks the last
            QCOMPARE(result.netValues[out], result.netValues[selected + 1]);
        }
    }

    //===================== Registers ========================

    void registers_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<bool>("hasClock");
        QTest::addColumn<bool>("hasEnable");
        for (const RType type : {RType::D, RType::T, RType::SR, RType::JK}) {
            QTest::addRow("%s flip-flop with enable", name(type)) << static_cast<int>(type) << true << true;
            QTest::addRow("%s flip-flop without enable", name(type)) << static_cast<int>(type) << true << false;
            QTest::addRow("%s latch with enable", name(type)) << static_cast<int>(type) << false << true;
            QTest::addRow("%s latch without enable", name(type)) << static_cast<int>(type) << false << false;
        }
    }
    void registers()
    {
        QFETCH(int, type);
        QFETCH(bool, hasClock);
        QFETCH(bool, hasEnable);
        const RType regType = static_cast<RType>(type);
        const u32 in1 = 1, in2 = 2, clk = 3, en = 4, out = 5;

        // Cycle lengths 5, 7, 4, 3 share no factor, so 210 ticks see many situations
        ExportGraph graph;
        graph.sources.push_back(pattern(in1, {1, 0, 1, 1, 0}));
        graph.sources.push_back(pattern(in2, {0, 0, 1, 0, 0, 0, 1}));
        graph.sources.push_back(pattern(clk, {0, 1, 1, 0}));
        graph.sources.push_back(pattern(en, {1, 1, 0}));
        graph.registers.emplace_back(regType, in1, in2, out, hasClock ? clk : 0, hasEnable ? en : 0);
        graph.numNets = out;

        const auto results = run(graph, 210);
        QCOMPARE(results.size(), 210);

        bool stored = false, previousClock = false;
        int changes = 0;
        for (int tick = 0; tick < results.size(); tick++) {
            const auto& nets = results[tick].netValues;
            const bool a = nets[in1], b = nets[in2], clock = nets[clk], enable = nets[en];

            const bool before = stored;
            const bool enabled = !hasEnable || enable;
            if (hasClock) {
                if (!previousClock && clock && enabled) {
                    switch (regType) {
                    case RType::D:  stored = b ? false : a; break;                 // second pin is a reset
                    case RType::T:  if (a) stored = !stored; if (b) stored = false; break;
                    case RType::SR: if (a && !b) stored = true; else if (!a && b) stored = false; break;
                    case RType::JK: if (a && !b) stored = true; else if (!a && b) stored = false; else if (a && b) stored = !stored; break;
                    }
                }
                previousClock = clock;
            } else if (enabled) {
                switch (regType) {
                case RType::D:
                case RType::T:  stored = a; break;
                case RType::SR: if (a && !b) stored = true; else if (!a && b) stored = false; break;
                case RType::JK: if (a && !b) stored = true; else if (!a && b) stored = false; else if (a && b) stored = !stored; break;
                }
            }
            changes += (stored != before);

            // The new value shows in the edge's tick, as in an HDL simulator
            if (bool(nets[out]) != stored) QFAIL(qPrintable(u"tick %1: output pin shows %2 but %3 was stored"_s.arg(tick).arg(nets[out]).arg(stored)));
            if (bool(results[tick].registerValues[0]) != stored) {
                QFAIL(qPrintable(u"tick %1 (in1=%2 in2=%3 clk=%4 en=%5): register holds %6, expected %7"_s
                    .arg(tick).arg(a).arg(b).arg(clock).arg(enable).arg(bool(results[tick].registerValues[0])).arg(stored)));
            }
        }
        QVERIFY2(changes > 5, "the test inputs should make the register change many times");
    }

    //===================== Sources ========================

    void sourcesStepThroughTheirValuesAndRepeat()
    {
        ExportGraph graph;
        graph.sources.push_back(pattern(1, {1, 0, 0}));
        graph.sources.push_back(pattern(2, {0, 1}));
        graph.numNets = 2;

        const auto results = run(graph, 9);
        QCOMPARE(results.size(), 9);
        const bool first[] = {1, 0, 0}, second[] = {0, 1};
        for (int tick = 0; tick < 9; tick++) {
            QCOMPARE(results[tick].sourceTick, u64(tick));
            QCOMPARE(bool(results[tick].netValues[1]), first[tick % 3]);
            QCOMPARE(bool(results[tick].netValues[2]), second[tick % 2]);
        }
    }

    void aSourceDrivesEachOfItsPins()
    {
        // 3-pin counter on nets 1..3 (pin 1 unwired), 40-bit counter with its top pin on net 4
        ExportGraph graph;
        graph.sources.push_back(Source({1, 0, 3}, SourceProps::counter(3)));
        SourceProps wide = SourceProps::counter(40);
        wide.start = (u64(1) << 39) - 2;                           // the top bit turns on after 2 steps
        std::vector<u32> widePins(40, 0);
        widePins[39] = 4;
        graph.sources.push_back(Source(widePins, wide));
        graph.numNets = 4;

        const auto results = run(graph, 10);
        for (int tick = 0; tick < 10; tick++) {
            const int value = tick % 8;
            QCOMPARE(bool(results[tick].netValues[1]), bool(value & 1));
            QCOMPARE(bool(results[tick].netValues[2]), false);   // nothing drives net 2
            QCOMPARE(bool(results[tick].netValues[3]), bool(value & 4));
            QCOMPARE(bool(results[tick].netValues[4]), tick >= 2);
        }
    }

    void emptyCircuitRuns()
    {
        const auto results = run(ExportGraph{}, 3);
        QCOMPARE(results.size(), 3);
        QCOMPARE(results[0].netValues.size(), std::size_t(1));      // just net 0
        QVERIFY(results[0].registerValues.empty());
    }

    //===================== Start, pause, stop ========================

    void firstResultComesAtOnce()
    {
        Simulator sim;
        int received = 0;
        connect(&sim, &Simulator::sendResult, &sim, [&received](const SimResult&) { received++; });
        sim.SimController();

        ExportGraph graph;
        graph.sources.push_back(pattern(1, {1}));
        graph.numNets = 1;
        sim.receiveCircuit(graph);
        QCOMPARE(received, 1);          // without waiting for the 100 ms timer
    }

    void resultsCarryTheRunId()
    {
        Simulator sim;
        QList<SimResult> results;
        connect(&sim, &Simulator::sendResult, &sim, [&results](SimResult result) { results.append(std::move(result)); });
        sim.SimController();
        sim.setTimerPeriod(1);

        ExportGraph graph;
        graph.sources.push_back(pattern(1, {0, 1, 1}));
        graph.numNets = 1;

        graph.runId = 7;
        sim.receiveCircuit(graph);
        QTRY_VERIFY(results.size() >= 4);
        for (const SimResult& result : results) QCOMPARE(result.runId, u32(7));

        results.clear();
        graph.runId = 8;
        sim.receiveCircuit(graph);                                  // loading again starts from step 0
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].runId, u32(8));
        QCOMPARE(results[0].sourceTick, u64(0));
    }

    void pauseKeepsEverythingAndContinueCarriesOn()
    {
        Simulator sim;
        QList<SimResult> results;
        connect(&sim, &Simulator::sendResult, &sim, [&results](SimResult result) { results.append(std::move(result)); });
        sim.SimController();
        sim.setTimerPeriod(1);

        // Toggles on every edge, so there is state to lose
        ExportGraph graph;
        graph.sources.push_back(pattern(1, {0, 1}));                // clock
        graph.sources.push_back(pattern(2, {1}));                   // T = 1
        graph.sources.push_back(pattern(3, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}));
        graph.registers.emplace_back(RType::T, 2, 0, 4, 1, 0);
        graph.numNets = 4;
        sim.receiveCircuit(graph);
        QTRY_VERIFY(results.size() >= 9);

        sim.setRunning(false);
        const qsizetype paused = results.size();
        const SimResult last = results.last();
        QTest::qWait(60);
        QCOMPARE(results.size(), paused);                           // 60 tick periods later: nothing

        sim.setRunning(true);
        QTRY_VERIFY(results.size() > paused);
        const SimResult next = results[paused];
        QCOMPARE(next.sourceTick, last.sourceTick + 1);                                   // the next tick, not tick 0
        QCOMPARE(bool(next.netValues[4]), bool(last.registerValues[0]));                  // the register kept its value
    }

    void unloadStopsTheSimulation()
    {
        Simulator sim;
        int received = 0;
        connect(&sim, &Simulator::sendResult, &sim, [&received](const SimResult&) { received++; });
        sim.SimController();
        sim.setTimerPeriod(1);

        ExportGraph graph;
        graph.sources.push_back(pattern(1, {0, 1}));
        graph.numNets = 1;
        sim.receiveCircuit(graph);
        QTRY_VERIFY(received >= 3);

        sim.unloadCircuit();
        const int stopped = received;
        QTest::qWait(60);
        QCOMPARE(received, stopped);

        sim.setRunning(true);           // with nothing loaded must not crash
        QTest::qWait(20);
        sim.setRunning(false);
    }

    void speedCanChangeBeforeTheTimerExists()
    {
        Simulator sim;
        sim.setTimerPeriod(5);          // the GUI may send this before the thread starts
        sim.SimController();
        sim.setTimerPeriod(5);
    }
};

QTEST_GUILESS_MAIN(TestSimulator)
#include "tst_simulator.moc"
