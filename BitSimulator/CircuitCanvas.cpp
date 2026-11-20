#include "CircuitCanvas.h"
#include <QApplication>
#include <QCollator>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QToolButton>
#include <QWheelEvent>
#include <cmath>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;

//===================== QGraphicsScene ========================

CircuitScene::CircuitScene(QObject* parent)
    : QGraphicsScene(parent)
{
    setSceneRect(0, 0, 160000, 100000);

    m_frameTimer.setSingleShot(true);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_frameTimer, &QTimer::timeout, this, &CircuitScene::applyPendingResult);
}

void CircuitScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    const Theme& theme = currentTheme();
    painter->fillRect(rect, theme.canvas);

    constexpr qreal DotSpacing = 20;     // scene units
    constexpr qreal MinDotGap = 12;      // screen pixels; zoomed out, only every 2nd, 4th, ... dot
    constexpr qreal DotSize = 2;         // screen pixels

    qreal spacing = DotSpacing;
    const qreal pixelsPerUnit = painter->worldTransform().m11();
    if (pixelsPerUnit <= 0) return;
    while (spacing * pixelsPerUnit < MinDotGap) spacing *= 2;

    // Include dots just outside `rect`, or a partial redraw would cut those on its edges
    const qreal reach = DotSize / pixelsPerUnit;
    const QRectF area = rect.adjusted(-reach, -reach, reach, reach);

    QList<QPointF> dots;
    const qreal firstX = std::ceil(area.left() / spacing) * spacing;
    const qreal firstY = std::ceil(area.top() / spacing) * spacing;
    dots.reserve(qsizetype((area.width() / spacing + 1) * (area.height() / spacing + 1)));
    for (qreal y = firstY; y <= area.bottom(); y += spacing) {
        for (qreal x = firstX; x <= area.right(); x += spacing) dots.append(QPointF(x, y));
    }

    QPen pen(theme.dot, DotSize, Qt::SolidLine, Qt::RoundCap);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->drawPoints(dots.constData(), int(dots.size()));
}

namespace {
ComponentItem* createComponent(const ItemProps& props)
{
    return std::visit([](const auto& p) -> ComponentItem* {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, GateProps>)          return new GateItem(p);
        else if constexpr (std::is_same_v<T, RegisterProps>) return new RegisterItem(p);
        else if constexpr (std::is_same_v<T, MuxProps>)      return new MuxItem(p);
        else if constexpr (std::is_same_v<T, SourceProps>)   return new SourceItem(p);
        else if constexpr (std::is_same_v<T, DisplayProps>)  return new DisplayItem(p);
        else                                                 return new BlockItem(p);
    }, props);
}
}

CircuitScene::~CircuitScene()
{
    // ~QGraphicsScene may emit selectionChanged into listeners after our members are gone
    disconnect(this, &QGraphicsScene::selectionChanged, nullptr, nullptr);
}

ComponentItem* CircuitScene::addComponent(const ItemProps& props, QPointF position)
{
    if (isLocked() || m_readOnly) return nullptr;

    ComponentItem* item = createComponent(props);
    if (auto* source = dynamic_cast<SourceItem*>(item)) source->setName(automaticName(item));
    else if (auto* display = dynamic_cast<DisplayItem*>(item)) display->setName(automaticName(item));
    item->setPos(position);
    addItem(item);
    watchMoves(item);
    circuitEdited();
    return item;
}

ComponentItem::EditResult CircuitScene::renameComponent(ComponentItem* item, const QString& name)
{
    if (m_readOnly) return std::unexpected(tr("The inside of a block cannot be edited."));
    if (isLocked()) return std::unexpected(tr("Pause the simulation to edit the circuit."));

    const QString given = name.simplified();
    if (auto* source = dynamic_cast<SourceItem*>(item)) {
        if (given == source->name()) return {};
        source->setName(given.isEmpty() ? automaticName(item) : given);
    } else if (auto* display = dynamic_cast<DisplayItem*>(item)) {
        if (given == display->name()) return {};
        display->setName(given.isEmpty() ? automaticName(item) : given);
    } else {
        return std::unexpected(tr("Only sources and displays have a name."));
    }
    circuitEdited();
    return {};
}

QString CircuitScene::automaticName(const ComponentItem* item) const
{
    const bool isSource = dynamic_cast<const SourceItem*>(item);
    if (!isSource && !dynamic_cast<const DisplayItem*>(item)) return {};
    const QString stem = isSource ? tr("Source") : tr("Display");

    // The lowest number no other component of the kind has
    QSet<int> taken;
    const QString prefix = stem + u' ';
    for (QGraphicsItem* other : items()) {
        if (other == item) continue;
        QString otherName;
        if (const auto* source = dynamic_cast<const SourceItem*>(other); source && isSource) otherName = source->name();
        else if (const auto* display = dynamic_cast<const DisplayItem*>(other); display && !isSource) otherName = display->name();
        if (!otherName.startsWith(prefix)) continue;
        bool ok = false;
        const int number = otherName.mid(prefix.size()).toInt(&ok);
        if (ok) taken.insert(number);
    }
    int number = 1;
    while (taken.contains(number)) number++;
    return prefix + QString::number(number);
}

ComponentItem::EditResult CircuitScene::editComponent(ComponentItem* item, const ItemProps& props)
{
    if (m_readOnly) return std::unexpected(tr("The inside of a block cannot be edited."));
    if (isLocked()) return std::unexpected(tr("Pause the simulation to edit the circuit."));

    auto result = item->setProps(props);
    if (result) circuitEdited();
    return result;
}

