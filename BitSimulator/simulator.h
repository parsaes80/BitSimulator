#pragma once
#include <QObject>
#include <QTimer>
#include "general.h"

class Simulator : public QObject {
    Q_OBJECT
public:
    explicit Simulator(QObject* parent = nullptr) : QObject(parent) {}

    // All of these run in the simulator's thread. The GUI reaches them through queued connections,
    // so they execute in the order the GUI asked for them and no state is shared between threads.
public:
    void receiveCircuit(ExportGraph graph);   // replaces any previous circuit and starts running it
    void setRunning(bool running);            // pause (keeps all state) or continue
    void unloadCircuit();                     // stop and forget the circuit
    void SimController(); 
    void setTimerPeriod(int milliseconds);
    void printRunDataInfo();
signals:
    void sendResult(SimResult result);
private:
    void tick();
    void clearCircuit();
    void processGates(u32 changedNetId);
    void processMuxes(u32 changedNetId);
    void processRegisters(bool firstTick);   // firstTick: only note the clock levels, there is no edge yet

    // Worklist + per-net "already queued" flag replaces std::set: O(1) dedup, no per-event allocation
    inline void enqueue(u32 netId) {
        if (!m_inQueue[netId]) {
            m_inQueue[netId] = 1;
            m_eventQueue.push_back(netId);
        }
    }

    QTimer* m_timer = nullptr;

    std::vector<u8> m_nets; // not bool for performance
    std::vector<Gate> m_gates;
    std::vector<Register> m_registers;
    std::vector<Source> m_sources;
    std::vector<Mux> m_muxes;

    bool m_firstTick = true;
    u64 m_tick = 0;                 // ticks done in this run; sources compute their value from it
    bool m_running = false;
    u32 m_runId = 0;

    // Tick timing statistics (a running total, so a long run does not grow memory)
    long long m_tickMicrosTotal = 0;
    long long m_tickCount = 0;

    std::vector<std::vector<u32>> m_gateFanout;
    std::vector<std::vector<u32>> m_MuxFanout;

    std::vector<u32> m_eventQueue;
    std::vector<u8> m_inQueue; // indexed by net id

};

