#include "Items.h"
#include <QFontMetricsF>
#include <QPainterPath>
#include <algorithm>

using namespace Qt::StringLiterals;

//===================== PortItem   ========================

bool sim_values_visible = false;

PortItem::PortItem(PortType portType, QGraphicsItem* parent)
    : QGraphicsEllipseItem(-4, -4, 8, 8, parent), m_portType(portType)
{
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsMovable, false);
}

void PortItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    // Colors are chosen here instead of with setPen()/setBrush(), which would schedule another repaint
    const Theme& theme = currentTheme();
    const bool wired = !m_connections.isEmpty();
    QColor ring = theme.pin;
    QColor fill = theme.canvas;              // hollow: nothing connected
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

void PortItem::setValue(bool value)
{
    if (value == m_value) return;
    m_value = value;
    update();
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
        painter->setPen(QPen(currentTheme().selection, 1.5, Qt::DashLine));
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
        ports.append(new PortItem(type, this));
    }
}

void ComponentItem::setOptionalPort(PortItem*& port, bool wanted, PortType type)
{
    if (wanted && !port) {
        port = new PortItem(type, this);
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
    m_outputPort = new PortItem(PortType::OUT, this);
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

void GateItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    drawSelection(painter, option);

    // A dark body, outlined in the gate's color
    const Theme& theme = currentTheme();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(theme.gateColor(m_props.type), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(theme.fillFor(theme.gateColor(m_props.type)));
    drawGateSymbol(painter, m_props.type, m_rect);
}

void drawGateSymbol(QPainter* painter, GType type, const QRectF& body)
{
    const double halfWidth = body.width() / 2, halfHeight = body.height() / 2;
    painter->save();
    painter->translate(body.center());

    QPainterPath shape;
    shape.moveTo(-halfWidth, -halfHeight);
    switch (type) {
    case GType::AND:
    case GType::NAND:
        shape.lineTo(0, -halfHeight);
        shape.arcTo(0, -halfHeight, halfWidth, body.height(), 90, -180);
        shape.lineTo(-halfWidth, halfHeight);
        break;
    case GType::OR:
    case GType::NOR:
    case GType::XOR:
    case GType::XNOR:
        shape.quadTo(-halfWidth / 2, 0, -halfWidth, halfHeight);
        shape.lineTo(halfWidth / 2, halfHeight);
        shape.quadTo(body.width() * 3 / 4, 0, halfWidth / 2, -halfHeight);
        break;
    case GType::NOT:
        shape.lineTo(-halfWidth, halfHeight);
        shape.lineTo(halfWidth, 0);
        break;
    }
    shape.closeSubpath();
    painter->drawPath(shape);

    if (type == GType::XOR || type == GType::XNOR) {
        // The second curve on the input side
        const double offset = body.width() * 0.1;
        QPainterPath curve;
        curve.moveTo(-halfWidth - offset, -halfHeight * 0.9);
        curve.quadTo(-halfWidth * 0.6, 0, -halfWidth - offset, halfHeight * 0.9);
        painter->strokePath(curve, painter->pen());
    }
    if (type == GType::NAND || type == GType::NOR || type == GType::XNOR) {
        const double bubble = std::min(10.0, body.width() * 0.2);
        painter->drawEllipse(QRectF(halfWidth, -bubble / 2, bubble, bubble));
    }
    painter->restore();
}

void GateItem::layoutPorts()
{
    // The body grows with the number of inputs; a NOT gate has a fixed size
    const QSizeF size = componentSize(m_props);
    m_rect = QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height());

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
    : QGraphicsObject(parent), localStartPos(startpos), localEndPos(endpos), m_pen(QPen(currentTheme().wire, 2))
{
    setFlags(ItemIsSelectable);
}

QRectF WireItem::boundingRect() const
{
    const qreal extra = m_pen.widthF() / 2 + 5;   // room for the pen and the glow of a wire carrying 1
    return QRectF(localStartPos, localEndPos).normalized().adjusted(-extra, -extra, extra, extra);
}

QPainterPath WireItem::path() const
{
    // The vertical part sits halfway between the two ends
    const qreal midX = (localStartPos.x() + localEndPos.x()) / 2.0;
    QPainterPath path(localStartPos);
    path.lineTo(midX, localStartPos.y());
    path.lineTo(midX, localEndPos.y());
    path.lineTo(localEndPos);
    return path;
}

QPainterPath WireItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(10);   // a click this close to the wire selects it
    return stroker.createStroke(path());
}

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
    const QPainterPath path = this->path();
    painter->setRenderHint(QPainter::Antialiasing);

    // A wire still being dragged keeps the pen the scene gave it
    if (!m_startPort || !m_endPort) {
        painter->strokePath(path, m_pen);
        return;
    }

    const Theme& theme = currentTheme();
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
void WireItem::setValue(bool value)
{
    if (value == m_value) return;
    m_value = value;
    update();
}

void WireItem::setEnds(QPointF start, QPointF end)
{
    prepareGeometryChange();
    localStartPos = mapFromScene(start);
    localEndPos = mapFromScene(end);
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

//===================== Port names ========================

namespace {
// The name of a module port on a source or display, in a strip across the top of it
constexpr double NameHeight = 16;
QFont nameFont()
{
    QFont font;
    font.setPointSizeF(8);
    font.setBold(true);
    return font;
}
double nameWidth(const QString& name)
{
    return name.isEmpty() ? 0 : QFontMetricsF(nameFont()).horizontalAdvance(name) + 12;
}
void drawName(QPainter* painter, const QRectF& rect, const QString& name, const QColor& text, const QColor& line)
{
    if (name.isEmpty()) return;
    painter->save();
    painter->setFont(nameFont());
    painter->setPen(text);
    painter->drawText(QRectF(rect.left(), rect.top() + 1, rect.width(), NameHeight - 1), Qt::AlignCenter, name);
    painter->setPen(QPen(line, 1));
    painter->drawLine(QPointF(rect.left() + 3, rect.top() + NameHeight), QPointF(rect.right() - 3, rect.top() + NameHeight));
    painter->restore();
}
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

    const Theme& theme = currentTheme();
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
    painter->setPen(lit ? theme.canvas : theme.componentText);
    drawName(painter, m_rect, m_name, lit ? theme.canvas : theme.componentText, lit ? theme.canvas : theme.source);

    // A wide source is a tall, narrow box: run a number that does not fit across it along its length,
    // and shrink the font if it does not fit that way either
    const QRectF body = m_rect.adjusted(0, m_name.isEmpty() ? 0 : NameHeight, 0, 0);
    QRectF box = body;
    painter->save();
    if (QFontMetricsF(font).horizontalAdvance(text) > box.width() - 4 && box.height() > box.width()) {
        painter->translate(box.center());
        painter->rotate(-90);
        box = QRectF(-body.height() / 2, -body.width() / 2, body.height(), body.width());
    }
    const qreal needed = QFontMetricsF(font).horizontalAdvance(text);
    if (needed > box.width() - 4) {
        font.setPointSizeF(std::max(5.0, font.pointSizeF() * (box.width() - 4) / needed));
        painter->setFont(font);
    }
    painter->drawText(box, Qt::AlignCenter, text);
    painter->restore();
}

void SourceItem::setName(const QString& name)
{
    prepareGeometryChange();
    m_name = name;
    layoutPorts();
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
    // The body holds the value and the pins; a name strip goes on top of it
    const QSizeF size = componentSize(m_props, m_name);
    const double header = m_name.isEmpty() ? 0 : NameHeight;
    const double bodyHeight = size.height() - header;
    m_rect = QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height());

    double halfWidth = m_rect.width() / 2;
    double diffHeight = bodyHeight / (m_outputPorts.size() + 1);
    double currHeight = m_rect.top() + header;

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
    const QSizeF size = componentSize(m_props);
    m_rect = QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height());

    m_inputPortOne = new PortItem(PortType::IN, this);
    m_inputPortTwo = new PortItem(PortType::IN, this);
    m_outputPort = new PortItem(PortType::OUT, this);
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

void RegisterItem::setValue(bool value)
{
    if (value == m_value) return;
    m_value = value;
    update();
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
    const Theme& theme = currentTheme();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(m_value ? theme.wireHigh : theme.reg, 2));
    painter->setBrush(m_value ? theme.wireHigh : theme.fillFor(theme.reg));
    painter->drawRoundedRect(m_rect, 3, 3);
    painter->setPen(m_value ? theme.canvas : theme.componentText);

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
    const QSizeF size = componentSize(m_props);
    m_rect = QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height());

    m_outputPort = new PortItem(PortType::OUT, this);
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
    painter->setPen(QPen(currentTheme().mux, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(currentTheme().fillFor(currentTheme().mux));
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

    const Theme& theme = currentTheme();
    painter->setRenderHint(QPainter::Antialiasing);

    // Casing, outlined in the display's color
    painter->setPen(QPen(theme.display, 2));
    painter->setBrush(theme.fillFor(theme.display));
    painter->drawRoundedRect(m_rect, 4, 4);

    drawName(painter, m_rect, m_name, theme.componentText, theme.display);

    // The screen: as dark as the canvas
    const QRectF body = m_rect.adjusted(0, m_name.isEmpty() ? 0 : NameHeight, 0, 0);
    QRectF displayArea = body.adjusted(5, 5, -5, -5);
    painter->setPen(Qt::NoPen);
    painter->setBrush(theme.canvas);
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

    // A small "DISPLAY" label at the bottom, unless the display has a name
    if (!m_name.isEmpty()) return;
    QFont labelFont = painter->font();
    labelFont.setBold(false);
    labelFont.setPointSize(6);
    painter->setFont(labelFont);
    painter->setPen(QPen(theme.pin, 1));
    QRectF labelRect(m_rect.left(), m_rect.bottom() - 10, m_rect.width(), 8);
    painter->drawText(labelRect, Qt::AlignCenter, "DISPLAY");
}

void DisplayItem::setName(const QString& name)
{
    prepareGeometryChange();
    m_name = name;
    layoutPorts();
}

void DisplayItem::layoutPorts()
{
    // The body holds the screen and the pins; a name strip goes on top of it
    const QSizeF size = componentSize(m_props, m_name);
    const double header = m_name.isEmpty() ? 0 : NameHeight;
    const double bodyHeight = size.height() - header;
    m_rect = QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height());

    double halfWidth = m_rect.width() / 2;
    const double bodyTop = m_rect.top() + header;

    double diffHeight = bodyHeight / (m_inputPorts.size() + 1);
    double currHeight = bodyTop;
    for (PortItem* inputPort : std::as_const(m_inputPorts)) {
        currHeight += diffHeight;
        inputPort->setPos(-halfWidth, currHeight);
    }

    diffHeight = bodyHeight / (m_outputPorts.size() + 1);
    currHeight = bodyTop;
    for (PortItem* outputPort : std::as_const(m_outputPorts)) {
        currHeight += diffHeight;
        outputPort->setPos(halfWidth, currHeight);
    }

    updateWires();
    update();
}