QList<ComponentItem*> CircuitScene::findInputsAndOutputs(const QString& pattern) const
{
    QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid()) regex.setPattern(QRegularExpression::escape(pattern));   // e.g. "a[" while still typing
    QList<std::pair<ComponentItem*, QString>> found;
    for (QGraphicsItem* item : items()) {
        QString name;
        if (auto* source = dynamic_cast<SourceItem*>(item)) name = source->name();
        else if (auto* display = dynamic_cast<DisplayItem*>(item)) name = display->name();
        else continue;
        if (regex.match(name).hasMatch()) found.append({static_cast<ComponentItem*>(item), name});
    }
    QCollator collator;
    collator.setNumericMode(true);
    std::ranges::stable_sort(found, [&collator](const auto& a, const auto& b) {
        const bool aIn = dynamic_cast<SourceItem*>(a.first), bIn = dynamic_cast<SourceItem*>(b.first);
        if (aIn != bIn) return aIn;
        return collator.compare(a.second, b.second) < 0;
    });
    QList<ComponentItem*> result;
    for (const auto& [item, name] : found) result.append(item);
    return result;
}

void CircuitScene::deleteSelection()
{
    if (isLocked() || m_readOnly) return;
    cancelWireConnection();   // a dragged wire must not end at a pin that goes away

    const QList<QGraphicsItem*> selected = selectedItems();
    bool removedAny = false;

    // Wires first, so a component selected with all its wires goes too
    for (QGraphicsItem* item : selected) {
        if (auto* wire = dynamic_cast<WireItem*>(item)) {
            wire->setStartPort(nullptr);
            wire->setEndPort(nullptr);
            removeItem(wire);
            wire->deleteLater();
            removedAny = true;
        }
    }
    for (QGraphicsItem* item : selected) {
        if (auto* component = dynamic_cast<ComponentItem*>(item); component && !component->hasConnections()) {
            removeItem(component);
            component->deleteLater();
            removedAny = true;
        }
    }

    if (removedAny) circuitEdited();
}

void CircuitScene::resetCircuit()
{
    stopSim();
    cancelWireConnection();
    clear();
    m_wireMap.clear();
    m_displays.clear();
    m_sources.clear();
    m_lastNetValues.clear();
    m_wireBits.clear();
    m_registerNodes.clear();
    m_design = {};
    designChanged();
}

//===================== Simulation state ========================

void CircuitScene::setSimState(SimState state)
{
    if (m_simState == state) return;
    m_simState = state;
    sim_values_visible = (state != SimState::Stopped);
    update();
    simStateChanged(state);
}

void CircuitScene::startOrResumeSim()
{
    if (m_simState == SimState::Running) return;

    if (m_simState == SimState::Paused) {
        // Nothing was edited since Pause (an edit ends the run)
        cancelWireConnection();
        setSimState(SimState::Running);
        setSimRunningSIG(true);
        return;
    }

    cancelWireConnection();
    ExportGraph graph = buildGraph();
    graph.runId = ++m_runId;
    setSimState(SimState::Running);
    startSimSIG(graph);
}

void CircuitScene::pauseSim()
{
    if (m_simState != SimState::Running) return;
    setSimState(SimState::Paused);
    setSimRunningSIG(false);
}

void CircuitScene::stopSim()
{
    if (m_simState == SimState::Stopped) return;

    ++m_runId;                 // results still on their way are ignored
    stopSimSIG();
    m_wireMap.clear();
    m_displays.clear();
    m_sources.clear();
    m_lastNetValues.clear();
    m_lastRegisterValues.clear();
    m_instanceNets.clear();
    m_instanceRegisters.clear();
    resetValues();
    setSimState(SimState::Stopped);
}

void CircuitScene::circuitEdited()
{
    if (m_simState == SimState::Paused) stopSim();
    circuitModified();
}

void CircuitScene::watchMoves(QGraphicsObject* item)
{
    connect(item, &QGraphicsObject::xChanged, this, &CircuitScene::circuitModified);
    connect(item, &QGraphicsObject::yChanged, this, &CircuitScene::circuitModified);
}

void CircuitScene::cancelWireConnection()
{
    if (m_currWire) {
        removeItem(m_currWire);
        delete m_currWire;
    }
    clearHighlights();
    m_connectingWire = false;
    m_currWire = nullptr;
    m_currWireStartPort = nullptr;
}

void CircuitScene::resetValues()
{
    QList<DisplayItem*> displays;
    for (QGraphicsItem* item : items()) {
        if (auto* port = dynamic_cast<PortItem*>(item)) port->setValue(false);
        else if (auto* wire = dynamic_cast<WireItem*>(item)) wire->setValue(false);
        else if (auto* reg = dynamic_cast<RegisterItem*>(item)) reg->setValue(false);
        else if (auto* source = dynamic_cast<SourceItem*>(item)) source->setTick(0);
        else if (auto* display = dynamic_cast<DisplayItem*>(item)) displays.append(display);
    }
    for (DisplayItem* display : std::as_const(displays)) display->updateValue();   // after their pins were cleared
}

void CircuitScene::setToolProps(const ItemProps& props)
{
    m_toolDefaults[props.index()] = props;
    m_activeTool = props.index();
    toolChanged();
}

void CircuitScene::selectTool(std::size_t kind)
{
    if (kind >= m_toolDefaults.size()) return;
    clearSelection();
    m_activeTool = kind;
    toolChanged();
}

void CircuitScene::selectGateTool(GType gateType)
{
    auto gate = std::get<GateProps>(m_toolDefaults[itemKind<GateProps>]);
    gate.type = gateType;
    m_toolDefaults[itemKind<GateProps>] = normalized(gate);
    selectTool(itemKind<GateProps>);
}

PortItem* CircuitScene::findNearestPort(const QPointF& scenePos, double threshold)
{
    PortItem* nearestPort = nullptr;
    double minDistance = threshold;

    for (QGraphicsItem* item : items(QRectF(scenePos - QPointF(threshold, threshold),
        QSizeF(threshold * 2, threshold * 2)))) {
        PortItem* port = dynamic_cast<PortItem*>(item);
        if (port) {
            QPointF portPos = port->mapToScene(QPointF(0, 0));
            double distance = QLineF(scenePos, portPos).length();
            if (distance < minDistance) {
                minDistance = distance;
                nearestPort = port;
            }
        }
    }

    return nearestPort;
}

void CircuitScene::clearHighlights()
{
    for (PortItem* port : {m_currWireStartPort, m_snapPort}) {
        if (port) port->setHighlighted(false);
    }
    m_snapPort = nullptr;
}

