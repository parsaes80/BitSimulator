#include "CircuitCanvas.h"
#include <cmath>
#include <QHash>
#include <QMouseEvent>
#include <QQueue>
#include <QSet>
#include <QKeyEvent>
#include <QDebug>
#include <qscrollbar.h>
#include <vector>
#include <QApplication>

extern GlobalMap map;
extern QGraphicsScene* scenePtr;

// True while the items should draw simulation values (running or paused). Only the GUI thread uses it.
bool sim_values_visible = false;

//===================== QGraphicsScene ========================

CircuitScene::CircuitScene(QObject* parent)
    : QGraphicsScene(parent), m_connectingWire(false), m_currWire(nullptr)
{
    setSceneRect(0, 0, 160000, 100000); // Large canvas

    // Force full scene update on any change
    connect(this, &QGraphicsScene::changed, this, [this]() { update(); });

    // Force update when selection changes
    connect(this, &QGraphicsScene::selectionChanged, this, [this]() { update(); });
}

void CircuitScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    const CanvasTheme& theme = canvasTheme();
    painter->fillRect(rect, theme.background);

    constexpr qreal DotSpacing = 20;     // scene units between two dots
    constexpr qreal MinDotGap = 12;      // screen pixels; zoomed out further, only every 2nd, 4th, ... dot is drawn
    constexpr qreal DotSize = 2;         // screen pixels, at every zoom level

    qreal spacing = DotSpacing;
    const qreal pixelsPerUnit = painter->worldTransform().m11();
    if (pixelsPerUnit <= 0) return;
    while (spacing * pixelsPerUnit < MinDotGap) spacing *= 2;

    QList<QPointF> dots;
    const qreal firstX = std::ceil(rect.left() / spacing) * spacing;
    const qreal firstY = std::ceil(rect.top() / spacing) * spacing;
    dots.reserve(qsizetype((rect.width() / spacing + 1) * (rect.height() / spacing + 1)));
    for (qreal y = firstY; y <= rect.bottom(); y += spacing) {
        for (qreal x = firstX; x <= rect.right(); x += spacing) dots.append(QPointF(x, y));
    }

    QPen pen(theme.dot, DotSize, Qt::SolidLine, Qt::RoundCap);
    pen.setCosmetic(true);               // the same size on screen however far the view is zoomed
    painter->setPen(pen);
    painter->drawPoints(dots.constData(), int(dots.size()));
}

CircuitScene::~CircuitScene()
{
    // ~QGraphicsScene deletes the items next, which can emit selectionChanged. Listeners such as the
    // properties panel would then call into this object after its members are destroyed.
    disconnect(this, &QGraphicsScene::selectionChanged, nullptr, nullptr);
}

ComponentItem* CircuitScene::addComponent(const ItemProps& props, QPointF position)
{
    if (isLocked()) return nullptr;

    ComponentItem* item = std::visit([](const auto& p) -> ComponentItem* {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, GateProps>)          return new GateItem(p);
        else if constexpr (std::is_same_v<T, RegisterProps>) return new RegisterItem(p);
        else if constexpr (std::is_same_v<T, MuxProps>)      return new MuxItem(p);
        else if constexpr (std::is_same_v<T, SourceProps>)   return new SourceItem(p);
        else                                                 return new DisplayItem(p);
    }, props);

    item->setPos(position);
    addItem(item);
    circuitEdited();
    return item;
}

ComponentItem::EditResult CircuitScene::editComponent(ComponentItem* item, const ItemProps& props)
{
    if (isLocked()) return std::unexpected(tr("Pause the simulation to edit the circuit."));

    auto result = item->setProps(props);
    if (result) circuitEdited();
    return result;
}

void CircuitScene::deleteSelection()
{
    if (isLocked()) return;

    const QList<QGraphicsItem*> selected = selectedItems();
    bool removedAny = false;

    // Wires first, so a component selected together with all of its wires goes too
    for (QGraphicsItem* item : selected) {
        if (auto* wire = dynamic_cast<WireItem*>(item)) {
            wire->setStartPort(nullptr);
            wire->setEndPort(nullptr);
            removeItem(wire);
            wire->deleteLater();
            removedAny = true;
        }
    }
    // Only a component with no wires attached can be deleted
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
    map.clear();
    m_displays.clear();
    m_lastNetValues.clear();
}

