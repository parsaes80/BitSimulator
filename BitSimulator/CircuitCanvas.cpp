#include "CircuitCanvas.h"
#include <QMouseEvent>
#include <qscrollbar.h>
//===================== PortItem   ========================

PortItem::PortItem(PortType portType, int pinIndex, QGraphicsItem* parent)
    : QGraphicsEllipseItem(-4, -4, 8, 8, parent)  // 8x8 circle centered at (0,0)
    , m_portType(portType)
    , m_pinIndex(pinIndex)
{
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsMovable, false);
    setPen(QPen(Qt::black, 2));
    setBrush(Qt::black);
}

void PortItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    // Change appearance based on state
    if (m_highlighted) {
        setPen(QPen(Qt::yellow, 1));
        setBrush(Qt::yellow);
    }
    else if (!m_connections.isEmpty()) {
        setPen(QPen(Qt::green, 1));
        setBrush(Qt::green);
    }
    else {
        setPen(QPen(Qt::black, 1));
        setBrush(Qt::black);
    }

    QGraphicsEllipseItem::paint(painter, option, widget);
}

bool PortItem::canConnectTo(PortItem* otherPort) const
{
    if (!otherPort || otherPort == this) return false;

    // Input ports can connect to output ports and vice versa
    return (m_portType == PortType::IN && otherPort->m_portType == PortType::OUT) ||
        (m_portType == PortType::OUT && otherPort->m_portType == PortType::IN);
}

void PortItem::addConnection(WireItem* wire)
{
    if (wire && !m_connections.contains(wire)) {
        m_connections.append(wire);
        update(); // Refresh appearance
    }
}

void PortItem::removeConnection(WireItem* wire)
{
    if (m_connections.removeAll(wire) > 0) {
        update(); // Refresh appearance
    }
}

void PortItem::setHighlighted(bool highlighted)
{
    if (m_highlighted != highlighted) {
        m_highlighted = highlighted;
        update();
    }
}

//===================== GateItem   ========================

GateItem::GateItem(GType gateType, QGraphicsItem* parent)
    : QGraphicsItem(parent)
    , m_gateType(gateType)
{
    // Enable item flags for interaction
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);

    // Set size
    m_rect = QRectF(-25, -20, 50, 40);

    // Create ports after setting up the gate
    createPorts();
}

void GateItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(widget)

    // Draw selection highlight
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(Qt::blue, 3));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_rect.adjusted(-2, -2, 2, 2));
    }

    // Draw gate body based on type
    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(QColor(255, 215, 150)); // Light orange/beige color like in your image

    drawGateShape(painter);

    // Draw input/output pins
    //drawPins(painter);
}

// In CircuitCanvas.cpp - add to GateItem::itemChange
QVariant GateItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && scene()) {
        // Snap to grid
        QPointF newPos = value.toPointF();
        int gridSize = 10;
        newPos.setX(qRound(newPos.x() / gridSize) * gridSize);
        newPos.setY(qRound(newPos.y() / gridSize) * gridSize);

        return newPos;
    }

    return QGraphicsItem::itemChange(change, value);
}

void GateItem::drawPins(QPainter* painter)
{
    double width = m_rect.width();
    double height = m_rect.height();
    double pinLenght = 5;
    painter->setPen(QPen(Qt::darkGray, 2));
    // Input pins
    painter->drawLine(-width / 2, -height / 4, -width / 2 - pinLenght, -height / 4);
    painter->drawLine(-width / 2, height / 4, -width / 2 - pinLenght, height / 4);
    // Output pin
    painter->drawLine(width / 2, 0, width / 2 + pinLenght, 0);
}

void GateItem::drawGateShape(QPainter* painter)
{
    switch (m_gateType) {
    case GType::AND:
        drawAndGate(painter);
        break;
    case GType::OR:
        drawOrGate(painter);
        break;
    case GType::XOR:
        drawXorGate(painter);
        break;
    case GType::NAND:
        drawAndGate(painter);
        drawNotBubble(painter);
        break;
    case GType::NOR:
        drawOrGate(painter);
        drawNotBubble(painter);
        break;
    case GType::XNOR:
        drawXorGate(painter);
        drawNotBubble(painter);
        break;
    case GType::NOT:
        drawNotGate(painter);
        break;
    }
}

