#include "Items.h"
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
    : QGraphicsObject(parent)
    , m_gateType(gateType)
{
    // Enable item flags for interaction
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

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
    } else if (isConnected()) {
        setPen(QPen(Qt::darkGreen, 2)); // Connected wires are green
    } else {
        setPen(QPen(Qt::red, 2)); // Unconnected wires are red
    }

    QGraphicsLineItem::paint(painter, option, widget);
}

//===================== SourceItem ========================

SourceItem::SourceItem(QGraphicsObject* parent)
    : QGraphicsObject(parent)
    , m_rect(-15, -15, 30, 30)
{
    // Enable item flags for interaction
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    addPorts();
}

void SourceItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(widget)

    // Draw selection highlight
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(Qt::blue, 3));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_rect.adjusted(-2, -2, 2, 2));
    }

    // Draw source as a circle
    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(QColor(255, 100, 100)); // Red color for source
    painter->drawEllipse(m_rect);
    
    // Draw "1" or "0" in the center to indicate state
    painter->setPen(QPen(Qt::white, 2));
    painter->drawText(m_rect, Qt::AlignCenter, "1");
}

void SourceItem::addPorts()
{
    // Add output port on the right side
    PortItem* outputPort = new PortItem(PortType::OUT, -1, this);
    outputPort->setPos(15, 0); // Right side of the circle
    m_outPorts.append(outputPort);
}
