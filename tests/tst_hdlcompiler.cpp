// The Verilog compiler: Verilog text in, a design (one laid-out netlist per module) out; and the
// canvas showing it: blocks for submodules, simulated through, opened like folders.
//
// It runs the real tools (sv2v, yosys, then Graphviz dot), so every test here is skipped when they
// are not available in the working directory.

#include "TestSupport.h"
#include "hdlcompiler.h"
#include "simulator.h"
#include <QFileInfo>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

using namespace TestSupport;

namespace {

struct CompileRun {
    bool finished = false;
    bool ok = false;
    int graphs = 0;
    NetDesign design;
    QStringList errors;

    // The nodes of a module; the top module by default
    QList<NetNode> nodes(const QString& module = {}) const
    {
        const NetModule* found = design.module(module.isEmpty() ? design.top : module);
        return found ? found->nodes : QList<NetNode>{};
    }
    // How many nodes of a module hold `type`, e.g. count(GType::XOR) or count(ModuleRef{"adder"})
    template <typename T> int count(T type, const QString& module = {}) const
    {
        return countIf([type](const NetNode& n) { const T* t = std::get_if<T>(&n.type); return t && *t == type; }, module);
    }
    template <typename T> int countKind(const QString& module = {}) const
    {
        return countIf([](const NetNode& n) { return std::holds_alternative<T>(n.type); }, module);
    }
    int gates(const QString& module = {}) const { return countKind<GType>(module); }
    int muxes(const QString& module = {}) const { return countKind<MType>(module); }

private:
    template <typename Pred> int countIf(Pred pred, const QString& module) const
    {
        return static_cast<int>(std::ranges::count_if(nodes(module), pred));
    }
};

CompileRun compile(HDLCompiler& compiler, const QString& verilog)
{
    CompileRun run;
    const auto c1 = QObject::connect(&compiler, &HDLCompiler::sendDesign, &compiler, [&run](const NetDesign& design) { run.design = design; run.graphs++; });
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

// The Compile Dir path: files on disk instead of the editor's text
CompileRun compileFiles(const QStringList& files, const QStringList& includeDirs = {})
{
    HDLCompiler compiler;
    CompileRun run;
    QObject::connect(&compiler, &HDLCompiler::sendDesign, &compiler, [&run](const NetDesign& design) { run.design = design; run.graphs++; });
    QObject::connect(&compiler, &HDLCompiler::error, &compiler, [&run](const QString& message) { run.errors << message; });
    QObject::connect(&compiler, &HDLCompiler::finished, &compiler, [&run](bool ok) { run.finished = true; run.ok = ok; });
    compiler.receiveFiles(files, includeDirs);
    return run;
}

void writeFile(const QString& path, const QString& text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(text.toUtf8());
}

// Runs a netlist in this thread and returns the first `ticks` results (as tst_simulator does)
QList<SimResult> simulate(const ExportGraph& graph, int ticks)
{
    Simulator sim;
    QList<SimResult> results;
    QEventLoop loop;
    QObject::connect(&sim, &Simulator::sendResult, &sim, [&](SimResult result) {
        results.append(std::move(result));
        if (results.size() >= ticks) loop.quit();
    });
    sim.SimController();
    sim.setTimerPeriod(0);
    sim.receiveCircuit(graph);
    if (results.size() < ticks) {
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    sim.unloadCircuit();
    if (results.size() > ticks) results.resize(ticks);
    return results;
}

// Two levels of submodules: top uses add2 (three half adders, an OR, and an input passed straight
// through to an output) and dff2 (two flip-flops); top also drives a constant 1.
const QString hierarchicalVerilog = u"module ha(input wire a, input wire b, output wire s, output wire c);\n"
    "  assign s = a ^ b;\n  assign c = a & b;\nendmodule\n"
    "module add2(input wire [1:0] x, input wire [1:0] y, output wire [2:0] z, output wire [1:0] xcopy);\n"
    "  wire c0, s1, c1a, c1b;\n"
    "  ha h0(.a(x[0]), .b(y[0]), .s(z[0]), .c(c0));\n"
    "  ha h1(.a(x[1]), .b(y[1]), .s(s1), .c(c1a));\n"
    "  ha h2(.a(s1), .b(c0), .s(z[1]), .c(c1b));\n"
    "  assign z[2] = c1a | c1b;\n  assign xcopy = x;\nendmodule\n"
    "module dff2(input wire clk, input wire [1:0] d, output reg [1:0] q);\n"
    "  always @(posedge clk) q <= d;\nendmodule\n"
    "module top(input wire clk, input wire [1:0] a, input wire [1:0] b,\n"
    "           output wire [2:0] sum, output wire [1:0] acopy, output wire [1:0] regq, output wire one);\n"
    "  add2 u(.x(a), .y(b), .z(sum), .xcopy(acopy));\n"
    "  dff2 r(.clk(clk), .d(sum[1:0]), .q(regq));\n"
    "  assign one = 1'b1;\nendmodule\n"_s;

template <typename T>
T* findItem(const QGraphicsScene* scene, auto&& pred)
{
    for (T* item : TestSupport::itemsOfType<T>(scene)) {
        if (pred(item)) return item;
    }
    return nullptr;
}

BlockItem* blockNamed(const QGraphicsScene* scene, const QString& instance)
{
    return findItem<BlockItem>(scene, [&](BlockItem* b) { return b->blockProps().instance == instance; });
}

// The component that drives a pin, through the wire on it
QGraphicsObject* driverOf(PortItem* pin)
{
    return pin->getConnections().isEmpty() ? nullptr : pin->getConnections().first()->getStartPort()->getParent();
}

// The value on a terminal's pins, lowest bit first
int terminalValue(const QGraphicsScene* scene, const QString& name)
{
    const TerminalItem* terminal = findItem<TerminalItem>(scene, [&](TerminalItem* t) { return t->name() == name; });
    if (!terminal) return -1;
    int value = 0;
    for (int i = 0; i < terminal->pins().size(); i++) value |= int(terminal->pins()[i]->getValue()) << i;
    return value;
}

} // namespace

class TestHdlCompiler : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { if (!hdlToolsAvailable()) QSKIP(MissingHdlTools); }

    void aSingleGate()
    {
        const CompileRun run = compile(u"module top(input wire a, input wire b, output wire y);\nassign y = a & b;\nendmodule\n"_s);
        QVERIFY(run.finished && run.ok);
        QCOMPARE(run.graphs, 1);
        QVERIFY2(run.errors.isEmpty(), qPrintable(run.errors.join('\n')));

        QCOMPARE(run.count(GType::AND), 1);
        QCOMPARE(run.gates(), 1);
        QCOMPARE(run.count(IOType::IN), 2);
        QCOMPARE(run.count(IOType::OUT), 1);
        QCOMPARE(run.nodes().size(), 4);
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
        const CompileRun run = compile(u"module top(input wire a, input wire b, output wire y);\nassign y = %1;\nendmodule\n"_s.arg(expression));
        QVERIFY(run.ok);
        QCOMPARE(run.gates(), 1);
        QCOMPARE(run.count(static_cast<GType>(type)), 1);
    }

    void everyWireHasADriver()
    {
        const CompileRun run = compile(u"module top(input wire clk, input wire [1:0] a, input wire [1:0] b, input wire s, output reg [1:0] r);\n"
            "always @(posedge clk) r <= s ? (a & b) : (a ^ b);\nendmodule\n"_s);
        QVERIFY(run.ok);
        QCOMPARE(run.countKind<FlipFlop>(), 2);                              // one flip-flop per output bit
        QCOMPARE(run.muxes(), 2);
        QCOMPARE(run.count(IOType::IN), 4);                              // clk, a, b, s
        QCOMPARE(run.count(IOType::OUT), 1);

        // Collect which bits are driven and which are read; nothing may be read without being driven
        QSet<int> driven, read;
        for (const NetNode& node : run.nodes()) {
            for (const NetPort& port : node.ports) {
                for (const int bit : port.bits) {
                    if (bit != NetBit::Zero) (port.type == PortType::OUT ? driven : read).insert(bit);
                }
            }
        }
        QVERIFY(!read.isEmpty());
        QVERIFY2((read - driven).isEmpty(), "a component input is connected to a bit that nothing drives");
    }

    void componentsGetDifferentPositions()
    {
        const CompileRun run = compile(u"module top(input wire a, input wire b, input wire c, output wire y);\nassign y = (a & b) | c;\nendmodule\n"_s);
        QVERIFY(run.ok);
        QSet<QPair<int, int>> positions;
        for (const NetNode& node : run.nodes()) positions.insert({qRound(node.position.x()), qRound(node.position.y())});
        QCOMPARE(positions.size(), run.nodes().size());                    // the layout step placed each one somewhere else
    }

    void inputsBecomeSourcesCoveringTheirWidth()
    {
        // A 1-bit input toggles every tick; a wider one counts 0, 1, 2, ... holding each value for
        // 2 ticks. Wide inputs used to get a list too long to play (15 bits) or almost no values (32 bits).
        const CompileRun run = compile(u"module top(input wire clk, input wire [14:0] a, input wire [31:0] b, output wire [31:0] y);\n"
            "assign y = b ^ {17'b0, a} ^ {31'b0, clk};\nendmodule\n"_s);
        QVERIFY2(run.ok, qPrintable(run.errors.join(u'\n')));

        CircuitScene scene;
        scene.receiveDesign(run.design);
        QMap<int, SourceProps> byWidth;
        for (SourceItem* source : TestSupport::itemsOfType<SourceItem>(&scene)) byWidth[source->sourceProps().bits] = source->sourceProps();
        QCOMPARE(byWidth.keys(), (QList<int>{1, 15, 32}));

        QCOMPARE(byWidth[1].mode, SourceMode::Waveforms);
        QCOMPARE(sourceValueAt(byWidth[1], 0), u64(0));
        QCOMPARE(sourceValueAt(byWidth[1], 1), u64(1));
        for (const int width : {15, 32}) {
            const SourceProps& counter = byWidth[width];
            QCOMPARE(counter.mode, SourceMode::Counter);
            QCOMPARE(counter.ticksPerStep, 2);
            const u64 largest = (u64(1) << width) - 1;
            QCOMPARE(sourceValueAt(counter, 3), u64(1));
            QCOMPARE(sourceValueAt(counter, 2 * largest), largest);          // reaches the top of its range
            QCOMPARE(sourceValueAt(counter, 2 * (largest + 1)), u64(0));     // then wraps
        }
    }

    void aFolderOfFilesWithSubmodulesAndIncludes()
    {
        // cpu.sv (the top: nothing instantiates it) uses an adder from another folder, whose width
        // comes from a header in a third folder; folder names have spaces in them
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        writeFile(root + "/shared headers/widths.vh", "`define W 4\n");
        writeFile(root + "/rtl parts/adder.v",
                  "`include \"widths.vh\"\nmodule adder(input wire [`W-1:0] a, b, output wire [`W-1:0] s);\n"
                  "  assign s = a ^ b;\nendmodule\n");
        writeFile(root + "/cpu.sv",
                  "module cpu(input logic clk, input logic [3:0] x, y, output logic [3:0] r);\n"
                  "  logic [3:0] s;\n  adder u0(.a(x), .b(y), .s(s));\n  always_ff @(posedge clk) r <= s;\nendmodule\n");

        const CompileRun run = compileFiles({root + "/rtl parts/adder.v", root + "/cpu.sv"}, {root + "/shared headers"});
        QVERIFY2(run.ok, qPrintable(run.errors.join(u'\n')));
        QCOMPARE(run.graphs, 1);
        QCOMPARE(run.design.top, u"cpu"_s);
        QCOMPARE(run.count(ModuleRef{u"adder"_s}), 1);      // the adder is a block in cpu
        QCOMPARE(run.count(GType::XOR, u"adder"_s), 4);     // with its gates inside
        QCOMPARE(run.gates(), 0);
        QCOMPARE(run.countKind<FlipFlop>(), 4);
        QCOMPARE(run.count(IOType::IN), 3);                              // clk, x, y
        QCOMPARE(run.count(IOType::OUT), 1);                             // r
    }

    void packagesAndImportsInAnyFileOrder()
    {
        // Like a typical SystemVerilog project: a package with an enum, imported with "import pkg::*;"
        // and used as a port type. Yosys's own reader cannot do this; sv2v converts it first.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        writeFile(root + "/alu_pkg.sv",
                  "package alu_pkg;\n  typedef enum logic [1:0] {OP_AND = 2'd0, OP_OR = 2'd1, OP_XOR = 2'd2} op_t;\nendpackage\n");
        writeFile(root + "/ALU.sv",
                  "import alu_pkg::*;\n"
                  "module ALU(input logic a, b, input op_t op, output logic y);\n"
                  "  always_comb case (op)\n    OP_AND: y = a & b;\n    OP_OR: y = a | b;\n    default: y = a ^ b;\n  endcase\n"
                  "endmodule\n");

        // The module that uses the package first, the package last: alphabetical, like the file list
        const CompileRun run = compileFiles({root + "/ALU.sv", root + "/alu_pkg.sv"});
        QVERIFY2(run.ok, qPrintable(run.errors.join(u'\n')));
        QCOMPARE(run.graphs, 1);
        QCOMPARE(run.count(IOType::IN), 3);                              // a, b and the 2-bit op
        QCOMPARE(run.count(IOType::OUT), 1);
        QVERIFY(run.gates() + run.muxes() > 0);
    }