//===================== Simulation state ========================

void CircuitScene::setSimState(SimState state)
{
    if (m_simState == state) return;
    m_simState = state;
    sim_values_visible = (state != SimState::Stopped);
    update();
    emit simStateChanged(state);
}

void CircuitScene::startOrResumeSim()
{
    if (m_simState == SimState::Running) return;

    if (m_simState == SimState::Paused) {
        // Nothing was edited since Pause (an edit would have ended the run), so just carry on
        cancelWireConnection();
        setSimState(SimState::Running);
        emit setSimRunningSIG(true);
        return;
    }

    cancelWireConnection();
    ExportGraph graph = buildGraph();
    graph.runId = ++m_runId;
    setSimState(SimState::Running);
    emit startSimSIG(graph);
}

void CircuitScene::pauseSim()
{
    if (m_simState != SimState::Running) return;
    setSimState(SimState::Paused);
    emit setSimRunningSIG(false);
}

void CircuitScene::stopSim()
{
    if (m_simState == SimState::Stopped) return;

    ++m_runId;                 // results of this run that are still on their way are now ignored
    emit stopSimSIG();
    map.clear();
    m_displays.clear();
    m_lastNetValues.clear();
    resetValues();
    setSimState(SimState::Stopped);
}

void CircuitScene::circuitEdited()
{
    // The frozen values describe the circuit as it was; once it changes they are meaningless
    if (m_simState == SimState::Paused) stopSim();
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
        else if (auto* source = dynamic_cast<SourceItem*>(item)) source->setIdx(0);
        else if (auto* display = dynamic_cast<DisplayItem*>(item)) displays.append(display);
    }
    for (DisplayItem* display : std::as_const(displays)) display->updateValue();   // after their pins were cleared
}

void CircuitScene::setToolProps(const ItemProps& props)
{
    m_toolDefaults[props.index()] = props;
    m_activeTool = props.index();
    emit toolChanged();
}

void CircuitScene::selectTool(std::size_t kind)
{
    if (kind >= m_toolDefaults.size()) return;
    clearSelection();            // picking a tool means "I want to place something new"
    m_activeTool = kind;
    emit toolChanged();
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
    for (QGraphicsItem* item : items()) {
        PortItem* port = dynamic_cast<PortItem*>(item);
        if (port) {
            port->setHighlighted(false);
        }
    }
}

void CircuitScene::startWireConnection(QPointF startPoint)
{
    // Find nearest port for magnetic snapping
    PortItem* startPort = findNearestPort(startPoint);

    m_connectingWire = true;

    if (startPort) {
        m_wireStartPoint = startPort->mapToScene(QPointF(0, 0));
        m_currWireStartPort = startPort;
        startPort->setHighlighted(true);
    }
    else {
        m_wireStartPoint = startPoint;
        m_currWireStartPort = nullptr;
    }

    m_currWire = new WireItem(m_wireStartPoint, m_wireStartPoint);
    m_currWire->setPen(QPen(canvasTheme().drag, 2, Qt::DashLine));
    addItem(m_currWire);
}

void CircuitScene::updateWireConnection(QPointF currentPoint)
{
    if (m_connectingWire && m_currWire) {
        // Find nearest port for magnetic snapping
        PortItem* nearPort = findNearestPort(currentPoint);

        // Clear previous highlights
        for (QGraphicsItem* item : items()) {
            if (PortItem* port = dynamic_cast<PortItem*>(item)) {
                if (port != m_currWireStartPort) {
                    port->setHighlighted(false);
                }
            }
        }

        QPointF endPoint = currentPoint;
        if (nearPort && nearPort != m_currWireStartPort &&
            (!m_currWireStartPort || m_currWireStartPort->canConnectTo(nearPort))) {
            // Snap to port and highlight it
            endPoint = nearPort->mapToScene(QPointF(0, 0));
            nearPort->setHighlighted(true);
            m_currWire->setPen(QPen(canvasTheme().snap, 2.5, Qt::DashLine)); // over a pin it can connect to
        }
        else {
            m_currWire->setPen(QPen(canvasTheme().drag, 2, Qt::DashLine));
        }

        m_currWire->setStartPos(m_wireStartPoint);
        m_currWire->setStartPos(endPoint);
    }
}

