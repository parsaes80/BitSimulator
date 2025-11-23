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
    setSceneRect(0, 0, 16000, 10000); // Large canvas

    // Force full scene update on any change
    connect(this, &QGraphicsScene::changed, this, [this]() { update(); });

    // Force update when selection changes
    connect(this, &QGraphicsScene::selectionChanged, this, [this]() { update(); });
}

void CircuitScene::addGate(GType gateType, QPointF position)
{
    GateItem* gate = new GateItem(gateType,m_numInputs);
    gate->setPos(position);
    addItem(gate);
}
void CircuitScene::addSource(QPointF position)
{
    SourceItem* source = new SourceItem(m_srcValues);
    source->setPos(position);
    addItem(source);
}
void CircuitScene::addRegister(RType RegType, QPointF position) {
    RegisterItem* Reg = new RegisterItem(RegType,m_isFlipFlop,m_hasEnable);
    Reg->setPos(position);
    addItem(Reg);
}
void CircuitScene::addMux(MType MuxType, QPointF position) {
    MuxItem* Mux = new MuxItem(MuxType,m_numInputs);
    Mux->setPos(position);
    addItem(Mux);
}
void CircuitScene::addDisplay(int numIn,int numOut, QPointF position) {
    DisplayItem* Display = new DisplayItem(numIn,numOut);
    Display->setPos(position);
    addItem(Display);
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
        
        // Store the register index for this RegisterItem
        map.reg2Idx[regItem] = regItemIdx;
        map.Idx2reg[regItemIdx] = regItem;
        
        outNet = 0;inNet2 = 0; inNet = 0; readEnbNet = 0; clkNet = 0;
        clkNet = getNetId(regItem->getClkPort());
        readEnbNet = getNetId(regItem->getReadEnablePort());
        outNet = getNetId(regItem->getOutputPort());
        inNet = getNetId(regItem->getInputPort()); 
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

void CircuitScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    // Handle our custom cases first
    if (event->button() == Qt::LeftButton) {
        QGraphicsItem* clickedItem = itemAt(event->scenePos(), QTransform());
        if (!clickedItem) {
            // Only add items if we didn't click on an existing item
            std::visit([&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;

                if constexpr (std::is_same_v<T, GType>) {
                    addGate(arg, event->scenePos());
                }
                else if constexpr (std::is_same_v<T, RType>) {
                    addRegister(arg, event->scenePos());
                }
                else if constexpr (std::is_same_v<T, MType>) {
                    addMux(arg,event->scenePos());
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

void CircuitCanvas::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasText() && event->mimeData()->text().startsWith("gate:")) {
        event->acceptProposedAction();
    }
}

void CircuitCanvas::dropEvent(QDropEvent* event)
{
    QString gateData = event->mimeData()->text();
    if (gateData.startsWith("gate:")) {
        int gateTypeInt = gateData.mid(5).toInt();
        GType gateType = static_cast<GType>(gateTypeInt);
        addGate(gateType, event->position().toPoint());
        event->acceptProposedAction();
    }
}
