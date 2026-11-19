#pragma once
#include <QPushButton>
#include "general.h"

class QPainter;

// Toolbar component buttons (promoted in the .ui), each drawing its component
class ToolbarButton : public QPushButton {
    Q_OBJECT
public:
    using QPushButton::QPushButton;

protected:
    void paintEvent(QPaintEvent* event) override;
    // Origin at the button's centre; `size` is the room for it
    virtual void drawIcon(QPainter& painter, QSizeF size) = 0;
    // Scales so a picture `design` units big fills `size`
    static void fit(QPainter& painter, QSizeF size, QSizeF design);
    static void drawLabel(QPainter& painter, const QRectF& rect, const QString& text, int pointSize = 8, bool bold = false);
};

class GateButton : public ToolbarButton {
    Q_OBJECT
public:
    using ToolbarButton::ToolbarButton;
    GType getGateType() const { return m_gateType; }
    void setGateType(GType gateType) { m_gateType = gateType; update(); }

protected:
    void drawIcon(QPainter& painter, QSizeF size) override;

private:
    GType m_gateType = GType::AND;
};

class SourceButton : public ToolbarButton {
    Q_OBJECT
public:
    using ToolbarButton::ToolbarButton;
protected:
    void drawIcon(QPainter& painter, QSizeF size) override;
};

class RegisterButton : public ToolbarButton {
    Q_OBJECT
public:
    using ToolbarButton::ToolbarButton;
protected:
    void drawIcon(QPainter& painter, QSizeF size) override;
};

class MuxButton : public ToolbarButton {
    Q_OBJECT
public:
    using ToolbarButton::ToolbarButton;
protected:
    void drawIcon(QPainter& painter, QSizeF size) override;
};

class DisplayButton : public ToolbarButton {
    Q_OBJECT
public:
    using ToolbarButton::ToolbarButton;
protected:
    void drawIcon(QPainter& painter, QSizeF size) override;
};
