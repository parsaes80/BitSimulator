#pragma once
#include <QObject>
#include <QTimer>
#include "general.h"

class Simulator : public QObject {
    Q_OBJECT
public:
    explicit Simulator(QObject *parent = nullptr);
    void clearCircuit();
  
public slots:
    void tick();
    void receiveCircuit(ExportGraph graph);
    void SimController(); 
    void setTimerPeriod(int milliseconds);
signals:
    void sendResult(SimResult result);
private:
    QTimer* m_timer = nullptr;

    std::vector<bool> m_nets;
    std::vector<Gate> m_gates;
    std::vector<Register> m_registers;
    std::vector<Source> m_sources;

    std::vector<u32> m_gateInputs;     
    std::vector<u32> m_sourceOutputs;
     
};

