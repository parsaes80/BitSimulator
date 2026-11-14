#pragma once
#include <QAbstractButton>
#include <QPainterPath>
#include <QPushButton>
#include <QPainter>
#include "general.h"

class GateButton : public QPushButton{
    Q_OBJECT  
public:
    GateButton(GType gateType, QWidget* parent = nullptr);
    GateButton(QWidget* parent = nullptr);
    GType getGateType() const { return m_gateType; };
    void setGateType(GType gateType) { m_gateType = gateType; };

protected:
    void paintEvent(QPaintEvent* event) override;

public slots:
    void onButtonClicked() { emit gateTypeSelected(m_gateType); };
signals:
    void gateTypeSelected(GType gateType);
private:
    void drawGateSymbol(QPainter* painter, double width, double height);
    void drawANDGate(QPainter* painter, double width, double height);
    void drawORGate(QPainter* painter, double width, double height);
    void drawXORGate(QPainter* painter, double width, double height);
    void drawNOTGate(QPainter* painter, double width, double height);
    void drawNOTBubble(QPainter* painter, double width, double height);
    void drawNANDGate(QPainter* painter, double width, double height);
    void drawNORGate(QPainter* painter, double width, double height);
    void drawXNORGate(QPainter* painter, double width, double height);
    GType m_gateType;
};

class SourceButton : public QPushButton{
    Q_OBJECT
public:
    SourceButton(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

public slots:
    void onButtonClicked() { emit sourceSelected(); };
signals:
    void sourceSelected();
};

class RegisterButton : public QPushButton{
    Q_OBJECT
public:
    RegisterButton(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

public slots:
    void onButtonClicked() { emit RegSelected(); };
signals:
    void RegSelected();
};

class MuxButton : public QPushButton{
    Q_OBJECT
public:
    MuxButton(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

public slots:
    void onButtonClicked() { emit MuxSelected(); };
signals:
    void MuxSelected();
};

class DisplayButton : public QPushButton{
    Q_OBJECT
public:
    DisplayButton(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

public slots:
    void onButtonClicked() { emit DisplaySelected(); };
signals:
    void DisplaySelected();
};
