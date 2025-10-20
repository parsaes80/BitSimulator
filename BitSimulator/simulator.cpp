#include "simulator.h"
#include "general.h"
#include <QThread>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <queue>
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
}

void Simulator::tick() {
    sim_running = true;

    std::set<u32> eventQueue;

    for (auto& reg : m_registers) {
        bool oldValue = m_nets[reg.outID];
        m_nets[reg.outID] = reg.storedValue;  // Output the stored value    
        eventQueue.insert(reg.outID);
    }
    for (auto& source : m_sources) {
        // Check bounds BEFORE incrementing and accessing
        source.currentIndex++;
        if (source.currentIndex >= source.cycleValues.size()) {
            source.currentIndex = 0;  // Wrap around to beginning
        }
        bool oldValue = m_nets[source.outID];
        m_nets[source.outID] = source.cycleValues[source.currentIndex];
        
        eventQueue.insert(source.outID);
    }

    int propagationStep = 0;
    int maxSteps = 1000; // Safety limit to prevent infinite loops

    while (!eventQueue.empty() && propagationStep < maxSteps) {
         auto it = eventQueue.begin();
         u32 changedNetId = *it;
         eventQueue.erase(it);

        // Find all gates that have this net as an input
        for (size_t gateIdx = 0; gateIdx < m_gates.size(); gateIdx++) {
            Gate& gate = m_gates[gateIdx];

            // Check if this gate uses the changed net as input
            bool gateUsesNet = false;
            for (u16 i = 0; i < gate.numInputs; i++) {
                u32 inputNetId = m_gateInputs[gate.inID + i];
                if (inputNetId == changedNetId) {
                    gateUsesNet = true;
                    break;
                }
            }

            if (!gateUsesNet) {
                continue; // This gate doesn't use the changed net
            }

            // Gather all inputs first
            std::vector<bool> inputs;
            inputs.reserve(gate.numInputs);

            for (u16 i = 0; i < gate.numInputs; i++) {
                u32 inputNetId = m_gateInputs[gate.inID + i];
                bool inputValue = (inputNetId < m_nets.size()) ? m_nets[inputNetId] : false;
                inputs.push_back(inputValue);
                
            }
            // ===== Evaluate gate logic ONCE with all inputs =====
            bool newOutput;

            switch (gate.gateType) {
            case GType::AND:
                newOutput = true;
                for (bool input : inputs) {
                    newOutput = newOutput && input;
                }
                break;
            case GType::OR:
                newOutput = false;
                for (bool input : inputs) {
                    newOutput = newOutput || input;
                }
                break;
            case GType::NAND:
                newOutput = true;
                for (bool input : inputs) {
                    newOutput = newOutput && input;
                }
                newOutput = !newOutput;  
                break;
            case GType::NOR:
                newOutput = false;
                for (bool input : inputs) {
                    newOutput = newOutput || input;
                }
                newOutput = !newOutput;  
                break;
            case GType::XOR:
                newOutput = false;
                for (bool input : inputs) {
                    newOutput = newOutput ^ input;
                }
                break;
            case GType::XNOR:
                newOutput = false;
                for (bool input : inputs) {
                    newOutput = newOutput ^ input;
                }
                newOutput = !newOutput;  
                break;

            case GType::NOT:
                newOutput = !inputs[0];
                break;

            default:               
                break;
            }

            // ===== Check if output changed =====
            u32 outputNetId = gate.outID;
            if (outputNetId < m_nets.size()) {
                bool oldValue = m_nets[outputNetId];

                if (oldValue != newOutput) {
                    // Output changed - update net and queue it
                    m_nets[outputNetId] = newOutput;
                    eventQueue.insert(outputNetId);
                }
            }
        }
        propagationStep++;
    }
    // Update your register processing in tick():
    for (auto& reg : m_registers) {
        bool currentClock =  m_nets[reg.clkID];
        bool currentEnable = m_nets[reg.enableID];

        // Determine behavior based on connections
        bool hasClockConnection = (reg.clkID != 0);  // 0 means not connected
        bool hasEnableConnection = (reg.enableID != 0);

        if (hasClockConnection) {
            // FLIP-FLOP BEHAVIOR (edge-triggered) (clk connected)
            bool risingEdge = !reg.prevClkState && currentClock;

            if (risingEdge) {
                // Check enable if connected, otherwise always enabled
                bool enabled = !hasEnableConnection || currentEnable;

                if (enabled) {                   
                    reg.storedValue = m_nets[reg.inID];          
                }
            }
            reg.prevClkState = currentClock;
        }
        else if (hasEnableConnection && currentEnable) {
            // LATCH BEHAVIOR (level-triggered) (clk not connected)
            reg.storedValue = m_nets[reg.inID];
        }
        else {
            reg.storedValue = m_nets[reg.inID];
        }
    }

    SimResult result;

    for (int i = 0; i < m_sources.size(); i++) {
        result.sourcesCurrIdx.push_back(m_sources[i].currentIndex);
    }
    for (int i = 0; i < m_registers.size(); i++) {
        result.registerValues.push_back(m_registers[i].storedValue);
    }

    result.netValues = m_nets;
    emit sendResult(result);
}

void Simulator::receiveCircuit(ExportGraph graph){

    clearCircuit();

    m_gates = graph.gates;
    m_sources = graph.sources;
    m_registers = graph.registers;

    m_gateInputs = graph.gateInputs;

    auto numNets = map.net2wire.size();
    for (int i = 0; i <= numNets; i++) { m_nets.push_back(false);};

    sim_running = true;
}

void Simulator::setTimerPeriod(int milliseconds) {
    m_timer->setInterval(milliseconds);
}

void Simulator::SimController() {
    m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this]() {
        if (sim_running) {
            // Start timing
            auto startTime = std::chrono::high_resolution_clock::now();
            
            // Execute the tick
            tick();
            
            // End timing and calculate duration
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            
            // Print execution time
            //qDebug() << "Tick execution time:" << duration.count() << "microseconds (" 
                     //<< duration.count() / 1000.0 << "ms)";
        }
        });
    m_timer->start(100);  
}