    void aFolderCompileReportsErrors()
    {
        QTemporaryDir dir;
        writeFile(dir.path() + "/broken.v", "module broken(input wire a, output wire y);\n assign y = ;\nendmodule\n");
        const CompileRun broken = compileFiles({dir.path() + "/broken.v"});
        QVERIFY(broken.finished && !broken.ok);
        const QString message = broken.errors.join(u' ');
        QVERIFY2(message.contains(u"broken.v:2"_s) && message.contains(u"Parse error"_s), qPrintable(message));   // file and line

        const CompileRun nothing = compileFiles({});
        QVERIFY(nothing.finished && !nothing.ok);
        QVERIFY(nothing.errors.join(u' ').contains(u"No Verilog files"_s));
    }

    void aSyntaxErrorIsReported()
    {
        const CompileRun run = compile(u"module top(input wire a, output wire y);\nassign y = a &&& ;\nendmodule\n"_s);
        QVERIFY(run.finished);
        QVERIFY(!run.ok);
        QCOMPARE(run.graphs, 0);                                         // no half-built circuit is sent
        QCOMPARE(run.errors.size(), 1);
        QVERIFY2(run.errors.first().contains(u"code.v:2"_s) && run.errors.first().contains(u"Parse error"_s),
                 qPrintable(run.errors.first()));                        // sv2v names the line
        QVERIFY2(run.errors.first().contains(u"code.v:2"_s), qPrintable(run.errors.first()));     // with the line number
    }

