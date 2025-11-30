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

void PortItem::removeConnection(WireItem* wire)
{
    if (m_connections.removeAll(wire) > 0) {
        update(); 
    }
}

//===================== GateItem   ========================

GateItem::GateItem(GType gateType, int numInputs, QGraphicsItem* parent):
    QGraphicsObject(parent),m_gateType(gateType),m_numInputs(numInputs)
{
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    int x = -25;
    int y = x - ((m_numInputs-2)*5) + 3;
    int height = y* -2;

    if(gateType == GType::NOT) {
        m_numInputs =1;
        y=-22;
        height= 44;
    }
    m_rect = QRectF(x, y , 50, height);

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

    painter->setRenderHint(QPainter::Antialiasing, true);
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
    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(QColor(255, 215, 150));
    QPainterPath path;

    double width = m_rect.width();
    double height = m_rect.height();
    double halfWidth = width / 2;
    double halfHeight = height / 2;

    path.moveTo(-halfWidth, -halfHeight);
    path.lineTo(0, -halfHeight);
    path.arcTo(0, -halfHeight, halfWidth, height, 90, -180);
    path.lineTo(-halfWidth, halfHeight);
    path.lineTo(-halfWidth, -halfHeight);

    painter->fillPath(path, painter->brush());
    painter->drawPath(path);
}

void GateItem::drawOrGate(QPainter* painter)
{
    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(QColor(0, 255, 140));
    QPainterPath path;
    double width = m_rect.width();
    double height = m_rect.height();
    double halfWidth = width / 2;
    double halfHeight = height / 2;

    path.moveTo(-halfWidth, -halfHeight);
    path.quadTo(-halfWidth / 2, 0, -halfWidth, halfHeight);
    path.lineTo(halfWidth / 2, halfHeight);
    path.quadTo(width * 3 / 4, 0, halfWidth / 2, -halfHeight);

    path.lineTo(-halfWidth, -halfHeight);
    painter->fillPath(path, painter->brush());
    painter->drawPath(path);
}

void GateItem::drawXorGate(QPainter* painter)
{
    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(QColor(142, 43, 255));
    QPainterPath path;
    double width = m_rect.width();
    double height = m_rect.height();
    double halfWidth = width / 2;
    double halfHeight = height / 2;

    path.moveTo(-halfWidth, -halfHeight);
    path.quadTo(-halfWidth / 2, 0, -halfWidth, halfHeight);
    path.lineTo(halfWidth / 2, halfHeight);
    path.quadTo(width * 3 / 4, 0, halfWidth / 2, -halfHeight);
    path.lineTo(-halfWidth, -halfHeight);
    painter->fillPath(path, painter->brush());
    painter->drawPath(path);

    QPainterPath extraLine;

    painter->setBrush(Qt::NoBrush);
    double offset = width * 0.1;
    extraLine.moveTo(-halfWidth - offset, -halfHeight*0.9 );
    extraLine.quadTo(-halfWidth * 0.6, 0, -halfWidth - offset, halfHeight*0.9);
    painter->drawPath(extraLine);
}

void GateItem::drawNotGate(QPainter* painter)
{
    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(QColor(255, 0, 0));
    QPainterPath path;
    double width = m_rect.width();
    double height = m_rect.height();
    double halfWidth = width / 2;
    double halfHeight = height / 2;

    path.moveTo(-halfWidth, -halfHeight);
    path.lineTo(-halfWidth, halfHeight);
    path.lineTo(halfWidth , 0);
    path.lineTo(-halfWidth, -halfHeight);
    painter->fillPath(path, painter->brush());
    painter->drawPath(path);
    
}

void GateItem::drawNotBubble(QPainter* painter)
{
    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(QColor(255, 0, 0));
    double width = m_rect.width();
    double halfWidth = width / 2;
    double bubbleSize = 10;
    
    // Draw centered at y=0 by offsetting y by -bubbleSize/2
    painter->drawEllipse(halfWidth, -bubbleSize/2, bubbleSize, bubbleSize);
    painter->setBrush(QColor(255, 215, 150)); // Restore original brush
}

void GateItem::createPorts()
{
    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;
    double diffHeight = m_rect.height() / (m_numInputs + 1);
    double currHeight = -halfHeight;

    for (int i = 0; i < m_numInputs; i++) {
        currHeight += diffHeight;
        PortItem* inputPort = new PortItem(PortType::IN, i, this);
        inputPort->setPos(-halfWidth, currHeight);
        m_inputPorts.append(inputPort);
    }
    // Create output port
    m_outputPort = new PortItem(PortType::OUT, -1, this);
    
    bool hasNotBubble = (m_gateType == GType::NAND ||
                         m_gateType == GType::NOR ||
                         m_gateType == GType::XNOR);
    
    if (hasNotBubble) {
        m_outputPort->setPos(halfWidth + 14, 0); // Shift right by 10 (bubble size)
    } else {
        m_outputPort->setPos(halfWidth, 0);
    }
}

