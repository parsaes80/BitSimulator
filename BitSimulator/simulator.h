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
    void SimController(); // Parameterless slot for auto-starting

signals:
    void sendResult(SimResult result);
private:
    bool sim_running = false;

    std::vector<Net> m_nets;
    std::vector<Gate> m_gates;
    std::vector<Register> m_registers;
    std::vector<u32> m_gateInputs;
    std::vector<Source> m_sources;
    std::vector<WireItem*> m_uiWireMap;
    // ID counters for proper management
    u32 nextGateinputID = 0;
    u32 nextNetId = 0;
    u32 nextGateId = 0;
    u32 nextRegisterId = 0;
};

