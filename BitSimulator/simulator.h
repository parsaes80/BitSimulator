#pragma once
#include <QObject>
#include <QTimer>
#include "general.h"
#include <set>

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
    void printRunDataInfo();
signals:
    void sendResult(SimResult result);
private:
    void processGates(u32 changedNetId);
    void processMuxes(u32 changedNetId);
    void processRegisters();

    QTimer* m_timer = nullptr;

    std::vector<u8> m_nets; // not bool for performance
    std::vector<Gate> m_gates;
    std::vector<Register> m_registers;
    std::vector<Source> m_sources;
    std::vector<Mux> m_muxes;

    std::vector<u32> m_gateInputs;     
    std::vector<u32> m_sourceOutputs;

    bool firstTick = true;

    std::vector<long> runData;

    std::vector<std::vector<u32>> m_gateFanout;
    std::vector<std::vector<u32>> m_MuxFanout;

    std::set<u32> eventQueue;

};

