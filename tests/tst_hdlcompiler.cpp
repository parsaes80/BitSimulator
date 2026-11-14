// The Verilog compiler on its own: Verilog text in, a graph of components out.
//
// It runs the real tools (yosys, then Graphviz dot), so every test here is skipped when they
// are not available in the working directory.

#include "TestSupport.h"
#include "hdlcompiler.h"

using namespace TestSupport;

namespace {

struct CompileRun {
    bool finished = false;
    bool ok = false;
    int graphs = 0;
    QHash<QString, Node> graph;
    QStringList errors;

    int count(GType type) const { return countIf([type](const Node& n) { return std::holds_alternative<GType>(n.type) && std::get<GType>(n.type) == type; }); }
    int count(RType type) const { return countIf([type](const Node& n) { return std::holds_alternative<RType>(n.type) && std::get<RType>(n.type) == type; }); }
    int count(IOType type) const { return countIf([type](const Node& n) { return std::holds_alternative<IOType>(n.type) && std::get<IOType>(n.type) == type; }); }
    int gates() const { return countIf([](const Node& n) { return std::holds_alternative<GType>(n.type); }); }
    int muxes() const { return countIf([](const Node& n) { return std::holds_alternative<MType>(n.type); }); }

private:
    template <typename Pred> int countIf(Pred pred) const { return static_cast<int>(std::ranges::count_if(graph, pred)); }
};

CompileRun compile(HDLCompiler& compiler, const QString& verilog)
{
    CompileRun run;
    const auto c1 = QObject::connect(&compiler, &HDLCompiler::sendGraph, &compiler, [&run](const QHash<QString, Node>& graph) { run.graph = graph; run.graphs++; });
    const auto c2 = QObject::connect(&compiler, &HDLCompiler::error, &compiler, [&run](const QString& message) { run.errors << message; });
    const auto c3 = QObject::connect(&compiler, &HDLCompiler::finished, &compiler, [&run](bool ok) { run.finished = true; run.ok = ok; });
    compiler.receiveCode(verilog);          // runs yosys and dot and returns when they are done
    QObject::disconnect(c1); QObject::disconnect(c2); QObject::disconnect(c3);
    return run;
}

CompileRun compile(const QString& verilog)
{
    HDLCompiler compiler;
    return compile(compiler, verilog);
}

} // namespace