void CircuitScene::startWireConnection(QPointF startPoint)
{
    // Snaps to the nearest pin
    m_currWireStartPort = findNearestPort(startPoint);
    m_wireStartPoint = m_currWireStartPort ? m_currWireStartPort->scenePos() : startPoint;
    if (m_currWireStartPort) m_currWireStartPort->setHighlighted(true);

    m_connectingWire = true;
    m_currWire = new WireItem(m_wireStartPoint, m_wireStartPoint);
    m_currWire->setPen(QPen(currentTheme().drag, 2, Qt::DashLine));
    addItem(m_currWire);
}

void CircuitScene::updateWireConnection(QPointF currentPoint)
{
    if (!m_connectingWire || !m_currWire) return;

    PortItem* nearPort = findNearestPort(currentPoint);
    const bool canSnap = nearPort && nearPort != m_currWireStartPort
                         && (!m_currWireStartPort || m_currWireStartPort->canConnectTo(nearPort));
    PortItem* snapPort = canSnap ? nearPort : nullptr;
    if (snapPort != m_snapPort) {
        if (m_snapPort) m_snapPort->setHighlighted(false);
        m_snapPort = snapPort;
        if (m_snapPort) m_snapPort->setHighlighted(true);
    }

    m_currWire->setPen(m_snapPort ? QPen(currentTheme().snap, 2.5, Qt::DashLine) : QPen(currentTheme().drag, 2, Qt::DashLine));
    m_currWire->setEnds(m_wireStartPoint, m_snapPort ? m_snapPort->scenePos() : currentPoint);
}

void CircuitScene::finishWireConnection(QPointF endPoint)
{
    if (!m_connectingWire) return;
    PortItem* startPort = m_currWireStartPort;
    PortItem* endPort = findNearestPort(endPoint);
    cancelWireConnection();   // a real wire replaces it if the pins fit

    // Not while running (the drag may predate Start); only output to free input
    if (isLocked() || !startPort || !endPort || !endPort->canConnectTo(startPort)
        || endPort->getPortType() != PortType::IN || !endPort->getConnections().isEmpty()) return;
    addWire(startPort, endPort);
    circuitEdited();
}

ExportGraph CircuitScene::buildGraph()
{
    m_wireMap.clear();
    m_displays.clear();
    m_sources.clear();
    m_lastNetValues.clear();   // nets are renumbered, so the next result repaints all
    m_instanceNets.clear();
    m_instanceRegisters.clear();

    QList<WireItem*> wires;
    QList<ComponentItem*> components;
    QList<BlockItem*> blocks;
    for (QGraphicsItem* item : items()) {
        if (auto* wire = dynamic_cast<WireItem*>(item)) {
            if (wire->isConnected()) wires.append(wire);   // one not attached at both ends carries nothing
        } else if (auto* block = dynamic_cast<BlockItem*>(item)) {
            blocks.append(block);
        } else if (auto* component = dynamic_cast<ComponentItem*>(item)) {
            components.append(component);
        }
    }

    // One net per wired output pin (0 = not connected); a display passes inputs through
    u32 nextNet = 1;
    QHash<PortItem*, u32> outputNets;
    std::function<u32(PortItem*)> netOf = [&](PortItem* pin) -> u32 {
        if (!pin || pin->getConnections().isEmpty()) return 0;
        if (pin->getPortType() == PortType::IN) return netOf(pin->getConnections().first()->getStartPort());
        if (const auto it = outputNets.constFind(pin); it != outputNets.constEnd()) return *it;
        if (auto* display = dynamic_cast<DisplayItem*>(pin->getParent())) {
            outputNets.insert(pin, 0);   // displays wired in a ring carry nothing
            const u32 net = netOf(display->getInputPorts().value(display->getOutputPorts().indexOf(pin)));
            outputNets.insert(pin, net);
            return net;
        }
        outputNets.insert(pin, nextNet);
        return nextNet++;
    };
    const auto netsOf = [&](const QList<PortItem*>& pins) {
        std::vector<u32> nets;
        nets.reserve(pins.size());
        for (PortItem* pin : pins) nets.push_back(netOf(pin));
        return nets;
    };
    // Unwired source pins get nets too: the waveforms show every source bit
    const auto sourceNetsOf = [&](const QList<PortItem*>& pins) {
        std::vector<u32> nets;
        nets.reserve(pins.size());
        for (PortItem* pin : pins) nets.push_back(pin->getConnections().isEmpty() ? nextNet++ : netOf(pin));
        return nets;
    };

    for (WireItem* wire : std::as_const(wires)) {
        const u32 net = netOf(wire->getStartPort());
        m_wireMap.wire2net[wire] = net;
        m_wireMap.net2wire[net].push_back(wire);
    }

    ExportGraph graph;
    for (ComponentItem* component : std::as_const(components)) {
        if (auto* gate = dynamic_cast<GateItem*>(component)) {
            graph.gates.emplace_back(gate->getGateType(), netsOf(gate->getInputPorts()), netOf(gate->getOutputPort()));
        } else if (auto* mux = dynamic_cast<MuxItem*>(component)) {
            graph.muxes.emplace_back(netsOf(mux->getInputPorts()), netsOf(mux->getAddressPorts()), netOf(mux->getOutputPort()));
        } else if (auto* reg = dynamic_cast<RegisterItem*>(component)) {
            m_wireMap.registers.push_back(reg);
            const auto props = std::get<RegisterProps>(reg->props());
            graph.registers.emplace_back(props.type, netOf(reg->getInputPort()), netOf(reg->getInputPortTwo()), netOf(reg->getOutputPort()),
                                         netOf(reg->getClkPort()), netOf(reg->getReadEnablePort()), props.asyncReset, props.resetValue);
        } else if (auto* source = dynamic_cast<SourceItem*>(component)) {
            m_sources.append(source);   // the simulator computes their values
            graph.sources.emplace_back(sourceNetsOf(source->getOutputPorts()), source->sourceProps());
        } else if (auto* display = dynamic_cast<DisplayItem*>(component)) {
            m_displays.append(display);
        }
    }
    graph.numNets = nextNet - 1;
    if (!blocks.isEmpty()) expandBlocks(graph, blocks, netOf);
    buildWaveSignals(graph, blocks);
    return graph;
}

