#include "simulator.h"
#include "general.h"
#include <QThread>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <chrono>

extern bool sim_running;
extern GlobalMap map;

Simulator::Simulator(QObject *parent) : QObject(parent) {
    sim_running = false;

    m_nets.reserve(1000);
    m_gates.reserve(500);
    m_registers.reserve(100);
    m_sources.reserve(100);
    m_gateInputs.reserve(2000);  // Flat array for all gate inputs
}

void Simulator::clearCircuit() {
    m_nets.clear();
    m_gates.clear();
    m_registers.clear();
    m_sources.clear();
    m_gateInputs.clear();
    m_gateFanout.clear();
    m_MuxFanout.clear();
}

void Simulator::processGates(u32 changedNetId) {
    const auto& fanout = m_gateFanout[changedNetId];
    for (u32 gateIdx : fanout) {
        Gate& gate = m_gates[gateIdx];

        if (gate.outID == 0) {
            continue;
        }

        std::vector<u32> &inputs = gate.inIDs;

        bool newOutput;

        switch (gate.gateType) {
        case GType::AND:
            newOutput = true;
            for (u32 netId : inputs) {
                newOutput = newOutput && m_nets[netId];
            }
            break;
        case GType::OR:
            newOutput = false;
            for (u32 netId : inputs) {
                newOutput = newOutput || m_nets[netId];
            }
            break;
        case GType::NAND:
            newOutput = true;
            for (u32 netId : inputs) {
                newOutput = newOutput && m_nets[netId];
            }
            newOutput = !newOutput;
            break;
        case GType::NOR:
            newOutput = false;
            for (u32 netId : inputs) {
                newOutput = newOutput || m_nets[netId];
            }
            newOutput = !newOutput;
            break;
        case GType::XOR:
            newOutput = false;
            for (u32 netId : inputs) {
                newOutput = newOutput ^ m_nets[netId];
            }
            break;
        case GType::XNOR:
            newOutput = false;
            for (u32 netId : inputs) {
                newOutput = newOutput ^ m_nets[netId];
            }
            newOutput = !newOutput;
            break;
        case GType::NOT:
            newOutput = inputs.empty() ? false : !m_nets[inputs[0]];
            break;
        default:
            newOutput = false;
            break;
        }

        // ===== Check if output changed =====
        u32 outputNetId = gate.outID;

        bool oldValue = m_nets[outputNetId];

        if (oldValue != newOutput) {
            // Output changed - update net and queue it
            m_nets[outputNetId] = newOutput;
            eventQueue.insert(outputNetId);
        }

    }
}

void Simulator::processMuxes(u32 changedNetId) {
    const auto& fanout = m_MuxFanout[changedNetId];
    for (u32 muxIdx : fanout) {
        Mux& mux = m_muxes[muxIdx];

        if (mux.outID == 0) {
            continue;
        }

        // Read address inputs and compute selected index
        u32 selectedIndex = 0;
        for (size_t i = 0; i < mux.inAddress.size(); i++) {
            u32 addrNetId = mux.inAddress[i];
            if (m_nets[addrNetId]) {
                selectedIndex |= (1 << i);  // Build binary address
            }
        }

        // Clamp to valid range
        if (selectedIndex >= mux.inData.size()) {
            selectedIndex = mux.inData.size() - 1;
        }

        // Read selected data input
        u32 selectedDataNetId = mux.inData[selectedIndex];
        bool newOutput = m_nets[selectedDataNetId];

        // Update output if changed
        u32 outputNetId = mux.outID;

        bool oldValue = m_nets[outputNetId];
        if (oldValue != newOutput) {
            m_nets[outputNetId] = newOutput;
            eventQueue.insert(outputNetId);
        }
    }
}

