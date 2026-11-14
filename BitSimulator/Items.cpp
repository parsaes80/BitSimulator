#include "Items.h"
#include <QFontMetricsF>
#include <QMouseEvent>
#include <qscrollbar.h>
#include <algorithm>
//===================== PortItem   ========================

extern bool sim_values_visible;

PortItem::PortItem(PortType portType, int pinIndex, QGraphicsItem* parent)
    : QGraphicsEllipseItem(-4, -4, 8, 8, parent)
    ,m_pinIndex(pinIndex)
    , m_portType(portType)
{
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsMovable, false);
}

void PortItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    // Colors are chosen here instead of with setPen()/setBrush(), which would schedule another repaint
    const CanvasTheme& theme = canvasTheme();
    const bool wired = !m_connections.isEmpty();
    QColor ring = theme.pin;
    QColor fill = theme.background;              // hollow: nothing connected
    if (m_highlighted) {
        ring = fill = theme.snap;
    }
    else if (wired) {
        ring = fill = !sim_values_visible ? theme.pinWired : (m_value ? theme.wireHigh : theme.wireLow);
    }

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(ring, 1.5));
    painter->setBrush(fill);
    painter->drawEllipse(rect().adjusted(0.75, 0.75, -0.75, -0.75));
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

//===================== ComponentItem ========================

ComponentItem::ComponentItem(QGraphicsItem* parent) : QGraphicsObject(parent)
{
    setFlag(QGraphicsObject::ItemIsMovable, true);
    setFlag(QGraphicsObject::ItemIsSelectable, true);
    setFlag(QGraphicsObject::ItemSendsGeometryChanges, true);
}

bool ComponentItem::hasConnections() const
{
    return std::ranges::any_of(ports(), [](const PortItem* port) { return port && !port->getConnections().isEmpty(); });
}