void CircuitScene::buildWaveSignals(ExportGraph& graph, const QList<BlockItem*>& blocks)
{
    m_waveSignals.clear();

    // m_sources is in graph.sources order (a constant 1 for blocks comes after)
    for (qsizetype i = 0; i < m_sources.size(); i++) {
        m_waveSignals.append({QString(), QString(), m_sources[i]->name(), false, graph.sources[size_t(i)].outIDs});
    }
    for (DisplayItem* display : std::as_const(m_displays)) {
        WaveSignal signal{QString(), QString(), display->name(), true, {}};
        for (PortItem* pin : display->getInputPorts()) {
            const auto wire = pin->getConnections().isEmpty() ? m_wireMap.wire2net.end()
                                                              : m_wireMap.wire2net.find(pin->getConnections().first());
            signal.nets.push_back(wire == m_wireMap.wire2net.end() ? 0 : wire->second);
        }
        m_waveSignals.append(std::move(signal));
    }
    // Inputs, then outputs, by name with numbers in order ("Source 2" < "Source 10")
    QCollator collator;
    collator.setNumericMode(true);
    std::ranges::stable_sort(m_waveSignals, [&collator](const WaveSignal& a, const WaveSignal& b) {
        if (a.output != b.output) return !a.output;
        return collator.compare(a.name, b.name) < 0;
    });

    std::function<void(const QString&, const QString&)> addInstance = [&](const QString& path, const QString& moduleName) {
        const NetModule* module = m_design.module(moduleName);
        if (!module) return;
        const QHash<int, u32> nets = m_instanceNets.value(path);
        for (const NetNode& node : module->nodes) {
            const auto* io = std::get_if<IOType>(&node.type);
            if (!io || node.ports.isEmpty()) continue;
            WaveSignal signal{path, moduleName, node.id, *io == IOType::OUT, {}};
            for (int bit : node.ports.first().bits) {
                if (bit == NetBit::One) signal.nets.push_back(WaveSignal::AlwaysOne);
                else if (bit == NetBit::Zero) signal.nets.push_back(0);
                else signal.nets.push_back(nets.value(bit, 0));
            }
            m_waveSignals.append(std::move(signal));
        }
        for (const NetNode& node : module->nodes) {
            if (const auto* ref = std::get_if<ModuleRef>(&node.type)) addInstance(path + u'/' + node.id, ref->module);
        }
    };
    for (BlockItem* block : blocks) addInstance(block->blockProps().instance, block->blockProps().module);

    std::vector<u32> recorded;
    for (const WaveSignal& signal : std::as_const(m_waveSignals)) {
        for (u32 net : signal.nets) {
            if (net != 0 && net != WaveSignal::AlwaysOne) recorded.push_back(net);
        }
    }
    std::ranges::sort(recorded);
    recorded.erase(std::unique(recorded.begin(), recorded.end()), recorded.end());
    graph.recordedNets = std::move(recorded);
}

namespace {
// Every screen frame, at most 60 per second; faster can't be followed anyway
int frameIntervalMs()
{
    const QScreen* screen = QGuiApplication::primaryScreen();
    const qreal hz = screen && screen->refreshRate() > 0 ? screen->refreshRate() : 60.0;
    return int(std::ceil(1000.0 / std::clamp(hz, 30.0, 60.0)));
}
}

void CircuitScene::queueResult(const SimResult& result)
{
    m_pendingResult = result;          // replaces one not shown yet
    m_hasPendingResult = true;
    if (m_frameTimer.isActive()) return;
    // A whole frame since the last one: show it now, else at the next frame
    const qint64 sinceLast = m_sinceLastFrame.isValid() ? m_sinceLastFrame.elapsed() : std::numeric_limits<qint64>::max();
    m_frameTimer.start(int(std::max<qint64>(0, frameIntervalMs() - sinceLast)));
}

void CircuitScene::applyPendingResult()
{
    if (!m_hasPendingResult) return;
    m_hasPendingResult = false;
    m_sinceLastFrame.start();
    receiveResult(m_pendingResult);
}

void CircuitScene::receiveResult(const SimResult& result)
{
    // A late result of a stopped or replaced run is dropped; a paused run takes its own
    if (m_simState == SimState::Stopped || result.runId != m_runId) return;

    const auto& nets = result.netValues;
    // First result of a run: repaint every net
    const bool fullUpdate = m_lastNetValues.size() != nets.size();

    for (size_t i = 1; i < nets.size(); i++) {
        if (!fullUpdate && m_lastNetValues[i] == nets[i]) {continue;}

        auto it = m_wireMap.net2wire.find(static_cast<u32>(i));
        if (it == m_wireMap.net2wire.end()) {continue;}

        const bool value = nets[i];
        for (auto* wire : it->second) {
            wire->setValue(value);
            wire->getEndPort()->setValue(value);
            wire->getStartPort()->setValue(value);
        }
    }
    m_lastNetValues = nets;

    for (const auto& source : m_sources) {
        if (source) {source->setTick(result.sourceTick);}   // null if the source was deleted
    }
    for (size_t i = 0; i < std::min(result.registerValues.size(), m_wireMap.registers.size()); i++) {
        m_wireMap.registers[i]->setValue(result.registerValues[i]);
    }
    m_lastRegisterValues = result.registerValues;

    for (const auto& display : m_displays) {
        if (display) {display->updateValue();}   // null if the display was deleted
    }
    valuesChanged();
}

//===================== Compiled designs ========================

void CircuitScene::receiveDesign(const NetDesign& design)
{
    resetCircuit();   // the simulation must never outlive its items
    m_design = design;
    buildModule(m_design, m_design.top, true);
}