void DisplayItem::updateValue()
{
    // The top pin is bit 0
    int value = 0;
    for (qsizetype i = m_inputPorts.size() - 1; i >= 0; i--) value = (value << 1) | int(m_inputPorts[i]->getValue());
    if (value == m_value) return;
    m_value = value;
    update();
}

//===================== BlockItem ========================

namespace {
constexpr double BlockWidth = 150, BlockHeader = 36;

QString portLabel(const BlockPort& port)
{
    return port.width > 1 ? u"%1[%2]"_s.arg(port.name).arg(port.width) : port.name;
}
}

BlockItem::BlockItem(const BlockProps& props, QGraphicsItem* parent)
    : ComponentItem(parent), m_props(props)
{
    int inputs = 0, outputs = 0;
    for (const BlockPort& port : m_props.ports) (port.output ? outputs : inputs) += port.width;
    const QSizeF size = componentSize(m_props);
    m_rect = QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height());

    resizePorts(m_inputPorts, inputs, PortType::IN);
    resizePorts(m_outputPorts, outputs, PortType::OUT);
    const double firstRow = m_rect.top() + BlockHeader + PinSpacing / 2;
    for (int i = 0; i < inputs; i++) m_inputPorts[i]->setPos(m_rect.left(), firstRow + i * PinSpacing);
    for (int i = 0; i < outputs; i++) m_outputPorts[i]->setPos(m_rect.right(), firstRow + i * PinSpacing);
}