void ComponentItem::drawSelection(QPainter* painter, const QStyleOptionGraphicsItem* option) const
{
    if (option->state & QStyle::State_Selected) {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(canvasTheme().selection, 1.5, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(m_rect.adjusted(-SelectionMargin, -SelectionMargin, SelectionMargin, SelectionMargin), 5, 5);
    }
}

ComponentItem::EditResult ComponentItem::checkRemovable(const QList<PortItem*>& ports, int newCount, const QString& what)
{
    for (int i = std::max(newCount, 0); i < ports.size(); i++) {
        if (!ports[i]->getConnections().isEmpty()) {
            return std::unexpected(tr("%1 %2 is wired. Remove its wire first.").arg(what).arg(i + 1));
        }
    }
    return {};
}

ComponentItem::EditResult ComponentItem::checkRemovable(const PortItem* port, bool stillWanted, const QString& what)
{
    if (port && !stillWanted && !port->getConnections().isEmpty()) {
        return std::unexpected(tr("The %1 pin is wired. Remove its wire first.").arg(what));
    }
    return {};
}

void ComponentItem::resizePorts(QList<PortItem*>& ports, int count, PortType type)
{
    while (ports.size() > count) {
        delete ports.takeLast();          // callers have checked that it has no wires
    }
    while (ports.size() < count) {
        ports.append(new PortItem(type, ports.size(), this));
    }
}

void ComponentItem::setOptionalPort(PortItem*& port, bool wanted, PortType type)
{
    if (wanted && !port) {
        port = new PortItem(type, -1, this);
    } else if (!wanted && port) {
        delete port;                      // callers have checked that it has no wires
        port = nullptr;
    }
}

void ComponentItem::updateWires() const
{
    for (const PortItem* port : ports()) {
        if (!port) continue;
        for (WireItem* wire : port->getConnections()) {
            if (wire->isConnected()) {
                wire->updateWirePosition();
                wire->update();
            }
        }
    }
}

//===================== GateItem   ========================

GateItem::GateItem(const GateProps& props, QGraphicsItem* parent)
    : ComponentItem(parent), m_props(normalized(props))
{
    m_outputPort = new PortItem(PortType::OUT, -1, this);
    resizePorts(m_inputPorts, m_props.numInputs, PortType::IN);
    layoutPorts();
}

ComponentItem::EditResult GateItem::setProps(const ItemProps& props)
{
    const auto* requested = std::get_if<GateProps>(&props);
    if (!requested) return std::unexpected(tr("These properties do not belong to a gate."));

    const GateProps next = normalized(*requested);
    if (auto ok = checkRemovable(m_inputPorts, next.numInputs, tr("Input")); !ok) return ok;

    prepareGeometryChange();
    m_props = next;
    resizePorts(m_inputPorts, m_props.numInputs, PortType::IN);
    layoutPorts();
    return {};
}

void GateItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{

    drawSelection(painter, option);

    // Every gate shape below draws with this pen and brush: a dark body, outlined in the gate's color
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(canvasTheme().gateColor(m_props.type), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(canvasTheme().fillFor(canvasTheme().gateColor(m_props.type)));
    drawGateShape(painter);
}

void GateItem::drawGateShape(QPainter* painter)
{
    switch (m_props.type) {
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
    double width = m_rect.width();
    double halfWidth = width / 2;
    double bubbleSize = 10;
    
    // Draw centered at y=0 by offsetting y by -bubbleSize/2
    painter->drawEllipse(halfWidth, -bubbleSize/2, bubbleSize, bubbleSize);
}

void GateItem::layoutPorts()
{
    // The body grows with the number of inputs; a NOT gate has a fixed size
    int x = -25;
    int y = x - ((m_props.numInputs - 2) * 5) + 3;
    int height = y * -2;
    if (m_props.type == GType::NOT) {
        y = -22;
        height = 44;
    }
    m_rect = QRectF(x, y, 50, height);

    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;
    double diffHeight = m_rect.height() / (m_inputPorts.size() + 1);
    double currHeight = -halfHeight;

    for (PortItem* inputPort : std::as_const(m_inputPorts)) {
        currHeight += diffHeight;
        inputPort->setPos(-halfWidth, currHeight);
    }

    bool hasNotBubble = (m_props.type == GType::NAND ||
                         m_props.type == GType::NOR ||
                         m_props.type == GType::XNOR);

    m_outputPort->setPos(hasNotBubble ? halfWidth + 14 : halfWidth, 0);   // make room for the bubble

    updateWires();
    update();
}

//===================== Wire Item   ========================

WireItem::WireItem(QPointF startpos, QPointF endpos, QGraphicsItem* parent)
    : localStartPos(startpos), localEndPos(endpos), QGraphicsObject(parent), m_pen(QPen(canvasTheme().wire, 2))
{
    setFlags(ItemIsSelectable);
}

QRectF WireItem::boundingRect() const
{
    // Handle case where positions aren't set yet
    if (localStartPos.isNull() && localEndPos.isNull()) {
        return QRectF(0, 0, 1, 1); 
    }

    qreal penWidth = m_pen.width();
    qreal extra = penWidth / 2.0 + 5; // Small extra margin for selection

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
    // Horizontal, vertical, horizontal: the vertical part sits halfway between the two ends
    const qreal midX = (localStartPos.x() + localEndPos.x()) / 2.0;
    QPainterPath path(localStartPos);
    path.lineTo(midX, localStartPos.y());
    path.lineTo(midX, localEndPos.y());
    path.lineTo(localEndPos);

    painter->setRenderHint(QPainter::Antialiasing);

    // A wire still being dragged keeps the pen the scene gave it
    if (!m_startPort || !m_endPort) {
        painter->strokePath(path, m_pen);
        return;
    }

    const CanvasTheme& theme = canvasTheme();
    QPen pen = m_pen;
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setColor(!sim_values_visible ? theme.wire : (m_value ? theme.wireHigh : theme.wireLow));

    if (isSelected()) {
        pen.setColor(theme.selection);
        pen.setWidthF(3);
    }
    else if (sim_values_visible && m_value) {
        // A soft halo under a wire that carries a 1
        QPen glow = pen;
        QColor haze = pen.color();
        haze.setAlpha(60);
        glow.setColor(haze);
        glow.setWidthF(pen.widthF() + 5);
        painter->strokePath(path, glow);
    }

    painter->strokePath(path, pen);
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
    prepareGeometryChange();   // the bounding rect follows the two ends

    // Get the scene positions of both ports
    QPointF startPos = m_startPort->mapToScene(QPointF(0, 0));
    QPointF endPos = m_endPort->mapToScene(QPointF(0, 0));

    // Convert to this item's coordinate system
    localStartPos = mapFromScene(startPos);
    localEndPos = mapFromScene(endPos);
    
}

//===================== SourceItem ========================

SourceItem::SourceItem(const SourceProps& props, QGraphicsItem* parent)
    : ComponentItem(parent), m_props(normalized(props))
{
    resizePorts(m_outputPorts, m_props.bits, PortType::OUT);
    layoutPorts();
}

ComponentItem::EditResult SourceItem::setProps(const ItemProps& props)
{
    const auto* requested = std::get_if<SourceProps>(&props);
    if (!requested) return std::unexpected(tr("These properties do not belong to a source."));

    const SourceProps next = normalized(*requested);
    if (auto ok = checkRemovable(m_outputPorts, next.bits, tr("Output")); !ok) return ok;

    prepareGeometryChange();
    m_props = next;
    m_tick = 0;
    resizePorts(m_outputPorts, m_props.bits, PortType::OUT);
    layoutPorts();
    return {};
}

void SourceItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    drawSelection(painter, option);

    const CanvasTheme& theme = canvasTheme();
    painter->setRenderHint(QPainter::Antialiasing);
    QFont font = painter->font();
    font.setBold(true);
    painter->setFont(font);

    // The value at the tick the simulator last reported (tick 0, the first value, when not running)
    const u64 current = value();
    const QString text = QString::number(current);
    const bool lit = m_props.bits == 1 && current != 0 && sim_values_visible;   // a 1-pin source showing a 1

    painter->setPen(QPen(lit ? theme.wireHigh : theme.source, 2));
    painter->setBrush(lit ? theme.wireHigh : theme.fillFor(theme.source));
    painter->drawRoundedRect(m_rect, 3, 3);
    painter->setPen(lit ? theme.background : theme.componentText);

    // A wide source is a tall, narrow box: run a number that does not fit across it along its length,
    // and shrink the font if it does not fit that way either
    QRectF box = m_rect;
    painter->save();
    if (QFontMetricsF(font).horizontalAdvance(text) > box.width() - 4 && box.height() > box.width()) {
        painter->translate(box.center());
        painter->rotate(-90);
        box = QRectF(-m_rect.height() / 2, -m_rect.width() / 2, m_rect.height(), m_rect.width());
    }
    const qreal needed = QFontMetricsF(font).horizontalAdvance(text);
    if (needed > box.width() - 4) {
        font.setPointSizeF(std::max(5.0, font.pointSizeF() * (box.width() - 4) / needed));
        painter->setFont(font);
    }
    painter->drawText(box, Qt::AlignCenter, text);
    painter->restore();
}

void SourceItem::setTick(u64 tick)
{
    if (tick == m_tick) return;
    const bool changed = sourceValueAt(m_props, tick) != value();
    m_tick = tick;
    if (changed) update();
}

void SourceItem::layoutPorts()
{
    int x = -15;
    int y = x - ((m_props.bits - 1) * 5);
    int height = y * -2;
    m_rect = QRectF(x, y, 30, height);

    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;
    double diffHeight = m_rect.height() / (m_outputPorts.size() + 1);
    double currHeight = -halfHeight;

    for (PortItem* outputPort : std::as_const(m_outputPorts)) {
        currHeight += diffHeight;
        outputPort->setPos(halfWidth, currHeight);
    }

    updateWires();
    update();
}

//===================== RegisterItem ========================

RegisterItem::RegisterItem(const RegisterProps& props, QGraphicsItem* parent)
    : ComponentItem(parent), m_props(normalized(props))
{
    m_rect = QRectF(-20, -30, 40, 60);

    m_inputPortOne = new PortItem(PortType::IN, 0, this);
    m_inputPortTwo = new PortItem(PortType::IN, 1, this);
    m_outputPort = new PortItem(PortType::OUT, -1, this);
    layoutPorts();
}

QList<PortItem*> RegisterItem::ports() const
{
    QList<PortItem*> all{m_inputPortOne, m_inputPortTwo, m_outputPort};
    if (m_clkPort) all.append(m_clkPort);
    if (m_readEnbPort) all.append(m_readEnbPort);
    return all;
}

ComponentItem::EditResult RegisterItem::setProps(const ItemProps& props)
{
    const auto* requested = std::get_if<RegisterProps>(&props);
    if (!requested) return std::unexpected(tr("These properties do not belong to a register."));

    const RegisterProps next = normalized(*requested);
    if (auto ok = checkRemovable(m_clkPort, next.edgeTriggered, tr("clock")); !ok) return ok;
    if (auto ok = checkRemovable(m_readEnbPort, next.hasEnable, tr("enable")); !ok) return ok;

    m_props = next;
    layoutPorts();
    return {};
}

void RegisterItem::layoutPorts()
{
    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;

    // The clock pin exists only on flip-flops, the enable pin only when requested
    setOptionalPort(m_clkPort, m_props.edgeTriggered, PortType::IN);
    setOptionalPort(m_readEnbPort, m_props.hasEnable, PortType::IN);

    m_inputPortOne->setPos(-halfWidth, -halfHeight + halfHeight / 8);
    m_inputPortTwo->setPos(-halfWidth, -halfHeight / 2);
    if (m_clkPort) m_clkPort->setPos(-halfWidth, 0);
    if (m_readEnbPort) m_readEnbPort->setPos(-halfWidth, halfHeight / 2);
    m_outputPort->setPos(halfWidth, 0); // Right side, center

    updateWires();
    update();
}

void RegisterItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {

    drawSelection(painter, option);

    // A register that holds a 1 lights up like a wire carrying one
    const CanvasTheme& theme = canvasTheme();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(m_value ? theme.wireHigh : theme.reg, 2));
    painter->setBrush(m_value ? theme.wireHigh : theme.fillFor(theme.reg));
    painter->drawRoundedRect(m_rect, 3, 3);
    painter->setPen(m_value ? theme.background : theme.componentText);

    // Label the register with its type so an edit in the properties panel is visible on the canvas
    const QString label = registerTypeName(m_props.type);
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSize(label.size() > 1 ? 12 : 15);
    painter->setFont(font);
    painter->drawText(m_rect, Qt::AlignCenter, label);
}

//===================== MuxItem ========================

MuxItem::MuxItem(const MuxProps& props, QGraphicsItem* parent)
    : ComponentItem(parent), m_props(normalized(props))
{
    m_rect = QRectF(-20, -30, 40, 60);

    m_outputPort = new PortItem(PortType::OUT, -1, this);
    resizePorts(m_inputDataPorts, m_props.numInputs, PortType::IN);
    resizePorts(m_inputAddressPorts, muxSelectLines(m_props.numInputs), PortType::IN);
    layoutPorts();
}

ComponentItem::EditResult MuxItem::setProps(const ItemProps& props)
{
    const auto* requested = std::get_if<MuxProps>(&props);
    if (!requested) return std::unexpected(tr("These properties do not belong to a multiplexer."));

    const MuxProps next = normalized(*requested);
    const int selectLines = muxSelectLines(next.numInputs);
    if (auto ok = checkRemovable(m_inputDataPorts, next.numInputs, tr("Input")); !ok) return ok;
    if (auto ok = checkRemovable(m_inputAddressPorts, selectLines, tr("Select line")); !ok) return ok;

    m_props = next;
    resizePorts(m_inputDataPorts, m_props.numInputs, PortType::IN);
    resizePorts(m_inputAddressPorts, selectLines, PortType::IN);
    layoutPorts();
    return {};
}

void MuxItem::layoutPorts()
{
    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;

    // Data inputs down the left side
    double diffHeight = m_rect.height() / (m_inputDataPorts.size() + 1);
    double currHeight = -halfHeight;
    for (PortItem* inputPort : std::as_const(m_inputDataPorts)) {
        currHeight += diffHeight;
        inputPort->setPos(-halfWidth, currHeight);
    }

    // Select lines along the bottom, from right to left
    double diffWidth = m_rect.width() / (m_inputAddressPorts.size() + 1);
    double currWidth = halfWidth;
    for (PortItem* addressPort : std::as_const(m_inputAddressPorts)) {
        currWidth -= diffWidth;
        addressPort->setPos(currWidth, halfHeight);
    }

    m_outputPort->setPos(halfWidth, 0); // Right side, center

    updateWires();
    update();
}

void MuxItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget){

    drawSelection(painter, option);

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
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(canvasTheme().mux, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(canvasTheme().fillFor(canvasTheme().mux));
    painter->drawPath(path);
}

//===================== DisplayItem ========================

DisplayItem::DisplayItem(const DisplayProps& props, QGraphicsItem* parent)
    : ComponentItem(parent), m_props(normalized(props))
{
    resizePorts(m_inputPorts, m_props.numInputs, PortType::IN);
    resizePorts(m_outputPorts, m_props.numOutputs, PortType::OUT);
    layoutPorts();
}

ComponentItem::EditResult DisplayItem::setProps(const ItemProps& props)
{
    const auto* requested = std::get_if<DisplayProps>(&props);
    if (!requested) return std::unexpected(tr("These properties do not belong to a display."));

    const DisplayProps next = normalized(*requested);
    if (auto ok = checkRemovable(m_inputPorts, next.numInputs, tr("Input")); !ok) return ok;
    if (auto ok = checkRemovable(m_outputPorts, next.numOutputs, tr("Output")); !ok) return ok;

    prepareGeometryChange();
    m_props = next;
    resizePorts(m_inputPorts, m_props.numInputs, PortType::IN);
    resizePorts(m_outputPorts, m_props.numOutputs, PortType::OUT);
    layoutPorts();
    return {};
}

void DisplayItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    drawSelection(painter, option);

    const CanvasTheme& theme = canvasTheme();
    painter->setRenderHint(QPainter::Antialiasing);

    // Casing, outlined in the display's color
    painter->setPen(QPen(theme.display, 2));
    painter->setBrush(theme.fillFor(theme.display));
    painter->drawRoundedRect(m_rect, 4, 4);

    // The screen: as dark as the canvas
    QRectF displayArea = m_rect.adjusted(5, 5, -5, -5);
    painter->setPen(Qt::NoPen);
    painter->setBrush(theme.background);
    painter->drawRoundedRect(displayArea, 2, 2);

    // The value
    painter->setPen(QPen(theme.display, 2));
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
    painter->setPen(QPen(theme.pin, 1));
    QRectF labelRect(m_rect.left(), m_rect.bottom() - 10, m_rect.width(), 8);
    painter->drawText(labelRect, Qt::AlignCenter, "DISPLAY");
}

