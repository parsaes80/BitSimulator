#include "MainWindow.h"
#include <QDebug>
#include "CircuitCanvas.h"
#include "Toolbar.h"

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent)
{
    // Create thread and Simulator
    simThread = new QThread(this);
    simObj = new Simulator;
    simObj->moveToThread(simThread);

    setup();

    simThread->start();
}

MainWindow::~MainWindow()
{
    simThread->quit();
    simThread->wait();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    qDebug() << "Closing application - stopping simulator thread...";

    if (simThread && simThread->isRunning()) {
        simThread->quit();

        if (!simThread->wait(3000)) {  // Wait up to 3 seconds
            qDebug() << "Thread didn't quit gracefully, terminating...";
            simThread->terminate();
            simThread->wait(1000);
        }
    }

    qDebug() << "Simulator thread stopped";
    QMainWindow::closeEvent(event); 
}

void MainWindow::setup()
{
    ui.setupUi(this);
    // setup button connections to the scene
    ui.andButton->setGateType(GType::AND);
    ui.orButton->setGateType(GType::OR);
    ui.nandButton->setGateType(GType::NAND);
    ui.norButton->setGateType(GType::NOR);
    ui.xorButton->setGateType(GType::XOR);
    ui.xnorButton->setGateType(GType::XNOR);
    ui.notButton->setGateType(GType::NOT);
    connect(ui.andButton,&GateButton::gateTypeSelected,ui.camera->getScene(),&CircuitScene::setNextGateType);
    connect(ui.orButton,&GateButton::gateTypeSelected,ui.camera->getScene(),&CircuitScene::setNextGateType);
    connect(ui.notButton,&GateButton::gateTypeSelected,ui.camera->getScene(),&CircuitScene::setNextGateType);
    connect(ui.nandButton,&GateButton::gateTypeSelected,ui.camera->getScene(),&CircuitScene::setNextGateType);
    connect(ui.norButton,&GateButton::gateTypeSelected,ui.camera->getScene(),&CircuitScene::setNextGateType);
    connect(ui.xorButton,&GateButton::gateTypeSelected,ui.camera->getScene(),&CircuitScene::setNextGateType);
    connect(ui.xnorButton,&GateButton::gateTypeSelected,ui.camera->getScene(),&CircuitScene::setNextGateType);
    connect(ui.srcButton,&SourceButton::sourceSelected,ui.camera->getScene(),&CircuitScene::setNextSource);

    connect(ui.camera->getScene(),&CircuitScene::startSimSIG,simObj,&Simulator::receiveCircuit); //connect scene and sim
    connect(simThread, &QThread::finished, simObj, &QObject::deleteLater);
    connect(simThread, &QThread::started, simObj, &Simulator::SimController);
    connect(simObj, &Simulator::sendResult,ui.camera->getScene(),&CircuitScene::receiveResult);

    qRegisterMetaType<ExportGraph>("ExportGraph");
    qRegisterMetaType<SimResult>("SimResult");
}
void MainWindow::on_startButton_clicked()
{
    ui.camera->getScene()->startSim();
}
