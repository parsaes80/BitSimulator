#include "CircuitCanvas.h"
#include <QMouseEvent>

// GateItem Implementation
GateItem::GateItem(GType gateType, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_gateType(gateType) {
    
    // Enable item flags for interaction
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);

    // Set size
    m_rect = QRectF(-30, -20, 60, 40);
}

QRectF GateItem::boundingRect() const {
    return m_rect.adjusted(-2, -2, 2, 2); // Add border for selection
}

void GateItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget)

    // Draw selection highlight
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(Qt::blue, 3));
        painter->drawRect(m_rect);
    }

    // Draw gate body
    painter->setPen(QPen(Qt::black, 2));
    painter->setBrush(Qt::yellow);
    painter->drawRect(m_rect);

    // Draw gate label
    painter->setPen(Qt::black);
    painter->drawText(m_rect, Qt::AlignCenter, gateTypeToString());

    // Draw input/output pins
    drawPins(painter);
}

QVariant GateItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionChange && scene()) {
        // Snap to grid
        QPointF newPos = value.toPointF();
        int gridSize = 20;
        newPos.setX(qRound(newPos.x() / gridSize) * gridSize);
        newPos.setY(qRound(newPos.y() / gridSize) * gridSize);
        return newPos;
    }
    return QGraphicsItem::itemChange(change, value);
}

GType GateItem::getGateType() const {
    return m_gateType;
}

QPointF GateItem::getInputPin(int index) const {
    return mapToScene(QPointF(-30, -10 + index * 20));
}

QPointF GateItem::getOutputPin() const {
    return mapToScene(QPointF(30, 0));
}

void GateItem::drawPins(QPainter* painter) {
    painter->setPen(QPen(Qt::darkGray, 2));
    // Input pins
    painter->drawLine(-30, -10, -35, -10);
    painter->drawLine(-30, 10, -35, 10);
    // Output pin
    painter->drawLine(30, 0, 35, 0);
}

QString GateItem::gateTypeToString() const {
    switch (m_gateType) {
    case GType::AND: return "AND";
    case GType::OR: return "OR";
    case GType::XOR: return "XOR";
    case GType::NAND: return "NAND";
    case GType::NOR: return "NOR";
    case GType::XNOR: return "XNOR";
    }
    return "GATE";
}

// WireItem Implementation
WireItem::WireItem(const QLineF& line, QGraphicsItem* parent)
    : QGraphicsLineItem(line, parent) {
    
    setPen(QPen(Qt::black, 2));
    setFlag(QGraphicsItem::ItemIsSelectable, true);
}

void WireItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    // Change color when selected
    if (isSelected()) {
        setPen(QPen(Qt::blue, 3));
    }
    else {
        setPen(QPen(Qt::black, 2));
    }

    QGraphicsLineItem::paint(painter, option, widget);

    // Draw connection points
    painter->setBrush(Qt::black);
    QLineF l = line();
    painter->drawEllipse(l.p1(), 3, 3);
    painter->drawEllipse(l.p2(), 3, 3);
}

// CircuitScene Implementation
CircuitScene::CircuitScene(QObject* parent) 
    : QGraphicsScene(parent), m_connectingWire(false), m_currentWire(nullptr) {
    setSceneRect(0, 0, 2000, 2000); // Large canvas
}

void CircuitScene::addGate(GType gateType, QPointF position) {
    GateItem* gate = new GateItem(gateType);
    gate->setPos(position);
    addItem(gate);

    emit gateAdded(gate);
}

void CircuitScene::startWireConnection(QPointF startPoint) {
    m_connectingWire = true;
    m_wireStartPoint = startPoint;
    m_currentWire = new WireItem(QLineF(startPoint, startPoint));
    m_currentWire->setPen(QPen(Qt::red, 2, Qt::DashLine)); // Temporary wire
    addItem(m_currentWire);
}