    void theTopModuleCanHaveAnyName()
    {
        // The top is the module no other module uses, whatever it is called
        const CompileRun single = compile(u"module adder(input wire a, input wire b, output wire y);\nassign y = a ^ b;\nendmodule\n"_s);
        QVERIFY2(single.ok, qPrintable(single.errors.join(u'\n')));
        QCOMPARE(single.count(GType::XOR), 1);

        // With a submodule written above it, the outer module is still the top; the submodule is a block in it
        const CompileRun nested = compile(u"module inv(input wire a, output wire y);\nassign y = ~a;\nendmodule\n"
            "module pair(input wire p, q, output wire r, s);\ninv i0(.a(p), .y(r));\ninv i1(.a(q), .y(s));\nendmodule\n"_s);
        QVERIFY2(nested.ok, qPrintable(nested.errors.join(u'\n')));
        QCOMPARE(nested.graphs, 1);
        QCOMPARE(nested.design.top, u"pair"_s);
        QCOMPARE(nested.count(ModuleRef{u"inv"_s}), 2);     // i0 and i1
        QCOMPARE(nested.count(GType::NOT, u"inv"_s), 1);
        QCOMPARE(nested.count(IOType::IN), 2);                           // p and q: the ports of pair, not of inv
        QCOMPARE(nested.count(IOType::OUT), 2);
    }