CircuitData CircuitScene::circuitData() const
{
    CircuitData data;
    data.design = m_design;
    QHash<const QGraphicsObject*, int> indexOf;
    for (QGraphicsItem* item : items(Qt::AscendingOrder)) {
        auto* component = dynamic_cast<ComponentItem*>(item);
        if (!component) continue;
        QString name;
        if (auto* source = dynamic_cast<SourceItem*>(component)) name = source->name();
        else if (auto* display = dynamic_cast<DisplayItem*>(component)) name = display->name();
        indexOf.insert(component, int(data.components.size()));
        data.components.append({component->props(), component->pos(), name});
    }
    for (QGraphicsItem* item : items(Qt::AscendingOrder)) {
        auto* wire = dynamic_cast<WireItem*>(item);
        if (!wire || !wire->isConnected()) continue;
        const auto end = [&indexOf](PortItem* pin) -> std::pair<int, int> {
            auto* component = dynamic_cast<ComponentItem*>(pin->getParent());
            if (!component || !indexOf.contains(component)) return {-1, -1};
            return {indexOf[component], int(componentPins(component).indexOf(pin))};
        };
        const auto [from, fromPin] = end(wire->getStartPort());
        const auto [to, toPin] = end(wire->getEndPort());
        if (from >= 0 && to >= 0) data.wires.append({from, fromPin, to, toPin});
    }
    return data;
}

std::expected<void, QString> CircuitScene::loadCircuit(const CircuitData& data)
{
    // Built and checked off the scene first, so a damaged file changes nothing
    std::vector<std::unique_ptr<ComponentItem>> built;
    for (const SavedComponent& saved : data.components) {
        if (const auto* block = std::get_if<BlockProps>(&saved.props); block && !data.design.module(block->module))
            return std::unexpected(tr("A block uses the module %1, which the file does not have.").arg(block->module));
        built.emplace_back(createComponent(saved.props));
    }
    QList<std::pair<PortItem*, PortItem*>> wires;
    QSet<PortItem*> usedInputs;
    for (qsizetype i = 0; i < data.wires.size(); i++) {
        const SavedWire& saved = data.wires[i];
        const auto pin = [&built](int component, int index) -> PortItem* {
            if (component < 0 || component >= int(built.size())) return nullptr;
            return componentPins(built[component].get()).value(index);
        };
        PortItem* from = pin(saved.from, saved.fromPin);
        PortItem* to = pin(saved.to, saved.toPin);
        PortItem* input = from && from->getPortType() == PortType::IN ? from : to;
        if (!from || !to || !to->canConnectTo(from) || usedInputs.contains(input))
            return std::unexpected(tr("Wire %1 does not connect an output to a free input.").arg(i + 1));
        usedInputs.insert(input);
        wires.append(std::pair{from, to});
    }

    resetCircuit();
    m_design = data.design;
    for (qsizetype i = 0; i < data.components.size(); i++) {
        ComponentItem* item = built[i].release();
        const QString& name = data.components[i].name;   // as saved: a compiled design's constant 1 has none
        if (auto* source = dynamic_cast<SourceItem*>(item)) source->setName(name);
        else if (auto* display = dynamic_cast<DisplayItem*>(item)) display->setName(name);
        item->setPos(data.components[i].position);
        addItem(item);
        watchMoves(item);
    }
    for (const auto& [from, to] : std::as_const(wires)) addWire(from, to);
    circuitEdited();
    return {};
}

WireItem* CircuitScene::addWire(PortItem* from, PortItem* to)
{
    auto* wire = new WireItem(from->mapToScene(QPointF(0, 0)), to->mapToScene(QPointF(0, 0)));
    wire->setStartPort(from);
    wire->setEndPort(to);
    from->addConnection(wire);
    to->addConnection(wire);
    wire->setPen(QPen(currentTheme().wire, 2));
    addItem(wire);

    for (QGraphicsObject* end : {from->getParent(), to->getParent()}) {
        if (!end) continue;
        connect(end, &QGraphicsObject::xChanged, wire, &WireItem::updateWirePosition);
        connect(end, &QGraphicsObject::yChanged, wire, &WireItem::updateWirePosition);
    }
    wire->updateWirePosition();
    return wire;
}

void CircuitScene::buildModule(const NetDesign& design, const QString& moduleName, bool topLevel)
{
    const NetModule* module = design.module(moduleName);
    if (!module) return;

    QHash<int, PortItem*> drivers;                 // bit -> driving pin
    QList<std::pair<int, PortItem*>> readers;
    const auto pinBits = [](const QList<PortItem*>& pins, const QList<int>& bits, auto&& use) {
        for (qsizetype i = 0; i < std::min(pins.size(), bits.size()); i++) use(bits[i], pins[i]);
    };
    const auto drives = [&](const QList<PortItem*>& pins, const QList<int>& bits) {
        pinBits(pins, bits, [&](int bit, PortItem* pin) { if (bit != NetBit::Zero) drivers.insert(bit, pin); });
    };
    const auto reads = [&](const QList<PortItem*>& pins, const QList<int>& bits) {
        pinBits(pins, bits, [&](int bit, PortItem* pin) { if (bit != NetBit::Zero) readers.append({bit, pin}); });
    };

    for (const NetNode& node : module->nodes) {
        const NodePins pins = nodePins(node);
        QGraphicsObject* item = nullptr;

        if (const auto props = nodeProps(node, topLevel)) {
            ComponentItem* component = createComponent(*props);
            if (auto* gate = dynamic_cast<GateItem*>(component)) {
                reads(gate->getInputPorts(), pins.inputs);
                drives({gate->getOutputPort()}, pins.outputs);
            } else if (auto* mux = dynamic_cast<MuxItem*>(component)) {
                reads(mux->getInputPorts() + mux->getAddressPorts(), pins.inputs + pins.select);
                drives({mux->getOutputPort()}, pins.outputs);
            } else if (auto* reg = dynamic_cast<RegisterItem*>(component)) {
                reads({reg->getInputPort(), reg->getClkPort(), reg->getReadEnablePort()}, {pins.inputs.value(0), pins.clock, pins.enable});
                if (reg->getInputPortTwo()) reads({reg->getInputPortTwo()}, {pins.input2});
                drives({reg->getOutputPort()}, pins.outputs);
                if (!topLevel) m_registerNodes.insert(reg, node.id);
            } else if (auto* block = dynamic_cast<BlockItem*>(component)) {
                reads(block->getInputPorts(), pins.inputs);
                drives(block->getOutputPorts(), pins.outputs);
            } else if (auto* source = dynamic_cast<SourceItem*>(component)) {   // a module input, or the constant 1
                source->setName(portName(node));
                drives(source->getOutputPorts(), pins.outputs);
            } else if (auto* display = dynamic_cast<DisplayItem*>(component)) {
                display->setName(portName(node));
                reads(display->getInputPorts(), pins.inputs);
            }
            item = component;
        } else {
            // Inside a block the ports are tags at its edges
            const bool input = isModulePort(node, IOType::IN);
            auto* terminal = new TerminalItem(node.id, !input, int(portBits(node).size()));
            if (input) drives(terminal->pins(), portBits(node));
            else reads(terminal->pins(), portBits(node));
            item = terminal;
        }

        item->setPos(node.position);
        if (m_readOnly || !topLevel) {
            item->setFlag(QGraphicsItem::ItemIsMovable, false);
            item->setFlag(QGraphicsItem::ItemIsSelectable, false);
        }
        addItem(item);
        if (topLevel) watchMoves(item);
    }

    for (const auto& [bit, reader] : std::as_const(readers)) {
        PortItem* driver = drivers.value(bit);
        if (!driver || driver == reader) continue;   // undriven bits read 0
        WireItem* wire = addWire(driver, reader);
        if (!topLevel) m_wireBits.insert(wire, bit);
    }
    if (topLevel) circuitEdited();
}

