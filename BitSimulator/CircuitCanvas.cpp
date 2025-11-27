#include "CircuitCanvas.h"
#include <QHash> // Add this line
#include <QMouseEvent>
#include <QQueue> // Add this line
#include <QSet>   // Add this line
#include <QKeyEvent>
#include <QDebug>
#include <qscrollbar.h>
#include <vector>
extern GlobalMap map;

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

QGraphicsObject* CircuitScene::addGate(GType gateType,int numIn, QPointF position)
{
    GateItem* gate = new GateItem(gateType,numIn);
    gate->setPos(position);
    addItem(gate);
    return gate;
}
QGraphicsObject* CircuitScene::addSource(QPointF position)
{
    SourceItem* source = new SourceItem(m_srcValues);
    source->setPos(position);
    addItem(source);
    return source;
}
QGraphicsObject* CircuitScene::addSource(std::variant<QList<bool>,QList<int>> srcValues,int numOut,QPointF position)
{
    SourceItem* source = new SourceItem(srcValues,numOut);
    source->setPos(position);
    addItem(source);
    return source;
}
QGraphicsObject* CircuitScene::addRegister(RType RegType, QPointF position) {
    RegisterItem* Reg = new RegisterItem(RegType,m_isFlipFlop,m_hasEnable);
    Reg->setPos(position);
    addItem(Reg);
    return Reg;
}
QGraphicsObject* CircuitScene::addRegister(RType RegType,bool hasEnable, QPointF position) {
    RegisterItem* Reg = new RegisterItem(RegType,true,hasEnable);
    Reg->setPos(position);
    addItem(Reg);
    return Reg;
}
QGraphicsObject* CircuitScene::addMux(MType MuxType,int numIn, QPointF position) {
    MuxItem* Mux = new MuxItem(MuxType,m_numInputs);
    Mux->setPos(position);
    addItem(Mux);
    return Mux;
}
QGraphicsObject* CircuitScene::addDisplay(int numIn,int numOut, QPointF position) {
    DisplayItem* Display = new DisplayItem(numIn,numOut);
    Display->setPos(position);
    addItem(Display);
    return Display;
}
// Add to CircuitScene
PortItem* CircuitScene::findNearestPort(const QPointF& scenePos, double threshold)
{
    PortItem* nearestPort = nullptr;
    double minDistance = threshold;

    // Check all items in the scene
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
    // Find all PortItem objects and unhighlight them
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
    m_currWire->setPen(QPen(Qt::red, 2, Qt::DashLine));
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
            m_currWire->setPen(QPen(Qt::green, 3, Qt::DashLine)); // Green when near valid port
        }
        else {
            m_currWire->setPen(QPen(Qt::red, 2, Qt::DashLine)); // Red otherwise
        }

        m_currWire->setStartPos(m_wireStartPoint);
        m_currWire->setStartPos(endPoint);
    }
}

