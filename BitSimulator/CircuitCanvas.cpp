#include "CircuitCanvas.h"
#include <QMouseEvent>
#include <qscrollbar.h>

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
