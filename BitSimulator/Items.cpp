#include "Items.h"
#include <QMouseEvent>
#include <qscrollbar.h>
//===================== PortItem   ========================

extern bool sim_running;

PortItem::PortItem(PortType portType, int pinIndex, QGraphicsItem* parent)
    : QGraphicsEllipseItem(-4, -4, 8, 8, parent)
    ,m_pinIndex(pinIndex)
    , m_portType(portType)
{
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsMovable, false);
    setPen(QPen(Qt::black, 2));
    setBrush(Qt::black);
}

void PortItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
        if (!sim_running) {
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
        }
        else {
            if (m_value) {
                setPen(QPen(Qt::red, 1));
                setBrush(Qt::red);
            }
            else {
                setPen(QPen(Qt::black, 1));
                setBrush(Qt::black);
            }
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

GateItem::GateItem(GType gateType, QGraphicsItem* parent):QGraphicsObject(parent),m_gateType(gateType)
{
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    m_rect = QRectF(-25, -20, 50, 40);

    createPorts();
}

void GateItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
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

WireItem::WireItem(QPointF startpos, QPointF endpos, QGraphicsItem* parent)
    : localStartPos(startpos), localEndPos(endpos), QGraphicsObject(parent), m_pen(QPen(Qt::black, 2))
{
    setFlags(ItemIsSelectable);
}

QRectF WireItem::boundingRect() const
{
    // Handle case where positions aren't set yet
    if (localStartPos.isNull() && localEndPos.isNull()) {
        return QRectF(0, 0, 1, 1);  // Minimal fallback
    }

    // Calculate midpoint for orthogonal wire
    qreal midX = (localStartPos.x() + localEndPos.x()) / 2.0;

    // Find bounding rectangle that encompasses all three line segments
    qreal left = qMin(localStartPos.x(), qMin(midX, localEndPos.x()));
    qreal right = qMax(localStartPos.x(), qMax(midX, localEndPos.x()));
    qreal top = qMin(localStartPos.y(), localEndPos.y());
    qreal bottom = qMax(localStartPos.y(), localEndPos.y());

    QRectF rect(left, top, right - left, bottom - top);

    // Add padding for pen width
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
    }
}

void WireItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    // Use the stored pen, but modify color based on selection
    QPen currentPen = m_pen;

    // Color based on simulation state
    if (!sim_running) {
        if (isSelected()) {
            currentPen.setColor(Qt::blue);
            currentPen.setWidth(3);
        }
        else {
            currentPen.setColor(Qt::black);
        }
    }
    else {
        // During simulation, color based on wire value
        if (m_value) {
            currentPen.setColor(Qt::red);   // High signal
        }
        else {
            currentPen.setColor(Qt::black); // Low signal
        }

        if (isSelected()) {
            currentPen.setWidth(3);
        }
    }

    painter->setPen(currentPen);

    // Calculate the middle point horizontally between start and end
    qreal midX = (localStartPos.x() + localEndPos.x()) / 2.0;

    // Draw orthogonal path: horizontal -> vertical -> horizontal
    painter->drawLine(localStartPos.x(), localStartPos.y(), midX, localStartPos.y());          // Horizontal from start to middle
    painter->drawLine(midX, localStartPos.y(), midX, localEndPos.y());                        // Vertical from start height to end height
    painter->drawLine(midX, localEndPos.y(), localEndPos.x(), localEndPos.y());               // Horizontal from middle to end
}

void WireItem::updateWirePosition()
{  
    // Get the scene positions of both ports
    QPointF startPos = m_startPort->mapToScene(QPointF(0, 0));
    QPointF endPos = m_endPort->mapToScene(QPointF(0, 0));

    // Convert to this item's coordinate system
    localStartPos = mapFromScene(startPos);
    localEndPos = mapFromScene(endPos);
    
}
//===================== SourceItem ========================

SourceItem::SourceItem(QGraphicsItem* parent): QGraphicsObject(parent), m_rect(-15, -15, 30, 30)
{
    // Enable item flags for interaction
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    m_currIdx = 0;
    m_cycleValues.append(false);
    addPorts();
}
SourceItem::SourceItem(QList<bool>& cycleValues,QGraphicsItem* parent) : QGraphicsObject(parent), m_rect(-15, -15, 30, 30), m_cycleValues(cycleValues)
{
    // Enable item flags for interaction
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    m_currIdx = 0;

    addPorts();
}
void SourceItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    // Draw selection highlight
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(Qt::blue, 3));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_rect.adjusted(-2, -2, 2, 2));
    }

    painter->setPen(QPen(Qt::black, 2));
    if (m_cycleValues[m_currIdx]) {
        painter->setBrush(QColor(255, 150, 150)); 
        painter->drawRect(m_rect);

        // Draw "1" or "0" in the center to indicate state
        painter->setPen(QPen(Qt::white, 2));
        painter->drawText(m_rect, Qt::AlignCenter, "1");
    }
    else {
        painter->setBrush(QColor(255, 255, 255)); 
        painter->drawRect(m_rect);

        painter->setPen(QPen(Qt::black, 2));
        painter->drawText(m_rect, Qt::AlignCenter, "0");
    }
}

void SourceItem::addPorts()
{
    PortItem* outputPort = new PortItem(PortType::OUT, -1, this);
    outputPort->setPos(15, 0);
    m_outPorts.append(outputPort);
}

