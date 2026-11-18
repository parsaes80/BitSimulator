#pragma once
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

class QProcess;
#include <algorithm>
#include <optional>
#include <variant>
#include "Items.h"   // BlockProps and componentSize, for blocks and for dot's node sizes
#include <QOperatingSystemVersion>

// The external tools, relative to the app's folder (the build copies them there from third_party/)
struct ToolPaths {
    QString yosys;   // yosys for Windows: the OSS CAD Suite; for Linux: yosys_bundle
    QString sv2v;    // third_party/sv2v
    QString dot;     // Graphviz, built from third_party/graphviz
    QString ghdl;    // VHDL. For Windows: GHDL's own build in third_party/ghdl, since the OSS CAD Suite for Windows has none; for Linux: yosys_bundle
};
// Windows paths
inline const ToolPaths WindowsToolPaths{".\\oss-cad-suite\\bin\\yosys.exe", ".\\sv2v.exe", ".\\dot.exe",
                                        ".\\ghdl\\bin\\ghdl.exe"};
// Linux paths
inline const ToolPaths LinuxToolPaths{"./yosys_bundle/bin/yosys", "./sv2v", "./dot", "./yosys_bundle/bin/ghdl"};
// The paths for the system the app is running on
inline const ToolPaths& toolPaths()
{
    return QOperatingSystemVersion::currentType() == QOperatingSystemVersion::Windows ? WindowsToolPaths : LinuxToolPaths;
}

// A compiled design: the modules yosys produced, each a list of nodes wired together by numbered bits.
//
// Bit numbers: 0 is a constant 0 (yosys's "0", "x" and "z"), 1 is a constant 1, and 2 and up are nets
// (yosys numbers its nets from 2). Within a module, every node that uses the same bit is connected.

namespace NetBit {
inline constexpr int Zero = 0;
inline constexpr int One = 1;
}

// A submodule instance: drawn as a block that opens onto the module's own contents
struct ModuleRef {
    QString module;
    bool operator==(const ModuleRef&) const = default;
};

// A flip-flop or latch of the design. The compiler has yosys turn every kind of flip-flop into a few
// (dfflegalize): clock, enable and reset are active-high, a flip-flop changes on the rising clock edge,
// and its power-up value is 0. Ports: D, Q, C (clock), E (enable) and R (reset).
struct FlipFlop {
    enum class Reset : std::uint8_t { None, Sync, Async };
    bool latch = false;             // follows D while E is 1 (no clock)
    Reset reset = Reset::None;      // Sync: R acts on the clock edge, Async: at once
    bool resetValue = false;        // what R sets Q to
    bool operator==(const FlipFlop&) const = default;
};

// The constant 1 of a module, drawn as a 1-bit constant source
struct ConstOne {
    bool operator==(const ConstOne&) const = default;
};

struct NetPort {
    QString name;
    PortType type = PortType::IN;   // IN: the node reads these bits; OUT: the node drives them
    QList<int> bits;                // lowest bit first
};

struct NetNode {
    QString id;                     // unique within its module (a yosys cell name, port name or instance name)
    std::variant<GType, MType, FlipFlop, IOType, ModuleRef, ConstOne> type;
    QList<NetPort> ports;           // sorted by name, so pins come out in the same order every time
    QPointF position;               // canvas position, from the layout

    const NetPort* port(const QString& name) const
    {
        const auto it = std::ranges::find(ports, name, &NetPort::name);
        return it == ports.end() ? nullptr : &*it;
    }
};

// Which bits go to which pins of a node's canvas component, in pin order.
// Pins that are not connected are bit 0.
struct NodePins {
    QList<int> inputs;              // gate inputs; mux data inputs (A, B); register D; block / output port bits
    QList<int> select;              // mux select (S)
    int clock = 0;                  // register C
    int enable = 0;                 // register E
    int input2 = 0;                 // register R (reset)
    QList<int> outputs;             // gate Y; mux Y; register Q; block / input port bits; constant
};

