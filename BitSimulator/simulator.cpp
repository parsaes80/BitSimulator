#include "simulator.h"
#include "general.h"
#include <QThread>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>

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
    m_net2wire.clear();
}

void Simulator::simulate(const int numclks) {
    sim_running = true;

    qDebug() << "Starting simulation with" << m_nets.size() << "nets," << m_gates.size() << "gates,"<< m_registers.size() << "registers";

    for (int clk = 0; clk < numclks ; clk++) {

    }
}

void Simulator::receiveCircuit(ExportGraph graph){


    clearCircuit();

    // Copy circuit components (gates and sources contain their output IDs)
    m_gates = graph.gates;
    m_sources = graph.sources;
    m_nets = graph.nets;

    m_gateInputs = graph.gateInputs;

    m_net2wire = graph.net2wire;
    
    int wiresWithMapping = 0;
    for (size_t i = 0; i < m_net2wire.size(); ++i) {
        if (m_net2wire[i]) {
            wiresWithMapping++;
        }
    }

    sim_running = true;
}

void Simulator::SimController() {

    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {if (sim_running) {simulate(100);}});
    timer->start(10000); // every 10 ms
}
