#include "Toolbar.h"
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>

GateButton::GateButton(GType gateType, QWidget* parent)
    : QPushButton(parent)
    , m_gateType(gateType)
{
    setText("");        // Remove text
    setCheckable(false); // Allow toggle state
    setMinimumSize(60, 40);
    setMaximumSize(60, 40);
    
    // Connect the button click to our slot
    connect(this, &QPushButton::clicked, this, &GateButton::onButtonClicked);
    qDebug() << "Connection made in constructor";
}
GateButton::GateButton(QWidget* parent)
    : QPushButton(parent)
{
    setText("");         // Remove text
    setCheckable(false); // Allow toggle state
    setMinimumSize(60, 40);
    setMaximumSize(60, 40);
    connect(this, &QPushButton::clicked, this, &GateButton::onButtonClicked);
    qDebug() << "Connection made in constructor";
}

void GateButton::paintEvent(QPaintEvent* event)
{
    // Draw button background first
    QPushButton::paintEvent(event);

    // Draw gate symbol on top
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Center the drawing area
    QRect drawRect = rect().adjusted(8, 8, -8, -8);
    painter.translate(drawRect.center());

    // Scale to fit button
    double scale = qMin(drawRect.width() / 40.0, drawRect.height() / 30.0);
    painter.scale(scale, scale);

    drawGateSymbol(&painter);
}

void GateButton::drawGateSymbol(QPainter* painter)
{
    // Set common styling for all gates
    painter->setPen(QPen(isChecked() ? Qt::white : Qt::black, 2));
    painter->setBrush(QColor(255, 215, 150));
    
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

void GateButton::drawAndGate(QPainter* painter)
{
    QPainterPath path;

    path.moveTo(-15, -10);
    path.lineTo(-5, -10);
    path.arcTo(-5, -10, 15, 20, 90, -180);
    path.lineTo(-15, 10);
    path.closeSubpath();
    painter->fillPath(path, painter->brush());
    painter->drawPath(path);
}

void GateButton::drawOrGate(QPainter* painter)
{
    QPainterPath path;
    
    path.moveTo(-15, -10);
    path.quadTo(-7, 0, -15, 10);
    path.lineTo(5, 10);
    path.quadTo(15, 0, 5, -10);
    path.lineTo(-15, -10);
    painter->fillPath(path, painter->brush());
    painter->drawPath(path);
}

void GateButton::drawXorGate(QPainter* painter)
{
    drawOrGate(painter);

    QPainterPath extraLine;
    
    extraLine.moveTo(-18, -8);
    extraLine.quadTo(-12, 0, -18, 8);
    painter->drawPath(extraLine);
}

void GateButton::drawNotGate(QPainter* painter)
{
    QPainterPath path;

    double bubbleSize = 6;

    path.moveTo(-15, -8);
    path.lineTo(-15, 8);
    path.lineTo(10, 0);
    path.lineTo(-15, -8);
    painter->fillPath(path, painter->brush());
    painter->drawPath(path);

    // Draw NOT bubble at the tip
    painter->setBrush(Qt::white);
    painter->drawEllipse(10, -3, bubbleSize, bubbleSize);
}

void GateButton::drawNotBubble(QPainter* painter)
{
    painter->setBrush(Qt::white);
    double bubbleSize = 6;
    painter->drawEllipse(12, -3, bubbleSize, bubbleSize);
}
