#include "MainWindow.h"
#include <QDebug>
#include <QDockWidget>
#include <QMessageBox>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include "CircuitCanvas.h"
#include "PropertiesPanel.h"
#include "Toolbar.h"

GlobalMap map;

QGraphicsScene* scenePtr;
MainWindow* mainMenuPtr;

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

    ui.camera->getScene()->stopSim();

    if (simThread && simThread->isRunning()) {
        // Run it in the simulator's own thread, which is the only one allowed to read its data
        QMetaObject::invokeMethod(simObj, &Simulator::printRunDataInfo, Qt::BlockingQueuedConnection);
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
    mainMenuPtr = this;
    CircuitScene* scene = ui.camera->getScene();

    // Set the slider up before connecting it, so its initial value is not sent to a simulator
    // that has not started yet. 3350 maps to ~100 ms per tick, the simulator's own initial period.
    ui.slider->setRange(1, 10000);
    ui.slider->setValue(3350);

    setupPropertiesPanel();
    setupColorScheme();

    // Toolbar: each button makes its component the active tool
    ui.andButton->setGateType(GType::AND);
    ui.orButton->setGateType(GType::OR);
    ui.nandButton->setGateType(GType::NAND);
    ui.norButton->setGateType(GType::NOR);
    ui.xorButton->setGateType(GType::XOR);
    ui.xnorButton->setGateType(GType::XNOR);
    ui.notButton->setGateType(GType::NOT);

    for (GateButton* button : {ui.andButton, ui.orButton, ui.notButton, ui.nandButton,
                               ui.norButton, ui.xorButton, ui.xnorButton}) {
        connect(button, &GateButton::gateTypeSelected, scene, &CircuitScene::selectGateTool);
    }
    connect(ui.srcButton, &SourceButton::sourceSelected, scene, [scene] { scene->selectTool(itemKind<SourceProps>); });
    connect(ui.regButton, &RegisterButton::RegSelected, scene, [scene] { scene->selectTool(itemKind<RegisterProps>); });
    connect(ui.muxButton, &MuxButton::MuxSelected, scene, [scene] { scene->selectTool(itemKind<MuxProps>); });
    connect(ui.displayButton, &DisplayButton::DisplaySelected, scene, [scene] { scene->selectTool(itemKind<DisplayProps>); });

    // Toolbar: simulation controls
    connect(ui.startButton, &QPushButton::clicked, this, &MainWindow::toggleSimulation);
    connect(ui.clearButton, &QPushButton::clicked, this, &MainWindow::clearCircuit);
    connect(ui.compileButton, &QPushButton::clicked, this, &MainWindow::compileHdl);
    connect(ui.slider, &QSlider::valueChanged, this, &MainWindow::updateTimerPeriod);
    connect(scene, &CircuitScene::simStateChanged, this, &MainWindow::showSimState);
    showSimState(scene->simState());

    // Simulator and HDL compiler threads
    connect(ui.hdlEditor,&TextEditor::sendCode,compiler,&HDLCompiler::receiveCode);
    connect(scene,&CircuitScene::startSimSIG,simObj,&Simulator::receiveCircuit);
    connect(scene,&CircuitScene::setSimRunningSIG,simObj,&Simulator::setRunning);
    connect(scene,&CircuitScene::stopSimSIG,simObj,&Simulator::unloadCircuit);
    connect(simThread, &QThread::finished, simObj, &QObject::deleteLater);
    connect(compilerThread, &QThread::finished, compiler, &QObject::deleteLater);
    connect(simThread, &QThread::started, simObj, &Simulator::SimController);
    connect(simObj, &Simulator::sendResult,scene,&CircuitScene::receiveResult);
    connect(this, &MainWindow::sendTimerPeriod, simObj, &Simulator::setTimerPeriod);
    connect(compiler, &HDLCompiler::sendGraph, scene, &CircuitScene::receiveGraph);
    connect(compiler, &HDLCompiler::error, this, [this](const QString& message) { m_compileError = message; });
    connect(compiler, &HDLCompiler::finished, this, &MainWindow::compileFinished);

    qRegisterMetaType<ExportGraph>("ExportGraph");
    qRegisterMetaType<SimResult>("SimResult");
}

