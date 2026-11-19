#include "MainWindow.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QFrame>
#include <QListWidget>
#include <QSet>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QMessageBox>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include "CircuitCanvas.h"
#include "PropertiesPanel.h"
#include "Toolbar.h"
#include "Waveform.h"

using namespace Qt::StringLiterals;

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent)
{
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
        // Only the simulator's thread may read its data
        QMetaObject::invokeMethod(simObj, &Simulator::printRunDataInfo, Qt::BlockingQueuedConnection);
        simThread->quit();

        if (!simThread->wait(3000)) {
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
    // Whatever tab Designer last saved
    ui.tabWidget->setCurrentWidget(ui.CircuitTab);
    CircuitScene* scene = ui.camera->getScene();

    // Set up before connecting, so a not-yet-started simulator gets no value; 3350 ~ 100 ms per tick
    ui.slider->setRange(1, 10000);
    ui.slider->setValue(3350);

    setupPropertiesPanel();
    setupColorScheme();
    setupWaveforms();

    ui.andButton->setGateType(GType::AND);
    ui.orButton->setGateType(GType::OR);
    ui.nandButton->setGateType(GType::NAND);
    ui.norButton->setGateType(GType::NOR);
    ui.xorButton->setGateType(GType::XOR);
    ui.xnorButton->setGateType(GType::XNOR);
    ui.notButton->setGateType(GType::NOT);

    //----- Toolbar -----
    for (GateButton* button : {ui.andButton, ui.orButton, ui.notButton, ui.nandButton,
                               ui.norButton, ui.xorButton, ui.xnorButton}) {
        connect(button, &QPushButton::clicked, scene, [scene, button] { scene->selectGateTool(button->getGateType()); });
    }
    connect(ui.srcButton, &QPushButton::clicked, scene, [scene] { scene->selectTool(itemKind<SourceProps>); });
    connect(ui.regButton, &QPushButton::clicked, scene, [scene] { scene->selectTool(itemKind<RegisterProps>); });
    connect(ui.muxButton, &QPushButton::clicked, scene, [scene] { scene->selectTool(itemKind<MuxProps>); });
    connect(ui.displayButton, &QPushButton::clicked, scene, [scene] { scene->selectTool(itemKind<DisplayProps>); });

    //----- Start/Pause, Reset, Clear and the speed slider -----
    connect(ui.startButton, &QPushButton::clicked, this, &MainWindow::toggleSimulation);
    connect(ui.waveStartButton, &QPushButton::clicked, this, &MainWindow::toggleSimulation);
    connect(ui.resetButton, &QPushButton::clicked, this, &MainWindow::resetSimulation);
    connect(ui.waveResetButton, &QPushButton::clicked, this, &MainWindow::resetSimulation);
    connect(ui.clearButton, &QPushButton::clicked, this, &MainWindow::clearCircuit);
    connect(ui.slider, &QSlider::valueChanged, this, &MainWindow::updateTimerPeriod);
    connect(scene, &CircuitScene::simStateChanged, this, &MainWindow::showSimState);
    connect(m_waveModel, &WaveformModel::signalsChanged, this, [this, scene] { showSimState(scene->simState()); });   // Reset's state
    showSimState(scene->simState());

    //----- Compile file tab -----
    connect(ui.compileButton, &QPushButton::clicked, this, &MainWindow::compileHdl);
    connect(this, &MainWindow::sendCodeToCompiler, compiler, &HDLCompiler::receiveCode);
    setupHdlEditor();

    //----- Compile Dir tab -----
    connect(ui.selectPathButton, &QPushButton::clicked, this, &MainWindow::chooseCompileDir);
    connect(ui.compileDirPath, &QLineEdit::editingFinished, this, [this] {
        // Once per edit, not on every keystroke
        if (ui.compileDirPath->text().trimmed() != m_scannedDir) scanCompileDir();
    });
    connect(ui.compileFilesDisplayer, &QListWidget::itemChanged, this, &MainWindow::updateCompileDirStatus);
    connect(ui.compileDirButton, &QPushButton::clicked, this, &MainWindow::compileDir);
    connect(this, &MainWindow::sendFilesToCompiler, compiler, &HDLCompiler::receiveFiles);
    updateCompileDirStatus();

    //----- HDL compiler thread -----
    connect(compilerThread, &QThread::finished, compiler, &QObject::deleteLater);
    connect(compiler, &HDLCompiler::sendDesign, scene, &CircuitScene::receiveDesign);

    //----- The properties panel is for the circuit itself, so it hides inside a block -----
    connect(ui.camera, &CircuitCanvas::depthChanged, this, [this](int depth) { m_propertiesFrame->setVisible(depth == 0); });
    connect(compiler, &HDLCompiler::error, this, [this](const QString& message) { m_compileError = message; });
    connect(compiler, &HDLCompiler::finished, this, &MainWindow::compileFinished);

    //----- Simulator thread -----
    connect(simThread, &QThread::started, simObj, &Simulator::SimController);
    connect(simThread, &QThread::finished, simObj, &QObject::deleteLater);
    connect(scene, &CircuitScene::startSimSIG, simObj, &Simulator::receiveCircuit);
    connect(scene, &CircuitScene::setSimRunningSIG, simObj, &Simulator::setRunning);
    connect(scene, &CircuitScene::stopSimSIG, simObj, &Simulator::unloadCircuit);
    connect(simObj, &Simulator::sendResult, scene, &CircuitScene::queueResult);   // shown at most once per frame
    connect(this, &MainWindow::sendTimerPeriod, simObj, &Simulator::setTimerPeriod);

    qRegisterMetaType<ExportGraph>("ExportGraph");
    qRegisterMetaType<SimResult>("SimResult");
    qRegisterMetaType<WaveChanges>("WaveChanges");
    qRegisterMetaType<NetDesign>("NetDesign");
}

void MainWindow::setupHdlEditor()
{
    QFont font(u"Courier"_s);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(12);
    ui.hdlEditor->setFont(font);
    ui.hdlEditor->setTabStopDistance(4 * QFontMetricsF(font).horizontalAdvance(u' '));
    ui.hdlEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    ui.hdlEditor->setPlaceholderText(tr("Enter your Verilog or VHDL code here..."));
}

void MainWindow::setupPropertiesPanel()
{
    m_properties = new PropertiesPanel(ui.camera->getScene());

    auto* scroll = new QScrollArea;
    scroll->setObjectName(u"propertiesScroll"_s);
    scroll->setWidget(m_properties);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // A child of the canvas, not its viewport, which pans its children
    m_propertiesFrame = new FloatingPanel(tr("Properties"), scroll, ui.camera->viewport(), ui.camera);
    m_propertiesFrame->setObjectName(u"propertiesFrame"_s);
}


void MainWindow::setupWaveforms()
{
    CircuitScene* scene = ui.camera->getScene();
    m_waveModel = new WaveformModel(this);
    ui.waveView->setModel(m_waveModel);
    new WaveSignalTree(ui.waveSignalTree, m_waveModel, this);
    ui.waveSplitter->setStretchFactor(1, 1);
    ui.waveSplitter->setSizes({240, 1000});

    // A run's history stays after it stops; a new circuit starts over
    connect(scene, &CircuitScene::startSimSIG, m_waveModel, [this, scene](const ExportGraph& graph) {
        m_waveModel->startRun(graph.runId, scene->waveSignals(), graph.recordedNets, scene->design().top);
    });
    connect(scene, &CircuitScene::simStateChanged, m_waveModel, [this](SimState state) {
        if (state == SimState::Stopped) m_waveModel->stopRun();
        updateWaveStatus();
    });
    connect(scene, &CircuitScene::designChanged, m_waveModel, &WaveformModel::resetSignals);
    connect(simObj, &Simulator::sendWaveChanges, m_waveModel, &WaveformModel::addChanges);   // queued: from the simulator's thread

    connect(ui.waveZoomIn, &QPushButton::clicked, ui.waveView, &WaveformView::zoomIn);
    connect(ui.waveZoomOut, &QPushButton::clicked, ui.waveView, &WaveformView::zoomOut);
    connect(ui.waveZoomFit, &QPushButton::clicked, ui.waveView, [this] {
        ui.waveView->setFollow(false);
        ui.waveView->zoomToFit();
    });
    connect(ui.waveFollow, &QPushButton::toggled, ui.waveView, &WaveformView::setFollow);
    connect(ui.waveView, &WaveformView::followChanged, ui.waveFollow, &QPushButton::setChecked);
    connect(m_waveModel, &WaveformModel::historyGrew, this, &MainWindow::updateWaveStatus);
    connect(m_waveModel, &WaveformModel::signalsChanged, this, &MainWindow::updateWaveStatus);
    updateWaveStatus();
}

void MainWindow::updateWaveStatus()
{
    const WaveHistory& history = m_waveModel->history();
    QString text;
    if (!history.isEmpty()) {
        switch (ui.camera->getScene()->simState()) {
        case SimState::Running: text = tr("Tick %1").arg(history.lastTick()); break;
        case SimState::Paused: text = tr("Paused at tick %1").arg(history.lastTick()); break;
        case SimState::Stopped: text = tr("Stopped at tick %1").arg(history.lastTick()); break;
        }
    }
    ui.waveStatus->setText(text);
}

void MainWindow::setupColorScheme()
{
    connect(ui.themeButton, &QPushButton::clicked, this, &MainWindow::toggleColorScheme);
    // Follows the system until the user picks a mode
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_followSystemScheme) setColorScheme(systemColorScheme());
    });
    setColorScheme(systemColorScheme());
}

