#pragma once
#include <QObject>
#include "components.h"

class Simulator : public QObject {
    Q_OBJECT
public:
    explicit Simulator(QObject *parent = nullptr);
    
    // ID-managed add functions
    u32 addNet(bool initialValue = false);
    u32 addGate(GType gateType, const std::vector<u32>& inputNetIds);  // Remove numInputs parameter
    u32 addRegister(RType regType, u32 inputNetId);
    
    void clearCircuit();
    
public slots:
    void simulate(int numclks);
    void SimController(); // Parameterless slot for auto-starting

signals:
    void finished();


private:
    bool sim_running = false;
    
    std::vector<Net> nets;
    std::vector<Gate> gates;
    std::vector<Register> registers;
    std::vector<u32> gateInputs;
    
    // ID counters for proper management
    u32 nextGateinputID = 0;
    u32 nextNetId = 0;
    u32 nextGateId = 0;
    u32 nextRegisterId = 0;
};