void MainWindow::setupPropertiesPanel()
{
    // The canvas gets its own (nested) main window so that the properties dock sits beside the
    // canvas only. Docked to this window it would also sit beside the toolbar and widen the app.
    auto* canvasHost = new QMainWindow;
    canvasHost->setWindowFlags(Qt::Widget);
    ui.gridLayout_2->replaceWidget(ui.camera, canvasHost);
    canvasHost->setCentralWidget(ui.camera);

    m_properties = new PropertiesPanel(ui.camera->getScene());

    auto* dock = new QDockWidget(tr("Properties"), canvasHost);
    dock->setObjectName(QStringLiteral("propertiesDock"));
    dock->setWidget(m_properties);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);   // no close button: there is no menu to bring it back
    canvasHost->addDockWidget(Qt::RightDockWidgetArea, dock);
    canvasHost->resizeDocks({dock}, {270}, Qt::Horizontal);
}

void MainWindow::setupColorScheme()
{
    // The style sheet setupUi() just applied is the dark one; light mode is derived from it
    m_themeSheets = ThemeSheets(styleSheet());
    if (!m_themeSheets.unpairedColors().isEmpty()) {
        qWarning() << "BitSimulator.ui: these style sheet colors have no light-mode color in the table at its top:"
                   << m_themeSheets.unpairedColors();
    }

    connect(ui.themeButton, &QPushButton::clicked, this, &MainWindow::toggleColorScheme);
    // Follow the system setting, also when it changes while the app runs, until the user picks a mode
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_followSystemScheme) setColorScheme(systemColorScheme());
    });
    setColorScheme(systemColorScheme());
}

void MainWindow::setColorScheme(ColorScheme scheme)
{
    m_colorScheme = scheme;
    const bool dark = (scheme == ColorScheme::Dark);

    // The button shows the mode it switches to; BitSimulator.ui picks its icon by this property
    ui.themeButton->setProperty("scheme", dark ? "dark" : "light");
    ui.themeButton->setToolTip(dark ? tr("Switch to light mode") : tr("Switch to dark mode"));

    applyPalette(*qApp, m_themeSheets, scheme);
    // Setting the style sheet re-styles every widget, and through its qproperty-* rule hands the canvas its colors
    setStyleSheet(m_themeSheets.sheet(scheme));
}

void MainWindow::toggleColorScheme()
{
    m_followSystemScheme = false;
    setColorScheme(m_colorScheme == ColorScheme::Dark ? ColorScheme::Light : ColorScheme::Dark);
}

void MainWindow::updateTimerPeriod(int value) {
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
void MainWindow::toggleSimulation()
{
    CircuitScene* scene = ui.camera->getScene();
    if (scene->simState() == SimState::Running) scene->pauseSim();
    else scene->startOrResumeSim();     // from Stopped: run from the first tick; from Paused: continue
}

void MainWindow::showSimState(SimState state)
{
    // One button: "Start" or "Pause". BitSimulator.ui colors it through the #startButton[running="true"] rule.
    const bool running = (state == SimState::Running);
    ui.startButton->setText(running ? tr("Pause") : tr("Start"));
    ui.startButton->setProperty("running", running);
    ui.startButton->style()->unpolish(ui.startButton);   // a style sheet only re-reads a property on a re-polish
    ui.startButton->style()->polish(ui.startButton);
    ui.startButton->setToolTip(running ? tr("Freeze the simulation")
                                       : state == SimState::Paused ? tr("Continue the simulation")
                                                                   : tr("Run the circuit from the first tick"));
}

void MainWindow::clearCircuit()
{
    ui.camera->getScene()->resetCircuit();   // stops the simulation, then empties the canvas
}

void MainWindow::compileHdl()
{
    // Stop first: the simulation must not be running when the compiled circuit replaces this one.
    // The current circuit stays on the canvas until the new one has compiled successfully.
    ui.camera->getScene()->stopSim();

    m_compileError.clear();
    ui.compileButton->setEnabled(false);
    statusBar()->showMessage(tr("Compiling..."));
    ui.hdlEditor->onSendCode();
}

void MainWindow::compileFinished(bool ok)
{
    ui.compileButton->setEnabled(true);

    if (ok) {
        statusBar()->showMessage(tr("Compiled. The new circuit is on the canvas."), 6000);
        return;
    }

    statusBar()->showMessage(tr("Compilation failed. The circuit on the canvas was kept."), 8000);

    const QString details = m_compileError.isEmpty() ? tr("The compiler gave no further details.") : m_compileError;
    auto* box = new QMessageBox(QMessageBox::Warning, tr("Compilation failed"),
                                tr("The Verilog could not be compiled, so the circuit on the canvas was left as it is."),
                                QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("compileErrorBox"));
    box->setInformativeText(details.size() > 600 ? details.left(600) + QStringLiteral("...") : details);
    if (details.size() > 600) box->setDetailedText(details);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();        // not exec(): the window stays usable
}