    void submodulesBecomeBlocks()
    {
        const CompileRun run = compile(hierarchicalVerilog);
        QVERIFY2(run.ok, qPrintable(run.errors.join(u'\n')));
        QCOMPARE(run.design.top, u"top"_s);
        QCOMPARE(run.design.modules.size(), 4);
        QCOMPARE(run.count(ModuleRef{u"add2"_s}), 1);
        QCOMPARE(run.count(ModuleRef{u"dff2"_s}), 1);
        QCOMPARE(run.count(ModuleRef{u"ha"_s}, u"add2"_s), 3);
        QCOMPARE(run.countKind<FlipFlop>(u"dff2"_s), 2);

        CircuitScene scene;
        scene.receiveDesign(run.design);
        QCOMPARE(itemsOfType<BlockItem>(&scene).size(), 2);              // u and r
        BlockItem* u = blockNamed(&scene, u"u"_s);
        QVERIFY(u);
        QCOMPARE(u->blockProps().module, u"add2"_s);
        QCOMPARE(u->getInputPorts().size(), 4);                          // x[2], y[2]
        QCOMPARE(u->getOutputPorts().size(), 5);                         // xcopy[2], z[3]
        QVERIFY(u->flags() & QGraphicsItem::ItemIsMovable);              // the top level can be edited
        QVERIFY(!u->setProps(BlockProps{}));                             // but a block has nothing to change
        QCOMPARE(itemsOfType<TerminalItem>(&scene).size(), 0);           // the top's ports are sources and displays

        // The inside of a block: port terminals instead of sources and displays, nothing editable
        CircuitScene inside;
        inside.setReadOnly(true);
        inside.buildModule(run.design, u"add2"_s, false);
        QCOMPARE(itemsOfType<TerminalItem>(&inside).size(), 4);          // x, y, z, xcopy
        QCOMPARE(itemsOfType<BlockItem>(&inside).size(), 3);             // h0, h1, h2
        QCOMPARE(itemsOfType<SourceItem>(&inside).size(), 0);
        for (ComponentItem* item : itemsOfType<ComponentItem>(&inside)) QVERIFY(!(item->flags() & QGraphicsItem::ItemIsMovable));
        for (WireItem* wire : itemsOfType<WireItem>(&inside)) QVERIFY(wire->isConnected());
        QCOMPARE(inside.addComponent(GateProps{}, QPointF(80000, 50000)), nullptr);
        const int before = inside.items().size();
        for (QGraphicsItem* item : inside.items()) item->setSelected(true);
        inside.deleteSelection();
        QCOMPARE(inside.items().size(), before);
    }

