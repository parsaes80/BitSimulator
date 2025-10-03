#include "CircuitCanvas.h"
#include <QHash> // Add this line
#include <QMouseEvent>
#include <QQueue> // Add this line
#include <QSet>   // Add this line
#include <QKeyEvent>
#include <QDebug>
#include <qscrollbar.h>
#include <vector>
//===================== QGraphicsScene ========================

CircuitScene::CircuitScene(QObject* parent)
    : QGraphicsScene(parent)
    , m_connectingWire(false)
    , m_currentWire(nullptr)
{
    setSceneRect(0, 0, 2000, 2000); // Large canvas

    // Force full scene update on any change
    connect(this, &QGraphicsScene::changed, this, [this]() { update(); });

    // Force update when selection changes
    connect(this, &QGraphicsScene::selectionChanged, this, [this]() { update(); });
}

void CircuitScene::addGate(GType gateType, QPointF position)
{
    GateItem* gate = new GateItem(gateType);
    gate->setPos(position);
    addItem(gate);
}
void CircuitScene::addSource(QPointF position)
{
    SourceItem* source = new SourceItem();
    source->setPos(position);
    addItem(source);
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
        m_currentWireStartPort = startPort;
        startPort->setHighlighted(true);
    }
    else {
        m_wireStartPoint = startPoint;
        m_currentWireStartPort = nullptr;
    }

    m_currentWire = new WireItem(QLineF(m_wireStartPoint, m_wireStartPoint));
    m_currentWire->setPen(QPen(Qt::red, 2, Qt::DashLine));
    addItem(m_currentWire);
}

void CircuitScene::updateWireConnection(QPointF currentPoint)
{
    if (m_connectingWire && m_currentWire) {
        // Find nearest port for magnetic snapping
        PortItem* nearPort = findNearestPort(currentPoint);

        // Clear previous highlights
        for (QGraphicsItem* item : items()) {
            if (PortItem* port = dynamic_cast<PortItem*>(item)) {
                if (port != m_currentWireStartPort) {
                    port->setHighlighted(false);
                }
            }
        }

        QPointF endPoint = currentPoint;
        if (nearPort && nearPort != m_currentWireStartPort &&
            (!m_currentWireStartPort || m_currentWireStartPort->canConnectTo(nearPort))) {
            // Snap to port and highlight it
            endPoint = nearPort->mapToScene(QPointF(0, 0));
            nearPort->setHighlighted(true);
            m_currentWire->setPen(QPen(Qt::green, 3, Qt::DashLine)); // Green when near valid port
        }
        else {
            m_currentWire->setPen(QPen(Qt::red, 2, Qt::DashLine)); // Red otherwise
        }

        m_currentWire->setLine(QLineF(m_wireStartPoint, endPoint));
    }
}

void CircuitScene::finishWireConnection(QPointF endPoint)
{
    if (m_connectingWire && m_currentWire) {
        PortItem* endPort = findNearestPort(endPoint);

        if (endPort && m_currentWireStartPort && endPort->canConnectTo(m_currentWireStartPort)) {
            // Set the ports
            m_currentWire->setStartPort(m_currentWireStartPort);
            m_currentWire->setEndPort(endPort);

            // Add connections to ports
            m_currentWireStartPort->addConnection(m_currentWire);
            endPort->addConnection(m_currentWire);

            // Connect position change signals to wire update
            QGraphicsObject* startGate = m_currentWireStartPort->getParentGate();
            QGraphicsObject* endGate = endPort->getParentGate();

            if (startGate) {
                connect(startGate,
                        &QGraphicsObject::xChanged,
                        m_currentWire,
                        &WireItem::updateWirePosition);
                connect(startGate,
                        &QGraphicsObject::yChanged,
                        m_currentWire,
                        &WireItem::updateWirePosition);
            }

            if (endGate) {
                connect(endGate,
                        &QGraphicsObject::xChanged,
                        m_currentWire,
                        &WireItem::updateWirePosition);
                connect(endGate,
                        &QGraphicsObject::yChanged,
                        m_currentWire,
                        &WireItem::updateWirePosition);
            }

            // Set final wire appearance
            m_currentWire->setPen(QPen(Qt::black, 2));
            m_currentWire->updateWirePosition(); // Initial position update
        } else {
            // Remove invalid wire
            removeItem(m_currentWire);
            delete m_currentWire;
        }

        // Clean up
        clearHighlights();
        m_connectingWire = false;
        m_currentWire = nullptr;
        m_currentWireStartPort = nullptr;
    }
}

