#include "Toolbar.h"
#include "Items.h"
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextEdit>

GateButton::GateButton(GType gateType, QWidget* parent)
    : QPushButton(parent)
    , m_gateType(gateType)
{
    setText("");        // Remove text
    setCheckable(false); // Allow toggle state
    
    connect(this, &QPushButton::clicked, this, &GateButton::onButtonClicked);
}
GateButton::GateButton(QWidget* parent): QPushButton(parent)
{
    setText("");         // Remove text
    setCheckable(false); // Allow toggle state
    connect(this, &QPushButton::clicked, this, &GateButton::onButtonClicked);
}
void GateButton::paintEvent(QPaintEvent* event)
{
    // Draw button background first
    QPushButton::paintEvent(event);

    // Draw gate symbol on top
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Use 75% of button size for gate drawing
    QRect buttonRect = rect();
    double gateWidth = buttonRect.width() * 0.7;
    double gateHeight = buttonRect.height() * 0.75;
    
    // Center the gate in the button
    QRectF gateRect(
        (buttonRect.width() - gateWidth) / 2,
        (buttonRect.height() - gateHeight) / 2,
        gateWidth,
        gateHeight
    );
    
    painter.translate(gateRect.center());
    painter.scale(1.0, 1.0);  // No additional scaling needed

    drawGateSymbol(&painter, gateWidth, gateHeight);
}

void GateButton::drawGateSymbol(QPainter* painter, double width, double height)
{
    // Set common styling for all gates
    painter->setPen(QPen(isChecked() ? Qt::white : Qt::black, 2));
    painter->setBrush(QColor(255, 215, 150));
    
    switch (m_gateType) {
    case GType::AND:
        drawAndGate(painter, width, height);
        break;
    case GType::OR:
        drawOrGate(painter, width, height);
        break;
    case GType::XOR:
        drawXorGate(painter, width, height);
        break;
    case GType::NAND:
        drawAndGate(painter, width, height);
        drawNotBubble(painter, width, height);
        break;
    case GType::NOR:
        drawOrGate(painter, width, height);
        drawNotBubble(painter, width, height);
        break;
    case GType::XNOR:
        drawXorGate(painter, width, height);
        drawNotBubble(painter, width, height);
        break;
    case GType::NOT:
        drawNotGate(painter, width, height);
        break;
    }
}

void GateButton::drawAndGate(QPainter* painter, double width, double height)
{  
    QPainterPath path;
    
    double halfWidth = width / 2;
    double halfHeight = height / 2;

    path.moveTo(-halfWidth, -halfHeight);
    path.lineTo(0, -halfHeight);
    path.arcTo(0, -halfHeight, halfWidth, height, 90, -180);
    path.lineTo(-halfWidth, halfHeight);
    path.lineTo(-halfWidth, -halfHeight);

    painter->setBrush(QColor(255, 215, 150));

    painter->fillPath(path, painter->brush());
    painter->drawPath(path);
}

void GateButton::drawOrGate(QPainter* painter, double width, double height)
{
    painter->setBrush(QColor(0, 255, 140));

    QPainterPath path;
    
    double halfWidth = width / 2;
    double halfHeight = height / 2;
    
    path.moveTo(-halfWidth, -halfHeight);
    path.quadTo(-halfWidth / 2, 0, -halfWidth, halfHeight);
    path.lineTo(halfWidth / 2, halfHeight);
    path.quadTo(width * 3 / 4, 0, halfWidth / 2, -halfHeight);
    path.lineTo(-halfWidth, -halfHeight);

    painter->setBrush(QColor(0, 255, 140));

    painter->fillPath(path, painter->brush());
    painter->drawPath(path);
}

