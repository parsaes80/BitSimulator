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
    std::vector<Source> m_sources;

    // Gate inputs use the inID field in Gate struct as index into this array
    // Gate/Source outputs use the outID field in their respective structs 
    std::vector<u32> m_gateInputs;     // Flat array: all gate inputs concatenated

    // UI mapping for visual feedback
    std::vector<WireItem*> m_uiWireMap;
};

