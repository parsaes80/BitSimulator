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
#ifdef Q_OS_WASM
#include <QEventLoop>
#include <QTimer>
#include <emscripten.h>
#endif

using namespace Qt::StringLiterals;

#ifdef Q_OS_WASM
// yosys and dot run in Web Workers (web/*-worker.js); C++ starts them and asks how they are doing
EM_JS_DEPS(bitsimulatorWorkers, "$stringToNewUTF8");

// One yosys worker, kept between compiles; its files go through the page's working directory
EM_JS(void, ensureYosysWorker, (), {
    if (Module.bitsimYosys && !Module.bitsimYosys.broken) return;
    const worker = new Worker(new URL("yosys-worker.js", document.baseURI), { type: "module" });
    const yosys = { worker: worker, current: 0, nextId: 1, state: 0, broken: false, done: 0, total: 0 };
    worker.onmessage = event => {
        const data = event.data;
        if (data.download) { yosys.done = data.download.done; yosys.total = data.download.total; return; }
        if (data.id !== yosys.current) return;
        if (data.ok && data.netlist) FS.writeFile("code_netlist.json", data.netlist);
        else FS.writeFile("yosys_log.txt", data.log || "Yosys wrote no netlist.");
        yosys.state = data.ok && data.netlist ? 1 : 2;
    };
    worker.onerror = event => {
        FS.writeFile("yosys_log.txt", "Yosys could not be loaded: " + (event.message || "the worker failed to start"));
        yosys.state = 2;
        yosys.broken = true;
    };
    Module.bitsimYosys = yosys;
});
EM_JS(void, warmUpYosys, (), {
    Module.bitsimYosys.worker.postMessage({ id: -1, args: ["-V"], files: {} });
});
EM_JS(void, startYosysJob, (const char* script, const char* inputNames), {
    const yosys = Module.bitsimYosys;
    yosys.current = yosys.nextId++;
    yosys.state = 0;
    const files = { "compile.ys": UTF8ToString(script) };
    for (const name of JSON.parse(UTF8ToString(inputNames))) files[name] = FS.readFile(name, { encoding: "utf8" });
    yosys.worker.postMessage({ id: yosys.current, args: ["-q", "-s", "compile.ys"], files: files });
});
EM_JS(int, yosysJobState, (), { return Module.bitsimYosys.state; });   // 0 running, 1 done, 2 failed
EM_JS(double, yosysDownloaded, (), { return Module.bitsimYosys.done; });
EM_JS(double, yosysDownloadSize, (), { return Module.bitsimYosys.total; });
EM_JS(void, stopYosysJob, (), {
    Module.bitsimYosys.worker.terminate();
    Module.bitsimYosys.broken = true;
});

// A worker per dot run, ended when it reports, or to stop it
EM_JS(void, startDotJob, (int id, const char* graph, int fast), {
    Module.bitsimDot = Module.bitsimDot || new Map();
    const worker = new Worker(new URL("dot-worker.js", document.baseURI), { type: "module" });
    const job = { worker: worker, state: 0, text: "" };
    worker.onmessage = event => { job.state = event.data.ok ? 1 : 2; job.text = event.data.text; worker.terminate(); };
    worker.onerror = event => { job.state = 2; job.text = "dot could not run: " + (event.message || "the worker failed to start"); worker.terminate(); };
    Module.bitsimDot.set(id, job);
    const args = ["-Kdot", "-Tplain"];
    if (fast) args.push("-Gnslimit=0.1", "-Gmclimit=0.1");
    worker.postMessage({ graph: UTF8ToString(graph), args: args });
});
EM_JS(int, dotJobState, (int id), {   // 0 running, 1 done, 2 failed
    const job = Module.bitsimDot.get(id);
    return job ? job.state : 2;
});
EM_JS(char*, takeDotJobText, (int id), {   // the layout, or why it failed; the caller frees it
    const job = Module.bitsimDot.get(id);
    Module.bitsimDot.delete(id);
    return stringToNewUTF8(job ? job.text : "");
});
EM_JS(void, killDotJob, (int id), {
    const job = Module.bitsimDot.get(id);
    if (job) job.worker.terminate();
    Module.bitsimDot.delete(id);
});
#endif

// receiveCode / receiveFiles -> compile (sv2v, yosys, netlist) -> layoutDesign (dot) -> sendDesign