void GateItem::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // Check if all ports are empty
        bool allPortsEmpty = true;
        
        // Check input ports
        for (PortItem* port : std::as_const(m_inputPorts)) {
            if (!port->getConnections().isEmpty()) {
                allPortsEmpty = false;
                break;
            }
        }
        
        // Check output port
        if (allPortsEmpty && m_outputPort && !m_outputPort->getConnections().isEmpty()) {
            allPortsEmpty = false;
        }
        
        // If all ports are empty, remove the item
        if (allPortsEmpty) {
            scene()->removeItem(this);
            deleteLater();
        }
    } else {
        QGraphicsObject::keyPressEvent(event);
    }
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

    qreal penWidth = m_pen.width();
    qreal extra = penWidth / 2.0 + 2; // Small extra margin for selection

    qreal left = qMin(localStartPos.x(), localEndPos.x());
    qreal right = qMax(localStartPos.x(), localEndPos.x());
    qreal top = qMin(localStartPos.y(), localEndPos.y());
    qreal bottom = qMax(localStartPos.y(), localEndPos.y());

    return QRectF(left - extra, top - extra,
                  right - left + 2*extra,
                  bottom - top + 2*extra);
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
            currentPen.setColor(Qt::blue);
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
QPainterPath WireItem::shape() const
{
    QPainterPath path;

    if (localStartPos.isNull() && localEndPos.isNull()) {
        return path;
    }

    qreal midX = (localStartPos.x() + localEndPos.x()) / 2.0;

    // Create a stroker to make a clickable area around the wire
    QPainterPathStroker stroker;
    stroker.setWidth(10); // 10 pixel click tolerance

    // Draw the orthogonal path
    path.moveTo(localStartPos);
    path.lineTo(midX, localStartPos.y());
    path.lineTo(midX, localEndPos.y());
    path.lineTo(localEndPos);

    return stroker.createStroke(path);
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

void WireItem::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // For wires, we remove them regardless of connections
        // since the wire itself IS the connection
        if (m_startPort) {
            m_startPort->removeConnection(this);
        }
        if (m_endPort) {
            m_endPort->removeConnection(this);
        }
        scene()->removeItem(this);
        deleteLater();
    } else {
        QGraphicsObject::keyPressEvent(event);
    }
}

//===================== SourceItem ========================

SourceItem::SourceItem(const std::variant<QList<bool>,QList<int>>& cycleValues, QGraphicsItem* parent) :
    QGraphicsObject(parent), m_srcValues(cycleValues)
{
    // Enable item flags for interaction
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    m_currIdx = 0;
    if(std::holds_alternative<QList<bool>>(m_srcValues)){
        m_numOutputs = 1;
    }
    else{
        const auto& intValues = std::get<QList<int>>(m_srcValues);
        int maxValue = *std::ranges::max_element(intValues);
        m_numOutputs =  static_cast<int>(std::floor(std::log2(maxValue))) + 1;
    }
    int x = -15;
    int y = x - ((m_numOutputs-1)*5);
    int height = y* -2;
    m_rect = QRectF(x, y , 30, height);
    addPorts();
}
SourceItem::SourceItem(const std::variant<QList<bool>,QList<int>>& cycleValues,int numOut, QGraphicsItem* parent) :
    QGraphicsObject(parent), m_srcValues(cycleValues)
{
    // Enable item flags for interaction
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    m_currIdx = 0;
    m_numOutputs = numOut;

    int x = -15;
    int y = x - ((m_numOutputs-1)*5);
    int height = y* -2;
    m_rect = QRectF(x, y , 30, height);
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
    if(std::holds_alternative<QList<bool>>(m_srcValues)){
        // Fix: Use std::get to access the value
        const auto& boolValues = std::get<QList<bool>>(m_srcValues);
        if (boolValues[m_currIdx]) {
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
    else { // QList<int> - show the number at the current index
        const auto& intValues = std::get<QList<int>>(m_srcValues);
        painter->setBrush(QColor(200, 200, 255));
        painter->drawRect(m_rect);

        painter->setPen(QPen(Qt::black, 2));
        painter->drawText(m_rect, Qt::AlignCenter, QString::number(intValues[m_currIdx]));
    }
}

void SourceItem::addPorts()
{
    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;
    double diffHeight = m_rect.height() / (m_numOutputs + 1);
    double currHeight = -halfHeight;

    for (int i = 0; i < m_numOutputs; i++) {
        currHeight += diffHeight;
        PortItem* outputPort = new PortItem(PortType::OUT, i, this);
        outputPort->setPos(halfWidth, currHeight);
        m_outputPorts.append(outputPort);
    }
}

void SourceItem::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // Check if all output ports are empty
        bool allPortsEmpty = true;
        
        for (PortItem* port : std::as_const(m_outputPorts)) {
            if (!port->getConnections().isEmpty()) {
                allPortsEmpty = false;
                break;
            }
        }
        
        // If all ports are empty, remove the item
        if (allPortsEmpty) {
            scene()->removeItem(this);
            deleteLater();
        }
    }
    else {
        QGraphicsObject::keyPressEvent(event);
    }
}