    void portsShowTheirNamesFromTheCode()
    {
        const CompileRun run = compile(hierarchicalVerilog);
        QVERIFY2(run.ok, qPrintable(run.errors.join(u'\n')));
        CircuitScene scene;
        scene.receiveDesign(run.design);

        // Every input is a source and every output a display, named as in the code. The constant 1
        // is a source too, but not a port: it has no name.
        QStringList sources, displays;
        for (SourceItem* source : itemsOfType<SourceItem>(&scene)) sources << source->name();
        for (DisplayItem* display : itemsOfType<DisplayItem>(&scene)) displays << display->name();
        sources.sort();
        displays.sort();
        QCOMPARE(sources, (QStringList{QString(), u"a"_s, u"b"_s, u"clk"_s}));
        QCOMPARE(displays, (QStringList{u"acopy"_s, u"one"_s, u"regq"_s, u"sum"_s}));

        // Each one is wide enough for its name; one placed by hand has no name and keeps its size
        QFont font;
        font.setPointSizeF(8);
        font.setBold(true);
        for (ComponentItem* item : itemsOfType<ComponentItem>(&scene)) {
            QString name;
            if (auto* source = dynamic_cast<SourceItem*>(item)) name = source->name();
            else if (auto* display = dynamic_cast<DisplayItem*>(item)) name = display->name();
            if (!name.isEmpty()) QVERIFY2(item->boundingRect().width() > QFontMetricsF(font).horizontalAdvance(name), qPrintable(name));
        }
        CircuitScene drawn;
        auto* manual = dynamic_cast<SourceItem*>(drawn.addComponent(SourceProps::counter(2), QPointF(80000, 50000)));
        QVERIFY(manual && manual->name().isEmpty());
        QCOMPARE(qRound(manual->shape().boundingRect().width()), 34);     // 30 wide, as before
        auto* display = dynamic_cast<DisplayItem*>(drawn.addComponent(DisplayProps{2, 0}, QPointF(80200, 50000)));
        QVERIFY(display && display->name().isEmpty());

        // Every component is exactly the size dot was told (componentSize), named or not
        const auto sizeOf = [](ComponentItem* item) { return item->shape().boundingRect().adjusted(2, 2, -2, -2).size(); };
        QList<ComponentItem*> all = itemsOfType<ComponentItem>(&scene) + itemsOfType<ComponentItem>(&drawn);
        for (const ItemProps& props : {ItemProps(GateProps{GType::NOT, 1}), ItemProps(GateProps{GType::AND, 5}), ItemProps(RegisterProps{}),
                                       ItemProps(MuxProps{MType::MUX, 4}), ItemProps(DisplayProps{3, 1})}) {
            all << drawn.addComponent(props, QPointF(81000, 50000 + 200 * all.size()));
        }
        for (ComponentItem* item : all) {
            QString name;
            if (auto* source = dynamic_cast<SourceItem*>(item)) name = source->name();
            else if (auto* display = dynamic_cast<DisplayItem*>(item)) name = display->name();
            QCOMPARE(sizeOf(item), componentSize(item->props(), name));
        }

        // dot is told the sizes, so the wider components do not run into each other
        const QList<ComponentItem*> components = itemsOfType<ComponentItem>(&scene);
        for (qsizetype i = 0; i < components.size(); i++) {
            for (qsizetype j = i + 1; j < components.size(); j++) {
                QVERIFY2(!components[i]->sceneBoundingRect().intersects(components[j]->sceneBoundingRect()),
                         qPrintable(itemTitle(components[i]->props()) + u" and " + itemTitle(components[j]->props())));
            }
        }

        // Inside a block, the port tags fit their names too
        CircuitScene inside;
        inside.buildModule(run.design, u"add2"_s, false);
        QFont tagFont;
        tagFont.setPointSizeF(8);
        for (TerminalItem* terminal : itemsOfType<TerminalItem>(&inside)) {
            const QString label = terminal->pins().size() > 1 ? u"%1[%2]"_s.arg(terminal->name()).arg(terminal->pins().size()) : terminal->name();
            QVERIFY(terminal->boundingRect().width() > QFontMetricsF(tagFont).horizontalAdvance(label) + 10);
            QCOMPARE(terminal->boundingRect().adjusted(2, 2, -2, -2).size(), terminalSize(terminal->name(), int(terminal->pins().size())));
        }
    }