inline NodePins nodePins(const NetNode& node)
{
    using namespace Qt::StringLiterals;
    NodePins pins;
    const auto bitOf = [&node](const QString& name) {
        const NetPort* port = node.port(name);
        return port && !port->bits.isEmpty() ? port->bits.first() : NetBit::Zero;
    };
    const auto allBits = [&node](PortType type) {
        QList<int> bits;
        for (const NetPort& port : node.ports) {
            if (port.type == type) bits += port.bits;
        }
        return bits;
    };

    if (std::holds_alternative<GType>(node.type)) {
        pins.inputs = allBits(PortType::IN);                  // A, B
        pins.outputs = {bitOf(u"Y"_s)};
    } else if (std::holds_alternative<MType>(node.type)) {
        pins.inputs = {bitOf(u"A"_s), bitOf(u"B"_s)};   // A when S = 0, B when S = 1
        pins.select = {bitOf(u"S"_s)};
        pins.outputs = {bitOf(u"Y"_s)};
    } else if (std::holds_alternative<FlipFlop>(node.type)) {
        pins.inputs = {bitOf(u"D"_s)};
        pins.clock = bitOf(u"C"_s);
        pins.enable = bitOf(u"E"_s);
        pins.input2 = bitOf(u"R"_s);
        pins.outputs = {bitOf(u"Q"_s)};
    } else {
        // Module ports, blocks and the constant: every bit of every port, ports in name order
        pins.inputs = allBits(PortType::IN);
        pins.outputs = allBits(PortType::OUT);
    }
    return pins;
}

// The block for a submodule instance: ports in name order, inputs on the left, outputs on the right.
// A port left unconnected in the instance (".q()") has no bits here: it gets no pins and no label.
inline BlockProps blockProps(const NetNode& node, const ModuleRef& ref)
{
    BlockProps props{node.id, ref.module, {}};
    for (const NetPort& port : node.ports) {
        if (!port.bits.isEmpty()) props.ports.append(BlockPort{port.name, port.type == PortType::OUT, int(port.bits.size())});
    }
    return props;
}

inline bool isModulePort(const NetNode& node, IOType direction)
{
    const auto* io = std::get_if<IOType>(&node.type);
    return io && *io == direction;
}

// The bits of a module port node (its only port)
inline const QList<int>& portBits(const NetNode& node)
{
    static const QList<int> none;
    return node.ports.isEmpty() ? none : node.ports.first().bits;
}

//===================== VHDL ========================

// A VHDL file, by its extension: .vhd or .vhdl, in any case
inline bool isVhdlFile(const QString& path)
{
    using namespace Qt::StringLiterals;
    return path.endsWith(u".vhd"_s, Qt::CaseInsensitive) || path.endsWith(u".vhdl"_s, Qt::CaseInsensitive);
}