void GateButton::drawXorGate(QPainter* painter, double width, double height)
{
    painter->setBrush(QColor(142, 43, 255));

    double halfWidth = width / 2;
    double halfHeight = height / 2;

    QPainterPath path;

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

void GateButton::drawNotGate(QPainter* painter, double width, double height)
{
    painter->setBrush(QColor(255, 0, 0));
    QPainterPath path;

    double halfWidth = width / 2;
    double halfHeight = height / 2;

    path.moveTo(-halfWidth, -halfHeight);
    path.lineTo(-halfWidth, halfHeight);
    path.lineTo(halfWidth , 0);
    path.lineTo(-halfWidth, -halfHeight);
    painter->fillPath(path, painter->brush());
    painter->drawPath(path);

}

void GateButton::drawNotBubble(QPainter* painter, double width, double height)
{
    painter->setBrush(Qt::red);
    double bubbleRadius = width * 0.08;  // Bubble size relative to width
    double QuarterWidth = width / 4;
    
    painter->drawEllipse(QRectF(QuarterWidth + 3*bubbleRadius, -bubbleRadius, bubbleRadius * 2, bubbleRadius * 2));
}

//===================== SourceButton ========================

SourceButton::SourceButton(QWidget* parent) : QPushButton(parent)
{
    setText("");         // Remove text
    setCheckable(false);  // Allow toggle state
    
    // Connect the button click to our slot
    connect(this, &QPushButton::clicked, this, &SourceButton::onButtonClicked);
}

void SourceButton::paintEvent(QPaintEvent* event)
{
    // Draw button background first
    QPushButton::paintEvent(event);

    // Draw source symbol on top
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Center the drawing area
    QRect drawRect = rect().adjusted(8, 8, -8, -8);
    painter.translate(drawRect.center());

    // Scale to fit button
    double scale = qMin(drawRect.width() / 40.0, drawRect.height() / 30.0);
    painter.scale(scale, scale);

    // Set styling for source
    painter.setPen(QPen(isChecked() ? Qt::white : Qt::black, 2));
    painter.setBrush(QColor(255, 100, 100)); // Red color
    
    // Draw red square
    QRectF sourceRect(-12, -10, 24, 20);
    painter.fillRect(sourceRect, painter.brush());
    painter.drawRect(sourceRect);
    
    // Draw "1" in the center to indicate it's a logic source
    painter.setPen(QPen(isChecked() ? Qt::white : Qt::white, 2));
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(12);
    painter.setFont(font);
    painter.drawText(sourceRect, Qt::AlignCenter, "1");
}


//===================== RegisterButton ========================

RegisterButton::RegisterButton(QWidget* parent) : QPushButton(parent) {
    setText("");         // Remove text
    setCheckable(false);  // Allow toggle state

    // Connect the button click to our slot
    connect(this, &QPushButton::clicked, this, &RegisterButton::onButtonClicked);
}

void RegisterButton::paintEvent(QPaintEvent* event)
{
    // Draw button background first
    QPushButton::paintEvent(event);

    // Draw register symbol on top
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Center the drawing area
    QRect drawRect = rect().adjusted(8, 8, -8, -8);
    painter.translate(drawRect.center());

    // Scale to fit button
    double scale = qMin(drawRect.width() / 40.0, drawRect.height() / 30.0);
    painter.scale(scale, scale);

    // Set styling for register
    painter.setPen(QPen(isChecked() ? Qt::white : Qt::black, 2));
    painter.setBrush(QColor(100, 150, 255)); // Blue color (different from source)
    
    // Draw rectangle that's taller than wide (register shape)
    QRectF registerRect(-10, -14, 20, 28);  // Width: 20, Height: 28 (taller)
    painter.fillRect(registerRect, painter.brush());
    painter.drawRect(registerRect);
    
    // Draw "R" in the center to indicate it's a register
    painter.setPen(QPen(Qt::white, 2));
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(12);
    painter.setFont(font);
    painter.drawText(registerRect, Qt::AlignCenter, "R");
    
    // Optional: Draw a small clock symbol (triangle) at bottom
    painter.setPen(QPen(Qt::white, 1.5));
    painter.setBrush(Qt::white);
    QPainterPath clockTriangle;
    clockTriangle.moveTo(-3, 10);   // Left point
    clockTriangle.lineTo(3, 10);    // Right point  
    clockTriangle.lineTo(0, 7);     // Top point
    clockTriangle.closeSubpath();
    painter.fillPath(clockTriangle, painter.brush());
    painter.drawPath(clockTriangle);
}

//===================== MuxButton ========================
MuxButton::MuxButton(QWidget* parent) : QPushButton(parent) {
    setText("");         // Remove text
    setCheckable(false);  // Allow toggle state

    // Connect the button click to our slot
    connect(this, &QPushButton::clicked, this, &MuxButton::onButtonClicked);
}

void MuxButton::paintEvent(QPaintEvent* event)
{
    // Draw button background first
    QPushButton::paintEvent(event);

    // Draw register symbol on top
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Center the drawing area
    QRect drawRect = rect().adjusted(8, 8, -8, -8);
    painter.translate(drawRect.center());
    QPainterPath path;
    double width = drawRect.width();
    double height = drawRect.height();
    double halfWidth = width / 2;
    double halfHeight = height / 2;
    double qurtHeight = halfHeight / 2;
    path.moveTo(-halfWidth,-halfHeight);
    path.lineTo(halfWidth,-qurtHeight);
    path.lineTo(halfWidth,qurtHeight);
    path.lineTo(-halfWidth,halfHeight);
    path.lineTo(-halfWidth,-halfHeight);
    painter.setBrush(QColor(255, 164, 0));
    painter.setPen(QPen(isChecked() ? Qt::white : Qt::black, 4));
    painter.drawPath(path);
    painter.fillPath(path, painter.brush());
}
//===================== DisplayButton ========================

DisplayButton::DisplayButton(QWidget* parent) : QPushButton(parent) {
    setText("");         // Remove text
    setCheckable(false);  // Allow toggle state

    // Connect the button click to our slot
    connect(this, &QPushButton::clicked, this, &DisplayButton::onButtonClicked);
}

void DisplayButton::paintEvent(QPaintEvent* event)
{
    // Draw button background first
    QPushButton::paintEvent(event);

    // Draw display symbol on top
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Center the drawing area
    QRect drawRect = rect().adjusted(8, 8, -8, -8);
    painter.translate(drawRect.center());

    // Scale to fit button
    double scale = qMin(drawRect.width() / 40.0, drawRect.height() / 60.0);
    painter.scale(scale, scale);

    // Draw outer border (dark gray casing)
    QRectF outerRect(-20, -30, 40, 60);
    painter.setPen(QPen(isChecked() ? Qt::white : Qt::black, 2));
    painter.setBrush(QColor(40, 40, 40));
    painter.drawRect(outerRect);

    // Draw inner display area (LED-style display)
    QRectF displayArea = outerRect.adjusted(5, 5, -5, -5);
    painter.setPen(QPen(QColor(20, 20, 20), 1));
    painter.setBrush(QColor(20, 60, 20)); // Dark green background
    painter.drawRect(displayArea);

    // Draw a sample number (8) in LED green color
    painter.setPen(QPen(QColor(0, 255, 0), 2));
    QFont font = painter.font();
    font.setBold(true);
    font.setFamily("Courier");
    font.setPointSize(16);
    painter.setFont(font);
    painter.drawText(displayArea, Qt::AlignCenter, "8");
}

//===================== ItemOverlay ========================

ItemOverlay::ItemOverlay(QWidget* parent) : QStackedWidget(parent) {}

void ItemOverlay::onSourceClicked(SourceItem* sourcePtr) {
    m_currentSource = sourcePtr;
    this->setCurrentIndex(4);
    
    // Find the text edit widget in page 4 (current page)
    QWidget* currentPage = this->widget(4);
    QTextEdit* textEdit = currentPage->findChild<QTextEdit*>("srcTextEdit"); // Use object name from UI
    
    // Alternative if you don't know the object name:
    // QPlainTextEdit* textEdit = currentPage->findChild<QPlainTextEdit*>();
    
    if (!textEdit) {
        qDebug() << "TextEdit not found!";
        return;
    }
    
    auto values = sourcePtr->getValues();
    
    if (std::holds_alternative<QList<bool>>(values)) {
        const auto& boolValues = std::get<QList<bool>>(values);
        
        // Convert bool list to comma-separated string
        QStringList strList;
        for (bool val : boolValues) {
            strList << (val ? "1" : "0");
        }
        
        textEdit->setPlainText(strList.join(", "));
    }
    else {
        const auto& intValues = std::get<QList<int>>(values);
        
        // Convert int list to comma-separated string
        QStringList strList;
        for (int val : intValues) {
            strList << QString::number(val);
        }
        
        textEdit->setPlainText(strList.join(", "));
    }
}

