#include "MainWindow.h"
#include <QDebug>
#include "CircuitCanvas.h"
#include "Toolbar.h"

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent)
{
    // Create thread and Simulator
    simThread = new QThread(this);
    simObj = new Simulator(nullptr);
    compilerThread = new QThread(this);
    compiler = new HDLCompiler(nullptr);

    simObj->moveToThread(simThread);
    compiler->moveToThread(compilerThread);
    setup();

    simThread->start();
    compilerThread->start();
}

MainWindow::~MainWindow()
{
    simThread->quit();
    simThread->wait();
    
    compilerThread->quit();
    compilerThread->wait();
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
    ui.slider->setRange(1, 10000);  // 1ms to 1000ms
    ui.slider->setValue(3350);
    on_slider_valueChanged(3350);
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
    connect(ui.muxButton, &MuxButton::MuxSelected, ui.camera->getScene(), &CircuitScene::setNextMux);
    connect(ui.displayButton, &DisplayButton::DisplaySelected, ui.camera->getScene(), &CircuitScene::setNextDisplay);

    connect(ui.hdlEditor,&TextEditor::sendCode,compiler,&HDLCompiler::receiveCode);
    connect(ui.camera->getScene(),&CircuitScene::startSimSIG,simObj,&Simulator::receiveCircuit); //connect scene and sim
    connect(simThread, &QThread::finished, simObj, &QObject::deleteLater);
    connect(compilerThread, &QThread::finished, compiler, &QObject::deleteLater);
    connect(simThread, &QThread::started, simObj, &Simulator::SimController);
    connect(simObj, &Simulator::sendResult,ui.camera->getScene(),&CircuitScene::receiveResult);
    connect(this, &MainWindow::sendTimerPeriod, simObj, &Simulator::setTimerPeriod);

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
    }
    else {
        // Invalid input
        values.clear();
        values.append(0);
        scene->setSrcCycleValues(values);
        ui.srcvalues->setStyleSheet("QTextEdit { background-color: #ffcccc; }");
        // ui.statusLabel->setText("Invalid input - use only 0s and 1s");
    }
}

void MainWindow::on_slider_valueChanged(int value) {
    double minInput = 1.0;      // Slider minimum
    double maxInput = 10000.0;  // Slider maximum
    double minOutput = 1.0;     // Fastest speed (1ms)
    double maxOutput = 1000.0; // Slowest speed (10000ms)

    // Normalize input to [0, 1] range
    double normalizedInput = (value - minInput) / (maxInput - minInput);

    // Invert so higher slider values = faster speed (lower ms)
    double invertedInput = 1.0 - normalizedInput;

    // Apply logarithmic scaling
    double logMin = std::log(minOutput);
    double logMax = std::log(maxOutput);
    double logResult = logMin + invertedInput * (logMax - logMin);

    // Convert back from log space
    int result = static_cast<int>(std::round(std::exp(logResult)));

    // Clamp to valid range
    result = qBound(1, result, 10000);

    emit sendTimerPeriod(result);
}

void MainWindow::on_pushButton_clicked()
{
    ui.hdlEditor->onSendCode();
}