    void blocksAreSimulatedLikeTheirContents()
    {
        const CompileRun run = compile(hierarchicalVerilog);
        QVERIFY2(run.ok, qPrintable(run.errors.join(u'\n')));
        CircuitScene scene;
        scene.receiveDesign(run.design);
        BlockItem* u = blockNamed(&scene, u"u"_s);
        BlockItem* r = blockNamed(&scene, u"r"_s);
        QVERIFY(u && r);

        // Feed every combination: a counts every tick, b every 4 ticks
        auto* a = dynamic_cast<SourceItem*>(driverOf(u->getInputPorts()[0]));
        auto* b = dynamic_cast<SourceItem*>(driverOf(u->getInputPorts()[2]));
        QVERIFY(a && b && a != b);
        QVERIFY(scene.editComponent(a, SourceProps::counter(2, 1)));
        QVERIFY(scene.editComponent(b, SourceProps::counter(2, 4)));

        const auto displayFrom = [&](BlockItem* block, int width) {
            return findItem<DisplayItem>(&scene, [&](DisplayItem* d) {
                return d->getInputPorts().size() == width && driverOf(d->getInputPorts()[0]) == block;
            });
        };
        DisplayItem* sum = displayFrom(u, 3);
        DisplayItem* acopy = displayFrom(u, 2);
        DisplayItem* regq = displayFrom(r, 2);
        DisplayItem* one = findItem<DisplayItem>(&scene, [](DisplayItem* d) { return d->getInputPorts().size() == 1; });
        QVERIFY(sum && acopy && regq && one);

        ExportGraph graph;
        connect(&scene, &CircuitScene::startSimSIG, this, [&graph](const ExportGraph& g) { graph = g; });
        scene.startOrResumeSim();
        QCOMPARE(graph.registers.size(), std::size_t(2));                // dff2's flip-flops, inside block r
        QCOMPARE(graph.gates.size(), std::size_t(7));                    // 3 half adders and the OR, inside block u

        // The view of the inside of u and of one of its half adders, following the run
        CircuitScene insideU, insideH0;
        insideU.buildModule(run.design, u"add2"_s, false);
        insideH0.buildModule(run.design, u"ha"_s, false);

        QSet<int> earlierSums{0};
        const QList<SimResult> results = simulate(graph, 64);
        QCOMPARE(results.size(), 64);
        for (SimResult result : results) {
            result.runId = graph.runId;
            scene.receiveResult(result);
            const int tick = int(result.sourceTick);
            const int expected = int(a->value() + b->value());
            QVERIFY2(sum->getValue() == expected, qPrintable(u"tick %1: %2 + %3 gave %4"_s.arg(tick).arg(a->value()).arg(b->value()).arg(sum->getValue())));
            QCOMPARE(acopy->getValue(), int(a->value()));                // passed straight through the block
            QCOMPARE(one->getValue(), 1);
            QVERIFY2(earlierSums.contains(regq->getValue()), qPrintable(u"tick %1: the register holds %2"_s.arg(tick).arg(regq->getValue())));

            scene.showInstanceValues(u"u"_s, &insideU);
            scene.showInstanceValues(u"u/h0"_s, &insideH0);
            QCOMPARE(terminalValue(&insideU, u"x"_s), int(a->value()));
            QCOMPARE(terminalValue(&insideU, u"y"_s), int(b->value()));
            QCOMPARE(terminalValue(&insideU, u"z"_s), expected);
            QCOMPARE(terminalValue(&insideH0, u"s"_s), expected & 1);
            QCOMPARE(terminalValue(&insideH0, u"c"_s), int(a->value() & b->value() & 1));
            earlierSums.insert(expected & 3);
        }
        scene.stopSim();
    }

    void blocksOpenOnTheSameCanvas()
    {
        const CompileRun run = compile(hierarchicalVerilog);
        QVERIFY2(run.ok, qPrintable(run.errors.join(u'\n')));

        CircuitCanvas canvas;
        canvas.resize(900, 600);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        CircuitScene* top = canvas.getScene();
        top->receiveDesign(run.design);
        QSignalSpy depth(&canvas, &CircuitCanvas::depthChanged);

        // Double-click the block
        BlockItem* u = blockNamed(top, u"u"_s);
        QVERIFY(u);
        canvas.centerOn(u);
        QTest::mouseDClick(canvas.viewport(), Qt::LeftButton, {}, canvas.mapFromScene(u->sceneBoundingRect().center()));
        QCOMPARE(canvas.depth(), 1);
        QCOMPARE(canvas.currentPath(), u"u"_s);
        QVERIFY(canvas.scene() == canvas.currentScene() && canvas.currentScene() != top);
        QVERIFY(canvas.currentScene()->isReadOnly());
        QCOMPARE(itemsOfType<BlockItem>(canvas.currentScene()).size(), 3);
        auto* breadcrumb = canvas.findChild<QFrame*>(u"blockBreadcrumb"_s);
        QVERIFY(breadcrumb && breadcrumb->isVisible());
        QStringList path;
        for (QToolButton* button : breadcrumb->findChildren<QToolButton*>()) path << button->text();
        QCOMPARE(path, (QStringList{u"top"_s, u"u (add2)"_s}));

        // A block inside a block
        canvas.openBlock(blockNamed(canvas.currentScene(), u"h1"_s));
        QCOMPARE(canvas.depth(), 2);
        QCOMPARE(canvas.currentPath(), u"u/h1"_s);
        QCOMPARE(itemsOfType<GateItem>(canvas.currentScene()).size(), 2);
        QCOMPARE(breadcrumb->findChildren<QToolButton*>().size(), 3);
        QVERIFY2(breadcrumb->width() > 100, "the path bar was measured before its buttons appeared");

        // Esc goes up one level, the path's first entry back to the top
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QCOMPARE(canvas.depth(), 1);
        QCOMPARE(canvas.currentPath(), u"u"_s);
        breadcrumb->findChildren<QToolButton*>().first()->click();
        QCOMPARE(canvas.depth(), 0);
        QVERIFY(canvas.scene() == top);
        QVERIFY(!breadcrumb->isVisible());
        QCOMPARE(depth.size(), 4);

        // A new design or Clear always returns to the top
        canvas.openBlock(blockNamed(top, u"r"_s));
        QCOMPARE(canvas.depth(), 1);
        top->receiveDesign(run.design);
        QCOMPARE(canvas.depth(), 0);
        canvas.openBlock(blockNamed(top, u"u"_s));
        top->resetCircuit();
        QCOMPARE(canvas.depth(), 0);
        QVERIFY(canvas.scene() == top);
    }

