#include "MainWindow.h"
#include <QDebug>
#include "CircuitCanvas.h"
#include "Toolbar.h"

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent)
{
    setupUI();
    // Create thread and Simulator
    simThread = new QThread(this);
    simObj = new Simulator;
    
    simObj->moveToThread(simThread);
    
    connect(simThread, &QThread::finished, simObj, &QObject::deleteLater);
    connect(simThread, &QThread::started, simObj, &Simulator::SimController);

    simThread->start();
}

MainWindow::~MainWindow()
{
    simThread->quit();
    simThread->wait();
}
// setup button connections to the scene
void MainWindow::setupUI()
{
    ui.setupUi(this);
    ui.andButton->setGateType(GType::AND);
    ui.orButton->setGateType(GType::OR);
    ui.nandButton->setGateType(GType::NAND);
    ui.norButton->setGateType(GType::NOR);
    ui.xorButton->setGateType(GType::XOR);
    ui.xnorButton->setGateType(GType::XNOR);
    ui.notButton->setGateType(GType::NOT);
    connect(ui.andButton,
            &GateButton::gateTypeSelected,
            ui.Camera->getScene(),
            &CircuitScene::setNextGateType);
    connect(ui.orButton,
            &GateButton::gateTypeSelected,
            ui.Camera->getScene(),
            &CircuitScene::setNextGateType);
    connect(ui.notButton,
            &GateButton::gateTypeSelected,
            ui.Camera->getScene(),
            &CircuitScene::setNextGateType);
    connect(ui.nandButton,
            &GateButton::gateTypeSelected,
            ui.Camera->getScene(),
            &CircuitScene::setNextGateType);
    connect(ui.norButton,
            &GateButton::gateTypeSelected,
            ui.Camera->getScene(),
            &CircuitScene::setNextGateType);
    connect(ui.xorButton,
            &GateButton::gateTypeSelected,
            ui.Camera->getScene(),
            &CircuitScene::setNextGateType);
    connect(ui.xnorButton,
            &GateButton::gateTypeSelected,
            ui.Camera->getScene(),
            &CircuitScene::setNextGateType);
    connect(ui.srcButton,
            &SourceButton::sourceSelected,
            ui.Camera->getScene(),
            &CircuitScene::setNextSource);
}
void MainWindow::on_startButton_clicked()
{
    ui.Camera->getScene()->startSim();
}
