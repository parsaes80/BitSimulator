#include "CircuitFile.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <optional>

using namespace Qt::StringLiterals;

QList<PortItem*> componentPins(ComponentItem* item)
{
    if (auto* gate = dynamic_cast<GateItem*>(item)) return gate->getInputPorts() << gate->getOutputPort();
    if (auto* mux = dynamic_cast<MuxItem*>(item)) return mux->getInputPorts() + mux->getAddressPorts() << mux->getOutputPort();
    if (auto* reg = dynamic_cast<RegisterItem*>(item))
        return {reg->getInputPort(), reg->getInputPortTwo(), reg->getClkPort(), reg->getReadEnablePort(), reg->getOutputPort()};
    if (auto* source = dynamic_cast<SourceItem*>(item)) return source->getOutputPorts();
    if (auto* display = dynamic_cast<DisplayItem*>(item)) return display->getInputPorts() + display->getOutputPorts();
    if (auto* block = dynamic_cast<BlockItem*>(item)) return block->getInputPorts() + block->getOutputPorts();
    return {};
}

namespace {

// Enums by name, indexed by value, so files stay readable and survive reordered enums
constexpr const char* GateNames[] = {"NOT", "AND", "OR", "XOR", "NAND", "NOR", "XNOR"};
constexpr const char* RegisterNames[] = {"SR", "JK", "D", "T"};
constexpr const char* MuxNames[] = {"MUX", "DEMUX"};
constexpr const char* ModeNames[] = {"counter", "grayCode", "walkingOne", "random", "list", "waveforms", "constant"};
constexpr const char* ResetNames[] = {"none", "sync", "async"};

template <std::size_t N>
QString nameOf(const char* const (&names)[N], int value) { return QString::fromLatin1(names[value]); }

template <typename Enum, std::size_t N>
std::optional<Enum> enumOf(const char* const (&names)[N], const QJsonValue& value)
{
    for (std::size_t i = 0; i < N; i++) {
        if (value.toString() == QLatin1StringView(names[i])) return Enum(i);
    }
    return std::nullopt;
}

// u64 as decimal text: JSON numbers are doubles and lose bits past 2^53
QJsonValue u64Json(std::uint64_t value) { return QString::number(value); }
std::uint64_t u64Of(const QJsonValue& value) { return value.toString().toULongLong(); }

QJsonArray intsJson(const QList<int>& values)
{
    QJsonArray array;
    for (int value : values) array.append(value);
    return array;
}

QList<int> intsOf(const QJsonValue& value)
{
    QList<int> result;
    for (const QJsonValue& item : value.toArray()) result.append(item.toInt());
    return result;
}

//===================== Components ========================

QJsonObject propsJson(const ItemProps& props)
{
    QJsonObject json;
    std::visit([&json](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, GateProps>) {
            json["kind"_L1] = "gate"_L1;
            json["type"_L1] = nameOf(GateNames, int(p.type));
            json["inputs"_L1] = p.numInputs;
        } else if constexpr (std::is_same_v<T, RegisterProps>) {
            json["kind"_L1] = "register"_L1;
            json["type"_L1] = nameOf(RegisterNames, int(p.type));
            json["flipFlop"_L1] = p.edgeTriggered;
            json["enable"_L1] = p.hasEnable;
            json["asyncReset"_L1] = p.asyncReset;
            json["resetValue"_L1] = p.resetValue;
        } else if constexpr (std::is_same_v<T, MuxProps>) {
            json["kind"_L1] = "mux"_L1;
            json["type"_L1] = nameOf(MuxNames, int(p.type));
            json["inputs"_L1] = p.numInputs;
        } else if constexpr (std::is_same_v<T, SourceProps>) {
            json["kind"_L1] = "source"_L1;
            json["mode"_L1] = nameOf(ModeNames, int(p.mode));
            json["bits"_L1] = p.bits;
            json["ticksPerStep"_L1] = p.ticksPerStep;
            json["countDown"_L1] = p.countDown;
            json["start"_L1] = u64Json(p.start);
            json["step"_L1] = u64Json(p.step);
            json["seed"_L1] = qint64(p.seed);
            QJsonArray values;
            for (std::uint64_t value : p.values) values.append(u64Json(value));
            json["values"_L1] = values;
            QJsonArray waves;   // one "0110" string per pin
            for (const QList<bool>& wave : p.waves) {
                QString steps;
                for (bool high : wave) steps += high ? u'1' : u'0';
                waves.append(steps);
            }
            json["waves"_L1] = waves;
            json["constant"_L1] = u64Json(p.constant);
            json["repeat"_L1] = p.repeat;
        } else if constexpr (std::is_same_v<T, DisplayProps>) {
            json["kind"_L1] = "display"_L1;
            json["inputs"_L1] = p.numInputs;
            json["outputs"_L1] = p.numOutputs;
        } else {
            json["kind"_L1] = "block"_L1;
            json["instance"_L1] = p.instance;
            json["module"_L1] = p.module;
            QJsonArray ports;
            for (const BlockPort& port : p.ports)
                ports.append(QJsonObject{{"name"_L1, port.name}, {"output"_L1, port.output}, {"width"_L1, port.width}});
            json["ports"_L1] = ports;
        }
    }, props);
    return json;
}