void DisplayItem::layoutPorts()
{
    int bitWidth = std::max(m_props.numInputs, m_props.numOutputs);
    int x = -23;
    int y = x - (bitWidth * 4);
    int height = y * -2;
    int width = x * -2;
    m_rect = QRectF(x, y, width, height);

    double halfWidth = m_rect.width() / 2;
    double halfHeight = m_rect.height() / 2;

    double diffHeight = m_rect.height() / (m_inputPorts.size() + 1);
    double currHeight = -halfHeight;
    for (PortItem* inputPort : std::as_const(m_inputPorts)) {
        currHeight += diffHeight;
        inputPort->setPos(-halfWidth, currHeight);
    }

    diffHeight = m_rect.height() / (m_outputPorts.size() + 1);
    currHeight = -halfHeight;
    for (PortItem* outputPort : std::as_const(m_outputPorts)) {
        currHeight += diffHeight;
        outputPort->setPos(halfWidth, currHeight);
    }

    updateWires();
    update();
}

void DisplayItem::updateValue()
{
    int value = 0;
    for(int i = m_inputPorts.size() - 1; i >= 0; i--) {
        value = value << 1;  // Shift first
        if (m_inputPorts[i]->getValue()) {
            value |= 1;  // Set the least significant bit
        }
    }
    m_value = value;  // Store the calculated value
}