void CircuitScene::finishWireConnection(QPointF endPoint)
{
    // A drag that began before the simulation started must not add a wire to the running circuit
    if (isLocked()) {
        cancelWireConnection();
        return;
    }

    if (m_connectingWire && m_currWire) {
        PortItem* endPort = findNearestPort(endPoint);

        if (endPort && m_currWireStartPort && endPort->canConnectTo(m_currWireStartPort)
            && endPort->getPortType() == PortType::IN && endPort->getConnections().isEmpty()) {
            // Set the ports
            m_currWire->setStartPort(m_currWireStartPort);
            m_currWire->setEndPort(endPort);

            // Add connections to ports
            m_currWireStartPort->addConnection(m_currWire);
            endPort->addConnection(m_currWire);

            // Connect position change signals to wire update
            QGraphicsObject* startGate = m_currWireStartPort->getParent();
            QGraphicsObject* endGate = endPort->getParent();

            if (startGate) {
                connect(startGate,&QGraphicsObject::xChanged,m_currWire,&WireItem::updateWirePosition);
                connect(startGate,&QGraphicsObject::yChanged,m_currWire,&WireItem::updateWirePosition);
            }

            if (endGate) {
                connect(endGate,&QGraphicsObject::xChanged,m_currWire,&WireItem::updateWirePosition);
                connect(endGate,&QGraphicsObject::yChanged,m_currWire,&WireItem::updateWirePosition);
            }
            // Set final wire appearance
            m_currWire->setPen(QPen(canvasTheme().wire, 2));
            m_currWire->updateWirePosition();
            circuitEdited();
        } else {
            // Remove invalid wire
            removeItem(m_currWire);
            delete m_currWire;
        }

        // Clean up
        clearHighlights();
        m_connectingWire = false;
        m_currWire = nullptr;
        m_currWireStartPort = nullptr;
    }
}