void CircuitScene::startSim()
{
    ExportGraph graph;
    graph.clear();

    // Collections to track items and assign IDs
    QList<GateItem*> gateItems;
    QList<SourceItem*> sourceItems;
    QList<WireItem*> wireItems;
    QHash<PortItem*, u32> portToNetMap; // Maps ports to net indices

    u32 netCounter = 0;

    qDebug() << "\n========== STARTING CIRCUIT EXPORT ==========";
    
    // Step 1: Find all items and validate connections
    for (QGraphicsItem* item : items()) {
        if (WireItem* wire = dynamic_cast<WireItem*>(item)) {
            if (wire->isConnected()) {
                wireItems.push_back(wire);
            } else {
                qDebug() << "Warning: Found disconnected wire, skipping";
            }
        }
        else if (GateItem* gate = dynamic_cast<GateItem*>(item)) {
            gateItems.push_back(gate);
        }
        else if (SourceItem* source = dynamic_cast<SourceItem*>(item)) {
            sourceItems.push_back(source);
        }
    }
    
    qDebug() << "\n=== Step 1: Items Found ===";
    qDebug() << "Wires found:" << wireItems.size();
    qDebug() << "Gates found:" << gateItems.size();
    qDebug() << "Sources found:" << sourceItems.size();
    
    // Calculate expected port count
    int expectedInputPorts = 0;
    int expectedOutputPorts = 0;
    for (GateItem* gate : gateItems) {
        switch (gate->getGateType()) {
        case GType::NOT:
            expectedInputPorts += 1;
            break;
        case GType::AND:
        case GType::OR:
        case GType::XOR:
        case GType::NAND:
        case GType::NOR:
        case GType::XNOR:
            expectedInputPorts += 2;
            break;
        }
        expectedOutputPorts += 1; // Each gate has 1 output
    }
    expectedOutputPorts += sourceItems.size(); // Each source has 1 output
    
    qDebug() << "Expected input ports:" << expectedInputPorts;
    qDebug() << "Expected output ports:" << expectedOutputPorts;
    qDebug() << "Total expected ports:" << (expectedInputPorts + expectedOutputPorts);
    qDebug() << "Wires can connect at most:" << (wireItems.size() * 2) << "ports";

    // Step 2: Build connection groups from wires efficiently
    QHash<PortItem*, QSet<PortItem*>> connectedPorts;
    QHash<WireItem*, u32> wireToNetMap;
    
    for (WireItem* wire : wireItems) {
        PortItem* startPort = wire->getStartPort();
        PortItem* endPort = wire->getEndPort();

        if (!startPort || !endPort) {
            qDebug() << "Warning: Wire with null ports found, skipping";
            continue;
        }

        connectedPorts[startPort].insert(endPort);
        connectedPorts[endPort].insert(startPort);
    }

    // Step 3: Create nets using BFS for connected port groups
    QSet<PortItem*> processedPorts;
    
    qDebug() << "\n=== Step 3: Creating nets from wires ===";
    qDebug() << "Total wires to process:" << wireItems.size();
    qDebug() << "Total ports with connections:" << connectedPorts.size();
    
    for (auto it = connectedPorts.begin(); it != connectedPorts.end(); ++it) {
        if (!processedPorts.contains(it.key())) {
            u32 currentNetId = netCounter++;
            graph.nets.push_back(Net{false, currentNetId});
            
            QQueue<PortItem*> portsToProcess;
            portsToProcess.enqueue(it.key());
            QSet<WireItem*> wiresInThisNet;
            QSet<PortItem*> portsInThisNet;

            while (!portsToProcess.isEmpty()) {
                PortItem* currentPort = portsToProcess.dequeue();
                if (processedPorts.contains(currentPort))
                    continue;

                processedPorts.insert(currentPort);
                portToNetMap[currentPort] = currentNetId;
                portsInThisNet.insert(currentPort);

                // Find wires connected to this port
                for (WireItem* wire : wireItems) {
                    if ((wire->getStartPort() == currentPort || wire->getEndPort() == currentPort) &&
                        !wiresInThisNet.contains(wire)) {
                        wiresInThisNet.insert(wire);
                        wireToNetMap[wire] = currentNetId;
                    }
                }

                // Add connected ports to queue
                for (PortItem* connectedPort : connectedPorts[currentPort]) {
                    if (!processedPorts.contains(connectedPort)) {
                        portsToProcess.enqueue(connectedPort);
                    }
                }
            }
            
            qDebug() << "  Net" << currentNetId << "created with" << wiresInThisNet.size() 
                     << "wire(s) and" << portsInThisNet.size() << "port(s)";
        }
    }
    
    qDebug() << "Nets created from wires:" << netCounter;

    // Handle isolated ports (not connected to any wire)
    qDebug() << "\n=== Step 3b: Handling isolated/unconnected ports ===";
    
    QSet<PortItem*> allPorts;
    int totalGatePorts = 0;
    int totalSourcePorts = 0;
    
    for (GateItem* gate : gateItems) {
        for (PortItem* port : gate->getInputPorts()) {
            if (port) {
                allPorts.insert(port);
                totalGatePorts++;
            }
        }
        if (PortItem* outputPort = gate->getOutputPort()) {
            allPorts.insert(outputPort);
            totalGatePorts++;
        }
    }
    for (SourceItem* source : sourceItems) {
        if (PortItem* outputPort = source->getOutputPort()) {
            allPorts.insert(outputPort);
            totalSourcePorts++;
        }
    }

    qDebug() << "Total gates:" << gateItems.size();
    qDebug() << "Total sources:" << sourceItems.size();
    qDebug() << "Total gate ports:" << totalGatePorts;
    qDebug() << "Total source ports:" << totalSourcePorts;
    qDebug() << "Total unique ports:" << allPorts.size();
    qDebug() << "Ports already in nets:" << portToNetMap.size();

    int isolatedCount = 0;
    for (PortItem* port : allPorts) {
        if (!portToNetMap.contains(port)) {
            u32 isolatedNetId = netCounter++;
            graph.nets.push_back(Net{false, isolatedNetId});
            portToNetMap[port] = isolatedNetId;
            
            // Determine port type for better debugging
            QString portInfo = "unknown";
            if (PortItem* p = port) {
                portInfo = (p->getPortType() == PortType::IN ? "INPUT" : "OUTPUT");
                portInfo += " pin " + QString::number(p->getPinIndex());
            }
            qDebug() << "  Created isolated net" << isolatedNetId << "for unconnected" << portInfo << "port";
            isolatedCount++;
        }
    }
    
    qDebug() << "Total isolated nets created:" << isolatedCount;
    qDebug() << "Total nets after isolation handling:" << netCounter;

    // Step 4: Create UI mappings for wires
    graph.wire2net.resize(wireItems.size());
    graph.net2wire.resize(netCounter, nullptr); // Initialize with nullptr
    m_simToUIMap.resize(netCounter, nullptr);     // Initialize with nullptr
    
    for (int i = 0; i < wireItems.size(); ++i) {
        WireItem* wire = wireItems[i];
        if (wireToNetMap.contains(wire)) {
            u32 netId = wireToNetMap[wire];
            graph.wire2net[i] = netId;
            
            // Only set if not already set (in case multiple wires share a net)
            // Use the first wire we encounter for each net
            if (!graph.net2wire[netId]) {
                graph.net2wire[netId] = wire;
                m_simToUIMap[netId] = wire;
            }
        }
    }
    
    // Debug: Report nets without wires (isolated nets)
    int isolatedNetCount = 0;
    for (u32 i = 0; i < netCounter; ++i) {
        if (!graph.net2wire[i]) {
            isolatedNetCount++;
        }
    }
    if (isolatedNetCount > 0) {
        qDebug() << "Note:" << isolatedNetCount << "isolated nets (unconnected ports) have no visual wires";
    }

    // Step 5: Create Gate structs with FLAT array for inputs (cache-friendly)
    for (int i = 0; i < gateItems.size(); ++i) {
        GateItem* gateItem = gateItems[i];

        u16 numInputs = 0;
        switch (gateItem->getGateType()) {
        case GType::NOT:
            numInputs = 1;
            break;
        case GType::AND:
        case GType::OR:
        case GType::XOR:
        case GType::NAND:
        case GType::NOR:
        case GType::XNOR:
            numInputs = 2;
            break;
        }

        Gate gate{gateItem->getGateType(), 0, 0, numInputs};
        
        // Set inID to the current size of the flat array (where this gate's inputs start)
        gate.inID = graph.gateInputs.size();

        // Add input net IDs directly to the flat array
        for (int pinIndex = 0; pinIndex < numInputs; pinIndex++) {
            PortItem* inputPort = gateItem->getInputPort(pinIndex);
            if (inputPort && portToNetMap.contains(inputPort)) {
                graph.gateInputs.push_back(portToNetMap[inputPort]);
            } else {
                qDebug() << "Warning: Gate" << i << "pin" << pinIndex << "has no connection";
                graph.gateInputs.push_back(0); // Default to net 0
            }
        }

        // Get output net ID and store it in the gate struct
        u32 outputNetId = 0;
        PortItem* outputPort = gateItem->getOutputPort();
        if (outputPort && portToNetMap.contains(outputPort)) {
            outputNetId = portToNetMap[outputPort];
        } else {
            qDebug() << "Warning: Gate" << i << "output has no connection";
        }

        gate.outID = outputNetId;
        graph.gates.push_back(gate);
        // No need for separate gateOutputs vector - it's in gate.outID!
    }

    // Step 6: Create Source structs
    for (int i = 0; i < sourceItems.size(); ++i) {
        SourceItem* sourceItem = sourceItems[i];
        
        // Get output net ID
        u32 outputNetId = 0;
        PortItem* outputPort = sourceItem->getOutputPort();
        if (outputPort && portToNetMap.contains(outputPort)) {
            outputNetId = portToNetMap[outputPort];
        } else {
            qDebug() << "Warning: Source" << i << "output has no connection";
        }
        
        // Create source with output ID directly in the struct
        Source source{false, outputNetId};
        graph.sources.push_back(source);
        // No need for separate sourceOutputs vector - it's in source.outID!
    }

    // Step 7: Set totals and debug output
    graph.totalGates = graph.gates.size();
    graph.totalSources = graph.sources.size();
    graph.totalNets = graph.nets.size();

    qDebug() << "\n========== Circuit Export Summary ==========";
    qDebug() << "Total Nets:" << graph.totalNets;
    qDebug() << "Total Wires:" << wireItems.size();
    qDebug() << "Total Gates:" << graph.totalGates;
    qDebug() << "Total Sources:" << graph.totalSources;
    qDebug() << "Flat gate inputs array size:" << graph.gateInputs.size();
    
    // Debug: Show net to wire mapping
    qDebug() << "\nNet to Wire Mapping:";
    for (u32 i = 0; i < graph.totalNets; ++i) {
        if (graph.net2wire[i]) {
            qDebug() << "  Net" << i << "→ WireItem" << (void*)graph.net2wire[i];
        } else {
            qDebug() << "  Net" << i << "→ [No Wire - Isolated Port]";
        }
    }
    
    // Debug: Show gate information
    qDebug() << "\nGate Information:";
    for (size_t i = 0; i < graph.gates.size(); ++i) {
        const Gate& gate = graph.gates[i];
        qDebug() << "  Gate" << i << "- Type:" << (int)gate.gateType 
                 << "Inputs:" << gate.numInputs 
                 << "InID:" << gate.inID 
                 << "OutID:" << gate.outID;
        // Show input net IDs
        QString inputs = "    Input nets: [";
        for (u16 j = 0; j < gate.numInputs; ++j) {
            if (j > 0) inputs += ", ";
            inputs += QString::number(graph.gateInputs[gate.inID + j]);
        }
        inputs += "]";
        qDebug() << inputs;
    }
    
    // Debug: Show source information
    qDebug() << "\nSource Information:";
    for (size_t i = 0; i < graph.sources.size(); ++i) {
        const Source& source = graph.sources[i];
        qDebug() << "  Source" << i << "- Value:" << source.value << "OutID:" << source.outID;
    }
    
    qDebug() << "==========================================\n";
    qDebug() << "Emitting startSimSIG...";
    
    emit startSimSIG(graph);
}

