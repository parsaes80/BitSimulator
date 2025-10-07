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
    bool sim_running = false;

    std::vector<bool> m_nets;
    std::vector<Gate> m_gates;
    std::vector<Register> m_registers;
    std::vector<Source> m_sources;

    std::vector<u32> m_gateInputs;     
    std::vector<u32> m_sourceOutputs;
     
    std::unordered_map <u32, std::vector<WireItem*>> m_net2wire;
    std::unordered_map <WireItem*, u32> m_wire2net;
};

