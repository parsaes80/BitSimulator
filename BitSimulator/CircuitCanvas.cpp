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
    u32 wireIndex = 0;

    // Step 1: Find all gates and sources
    for (QGraphicsItem* item : items()) {
        if (WireItem* wire = dynamic_cast<WireItem*>(item)) {
            if (wire->isConnected()) {
                wireItems.push_back(wire);

                // Create mapping: Wire UI Index → Net ID
                graph.wireUItoSimMap.push_back(netCounter);

                // Create reverse mapping: Net ID → Wire Pointer
                if (graph.simToUIMap.size() <= netCounter) {
                    graph.simToUIMap.resize(netCounter + 1);
                }
                graph.simToUIMap[netCounter] = wire;

                // Create the net with ID
                graph.nets.push_back(Net{false, netCounter});

                // Map both ports to this net
                PortItem* startPort = wire->getStartPort();
                PortItem* endPort = wire->getEndPort();
                if (startPort) portToNetMap[startPort] = netCounter;
                if (endPort) portToNetMap[endPort] = netCounter;

                netCounter++;
                wireIndex++;
            }
        }
        else if (GateItem* gate = dynamic_cast<GateItem*>(item)) {
            gateItems.push_back(gate);
        }
        else if (SourceItem* source = dynamic_cast<SourceItem*>(item)) {
            sourceItems.push_back(source);
        }
    }

    // Step 2: Create nets for each unique connection
    QHash<PortItem*, QList<PortItem*>> connectedPorts; // Groups of connected ports

    // Build connection groups from wires
    for (WireItem* wire : wireItems) {
        PortItem* startPort = wire->getStartPort();
        PortItem* endPort = wire->getEndPort();

        if (startPort && endPort) {
            if (!connectedPorts.contains(startPort)) {
                connectedPorts[startPort] = QList<PortItem*>();
            }
            if (!connectedPorts.contains(endPort)) {
                connectedPorts[endPort] = QList<PortItem*>();
            }

            connectedPorts[startPort].append(endPort);
            connectedPorts[endPort].append(startPort);
        }
    }

    // Assign net IDs to connected port groups
    QSet<PortItem*> processedPorts;
    for (auto it = connectedPorts.begin(); it != connectedPorts.end(); ++it) {
        if (!processedPorts.contains(it.key())) {
            // Create a new net
            u32 currentNetId = netCounter++;
            graph.nets.push_back(Net{false, currentNetId}); // Default to false
            // BFS to find all connected ports in this net
            QQueue<PortItem*> portsToProcess;
            portsToProcess.enqueue(it.key());

            while (!portsToProcess.isEmpty()) {
                PortItem* currentPort = portsToProcess.dequeue();
                if (processedPorts.contains(currentPort))
                    continue;

                processedPorts.insert(currentPort);
                portToNetMap[currentPort] = currentNetId;

                // Add all connected ports
                for (PortItem* connectedPort : connectedPorts[currentPort]) {
                    if (!processedPorts.contains(connectedPort)) {
                        portsToProcess.enqueue(connectedPort);
                    }
                }
            }
        }
    }

    // Step 3: Create Gate structs
    for (int i = 0; i < gateItems.size(); ++i) {
        GateItem* gateItem = gateItems[i];

        // Count inputs for this gate type
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
            break; // Can be extended for more inputs
        }

        // Create gate
        Gate gate(gateItem->getGateType(), 0, 0, numInputs); // inID and outID will be set below

        // Find input and output ports
        QList<PortItem*> inputPorts;
        PortItem* outputPort = nullptr;

        // Get ports from the gate (you'll need to add getters to GateItem)
        for (int pinIndex = 0; pinIndex < numInputs; pinIndex++) {
            PortItem* inputPort = gateItem->getInputPort(pinIndex);
            if (inputPort && portToNetMap.contains(inputPort)) {
                inputPorts.append(inputPort);
            }
        }

        outputPort = gateItem->getOutputPort();

        // Map ports to nets
        std::vector<u32> inputNetIds;
        for (PortItem* inputPort : inputPorts) {
            if (portToNetMap.contains(inputPort)) {
                inputNetIds.push_back(portToNetMap[inputPort]);
            }
        }

        u32 outputNetId = 0;
        if (outputPort && portToNetMap.contains(outputPort)) {
            outputNetId = portToNetMap[outputPort];
        }

        // Set the inID to the start index in a flattened input array
        gate.inID = graph.gateInputs.size(); // This will be the index where this gate's inputs start
        gate.outID = outputNetId;

        graph.gates.push_back(gate);
        graph.gateInputs.push_back(inputNetIds);
        graph.gateOutputs.push_back(outputNetId);
    }

    // Step 4: Create Source structs
    for (int i = 0; i < sourceItems.size(); ++i) {
        SourceItem* sourceItem = sourceItems[i];

        Source source(false); // Default value
        graph.sources.push_back(source);

        // Find output port and map to net
        PortItem* outputPort = nullptr; // You'll need to add getter to SourceItem
        // outputPort = sourceItem->getOutputPort();

        u32 outputNetId = 0;
        if (outputPort && portToNetMap.contains(outputPort)) {
            outputNetId = portToNetMap[outputPort];
        }

        graph.sourceOutputs.push_back(outputNetId);
    }

    // Step 5: Set totals
    graph.totalGates = graph.gates.size();
    graph.totalSources = graph.sources.size();
    graph.totalNets = graph.nets.size();
    qDebug() << "startsim emmited";
    emit startSimSIG(graph);
};
void CircuitScene::receiveResult(SimResult result)
{
    qDebug() << "Updating UI with simulation step:" << result.simulationStep;

    // Update wire colors based on net values
    for (size_t i = 0; i < result.netIds.size() && i < result.netValues.size(); i++) {
        u32 netId = result.netIds[i];
        bool value = result.netValues[i];

        // Find the corresponding wire in UI
        if (netId < m_wireMapping.size() && m_wireMapping[netId]) {
            WireItem* wire = m_wireMapping[netId];

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
