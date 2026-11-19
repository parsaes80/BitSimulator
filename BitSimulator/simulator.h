#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include "general.h"

class Simulator : public QObject {
    Q_OBJECT
public:
    explicit Simulator(QObject* parent = nullptr) : QObject(parent) {}

    // Called through queued connections, in the simulator's thread; no state is shared
public:
    void receiveCircuit(ExportGraph graph);   // replaces any previous circuit
    void setRunning(bool running);            // pause or continue
    void unloadCircuit();
    void SimController(); 
    void setTimerPeriod(int milliseconds);
    void printRunDataInfo();
signals:
    void sendResult(SimResult result);
    // None may be skipped: together they are the whole waveform history
    void sendWaveChanges(WaveChanges changes);
private:
    void tick();
    // Only nets in the event queue can have changed, so only those are checked
    void recordWaveChanges(u64 tick);
    void reportWaveChanges();

    static constexpr int WaveReportMs = 30;
    void clearCircuit();
    void processGates(u32 changedNetId);
    void processMuxes(u32 changedNetId);
    void processRegisters(bool firstTick);   // firstTick: no edge yet, only note the clock levels
    void settle(size_t& head, int& steps);
    // false if no output changed
    bool driveRegisterOutputs();

    // Rounds of register updates per tick (a flip-flop clocked by another); stops a loop that never settles
    static constexpr int MaxRegisterRounds = 256;
    static constexpr int MaxPropagationSteps = 100000;   // gate and mux evaluations per tick

    // Per-net queued flag: O(1) dedup without std::set
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
    u64 m_tick = 0;                 // sources compute their value from it
    bool m_running = false;
    u32 m_runId = 0;

    // Tick timing statistics (a running total, so a long run does not grow memory)
    long long m_tickMicrosTotal = 0;
    long long m_tickCount = 0;

    std::vector<std::vector<u32>> m_gateFanout;
    std::vector<std::vector<u32>> m_MuxFanout;

    std::vector<u32> m_eventQueue;
    std::vector<u8> m_inQueue; // by net id

    std::vector<u32> m_recordedNets;
    std::vector<u8> m_recordedValues;
    std::vector<u32> m_recordedSlot;    // by net id: index in m_recordedNets + 1, 0 = not recorded
    WaveChanges m_waveChanges;
    QElapsedTimer m_sinceWaveReport;

};