void MainWindow::setColorScheme(ColorScheme scheme)
{
    m_colorScheme = scheme;
    const bool dark = (scheme == ColorScheme::Dark);

    ui.themeButton->setIcon(colorSchemeIcon(scheme));
    ui.themeButton->setToolTip(dark ? tr("Switch to light mode") : tr("Switch to dark mode"));

    // Canvas items paint with the current theme; widgets get the palette and style sheet
    setCurrentTheme(scheme);
    const Theme& theme = currentTheme();
    applyPalette(*qApp, theme);
    setStyleSheet(themeStyleSheet(theme));
    ui.camera->resetCachedContent();   // the cached dot grid has the old colors
    ui.camera->viewport()->update();
}

void MainWindow::toggleColorScheme()
{
    m_followSystemScheme = false;
    setColorScheme(m_colorScheme == ColorScheme::Dark ? ColorScheme::Light : ColorScheme::Dark);
}

void MainWindow::updateTimerPeriod(int value) {
    double minInput = 1.0;
    double maxInput = 10000.0;
    double minOutput = 1.0;     // ms per tick
    double maxOutput = 1000.0;

    double normalizedInput = (value - minInput) / (maxInput - minInput);

    // Higher slider = faster
    double invertedInput = 1.0 - normalizedInput;

    // Logarithmic
    double logMin = std::log(minOutput);
    double logMax = std::log(maxOutput);
    double logResult = logMin + invertedInput * (logMax - logMin);

    int result = static_cast<int>(std::round(std::exp(logResult)));

    result = qBound(1, result, 10000);

    sendTimerPeriod(result);
}
void MainWindow::toggleSimulation()
{
    CircuitScene* scene = ui.camera->getScene();
    if (scene->simState() == SimState::Running) scene->pauseSim();
    else scene->startOrResumeSim();     // from Stopped: tick 0; from Paused: continue
}