ExportGraph CircuitScene::buildGraph()
{
    ExportGraph graph;
    graph.clear();
    map.clear();

    // Collections to track items and assign IDs
    QList<GateItem*> gateItems;
    QList<SourceItem*> sourceItems;
    QList<WireItem*> wireItems;
    QList<RegisterItem*> registerItems;
    QList<MuxItem*> muxItems;
    QList<DisplayItem*> displayItems;

    for (QGraphicsItem* item : items()) {
        if (WireItem* wire = dynamic_cast<WireItem*>(item)) {
            if (wire->isConnected()) wireItems.push_back(wire);   // ignore a wire that is not attached at both ends
        }
        else if (GateItem* gate = dynamic_cast<GateItem*>(item)) {
            gateItems.push_back(gate);
        }
        else if (SourceItem* source = dynamic_cast<SourceItem*>(item)) {
            sourceItems.push_back(source);
        }
        else if (RegisterItem* reg = dynamic_cast<RegisterItem*>(item)) {
            registerItems.push_back(reg);
        }
        else if (MuxItem* mux = dynamic_cast<MuxItem*>(item)) {
            muxItems.push_back(mux);
        }
        else if (DisplayItem* display = dynamic_cast<DisplayItem*>(item)) {
            displayItems.push_back(display);
        }
    }

    m_displays.clear();
    for (DisplayItem* display : displayItems) {m_displays.push_back(display);}
    m_lastNetValues.clear();   // net ids are reassigned below, so force a full repaint on the next result

    // wire,net mapping
    QHash<PortItem*, u32> outputPortToNet;
    u32 netCounter = 1;

    for (WireItem* wire : wireItems) {
        PortItem* startPort = wire->getStartPort();
        // Skip display output wires in first pass
        if(dynamic_cast<DisplayItem*>(startPort->getParent())) {continue;}

        // If this output port doesn't have a net ID yet, assign one
        if (!outputPortToNet.contains(startPort)) {outputPortToNet[startPort] = netCounter++;}

        // All wires from the same output port get the same net ID
        u32 netId = outputPortToNet[startPort];
        map.wire2net[wire] = netId;

        // Store wire for each net (for UI mapping)
        map.net2wire[netId].push_back(wire);
    }
    // SECOND PASS: Process display output wires (inputs are already mapped)
    for (WireItem* wire : wireItems) {
        PortItem* startPort = wire->getStartPort();

        // Only handle display output ports
        auto* display = dynamic_cast<DisplayItem*>(startPort->getParent());
        if(!display) {
            continue; // Already processed in first pass
        }

        // Get the index of this output port
        int outputIndex = display->getOutputPorts().indexOf(startPort);

        if(outputIndex != -1 && outputIndex < display->getInputPorts().size()) {
            // Get the corresponding input port
            PortItem* matchedInputPort = display->getInputPorts().at(outputIndex);

            // Check if the matched input port has a connection
            if(!matchedInputPort->getConnections().isEmpty()) {
                WireItem* matchedInputWire = matchedInputPort->getConnections().at(0);

                // Now the input wire SHOULD have a net ID from first pass
                if(map.wire2net.contains(matchedInputWire)) {
                    u32 netId = map.wire2net[matchedInputWire];
                    map.wire2net[wire] = netId;
                    map.net2wire[netId].push_back(wire);
                } else {
                    // Fallback: input not connected, assign new net
                    if (!outputPortToNet.contains(startPort)) {
                        outputPortToNet[startPort] = netCounter++;
                    }
                    u32 netId = outputPortToNet[startPort];
                    map.wire2net[wire] = netId;
                    map.net2wire[netId].push_back(wire);
                }
            }
            else{
                map.wire2net[wire] = 0;
                map.net2wire[0].push_back(wire);
            }
        }
    }
    std::vector<Gate> gates;
    std::vector<Source> sources;
    std::vector<Register> registers;
    std::vector<u32> gateInputs;

    auto getNetId = [&](PortItem* port) -> u32 {
        if (!port || port->getConnections().isEmpty()) {
            return 0;
        }
        return map.wire2net[port->getConnections().at(0)];
    };

    // Wire,Gate Mapping
    for (int gateItemIdx = 0; gateItemIdx < gateItems.size(); gateItemIdx++)
    {
        auto* gateItem = gateItems[gateItemIdx];

        // Store the gate index for this GateItem
        map.gate2Idx[gateItem] = gateItemIdx;
        map.Idx2gate[gateItemIdx] = gateItem;

        QList<u32> inputnets;
        for (auto port:gateItem->getInputPorts()) {
            inputnets.append(getNetId(port));
        }

        u32 outNet = 0;
        if (!gateItem->getOutputPort()->getConnections().isEmpty()) {
            WireItem* outwire = gateItem->getOutputPort()->getConnections().at(0);
            outNet = map.wire2net[outwire];
        }

        for (auto inputnet : inputnets) { gateInputs.push_back(inputnet);};

        gates.push_back(Gate(gateItem->getGateType(), gateInputs, outNet));
        gateInputs.clear();
    }

    //Wire, Source Mapping
    u32 outNet, inNet2, inNet, readEnbNet, clkNet;
    for (int sourceItemIdx = 0; sourceItemIdx < sourceItems.size(); sourceItemIdx++)
    {
        auto* sourceItem = sourceItems[sourceItemIdx];

        // Store the starting sim source index for this SourceItem
        int startingSimIndex = sources.size();
        map.source2Idx[sourceItem] = startingSimIndex;
        map.Idx2source[startingSimIndex] = sourceItem;

        if(std::holds_alternative<QList<bool>>(sourceItem->getValues())){
            // Single output port, cycling through bool values
            outNet = 0;
            if (!sourceItem->getOutputPorts().at(0)->getConnections().isEmpty()) {
                WireItem* outwire = sourceItem->getOutputPorts().at(0)->getConnections().at(0);
                outNet = map.wire2net[outwire];
            }

            std::vector<bool> cycleValues;
            for (auto value : std::get<QList<bool>>(sourceItem->getValues())) {
                cycleValues.push_back(value);
            }
            sources.push_back(Source(outNet, cycleValues));
        }
        else if(std::holds_alternative<QList<int>>(sourceItem->getValues())){
            // Multiple output ports (one per bit), cycling through int values
            const auto intValues = std::get<QList<int>>(sourceItem->getValues());

            // For each bit position (output port)
            for (int bitIndex = 0; bitIndex < sourceItem->getNumOutputPorts(); bitIndex++)
            {
                outNet = 0;
                if (!sourceItem->getOutputPorts().at(bitIndex)->getConnections().isEmpty()) {
                    WireItem* outwire = sourceItem->getOutputPorts().at(bitIndex)->getConnections().at(0);
                    outNet = map.wire2net[outwire];
                }

                // Extract this bit from each int value in the cycle
                std::vector<bool> cycleValues;
                for (int value : intValues) {
                    bool bitValue = (value >> bitIndex) & 1;
                    cycleValues.push_back(bitValue);
                }

                sources.push_back(Source(outNet, cycleValues));
            }
        }
    }
    //Wire, Mux Mapping
    std::vector<Mux> muxes;
    for (auto* muxItem : muxItems)
    {
        std::vector<u32> dataInputNets;
        std::vector<u32> addressInputNets;
        outNet = 0;

        for (auto port : muxItem->getInputPorts()) {
            dataInputNets.push_back(getNetId(port));
        }

        for (auto port : muxItem->getAddressPorts()) {
            addressInputNets.push_back(getNetId(port));
        }

        if (!muxItem->getOutputPort()->getConnections().isEmpty()) {
            WireItem* outwire = muxItem->getOutputPort()->getConnections()[0];
            outNet = map.wire2net[outwire];
        }

        muxes.push_back(Mux(dataInputNets, addressInputNets, outNet));
    }

    for (int regItemIdx = 0; regItemIdx < registerItems.size(); regItemIdx++)
    {
        auto* regItem = registerItems[regItemIdx];

        map.reg2Idx[regItem] = regItemIdx;
        map.Idx2reg[regItemIdx] = regItem;

        clkNet = getNetId(regItem->getClkPort());
        readEnbNet = getNetId(regItem->getReadEnablePort());
        outNet = getNetId(regItem->getOutputPort());
        inNet = getNetId(regItem->getInputPort());
        inNet2 = getNetId(regItem->getInputPortTwo());
        getNetId(regItem->getInputPortTwo());
        registers.push_back(Register(regItem->getRegType(), inNet, inNet2, outNet, clkNet,readEnbNet));
    }

    graph.gates = gates;
    graph.sources = sources;
    graph.registers = registers;
    graph.muxes = muxes;
    graph.numNets = netCounter - 1;    // nets are numbered 1..netCounter-1; net 0 means "not connected"
    return graph;
}

