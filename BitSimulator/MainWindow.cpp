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
    //setup slider
    ui.slider->setRange(1, 10000);  // 1ms to 1000ms
    ui.slider->setValue(3350);
    on_slider_valueChanged(3350);

    ui.numDisplayInputs->setRange(1,8);
    ui.numDisplayInputs->setValue(2);
    ui.numDisplayOutputs->setRange(0,8);
    ui.numDisplayOutputs->setValue(2);
    ui.numGateMuxInputs->setRange(2, 8);
    ui.numGateMuxInputs->setValue(2);

    ui.overlay->setCurrentIndex(0);

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

    connect(ui.andButton,&GateButton::setOverlay,ui.overlay,&QStackedWidget::setCurrentIndex);
    connect(ui.orButton,&GateButton::setOverlay,ui.overlay,&QStackedWidget::setCurrentIndex);
    connect(ui.notButton,&GateButton::setOverlay,ui.overlay,&QStackedWidget::setCurrentIndex);
    connect(ui.nandButton,&GateButton::setOverlay,ui.overlay,&QStackedWidget::setCurrentIndex);
    connect(ui.norButton,&GateButton::setOverlay,ui.overlay,&QStackedWidget::setCurrentIndex);
    connect(ui.xorButton,&GateButton::setOverlay,ui.overlay,&QStackedWidget::setCurrentIndex);
    connect(ui.xnorButton,&GateButton::setOverlay,ui.overlay,&QStackedWidget::setCurrentIndex);
    connect(ui.srcButton,&SourceButton::setOverlay,ui.overlay,&QStackedWidget::setCurrentIndex);
    connect(ui.regButton, &RegisterButton::setOverlay, ui.overlay,&QStackedWidget::setCurrentIndex);
    connect(ui.muxButton, &MuxButton::setOverlay,ui.overlay,&QStackedWidget::setCurrentIndex);
    connect(ui.displayButton, &DisplayButton::setOverlay,ui.overlay,&QStackedWidget::setCurrentIndex);

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
void MainWindow::on_startButton_clicked()
{
    ui.camera->getScene()->startSim();
}
void MainWindow::on_pushButton_clicked()
{
    ui.hdlEditor->onSendCode();
}

void MainWindow::on_overlay_currentChanged(int arg1)
{
    auto scene = ui.camera->getScene();
    
    // Block signals to prevent feedback loops
    const bool oldState = ui.overlay->blockSignals(true);
    
    switch(arg1) {
    case 0: // Gate/Mux page
        ui.numGateMuxInputs->setValue(scene->getNumInputs());
        break;
        
    case 1: // Register page
        // Set register type based on current scene state
        if (scene->getHasEnable()) {
            ui.regEnableType->setCurrentIndex(1);
        } else {
            ui.regEnableType->setCurrentIndex(0);
        }
        
        if (scene->getIsFlipFlop()) {
            ui.regHoldType->setCurrentIndex(0);
        } else {
            ui.regHoldType->setCurrentIndex(1);
        }
        break;
        
    case 2: // Source page
        if (scene->getSrcIsWave()) {
            ui.srcType->setCurrentIndex(0);
            // Show current bool values in text edit
            if (std::holds_alternative<QList<bool>>(scene->getSrcValues())) {
                const auto boolValues = std::get<QList<bool>>(scene->getSrcValues());
                QString text;
                for (bool val : boolValues) {
                    text += val ? "1" : "0";
                }
                ui.srcValues->setPlainText(text);
            }
        } else {
            ui.srcType->setCurrentIndex(1);
            // Show current int values in text edit
            if (std::holds_alternative<QList<int>>(scene->getSrcValues())) {
                const auto intValues = std::get<QList<int>>(scene->getSrcValues());
                QStringList textList;
                for (int val : intValues) {
                    textList << QString::number(val);
                }
                ui.srcValues->setPlainText(textList.join(","));
            }
        }
        break;
        
    case 3: // Display page
        ui.numDisplayInputs->setValue(scene->getNumInputs());
        ui.numDisplayOutputs->setValue(scene->getNumOutputs());
        break;
    }
    
    ui.overlay->blockSignals(oldState);
}

void MainWindow::on_numGateMuxInputs_valueChanged(int value)
{
    ui.camera->getScene()->setNumInputs(value);
}

void MainWindow::on_regType_currentIndexChanged(int index)
{
    switch (index) {
    case 0:
        ui.camera->getScene()->setNextRegister(RType::SR);
        break;
    case 1:
        ui.camera->getScene()->setNextRegister(RType::JK);
        break;
    case 2:
        ui.camera->getScene()->setNextRegister(RType::D);
        break;
    case 3:
        ui.camera->getScene()->setNextRegister(RType::T);
        break;
    default:
        break;
    }
}

void MainWindow::on_regHoldType_currentIndexChanged(int index)
{
    switch (index) {
    case 0:
        ui.camera->getScene()->setIsFlipFlop(true);
        break;
    case 1:
        ui.camera->getScene()->setIsFlipFlop(false);
        break;
    }
}

void MainWindow::on_regEnableType_currentIndexChanged(int index)
{
    switch (index) {
    case 0:
        ui.camera->getScene()->setHasEnable(true);
        break;
    case 1:
        ui.camera->getScene()->setHasEnable(false);
        break;
    }
}


void MainWindow::on_srcType_currentIndexChanged(int index)
{
    switch (index) {
    case 0:
        ui.camera->getScene()->setSrcValues(QList<bool>({false,true}));
        break;
    case 1:
        ui.camera->getScene()->setSrcValues(QList<int>({0,1,2}));
        break;
    }
}


void MainWindow::on_srcValues_textChanged()
{
    if(std::holds_alternative<QList<bool>>(ui.camera->getScene()->getSrcValues())){
        auto scene = ui.camera->getScene();
        QString text = ui.srcValues->toPlainText();

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
            ui.srcValues->setStyleSheet("");
            scene->setSrcValues(values);
        }
        else {
            // Invalid input
            values.clear();
            values.append(0);
            scene->setSrcValues(values);
            ui.srcValues->setStyleSheet("QTextEdit { background-color: #ffcccc; }");
            // ui.statusLabel->setText("Invalid input - use only 0s and 1s");
        }
    }
    else{
        // Handle QList<int> case
        auto scene = ui.camera->getScene();
        QString text = ui.srcValues->toPlainText();
        
        QList<int> values;
        bool isValid = true;
        
        // Split by comma or whitespace
        QStringList parts = text.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts);
        
        for (const QString& part : parts) {
            bool ok;
            int value = part.toInt(&ok);
            if (ok && value >= 0) {
                values.append(value);
            } else {
                isValid = false;
                break;
            }
        }
        
        if (isValid && !values.isEmpty()) {
            // Valid input
            ui.srcValues->setStyleSheet("");
            scene->setSrcValues(values);
        } else {
            // Invalid input
            values.clear();
            values.append(0);
            scene->setSrcValues(values);
            ui.srcValues->setStyleSheet("QTextEdit { background-color: #ffcccc; }");
        }
    }
}


void MainWindow::on_numDisplayInputs_valueChanged(int value)
{
    ui.camera->getScene()->setNumInputs(value);
}


void MainWindow::on_numDisplayOutputs_valueChanged(int value)
{
    ui.camera->getScene()->setNumOutputs(value);
}

