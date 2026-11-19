#include "Toolbar.h"
#include <QPainter>
#include <QPainterPath>
#include "Items.h"

using namespace Qt::StringLiterals;

//===================== ToolbarButton ========================

void ToolbarButton::paintEvent(QPaintEvent* event)
{
    QPushButton::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -8);
    painter.translate(area.center());
    drawIcon(painter, area.size());
}

void ToolbarButton::fit(QPainter& painter, QSizeF size, QSizeF design)
{
    const double scale = std::min(size.width() / design.width(), size.height() / design.height());
    painter.scale(scale, scale);
}

void ToolbarButton::drawLabel(QPainter& painter, const QRectF& rect, const QString& text, int pointSize, bool bold)
{
    painter.setPen(currentTheme().componentText);
    QFont font = painter.font();
    font.setBold(bold);
    font.setPointSize(pointSize);
    painter.setFont(font);
    painter.drawText(rect, Qt::AlignCenter, text);
}

//===================== The buttons ========================

void GateButton::drawIcon(QPainter& painter, QSizeF)
{
    // Sized from the whole button (70% x 75%), not the inset the other pictures use
    const Theme& theme = currentTheme();
    const QColor color = theme.gateColor(m_gateType);
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(theme.fillFor(color));
    const double width = rect().width() * 0.7, height = rect().height() * 0.75;
    const QRectF body(-width / 2, -height / 2, width, height);
    drawGateSymbol(&painter, m_gateType, body);
    // NOT's name sits in the triangle's wide end
    drawLabel(painter, m_gateType == GType::NOT ? body.translated(-body.width() / 6, 0) : body, gateTypeName(m_gateType));
}

void SourceButton::drawIcon(QPainter& painter, QSizeF size)
{
    fit(painter, size, {40, 30});
    const Theme& theme = currentTheme();
    const QRectF body(-12, -10, 24, 20);
    painter.setPen(QPen(theme.source, 2));
    painter.setBrush(theme.fillFor(theme.source));
    painter.drawRoundedRect(body, 3, 3);
    drawLabel(painter, body, u"1"_s, 12, true);
}

void RegisterButton::drawIcon(QPainter& painter, QSizeF size)
{
    fit(painter, size, {40, 30});
    const Theme& theme = currentTheme();
    const QRectF body(-10, -14, 20, 28);
    painter.setPen(QPen(theme.reg, 2));
    painter.setBrush(theme.fillFor(theme.reg));
    painter.drawRoundedRect(body, 3, 3);
    drawLabel(painter, body, u"R"_s, 12, true);

    QPainterPath clock;
    clock.moveTo(-3, 10);
    clock.lineTo(3, 10);
    clock.lineTo(0, 7);
    clock.closeSubpath();
    painter.setPen(QPen(theme.reg, 1.5));
    painter.setBrush(theme.reg);
    painter.drawPath(clock);
}

void MuxButton::drawIcon(QPainter& painter, QSizeF size)
{
    const double halfWidth = size.width() / 2, halfHeight = size.height() / 2;
    QPainterPath body;
    body.moveTo(-halfWidth, -halfHeight);
    body.lineTo(halfWidth, -halfHeight / 2);
    body.lineTo(halfWidth, halfHeight / 2);
    body.lineTo(-halfWidth, halfHeight);
    body.closeSubpath();
    painter.setPen(QPen(currentTheme().mux, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(currentTheme().fillFor(currentTheme().mux));
    painter.drawPath(body);
    drawLabel(painter, QRectF(-halfWidth, -halfHeight, size.width(), size.height()), u"MUX"_s);
}

void DisplayButton::drawIcon(QPainter& painter, QSizeF size)
{
    fit(painter, size, {40, 60});
    const Theme& theme = currentTheme();
    const QRectF casing(-20, -30, 40, 60);
    painter.setPen(QPen(theme.display, 2));
    painter.setBrush(theme.fillFor(theme.display));
    painter.drawRoundedRect(casing, 4, 4);

    const QRectF screen = casing.adjusted(5, 5, -5, -5);
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme.canvas);
    painter.drawRoundedRect(screen, 2, 2);

    painter.setPen(QPen(theme.display, 2));
    QFont font = painter.font();
    font.setBold(true);
    font.setFamily(u"Courier"_s);
    font.setPointSize(16);
    painter.setFont(font);
    painter.drawText(screen, Qt::AlignCenter, u"8"_s);
}