void Simulator::processRegisters() {
    for (auto& reg : m_registers) {

        bool currentClock = m_nets[reg.clkID];
        bool currentEnable = m_nets[reg.enableID];

        // Determine behavior based on connections
        bool hasClockConnection = (reg.clkID != 0);  // 0 means not connected
        bool hasEnableConnection = (reg.enableID != 0);

        // Read input values (net 0 = disconnected = false)
        bool input1 = m_nets[reg.inID];      // D, T, S, or J input
        bool input2 = m_nets[reg.InID2];     // R or K input (for SR/JK)

        if (hasClockConnection) {
            // FLIP-FLOP BEHAVIOR (edge-triggered)
            bool risingEdge = !reg.prevClkState && currentClock;

            if (risingEdge) {
                // Check enable if connected, otherwise always enabled
                bool enabled = !hasEnableConnection || currentEnable;

                if (enabled) {
                    // Execute flip-flop logic based on type
                    switch (reg.regType) {
                        case RType::D:
                            // D Flip-Flop: Q = D
                            reg.storedValue = input1;
                            if(input2){
                                reg.storedValue = false;
                            }
                            break;

                        case RType::T:
                            // T Flip-Flop: Q = T ? !Q : Q (toggle if T=1, hold if T=0)
                            if (input1) {
                                reg.storedValue = !reg.storedValue;
                            }
                            if(input2){
                                reg.storedValue = false;
                            }
                            break;

                        case RType::SR:
                            // SR Flip-Flop: S=input1, R=input2
                            // S=0, R=0: Hold
                            // S=1, R=0: Set (Q=1)
                            // S=0, R=1: Reset (Q=0)
                            // S=1, R=1: Invalid (treat as hold to avoid undefined behavior)
                            if (input1 && !input2) {
                                reg.storedValue = true;   // Set
                            } else if (!input1 && input2) {
                                reg.storedValue = false;  // Reset
                            }
                            // else: Hold (both 0 or both 1)
                            break;

                        case RType::JK:
                            // JK Flip-Flop: J=input1, K=input2
                            // J=0, K=0: Hold
                            // J=1, K=0: Set (Q=1)
                            // J=0, K=1: Reset (Q=0)
                            // J=1, K=1: Toggle
                            if (input1 && !input2) {
                                reg.storedValue = true;   // Set
                            } else if (!input1 && input2) {
                                reg.storedValue = false;  // Reset
                            } else if (input1 && input2) {
                                reg.storedValue = !reg.storedValue;  // Toggle
                            }
                            // else: Hold (both 0)
                            break;
                    }
                }
            }
            reg.prevClkState = currentClock;
        }
        else if (hasEnableConnection && currentEnable) {
            // LATCH BEHAVIOR (level-triggered when clock not connected)
            switch (reg.regType) {
                case RType::D:
                case RType::T:
                    // D/T Latch: transparent when enabled
                    reg.storedValue = input1;
                    break;

                case RType::SR:
                    // SR Latch
                    if (input1 && !input2) {
                        reg.storedValue = true;   // Set
                    } else if (!input1 && input2) {
                        reg.storedValue = false;  // Reset
                    }
                    break;

                case RType::JK:
                    // JK Latch (similar to SR, but J=K=1 toggles)
                    if (input1 && !input2) {
                        reg.storedValue = true;
                    } else if (!input1 && input2) {
                        reg.storedValue = false;
                    } else if (input1 && input2) {
                        reg.storedValue = !reg.storedValue;
                    }
                    break;
            }
        }
        else {
            // No clock, no enable: transparent latch (always follows input)
            switch (reg.regType) {
                case RType::D:
                case RType::T:
                    reg.storedValue = input1;
                    break;

                case RType::SR:
                    if (input1 && !input2) {
                        reg.storedValue = true;
                    } else if (!input1 && input2) {
                        reg.storedValue = false;
                    }
                    break;

                case RType::JK:
                    if (input1 && !input2) {
                        reg.storedValue = true;
                    } else if (!input1 && input2) {
                        reg.storedValue = false;
                    } else if (input1 && input2) {
                        reg.storedValue = !reg.storedValue;
                    }
                    break;
            }
        }
    }
}