void MainWindow::showSimState(SimState state)
{
    // Colored by the [running="true"] rule in Theme.cpp
    const bool running = (state == SimState::Running);
    for (QPushButton* button : {ui.startButton, ui.waveStartButton}) {
        button->setText(running ? tr("Pause") : tr("Start"));
        button->setProperty("running", running);
        button->style()->unpolish(button);   // a style sheet re-reads a property only on a re-polish
        button->style()->polish(button);
        button->setToolTip(running ? tr("Freeze the simulation")
                                   : state == SimState::Paused ? tr("Continue the simulation")
                                                               : tr("Run the circuit from the first tick"));
    }
    // A stopped circuit with no waveforms has nothing to reset
    const bool canReset = state != SimState::Stopped || !m_waveModel->history().isEmpty();
    ui.resetButton->setEnabled(canReset);
    ui.waveResetButton->setEnabled(canReset);
}

void MainWindow::resetSimulation()
{
    // Like an edit during a pause, but the waveforms go too
    ui.camera->getScene()->stopSim();
    m_waveModel->clearHistory();
    updateWaveStatus();
}

void MainWindow::clearCircuit()
{
    ui.camera->getScene()->resetCircuit();
}

void MainWindow::beginCompile(const QString& message)
{
    // The old circuit stays until the new one compiles
    ui.camera->getScene()->stopSim();

    m_compileError.clear();
    m_compiling = true;
    ui.compileButton->setEnabled(false);
    updateCompileDirStatus();
    statusBar()->showMessage(message);
}

void MainWindow::compileHdl()
{
    beginCompile(tr("Compiling..."));
    sendCodeToCompiler(ui.hdlEditor->toPlainText());
}

