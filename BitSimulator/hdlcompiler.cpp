#include "hdlcompiler.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <optional>
#include <QProcess>
#include <QSet>
#include <QTextStream>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;

// A compile, in the order it happens: receiveCode / receiveFiles -> compile (sv2v, yosys, reading
// the netlist) -> layoutDesign (dot) -> sendDesign. The helpers in the namespace below read yosys's
// netlist and build what dot lays out.

namespace {

// One module of yosys's JSON netlist as nodes: its ports, its cells (gates, multiplexers, flip-flops)
// and its submodule instances (blocks). Cell types the canvas has no component for go to `skipped`.
NetModule readModule(const QString& name, const QJsonObject& module, const QSet<QString>& moduleNames, QStringList& skipped)
{
    // A yosys bit is a net number from 2 up, or a constant: "1" is NetBit::One; "0", "x" and "z" are NetBit::Zero
    const auto bitsOf = [](const QJsonValue& value) {
        QList<int> bits;
        for (const QJsonValue& bit : value.toArray()) {
            bits.append(bit.isDouble() ? bit.toInt() : bit.toString() == "1"_L1 ? NetBit::One : NetBit::Zero);
        }
        return bits;
    };
    static const QHash<QString, GType> gates{
        {u"$_NOT_"_s, GType::NOT},   {u"$_AND_"_s, GType::AND},
        {u"$_OR_"_s, GType::OR},     {u"$_XOR_"_s, GType::XOR},
        {u"$_NAND_"_s, GType::NAND}, {u"$_NOR_"_s, GType::NOR},
        {u"$_XNOR_"_s, GType::XNOR},
    };

    // The flip-flops dfflegalize leaves (see compile), e.g. $_DFFE_PP1P_: enable, asynchronous reset to 1
    const auto flipFlopOf = [](const QString& type) -> std::optional<FlipFlop> {
        const auto resetTo = [&type](QLatin1String prefix) { return type.size() > prefix.size() && type[prefix.size()] == u'1'; };
        if (type == "$_DLATCH_P_"_L1) return FlipFlop{true, FlipFlop::Reset::None, false};
        if (type == "$_DFF_P_"_L1 || type == "$_DFFE_PP_"_L1) return FlipFlop{};
        if (type.startsWith("$_DFF_PP"_L1)) return FlipFlop{false, FlipFlop::Reset::Async, resetTo("$_DFF_PP"_L1)};
        if (type.startsWith("$_DFFE_PP"_L1)) return FlipFlop{false, FlipFlop::Reset::Async, resetTo("$_DFFE_PP"_L1)};
        if (type.startsWith("$_SDFFCE_PP"_L1)) return FlipFlop{false, FlipFlop::Reset::Sync, resetTo("$_SDFFCE_PP"_L1)};
        return std::nullopt;
    };

    NetModule result;
    result.name = name;
    bool usesOne = false;

    // The module's ports: an input port drives its bits into the module, an output port reads them
    const QJsonObject ports = module["ports"_L1].toObject();
    for (auto it = ports.begin(); it != ports.end(); ++it) {
        const QJsonObject port = it.value().toObject();
        const bool input = port["direction"_L1].toString() != "output"_L1;   // inout counts as input
        NetNode node;
        node.id = it.key();
        node.type = input ? IOType::IN : IOType::OUT;
        node.ports.append(NetPort{it.key(), input ? PortType::OUT : PortType::IN, bitsOf(port["bits"_L1])});
        usesOne |= !input && node.ports.first().bits.contains(NetBit::One);
        result.nodes.append(node);
    }

    // Cells: gates, muxes, flip-flops and instances of other modules
    const QJsonObject cells = module["cells"_L1].toObject();
    for (auto it = cells.begin(); it != cells.end(); ++it) {
        const QJsonObject cell = it.value().toObject();
        const QString cellType = cell["type"_L1].toString();
        NetNode node;
        node.id = it.key();
        if (moduleNames.contains(cellType)) node.type = ModuleRef{cellType};
        else if (const auto gate = gates.constFind(cellType); gate != gates.constEnd()) node.type = *gate;
        else if (cellType == "$_MUX_"_L1) node.type = MType::MUX;
        else if (const auto flipFlop = flipFlopOf(cellType)) node.type = *flipFlop;
        else {
            skipped.append(cellType);
            continue;
        }

        const QJsonObject connections = cell["connections"_L1].toObject();
        const QJsonObject directions = cell["port_directions"_L1].toObject();
        for (auto conn = connections.begin(); conn != connections.end(); ++conn) {
            const bool output = directions[conn.key()].toString() == "output"_L1;
            NetPort port{conn.key(), output ? PortType::OUT : PortType::IN, bitsOf(conn.value())};
            usesOne |= !output && port.bits.contains(NetBit::One);
            node.ports.append(port);
        }
        std::ranges::sort(node.ports, {}, &NetPort::name);
        result.nodes.append(node);
    }

    // Inputs tied to 1 are wired to one constant-1 node
    if (usesOne) {
        NetNode one;
        one.id = u"$const1"_s;
        one.type = ConstOne{};
        one.ports.append(NetPort{u"Y"_s, PortType::OUT, {NetBit::One}});
        result.nodes.append(one);
    }
    return result;
}

// For every bit: the node and output pin that drive it
QHash<int, std::pair<int, int>> driversOf(const NetModule& module)
{
    QHash<int, std::pair<int, int>> drivers;
    for (int n = 0; n < module.nodes.size(); n++) {
        const QList<int> outputs = nodePins(module.nodes[n]).outputs;
        for (int pin = 0; pin < outputs.size(); pin++) {
            if (outputs[pin] == NetBit::One || outputs[pin] >= 2) drivers.insert(outputs[pin], {n, pin});
        }
    }
    return drivers;
}

// Every input pin of a node, as the bits it reads (gate inputs, mux data and select, register pins, ...)
QList<int> inputBitsOf(const NetNode& node)
{
    const NodePins pins = nodePins(node);
    QList<int> bits = pins.inputs + pins.select;
    if (std::holds_alternative<FlipFlop>(node.type)) bits << pins.clock << pins.enable;
    return bits;
}

// The graph dot lays out: one record per node with its input pins on the left and output pins on the
// right, at least as big as its component is drawn, and an edge from each driving pin to every pin it drives
QString dotGraphOf(const NetModule& module, bool topLevel)
{
    // A module port is drawn with its name on it, so dot makes room for the name. In a record label
    // {, }, |, <, >, quotes, backslashes and spaces have a meaning and are escaped.
    const auto label = [](const NetNode& node) {
        if (!std::holds_alternative<IOType>(node.type) || node.id.isEmpty()) return u"x"_s;
        QString escaped;
        for (const QChar c : node.id) {
            if (u"{}|<>\"\\ "_s.contains(c)) escaped += u'\\';
            escaped += c;
        }
        return escaped;
    };

    QString text;
    QTextStream out(&text);
    out << "digraph m {\nrankdir=LR;\nnode [shape=record, fontsize=10, height=0.4];\n";
    for (int n = 0; n < module.nodes.size(); n++) {
        const NetNode& node = module.nodes[n];
        const int inputs = static_cast<int>(inputBitsOf(node).size());
        const int outputs = static_cast<int>(nodePins(node).outputs.size());
        out << "n" << n << " [label=\"";
        if (inputs > 0) {
            out << "{";
            for (int i = 0; i < inputs; i++) out << (i ? "|" : "") << "<i" << i << ">";
            out << "}|";
        }
        out << label(node);
        if (outputs > 0) {
            out << "|{";
            for (int o = 0; o < outputs; o++) out << (o ? "|" : "") << "<o" << o << ">";
            out << "}";
        }
        const QSizeF size = nodeSize(node, topLevel);   // in inches; canvas units are points
        out << "\", width=" << size.width() / 72.0 << ", height=" << size.height() / 72.0 << "];\n";
    }
    const auto drivers = driversOf(module);
    for (int n = 0; n < module.nodes.size(); n++) {
        const QList<int> inputs = inputBitsOf(module.nodes[n]);
        for (int pin = 0; pin < inputs.size(); pin++) {
            const auto driver = drivers.constFind(inputs[pin]);
            if (driver == drivers.constEnd()) continue;
            out << "n" << driver->first << ":o" << driver->second << " -> n" << n << ":i" << pin << ";\n";
        }
    }
    out << "}\n";
    return text;
}

// When even a fast dot takes too long: one column per logic level, inputs on the left, outputs on the
// right. A node's level is one more than the highest level of the nodes driving it; module inputs and
// the constant are level 0 and outputs get a last column of their own; flip-flop and block outputs
// start new paths, so feedback loops end. Each column is as wide as its widest component, and
// components are stacked by their real heights.
void layoutInColumns(NetModule& module, bool topLevel)
{
    const int count = static_cast<int>(module.nodes.size());
    if (count == 0) return;

    const auto drivers = driversOf(module);
    const auto startsPaths = [](const NetNode& node) {
        return std::holds_alternative<FlipFlop>(node.type) || std::holds_alternative<ModuleRef>(node.type)
            || std::holds_alternative<ConstOne>(node.type) || isModulePort(node, IOType::IN);
    };
    QList<int> level(count, 0);
    for (int pass = 0; pass < 200; pass++) {
        bool changed = false;
        for (int n = 0; n < count; n++) {
            const NetNode& node = module.nodes[n];
            int highest = -1;
            for (const int bit : inputBitsOf(node)) {
                const auto driver = drivers.constFind(bit);
                if (driver == drivers.constEnd()) continue;
                highest = std::max(highest, startsPaths(module.nodes[driver->first]) ? 0 : level[driver->first]);
            }
            const bool firstColumn = std::holds_alternative<ConstOne>(node.type) || isModulePort(node, IOType::IN);
            const int next = firstColumn ? 0 : highest + 1;
            if (next != level[n]) {
                level[n] = next;
                changed = true;
            }
        }
        if (!changed) break;
    }
    const int last = *std::ranges::max_element(level) + 1;
    for (int n = 0; n < count; n++) {
        if (isModulePort(module.nodes[n], IOType::OUT)) level[n] = last;
    }

    constexpr double Gap = 40;
    const int columns = last + 1;
    QList<double> columnWidth(columns, 0), columnFilled(columns, 0), columnX(columns, 0);
    QList<QSizeF> size(count);
    for (int n = 0; n < count; n++) {
        size[n] = nodeSize(module.nodes[n], topLevel);
        columnWidth[level[n]] = std::max(columnWidth[level[n]], size[n].width());
    }
    for (int c = 1; c < columns; c++) columnX[c] = columnX[c - 1] + (columnWidth[c - 1] + columnWidth[c]) / 2 + Gap;
    for (int n = 0; n < count; n++) {
        double& filled = columnFilled[level[n]];
        module.nodes[n].position = QPointF(columnX[level[n]], filled + size[n].height() / 2);
        filled += size[n].height() + Gap / 2;
    }
}

// The scene is 160000 x 100000 units; a module's drawing is centred in it
void centreInScene(NetModule& module)
{
    if (module.nodes.isEmpty()) return;
    double minX = std::numeric_limits<double>::max(), maxX = std::numeric_limits<double>::lowest();
    double minY = minX, maxY = maxX;
    for (const NetNode& node : module.nodes) {
        minX = std::min(minX, node.position.x()); maxX = std::max(maxX, node.position.x());
        minY = std::min(minY, node.position.y()); maxY = std::max(maxY, node.position.y());
    }
    const QPointF shift(80000 - (minX + maxX) / 2, 50000 - (minY + maxY) / 2);
    for (NetNode& node : module.nodes) node.position += shift;
}

} // namespace