namespace {

// Ports, cells and instances of one module; cell types with no component go to `skipped`
NetModule readModule(const QString& name, const QJsonObject& module, const QSet<QString>& moduleNames, QStringList& skipped)
{
    // A net number from 2 up, or "1" (NetBit::One); "0", "x" and "z" are NetBit::Zero
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

    // The kinds dfflegalize leaves, e.g. $_DFFE_PP1P_: enable, asynchronous reset to 1
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

    // An input port drives its bits into the module; an output port reads them
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

    if (usesOne) {
        NetNode one;
        one.id = u"$const1"_s;
        one.type = ConstOne{};
        one.ports.append(NetPort{u"Y"_s, PortType::OUT, {NetBit::One}});
        result.nodes.append(one);
    }
    return result;
}

// bit -> (node, output pin) driving it
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

QList<int> inputBitsOf(const NetNode& node)
{
    const NodePins pins = nodePins(node);
    QList<int> bits = pins.inputs + pins.select;
    if (std::holds_alternative<FlipFlop>(node.type)) bits << pins.clock << pins.enable;
    return bits;
}

// One record per node (inputs left, outputs right), sized like its component; an edge per connection
QString dotGraphOf(const NetModule& module, bool topLevel)
{
    // Record labels escape {, }, |, <, >, quotes, backslashes and spaces
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

// When even fast dot is too slow: one column per logic level; flip-flops and blocks break loops
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

void centreInScene(NetModule& module)
{
    if (module.nodes.isEmpty()) return;
    double minX = std::numeric_limits<double>::max(), maxX = std::numeric_limits<double>::lowest();
    double minY = minX, maxY = maxX;
    for (const NetNode& node : module.nodes) {
        minX = std::min(minX, node.position.x()); maxX = std::max(maxX, node.position.x());
        minY = std::min(minY, node.position.y()); maxY = std::max(maxY, node.position.y());
    }
    const QPointF shift(SceneSize::width / 2 - (minX + maxX) / 2, SceneSize::height / 2 - (minY + maxY) / 2);
    for (NetNode& node : module.nodes) node.position += shift;
}

// Gates, muxes, flip-flops and blocks
qsizetype cellCount(const NetModule& module)
{
    return std::ranges::count_if(module.nodes, [](const NetNode& node) {
        return !std::holds_alternative<IOType>(node.type) && !std::holds_alternative<ConstOne>(node.type);
    });
}

// -Tplain: "node <name> <x> <y> ..." in inches, y up; the canvas uses points, y down
void applyPlainLayout(NetModule& module, const QString& plain)
{
    for (const QString& line : plain.split(u'\n')) {
        const QStringList parts = line.split(u' ', Qt::SkipEmptyParts);
        if (parts.size() < 4 || parts[0] != "node"_L1 || !parts[1].startsWith(u'n')) continue;
        bool ok = false;
        const int n = parts[1].mid(1).toInt(&ok);
        if (!ok || n < 0 || n >= module.nodes.size()) continue;
        module.nodes[n].position = QPointF(parts[2].toDouble() * 72.0, -parts[3].toDouble() * 72.0);
    }
}

constexpr int PollMs = 20;   // how often a running tool is checked

#ifdef Q_OS_WASM
// Waits without blocking the page: it keeps drawing and taking input while the workers run (Asyncify allows the nested loop)
void letThePageRun()
{
    QEventLoop loop;
    QTimer::singleShot(PollMs, &loop, &QEventLoop::quit);
    loop.exec();
}
#endif

// Keeps the hierarchy; dfflegalize leaves only the flip-flop kinds in FlipFlop
QString yosysScript(const QString& readVerilog)
{
    return readVerilog + u"\n"
        "synth -auto-top -noalumacc\n"
        "dfflegalize -cell $_DFF_P_ 0 -cell $_DFFE_PP_ 0 -cell $_DFF_PP?_ 0 -cell $_DFFE_PP?P_ 0 -cell $_SDFFCE_PP?P_ 0 -cell $_DLATCH_P_ 0\n"
        "abc -g AND,OR,XOR,NAND,NOR,XNOR,MUX\n"
        "opt_clean\n"
        "write_json code_netlist.json\n"_s;
}

}

//===================== Compiling ========================

void HDLCompiler::receiveCode(const QString& code)
{
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

#ifdef Q_OS_WASM
void HDLCompiler::preloadTools()
{
    ensureYosysWorker();
    warmUpYosys();
}
#endif

bool HDLCompiler::compile(const QStringList& files, const QStringList& includeDirs)
{
#if QT_CONFIG(process)
    // 0. ghdl: any VHDL to Verilog (vhdl.v)
    QStringList verilogFiles, vhdlFiles;
    for (const QString& file : files) (isVhdlFile(file) ? vhdlFiles : verilogFiles).append(file);
    if (!vhdlFiles.isEmpty()) {
        if (!convertVhdl(vhdlFiles, verilogFiles, u"vhdl.v"_s)) return false;
        verilogFiles.append(u"vhdl.v"_s);
    }

    // 1. sv2v: everything to plain Verilog; yosys's own reader misses packages, imports and more
    QStringList sv2vArguments;
    for (const QString& dir : includeDirs) sv2vArguments << u"--incdir="_s + dir;
    sv2vArguments << u"--write=converted.v"_s << verilogFiles;
    if (!runTool(u"sv2v"_s, sv2vPath, sv2vArguments, 60000)) return false;

    // 2. yosys
    if (!writeTextFile(u"compile.ys"_s, yosysScript(u"read_verilog converted.v"_s))) return false;
    if (!runTool(u"Yosys"_s, yosysPath, {u"-s"_s, u"compile.ys"_s}, YosysTimeoutMs)) return false;
#else
    // In the page: yosys in a Web Worker, and no ghdl or sv2v, so plain Verilog and what yosys's own reader takes
    Q_UNUSED(includeDirs);
    if (std::ranges::any_of(files, isVhdlFile)) {
        error(u"VHDL needs the desktop app: GHDL, which turns VHDL into Verilog, does not run in a web page. "
              "Download it from https://github.com/parsaes80/BitSimulator/releases"_s);
        return false;
    }
    QFile::remove(u"code_netlist.json"_s);   // never read one left by an earlier compile
    ensureYosysWorker();
    const QByteArray inputs = QJsonDocument(QJsonArray::fromStringList(files)).toJson(QJsonDocument::Compact);
    startYosysJob(yosysScript(u"read_verilog -sv "_s + files.join(u' ')).toUtf8().constData(), inputs.constData());
    QElapsedTimer timer;
    timer.start();
    while (yosysJobState() == 0) {
        // The first compile downloads yosys; that time doesn't count against the deadline
        if (yosysDownloaded() < yosysDownloadSize()) {
            progress(tr("Downloading Yosys: %1 of %2 MB").arg(int(yosysDownloaded() / 1e6)).arg(int(yosysDownloadSize() / 1e6)));
            timer.restart();
        } else {
            progress(tr("Running Yosys..."));
        }
        if (timer.elapsed() > YosysTimeoutMs) {
            stopYosysJob();
            error(u"Yosys took longer than %1 seconds and was stopped."_s.arg(YosysTimeoutMs / 1000));
            return false;
        }
        letThePageRun();
    }
    if (yosysJobState() != 1) {
        QFile log(u"yosys_log.txt"_s);
        error(log.open(QIODevice::ReadOnly) ? QString::fromUtf8(log.readAll()) : u"Yosys failed."_s);
        return false;
    }
    qDebug() << "Yosys took" << timer.elapsed() << "ms";
#endif

    // 3. The netlist, every module
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

    // 4. Layout
    if (!layoutDesign(design)) return false;
    sendDesign(design);
    return true;
}

#if QT_CONFIG(process)
// All dots at once against one deadline; one that runs out retries faster: full, fast, columns
bool HDLCompiler::layoutDesign(NetDesign& design)
{
    const auto cellsOf = cellCount;
    struct Job {
        NetModule* module = nullptr;
        bool topLevel = false;
        bool fast = false;
        QString graphFile, layoutFile;   // files: a pipe could fill up and stall dot
        std::unique_ptr<QProcess> dot;
        qint64 finishedMs = -1;          // from the start of its round; -1 while running
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

    std::vector<Job*> round;
    for (Job& job : jobs) round.push_back(&job);
    while (!round.empty()) {
        for (Job* job : round) {
            QStringList arguments{u"-Tplain"_s, u"-o"_s, job->layoutFile, job->graphFile};
            // Fast: fewer crossing-reduction and positioning passes; a little less tidy
            if (job->fast) arguments << u"-Gnslimit=0.1"_s << u"-Gmclimit=0.1"_s;
            QFile::remove(job->layoutFile);   // never read one left by an earlier compile
            job->dot = std::make_unique<QProcess>();
            job->finishedMs = -1;
            job->dot->start(dotPath, arguments);
        }

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
        for (Job* job : round) {   // stop the late ones first, so none outlives a failure below
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

            QFile layout(job->layoutFile);
            if (!layout.open(QIODevice::ReadOnly | QIODevice::Text)) {
                error("Could not read the layout of " + module.name + ": " + layout.errorString());
                return false;
            }
            applyPlainLayout(module, QString::fromUtf8(layout.readAll()));
            centreInScene(module);
        }
        round = std::move(next);
    }
    return true;
}

//===================== Running the tools ========================

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

    // VHDL-2008; -fsynopsys: std_logic_unsigned/arith; -frelaxed: older code; "-e" alone finds the top
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

    // Rename "<entity>_B<arch>" modules to the entity, unless it became several modules or is taken
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
        // Only used modules: the top keeps its declared name, which may contain "_B" ("reg_Bank")
        const QRegularExpression use(u"(?<![\\w$\\\\])"_s + QRegularExpression::escape(module) + u"(?![\\w$])"_s);
        if (verilog.count(use) < 2) continue;
        verilog.replace(use, entity);
        taken.insert(entity);
    }
    return writeTextFile(output, verilog);
}

#else
// The desktop's rounds with Web Workers for processes: all dots at once against one deadline, then fast, then columns
bool HDLCompiler::layoutDesign(NetDesign& design)
{
    struct Job {
        NetModule* module = nullptr;
        bool topLevel = false;
        bool fast = false;
        QByteArray graph;
        int id = 0;
        qint64 finishedMs = -1;          // from the start of its round; -1 while running
    };
    static int nextJobId = 1;
    std::vector<Job> jobs;
    jobs.reserve(design.modules.size());
    for (NetModule& module : design.modules) {
        Job& job = jobs.emplace_back();
        job.module = &module;
        job.topLevel = (module.name == design.top);
        job.fast = cellCount(module) >= FastDotCells;
        job.graph = dotGraphOf(module, job.topLevel).toUtf8();
    }

    std::vector<Job*> round;
    for (Job& job : jobs) round.push_back(&job);
    while (!round.empty()) {
        for (Job* job : round) {
            job->id = nextJobId++;
            job->finishedMs = -1;
            startDotJob(job->id, job->graph.constData(), job->fast);
        }
        progress(round.size() == 1 ? tr("Laying out 1 module...") : tr("Laying out %1 modules...").arg(round.size()));

        QElapsedTimer clock;
        clock.start();
        for (bool waiting = true; waiting && clock.elapsed() < DotTimeoutMs;) {
            waiting = false;
            for (Job* job : round) {
                if (job->finishedMs >= 0) continue;
                if (dotJobState(job->id) != 0) job->finishedMs = clock.elapsed();
                else waiting = true;
            }
            if (waiting) letThePageRun();
        }
        for (Job* job : round) {
            if (job->finishedMs < 0) killDotJob(job->id);
        }

        std::vector<Job*> next;
        for (qsizetype i = 0; i < qsizetype(round.size()); i++) {
            Job* job = round[i];
            NetModule& module = *job->module;
            const QString name = u"dot%1 for %2 (%3 cells)"_s
                                     .arg(job->fast ? u" (fast)"_s : QString(), module.name).arg(cellCount(module));
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
            const bool ok = dotJobState(job->id) == 1;
            char* text = takeDotJobText(job->id);
            const QString result = QString::fromUtf8(text);
            free(text);
            if (!ok) {
                for (qsizetype j = i + 1; j < qsizetype(round.size()); j++) killDotJob(round[j]->id);   // their results are no longer wanted
                error(name + u": "_s + result);
                return false;
            }
            qDebug() << name << "took" << job->finishedMs << "ms";
            applyPlainLayout(module, result);
            centreInScene(module);
        }
        round = std::move(next);
    }
    return true;
}
#endif

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