ComponentItem::EditResult BlockItem::setProps(const ItemProps&)
{
    return std::unexpected(tr("A block shows a compiled module. Change the Verilog and compile again to change it."));
}

void BlockItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    drawSelection(painter, option);
    const Theme& theme = currentTheme();
    painter->setRenderHint(QPainter::Antialiasing);

    painter->setPen(QPen(theme.block, 2));
    painter->setBrush(theme.fillFor(theme.block));
    painter->drawRoundedRect(m_rect, 6, 6);

    // Instance name, and the module it is an instance of
    QFont font = painter->font();
    font.setBold(true);
    font.setPointSizeF(9);
    painter->setFont(font);
    painter->setPen(theme.componentText);
    const QRectF header = m_rect.adjusted(6, 4, -6, 0);
    painter->drawText(QRectF(header.left(), header.top(), header.width(), 16), Qt::AlignCenter, m_props.instance);
    font.setBold(false);
    font.setPointSizeF(7.5);
    painter->setFont(font);
    painter->setPen(theme.pin);
    painter->drawText(QRectF(header.left(), header.top() + 15, header.width(), 13), Qt::AlignCenter, m_props.module);

    // Port names next to their first pin
    font.setPointSizeF(7);
    painter->setFont(font);
    painter->setPen(theme.componentText);
    int inputRow = 0, outputRow = 0;
    const double firstRow = m_rect.top() + BlockHeader + PinSpacing / 2;
    for (const BlockPort& port : m_props.ports) {
        int& row = port.output ? outputRow : inputRow;
        const double y = firstRow + row * PinSpacing;
        const QRectF label(m_rect.left() + 8, y - PinSpacing / 2, m_rect.width() - 16, PinSpacing);
        painter->drawText(label, (port.output ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, portLabel(port));
        row += port.width;
    }
}