    void resetInputsStayZero()
    {
        // A reset toggling with the clock would reset the circuit on every clock edge
        const CompileRun run = compile(u"module top(input clk, input rst, input rst_n, input sys_reset, input resetn, input first, input burst, input [1:0] rst_bus,\n"
            "           output y);\n"
            "  assign y = clk ^ rst ^ rst_n ^ sys_reset ^ resetn ^ first ^ burst ^ rst_bus[0] ^ rst_bus[1];\n"
            "endmodule\n"_s);
        QVERIFY2(run.ok, qPrintable(run.errors.join(u'\n')));
        CircuitScene scene;
        scene.receiveDesign(run.design);
        QHash<QString, SourceProps> sources;
        for (SourceItem* source : itemsOfType<SourceItem>(&scene)) sources.insert(source->name(), source->sourceProps());
        for (const char* reset : {"rst", "rst_n", "sys_reset", "resetn"}) {
            QVERIFY2(sources.value(QLatin1String(reset)) == SourceProps::constantOf(0, 1), reset);
        }
        for (const char* other : {"clk", "first", "burst"}) {                 // "rst" inside a word is not a reset
            QVERIFY2(sources.value(QLatin1String(other)).mode == SourceMode::Waveforms, other);
        }
        QCOMPARE(sources.value(u"rst_bus"_s).mode, SourceMode::Counter);   // only 1-bit inputs
    }

    void everyKindOfFlipFlop()
    {
        // One of each kind yosys makes from Verilog. The compiler turns them into the simulator's few
        // kinds; here every output is compared with what the Verilog says, on every tick.
        const CompileRun run = compile(u"module kinds(input clk, input rst, input rst_n, input en, input en_n, input [7:0] d,\n"
            "             output reg [7:0] q1, q2, q3, q4, q5, output reg q7, output reg [7:0] q8);\n"
            "  always @(posedge clk or posedge rst) if (rst) q1 <= 8'hA5; else q1 <= d;              // asynchronous reset to 0s and 1s\n"
            "  always @(posedge clk or negedge rst_n) if (!rst_n) q2 <= 0; else if (!en_n) q2 <= d;  // active-low reset and enable\n"
            "  always @(posedge clk) if (rst) q3 <= 8'h3C; else if (en) q3 <= d;                      // synchronous reset before enable\n"
            "  always @(posedge clk) if (en) begin if (rst) q4 <= 8'h0F; else q4 <= d; end             // enable before reset\n"
            "  always @(negedge clk) q5 <= d;                                                          // falling edge\n"
            "  always @* if (en) q7 = d[1];                                                            // latch\n"
            "  initial q8 = 8'h81;\n"
            "  always @(posedge clk) q8 <= d;                                                          // power-up value\n"
            "endmodule\n"_s);
        QVERIFY2(run.ok, qPrintable(run.errors.join(u'\n')));

        CircuitScene scene;
        scene.receiveDesign(run.design);
        const auto source = [&](const char* name) { return findItem<SourceItem>(&scene, [&](SourceItem* s) { return s->name() == QLatin1String(name); }); };
        const auto display = [&](const char* name) { return findItem<DisplayItem>(&scene, [&](DisplayItem* d) { return d->name() == QLatin1String(name); }); };
        const QList<bool> rst{0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0};
        const QList<bool> rstN{1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1};
        const QList<bool> en{1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1};
        const QList<bool> enN{0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0};
        SourceProps data = SourceProps::counter(8);
        data.step = 37;
        QVERIFY(scene.editComponent(source("rst"), SourceProps::waveform(rst)));
        QVERIFY(scene.editComponent(source("rst_n"), SourceProps::waveform(rstN)));
        QVERIFY(scene.editComponent(source("en"), SourceProps::waveform(en)));
        QVERIFY(scene.editComponent(source("en_n"), SourceProps::waveform(enN)));
        QVERIFY(scene.editComponent(source("d"), data));
        QCOMPARE(source("clk")->sourceProps().mode, SourceMode::Waveforms);                // 0, 1, 0, 1, ...

        // The asynchronous reset to A5 is a register that resets to 1 without waiting for the clock
        const QList<RegisterItem*> registers = itemsOfType<RegisterItem>(&scene);
        QVERIFY(std::ranges::any_of(registers, [](RegisterItem* r) { const auto p = std::get<RegisterProps>(r->props()); return p.asyncReset && p.resetValue; }));

        ExportGraph graph;
        connect(&scene, &CircuitScene::startSimSIG, this, [&graph](const ExportGraph& g) { graph = g; });
        scene.startOrResumeSim();

        // The Verilog, one tick at a time. A flip-flop shows a new value from the tick after it changed.
        struct State { int q1 = 0, q2 = 0, q3 = 0, q4 = 0, q5 = 0, q7 = 0, q8 = 0x81; } state;
        bool previousClock = false;
        for (SimResult result : simulate(graph, 120)) {
            result.runId = graph.runId;
            scene.receiveResult(result);
            const int tick = int(result.sourceTick);
            const QString at = u"tick %1"_s.arg(tick);
            QVERIFY2(display("q1")->getValue() == state.q1, qPrintable(at + " q1"));
            QVERIFY2(display("q2")->getValue() == state.q2, qPrintable(at + " q2"));
            QVERIFY2(display("q3")->getValue() == state.q3, qPrintable(at + " q3"));
            QVERIFY2(display("q4")->getValue() == state.q4, qPrintable(at + " q4"));
            QVERIFY2(display("q5")->getValue() == state.q5, qPrintable(at + " q5"));
            QVERIFY2(display("q7")->getValue() == state.q7, qPrintable(at + " q7"));
            QVERIFY2(display("q8")->getValue() == state.q8, qPrintable(at + " q8"));

            const bool clock = tick & 1;
            const bool rising = tick > 0 && !previousClock && clock;     // no edge at power-up
            const bool falling = tick > 0 && previousClock && !clock;
            previousClock = clock;
            const int d = int(sourceValueAt(data, u64(tick)));
            const bool r = rst[tick % rst.size()], rn = rstN[tick % rstN.size()];
            const bool e = en[tick % en.size()], en_ = enN[tick % enN.size()];
            if (r) state.q1 = 0xA5; else if (rising) state.q1 = d;
            if (!rn) state.q2 = 0; else if (rising && !en_) state.q2 = d;
            if (rising) state.q3 = r ? 0x3C : (e ? d : state.q3);
            if (rising && e) state.q4 = r ? 0x0F : d;
            if (falling) state.q5 = d;
            if (e) state.q7 = (d >> 1) & 1;
            if (rising) state.q8 = d;
        }
        scene.stopSim();
    }