void Simulator::tick() {
    sim_running = true;

    if (firstTick) {
        for (size_t i = 1; i < m_nets.size(); i++) {  // Skip net 0
            eventQueue.insert(i);
        }
    firstTick = false;
    }

    for (auto& reg : m_registers) {
        if(!reg.outID){continue;} // dont't write to net 0
        bool oldValue = m_nets[reg.outID];
        if(oldValue != reg.storedValue){
            m_nets[reg.outID] = reg.storedValue;  // Output the stored value
            eventQueue.insert(reg.outID);
        }
    }

    for (auto& source : m_sources) {
        source.currentIndex++;
        if (source.currentIndex >= source.cycleValues.size()) {source.currentIndex = 0;}
        bool oldValue = m_nets[source.outID];
        if(source.outID!=0 && oldValue != source.cycleValues[source.currentIndex]){
            m_nets[source.outID] = source.cycleValues[source.currentIndex];
            eventQueue.insert(source.outID);
        }
    }

    int propagationStep = 0;
    int maxSteps = 100000;

    while (!eventQueue.empty() && propagationStep < maxSteps) {
        auto it = eventQueue.begin();
        u32 changedNetId = *it;
        eventQueue.erase(it);

        processGates(changedNetId);

        processMuxes(changedNetId);

        propagationStep++;
    }

    processRegisters();

    SimResult result;

    for (int i = 0; i < m_sources.size(); i++) {result.sourcesCurrIdx.push_back(m_sources[i].currentIndex);}
    for (int i = 0; i < m_registers.size(); i++) {result.registerValues.push_back(m_registers[i].storedValue);}

    result.netValues = m_nets;
    emit sendResult(result);
}

void Simulator::receiveCircuit(ExportGraph graph){

    clearCircuit();

    m_gates = graph.gates;
    m_sources = graph.sources;
    m_registers = graph.registers;
    m_muxes = graph.muxes;

    firstTick = true;
    auto numNets = map.net2wire.size();
    for (int i = 0; i <= numNets; i++) { m_nets.push_back(false);};

    m_gateFanout.assign(numNets + 1, {});
    m_MuxFanout.assign(numNets + 1, {});

    for (size_t gateIdx = 0; gateIdx < m_gates.size(); gateIdx++) {
        Gate& gate = m_gates[gateIdx];
        for (auto inputNetId: gate.inIDs) {
            if (inputNetId != 0) {
                m_gateFanout[inputNetId].push_back(gateIdx);
            }
        }
    }

    for (size_t muxIdx = 0; muxIdx < m_muxes.size(); muxIdx++) {
        Mux& mux = m_muxes[muxIdx];
        for (auto inputNetId : mux.inData) {
            if (inputNetId != 0) {
                m_MuxFanout[inputNetId].push_back(muxIdx);
            }
        }
        for (auto inputNetId : mux.inAddress) {
            if (inputNetId != 0) {
                m_MuxFanout[inputNetId].push_back(muxIdx);
            }
        }
    }

    sim_running = true;
}

void Simulator::setTimerPeriod(int milliseconds) {
    m_timer->setInterval(milliseconds);
}

void Simulator::printRunDataInfo() {
    if (runData.empty()) {
        qDebug() << "No tick timings recorded.";
        return;
    }

    long long total = 0;
    for (long value : runData) {
        total += value;
    }

    double average = static_cast<double>(total) / static_cast<double>(runData.size());
    qDebug() << "Average tick execution time:" << average << "microseconds";
}

void Simulator::SimController() {
    m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this]() {
        if (sim_running) {
            auto startTime = std::chrono::high_resolution_clock::now();

            tick();

            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

            auto time = duration.count();
            runData.push_back(time);
        }
        });
    m_timer->start(100);
}
