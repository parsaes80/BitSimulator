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
#include <QVBoxLayout>
#include <QMessageBox>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include "CircuitCanvas.h"
#include "PropertiesPanel.h"
#include "Toolbar.h"

using namespace Qt::StringLiterals;

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
    // Always open on the circuit, whichever tab was showing when the .ui was last saved in Designer
    ui.tabWidget->setCurrentWidget(ui.CircuitTab);
    CircuitScene* scene = ui.camera->getScene();

    // Set the slider up before connecting it, so its initial value is not sent to a simulator
    // that has not started yet. 3350 maps to ~100 ms per tick, the simulator's own initial period.
    ui.slider->setRange(1, 10000);
    ui.slider->setValue(3350);

    setupPropertiesPanel();
    setupColorScheme();

    ui.andButton->setGateType(GType::AND);
    ui.orButton->setGateType(GType::OR);
    ui.nandButton->setGateType(GType::NAND);
    ui.norButton->setGateType(GType::NOR);
    ui.xorButton->setGateType(GType::XOR);
    ui.xnorButton->setGateType(GType::XNOR);
    ui.notButton->setGateType(GType::NOT);

    //----- Toolbar: a component button makes that component the next one placed -----
    for (GateButton* button : {ui.andButton, ui.orButton, ui.notButton, ui.nandButton,
                               ui.norButton, ui.xorButton, ui.xnorButton}) {
        connect(button, &QPushButton::clicked, scene, [scene, button] { scene->selectGateTool(button->getGateType()); });
    }
    connect(ui.srcButton, &QPushButton::clicked, scene, [scene] { scene->selectTool(itemKind<SourceProps>); });
    connect(ui.regButton, &QPushButton::clicked, scene, [scene] { scene->selectTool(itemKind<RegisterProps>); });
    connect(ui.muxButton, &QPushButton::clicked, scene, [scene] { scene->selectTool(itemKind<MuxProps>); });
    connect(ui.displayButton, &QPushButton::clicked, scene, [scene] { scene->selectTool(itemKind<DisplayProps>); });

    //----- Toolbar: Start/Pause, Clear and the speed slider -----
    connect(ui.startButton, &QPushButton::clicked, this, &MainWindow::toggleSimulation);
    connect(ui.clearButton, &QPushButton::clicked, this, &MainWindow::clearCircuit);
    connect(ui.slider, &QSlider::valueChanged, this, &MainWindow::updateTimerPeriod);
    connect(scene, &CircuitScene::simStateChanged, this, &MainWindow::showSimState);
    showSimState(scene->simState());

    //----- HDL Editor tab: Compile sends the editor's text to the compiler -----
    connect(ui.compileButton, &QPushButton::clicked, this, &MainWindow::compileHdl);
    connect(this, &MainWindow::sendCodeToCompiler, compiler, &HDLCompiler::receiveCode);
    setupHdlEditor();

    //----- Compile Dir tab: choose a folder, check the files to use, compile them -----
    connect(ui.selectPathButton, &QPushButton::clicked, this, &MainWindow::chooseCompileDir);
    connect(ui.compileDirPath, &QLineEdit::editingFinished, this, [this] {
        // Enter or leaving the field: list the folder that was typed (once, not on every keystroke)
        if (ui.compileDirPath->text().trimmed() != m_scannedDir) scanCompileDir();
    });
    connect(ui.compileFilesDisplayer, &QListWidget::itemChanged, this, &MainWindow::updateCompileDirStatus);
    connect(ui.compileDirButton, &QPushButton::clicked, this, &MainWindow::compileDir);
    connect(this, &MainWindow::sendFilesToCompiler, compiler, &HDLCompiler::receiveFiles);
    updateCompileDirStatus();

    //----- HDL compiler thread: the compiled circuit goes to the canvas, the outcome to the window -----
    connect(compilerThread, &QThread::finished, compiler, &QObject::deleteLater);
    connect(compiler, &HDLCompiler::sendDesign, scene, &CircuitScene::receiveDesign);

    //----- Blocks: the properties panel is for the circuit itself, so it hides inside a block -----
    connect(ui.camera, &CircuitCanvas::depthChanged, this, [this](int depth) { m_propertiesFrame->setVisible(depth == 0); });
    connect(compiler, &HDLCompiler::error, this, [this](const QString& message) { m_compileError = message; });
    connect(compiler, &HDLCompiler::finished, this, &MainWindow::compileFinished);

    //----- Simulator thread: the canvas starts, pauses and stops it; results come back to the canvas -----
    connect(simThread, &QThread::started, simObj, &Simulator::SimController);
    connect(simThread, &QThread::finished, simObj, &QObject::deleteLater);
    connect(scene, &CircuitScene::startSimSIG, simObj, &Simulator::receiveCircuit);
    connect(scene, &CircuitScene::setSimRunningSIG, simObj, &Simulator::setRunning);
    connect(scene, &CircuitScene::stopSimSIG, simObj, &Simulator::unloadCircuit);
    connect(simObj, &Simulator::sendResult, scene, &CircuitScene::receiveResult);
    connect(this, &MainWindow::sendTimerPeriod, simObj, &Simulator::setTimerPeriod);

    qRegisterMetaType<ExportGraph>("ExportGraph");
    qRegisterMetaType<SimResult>("SimResult");
    qRegisterMetaType<NetDesign>("NetDesign");
}