class TestHdlCompiler : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { REQUIRE_HDL_TOOLS(); }

    void aSingleGate()
    {
        const CompileRun run = compile(QStringLiteral("module top(input wire a, input wire b, output wire y);\nassign y = a & b;\nendmodule\n"));
        QVERIFY(run.finished && run.ok);
        QCOMPARE(run.graphs, 1);
        QVERIFY2(run.errors.isEmpty(), qPrintable(run.errors.join('\n')));

        QCOMPARE(run.count(GType::AND), 1);
        QCOMPARE(run.gates(), 1);
        QCOMPARE(run.count(IOType::IN), 2);
        QCOMPARE(run.count(IOType::OUT), 1);
        QCOMPARE(run.graph.size(), 4);
    }

    void gateKinds_data()
    {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<int>("type");
        QTest::newRow("or") << "a | b" << static_cast<int>(GType::OR);
        QTest::newRow("xor") << "a ^ b" << static_cast<int>(GType::XOR);
        QTest::newRow("nand") << "~(a & b)" << static_cast<int>(GType::NAND);
        QTest::newRow("nor") << "~(a | b)" << static_cast<int>(GType::NOR);
        QTest::newRow("xnor") << "~(a ^ b)" << static_cast<int>(GType::XNOR);
        QTest::newRow("not") << "~a" << static_cast<int>(GType::NOT);
    }
    void gateKinds()
    {
        QFETCH(QString, expression);
        QFETCH(int, type);
        const CompileRun run = compile(QStringLiteral("module top(input wire a, input wire b, output wire y);\nassign y = %1;\nendmodule\n").arg(expression));
        QVERIFY(run.ok);
        QCOMPARE(run.gates(), 1);
        QCOMPARE(run.count(static_cast<GType>(type)), 1);
    }

    void everyWireHasADriver()
    {
        const CompileRun run = compile(QStringLiteral(
            "module top(input wire clk, input wire [1:0] a, input wire [1:0] b, input wire s, output reg [1:0] r);\n"
            "always @(posedge clk) r <= s ? (a & b) : (a ^ b);\nendmodule\n"));
        QVERIFY(run.ok);
        QCOMPARE(run.count(RType::D), 2);                                // one flip-flop per output bit
        QCOMPARE(run.muxes(), 2);
        QCOMPARE(run.count(IOType::IN), 4);                              // clk, a, b, s
        QCOMPARE(run.count(IOType::OUT), 1);

        // Collect which bits are driven and which are read; nothing may be read without being driven
        QSet<int> driven, read;
        for (const Node& node : run.graph) {
            for (const Port& port : node.ports) {
                for (const int bit : port.connections) (port.type == PortType::OUT ? driven : read).insert(bit);
            }
        }
        QVERIFY(!read.isEmpty());
        QVERIFY2((read - driven).isEmpty(), "a component input is connected to a bit that nothing drives");
    }

    void componentsGetDifferentPositions()
    {
        const CompileRun run = compile(QStringLiteral("module top(input wire a, input wire b, input wire c, output wire y);\nassign y = (a & b) | c;\nendmodule\n"));
        QVERIFY(run.ok);
        QSet<QPair<int, int>> positions;
        for (const Node& node : run.graph) positions.insert({qRound(node.position.x()), qRound(node.position.y())});
        QCOMPARE(positions.size(), run.graph.size());                    // the layout step placed each one somewhere else
    }

    void aSyntaxErrorIsReported()
    {
        const CompileRun run = compile(QStringLiteral("module top(input wire a, output wire y);\nassign y = a &&& ;\nendmodule\n"));
        QVERIFY(run.finished);
        QVERIFY(!run.ok);
        QCOMPARE(run.graphs, 0);                                         // no half-built circuit is sent
        QCOMPARE(run.errors.size(), 1);
        QVERIFY2(run.errors.first().contains(QStringLiteral("ERROR")), qPrintable(run.errors.first()));
        QVERIFY2(run.errors.first().contains(QStringLiteral("code.v:2")), qPrintable(run.errors.first()));     // with the line number
    }

    void theModuleMustBeCalledTop()
    {
        const CompileRun run = compile(QStringLiteral("module adder(input wire a, input wire b, output wire y);\nassign y = a ^ b;\nendmodule\n"));
        QVERIFY(run.finished && !run.ok);
        QCOMPARE(run.graphs, 0);
        QVERIFY(!run.errors.isEmpty());
    }

    void emptyTextFailsCleanly()
    {
        const CompileRun run = compile(QString());
        QVERIFY(run.finished && !run.ok);
        QCOMPARE(run.graphs, 0);
    }

    void oneCompilerCanCompileAgainAndAgain()
    {
        const QString andGate = QStringLiteral("module top(input wire a, input wire b, output wire y);\nassign y = a & b;\nendmodule\n");
        const QString twoGates = QStringLiteral("module top(input wire a, input wire b, input wire c, output wire y);\nassign y = (a ^ b) | c;\nendmodule\n");

        HDLCompiler compiler;
        const CompileRun first = compile(compiler, twoGates);
        const CompileRun second = compile(compiler, andGate);
        const CompileRun failed = compile(compiler, QStringLiteral("module top(;\n"));
        const CompileRun third = compile(compiler, andGate);
        QVERIFY(first.ok && second.ok && !failed.ok && third.ok);

        QCOMPARE(first.gates(), 2);
        // Nothing of an earlier compile may leak into a later one
        QCOMPARE(second.graph.size(), 4);
        QCOMPARE(second.gates(), 1);
        QCOMPARE(third.graph.size(), second.graph.size());
        const QList<QString> thirdIds = third.graph.keys(), secondIds = second.graph.keys();
        QCOMPARE(QSet<QString>(thirdIds.begin(), thirdIds.end()), QSet<QString>(secondIds.begin(), secondIds.end()));
    }
};

QTEST_MAIN(TestHdlCompiler)
#include "tst_hdlcompiler.moc"
