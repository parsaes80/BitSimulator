#pragma once
#include <QObject>
#include "general.h"

class Simulator : public QObject {
    Q_OBJECT
public:
    explicit Simulator(QObject *parent = nullptr);
    
    void clearCircuit();
  
public slots:
    void simulate(int numclks);
    void receiveCircuit(ExportGraph graph);
    void SimController(); 

signals:
    void sendResult(SimResult result);
private:
    

    std::vector<bool> m_nets;
    std::vector<Gate> m_gates;
    std::vector<Register> m_registers;
    std::vector<Source> m_sources;

    std::vector<u32> m_gateInputs;     
    std::vector<u32> m_sourceOutputs;
     
};

