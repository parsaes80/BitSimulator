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
    SourceItem* source = new SourceItem(m_nextSrcCycleValues);
    source->setPos(position);
    addItem(source);
}
void CircuitScene::addRegister(RType RegType, QPointF position) {
    RegisterItem* Reg = new RegisterItem(RegType);
    Reg->setPos(position);
    addItem(Reg);
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
            QGraphicsObject* startGate = m_currWireStartPort->getParentGate();
            QGraphicsObject* endGate = endPort->getParentGate();

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

    // Collections to track items and assign IDs
    QList<GateItem*> gateItems;
    QList<SourceItem*> sourceItems;
    QList<WireItem*> wireItems;
    QList<RegisterItem*> registerItems;
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
    }
    
    //QSet<QPair< QGraphicsObject*, QGraphicsObject*>> connections;

    //for (const auto* wire : wireItems) {
    //    QGraphicsObject* startItem = wire->getStartPort()->getParentGate();
    //    QGraphicsObject* endItem = wire->getEndPort()->getParentGate();
    //    connections.insert(QPair(startItem, endItem));
    //}

    for (int i = 0; i < gateItems.size(); i++) { map.gate2Idx[gateItems[i]] = i; map.Idx2gate[i] = gateItems[i]; }
    for (int i = 0; i < sourceItems.size(); i++) { map.source2Idx[sourceItems[i]] = i; map.Idx2source[i] = sourceItems[i];}
    for (int i = 0; i < registerItems.size(); i++) { map.reg2Idx[registerItems[i]] = i; map.Idx2reg[i] = registerItems[i];}

    // wire,net mapping 
    QHash<PortItem*, u32> outputPortToNet;
    u32 netCounter = 1; 
    for (WireItem* wire : wireItems) {
        PortItem* startPort = wire->getStartPort();

        // If this output port doesn't have a net ID yet, assign one
        if (!outputPortToNet.contains(startPort)) {
            outputPortToNet[startPort] = netCounter++;
        }

        // All wires from the same output port get the same net ID
        u32 netId = outputPortToNet[startPort];
        map.wire2net[wire] = netId;

        // Store first wire for each net (for UI mapping)
        map.net2wire[netId].push_back(wire);
    }

    std::vector<Gate> gates;
    std::vector<Source> sources;
    std::vector<Register> registers;
    std::vector<u32> gateInputs;
    //std::vector<u32> sourceOutputs;

    // Wire,Gate Mapping
    u32 gateInputindex = 0;
    for (auto* gateItem : gateItems)
    {
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
    u32 outNet;
    for (auto* sourceItem : sourceItems)
    {   
        outNet = 0;
        if (!sourceItem->getOutputPorts()[0]->getConnections().isEmpty()) {
            WireItem* outwire = sourceItem->getOutputPorts()[0]->getConnections()[0];
            outNet = map.wire2net[outwire];
        }
        std::vector<bool> cycleValues;
        for (auto value : sourceItem->getValues()) {cycleValues.push_back(value);};
        sources.push_back(Source(outNet, cycleValues));
    }
    u32 inNet;
    for (auto* regItem : registerItems)
    {
        outNet = 0; inNet = 0;
        if (!regItem->getOutputPort()->getConnections().isEmpty()) {
            WireItem* outwire = regItem->getOutputPort()->getConnections()[0];
            outNet = map.wire2net[outwire];
        }
        if (!regItem->getInputPort()->getConnections().isEmpty()) {
            WireItem* inwire = regItem->getInputPort()->getConnections()[0];
            inNet = map.wire2net[inwire];
        }
        registers.push_back(Register(RType::D, inNet, outNet));
    }
    graph.gates = gates;
    graph.sources = sources;
    graph.registers = registers;
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

    for (int i = 0; i < result.sourcesCurrIdx.size(); i++) {
        auto source = map.Idx2source[i];
        source->setIdx(result.sourcesCurrIdx[i]);    
    }
    for (int i = 0; i < result.registerValues.size(); i++) {
        auto reg = map.Idx2reg[i];
        reg->setValue(result.registerValues[i]);
    }
}

void CircuitScene::drawBackground(QPainter* painter, const QRectF& rect)
{
    // Draw grid
    painter->fillRect(rect, QColor(10, 200, 200));
}

void CircuitScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    // Handle our custom cases first
    if (event->button() == Qt::LeftButton) {
        QGraphicsItem* clickedItem = itemAt(event->scenePos(), QTransform());
        if (!clickedItem) {
            if (m_nextIsSource) 
                addSource(event->scenePos());
            else if(m_nextIsGate)
                addGate(m_nextGateType, event->scenePos());
            else if(m_nextIsRegister)
                addRegister(m_nextRegType, event->scenePos());
            event->accept();
            return;
        }
    } else if (event->button() == Qt::RightButton) {
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