void CircuitScene::finishWireConnection(QPointF endPoint)
{
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
            m_currWire->setPen(QPen(Qt::black, 2));
            m_currWire->updateWirePosition();
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

void CircuitScene::startSim()
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
            wireItems.push_back(wire);
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
            PortItem* matchedInputPort = display->getInputPorts()[outputIndex];

            // Check if the matched input port has a connection
            if(!matchedInputPort->getConnections().isEmpty()) {
                WireItem* matchedInputWire = matchedInputPort->getConnections()[0];

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
        return map.wire2net[port->getConnections()[0]];
    };

    // Wire,Gate Mapping
    u32 gateInputindex = 0;
    for (int gateItemIdx = 0; gateItemIdx < gateItems.size(); gateItemIdx++)
    {
        auto* gateItem = gateItems[gateItemIdx];

        // Store the gate index for this GateItem
        map.gate2Idx[gateItem] = gateItemIdx;
        map.Idx2gate[gateItemIdx] = gateItem;
        QList<u32> inputnets;
        for (auto port:gateItem->getInputPorts()) {
            if (!port->getConnections().isEmpty()) {
                WireItem* inputWire = port->getConnections()[0];
                u32 netID = map.wire2net[inputWire];
                inputnets.append(netID);
            }
            else {
                inputnets.append(0);
            }
        }

        u32 outNet = 0;
        if (!gateItem->getOutputPort()->getConnections().isEmpty()) {
            WireItem* outwire = gateItem->getOutputPort()->getConnections()[0];
            outNet = map.wire2net[outwire];
        }

        for (auto inputnet : inputnets) { gateInputs.push_back(inputnet);};

        gates.push_back(Gate(gateItem->getGateType(), gateInputindex, outNet, inputnets.size()));
        gateInputindex += inputnets.size();
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
            if (!sourceItem->getOutputPorts()[0]->getConnections().isEmpty()) {
                WireItem* outwire = sourceItem->getOutputPorts()[0]->getConnections()[0];
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
                if (!sourceItem->getOutputPorts()[bitIndex]->getConnections().isEmpty()) {
                    WireItem* outwire = sourceItem->getOutputPorts()[bitIndex]->getConnections()[0];
                    outNet = map.wire2net[outwire];
                }

                // Extract this bit from each int value in the cycle
                std::vector<bool> cycleValues;
                for (int value : intValues) {
                    // Extract bit at bitIndex position (LSB = bit 0)
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

        // Get data input nets (from getInputPorts which returns data ports)
        for (auto port : muxItem->getInputPorts()) {
            if (!port->getConnections().isEmpty()) {
                WireItem* inputWire = port->getConnections()[0];
                u32 netID = map.wire2net[inputWire];
                dataInputNets.push_back(netID);
            }
            else {
                dataInputNets.push_back(0);
            }
        }

        // Get address input nets (from getAddressPorts which returns address/select ports)
        for (auto port : muxItem->getAddressPorts()) {
            if (!port->getConnections().isEmpty()) {
                WireItem* inputWire = port->getConnections()[0];
                u32 netID = map.wire2net[inputWire];
                addressInputNets.push_back(netID);
            }
            else {
                addressInputNets.push_back(0);
            }
        }

        // Get output net
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
        inNet2 = (regItem->getRegType()!= RType::D)?getNetId(regItem->getInputPortTwo()):0; // HACK FIX NEED TO CHANGE LATER
        getNetId(regItem->getInputPortTwo());
        registers.push_back(Register(regItem->getRegType(), inNet, inNet2, outNet, clkNet,readEnbNet));
    }

    graph.gates = gates;
    graph.sources = sources;
    graph.registers = registers;
    graph.muxes = muxes;
    graph.gateInputs = gateInputs;
    emit startSimSIG(graph);
}

void CircuitScene::receiveResult(SimResult result)
{
    for (int i = 1; i < result.netValues.size();i++) {
        std::vector<WireItem*> wires = map.net2wire[i];
        for (auto* wire : wires) {
            if (result.netValues[i]) {
                wire->setValue(true);    // High signal = red
                wire->getEndPort()->setValue(true);
                wire->getStartPort()->setValue(true);
            }
            else {
                wire->setValue(false);     // Low signal = black
                wire->getEndPort()->setValue(false);
                wire->getStartPort()->setValue(false);
            }
        }
    }

    for (auto [source,Idx]: map.source2Idx) {
        source->setIdx(result.sourcesCurrIdx[Idx]);
    }
    for (int i = 0; i < result.registerValues.size(); i++) {
        auto reg = map.Idx2reg[i];
        reg->setValue(result.registerValues[i]);
    }

    for (QGraphicsItem* item : items()) {
        if (DisplayItem* display = dynamic_cast<DisplayItem*>(item)) {
            display->updateValue();
        }
    }
}

void CircuitScene::receiveGraph(const QHash<QString,Node> graph)
{
    qDebug() << "Received graph with" << graph.size() << "nodes";
    clear();

    QHash<QString,QGraphicsObject*> node2Item;

    for (auto it = graph.begin(); it != graph.end(); ++it) {
        const QString& nodeId = it.key();
        const Node& node = it.value();
        QGraphicsObject* item=nullptr;

        if( std::holds_alternative<GType>(node.type)){

            item = addGate(std::get<GType>(node.type),2,node.position);
        }
        else if( std::holds_alternative<RType>(node.type)){
            item = addRegister(std::get<RType>(node.type),false,node.position);
        }
        else if( std::holds_alternative<MType>(node.type)){
            item = addMux(std::get<MType>(node.type),2,node.position);
        }
        else if( std::holds_alternative<IOType>(node.type)){
            auto IoType = std::get<IOType>(node.type);
            if (IoType == IOType::IN){
                int bitWidth = node.ports[node.id].connections.size();
                if (bitWidth==1)
                    item = addSource(QList<bool>{false,true},bitWidth,node.position);
                else{
                    QList<int> values;
                    int maxValue = 1 << bitWidth;
                    for (int i = 0; i < maxValue; i++) {values.append(i); values.append(i);}
                    item = addSource(values,bitWidth,node.position);
                }
            }
            else{
                int bitWidth = node.ports[node.id].connections.size();
                item = addDisplay(bitWidth,0,node.position);
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
                    // Check if this port contains our bitNum
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
                    // Check if this port contains our bitNum
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

                int portCounter = 0;  // Count which input port we're on
                for (auto portIt = inputNode.ports.begin(); portIt != inputNode.ports.end(); ++portIt) {
                    const Port& port = portIt.value();
                    if (port.type == PortType::IN) {
                        if (port.name == inputPortName && port.connections.contains(bitNum)) {
                            // This is the right port!
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

                int portCounter = 0;  // Count which port we're on (within its category)
                for (auto portIt = inputNode.ports.begin(); portIt != inputNode.ports.end(); ++portIt) {
                    const Port& port = portIt.value();
                    if (port.type == PortType::IN) {
                        // Check if this port matches our category (address vs data)
                        bool thisIsAddress = port.name.contains("S") || port.name.contains("addr") || port.name.contains("sel");

                        if (thisIsAddress == isAddressPort) {
                            // We're in the right category, check if this is our port
                            if (port.name == inputPortName && port.connections.contains(bitNum)) {
                                if (isAddressPort && portCounter < addrPorts.size()) {
                                    inPort = addrPorts[portCounter];
                                } else if (!isAddressPort && portCounter < inPorts.size()) {
                                    inPort = inPorts[portCounter];
                                }
                                break;
                            }
                            portCounter++;  // Only increment for ports in the same category
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
                        else if (inputPortName.contains("EN") || inputPortName.toLower().contains("enable")) {
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



    // QList<QGraphicsObject*> components;
    // for (QGraphicsItem* item : items()) {
    //     if (GateItem* gate = dynamic_cast<GateItem*>(item)) {
    //         components.push_back(gate);
    //     }
    //     else if (SourceItem* source = dynamic_cast<SourceItem*>(item)) {
    //         components.push_back(source);
    //     }
    //     else if (RegisterItem* reg = dynamic_cast<RegisterItem*>(item)) {
    //         components.push_back(reg);
    //     }
    //     else if (MuxItem* mux = dynamic_cast<MuxItem*>(item)) {
    //         components.push_back(mux);
    //     }
    //     else if (DisplayItem* display = dynamic_cast<DisplayItem*>(item)) {
    //         components.push_back(display);
    //     }
    // }
    // // Group items by vertical proximity (within 100 pixels)
    // QList<QList<QGraphicsObject*>> groups;
    // QSet<QGraphicsObject*> processed;

    // for (auto* comp : components) {
    //     if (processed.contains(comp)) continue;

    //     QList<QGraphicsObject*> group;
    //     group.append(comp);
    //     processed.insert(comp);

    //     qreal compY = comp->pos().x();

    //     // Find all items at similar Y position
    //     for (auto* other : components) {
    //         if (processed.contains(other)) continue;

    //         qreal otherY = other->pos().x();
    //         if (qAbs(compY - otherY) < 100) {  // Within 100 pixels vertically
    //             group.append(other);
    //             processed.insert(other);
    //         }
    //     }

    //     if (group.size() > 1) {
    //         groups.append(group);
    //     }
    // }

    // // Process each group
    // for (auto& group : groups) {
    //     // Sort group by Y position (top to bottom)
    //     std::sort(group.begin(), group.end(), [](QGraphicsObject* a, QGraphicsObject* b) {
    //         return a->pos().y() < b->pos().y();
    //     });

    //     int groupSize = group.size();

    //     if (groupSize == 1) {
    //         continue; // Skip single items
    //     }

    //     // Apply gradient shifts
    //     for (int i = 0; i < groupSize; i++) {
    //         QGraphicsObject* obj = group[i];

    //         qreal shift;
    //         if (groupSize == 2) {
    //             // Special case: only 2 items
    //             shift = (i == 0) ? -5.0 : 5.0;
    //         } else {
    //             // 3 or more items: linear interpolation from -5 to +5
    //             // Middle item(s) should be close to 0
    //             shift = -200.0 + (100.0 * i / (groupSize - 1));
    //         }

    //         // Apply the shift
    //         QPointF currentPos = obj->pos();
    //         obj->setPos(currentPos.x() + shift, currentPos.y());
    //     }
    // }

    // // Update all wires after moving components
    // for (QGraphicsItem* wireItem : items()) {
    //     if (auto* wire = dynamic_cast<WireItem*>(wireItem)) {
    //         wire->updateWirePosition();
    //     }
    // }

    return;
}

void CircuitScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    // Handle our custom cases first
    if (event->button() == Qt::LeftButton) {
        QGraphicsItem* clickedItem = itemAt(event->scenePos(), QTransform());
        if (!clickedItem) {
            // Only add items if we didn't click on an existing item
            std::visit([&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;

                if constexpr (std::is_same_v<T, GType>) {
                    addGate(arg,m_numInputs, event->scenePos());
                }
                else if constexpr (std::is_same_v<T, RType>) {
                    addRegister(arg, event->scenePos());
                }
                else if constexpr (std::is_same_v<T, MType>) {
                    addMux(arg,m_numInputs,event->scenePos());
                }
                else if constexpr (std::is_same_v<T, bool>) {
                    if (arg)
                        addSource(event->scenePos());
                    else
                        addDisplay(m_numInputs,m_numOutputs,event->scenePos());
                }},
                m_nextItem);

            event->accept();
            return;
        }
    }
    else if (event->button() == Qt::RightButton) {
        // Right click starts wire connection
        startWireConnection(event->scenePos());
        event->accept();
        return;
    }

    // Pass other events to base class
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

    // Configure view
    setDragMode(QGraphicsView::RubberBandDrag);
    setRenderHint(QPainter::Antialiasing);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
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
        QList<QGraphicsItem*> selectedItems = m_scene->selectedItems();

        for (QGraphicsItem* item : selectedItems) {
            // Let the items handle their own deletion (they have keyPressEvent handlers)
            QKeyEvent deleteEvent(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
            m_scene->sendEvent(item, &deleteEvent);
        }
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
    if (event->button() == Qt::MiddleButton) {
        // Start camera dragging
        m_middleMousePressed = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event); // Call base class implementation
}

void CircuitCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_middleMousePressed) {
        // Calculate movement delta
        QPoint delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();

        // Move the viewport in the same direction as mouse movement
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
    if (event->button() == Qt::MiddleButton && m_middleMousePressed) {
        // Stop camera dragging
        m_middleMousePressed = false;
        setCursor(Qt::ArrowCursor);
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
    if (newScale < currentScale) {  // Zooming out
        QRectF viewportRect = viewport()->rect();

        // Calculate what the visible scene area would be with the new scale
        double newViewWidth = viewportRect.width() / newScale;
        double newViewHeight = viewportRect.height() / newScale;

        // Don't zoom out if the view would become larger than the scene
        if (newViewWidth >= sceneRect.width() || newViewHeight >= sceneRect.height()) {
            return;  // Prevent this zoom operation
        }
    }

    // Apply the zoom
    if (event->angleDelta().y() > 0) {
        scale(scaleFactor, scaleFactor);
    } else {
        scale(1.0 / scaleFactor, 1.0 / scaleFactor);
    }
}

