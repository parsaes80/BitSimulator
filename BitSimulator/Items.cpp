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

    return (m_portType == PortType::IN && otherPort->m_portType == PortType::OUT) ||
        (m_portType == PortType::OUT && otherPort->m_portType == PortType::IN);
}

void PortItem::addConnection(WireItem* wire)
{
    if (wire && !m_connections.contains(wire)) {
        m_connections.append(wire);
        update(); 
    }
}

void PortItem::removeConnections(WireItem* wire)
{
    if (m_connections.removeAll(wire) > 0) {
        update(); 
    }
}

//===================== GateItem   ========================

GateItem::GateItem(GType gateType, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_gateType(gateType)
{
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    m_rect = QRectF(-25, -20, 50, 40);

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
    double bubbleSize = 0; // Make bubble a bit bigger

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
    double bubbleSize = 0; // Make consistent with NOT gate
    
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

//void GateItem::mousePressEvent(QGraphicsSceneMouseEvent* event){QGraphicsItem::mousePressEvent(event);}

void GateItem::createPorts()
{
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

//===================== Wire Item   ========================

WireItem::WireItem(const QLineF& line, QGraphicsItem* parent)
    : QGraphicsObject(parent), m_line(line), m_pen(QPen(Qt::black, 2))
{
    setFlags(ItemIsSelectable);
}

QRectF WireItem::boundingRect() const
{
    // Create a bounding rect around the line with some padding
    QRectF rect = QRectF(m_line.p1(), m_line.p2()).normalized();
    qreal penWidth = m_pen.width();
    return rect.adjusted(-penWidth / 2, -penWidth / 2, penWidth / 2, penWidth / 2);
}

// Update WireItem methods
void WireItem::setStartPort(PortItem* port)
{
    if (m_startPort) {
        m_startPort->removeConnections(this);
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
        m_endPort->removeConnections(this);
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
    Q_UNUSED(option);
    Q_UNUSED(widget);

    // Use the stored pen, but modify color based on selection
    QPen currentPen = m_pen;
    if (isSelected()) {
        currentPen.setColor(Qt::blue);
        currentPen.setWidth(3);
    }
    
    painter->setPen(currentPen);
    painter->drawLine(m_line);
}

void WireItem::updateWirePosition()
{
    if (m_startPort && m_endPort) {
        // Get the scene positions of both ports
        QPointF startPos = m_startPort->mapToScene(QPointF(0, 0));
        QPointF endPos = m_endPort->mapToScene(QPointF(0, 0));

        // Convert to this item's coordinate system
        QPointF localStartPos = mapFromScene(startPos);
        QPointF localEndPos = mapFromScene(endPos);

        // Update the wire line
        setLine(QLineF(localStartPos, localEndPos));
    }
}
//===================== SourceItem ========================

SourceItem::SourceItem(QGraphicsItem* parent): QGraphicsObject(parent)
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
    painter->drawRect(m_rect);
    
    // Draw "1" or "0" in the center to indicate state
    painter->setPen(QPen(Qt::white, 2));
    painter->drawText(m_rect, Qt::AlignCenter, "1");
}

void SourceItem::addPorts()
{
    PortItem* outputPort = new PortItem(PortType::OUT, -1, this);
    outputPort->setPos(15, 0); // Right side of the circle
    m_outPorts.append(outputPort);
}