void MainWindow::setupHdlEditor()
{
    // A monospace font, tabs four spaces wide, and no line wrapping
    QFont font(u"Courier"_s);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(12);
    ui.hdlEditor->setFont(font);
    ui.hdlEditor->setTabStopDistance(4 * QFontMetricsF(font).horizontalAdvance(u' '));
    ui.hdlEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    ui.hdlEditor->setPlaceholderText(tr("Enter your Verilog code here..."));
}

void MainWindow::setupPropertiesPanel()
{
    // The panel floats over the top-right corner of the canvas, 2/3 of the canvas high, so the
    // canvas keeps its full width below it. Content taller than that scrolls.
    m_properties = new PropertiesPanel(ui.camera->getScene());

    auto* scroll = new QScrollArea;
    scroll->setObjectName(u"propertiesScroll"_s);
    scroll->setWidget(m_properties);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // A child of the canvas widget, not of its viewport: panning scrolls the viewport's children along
    m_propertiesFrame = new QFrame(ui.camera);
    m_propertiesFrame->setObjectName(u"propertiesFrame"_s);
    auto* layout = new QVBoxLayout(m_propertiesFrame);
    layout->setContentsMargins(2, 4, 2, 4);     // keeps the scroll bar inside the rounded corners
    layout->addWidget(scroll);

    ui.camera->viewport()->installEventFilter(this);   // follow the canvas when it is resized
    placePropertiesPanel();
}

void MainWindow::placePropertiesPanel()
{
    constexpr int Width = 270, Margin = 12;
    const QRect canvas = ui.camera->viewport()->geometry();   // the drawing area, without scroll bars
    const int width = std::max(0, std::min(Width, canvas.width() - 2 * Margin));
    const int height = canvas.height() * 2 / 3;
    m_propertiesFrame->setGeometry(canvas.right() + 1 - Margin - width, canvas.top() + Margin, width, height);
    m_propertiesFrame->raise();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (m_propertiesFrame && watched == ui.camera->viewport() && event->type() == QEvent::Resize) placePropertiesPanel();
    return QMainWindow::eventFilter(watched, event);
}


void MainWindow::setupColorScheme()
{
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

    // The button shows the mode it switches to
    ui.themeButton->setIcon(colorSchemeIcon(scheme));
    ui.themeButton->setToolTip(dark ? tr("Switch to light mode") : tr("Switch to dark mode"));

    // The canvas, its components and the toolbar icons paint with the current theme; the widgets
    // get the palette and the style sheet. Setting the style sheet re-styles and repaints every widget.
    setCurrentTheme(scheme);
    const Theme& theme = currentTheme();
    applyPalette(*qApp, theme);
    setStyleSheet(themeStyleSheet(theme));
    ui.camera->viewport()->update();
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
    double maxOutput = 1000.0;  // Slowest speed (1000ms)

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

    sendTimerPeriod(result);
}
void MainWindow::toggleSimulation()
{
    CircuitScene* scene = ui.camera->getScene();
    if (scene->simState() == SimState::Running) scene->pauseSim();
    else scene->startOrResumeSim();     // from Stopped: run from the first tick; from Paused: continue
}

void MainWindow::showSimState(SimState state)
{
    // One button: "Start" or "Pause". The style sheet (Theme.cpp) colors it through the #startButton[running="true"] rule.
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

void MainWindow::beginCompile(const QString& message)
{
    // Stop first: the simulation must not be running when the compiled circuit replaces this one.
    // The current circuit stays on the canvas until the new one has compiled successfully.
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
        // From the Compile Dir tab nothing of the result would be visible: show it
        if (ui.tabWidget->currentWidget() == ui.CompileDirTab) ui.tabWidget->setCurrentWidget(ui.CircuitTab);
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
        const QSignalBlocker blocker(ui.compileFilesDisplayer);   // filling the list is not a user's change
        ui.compileFilesDisplayer->clear();
    }
    const QDir root(path);
    if (path.isEmpty() || !root.exists()) {
        updateCompileDirStatus();
        return;
    }

    // Every Verilog and SystemVerilog file under the folder, and the folders holding them or
    // their headers (searched for `include). Stops after a few seconds on a huge folder.
    constexpr qint64 MaxScanMs = 3000;
    QElapsedTimer timer;
    timer.start();
    QStringList sources;
    QSet<QString> includeDirs;
    QDirIterator it(root.absolutePath(), {u"*.v"_s, u"*.sv"_s, u"*.vh"_s, u"*.svh"_s},
                    QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    bool stopped = false;
    while (it.hasNext()) {
        const QFileInfo file(it.next());
        includeDirs.insert(file.absolutePath());
        const QString suffix = file.suffix().toLower();
        if (suffix == u"v" || suffix == u"sv") sources.append(file.absoluteFilePath());
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
    else if (total == 0) text = tr("No .v or .sv files in this folder.");
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