std::optional<ItemProps> propsOf(const QJsonObject& json)
{
    const QString kind = json["kind"_L1].toString();
    if (kind == "gate"_L1) {
        const auto type = enumOf<GType>(GateNames, json["type"_L1]);
        if (!type) return std::nullopt;
        return normalized(GateProps{*type, json["inputs"_L1].toInt(2)});
    }
    if (kind == "register"_L1) {
        const auto type = enumOf<RType>(RegisterNames, json["type"_L1]);
        if (!type) return std::nullopt;
        RegisterProps p;
        p.type = *type;
        p.edgeTriggered = json["flipFlop"_L1].toBool(true);
        p.hasEnable = json["enable"_L1].toBool(true);
        p.asyncReset = json["asyncReset"_L1].toBool();
        p.resetValue = json["resetValue"_L1].toBool();
        return normalized(p);
    }
    if (kind == "mux"_L1) {
        const auto type = enumOf<MType>(MuxNames, json["type"_L1]);
        if (!type) return std::nullopt;
        return normalized(MuxProps{*type, json["inputs"_L1].toInt(2)});
    }
    if (kind == "source"_L1) {
        const auto mode = enumOf<SourceMode>(ModeNames, json["mode"_L1]);
        if (!mode) return std::nullopt;
        SourceProps p;
        p.mode = *mode;
        p.bits = json["bits"_L1].toInt(p.bits);
        p.ticksPerStep = json["ticksPerStep"_L1].toInt(p.ticksPerStep);
        p.countDown = json["countDown"_L1].toBool();
        p.start = u64Of(json["start"_L1]);
        p.step = u64Of(json["step"_L1]);
        p.seed = std::uint32_t(json["seed"_L1].toInteger(1));
        p.values.clear();
        for (const QJsonValue& value : json["values"_L1].toArray()) p.values.append(u64Of(value));
        p.waves.clear();
        for (const QJsonValue& wave : json["waves"_L1].toArray()) {
            QList<bool> steps;
            for (QChar c : wave.toString()) steps.append(c == u'1');
            p.waves.append(steps);
        }
        p.constant = u64Of(json["constant"_L1]);
        p.repeat = json["repeat"_L1].toBool(true);
        return normalized(p);
    }
    if (kind == "display"_L1) return normalized(DisplayProps{json["inputs"_L1].toInt(2), json["outputs"_L1].toInt(2)});
    if (kind == "block"_L1) {
        BlockProps p;
        p.instance = json["instance"_L1].toString();
        p.module = json["module"_L1].toString();
        for (const QJsonValue& value : json["ports"_L1].toArray()) {
            const QJsonObject port = value.toObject();
            p.ports.append({port["name"_L1].toString(), port["output"_L1].toBool(), std::max(1, port["width"_L1].toInt(1))});
        }
        return p;
    }
    return std::nullopt;
}

//===================== Compiled designs ========================

QJsonObject nodeTypeJson(const NetNode& node)
{
    return std::visit([](const auto& type) -> QJsonObject {
        using T = std::decay_t<decltype(type)>;
        if constexpr (std::is_same_v<T, GType>) return {{"gate"_L1, nameOf(GateNames, int(type))}};
        else if constexpr (std::is_same_v<T, MType>) return {{"mux"_L1, nameOf(MuxNames, int(type))}};
        else if constexpr (std::is_same_v<T, FlipFlop>)
            return {{"flipFlop"_L1, QJsonObject{{"latch"_L1, type.latch}, {"reset"_L1, nameOf(ResetNames, int(type.reset))},
                                                 {"resetValue"_L1, type.resetValue}}}};
        else if constexpr (std::is_same_v<T, IOType>) return {{"port"_L1, type == IOType::IN ? "in"_L1 : "out"_L1}};
        else if constexpr (std::is_same_v<T, ModuleRef>) return {{"instance"_L1, type.module}};
        else return {{"one"_L1, true}};
    }, node.type);
}

std::optional<decltype(NetNode::type)> nodeTypeOf(const QJsonObject& json)
{
    if (json.contains("gate"_L1)) {
        if (const auto type = enumOf<GType>(GateNames, json["gate"_L1])) return *type;
    } else if (json.contains("mux"_L1)) {
        if (const auto type = enumOf<MType>(MuxNames, json["mux"_L1])) return *type;
    } else if (json.contains("flipFlop"_L1)) {
        const QJsonObject ff = json["flipFlop"_L1].toObject();
        if (const auto reset = enumOf<FlipFlop::Reset>(ResetNames, ff["reset"_L1]))
            return FlipFlop{ff["latch"_L1].toBool(), *reset, ff["resetValue"_L1].toBool()};
    } else if (json.contains("port"_L1)) {
        return json["port"_L1].toString() == "in"_L1 ? IOType::IN : IOType::OUT;
    } else if (json.contains("instance"_L1)) {
        return ModuleRef{json["instance"_L1].toString()};
    } else if (json.contains("one"_L1)) {
        return ConstOne{};
    }
    return std::nullopt;
}