void GateItem::drawAndGate(QPainter* painter)
{
    // AND gate: Rectangle on left, semicircle on right
    QPainterPath path;
    double width = m_rect.width();
    double height = m_rect.height();
    double halfWidth = width / 2;
    double halfHeight = height / 2;

    path.moveTo(-halfWidth, -halfHeight);                    // Top left
    path.lineTo(0, -halfHeight);                             // Top middle
    path.arcTo(0, -halfHeight, halfWidth, height, 90, -180); // Circle that fits in right half
    path.lineTo(-halfWidth, halfHeight);                     // Bottom left
    path.lineTo(-halfWidth, -halfHeight);                    // Close path

    painter->fillPath(path, painter->brush());
    painter->drawPath(path);
}

void GateItem::drawOrGate(QPainter* painter)
{
    // OR gate: Curved shape
    QPainterPath path;
    double width = m_rect.width();
    double height = m_rect.height();
    double halfWidth = width / 2;
    double halfHeight = height / 2;

    // Left curved input side
    path.moveTo(-halfWidth, -halfHeight);
    path.quadTo(-halfWidth / 2, 0, -halfWidth, halfHeight); // Input curve
    // Bottom connection to right side
    path.lineTo(halfWidth / 2, halfHeight);
    // Right curved output side - reaches full width
    path.quadTo(width * 3 / 4, 0, halfWidth / 2, -halfHeight); // Output curve reaches right edge
    // Top connection
    path.lineTo(-halfWidth, -halfHeight);
    painter->fillPath(path, painter->brush());
    painter->drawPath(path);
}

void GateItem::drawXorGate(QPainter* painter)
{
    // Draw OR gate first
    drawOrGate(painter);
    double width = m_rect.width();
    double height = m_rect.height();
    double halfWidth = width / 2;
    double halfHeight = height / 2;
    
    // Add extra curved line on the left for XOR
    QPainterPath extraLine;
    extraLine.moveTo(-halfWidth, -halfHeight * 0.75);
    extraLine.quadTo(-halfWidth, 0, -halfWidth, halfHeight * 0.75);
    painter->drawPath(extraLine);
}

void GateItem::drawNotGate(QPainter* painter)
{
    // NOT gate: Triangle with bubble
    QPainterPath path;
    double width = m_rect.width();
    double height = m_rect.height();
    double halfWidth = width / 2;
    double halfHeight = height / 2;
    double bubbleSize = padding * 3; // Make bubble a bit bigger

    path.moveTo(-halfWidth, -halfHeight);       // Top left
    path.lineTo(-halfWidth, halfHeight);        // Bottom left
    path.lineTo(halfWidth - bubbleSize / 2, 0); // Right point (leave space for bubble)
    path.lineTo(-halfWidth, -halfHeight);       // Close triangle
    painter->fillPath(path, painter->brush());
    painter->drawPath(path);
    
    // Draw NOT bubble at the tip
    painter->setBrush(Qt::white);
    painter->drawEllipse(halfWidth - bubbleSize/2, -bubbleSize/2, bubbleSize, bubbleSize);
    painter->setBrush(QColor(255, 215, 150)); // Restore original brush
}

void GateItem::drawNotBubble(QPainter* painter)
{
    // Small circle at output for NOT operation
    painter->setBrush(Qt::white);
    double width = m_rect.width();
    double halfWidth = width / 2;
    double bubbleSize = padding * 3; // Make consistent with NOT gate
    
    painter->drawEllipse(halfWidth - bubbleSize/2, -bubbleSize/2, bubbleSize, bubbleSize);
    painter->setBrush(QColor(255, 215, 150)); // Restore original brush
}

QString GateItem::gateTypeToString() const
{
    switch (m_gateType) {
    case GType::AND:
        return "AND";
    case GType::OR:
        return "OR";
    case GType::XOR:
        return "XOR";
    case GType::NAND:
        return "NAND";
    case GType::NOR:
        return "NOR";
    case GType::XNOR:
        return "XNOR";
    case GType::NOT:
        return "NOT";
    }
    return "GATE";
}
void GateItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    qDebug() << "item mouse handler";
    QGraphicsItem::mousePressEvent(event);
}