void CircuitScene::receiveResult(const SimResult& result)
{
    // Results travel through the event queue, so one can arrive after its run was stopped or replaced.
    // A paused run still accepts its own last results: they are the state it froze in.
    if (m_simState == SimState::Stopped || result.runId != m_runId) return;

    const auto& nets = result.netValues;
    // Size mismatch = first result of a run (cache cleared in startSim): repaint every net
    const bool fullUpdate = m_lastNetValues.size() != nets.size();

    for (size_t i = 1; i < nets.size(); i++) {
        if (!fullUpdate && m_lastNetValues[i] == nets[i]) {continue;}

        auto it = map.net2wire.find(static_cast<u32>(i));
        if (it == map.net2wire.end()) {continue;}

        const bool value = nets[i];   // true = red, false = black
        for (auto* wire : it->second) {
            wire->setValue(value);
            wire->getEndPort()->setValue(value);
            wire->getStartPort()->setValue(value);
        }
    }
    m_lastNetValues = nets;

    for (auto [source,Idx]: map.source2Idx) {
        if (Idx < result.sourcesCurrIdx.size()) {source->setIdx(result.sourcesCurrIdx[Idx]);}
    }
    for (size_t i = 0; i < result.registerValues.size(); i++) {
        auto it = map.Idx2reg.find(static_cast<u32>(i));
        if (it != map.Idx2reg.end()) {it->second->setValue(result.registerValues[i]);}
    }

    for (const auto& display : m_displays) {
        if (display) {display->updateValue();}   // QPointer is null if the display was deleted
    }
}

