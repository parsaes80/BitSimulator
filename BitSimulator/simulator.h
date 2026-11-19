#pragma once
#include <QElapsedTimer>
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
    // The recorded nets' changes for the waveforms, about every WaveReportMs and when the run pauses
    // or stops. Unlike sendResult, none of these may be skipped: together they are the whole history.
    void sendWaveChanges(WaveChanges changes);
private:
    void tick();
    // After a tick's propagation: notes the recorded nets that changed. Only the nets in the event
    // queue can have changed (every write to a net queues it), so only those are looked at.
    void recordWaveChanges(u64 tick);
    void reportWaveChanges();           // sends what was recorded since the last report

    static constexpr int WaveReportMs = 30;
    void clearCircuit();
    void processGates(u32 changedNetId);
    void processMuxes(u32 changedNetId);
    void processRegisters(bool firstTick);   // firstTick: only note the clock levels, there is no edge yet
    // Works off the event queue from `head` until nothing changes (or the tick's step limit is reached)
    void settle(size_t& head, int& steps);
    // Puts every flip-flop's and latch's stored value on its output; false if none changed
    bool driveRegisterOutputs();

    // Within one tick, the flip-flops and latches act as often as their outputs set off further
    // changes (a flip-flop clocked by another, a latch following a latch); a loop that never settles
    // stops after this many rounds and goes on in the next tick
    static constexpr int MaxRegisterRounds = 256;
    static constexpr int MaxPropagationSteps = 100000;   // gate and mux evaluations per tick

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

    // Waveform recording: the nets to watch, their values after the last tick, and the changes not reported yet
    std::vector<u32> m_recordedNets;
    std::vector<u8> m_recordedValues;
    std::vector<u32> m_recordedSlot;    // by net id: its index in m_recordedNets + 1, or 0 if it is not recorded
    WaveChanges m_waveChanges;
    QElapsedTimer m_sinceWaveReport;

};

