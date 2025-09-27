#pragma once
#include <QAbstractButton>
#include <QPainterPath>
#include <QPushButton>
#include <QPainter>
#include "general.h"

class GateButton : public QPushButton
{
    Q_OBJECT
    
public:
    GateButton(GType gateType, QWidget* parent = nullptr);
    GateButton(QWidget* parent = nullptr);
    GType getGateType() const { return m_gateType; };
    void setGateType(GType gateType) { m_gateType = gateType; };
    
    // Method to connect to scene after construction
    void connectToScene(class CircuitScene* scene);

protected:
    void paintEvent(QPaintEvent* event) override;

public slots:
    void onButtonClicked() { emit gateTypeSelected(m_gateType); };
signals:
    void gateTypeSelected(GType gateType);

private:
    void drawGateSymbol(QPainter* painter);
    void drawAndGate(QPainter* painter);
    void drawOrGate(QPainter* painter);
    void drawXorGate(QPainter* painter);
    void drawNotGate(QPainter* painter);
    void drawNotBubble(QPainter* painter);

    GType m_gateType;
};