void CircuitScene::receiveGraph(const QHash<QString,Node> graph)
{
    qDebug() << "Received graph with" << graph.size() << "nodes";
    resetCircuit();   // stop the simulation first; it must never outlive the items it reports on

    QHash<QString,QGraphicsObject*> node2Item;

    for (auto it = graph.begin(); it != graph.end(); ++it) {
        const QString& nodeId = it.key();
        const Node& node = it.value();
        QGraphicsObject* item=nullptr;

        if( std::holds_alternative<GType>(node.type)){

            item = addComponent(GateProps{std::get<GType>(node.type), 2}, node.position);
        }
        else if( std::holds_alternative<RType>(node.type)){
            item = addComponent(RegisterProps{std::get<RType>(node.type), true, true}, node.position);
        }
        else if( std::holds_alternative<MType>(node.type)){
            item = addComponent(MuxProps{std::get<MType>(node.type), 2}, node.position);
        }
        else if( std::holds_alternative<IOType>(node.type)){
            auto IoType = std::get<IOType>(node.type);
            if (IoType == IOType::IN){
                int bitWidth = node.ports[node.id].connections.size();
                if (bitWidth==1)
                    item = addComponent(SourceProps{QList<bool>{false,true}, bitWidth}, node.position);
                else{
                    QList<int> values;
                    int maxValue = 1 << bitWidth;
                    for (int i = 0; i < maxValue; i++) {values.append(i); values.append(i);}
                    item = addComponent(SourceProps{values, bitWidth}, node.position);
                }
            }
            else{
                int bitWidth = node.ports[node.id].connections.size();
                item = addComponent(DisplayProps{bitWidth, 0}, node.position);
            }
        }
        node2Item[nodeId]=item;
    }

    QHash<int, QList<QPair<QString, QString>>> bitToInputs; // bit -> [(nodeId, portName), ...]
    QHash<int, QPair<QString, QString>> bitToOutput; // bit -> (nodeId, portName)

    for (auto it = graph.begin(); it != graph.end(); ++it) {
        const QString& nodeId = it.key();
        const Node& node = it.value();

        for (auto portIt = node.ports.begin(); portIt != node.ports.end(); ++portIt) {
            const QString& portName = portIt.key();
            const Port& port = portIt.value();

            for (int bitNum : port.connections) {
                if (port.type == PortType::IN) {
                    bitToInputs[bitNum].append({nodeId, portName});
                } else if (port.type == PortType::OUT) {
                    bitToOutput[bitNum] = {nodeId, portName};
                }
            }
        }
    }

    for (auto bitIt = bitToInputs.begin(); bitIt != bitToInputs.end(); ++bitIt) {
        int bitNum = bitIt.key();
        const QList<QPair<QString, QString>>& inputs = bitIt.value();

        if (!bitToOutput.contains(bitNum)) {
            qDebug() << "Warning: Bit" << bitNum << "has no output source";
            continue;
        }

        const auto& [outputNodeId, outputPortName] = bitToOutput[bitNum];

        QGraphicsObject* outputItem = node2Item[outputNodeId];
        PortItem* outPort =nullptr;
        QList<PortItem*> outPorts;

        if (auto ptr = dynamic_cast<GateItem*>(outputItem)){
            outPort = ptr->getOutputPort();
        }
        else if (auto ptr = dynamic_cast<MuxItem*>(outputItem)){
            outPort = ptr->getOutputPort();
        }
        else if (auto ptr = dynamic_cast<RegisterItem*>(outputItem)){
            outPort = ptr->getOutputPort();
        }
        else if(auto ptr = dynamic_cast<SourceItem*>(outputItem)){
            outPorts = ptr->getOutputPorts();
            // Find which port outputs this specific bit
            const Node& outputNode = graph[outputNodeId];
            for (auto portIt = outputNode.ports.begin(); portIt != outputNode.ports.end(); ++portIt) {
                const Port& port = portIt.value();
                if (port.type == PortType::OUT) {
                    int bitIndex = port.connections.indexOf(bitNum);
                    if (bitIndex != -1 && bitIndex < outPorts.size()) {
                        outPort = outPorts[bitIndex];
                        break;
                    }
                }
            }
        }
        else if(auto ptr = dynamic_cast<DisplayItem*>(outputItem)){
            outPorts = ptr->getOutputPorts();
            // Find which port outputs this specific bit
            const Node& outputNode = graph[outputNodeId];
            for (auto portIt = outputNode.ports.begin(); portIt != outputNode.ports.end(); ++portIt) {
                const Port& port = portIt.value();
                if (port.type == PortType::OUT) {
                    int bitIndex = port.connections.indexOf(bitNum);
                    if (bitIndex != -1 && bitIndex < outPorts.size()) {
                        outPort = outPorts[bitIndex];
                        break;
                    }
                }
            }
        }

        for (const auto& [inputNodeId, inputPortName] : inputs) {
            QGraphicsObject* inputItem = node2Item[inputNodeId];
            PortItem* inPort = nullptr;
            QList<PortItem*> inPorts;

            if (auto ptr = dynamic_cast<GateItem*>(inputItem)){
                inPorts = ptr->getInputPorts();
                const Node& inputNode = graph[inputNodeId];

                int portCounter = 0;  
                for (auto portIt = inputNode.ports.begin(); portIt != inputNode.ports.end(); ++portIt) {
                    const Port& port = portIt.value();
                    if (port.type == PortType::IN) {
                        if (port.name == inputPortName && port.connections.contains(bitNum)) {
                            if (portCounter < inPorts.size()) {
                                inPort = inPorts[portCounter];
                            }
                            break;
                        }
                        portCounter++;
                    }
                }
            }
            else if (auto ptr = dynamic_cast<MuxItem*>(inputItem)){
                inPorts = ptr->getInputPorts();
                QList<PortItem*> addrPorts = ptr->getAddressPorts();

                const Node& inputNode = graph[inputNodeId];

                // Determine if this is an address port by checking the port name
                bool isAddressPort = inputPortName.contains("S") || inputPortName.contains("addr") || inputPortName.contains("sel");

                int portCounter = 0;  
                for (auto portIt = inputNode.ports.begin(); portIt != inputNode.ports.end(); ++portIt) {
                    const Port& port = portIt.value();
                    if (port.type == PortType::IN) {
                        bool thisIsAddress = port.name.contains("S") || port.name.contains("addr") || port.name.contains("sel");

                        if (thisIsAddress == isAddressPort) {
                            if (port.name == inputPortName && port.connections.contains(bitNum)) {
                                if (isAddressPort && portCounter < addrPorts.size()) {
                                    inPort = addrPorts[portCounter];
                                } else if (!isAddressPort && portCounter < inPorts.size()) {
                                    inPort = inPorts[portCounter];
                                }
                                break;
                            }
                            portCounter++;  
                        }
                    }
                }
            }
            else if(auto ptr = dynamic_cast<DisplayItem*>(inputItem)){
                inPorts = ptr->getInputPorts();
                const Node& inputNode = graph[inputNodeId];

                for (auto portIt = inputNode.ports.begin(); portIt != inputNode.ports.end(); ++portIt) {
                    const Port& port = portIt.value();
                    if (port.type == PortType::IN) {
                        if (port.name == inputPortName && port.connections.contains(bitNum)) {
                            int portIndex = port.connections.indexOf(bitNum);
                            inPort = inPorts[portIndex];
                            break;
                        }
                    }
                }
            }
            else if (auto ptr = dynamic_cast<RegisterItem*>(inputItem)){
                const Node& inputNode = graph[inputNodeId];
                for (auto portIt = inputNode.ports.begin(); portIt != inputNode.ports.end(); ++portIt) {
                    const Port& port = portIt.value();
                    if (port.type == PortType::IN && port.name == inputPortName) {
                        // Map port name to the correct register port
                        if (inputPortName.contains("D") || inputPortName.toLower().contains("data")) {
                            inPort = ptr->getInputPort();
                        }
                        else if (inputPortName.contains("C") || inputPortName.toLower().contains("c")) {
                            inPort = ptr->getClkPort();
                        }
                        else if (inputPortName.contains("E") || inputPortName.toLower().contains("enable")) {
                            inPort = ptr->getReadEnablePort();
                        }
                        else if (inputPortName.contains("R") || inputPortName.toLower().contains("reset")) {
                            inPort = ptr->getInputPortTwo();
                        }
                        break;
                    }
                }
            }

            if (!inPort) {
                qDebug() << "Warning: Could not find input port" << inputPortName << "on node" << inputNodeId;
                continue;
            }

            QPointF outPos = outPort->mapToScene(QPointF(0, 0));
            QPointF endPos = inPort->mapToScene(QPointF(0, 0));

            WireItem* wire = new WireItem(outPos, endPos);
            wire->setStartPort(outPort);
            wire->setEndPort(inPort);

            outPort->addConnection(wire);
            inPort->addConnection(wire);

            addItem(wire);

            connect(outputItem,&QGraphicsObject::xChanged,wire,&WireItem::updateWirePosition);
            connect(outputItem,&QGraphicsObject::yChanged,wire,&WireItem::updateWirePosition);

            connect(inputItem,&QGraphicsObject::xChanged,wire,&WireItem::updateWirePosition);
            connect(inputItem,&QGraphicsObject::yChanged,wire,&WireItem::updateWirePosition);

            wire->updateWirePosition();

            qDebug() << "Created wire: bit" << bitNum << "from" << outputNodeId << outputPortName
                     << "to" << inputNodeId << inputPortName;
        }
    }

    return;
}

void CircuitScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {

    // Placing and wiring change the circuit, so they are off while the simulation runs
    if (isLocked() && (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)) {
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

    update();
}

//=====================QGraphicsView========================

CircuitCanvas::CircuitCanvas(QWidget* parent)
    : QGraphicsView(parent), m_middleMousePressed(false)
{
    m_scene = new CircuitScene(this);
    setScene(m_scene);
    scenePtr = m_scene;

    // Configure view
    setDragMode(QGraphicsView::RubberBandDrag);
    setRenderHint(QPainter::Antialiasing);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
}

void CircuitCanvas::setThemeColor(QColor CanvasTheme::* field, const QColor& color)
{
    canvasTheme().*field = color;
    m_scene->update();
}

void CircuitCanvas::keyPressEvent(QKeyEvent* event)
{
    // Track modifier keys
    m_ctrlPressed = event->modifiers() & Qt::ControlModifier;
    m_shiftPressed = event->modifiers() & Qt::ShiftModifier;

    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
    {
        m_scene->deleteSelection();
        event->accept();
        break;
    }
    default:
        // Pass unhandled keys to parent
        QGraphicsView::keyPressEvent(event);
        break;
    }
}


void CircuitCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Check if we clicked on an item
        QGraphicsItem* itemUnderMouse = itemAt(event->pos());
        
        if (!itemUnderMouse) {
            // Clicked on empty space - start camera dragging
            m_middleMousePressed = true;
            m_lastPanPoint = event->pos();
            m_panStartPoint = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        // If we clicked on an item, fall through to let QGraphicsView handle it
    }

    QGraphicsView::mousePressEvent(event);
}

void CircuitCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_middleMousePressed) {

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
    if (event->button() == Qt::LeftButton && m_middleMousePressed) {

        m_middleMousePressed = false;
        setCursor(Qt::ArrowCursor);

        // A click on empty canvas without dragging deselects everything
        if ((event->pos() - m_panStartPoint).manhattanLength() < QApplication::startDragDistance()) {
            m_scene->clearSelection();
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void CircuitCanvas::wheelEvent(QWheelEvent* event)
{
    // Get current transform and scene rect
    QTransform currentTransform = transform();
    double currentScale = currentTransform.m11();
    QRectF sceneRect = m_scene->sceneRect();

    const double scaleFactor = 1.15;

    // Calculate what the new scale would be
    double newScale = currentScale;
    if (event->angleDelta().y() > 0) {
        newScale *= scaleFactor;  // Zoom in
    } else {
        newScale /= scaleFactor;  // Zoom out
    }

    // Check if zooming out would make the view larger than the scene
    if (newScale < currentScale) {  
        QRectF viewportRect = viewport()->rect();

        double newViewWidth = viewportRect.width() / newScale;
        double newViewHeight = viewportRect.height() / newScale;

        // Don't zoom out if the view would become larger than the scene
        if (newViewWidth >= sceneRect.width() || newViewHeight >= sceneRect.height()) {
            return;  
        }
    }

    // Apply the zoom
    if (event->angleDelta().y() > 0) {
        scale(scaleFactor, scaleFactor);
    } else {
        scale(1.0 / scaleFactor, 1.0 / scaleFactor);
    }
}