void SourceItem::mousePressEvent(QGraphicsSceneMouseEvent* event){
    emit setOverlay(this);
}

//===================== RegisterItem ========================

RegisterItem::RegisterItem(RType RegType,bool isFlipFlop, bool hasEnable, QGraphicsItem* parent):m_regType(RegType) {
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    m_rect = QRectF(-20, -30, 40, 60);

    createPorts(isFlipFlop, hasEnable);
}

void RegisterItem::createPorts(bool isFlipFlop, bool hasEnable)
{
    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;

    PortItem* inputPort1 = new PortItem(PortType::IN, 0, this);
    inputPort1->setPos(-halfWidth, - halfHeight + halfHeight / 8);
    m_inputPortOne =inputPort1;

    PortItem* inputPort2 = new PortItem(PortType::IN, 1, this);
    inputPort2->setPos(-halfWidth, -halfHeight / 2 );
    m_inputPortTwo =inputPort2;

    if(isFlipFlop){
        m_clkPort = new PortItem(PortType::IN, 2, this);
        m_clkPort->setPos(-halfWidth, 0);
    }

    if(hasEnable){
        m_readEnbPort = new PortItem(PortType::IN, 3, this);
        m_readEnbPort->setPos(-halfWidth, halfHeight/2); // Right side, center
    }
    m_outputPort = new PortItem(PortType::OUT, -1, this);
    m_outputPort->setPos(halfWidth, 0); // Right side, center
}

void RegisterItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {

    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(Qt::blue, 3));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_rect.adjusted(-2, -2, 2, 2));
    }

    painter->setPen(QPen(Qt::black, 4));
    painter->drawRect(m_rect);

    if(m_value)
    {
        painter->setBrush(QColor(100, 150, 255)); // Blue color (like in RegisterButton)
        painter->setPen(QPen(Qt::white, 2));
    }
    else
    {
        painter->setBrush(QColor(255, 255, 255));
        painter->setPen(QPen(Qt::black, 2));
    }

    painter->fillRect(m_rect, painter->brush());

    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(15);
    painter->setFont(font);
    painter->drawText(m_rect, Qt::AlignCenter, "R");
}

void RegisterItem::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // Check if all ports are empty
        bool allPortsEmpty = true;
        
        // Check input ports
        if (m_inputPortOne && !m_inputPortOne->getConnections().isEmpty()) {
            allPortsEmpty = false;
        }
        if (m_inputPortTwo && !m_inputPortTwo->getConnections().isEmpty()) {
            allPortsEmpty = false;
        }
        if (m_clkPort && !m_clkPort->getConnections().isEmpty()) {
            allPortsEmpty = false;
        }
        if (m_readEnbPort && !m_readEnbPort->getConnections().isEmpty()) {
            allPortsEmpty = false;
        }
        
        // Check output port
        if (m_outputPort && !m_outputPort->getConnections().isEmpty()) {
            allPortsEmpty = false;
        }
        
        // If all ports are empty, remove the item
        if (allPortsEmpty) {
            scene()->removeItem(this);
            deleteLater();
        }
    } else {
        QGraphicsObject::keyPressEvent(event);
    }
}

//===================== MuxItem ========================

MuxItem::MuxItem(MType MuxType, int numInputs, QGraphicsItem* parent): m_muxType(MuxType),m_numInputs(numInputs) {

    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    m_rect = QRectF(-20, -30, 40, 60);

    createPorts();
}


void MuxItem::createPorts()
{
    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;
    double diffHeight = m_rect.height() / (m_numInputs + 1);
    double currHeight = -halfHeight;
    double currWidth  = halfWidth;

    //data ports
    for (int i = 0; i < m_numInputs; i++) {
        currHeight += diffHeight;
        PortItem* inputPort = new PortItem(PortType::IN, i, this);
        inputPort->setPos(-halfWidth, currHeight);
        m_inputDataPorts.append(inputPort);
    }

    m_numInputs = static_cast<int>(ceil(log(m_numInputs)));
    double diffWidth = m_rect.width() / (m_numInputs + 1);

    //address ports
    for (int i = 0; i < m_numInputs; i++) {
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

    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(Qt::blue, 3));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_rect.adjusted(-2, -2, 2, 2));
    }

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
    painter->setBrush(QColor(255, 164, 0));
    painter->setPen(QPen(Qt::black, 4));
    painter->drawPath(path);
    painter->fillPath(path, painter->brush());
}

