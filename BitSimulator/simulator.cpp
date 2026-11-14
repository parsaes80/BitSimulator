#include "simulator.h"
#include "general.h"
#include <QThread>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <chrono>

Simulator::Simulator(QObject *parent) : QObject(parent) {
    m_nets.reserve(1000);
    m_gates.reserve(500);
    m_registers.reserve(100);
    m_sources.reserve(100);
}

void Simulator::clearCircuit() {
    m_nets.clear();
    m_gates.clear();
    m_registers.clear();
    m_sources.clear();
    m_muxes.clear();
    m_gateFanout.clear();
    m_MuxFanout.clear();
    m_eventQueue.clear();
    m_inQueue.clear();
}

void Simulator::processGates(u32 changedNetId) {
    const auto& fanout = m_gateFanout[changedNetId];
    for (u32 gateIdx : fanout) {
        Gate& gate = m_gates[gateIdx];

        if (gate.outID == 0) {
            continue;
        }

        const std::vector<u32>& inputs = gate.inIDs;

        bool newOutput;

        // AND/OR families stop at the first controlling input; XOR needs every input
        switch (gate.gateType) {
        case GType::AND:
        case GType::NAND:
            newOutput = true;
            for (u32 netId : inputs) {
                if (!m_nets[netId]) { newOutput = false; break; }
            }
            if (gate.gateType == GType::NAND) newOutput = !newOutput;
            break;
        case GType::OR:
        case GType::NOR:
            newOutput = false;
            for (u32 netId : inputs) {
                if (m_nets[netId]) { newOutput = true; break; }
            }
            if (gate.gateType == GType::NOR) newOutput = !newOutput;
            break;
        case GType::XOR:
        case GType::XNOR:
            newOutput = false;
            for (u32 netId : inputs) {
                newOutput ^= (m_nets[netId] != 0);
            }
            if (gate.gateType == GType::XNOR) newOutput = !newOutput;
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
            enqueue(outputNetId);
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
            enqueue(outputNetId);
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
        else if (!hasEnableConnection) {
            // No clock, no enable: transparent latch (always follows input).
            // A latch whose enable pin is wired but low reaches none of these branches and holds its value.
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
    if (firstTick) {
        // Every net once, including net 0 ("not connected"): a gate with only unwired inputs
        // hangs off net 0 alone and would otherwise never be evaluated (a NOT of nothing is 1).
        for (size_t i = 0; i < m_nets.size(); i++) {
            enqueue(i);
        }
    firstTick = false;
    }

    for (auto& reg : m_registers) {
        if(!reg.outID){continue;} // dont't write to net 0
        bool oldValue = m_nets[reg.outID];
        if(oldValue != reg.storedValue){
            m_nets[reg.outID] = reg.storedValue;  // Output the stored value
            enqueue(reg.outID);
        }
    }

    // Every source puts its value for this tick on its pins (bit i on pin i)
    const u64 tick = m_tick++;
    for (const auto& source : m_sources) {
        const u64 value = sourceValueAt(source.props, tick);
        for (size_t bit = 0; bit < source.outIDs.size(); bit++) {
            const u32 net = source.outIDs[bit];
            if (net == 0) {continue;}   // pin not connected
            const u8 level = (value >> bit) & 1;
            if (m_nets[net] != level) {
                m_nets[net] = level;
                enqueue(net);
            }
        }
    }

    int propagationStep = 0;
    int maxSteps = 100000;

    // FIFO worklist; nets enqueued while processing are appended and picked up by the same loop
    size_t head = 0;
    while (head < m_eventQueue.size() && propagationStep < maxSteps) {
        u32 changedNetId = m_eventQueue[head++];
        m_inQueue[changedNetId] = 0;

        processGates(changedNetId);

        processMuxes(changedNetId);

        propagationStep++;
    }
    // Drop processed entries; anything left after hitting maxSteps carries over to the next tick
    m_eventQueue.erase(m_eventQueue.begin(), m_eventQueue.begin() + head);

    processRegisters();

    SimResult result;
    result.runId = m_runId;
    result.sourceTick = tick;
    result.registerValues.reserve(m_registers.size());
    for (const auto& reg : m_registers) {result.registerValues.push_back(reg.storedValue);}

    result.netValues = m_nets;
    emit sendResult(std::move(result));
}

void Simulator::receiveCircuit(ExportGraph graph){

    clearCircuit();

    m_gates = graph.gates;
    m_sources = graph.sources;
    m_registers = graph.registers;
    m_muxes = graph.muxes;

    firstTick = true;
    m_tick = 0;
    m_runId = graph.runId;
    const size_t numNets = graph.numNets;   // sent with the circuit; the GUI's wire map is not ours to read
    m_nets.assign(numNets + 1, 0);
    m_inQueue.assign(numNets + 1, 0);
    m_eventQueue.reserve(numNets + 1);

    m_gateFanout.assign(numNets + 1, {});
    m_MuxFanout.assign(numNets + 1, {});

    for (size_t gateIdx = 0; gateIdx < m_gates.size(); gateIdx++) {
        Gate& gate = m_gates[gateIdx];
        // Net 0 gets a fan-out list too. It never changes, so this only costs one evaluation on the first tick.
        for (auto inputNetId: gate.inIDs) {
            m_gateFanout[inputNetId].push_back(gateIdx);
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

    m_running = true;

    // Produce the first values right away; waiting for the timer would leave the canvas showing
    // a running circuit with no values for up to one tick period (a full second at slow speeds).
    tick();
    if (m_timer) m_timer->start();   // the next tick comes one whole period from now
}

void Simulator::setRunning(bool running) {
    m_running = running;
}

void Simulator::unloadCircuit() {
    m_running = false;
    clearCircuit();
}

void Simulator::setTimerPeriod(int milliseconds) {
    if (m_timer) m_timer->setInterval(milliseconds);
}

void Simulator::printRunDataInfo() {
    if (m_tickCount == 0) {
        qDebug() << "No tick timings recorded.";
        return;
    }

    double average = static_cast<double>(m_tickMicrosTotal) / static_cast<double>(m_tickCount);
    qDebug() << "Average tick execution time:" << average << "microseconds";
}

void Simulator::SimController() {
    m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this]() {
        if (m_running) {
            auto startTime = std::chrono::high_resolution_clock::now();

            tick();

            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

            m_tickMicrosTotal += duration.count();
            m_tickCount++;
        }
        });
    m_timer->start(100);
}
