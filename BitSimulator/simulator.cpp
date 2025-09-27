#include "simulator.h"
#include "general.h"
#include <QThread>
#include <QDebug>

Simulator::Simulator(QObject *parent) : QObject(parent) {
    sim_running = false;
    
    nets.reserve(1000);
    gates.reserve(500);
    registers.reserve(100);
    gateInputs.reserve(2000);
}

// ID-managed add functions
u32 Simulator::addNet(bool initialValue) {
    nets.push_back(initialValue);
    nextNetId++;
    return nextNetId;
}

u32 Simulator::addGate(const GType gateType, const std::vector<u32>& inputNetIds) {

    gates.push_back(Gate(gateType,nextGateId,10,2));
    return 0;
}

u32 Simulator::addRegister(RType regType, u32 inputNetId) {
    return 0;
}


void Simulator::clearCircuit() {
    nets.clear();
    gates.clear();
    registers.clear();
    gateInputs.clear();
    nextNetId = 0;
    nextGateId = 0;
    nextRegisterId = 0;
}

void Simulator::simulate(const int numclks) {
    sim_running = true;
    
    qDebug() << "Starting simulation with" << nets.size() << "nets," << gates.size() << "gates," << registers.size() << "registers";
    
    for (int clk = 0; clk < numclks ; clk++) {
      
    }
    
    emit finished();
}

void Simulator::SimController() {
    // Default simulation with 100 clock cycles
    for (int i = 0; i < 10; i++) if (i % 2)addNet(false); else addNet(true);
    std::vector<u32> innetIDs = { 2,3 };
    addGate(GType::AND, innetIDs);
    simulate(100);
}