//===================== Compiling ========================

void HDLCompiler::receiveCode(const QString& code)
{
    // The editor's text goes through the same steps as a folder's files, as code.v or code.vhd
    const QString file = looksLikeVhdl(code) ? u"code.vhd"_s : u"code.v"_s;
    finished(writeTextFile(file, code) && compile({file}, {}));
}

void HDLCompiler::receiveFiles(const QStringList& files, const QStringList& includeDirs)
{
    if (files.isEmpty()) {
        error(u"No Verilog or VHDL files were chosen."_s);
        finished(false);
        return;
    }
    finished(compile(files, includeDirs));
}

bool HDLCompiler::compile(const QStringList& files, const QStringList& includeDirs)
{
    // 0. ghdl turns any VHDL into Verilog, vhdl.v, which then goes along with the Verilog files
    QStringList verilogFiles, vhdlFiles;
    for (const QString& file : files) (isVhdlFile(file) ? vhdlFiles : verilogFiles).append(file);
    if (!vhdlFiles.isEmpty()) {
        if (!convertVhdl(vhdlFiles, verilogFiles, u"vhdl.v"_s)) return false;
        verilogFiles.append(u"vhdl.v"_s);
    }

    // 1. sv2v turns the files into one plain Verilog file, converted.v. Yosys's own reader supports only
    //    part of SystemVerilog (for example no "import pkg::*"); sv2v handles packages, imports, enums,
    //    structs and `include files, in whatever order the files come.
    QStringList sv2vArguments;
    for (const QString& dir : includeDirs) sv2vArguments << u"--incdir="_s + dir;
    sv2vArguments << u"--write=converted.v"_s << verilogFiles;
    if (!runTool(u"sv2v"_s, sv2vPath, sv2vArguments, 60000)) return false;

    // 2. Yosys synthesizes it to gates. The top module is found automatically (the module no other
    //    module uses), and the hierarchy is kept: each submodule instance becomes a block.
    //    dfflegalize turns yosys's many kinds of flip-flop into the few the simulator has (see FlipFlop):
    //    inverters make every clock rising-edge and every enable and reset active-high, extra logic
    //    handles a reset that only acts while enabled, and a flip-flop that powers up as 1 is stored
    //    inverted, so all power up as 0. One with both an asynchronous set and reset cannot be turned
    //    into these; yosys then stops with an error that says so.
    const QString script = u"read_verilog converted.v\n"
        "synth -auto-top -noalumacc\n"
        "dfflegalize -cell $_DFF_P_ 0 -cell $_DFFE_PP_ 0 -cell $_DFF_PP?_ 0 -cell $_DFFE_PP?P_ 0 -cell $_SDFFCE_PP?P_ 0 -cell $_DLATCH_P_ 0\n"
        "abc -g AND,OR,XOR,NAND,NOR,XNOR,MUX\n"
        "opt_clean\n"
        "write_json code_netlist.json\n"_s;
    if (!writeTextFile(u"compile.ys"_s, script)) return false;
    if (!runTool(u"Yosys"_s, yosysPath, {u"-s"_s, u"compile.ys"_s}, 100000)) return false;

    // 3. Read yosys's netlist, every module of it
    QFile netlist(u"code_netlist.json"_s);
    if (!netlist.open(QIODevice::ReadOnly)) {
        error("Could not open the netlist yosys wrote: " + netlist.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(netlist.readAll(), &parseError);
    const QJsonObject modules = document.object()["modules"_L1].toObject();
    if (parseError.error != QJsonParseError::NoError) {
        error("The netlist yosys wrote could not be read: " + parseError.errorString());
        return false;
    }
    if (modules.isEmpty()) {
        error(u"The design has no modules."_s);
        return false;
    }

    NetDesign design;
    const QStringList names = modules.keys();
    const QSet<QString> moduleNames(names.begin(), names.end());
    QStringList skipped;
    for (auto it = modules.begin(); it != modules.end(); ++it) {
        const QJsonObject module = it.value().toObject();
        design.modules.insert(it.key(), readModule(it.key(), module, moduleNames, skipped));
        if (module["attributes"_L1].toObject().contains("top"_L1)) design.top = it.key();
    }
    if (design.top.isEmpty()) design.top = names.first();
    if (!skipped.isEmpty()) {
        skipped.removeDuplicates();
        qDebug() << "Cell types the canvas has no component for, left out:" << skipped;
    }

    // 4. Lay every module out, and hand the design to the canvas
    if (!layoutDesign(design)) return false;
    sendDesign(design);
    return true;
}

// Every module gets its own dot process, and all of them start at once; then they are waited for
// together, against one deadline. A module whose dot runs out of time gets the next faster layout in
// a second round (again all at once): full dot, then fast dot, then columns by logic level.
bool HDLCompiler::layoutDesign(NetDesign& design)
{
    const auto cellsOf = [](const NetModule& module) {   // what yosys made: gates, muxes, flip-flops, blocks
        return std::ranges::count_if(module.nodes, [](const NetNode& node) {
            return !std::holds_alternative<IOType>(node.type) && !std::holds_alternative<ConstOne>(node.type);
        });
    };
    struct Job {
        NetModule* module = nullptr;
        bool topLevel = false;           // the top module's ports are sources and displays, not port tags
        bool fast = false;
        QString graphFile, layoutFile;   // dot reads one and writes the other (a file: a pipe could fill up and stall it)
        std::unique_ptr<QProcess> dot;
        qint64 finishedMs = -1;          // when it finished, from the start of its round; -1 while running
    };
    std::vector<Job> jobs;
    jobs.reserve(design.modules.size());
    for (NetModule& module : design.modules) {
        Job& job = jobs.emplace_back();
        job.module = &module;
        job.topLevel = (module.name == design.top);
        job.fast = cellsOf(module) >= FastDotCells;
        job.graphFile = u"module_%1.dot"_s.arg(jobs.size() - 1);
        job.layoutFile = u"module_%1.plain"_s.arg(jobs.size() - 1);
        if (!writeTextFile(job.graphFile, dotGraphOf(module, job.topLevel))) return false;
    }

    constexpr int PollMs = 20;   // how often the waiting checks on each dot
    std::vector<Job*> round;
    for (Job& job : jobs) round.push_back(&job);
    while (!round.empty()) {
        for (Job* job : round) {
            QStringList arguments{u"-Tplain"_s, u"-o"_s, job->layoutFile, job->graphFile};
            // Fast: dot's two slowest steps stop early. Crossing reduction (mclimit) does fewer passes and
            // the positioning (nslimit) fewer improvement steps, so the layout is a little less tidy.
            if (job->fast) arguments << u"-Gnslimit=0.1"_s << u"-Gmclimit=0.1"_s;
            QFile::remove(job->layoutFile);   // never read a layout left by an earlier compile
            job->dot = std::make_unique<QProcess>();
            job->finishedMs = -1;
            job->dot->start(dotPath, arguments);
        }

        // Wait until every dot has finished or the time is up; note when each one finished
        QElapsedTimer clock;
        clock.start();
        for (bool waiting = true; waiting && clock.elapsed() < DotTimeoutMs;) {
            waiting = false;
            for (Job* job : round) {
                if (job->finishedMs >= 0) continue;
                if (job->dot->state() == QProcess::NotRunning || job->dot->waitForFinished(PollMs)) job->finishedMs = clock.elapsed();
                else waiting = true;
            }
        }
        for (Job* job : round) {   // stop the ones that ran out of time first, so none outlives a failure below
            if (job->finishedMs >= 0) continue;
            job->dot->kill();
            job->dot->waitForFinished();
        }

        std::vector<Job*> next;
        for (Job* job : round) {
            NetModule& module = *job->module;
            const QString name = u"dot%1 for %2 (%3 cells)"_s
                                     .arg(job->fast ? u" (fast)"_s : QString(), module.name).arg(cellsOf(module));
            if (job->finishedMs < 0) {
                if (!job->fast) {
                    qDebug() << name << "ran out of time; trying a fast dot";
                    job->fast = true;
                    next.push_back(job);
                } else {
                    qDebug() << name << "ran out of time; placing it in columns";
                    layoutInColumns(module, job->topLevel);
                    centreInScene(module);
                }
                continue;
            }
            if (!checkFinished(*job->dot, name)) return false;
            qDebug() << name << "took" << job->finishedMs << "ms";

            // -Tplain: "node <name> <x> <y> ..." in inches, y upwards. The canvas uses points, y downwards.
            QFile layout(job->layoutFile);
            if (!layout.open(QIODevice::ReadOnly | QIODevice::Text)) {
                error("Could not read the layout of " + module.name + ": " + layout.errorString());
                return false;
            }
            for (const QString& line : QString::fromUtf8(layout.readAll()).split(u'\n')) {
                const QStringList parts = line.split(u' ', Qt::SkipEmptyParts);
                if (parts.size() < 4 || parts[0] != "node"_L1 || !parts[1].startsWith(u'n')) continue;
                bool ok = false;
                const int n = parts[1].mid(1).toInt(&ok);
                if (!ok || n < 0 || n >= module.nodes.size()) continue;
                module.nodes[n].position = QPointF(parts[2].toDouble() * 72.0, -parts[3].toDouble() * 72.0);
            }
            centreInScene(module);
        }
        round = std::move(next);
    }
    return true;
}

//===================== Running the tools ========================

// Runs a tool and waits for it; false (after error() said why) if it failed or ran out of time
bool HDLCompiler::runTool(const QString& name, const QString& program, const QStringList& arguments, int timeoutMs,
                          const QString& outputFile, QString* failure)
{
    QElapsedTimer timer;
    timer.start();
    QProcess process;
    if (!outputFile.isEmpty()) process.setStandardOutputFile(outputFile);
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs) && process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished();
        const QString message = u"%1 took longer than %2 seconds and was stopped."_s.arg(name).arg(timeoutMs / 1000);
        if (failure) *failure = message;
        else error(message);
        return false;
    }
    if (!checkFinished(process, name, failure)) return false;
    qDebug() << name << "took" << timer.elapsed() << "ms";
    return true;
}

