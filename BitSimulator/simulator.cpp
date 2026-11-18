#include "simulator.h"
#include <QDebug>
#include <QTimer>
#include <chrono>

namespace {
// The next value of an SR or JK register, edge-triggered or not: S (J) sets, R (K) resets, both
// together hold an SR register and toggle a JK register, neither holds.
bool setOrReset(RType type, bool stored, bool set, bool reset)
{
    if (set != reset) return set;
    return (set && type == RType::JK) ? !stored : stored;
}
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

void Simulator::processRegisters(bool firstTick) {
    for (auto& reg : m_registers) {
        // Net 0 is "not connected" and always reads 0
        const bool hasClock = reg.clkID != 0;
        const bool hasEnable = reg.enableID != 0;
        const bool clock = m_nets[reg.clkID];
        const bool enable = m_nets[reg.enableID];
        const bool input1 = m_nets[reg.inID];     // D, T, S or J
        const bool input2 = m_nets[reg.InID2];    // the reset of D and T, R or K

        const bool hasReset = reg.regType == RType::D || reg.regType == RType::T;   // their second input

        if (hasClock) {
            // Flip-flop: changes on the rising clock edge, when enabled (always, without an enable pin).
            // An asynchronous reset acts at once, whatever the clock and enable do.
            const bool risingEdge = !firstTick && !reg.prevClkState && clock;
            reg.prevClkState = clock;
            if (hasReset && reg.asyncReset && input2) {
                reg.storedValue = reg.resetValue;
                continue;
            }
            if (!risingEdge || (hasEnable && !enable)) continue;
            switch (reg.regType) {
            case RType::D: reg.storedValue = input2 ? reg.resetValue : input1; break;
            case RType::T: reg.storedValue = input2 ? reg.resetValue : (input1 != reg.storedValue); break;   // T = 1 toggles
            case RType::SR:
            case RType::JK: reg.storedValue = setOrReset(reg.regType, reg.storedValue, input1, input2); break;
            }
        } else if (!hasEnable || enable) {
            // Latch: follows its inputs while enabled (always, without an enable pin); a latch whose
            // enable pin is wired but low holds its value
            switch (reg.regType) {
            case RType::D:
            case RType::T: reg.storedValue = input1; break;
            case RType::SR:
            case RType::JK: reg.storedValue = setOrReset(reg.regType, reg.storedValue, input1, input2); break;
            }
        }
    }
}

void Simulator::tick() {
    const bool firstTick = m_firstTick;
    if (m_firstTick) {
        // Every net once, including net 0 ("not connected"): a gate with only unwired inputs
        // hangs off net 0 alone and would otherwise never be evaluated (a NOT of nothing is 1).
        for (size_t i = 0; i < m_nets.size(); i++) {
            enqueue(i);
        }
        m_firstTick = false;
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
    // The queue now lists every net that changed in this tick, also any left over after maxSteps
    recordWaveChanges(tick);

    // Drop processed entries; anything left after hitting maxSteps carries over to the next tick
    m_eventQueue.erase(m_eventQueue.begin(), m_eventQueue.begin() + head);

    processRegisters(firstTick);

    SimResult result;
    result.runId = m_runId;
    result.sourceTick = tick;
    result.registerValues.reserve(m_registers.size());
    for (const auto& reg : m_registers) {result.registerValues.push_back(reg.storedValue);}

    result.netValues = m_nets;
    // Recorded above, before the result: whatever the result sets off (a pause, say) finds this tick in the waveforms
    sendResult(std::move(result));
}

void Simulator::recordWaveChanges(u64 tick)
{
    // A net queued twice in one tick, or changed and changed back, is compared with its last recorded value
    for (const u32 net : m_eventQueue) {
        const u32 slot = m_recordedSlot[net];
        if (!slot) continue;
        const u8 value = m_nets[net];
        if (value == m_recordedValues[slot - 1]) continue;
        m_recordedValues[slot - 1] = value;
        m_waveChanges.changes.push_back({tick, net});
    }
    m_waveChanges.lastTick = tick;
    m_waveChanges.hasTicks = true;
    // At fast speeds many ticks go into one report; at slow ones every tick is reported at once
    if (m_sinceWaveReport.elapsed() >= WaveReportMs) reportWaveChanges();
}

void Simulator::reportWaveChanges()
{
    m_sinceWaveReport.restart();
    if (!m_waveChanges.hasTicks) return;
    WaveChanges report;
    report.runId = m_waveChanges.runId;
    report.lastTick = m_waveChanges.lastTick;
    report.hasTicks = true;
    std::swap(report.changes, m_waveChanges.changes);
    m_waveChanges.hasTicks = false;
    sendWaveChanges(std::move(report));
}

void Simulator::receiveCircuit(ExportGraph graph){

    reportWaveChanges();   // the end of a run that was not stopped first
    clearCircuit();

    m_gates = graph.gates;
    m_sources = graph.sources;
    m_registers = graph.registers;
    m_muxes = graph.muxes;

    m_firstTick = true;
    m_tick = 0;
    m_runId = graph.runId;

    // Every recorded net starts at 0; the first tick reports the ones that are not
    m_recordedNets = graph.recordedNets;
    std::erase_if(m_recordedNets, [&graph](u32 net) { return net == 0 || net > graph.numNets; });
    m_recordedValues.assign(m_recordedNets.size(), 0);
    m_recordedSlot.assign(size_t(graph.numNets) + 1, 0);
    for (size_t i = 0; i < m_recordedNets.size(); i++) m_recordedSlot[m_recordedNets[i]] = u32(i + 1);
    m_waveChanges = WaveChanges{};
    m_waveChanges.runId = graph.runId;
    m_sinceWaveReport.start();
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
    if (!running) reportWaveChanges();   // a paused run's waveforms reach the newest tick
}

void Simulator::unloadCircuit() {
    m_running = false;
    reportWaveChanges();                 // the waveforms keep the stopped run, up to its last tick
    m_recordedNets.clear();
    m_recordedValues.clear();
    m_recordedSlot.clear();
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