namespace {
// Flattens blocks for the simulator; union-find joins a port bit inside with the wire outside
struct BlockExpander {
    const NetDesign& design;
    ExportGraph& graph;
    QHash<QString, QHash<int, u32>>& instanceNets;
    QHash<QString, QHash<QString, u32>>& instanceRegisters;
    std::vector<u32> parent;   // net 0 (constant 0) stays a root
    u32 one = 0;               // made when first needed

    u32 newNet()
    {
        parent.push_back(u32(parent.size()));
        return u32(parent.size() - 1);
    }
    u32 find(u32 net)
    {
        while (parent[net] != net) net = parent[net] = parent[parent[net]];
        return net;
    }
    void join(u32 a, u32 b)
    {
        a = find(a);
        b = find(b);
        if (a == b) return;
        if (b == 0) std::swap(a, b);
        parent[b] = a;
    }
    u32 constantOne()
    {
        if (!one) {
            one = newNet();
            graph.sources.emplace_back(std::vector<u32>{one}, SourceProps::constantOf(1, 1));
        }
        return one;
    }
    u32 net(QHash<int, u32>& nets, int bit)
    {
        if (bit == NetBit::Zero) return 0;
        if (bit == NetBit::One) return constantOne();
        auto it = nets.find(bit);
        if (it == nets.end()) it = nets.insert(bit, newNet());
        return *it;
    }

    static QHash<QString, QList<int>> portBits(const NetModule& module)
    {
        QHash<QString, QList<int>> bits;
        for (const NetNode& node : module.nodes) {
            if (!std::holds_alternative<IOType>(node.type) || node.ports.isEmpty()) continue;
            bits.insert(node.id, node.ports.first().bits);
        }
        return bits;
    }

    // `nets` holds the nets of its port bits
    void expand(const QString& path, const NetModule& module, QHash<int, u32> nets)
    {
        for (const NetNode& node : module.nodes) {
            const NodePins pins = nodePins(node);
            const auto netsOf = [&](const QList<int>& bits) {
                std::vector<u32> result;
                for (int bit : bits) result.push_back(net(nets, bit));
                return result;
            };

            if (const auto* gate = std::get_if<GType>(&node.type)) {
                graph.gates.push_back(Gate(*gate, netsOf(pins.inputs), net(nets, pins.outputs.value(0))));
            } else if (std::holds_alternative<MType>(node.type)) {
                graph.muxes.push_back(Mux(netsOf(pins.inputs), netsOf(pins.select), net(nets, pins.outputs.value(0))));
            } else if (const auto* ff = std::get_if<FlipFlop>(&node.type)) {
                instanceRegisters[path].insert(node.id, u32(graph.registers.size()));
                graph.registers.emplace_back(RType::D, net(nets, pins.inputs.value(0)), net(nets, pins.input2), net(nets, pins.outputs.value(0)),
                                             net(nets, pins.clock), net(nets, pins.enable), ff->reset == FlipFlop::Reset::Async, ff->resetValue);
            } else if (const auto* ref = std::get_if<ModuleRef>(&node.type)) {
                const NetModule* child = design.module(ref->module);
                if (!child) continue;
                const QHash<QString, QList<int>> childBits = portBits(*child);
                QHash<int, u32> childNets;
                for (const NetPort& port : node.ports) {
                    const QList<int> inner = childBits.value(port.name);
                    for (qsizetype i = 0; i < std::min(inner.size(), port.bits.size()); i++) {
                        join(net(childNets, inner[i]), net(nets, port.bits[i]));
                    }
                }
                expand(path + u'/' + node.id, *child, std::move(childNets));
            } else {
                for (int bit : pins.outputs) net(nets, bit);   // module ports and the constant 1
                for (int bit : pins.inputs) net(nets, bit);
            }
        }
        instanceNets.insert(path, std::move(nets));
    }

