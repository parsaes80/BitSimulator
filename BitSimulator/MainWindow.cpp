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
    connect(ui.regButton, &RegisterButton::RegSelected, ui.camera->getScene(), &CircuitScene::setNextRegister);

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

void MainWindow::on_srcvalues_textChanged() {
    auto scene = ui.camera->getScene();
    QString text = ui.srcvalues->toPlainText();

    QList<bool> values;
    bool isValid = true;
    QString cleanText;  // For displaying cleaned version

    // Parse and clean text
    for (QChar c : text) {
        if (c == '0') {
            values.append(false);
            cleanText += '0';
        }
        else if (c == '1') {
            values.append(true);
            cleanText += '1';
        }
        else if (c.isSpace() || c == ',' || c == '-') {
            // Allow separators but don't include in cleanText
            continue;
        }
        else {
            isValid = false;
            break;
        }
    }

    if (isValid && !values.isEmpty()) {
        // Valid input
        ui.srcvalues->setStyleSheet("");
        scene->setSrcCycleValues(values);

        // Update status label (if you have one)
        QString statusText = QString("Pattern: %1 (%2 bits)")
            .arg(cleanText)
            .arg(values.size());
        //ui.statusLabel->setText(statusText);  // Optional status display

        qDebug() << statusText;
    }
    else {
        // Invalid input
        ui.srcvalues->setStyleSheet("QTextEdit { background-color: #ffcccc; }");
        // ui.statusLabel->setText("Invalid input - use only 0s and 1s");
    }
}