QJsonObject designJson(const NetDesign& design)
{
    QJsonArray modules;
    QStringList names = design.modules.keys();
    names.sort();   // stable files: QHash order varies
    for (const QString& name : std::as_const(names)) {
        QJsonArray nodes;
        for (const NetNode& node : design.modules[name].nodes) {
            QJsonArray ports;
            for (const NetPort& port : node.ports)
                ports.append(QJsonObject{{"name"_L1, port.name}, {"out"_L1, port.type == PortType::OUT}, {"bits"_L1, intsJson(port.bits)}});
            nodes.append(QJsonObject{{"id"_L1, node.id}, {"type"_L1, nodeTypeJson(node)}, {"ports"_L1, ports},
                                     {"x"_L1, node.position.x()}, {"y"_L1, node.position.y()}});
        }
        modules.append(QJsonObject{{"name"_L1, name}, {"nodes"_L1, nodes}});
    }
    return {{"top"_L1, design.top}, {"modules"_L1, modules}};
}

std::expected<NetDesign, QString> designOf(const QJsonObject& json)
{
    NetDesign design;
    design.top = json["top"_L1].toString();
    for (const QJsonValue& moduleValue : json["modules"_L1].toArray()) {
        const QJsonObject moduleJson = moduleValue.toObject();
        NetModule module;
        module.name = moduleJson["name"_L1].toString();
        for (const QJsonValue& nodeValue : moduleJson["nodes"_L1].toArray()) {
            const QJsonObject nodeJson = nodeValue.toObject();
            const auto type = nodeTypeOf(nodeJson["type"_L1].toObject());
            if (!type) return std::unexpected(QObject::tr("module %1 has a node of an unknown kind").arg(module.name));
            NetNode node{nodeJson["id"_L1].toString(), *type, {}, {nodeJson["x"_L1].toDouble(), nodeJson["y"_L1].toDouble()}};
            for (const QJsonValue& portValue : nodeJson["ports"_L1].toArray()) {
                const QJsonObject port = portValue.toObject();
                node.ports.append({port["name"_L1].toString(), port["out"_L1].toBool() ? PortType::OUT : PortType::IN, intsOf(port["bits"_L1])});
            }
            module.nodes.append(node);
        }
        design.modules.insert(module.name, module);
    }
    if (!design.isEmpty() && !design.module(design.top)) return std::unexpected(QObject::tr("its top module is missing"));
    return design;
}

} // namespace

//===================== The file ========================

QByteArray circuitToJson(const CircuitData& data)
{
    QJsonArray components;
    for (const SavedComponent& component : data.components) {
        QJsonObject json = propsJson(component.props);
        json["x"_L1] = component.position.x();
        json["y"_L1] = component.position.y();
        if (!component.name.isEmpty()) json["name"_L1] = component.name;
        components.append(json);
    }
    QJsonArray wires;
    for (const SavedWire& wire : data.wires)
        wires.append(QJsonObject{{"from"_L1, QJsonArray{wire.from, wire.fromPin}}, {"to"_L1, QJsonArray{wire.to, wire.toPin}}});

    QJsonObject root{{"app"_L1, "BitSimulator"_L1}, {"version"_L1, CircuitFileVersion},
                     {"components"_L1, components}, {"wires"_L1, wires}};
    if (!data.design.isEmpty()) root["design"_L1] = designJson(data.design);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::expected<CircuitData, QString> circuitFromJson(const QByteArray& json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (document.isNull()) return std::unexpected(QObject::tr("It is not valid JSON: %1.").arg(parseError.errorString()));
    const QJsonObject root = document.object();
    if (root["app"_L1].toString() != "BitSimulator"_L1) return std::unexpected(QObject::tr("It is not a BitSimulator circuit."));
    const int version = root["version"_L1].toInt();
    if (version > CircuitFileVersion)
        return std::unexpected(QObject::tr("It was saved by a newer BitSimulator (file version %1).").arg(version));

    CircuitData data;
    if (root.contains("design"_L1)) {
        auto design = designOf(root["design"_L1].toObject());
        if (!design) return std::unexpected(QObject::tr("Its compiled design is damaged: %1.").arg(design.error()));
        data.design = std::move(*design);
    }
    const QJsonArray components = root["components"_L1].toArray();
    for (qsizetype i = 0; i < components.size(); i++) {
        const QJsonObject json = components[i].toObject();
        const auto props = propsOf(json);
        if (!props) return std::unexpected(QObject::tr("Component %1 is of an unknown kind.").arg(i + 1));
        data.components.append({*props, {json["x"_L1].toDouble(), json["y"_L1].toDouble()}, json["name"_L1].toString()});
    }
    for (const QJsonValue& value : root["wires"_L1].toArray()) {
        const QJsonArray from = value["from"_L1].toArray(), to = value["to"_L1].toArray();
        data.wires.append({from.at(0).toInt(-1), from.at(1).toInt(-1), to.at(0).toInt(-1), to.at(1).toInt(-1)});
    }
    return data;
}