void GateItem::createPorts()
{
    // Clear existing ports
    for (PortItem* port : m_inputPorts) {
        delete port;
    }
    m_inputPorts.clear();

    if (m_outputPort) {
        delete m_outputPort;
        m_outputPort = nullptr;
    }

    // Create input ports based on gate type
    int numInputs = (m_gateType == GType::NOT) ? 1 : 2;

    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;

    for (int i = 0; i < numInputs; i++) {
        PortItem* inputPort = new PortItem(PortType::IN, i, this);

        // Position input ports on the left side
        if (numInputs == 1) {
            inputPort->setPos(-halfWidth, 0); // Center for single input (NOT gate)
        }
        else {
            inputPort->setPos(-halfWidth, -halfHeight / 2 + i * halfHeight); // Top and bottom for dual inputs
        }

        m_inputPorts.append(inputPort);
    }

    // Create output port
    m_outputPort = new PortItem(PortType::OUT, -1, this);
    m_outputPort->setPos(halfWidth, 0); // Right side, center
}

// Update the getInputPin and getOutputPin methods to use ports
QPointF GateItem::getInputPin(int index) const
{
    if (index >= 0 && index < m_inputPorts.size()) {
        return m_inputPorts[index]->mapToScene(QPointF(0, 0));
    }
    return QPointF();
}

QPointF GateItem::getOutputPin() const
{
    if (m_outputPort) {
        return m_outputPort->mapToScene(QPointF(0, 0));
    }
    return QPointF();
}
//===================== Wire Item   ========================

WireItem::WireItem(const QLineF& line, QGraphicsItem* parent)
    : QGraphicsLineItem(line, parent)
{
    setPen(QPen(Qt::black, 2));
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
}


// Update WireItem methods
void WireItem::setStartPort(PortItem* port)
{
    if (m_startPort) {
        m_startPort->removeConnection(this);
    }

    m_startPort = port;

    if (m_startPort) {
        m_startPort->addConnection(this);
        // Update wire position to port location
        QLineF currentLine = line();
        currentLine.setP1(m_startPort->mapToScene(QPointF(0, 0)));
        setLine(currentLine);
    }
}

void WireItem::setEndPort(PortItem* port)
{
    if (m_endPort) {
        m_endPort->removeConnection(this);
    }

    m_endPort = port;

    if (m_endPort) {
        m_endPort->addConnection(this);
        // Update wire position to port location
        QLineF currentLine = line();
        currentLine.setP2(m_endPort->mapToScene(QPointF(0, 0)));
        setLine(currentLine);
    }
}

void WireItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    // Change color based on connection state
    if (isSelected()) {
        setPen(QPen(Qt::blue, 3));
    }
    else if (isConnected()) {
        setPen(QPen(Qt::darkGreen, 2)); // Connected wires are green
    }
    else {
        setPen(QPen(Qt::red, 2)); // Unconnected wires are red
    }

    QGraphicsLineItem::paint(painter, option, widget);
}

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
        // Clear all highlights
        for (QGraphicsItem* item : items()) {
            if (PortItem* port = dynamic_cast<PortItem*>(item)) {
                port->setHighlighted(false);
            }
        }

        removeItem(m_currentWire);

        PortItem* endPort = findNearestPort(endPoint);

        // Only create wire if we have valid ports that can connect
        if (m_currentWireStartPort && endPort &&
            m_currentWireStartPort->canConnectTo(endPort)) {

            QPointF startPos = m_currentWireStartPort->mapToScene(QPointF(0, 0));
            QPointF endPos = endPort->mapToScene(QPointF(0, 0));

            WireItem* wire = new WireItem(QLineF(startPos, endPos));
            wire->setStartPort(m_currentWireStartPort);
            wire->setEndPort(endPort);
            addItem(wire);

            qDebug() << "Connected ports successfully";
        }
        else {
            qDebug() << "Wire connection failed - no valid ports";
        }

        delete m_currentWire;
        m_connectingWire = false;
        m_currentWire = nullptr;
        m_currentWireStartPort = nullptr;
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