//===================== RegisterItem ========================

RegisterItem::RegisterItem(RType RegType, QGraphicsItem* parent):m_regType(RegType) {
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    m_rect = QRectF(-20, -30, 40, 60);

    createPorts();
}

void RegisterItem::createPorts()
{
    int numInputs = (m_regType == RType::D || m_regType == RType::T) ? 1 : 2;

    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;


    // Position input ports on the left side
    if (numInputs == 1) {
        PortItem* inputPort1 = new PortItem(PortType::IN, 0, this);
        inputPort1->setPos(-halfWidth, (halfHeight / 2) - halfHeight);
        m_inputPortOne =inputPort1;
    }
    else {
        PortItem* inputPort1 = new PortItem(PortType::IN, 0, this);
        inputPort1->setPos(-halfWidth, - halfHeight + halfHeight / 8);
        m_inputPortOne =inputPort1;

        PortItem* inputPort2 = new PortItem(PortType::IN, 1, this);
        inputPort2->setPos(-halfWidth, -halfHeight / 2 );
        m_inputPortTwo =inputPort2;
    }

    m_clkPort = new PortItem(PortType::IN, numInputs+1, this);
    m_clkPort->setPos(-halfWidth, 0); // Right side, center

    m_readEnbPort = new PortItem(PortType::IN, numInputs+2, this);
    m_readEnbPort->setPos(-halfWidth, halfHeight/2); // Right side, center
    // Create output port
    m_outputPort = new PortItem(PortType::OUT, -1, this);
    m_outputPort->setPos(halfWidth, 0); // Right side, center
}

void RegisterItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    // Draw selection highlight
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(Qt::blue, 3));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_rect.adjusted(-2, -2, 2, 2));
    }

    // Set styling for register
    painter->setPen(QPen(Qt::black, 2));
    if(m_value)
        painter->setBrush(QColor(100, 150, 255)); // Blue color (like in RegisterButton)
    else
        painter->setBrush(QColor(255, 255, 255));
    // Draw rectangle that fills the entire m_rect
    painter->fillRect(m_rect, painter->brush());
    painter->drawRect(m_rect);
    
    // Draw "R" in the center to indicate it's a register
    painter->setPen(QPen(Qt::white, 2));
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(14);  // Slightly larger since m_rect is bigger
    painter->setFont(font);
    painter->drawText(m_rect, Qt::AlignCenter, "R");
    
    // Draw a small clock symbol (triangle) at the bottom
    painter->setPen(QPen(Qt::white, 1.5));
    painter->setBrush(Qt::white);
    QPainterPath clockTriangle;
    
    // Position clock triangle at bottom center of m_rect
    double bottomY = m_rect.bottom() - 8;  // 8 pixels from bottom
    double centerX = m_rect.center().x();
    
    clockTriangle.moveTo(centerX - 4, bottomY);     // Left point
    clockTriangle.lineTo(centerX + 4, bottomY);     // Right point  
    clockTriangle.lineTo(centerX, bottomY - 4);     // Top point
    clockTriangle.closeSubpath();
    
    painter->fillPath(clockTriangle, painter->brush());
    painter->drawPath(clockTriangle);
}

//===================== MuxItem ========================

MuxItem::MuxItem(MType MuxType, QGraphicsItem* parent): m_muxType(MuxType) {

    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    m_rect = QRectF(-20, -30, 40, 60);

    createPorts();
}


void MuxItem::createPorts()
{
    int numInputs = 2;

    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;
    double diffHeight = m_rect.height() / (numInputs + 1);
    double currHeight = -halfHeight;
    double currWidth  = halfWidth;

    //data ports
    for (int i = 0; i < numInputs; i++) {
        currHeight += diffHeight;
        PortItem* inputPort = new PortItem(PortType::IN, i, this);
        inputPort->setPos(-halfWidth, currHeight);
        m_inputDataPorts.append(inputPort);
    }

    numInputs = static_cast<int>(ceil(log(numInputs)));
    double diffWidth = m_rect.width() / (numInputs + 1);

    //address ports
    for (int i = 0; i < numInputs; i++) {
        currWidth -= diffWidth;
        PortItem* inputPort = new PortItem(PortType::IN, m_inputDataPorts.size() + i , this);
        inputPort->setPos(currWidth, halfHeight);
        m_inputAddressPorts.append(inputPort);
    }

    // Create output port
    m_outputPort = new PortItem(PortType::OUT, -1, this);
    m_outputPort->setPos(halfWidth, 0); // Right side, center
}

void MuxItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget){
        QPainterPath path;
        double width = m_rect.width();
        double height = m_rect.height();
        double halfWidth = width / 2;
        double halfHeight = height / 2;
        double qurtHeight = halfHeight / 2;

        path.moveTo(-halfWidth,-halfHeight);
        path.lineTo(halfWidth,-qurtHeight);
        path.lineTo(halfWidth,qurtHeight);
        path.lineTo(-halfWidth,halfHeight);
        path.lineTo(-halfWidth,-halfHeight);
        painter->setBrush(QColor(255, 215, 150));
        painter->setPen(QPen(Qt::black, 4));
        painter->drawPath(path);
        painter->fillPath(path, painter->brush());     
}