    void aFlipFlopWithAsyncSetAndResetIsReported()
    {
        // The simulator's flip-flops have one reset; yosys cannot turn this one into them, and says so
        const CompileRun run = compile(u"module sr(input clk, input rst_n, input set_n, input d, output reg q);\n"
            "  always @(posedge clk or negedge rst_n or negedge set_n) if (!rst_n) q <= 0; else if (!set_n) q <= 1; else q <= d;\n"
            "endmodule\n"_s);
        QVERIFY(run.finished && !run.ok);
        QVERIFY2(run.errors.join(u' ').contains(u"async set and reset"_s), qPrintable(run.errors.join(u'\n')));
    }

    void emptyTextFailsCleanly()
    {
        const CompileRun run = compile(QString());
        QVERIFY(run.finished && !run.ok);
        QCOMPARE(run.graphs, 0);
    }

    void oneCompilerCanCompileAgainAndAgain()
    {
        const QString andGate = u"module top(input wire a, input wire b, output wire y);\nassign y = a & b;\nendmodule\n"_s;
        const QString twoGates = u"module top(input wire a, input wire b, input wire c, output wire y);\nassign y = (a ^ b) | c;\nendmodule\n"_s;

        HDLCompiler compiler;
        const CompileRun first = compile(compiler, twoGates);
        const CompileRun second = compile(compiler, andGate);
        const CompileRun failed = compile(compiler, u"module top(;\n"_s);
        const CompileRun third = compile(compiler, andGate);
        QVERIFY(first.ok && second.ok && !failed.ok && third.ok);

        QCOMPARE(first.gates(), 2);
        // Nothing of an earlier compile may leak into a later one
        QCOMPARE(second.nodes().size(), 4);
        QCOMPARE(second.gates(), 1);
        QCOMPARE(second.design.modules.size(), 1);
        QCOMPARE(third.nodes().size(), second.nodes().size());
        QSet<QString> thirdIds, secondIds;
        for (const NetNode& node : third.nodes()) thirdIds.insert(node.id);
        for (const NetNode& node : second.nodes()) secondIds.insert(node.id);
        QCOMPARE(thirdIds, secondIds);
    }
};

QTEST_MAIN(TestHdlCompiler)
#include "tst_hdlcompiler.moc"