    // Consecutive numbers for the joined nets; returns the highest
    u32 renumber(WireMap& wires)
    {
        std::vector<u32> number(parent.size(), 0);
        u32 next = 1;
        for (u32 n = 1; n < parent.size(); n++) {
            const u32 root = find(n);
            if (root == 0) continue;
            if (!number[root]) number[root] = next++;
            number[n] = number[root];
        }
        const auto renumbered = [&](u32& net) { net = number[net]; };

        for (Gate& gate : graph.gates) {
            std::ranges::for_each(gate.inIDs, renumbered);
            renumbered(gate.outID);
        }
        for (Mux& mux : graph.muxes) {
            std::ranges::for_each(mux.inData, renumbered);
            std::ranges::for_each(mux.inAddress, renumbered);
            renumbered(mux.outID);
        }
        for (Register& reg : graph.registers) {
            for (u32* net : {&reg.inID, &reg.InID2, &reg.outID, &reg.clkID, &reg.enableID}) renumbered(*net);
        }
        for (Source& source : graph.sources) std::ranges::for_each(source.outIDs, renumbered);

        wires.net2wire.clear();
        for (auto& [wire, net] : wires.wire2net) {
            renumbered(net);
            wires.net2wire[net].push_back(wire);
        }
        for (QHash<int, u32>& nets : instanceNets) {
            for (u32& net : nets) renumbered(net);
        }
        return next - 1;
    }
};
}

void CircuitScene::expandBlocks(ExportGraph& graph, const QList<BlockItem*>& blocks, const std::function<u32(PortItem*)>& pinNet)
{
    BlockExpander expander{m_design, graph, m_instanceNets, m_instanceRegisters, {}};
    expander.parent.resize(graph.numNets + 1);
    for (u32 n = 0; n < expander.parent.size(); n++) expander.parent[n] = n;

    for (BlockItem* block : blocks) {
        const BlockProps& props = block->blockProps();
        const NetModule* module = m_design.module(props.module);
        if (!module) continue;

        const QHash<QString, QList<int>> portBits = BlockExpander::portBits(*module);
        QHash<int, u32> nets;
        qsizetype input = 0, output = 0;
        for (const BlockPort& port : props.ports) {
            const QList<int> bits = portBits.value(port.name);
            const QList<PortItem*> pins = port.output ? block->getOutputPorts() : block->getInputPorts();
            qsizetype& next = port.output ? output : input;
            for (int i = 0; i < port.width; i++, next++) {
                if (i >= bits.size() || next >= pins.size()) continue;
                const u32 outside = pinNet(pins[next]);
                const u32 inside = expander.net(nets, bits[i]);
                if (outside || !port.output) expander.join(inside, outside);   // an unwired input reads 0
            }
        }
        expander.expand(props.instance, *module, std::move(nets));
    }
    graph.numNets = expander.renumber(m_wireMap);
}

void CircuitScene::showInstanceValues(const QString& path, CircuitScene* view) const
{
    const QHash<int, u32> nets = m_instanceNets.value(path);
    const QHash<QString, u32> registers = m_instanceRegisters.value(path);
    const auto valueOf = [&](int bit) {
        if (bit == NetBit::One) return true;
        const u32 net = nets.value(bit, 0);
        return net != 0 && net < m_lastNetValues.size() && m_lastNetValues[net] != 0;
    };

    for (auto it = view->m_wireBits.cbegin(); it != view->m_wireBits.cend(); ++it) {
        const bool value = valueOf(it.value());
        it.key()->setValue(value);
        it.key()->getStartPort()->setValue(value);
        it.key()->getEndPort()->setValue(value);
    }
    for (auto it = view->m_registerNodes.cbegin(); it != view->m_registerNodes.cend(); ++it) {
        const u32 index = registers.value(it.value(), std::numeric_limits<u32>::max());
        it.key()->setValue(index < m_lastRegisterValues.size() && m_lastRegisterValues[index]);
    }
}

void CircuitScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {

    // No placing or wiring while running or inside a block
    if ((isLocked() || m_readOnly) && (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)) {
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        QGraphicsItem* clickedItem = itemAt(event->scenePos(), QTransform());
        if (!clickedItem) {
            addComponent(toolProps(), event->scenePos());

            event->accept();
            return;
        }
    }
    else if (event->button() == Qt::RightButton) {
        startWireConnection(event->scenePos());
        event->accept();
        return;
    }

    QGraphicsScene::mousePressEvent(event);
}

void CircuitScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Also when the click lands on one of its pins
        for (QGraphicsItem* item : items(event->scenePos())) {
            auto* block = dynamic_cast<BlockItem*>(item);
            if (!block && item->parentItem()) block = dynamic_cast<BlockItem*>(item->parentItem());
            if (block) {
                openBlock(block);
                event->accept();
                return;
            }
            if (dynamic_cast<ComponentItem*>(item)) break;   // another component is on top
        }
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}

void CircuitScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_connectingWire) {
        updateWireConnection(event->scenePos());

    } else {
        QGraphicsScene::mouseMoveEvent(event);
    }
}

void CircuitScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_connectingWire && (event->button() == Qt::RightButton || event->button() == Qt::LeftButton)) {
        finishWireConnection(event->scenePos());
    } else {
        QGraphicsScene::mouseReleaseEvent(event);
    }
}

//=====================QGraphicsView========================

CircuitCanvas::CircuitCanvas(QWidget* parent)
    : QGraphicsView(parent)
{
    m_scene = new CircuitScene(this);
    setScene(m_scene);

    setDragMode(QGraphicsView::RubberBandDrag);
    setRenderHint(QPainter::Antialiasing);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Cached; MainWindow::setColorScheme drops it on a theme change
    setCacheMode(QGraphicsView::CacheBackground);
    // One bounding rect: merging thousands of small update rects costs more than painting
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);

    m_breadcrumb = new QFrame(this);   // not the viewport's, so panning doesn't move it
    m_breadcrumb->setObjectName(u"blockBreadcrumb"_s);
    auto* layout = new QHBoxLayout(m_breadcrumb);
    layout->setContentsMargins(4, 3, 4, 3);
    layout->setSpacing(2);
    m_breadcrumb->hide();

    connect(m_scene, &CircuitScene::openBlock, this, &CircuitCanvas::openBlock);
    connect(m_scene, &CircuitScene::designChanged, this, [this] { showLevel(0); });
    connect(m_scene, &CircuitScene::valuesChanged, this, &CircuitCanvas::refreshBlockValues);
    connect(m_scene, &CircuitScene::simStateChanged, this, [this](SimState state) {
        if (state == SimState::Stopped && !m_levels.isEmpty()) m_levels.last().scene->clearValues();
    });
}

