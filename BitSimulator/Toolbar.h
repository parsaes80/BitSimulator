#pragma once
#include <QPushButton>
#include "general.h"

class QPainter;

// The component buttons of the toolbar. Each draws a small picture of its component in the
// component's canvas colors; clicking one makes that component the next one placed (MainWindow
// connects their clicked signals). They are promoted widgets in BitSimulator.ui.
class ToolbarButton : public QPushButton {
    Q_OBJECT
public:
    using QPushButton::QPushButton;

protected:
    void paintEvent(QPaintEvent* event) override;   // the button, then its picture over the middle
    // Draws the picture with the origin at the button's centre; `size` is the room for it
    virtual void drawIcon(QPainter& painter, QSizeF size) = 0;
    // Scales the painter so a picture drawn `design` units big fills `size`
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