bool HDLCompiler::checkFinished(QProcess& process, const QString& name, QString* failure)
{
    QString message;
    if (process.error() == QProcess::FailedToStart) {
        message = name + " did not run: " + process.errorString();
    } else if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        // The tools write their own messages (with file and line) to their error output
        message = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (message.isEmpty()) message = name + " failed with exit code " + QString::number(process.exitCode());
    } else {
        return true;
    }
    if (failure) *failure = message;
    else error(message);
    return false;
}

bool HDLCompiler::convertVhdl(const QStringList& vhdlFiles, const QStringList& verilogFiles, const QString& output)
{
    if (!QFile::exists(ghdlPath)) {
        error(u"VHDL needs ghdl, which is not bundled for this system yet (it would be %1)."_s.arg(ghdlPath));
        return false;
    }

    // --std=08 reads VHDL-2008; -fsynopsys allows the common std_logic_unsigned / std_logic_arith
    // packages; -frelaxed accepts older code that 2008 is strict about. "-e" without a name: ghdl finds
    // the top entity itself, the one no other entity uses, and makes a module of it and of every
    // entity below it. Its messages go to its error output; the Verilog to its standard output.
    QStringList arguments{u"synth"_s, u"--std=08"_s, u"-fsynopsys"_s, u"-frelaxed"_s, u"--out=verilog"_s};
    arguments << vhdlFiles << u"-e"_s;
    QString failure;
    if (!runTool(u"ghdl"_s, ghdlPath, arguments, 60000, output, &failure)) {
        if (failure.contains(u"no top unit found"_s)) {
            failure += u"\n\nghdl needs exactly one top entity: one that no other entity uses. Leave out entities "
                       "that are not part of the design, such as testbenches, or a second, separate design."_s;
        }
        error(failure);
        return false;
    }

    // ghdl names the module of a used entity "<entity>_B<architecture>"; give it the entity's own name,
    // so the block says "alu" and a Verilog module can use the entity by its name. Kept as it is when
    // an entity became several modules (one per set of generics) or the name is a Verilog module's.
    QFile file(output);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error("Could not read the Verilog ghdl wrote: " + file.errorString());
        return false;
    }
    QString verilog = QString::fromUtf8(file.readAll());
    file.close();

    static const QRegularExpression moduleName(u"^\\s*module\\s+([A-Za-z_][A-Za-z0-9_$]*)"_s, QRegularExpression::MultilineOption);
    const auto namesIn = [](const QString& text) {
        QSet<QString> names;
        for (auto it = moduleName.globalMatch(text); it.hasNext();) names.insert(it.next().captured(1));
        return names;
    };
    QSet<QString> taken;
    for (const QString& path : verilogFiles) {
        QFile source(path);
        if (source.open(QIODevice::ReadOnly | QIODevice::Text)) taken.unite(namesIn(QString::fromUtf8(source.readAll())));
    }
    const QSet<QString> modules = namesIn(verilog);
    QHash<QString, int> perEntity;
    for (const QString& module : modules) perEntity[vhdlEntityOfModule(module)]++;
    taken.unite(modules);

    for (const QString& module : modules) {
        const QString entity = vhdlEntityOfModule(module);
        if (entity == module || perEntity.value(entity) != 1 || taken.contains(entity)) continue;
        // Only a module that is used: the top entity keeps the name it was declared with, which can
        // contain "_B" itself ("reg_Bank")
        const QRegularExpression use(u"(?<![\\w$\\\\])"_s + QRegularExpression::escape(module) + u"(?![\\w$])"_s);
        if (verilog.count(use) < 2) continue;
        verilog.replace(use, entity);
        taken.insert(entity);
    }
    return writeTextFile(output, verilog);
}

bool HDLCompiler::writeTextFile(const QString& fileName, const QString& text)
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        error("Could not write " + fileName + ": " + file.errorString());
        return false;
    }
    file.write(text.toUtf8());
    return true;
}