void CircuitScene::updateWireConnection(QPointF currentPoint) {
    if (m_connectingWire && m_currentWire) {
        m_currentWire->setLine(QLineF(m_wireStartPoint, currentPoint));
    }
}

void CircuitScene::finishWireConnection(QPointF endPoint) {
    if (m_connectingWire && m_currentWire) {
        removeItem(m_currentWire);
        delete m_currentWire;

        // Create permanent wire
        WireItem* wire = new WireItem(QLineF(m_wireStartPoint, endPoint));
        addItem(wire);

        m_connectingWire = false;
        m_currentWire = nullptr;

        emit wireAdded(wire);
    }
}

void CircuitScene::drawBackground(QPainter* painter, const QRectF& rect) {
    // Draw grid
    painter->setPen(QPen(Qt::lightGray, 1, Qt::DotLine));

    int gridSize = 20;
    int left = int(rect.left()) - (int(rect.left()) % gridSize);
    int top = int(rect.top()) - (int(rect.top()) % gridSize);

    for (int x = left; x < rect.right(); x += gridSize) {
        painter->drawLine(x, rect.top(), x, rect.bottom());
    }
    for (int y = top; y < rect.bottom(); y += gridSize) {
        painter->drawLine(rect.left(), y, rect.right(), y);
    }
}

void CircuitScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton && event->modifiers() & Qt::ControlModifier) {
        // Start wire connection with Ctrl+Click
        startWireConnection(event->scenePos());
    }
    else {
        QGraphicsScene::mousePressEvent(event);
    }
}

void CircuitScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_connectingWire) {
        updateWireConnection(event->scenePos());
    }
    else {
        QGraphicsScene::mouseMoveEvent(event);
    }
}

void CircuitScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_connectingWire && event->button() == Qt::LeftButton) {
        finishWireConnection(event->scenePos());
    }
    else {
        QGraphicsScene::mouseReleaseEvent(event);
    }
}

// CircuitCanvas Implementation
CircuitCanvas::CircuitCanvas(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new CircuitScene(this);
    setScene(m_scene);

    // Configure view
    setDragMode(QGraphicsView::RubberBandDrag);
    setRenderHint(QPainter::Antialiasing);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

    // Connect signals
    connect(m_scene, &CircuitScene::gateAdded, this, &CircuitCanvas::onGateAdded);
    connect(m_scene, &CircuitScene::wireAdded, this, &CircuitCanvas::onWireAdded);
}

void CircuitCanvas::addGate(GType gateType, QPoint position) {
    QPointF scenePos = mapToScene(position);
    m_scene->addGate(gateType, scenePos);
}

void CircuitCanvas::clearCanvas() {
    m_scene->clear();
}

void CircuitCanvas::mousePressEvent(QMouseEvent* event) {
    addGate(GType::AND, event->pos());
    QGraphicsView::mousePressEvent(event); // Call base class implementation
}

void CircuitCanvas::wheelEvent(QWheelEvent* event) {
    // Zoom with mouse wheel
    const double scaleFactor = 1.15;
    if (event->angleDelta().y() > 0) {
        scale(scaleFactor, scaleFactor);
    }
    else {
        scale(1.0 / scaleFactor, 1.0 / scaleFactor);
    }
}

void CircuitCanvas::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasText() &&
        event->mimeData()->text().startsWith("gate:")) {
        event->acceptProposedAction();
    }
}

void CircuitCanvas::dropEvent(QDropEvent* event) {
    QString gateData = event->mimeData()->text();
    if (gateData.startsWith("gate:")) {
        int gateTypeInt = gateData.mid(5).toInt();
        GType gateType = static_cast<GType>(gateTypeInt);
        addGate(gateType, event->pos());
        event->acceptProposedAction();
    }
}

void CircuitCanvas::onGateAdded(GateItem* gate) {
    qDebug() << "Gate added at position:" << gate->pos();
    emit circuitChanged();
}

void CircuitCanvas::onWireAdded(WireItem* wire) {
    qDebug() << "Wire added:" << wire->line();
    emit circuitChanged();
}