CircuitScene* CircuitCanvas::currentScene() const
{
    return m_levels.isEmpty() ? m_scene : m_levels.last().scene;
}

QString CircuitCanvas::currentPath() const
{
    return m_levels.isEmpty() ? QString() : m_levels.last().path;
}

void CircuitCanvas::openBlock(BlockItem* block)
{
    if (!block || block->scene() != currentScene()) return;
    const BlockProps& props = block->blockProps();
    if (!m_scene->design().module(props.module)) return;   // left over from an older design

    Level level;
    level.path = m_levels.isEmpty() ? props.instance : m_levels.last().path + u'/' + props.instance;
    level.title = itemTitle(props);
    level.outerTransform = transform();
    level.outerCentre = viewCentre();
    level.scene = new CircuitScene(this);
    level.scene->setReadOnly(true);
    level.scene->buildModule(m_scene->design(), props.module, false);
    connect(level.scene, &CircuitScene::openBlock, this, &CircuitCanvas::openBlock);
    m_levels.append(level);

    showScene(level.scene);
    showWholeCircuit();
    refreshBlockValues();
    updateBreadcrumb();
    depthChanged(depth());
}

void CircuitCanvas::showLevel(int level)
{
    level = std::clamp(level, 0, depth());
    if (level == depth()) return;

    const Level outermostClosed = m_levels[level];
    while (depth() > level) {
        Level closed = m_levels.takeLast();
        if (closed.scene) closed.scene->deleteLater();
    }
    showScene(currentScene());
    setTransform(outermostClosed.outerTransform);
    centerOn(outermostClosed.outerCentre);
    refreshBlockValues();
    updateBreadcrumb();
    depthChanged(depth());
}

void CircuitCanvas::showWholeCircuit()
{
    // Never magnified
    const QRectF contents = currentScene()->itemsBoundingRect().adjusted(-60, -60, 60, 60);
    resetTransform();
    if (contents.isEmpty()) return;
    fitInView(contents, Qt::KeepAspectRatio);
    if (transform().m11() > 1) resetTransform();
    centerOn(contents.center());
}

void CircuitCanvas::focusOn(ComponentItem* item)
{
    if (!item || item->scene() != m_scene) return;
    showLevel(0);
    // The component with a few times its size around it, at most 2x magnified
    const QRectF body = item->sceneBoundingRect();
    const qreal margin = std::max({body.width(), body.height(), 100.0}) * 2;
    resetTransform();
    fitInView(body.adjusted(-margin, -margin, margin, margin), Qt::KeepAspectRatio);
    if (transform().m11() > 2) setTransform(QTransform::fromScale(2, 2));
    centerOn(body.center());
    m_scene->clearSelection();
    item->setSelected(true);
}

void CircuitCanvas::showScene(CircuitScene* scene)
{
    m_panning = false;
    setCursor(Qt::ArrowCursor);
    setScene(scene);
}

void CircuitCanvas::refreshBlockValues()
{
    if (m_levels.isEmpty() || m_scene->simState() == SimState::Stopped) return;
    m_scene->showInstanceValues(m_levels.last().path, m_levels.last().scene);
}

void CircuitCanvas::updateBreadcrumb()
{
    // Buttons added while visible appear late, so it would be measured empty
    m_breadcrumb->hide();
    auto* layout = static_cast<QHBoxLayout*>(m_breadcrumb->layout());
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (m_levels.isEmpty()) return;

    const QString top = m_scene->design().top.isEmpty() ? tr("Circuit") : m_scene->design().top;
    for (int level = 0; level <= depth(); level++) {
        if (level > 0) layout->addWidget(new QLabel(u"\u203A"_s, m_breadcrumb));
        auto* button = new QToolButton(m_breadcrumb);
        button->setText(level == 0 ? top : m_levels[level - 1].title);
        button->setAutoRaise(true);
        if (level == depth()) {
            button->setEnabled(false);   // where the canvas is now
        } else {
            button->setToolTip(tr("Go back to %1 (Esc goes back one level)").arg(button->text()));
            connect(button, &QToolButton::clicked, this, [this, level] { showLevel(level); });
        }
        layout->addWidget(button);
    }
    m_breadcrumb->show();
    m_breadcrumb->adjustSize();
    m_breadcrumb->move(viewport()->geometry().topLeft() + QPoint(12, 12));
    m_breadcrumb->raise();
}

void CircuitCanvas::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (m_breadcrumb->isVisible()) m_breadcrumb->move(viewport()->geometry().topLeft() + QPoint(12, 12));
}

void CircuitCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && !m_levels.isEmpty()) {
        showLevel(depth() - 1);
    } else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        currentScene()->deleteSelection();   // nothing inside a block
    } else {
        QGraphicsView::keyPressEvent(event);
        return;
    }
    event->accept();
}

void CircuitCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QGraphicsItem* itemUnderMouse = itemAt(event->pos());
        
        if (!itemUnderMouse) {
            // Empty canvas: pan
            m_panning = true;
            m_lastPanPoint = event->pos();
            m_panStartPoint = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }

    QGraphicsView::mousePressEvent(event);
}

void CircuitCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {

        QPoint delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();

        QScrollBar* hBar = horizontalScrollBar();
        QScrollBar* vBar = verticalScrollBar();

        hBar->setValue(hBar->value() - delta.x());
        vBar->setValue(vBar->value() - delta.y());

        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void CircuitCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_panning) {

        m_panning = false;
        setCursor(Qt::ArrowCursor);

        // A click without a drag deselects
        if ((event->pos() - m_panStartPoint).manhattanLength() < QApplication::startDragDistance()) {
            currentScene()->clearSelection();
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void CircuitCanvas::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() == 0) return;
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;

    // Never zoomed out past the scene
    const double newScale = transform().m11() * factor;
    const QRectF sceneArea = currentScene()->sceneRect();
    if (factor < 1 && (viewport()->width() / newScale >= sceneArea.width() || viewport()->height() / newScale >= sceneArea.height())) {
        return;
    }
    scale(factor, factor);
}