void MainWindow::compileFinished(bool ok)
{
    m_compiling = false;
    ui.compileButton->setEnabled(true);
    updateCompileDirStatus();

    if (ok) {
        statusBar()->showMessage(tr("Compiled. The new circuit is on the canvas."), 6000);
        ui.tabWidget->setCurrentWidget(ui.CircuitTab);
        return;
    }

    statusBar()->showMessage(tr("Compilation failed. The circuit on the canvas was kept."), 8000);

    const QString details = m_compileError.isEmpty() ? tr("The compiler gave no further details.") : m_compileError;
    auto* box = new QMessageBox(QMessageBox::Warning, tr("Compilation failed"),
                                tr("The Verilog could not be compiled, so the circuit on the canvas was left as it is."),
                                QMessageBox::Ok, this);
    box->setObjectName(u"compileErrorBox"_s);
    box->setInformativeText(details.size() > 600 ? details.left(600) + u"..."_s : details);
    if (details.size() > 600) box->setDetailedText(details);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();        // not exec(): the window stays usable
}

//===================== Compile Dir tab ========================

void MainWindow::chooseCompileDir()
{
    const QString current = ui.compileDirPath->text().trimmed();
    const QString start = !current.isEmpty() && QDir(current).exists() ? current : QDir::homePath();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose the folder with your Verilog files"), start);
    if (dir.isEmpty()) return;      // cancelled

    ui.compileDirPath->setText(QDir::toNativeSeparators(dir));
    scanCompileDir();
}

void MainWindow::scanCompileDir()
{
    const QString path = ui.compileDirPath->text().trimmed();
    m_scannedDir = path;
    m_compileIncludeDirs.clear();
    {
        const QSignalBlocker blocker(ui.compileFilesDisplayer);   // filling it is not a user's change
        ui.compileFilesDisplayer->clear();
    }
    const QDir root(path);
    if (path.isEmpty() || !root.exists()) {
        updateCompileDirStatus();
        return;
    }

    // HDL files under the folder and the folders for `include; gives up after a few seconds
    constexpr qint64 MaxScanMs = 3000;
    QElapsedTimer timer;
    timer.start();
    QStringList sources;
    QSet<QString> includeDirs;
    QDirIterator it(root.absolutePath(), {u"*.v"_s, u"*.sv"_s, u"*.vh"_s, u"*.svh"_s, u"*.vhd"_s, u"*.vhdl"_s},
                    QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    bool stopped = false;
    while (it.hasNext()) {
        const QFileInfo file(it.next());
        includeDirs.insert(file.absolutePath());
        const QString suffix = file.suffix().toLower();
        if (suffix == u"v" || suffix == u"sv" || suffix == u"vhd" || suffix == u"vhdl") sources.append(file.absoluteFilePath());
        if (timer.elapsed() > MaxScanMs) { stopped = true; break; }
    }
    std::ranges::sort(sources, [&root](const QString& a, const QString& b) {
        return root.relativeFilePath(a).compare(root.relativeFilePath(b), Qt::CaseInsensitive) < 0;
    });
    m_compileIncludeDirs = QStringList(includeDirs.begin(), includeDirs.end());
    m_compileIncludeDirs.sort();

    {
        const QSignalBlocker blocker(ui.compileFilesDisplayer);
        for (const QString& file : std::as_const(sources)) {
            auto* item = new QListWidgetItem(QDir::toNativeSeparators(root.relativeFilePath(file)), ui.compileFilesDisplayer);
            item->setData(Qt::UserRole, file);
            item->setToolTip(QDir::toNativeSeparators(file));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        }
    }
    updateCompileDirStatus();
    if (stopped) statusBar()->showMessage(tr("The folder is very large; only the files found in the first few seconds are listed."), 8000);
}

void MainWindow::updateCompileDirStatus()
{
    int checked = 0;
    const int total = ui.compileFilesDisplayer->count();
    for (int row = 0; row < total; row++) {
        if (ui.compileFilesDisplayer->item(row)->checkState() == Qt::Checked) checked++;
    }

    QString text;
    if (m_scannedDir.isEmpty()) text = tr("No folder chosen yet.");
    else if (!QDir(m_scannedDir).exists()) text = tr("This folder does not exist.");
    else if (total == 0) text = tr("No Verilog or VHDL files in this folder.");
    else text = tr("%1 of %2 files selected").arg(checked).arg(total);
    ui.compileDirCount->setText(text);
    ui.compileDirButton->setEnabled(checked > 0 && !m_compiling);
}

void MainWindow::compileDir()
{
    QStringList files;
    for (int row = 0; row < ui.compileFilesDisplayer->count(); row++) {
        const QListWidgetItem* item = ui.compileFilesDisplayer->item(row);
        if (item->checkState() == Qt::Checked) files.append(item->data(Qt::UserRole).toString());
    }
    if (files.isEmpty()) return;

    beginCompile(files.size() == 1 ? tr("Compiling 1 file...") : tr("Compiling %1 files...").arg(files.size()));
    sendFilesToCompiler(files, m_compileIncludeDirs);
}