//===================== TerminalItem ========================

TerminalItem::TerminalItem(const QString& name, bool output, int width, QGraphicsItem* parent)
    : QGraphicsObject(parent), m_name(name), m_output(output)
{
    const int pins = std::max(width, 1);
    const QSizeF size = terminalSize(name, width);
    m_rect = QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height());
    const double firstRow = m_rect.top() + 5 + BlockItem::PinSpacing / 2;
    for (int i = 0; i < pins; i++) {
        auto* pin = new PortItem(output ? PortType::IN : PortType::OUT, this);
        pin->setPos(output ? m_rect.left() : m_rect.right(), firstRow + i * BlockItem::PinSpacing);
        m_pins.append(pin);
    }
}

void TerminalItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const Theme& theme = currentTheme();
    painter->setRenderHint(QPainter::Antialiasing);

    // A tag pointing the way signals flow, left to right: input ports sit on the left of the
    // module's contents, output ports on the right
    const QRectF r = m_rect;
    const double tip = std::min(14.0, r.height() / 2);
    QPainterPath shape;
    shape.moveTo(r.left(), r.top());
    shape.lineTo(r.right() - tip, r.top());
    shape.lineTo(r.right(), r.center().y());
    shape.lineTo(r.right() - tip, r.bottom());
    shape.lineTo(r.left(), r.bottom());
    shape.closeSubpath();
    painter->setPen(QPen(theme.block, 1.5));
    painter->setBrush(theme.fillFor(theme.block));
    painter->drawPath(shape);

    QFont font = painter->font();
    font.setPointSizeF(8);
    painter->setFont(font);
    painter->setPen(theme.componentText);
    const QString label = m_pins.size() > 1 ? u"%1[%2]"_s.arg(m_name).arg(m_pins.size()) : m_name;
    painter->drawText(r.adjusted(6, 0, -tip, 0), Qt::AlignCenter, label);
}

//===================== Sizes ========================

QSizeF componentSize(const ItemProps& props, const QString& name)
{
    const double header = name.isEmpty() ? 0 : NameHeight;
    struct Visitor {
        double named;     // width the name needs (0 without one)
        double header;    // height of the name strip
        QSizeF operator()(const GateProps& p) const
        {
            const GateProps gate = normalized(p);
            return {50, gate.type == GType::NOT ? 44.0 : 44.0 + (gate.numInputs - 2) * 10.0};
        }
        QSizeF operator()(const RegisterProps&) const { return {40, 60}; }
        QSizeF operator()(const MuxProps&) const { return {40, 60}; }
        QSizeF operator()(const SourceProps& p) const
        {
            return {std::max(30.0, named), 30.0 + (normalized(p).bits - 1) * 10.0 + header};
        }
        QSizeF operator()(const DisplayProps& p) const
        {
            const DisplayProps display = normalized(p);
            return {std::max(46.0, named), 46.0 + std::max(display.numInputs, display.numOutputs) * 8.0 + header};
        }
        QSizeF operator()(const BlockProps& p) const
        {
            int inputs = 0, outputs = 0;
            for (const BlockPort& port : p.ports) (port.output ? outputs : inputs) += port.width;
            return {BlockWidth, BlockHeader + std::max({inputs, outputs, 1}) * BlockItem::PinSpacing + 8};
        }
    };
    return std::visit(Visitor{nameWidth(name), header}, props);
}

QSizeF terminalSize(const QString& name, int width)
{
    const int pins = std::max(width, 1);
    const double height = pins * BlockItem::PinSpacing + 10;
    QFont font;
    font.setPointSizeF(8);
    const QString label = pins > 1 ? u"%1[%2]"_s.arg(name).arg(pins) : name;
    const double tip = std::min(14.0, height / 2);
    return {std::max(90.0, QFontMetricsF(font).horizontalAdvance(label) + tip + 16), height};
}
