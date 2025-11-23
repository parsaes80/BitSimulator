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

protected:
    void paintEvent(QPaintEvent* event) override;

public slots:
    void onButtonClicked() { emit gateTypeSelected(m_gateType); emit setOverlay(0);};
signals:
    void gateTypeSelected(GType gateType);
    void setOverlay(int val);
private:
    void drawGateSymbol(QPainter* painter, double width, double height);
    void drawAndGate(QPainter* painter, double width, double height);
    void drawOrGate(QPainter* painter, double width, double height);
    void drawXorGate(QPainter* painter, double width, double height);
    void drawNotGate(QPainter* painter, double width, double height);
    void drawNotBubble(QPainter* painter, double width, double height);

    GType m_gateType;
};

class SourceButton : public QPushButton
{
    Q_OBJECT

public:
    SourceButton(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

public slots:
    void onButtonClicked() { emit sourceSelected(); emit setOverlay(2);};
signals:
    void sourceSelected();
    void setOverlay(int val);
};

class RegisterButton : public QPushButton
{
    Q_OBJECT

public:
    RegisterButton(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

public slots:
    void onButtonClicked() { emit RegSelected(RType::D);emit setOverlay(1);};
signals:
    void RegSelected(RType regType);
    void setOverlay(int val);
};

class MuxButton : public QPushButton
{
    Q_OBJECT

public:
    MuxButton(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

public slots:
    void onButtonClicked() { emit MuxSelected(MType::MUX);emit setOverlay(0);};
signals:
    void MuxSelected(MType muxType);
    void setOverlay(int val);
};

class DisplayButton : public QPushButton
{
    Q_OBJECT

public:
    DisplayButton(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

public slots:
    void onButtonClicked() { emit DisplaySelected(); emit setOverlay(3);};
signals:
    void DisplaySelected();
    void setOverlay(int val);
};