void CircuitScene::receiveResult(SimResult result)
{
    qDebug() << "Updating UI with simulation step:" << result.simulationStep;

    // Update wire colors based on net values
    for (size_t i = 0; i < result.netIds.size() && i < result.netValues.size(); i++) {
        u32 netId = result.netIds[i];
        bool value = result.netValues[i];

        // Find the corresponding wire in UI using cached mapping
        if (netId < m_simToUIMap.size() && m_simToUIMap[netId]) {
            WireItem* wire = m_simToUIMap[netId];

            // Update wire appearance based on value
            if (value) {
                wire->setPen(QPen(Qt::red, 3));    // High signal = red
            } else {
                wire->setPen(QPen(Qt::black, 2));  // Low signal = black
            }
        }
    }
}
void CircuitScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    // Draw grid
    painter->fillRect(rect, QColor(10, 200, 200));
}

// In CircuitCanvas.cpp - fix the mousePressEvent in CircuitScene:
void CircuitScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    qDebug() << "Scene mouse handler";

    // Handle our custom cases first
    if (event->button() == Qt::LeftButton) {
        QGraphicsItem* clickedItem = itemAt(event->scenePos(), QTransform());
        if (!clickedItem) {
            if (nextSource) {
                addSource(event->scenePos());
            } else
                addGate(nextGateType, event->scenePos());
            event->accept();
            return;
        }
    } else if (event->button() == Qt::RightButton) {
        startWireConnection(event->scenePos());
        event->accept();
        return;
    }

    // For all other cases (left-click on items, middle button, etc.)
    // use default Qt behavior
    QGraphicsScene::mousePressEvent(event);
}

void CircuitScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_connectingWire) {
        updateWireConnection(event->scenePos());
        // Optional: Add debug output (remove if too verbose)
        // qDebug() << "Updating wire to:" << event->scenePos();
    } else {
        QGraphicsScene::mouseMoveEvent(event);
    }
}

void CircuitScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_connectingWire
        && (event->button() == Qt::RightButton || event->button() == Qt::LeftButton)) {
        // Finish wire connection on either right or left button release
        finishWireConnection(event->scenePos());
        qDebug() << "Finished wire connection at:" << event->scenePos();
    } else {
        QGraphicsScene::mouseReleaseEvent(event);
    }

    // Force update after any mouse release to clear selection trails
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

// Add these includes at the top of CircuitCanvas.cpp


// Add this method to CircuitCanvas.cpp
void CircuitCanvas::keyPressEvent(QKeyEvent* event)
{
    qDebug() << "Key pressed:" << event->key() << "Text:" << event->text();

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
    
    qDebug() << "View mouse handler";
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
    // Zoom with mouse wheel
    const double scaleFactor = 1.15;
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