void MuxItem::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // Check if all ports are empty
        bool allPortsEmpty = true;
        
        // Check input data ports
        for (PortItem* port : std::as_const(m_inputDataPorts)) {
            if (!port->getConnections().isEmpty()) {
                allPortsEmpty = false;
                break;
            }
        }
        
        // Check input address ports
        if (allPortsEmpty) {
            for (PortItem* port : std::as_const(m_inputAddressPorts)) {
                if (!port->getConnections().isEmpty()) {
                    allPortsEmpty = false;
                    break;
                }
            }
        }
        
        // Check output port
        if (allPortsEmpty && m_outputPort && !m_outputPort->getConnections().isEmpty()) {
            allPortsEmpty = false;
        }
        
        // If all ports are empty, remove the item
        if (allPortsEmpty) {
            scene()->removeItem(this);
            deleteLater();
        }
    } else {
        QGraphicsObject::keyPressEvent(event);
    }
}

//===================== DisplayItem ========================

DisplayItem::DisplayItem(int numInputs,int numOutputs, QGraphicsItem* parent):
    m_numInputs(numInputs),m_numOutputs(numOutputs) {

    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);

    m_rect = QRectF(-20, -30, 40, 60);

    createPorts();
}

void DisplayItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    // Draw selection highlight
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(Qt::blue, 3));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(m_rect.adjusted(-2, -2, 2, 2));
    }

    // Draw outer border and background
    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(QColor(40, 40, 40)); // Dark gray background
    painter->drawRect(m_rect);

    // Draw inner display area (LED-style display)
    QRectF displayArea = m_rect.adjusted(5, 5, -5, -5);
    painter->setPen(QPen(QColor(20, 20, 20), 1));
    painter->setBrush(QColor(20, 60, 20)); // Dark green background (like old LED displays)
    painter->drawRect(displayArea);

    // Draw the value
    painter->setPen(QPen(QColor(0, 255, 0), 2)); // Bright green LED color
    QFont font = painter->font();
    font.setBold(true);
    font.setFamily("Courier");
    font.setPointSize(16);
    painter->setFont(font);
    
    QString displayText = QString::number(m_value);
    painter->drawText(displayArea, Qt::AlignCenter, displayText);

    // Add small "DISPLAY" label at the bottom
    QFont labelFont = painter->font();
    labelFont.setBold(false);
    labelFont.setPointSize(6);
    painter->setFont(labelFont);
    painter->setPen(QPen(QColor(180, 180, 180), 1));
    QRectF labelRect(m_rect.left(), m_rect.bottom() - 10, m_rect.width(), 8);
    painter->drawText(labelRect, Qt::AlignCenter, "DISPLAY");
}

void DisplayItem::createPorts()
{
    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;
    double diffHeight = m_rect.height() / (m_numInputs + 1);
    double currHeight = -halfHeight;


    for (int i = 0; i < m_numInputs; i++) {
        currHeight += diffHeight;
        PortItem* inputPort = new PortItem(PortType::IN, i, this);
        inputPort->setPos(-halfWidth, currHeight);
        m_inputPorts.append(inputPort);
    }
    currHeight = -halfHeight;
    diffHeight = m_rect.height() / (m_numOutputs + 1);
    for (int i = 0; i < m_numOutputs; i++) {
        currHeight += diffHeight;
        PortItem* outputPort = new PortItem(PortType::OUT, m_numInputs+i, this);
        outputPort->setPos(halfWidth, currHeight);
        m_outputPorts.append(outputPort);
    }
}

void DisplayItem::updateValue()
{
    int value = 0;
    for(int i = m_numInputs - 1; i >= 0; i--) {
        value = value << 1;  // Shift first
        if (m_inputPorts[i]->getValue()) {
            value |= 1;  // Set the least significant bit
        }
    }
    m_value = value;  // Store the calculated value
}

void DisplayItem::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // Check if all ports are empty
        bool allPortsEmpty = true;
        
        // Check input ports
        for (PortItem* port : std::as_const(m_inputPorts)) {
            if (!port->getConnections().isEmpty()) {
                allPortsEmpty = false;
                break;
            }
        }
        
        // Check output ports
        if (allPortsEmpty) {
            for (PortItem* port : std::as_const(m_outputPorts)) {
                if (!port->getConnections().isEmpty()) {
                    allPortsEmpty = false;
                    break;
                }
            }
        }
        
        // If all ports are empty, remove the item
        if (allPortsEmpty) {
            scene()->removeItem(this);
            deleteLater();
        }
    } else {
        QGraphicsObject::keyPressEvent(event);
    }
}
