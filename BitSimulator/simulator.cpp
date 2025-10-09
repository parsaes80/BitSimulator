#include "simulator.h"
#include "general.h"
#include <QThread>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <queue>

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

    qDebug() << "Starting simulation with" << m_nets.size() << "nets," << m_gates.size() << "gates,"<< m_registers.size() << "registers";

    std::queue<u32> eventQueue;

    for (auto& source : m_sources) {
        // Check bounds BEFORE incrementing and accessing
        source.currentIndex++;
        if (source.currentIndex >= source.cycleValues.size()) {
            source.currentIndex = 0;  // Wrap around to beginning
        }
        bool oldValue = m_nets[source.outID];
        m_nets[source.outID] = source.cycleValues[source.currentIndex];
        
        eventQueue.push(source.outID);
    }

    int propagationStep = 0;
    int maxSteps = 1000; // Safety limit to prevent infinite loops

    while (!eventQueue.empty() && propagationStep < maxSteps) {
        u32 changedNetId = eventQueue.front();
        eventQueue.pop();
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

            qDebug() << "  Evaluating Gate" << gateIdx << "type:" << (int)gate.gateType;

            // Gather all inputs first
            std::vector<bool> inputs;
            inputs.reserve(gate.numInputs);

            for (u16 i = 0; i < gate.numInputs; i++) {
                u32 inputNetId = m_gateInputs[gate.inID + i];
                bool inputValue = (inputNetId < m_nets.size()) ? m_nets[inputNetId] : false;
                inputs.push_back(inputValue);
                qDebug() << "Input" << i << ": Net" << inputNetId << "=" << inputValue;
            }

            // ===== FIX 3: Evaluate gate logic ONCE with all inputs =====
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
                newOutput = !newOutput;  // Negate at the end
                break;

            case GType::NOR:
                newOutput = false;
                for (bool input : inputs) {
                    newOutput = newOutput || input;
                }
                newOutput = !newOutput;  // Negate at the end
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
                newOutput = !newOutput;  // Negate at the end
                break;

            case GType::NOT:
                newOutput = !inputs[0];
                break;

            default:
                qDebug() << "    Unknown gate type!";
                break;
            }

            qDebug() << "  Gate" << gateIdx << "output: Net" << gate.outID << "=" << newOutput;

            // ===== Check if output changed =====
            u32 outputNetId = gate.outID;
            if (outputNetId < m_nets.size()) {
                bool oldValue = m_nets[outputNetId];

                if (oldValue != newOutput) {
                    // Output changed - update net and queue it
                    m_nets[outputNetId] = newOutput;
                    eventQueue.push(outputNetId);
                    qDebug() << "    -> Output changed from" << oldValue<< "to" << newOutput << "- queued Net" << outputNetId;   
                }
                else {
                    qDebug() << "    -> Output unchanged (" << newOutput << ")";
                }
            }
        }
        propagationStep++;
    }

    qDebug() << "=========== ONE CLOCK COMPLETED ==========";
    SimResult result;

    for (int i = 0; i < m_sources.size(); i++) {
        result.sourcesCurrIdx.push_back(m_sources[i].currentIndex);
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

void Simulator::SimController() {

    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {if (sim_running) {tick();}});
    timer->start(100); 
}