// Whether the editor's text is VHDL: it declares an entity ("entity counter is"). Verilog has no entities.
inline bool looksLikeVhdl(const QString& code)
{
    using namespace Qt::StringLiterals;
    static const QRegularExpression entity(u"^\\s*entity\\s+\\w+\\s+is\\b"_s,
                                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    return entity.match(code).hasMatch();
}

// The entity a module in ghdl's Verilog was made from. ghdl names the module of a used entity
// "<entity>_B<architecture>" in lower case but for the B, plus "_<value>" per generic: "add_n_Bbehav_4".
// Only for those: the top entity's module has the entity's name as it was declared, which may contain "_B".
inline QString vhdlEntityOfModule(const QString& module)
{
    using namespace Qt::StringLiterals;
    const qsizetype marker = module.indexOf(u"_B"_s);
    return marker > 0 ? module.left(marker) : module;
}

// Whether a port name says it is a reset: "rst" or "reset" as a word of the name ("rst", "sys_reset",
// "arst"), also active-low ("rst_n", "resetn", "rst_b"). Not "first" or "burst".
inline bool isResetName(const QString& name)
{
    using namespace Qt::StringLiterals;
    static const QRegularExpression reset(u"(^|_)[as]?(rst|reset)(n|_n|b|_b)?($|_)"_s,
                                          QRegularExpression::CaseInsensitiveOption);
    return reset.match(name).hasMatch();
}

// The component a node becomes on the canvas. topLevel: the module's ports become sources and
// displays (named after the port, see portName); inside a block they are port tags, which are not
// components, so there is nothing for them.
inline std::optional<ItemProps> nodeProps(const NetNode& node, bool topLevel)
{
    const NodePins pins = nodePins(node);
    if (const auto* gate = std::get_if<GType>(&node.type)) return normalized(GateProps{*gate, int(pins.inputs.size())});
    if (std::holds_alternative<MType>(node.type)) return MuxProps{MType::MUX, 2};
    if (const auto* ff = std::get_if<FlipFlop>(&node.type)) {
        return RegisterProps{RType::D, !ff->latch, true, ff->reset == FlipFlop::Reset::Async, ff->resetValue};
    }
    if (const auto* ref = std::get_if<ModuleRef>(&node.type)) return blockProps(node, *ref);
    if (std::holds_alternative<ConstOne>(node.type)) return SourceProps::constantOf(1, 1);
    if (!topLevel) return std::nullopt;
    const int width = int(portBits(node).size());
    // A 1-bit input toggles every tick (a clock for registers), except a reset, which stays 0: one
    // toggling with the clock would reset the circuit on every clock edge. A wider input counts
    // 0, 1, 2, ... holding each value for 2 ticks, so a register clocked by a 1-bit input sees every
    // value; the counter wraps at the input's width.
    if (isModulePort(node, IOType::IN)) {
        if (width == 1 && isResetName(node.id)) return SourceProps::constantOf(0, 1);
        return width == 1 ? SourceProps::waveform({false, true}) : SourceProps::counter(std::clamp(width, 1, SourceLimits::maxBits), 2);
    }
    return DisplayProps{std::max(width, 1), 0};
}

// The name a source or display shows: the module port's name; nothing for other nodes
inline QString portName(const NetNode& node)
{
    return std::holds_alternative<IOType>(node.type) ? node.id : QString();
}

// The size of a node's component on the canvas, or of its port tag inside a block
inline QSizeF nodeSize(const NetNode& node, bool topLevel)
{
    if (const auto props = nodeProps(node, topLevel)) return componentSize(*props, portName(node));
    return terminalSize(node.id, int(portBits(node).size()));
}

struct NetModule {
    QString name;
    QList<NetNode> nodes;
};

struct NetDesign {
    QString top;                    // the module no other module uses
    QHash<QString, NetModule> modules;

    bool isEmpty() const { return modules.isEmpty(); }
    const NetModule* module(const QString& name) const
    {
        const auto it = modules.constFind(name);
        return it == modules.constEnd() ? nullptr : &*it;
    }
};

class HDLCompiler : public QObject{
    Q_OBJECT
public:
    explicit HDLCompiler(QObject* parent = nullptr) : QObject(parent) {}

public:
    // Both find the top module automatically: the module no other module uses.
    // The HDL editor's text: VHDL if it declares an entity (see looksLikeVhdl), otherwise Verilog
    void receiveCode(const QString& code);
    // Verilog, SystemVerilog and VHDL files (absolute paths), mixed if need be (see isVhdlFile).
    // `includeDirs` are searched for Verilog `include files besides each file's own folder.
    void receiveFiles(const QStringList& files, const QStringList& includeDirs);

signals:
    void error(const QString& err);
    void finished(bool ok);   // emitted once per receiveCode() / receiveFiles(), after any error() or sendDesign()
    void sendDesign(const NetDesign& design);   // every module, laid out; the canvas shows the top one

private:
    // The steps after the code or files are in place: ghdl for any VHDL, sv2v, yosys, reading the
    // netlist, layout, then sendDesign(). False after error() said what went wrong.
    bool compile(const QStringList& files, const QStringList& includeDirs);
    // ghdl turns the VHDL files into one Verilog file, `output`: a module per entity, named like the
    // entity where it can be. `verilogFiles` are the design's other files, whose module names win.
    bool convertVhdl(const QStringList& vhdlFiles, const QStringList& verilogFiles, const QString& output);
    // Lays every module out at once: one dot process per module, all started together, so a design
    // takes as long as its slowest module instead of all of them one after another
    bool layoutDesign(NetDesign& design);

    // Runs a tool and waits for it. Its standard output goes to `outputFile`, if given. If it fails, the
    // message (the tool's own, with file and line) goes to error(), or to `failure` if that is given.
    bool runTool(const QString& name, const QString& program, const QStringList& arguments, int timeoutMs,
                 const QString& outputFile = {}, QString* failure = nullptr);
    // A finished tool succeeded; if not, its message goes to error(), or to `failure` if that is given
    bool checkFinished(QProcess& process, const QString& name, QString* failure = nullptr);
    bool writeTextFile(const QString& fileName, const QString& text);   // error() if it cannot

    // A module with fewer cells (gates, multiplexers, flip-flops, blocks) gets dot's full layout; a
    // bigger one a fast dot, which stops dot's two slowest steps early. Measured on a RISC-V: the
    // 1,215-cell core takes 57 s at full quality, the 8,062-cell register file 21 s fast.
    static constexpr int FastDotCells = 2000;
    // Longer than this, dot is stopped and the next faster layout used: fast dot, then columns.
    // All modules share the time: they run at the same time.
    static constexpr int DotTimeoutMs = 120000;
    // The external tools for this system (see ToolPaths above)
    QString yosysPath = toolPaths().yosys;
    QString sv2vPath = toolPaths().sv2v;
    QString dotPath = toolPaths().dot;
    QString ghdlPath = toolPaths().ghdl;
};


