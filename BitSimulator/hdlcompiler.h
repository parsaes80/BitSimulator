#pragma once
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <optional>
#include <variant>
#include "Items.h"   // BlockProps, componentSize
#include <QOperatingSystemVersion>

class QProcess;
// Relative to the app's folder; the build copies them there from third_party/
struct ToolPaths {
    QString yosys;   // Windows: OSS CAD Suite; Linux: yosys_bundle
    QString sv2v;
    QString dot;
    QString ghdl;    // Windows: third_party/ghdl (the OSS CAD Suite has none); Linux: yosys_bundle
};
inline const ToolPaths WindowsToolPaths{".\\oss-cad-suite\\bin\\yosys.exe", ".\\sv2v.exe", ".\\dot.exe",".\\ghdl\\bin\\ghdl.exe"};
inline const ToolPaths LinuxToolPaths{"./yosys_bundle/bin/yosys", "./sv2v", "./dot", "./yosys_bundle/bin/ghdl"};
inline const ToolPaths& toolPaths()
{
    return QOperatingSystemVersion::currentType() == QOperatingSystemVersion::Windows ? WindowsToolPaths : LinuxToolPaths;
}

// Bits: 0 = constant 0 (yosys "0", "x", "z"), 1 = constant 1, 2 and up = nets

namespace NetBit {
    inline constexpr int Zero = 0;
    inline constexpr int One = 1;
}

struct ModuleRef {
    QString module;
    bool operator==(const ModuleRef&) const = default;
};

// After dfflegalize: clock, enable and reset active-high, rising edge, powers up as 0. Ports D Q C E R.
struct FlipFlop {
    enum class Reset : std::uint8_t { None, Sync, Async };
    bool latch = false;             // follows D while E is 1 (no clock)
    Reset reset = Reset::None;      // Sync: R acts on the clock edge, Async: at once
    bool resetValue = false;        // what R sets Q to
    bool operator==(const FlipFlop&) const = default;
};

struct ConstOne {
    bool operator==(const ConstOne&) const = default;
};

struct NetPort {
    QString name;
    PortType type = PortType::IN;   // IN: the node reads these bits
    QList<int> bits;                // lowest bit first
};

struct NetNode {
    QString id;                     // a yosys cell, port or instance name
    std::variant<GType, MType, FlipFlop, IOType, ModuleRef, ConstOne> type;
    QList<NetPort> ports;           // by name, so pins keep their order
    QPointF position;

    const NetPort* port(const QString& name) const
    {
        const auto it = std::ranges::find(ports, name, &NetPort::name);
        return it == ports.end() ? nullptr : &*it;
    }
};

// A node's bits per canvas pin, in pin order; unconnected pins are bit 0
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
        pins.inputs = allBits(PortType::IN);
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
        // Ports, blocks and the constant: every bit, ports in name order
        pins.inputs = allBits(PortType::IN);
        pins.outputs = allBits(PortType::OUT);
    }
    return pins;
}

// Inputs left, outputs right; a port left unconnected (".q()") gets no pins
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

inline const QList<int>& portBits(const NetNode& node)
{
    static const QList<int> none;
    return node.ports.isEmpty() ? none : node.ports.first().bits;
}

//===================== VHDL ========================

inline bool isVhdlFile(const QString& path)
{
    using namespace Qt::StringLiterals;
    return path.endsWith(u".vhd"_s, Qt::CaseInsensitive) || path.endsWith(u".vhdl"_s, Qt::CaseInsensitive);
}

// VHDL declares an entity ("entity counter is"); Verilog has none
inline bool looksLikeVhdl(const QString& code)
{
    using namespace Qt::StringLiterals;
    static const QRegularExpression entity(u"^\\s*entity\\s+\\w+\\s+is\\b"_s,
                                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    return entity.match(code).hasMatch();
}

// ghdl names a used entity's module "<entity>_B<arch>[_<generic>]"; the top keeps its declared name
inline QString vhdlEntityOfModule(const QString& module)
{
    using namespace Qt::StringLiterals;
    const qsizetype marker = module.indexOf(u"_B"_s);
    return marker > 0 ? module.left(marker) : module;
}

// "rst"/"reset" as a word of the name, also active-low ("rst_n"); not "first" or "burst"
inline bool isResetName(const QString& name)
{
    using namespace Qt::StringLiterals;
    static const QRegularExpression reset(u"(^|_)[as]?(rst|reset)(n|_n|b|_b)?($|_)"_s,
                                          QRegularExpression::CaseInsensitiveOption);
    return reset.match(name).hasMatch();
}

// topLevel: ports become sources and displays; inside a block they are port tags, not components
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
    // 1-bit inputs toggle (a clock) but resets stay 0; wider ones count, 2 ticks per value
    if (isModulePort(node, IOType::IN)) {
        if (width == 1 && isResetName(node.id)) return SourceProps::constantOf(0, 1);
        return width == 1 ? SourceProps::waveform({false, true}) : SourceProps::counter(std::clamp(width, 1, SourceLimits::maxBits), 2);
    }
    return DisplayProps{std::max(width, 1), 0};
}

// The module port's name; empty for other nodes
inline QString portName(const NetNode& node)
{
    return std::holds_alternative<IOType>(node.type) ? node.id : QString();
}

// Its component's size, or its port tag's inside a block
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
    // Both find the top module themselves; editor text is VHDL if it declares an entity
    void receiveCode(const QString& code);
    // Absolute paths, Verilog/SystemVerilog/VHDL mixed; includeDirs are for `include
    void receiveFiles(const QStringList& files, const QStringList& includeDirs);
#ifdef Q_OS_WASM
    static void preloadTools();   // starts fetching yosys, so the first compile waits less
#endif

signals:
    void error(const QString& err);
    void finished(bool ok);   // once per receive call, after any error() or sendDesign()
    void sendDesign(const NetDesign& design);   // every module, laid out
    void progress(const QString& message);      // what the browser version is doing, for the status bar

private:
    // ghdl, sv2v, yosys, read the netlist, layout, sendDesign(); false after error()
    bool compile(const QStringList& files, const QStringList& includeDirs);
    // VHDL to one Verilog file; modules take the entity's name unless a Verilog module has it
    bool convertVhdl(const QStringList& vhdlFiles, const QStringList& verilogFiles, const QString& output);
    // One dot per module, all at once: as slow as the slowest module, not their sum
    bool layoutDesign(NetDesign& design);

    // Stdout to outputFile if given; on failure the tool's message goes to error() or failure
    bool runTool(const QString& name, const QString& program, const QStringList& arguments, int timeoutMs,
                 const QString& outputFile = {}, QString* failure = nullptr);
    bool checkFinished(QProcess& process, const QString& name, QString* failure = nullptr);
    bool writeTextFile(const QString& fileName, const QString& text);

    // From this many cells a module gets fast dot (RISC-V core: 1,215 cells, 57 s with full dot)
    static constexpr int FastDotCells = 2000;
    // Then dot is stopped and the next faster layout used; all modules share this time
    static constexpr int DotTimeoutMs = 120000;
    static constexpr int YosysTimeoutMs = 100000;
    QString yosysPath = toolPaths().yosys;
    QString sv2vPath = toolPaths().sv2v;
    QString dotPath = toolPaths().dot;
    QString ghdlPath = toolPaths().ghdl;
};


