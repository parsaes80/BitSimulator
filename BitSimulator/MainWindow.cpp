#include "MainWindow.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>
#include <QCompleter>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QSaveFile>
#include <QShortcut>
#include <QFontMetricsF>
#include <QFrame>
#include <QRegularExpression>
#include <QLocale>
#include <QHeaderView>
#include <QListWidget>
#include <QSet>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QScrollBar>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QUrl>
#include <QVBoxLayout>
#include "CircuitCanvas.h"
#include "PropertiesPanel.h"
#include "Toolbar.h"
#include "Waveform.h"
#ifdef Q_OS_WASM
#include <QBuffer>
#include <QTimer>
#include <emscripten.h>
#endif

using namespace Qt::StringLiterals;

namespace {
#ifdef Q_OS_WASM
constexpr const char* BrowserSavesDir = "/saves";

// The saves folder is kept in the browser's IndexedDB, so saves outlive the page
EM_ASYNC_JS(void, loadBrowserSaves, (const char* dir), {
    const path = UTF8ToString(dir);
    FS.mkdirTree(path);
    FS.mount(IDBFS, {}, path);
    await new Promise(resolve => FS.syncfs(true, error => { if (error) console.warn('BitSimulator: reading the saves failed', error); resolve(); }));
});
EM_JS(void, storeBrowserSaves, (), {
    FS.syncfs(false, error => { if (error) console.warn('BitSimulator: storing the saves failed', error); });
});
EM_JS(void, warnBeforeLeaving, (int warn), {
    window.onbeforeunload = warn ? (event => { event.preventDefault(); event.returnValue = ""; }) : null;
});
#else
void storeBrowserSaves() {}
#endif
}

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent)
{
    simThread = new QThread(this);
    simObj = new Simulator(nullptr);
    compilerThread = new QThread(this);
    compiler = new HDLCompiler(nullptr);

#if QT_CONFIG(thread)
    simObj->moveToThread(simThread);
    compiler->moveToThread(compilerThread);
#endif
    setup();

#if QT_CONFIG(thread)
    simThread->start();
    compilerThread->start();
#else
    // The browser build has one thread: the simulator ticks on this window's timers
    simObj->setParent(this);
    compiler->setParent(this);
    simObj->SimController();
#endif
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
    if (!maybeSave()) {
        event->ignore();
        return;
    }
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
#ifdef Q_OS_WASM
    // A web page cannot read a folder; yosys (about 50 MB) starts downloading when the Compile tab opens
    ui.tabWidget->removeTab(ui.tabWidget->indexOf(ui.CompileDirTab));
    ui.hdlEditorHint->setText(tr("Put your Verilog code in the editor; the top module is found automatically. VHDL and "
                                 "SystemVerilog packages need the desktop app. The first compile downloads Yosys (about 50 MB)."));
    connect(ui.tabWidget, &QTabWidget::currentChanged, this, [this] {
        if (ui.tabWidget->currentWidget() == ui.hdlEditorTab) HDLCompiler::preloadTools();
    });
#endif
    CircuitScene* scene = ui.camera->getScene();

    ui.slider->setRange(1, 10000);
    ui.slider->setValue(3350); // starting value

    setupPropertiesPanel();
    setupColorScheme();
    setupWaveforms();
    setupSearch();
    setupFiles();

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

    //----- Canvas messages (copy, cut, paste) -----
    connect(ui.camera, &CircuitCanvas::statusMessage, this, [this](const QString& text, int timeoutMs) { statusBar()->showMessage(text, timeoutMs); });

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
    connect(compiler, &HDLCompiler::progress, this, [this](const QString& message) { statusBar()->showMessage(message); });

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
    connect(ui.exportButton, &QPushButton::clicked, this, &MainWindow::exportWaveforms);
    connect(m_waveModel, &WaveformModel::historyGrew, this, &MainWindow::updateWaveStatus);
    connect(m_waveModel, &WaveformModel::signalsChanged, this, &MainWindow::updateWaveStatus);
    updateWaveStatus();
}

//===================== Files tab ========================

void MainWindow::setupFiles()
{
    connect(ui.newButton, &QPushButton::clicked, this, &MainWindow::newCircuit);
    connect(ui.openButton, &QPushButton::clicked, this, &MainWindow::openCircuitDialog);
    connect(ui.saveButton, &QPushButton::clicked, this, &MainWindow::save);
    connect(ui.saveAsButton, &QPushButton::clicked, this, &MainWindow::saveAs);
    connect(new QShortcut(QKeySequence::New, this), &QShortcut::activated, this, &MainWindow::newCircuit);
    connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, this, &MainWindow::openCircuitDialog);
    connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, &MainWindow::save);
    connect(new QShortcut(QKeySequence::SaveAs, this), &QShortcut::activated, this, &MainWindow::saveAs);
    connect(new QShortcut(QKeySequence::Undo, this), &QShortcut::activated, this, [this] { undoOrRedo(false); });
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), this), &QShortcut::activated, this, [this] { undoOrRedo(true); });

    ui.savesList->header()->setStretchLastSection(false);
    ui.savesList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui.savesList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui.savesList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui.savesList, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item) { openCircuit(item->data(0, Qt::UserRole).toString()); });
    connect(ui.savesList, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showSavesMenu);
    connect(ui.tabWidget, &QTabWidget::currentChanged, this, [this] {
        if (ui.tabWidget->currentWidget() == ui.filesTab) refreshSavesList();   // files may have changed outside the app
    });

    CircuitScene* scene = ui.camera->getScene();
    connect(scene, &CircuitScene::circuitModified, this, [this] {
        if (isWindowModified()) return;   // moves report every pixel
        setWindowModified(true);
        showFileState();
    });
    connect(scene, &CircuitScene::historyChanged, this, [this, scene] {
        setWindowModified(!scene->isAtSavedState());   // undoing back to the saved state is unchanged again
        showFileState();
    });
#ifdef Q_OS_WASM
    loadBrowserSaves(BrowserSavesDir);
    setSavesDir(QString::fromLatin1(BrowserSavesDir));
    ui.savesFolder->setText(tr("Stored in this browser"));
    ui.openButtonHint->setText(tr("Open a .bitsim file from your computer (Ctrl+O)"));
    ui.savesHint->setText(tr("Double-click a circuit to open it; right-click it to download or delete it."));
#else
    setSavesDir(QDir(QCoreApplication::applicationDirPath()).filePath(u"saves"_s));
#endif
}

#ifdef Q_OS_WASM
void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::ModifiedChange) warnBeforeLeaving(isWindowModified());
    QMainWindow::changeEvent(event);
}
#endif

void MainWindow::setSavesDir(const QString& dir)
{
    m_savesDir = dir;
    ui.savesFolder->setText(QDir::toNativeSeparators(dir));
    refreshSavesList();
    showFileState();
}

void MainWindow::showFileState()
{
    const QString name = m_currentFile.isEmpty() ? tr("Untitled") : QFileInfo(m_currentFile).completeBaseName();
    setWindowTitle(u"%1[*] - BitSimulator"_s.arg(name));
    ui.currentFileName->setText(isWindowModified() ? tr("%1 (unsaved changes)").arg(name) : name);
    ui.currentFilePath->setText(m_currentFile.isEmpty() ? tr("Not saved yet") : QDir::toNativeSeparators(m_currentFile));
    for (int i = 0; i < ui.savesList->topLevelItemCount(); i++) {
        QTreeWidgetItem* item = ui.savesList->topLevelItem(i);
        QFont font = item->font(0);
        font.setBold(QFileInfo(item->data(0, Qt::UserRole).toString()) == QFileInfo(m_currentFile));
        item->setFont(0, font);
    }
}

void MainWindow::refreshSavesList()
{
    ui.savesList->clear();
    const QFileInfoList files = QDir(m_savesDir).entryInfoList({u"*."_s + QLatin1StringView(CircuitFileSuffix)}, QDir::Files, QDir::Time);
    for (const QFileInfo& file : files) {   // newest first
        auto* item = new QTreeWidgetItem(ui.savesList, {file.completeBaseName(), QLocale().toString(file.lastModified(), QLocale::ShortFormat)});
        item->setData(0, Qt::UserRole, file.absoluteFilePath());
        item->setToolTip(0, QDir::toNativeSeparators(file.absoluteFilePath()));
    }
    showFileState();
}

void MainWindow::showSavesMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = ui.savesList->itemAt(pos);
    if (!item) return;
    const QString path = item->data(0, Qt::UserRole).toString();
    QMenu menu(this);
    connect(menu.addAction(tr("Open")), &QAction::triggered, this, [this, path] { openCircuit(path); });
#ifdef Q_OS_WASM
    connect(menu.addAction(tr("Download")), &QAction::triggered, this, [this, path] {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) QFileDialog::saveFileContent(file.readAll(), QFileInfo(path).fileName(), this);
    });
#endif
    connect(menu.addAction(tr("Delete...")), &QAction::triggered, this, [this, path] {
        const QString name = QFileInfo(path).completeBaseName();
        if (QMessageBox::question(this, tr("Delete circuit"), tr("Delete %1 from the saves folder? This cannot be undone.").arg(name),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        if (!QFile::remove(path)) {
            QMessageBox::warning(this, tr("Delete circuit"), tr("%1 could not be deleted.").arg(name));
            return;
        }
        storeBrowserSaves();
        if (QFileInfo(path) == QFileInfo(m_currentFile)) {
            m_currentFile.clear();   // the circuit stays on the canvas, now unsaved
            ui.camera->getScene()->markUnsaved();
        }
        refreshSavesList();
    });
    menu.exec(ui.savesList->viewport()->mapToGlobal(pos));
}

bool MainWindow::maybeSave()
{
    if (!m_askToSave || !isWindowModified() || ui.camera->getScene()->circuitData().components.isEmpty()) return true;
    const QString name = m_currentFile.isEmpty() ? tr("the untitled circuit") : QFileInfo(m_currentFile).completeBaseName();
    const auto answer = QMessageBox::warning(this, tr("Unsaved changes"), tr("Save the changes to %1?").arg(name),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save) return save();
    return answer == QMessageBox::Discard;
}

void MainWindow::newCircuit()
{
    if (!maybeSave()) return;
    ui.camera->getScene()->resetCircuit();
    m_currentFile.clear();
    ui.camera->getScene()->clearHistory();
    showFileState();
    ui.tabWidget->setCurrentWidget(ui.CircuitTab);
}

void MainWindow::openCircuitDialog()
{
#ifdef Q_OS_WASM
    // The browser's file picker; the file goes into the saves and opens from there
    const auto picked = [this](const QString& fileName, const QByteArray& content) {
        if (fileName.isEmpty()) return;   // cancelled
        // Out of the browser's callback first: the questions below wait in nested event loops
        QTimer::singleShot(0, this, [this, fileName, content] { importCircuit(fileName, content); });
    };
    QFileDialog::getOpenFileContent(tr("BitSimulator circuits (*.%1)").arg(QLatin1StringView(CircuitFileSuffix)), picked, this);
    return;
#endif
    const QString path = QFileDialog::getOpenFileName(this, tr("Open circuit"), m_savesDir,
                                                      tr("BitSimulator circuits (*.%1)").arg(QLatin1StringView(CircuitFileSuffix)));
    if (!path.isEmpty()) openCircuit(path);
}

#ifdef Q_OS_WASM
void MainWindow::importCircuit(const QString& fileName, const QByteArray& content)
{
    const QString name = QFileInfo(fileName).completeBaseName();
    if (const auto data = circuitFromJson(content); !data) {
        QMessageBox::warning(this, tr("Open circuit"), tr("%1 could not be opened. %2").arg(QFileInfo(fileName).fileName(), data.error()));
        return;
    }
    if (!maybeSave()) return;
    const QString path = QDir(m_savesDir).filePath(name + u'.' + QLatin1StringView(CircuitFileSuffix));
    if (QFileInfo::exists(path) && QMessageBox::question(this, tr("Open circuit"), tr("%1 is already in the saves. Replace it?").arg(name),
                                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) < 0 || !file.commit()) {
        QMessageBox::warning(this, tr("Open circuit"), tr("%1 could not be stored: %2").arg(name, file.errorString()));
        return;
    }
    storeBrowserSaves();
    loadCircuitFile(path);
}
#endif

bool MainWindow::openCircuit(const QString& path)
{
    return maybeSave() && loadCircuitFile(path);
}

bool MainWindow::loadCircuitFile(const QString& path)
{
    const QString name = QFileInfo(path).fileName();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Open circuit"), tr("%1 could not be read: %2").arg(name, file.errorString()));
        return false;
    }
    const auto data = circuitFromJson(file.readAll());
    const auto loaded = data ? ui.camera->getScene()->loadCircuit(*data) : std::unexpected(data.error());
    if (!loaded) {
        QMessageBox::warning(this, tr("Open circuit"), tr("%1 could not be opened. %2").arg(name, loaded.error()));
        return false;
    }
    m_currentFile = QFileInfo(path).absoluteFilePath();
    ui.camera->getScene()->clearHistory();
    showFileState();
    ui.tabWidget->setCurrentWidget(ui.CircuitTab);
    ui.camera->showWholeCircuit();
    statusBar()->showMessage(tr("Opened %1.").arg(name), 4000);
    return true;
}

bool MainWindow::save()
{
    return m_currentFile.isEmpty() ? saveAs() : saveCircuit(m_currentFile);
}

bool MainWindow::saveAs()
{
    QString name = m_currentFile.isEmpty() ? QString() : QFileInfo(m_currentFile).completeBaseName();
    while (true) {
        bool ok = false;
        name = QInputDialog::getText(this, tr("Save As"), tr("Name of the circuit in the saves folder:"), QLineEdit::Normal, name, &ok).trimmed();
        if (!ok) return false;
        static const QRegularExpression forbidden(uR"([\\/:*?"<>|])"_s);
        if (name.isEmpty() || name.contains(forbidden) || name.startsWith(u'.')) {
            QMessageBox::warning(this, tr("Save As"), tr("Use a name without \\ / : * ? \" < > | that doesn't start with a dot."));
            continue;
        }
        const QString path = QDir(m_savesDir).filePath(name + u'.' + QLatin1StringView(CircuitFileSuffix));
        if (QFileInfo::exists(path) && QFileInfo(path) != QFileInfo(m_currentFile)
            && QMessageBox::question(this, tr("Save As"), tr("%1 already exists. Replace it?").arg(name),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) continue;
        return saveCircuit(path);
    }
}

bool MainWindow::saveCircuit(const QString& path)
{
    const QString name = QFileInfo(path).fileName();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);   // all or nothing: a failed save leaves the old file
    if (!file.open(QIODevice::WriteOnly) || file.write(circuitToJson(ui.camera->getScene()->circuitData())) < 0 || !file.commit()) {
        QMessageBox::warning(this, tr("Save circuit"), tr("%1 could not be saved: %2").arg(name, file.errorString()));
        return false;
    }
    storeBrowserSaves();
    m_currentFile = QFileInfo(path).absoluteFilePath();
    ui.camera->getScene()->markSaved();
    refreshSavesList();
    statusBar()->showMessage(tr("Saved %1.").arg(name), 4000);
    return true;
}

void MainWindow::setupSearch()
{
    // Filled on every keystroke, so the completer shows the rows as they are instead of filtering them
    m_searchModel = new QStandardItemModel(this);
    m_searchCompleter = new QCompleter(m_searchModel, this);
    m_searchCompleter->setWidget(ui.searchBar);
    m_searchCompleter->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_searchCompleter->setMaxVisibleItems(12);
    m_searchCompleter->popup()->setParent(this, Qt::Popup);   // so the window's style sheet reaches it
    m_searchCompleter->popup()->setObjectName(u"searchResults"_s);

    m_searchIcon = ui.searchBar->addAction(searchIcon(currentTheme()), QLineEdit::LeadingPosition);
    connect(m_searchIcon, &QAction::triggered, ui.searchBar, qOverload<>(&QWidget::setFocus));
    connect(ui.searchBar, &QLineEdit::textEdited, this, &MainWindow::updateSearch);
    connect(m_searchCompleter, qOverload<const QModelIndex&>(&QCompleter::activated), this,
            [this](const QModelIndex& index) { jumpToSearchMatch(index.data(Qt::UserRole).toInt()); });
    connect(ui.searchBar, &QLineEdit::returnPressed, this, [this] {
        updateSearch(ui.searchBar->text());   // the circuit may have changed since the last keystroke
        if (m_searchMatches.size() == 1) jumpToSearchMatch(0);
    });
}

void MainWindow::updateSearch(const QString& text)
{
    m_searchModel->clear();
    m_searchMatches.clear();
    if (!text.isEmpty()) {
        for (ComponentItem* item : ui.camera->getScene()->findInputsAndOutputs(text)) {
            const auto* source = dynamic_cast<SourceItem*>(item);
            const QString name = source ? source->name() : static_cast<DisplayItem*>(item)->name();
            auto* row = new QStandardItem(u"%1   ·  %2"_s.arg(name, source ? tr("input") : tr("output")));
            row->setData(int(m_searchMatches.size()), Qt::UserRole);
            m_searchModel->appendRow(row);
            m_searchMatches.append(item);
        }
    }
    if (m_searchMatches.isEmpty()) {
        m_searchCompleter->popup()->hide();
        return;
    }
    // Wide enough for the longest row, right-aligned under the bar
    QAbstractItemView* list = m_searchCompleter->popup();
    const int width = std::clamp(list->sizeHintForColumn(0) + list->verticalScrollBar()->sizeHint().width() + 24,
                                 ui.searchBar->width(), 420);
    m_searchCompleter->complete(QRect(ui.searchBar->width() - width, 0, width, ui.searchBar->height()));
    list->setCurrentIndex({});   // nothing highlighted until the user picks a row
}

void MainWindow::jumpToSearchMatch(int match)
{
    if (match < 0 || match >= m_searchMatches.size() || !m_searchMatches[match]) return;
    m_searchCompleter->popup()->hide();
    ui.tabWidget->setCurrentWidget(ui.CircuitTab);
    ui.camera->focusOn(m_searchMatches[match]);
}

void MainWindow::updateWaveStatus()
{
    const WaveHistory& history = m_waveModel->history();
    QString text = tr("No run yet");
    if (!history.isEmpty()) {
        switch (ui.camera->getScene()->simState()) {
        case SimState::Running: text = tr("Tick %1").arg(history.lastTick()); break;
        case SimState::Paused: text = tr("Paused at tick %1").arg(history.lastTick()); break;
        case SimState::Stopped: text = tr("Stopped at tick %1").arg(history.lastTick()); break;
        }
    }
    ui.waveStatus->setText(text);
    ui.exportButton->setEnabled(!history.isEmpty());
}

void MainWindow::exportWaveforms()
{
    if (m_waveModel->history().isEmpty()) return;
    // Next to the circuit's file, named after it (or after the compiled top module)
    const QString base = !m_currentFile.isEmpty() ? QFileInfo(m_currentFile).completeBaseName()
                         : !m_waveModel->topName().isEmpty() ? m_waveModel->topName() : u"waveforms"_s;
#ifdef Q_OS_WASM
    // The browser downloads it
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    if (const auto written = writeVcd(buffer, *m_waveModel); !written) {
        QMessageBox::warning(this, tr("Export waveforms"), tr("The waveforms could not be written: %1").arg(written.error()));
        return;
    }
    QFileDialog::saveFileContent(buffer.data(), base + u".vcd"_s, this);
    return;
#endif
    const QString dir = !m_currentFile.isEmpty() ? QFileInfo(m_currentFile).absolutePath() : m_savesDir;
    QString path = QFileDialog::getSaveFileName(this, tr("Export waveforms"), QDir(dir).filePath(base + u".vcd"_s),
                                                tr("Value Change Dump (*.vcd)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += u".vcd"_s;

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    std::expected<void, QString> written = std::unexpected(QString());
    if (file.open(QIODevice::WriteOnly)) written = writeVcd(file, *m_waveModel);
    if (!written || !file.commit()) {
        const QString reason = !written && !written.error().isEmpty() ? written.error() : file.errorString();
        QMessageBox::warning(this, tr("Export waveforms"), tr("%1 could not be written: %2").arg(QFileInfo(path).fileName(), reason));
        return;
    }
    statusBar()->showMessage(tr("Exported ticks %1 to %2 to %3.").arg(m_waveModel->history().firstTick())
                                 .arg(m_waveModel->history().lastTick()).arg(QFileInfo(path).fileName()), 6000);
}

void MainWindow::setupColorScheme()
{
    connect(ui.themeButton, &QPushButton::clicked, this, &MainWindow::toggleColorScheme);
    connect(ui.aboutButton, &QPushButton::clicked, this, &MainWindow::showAboutDialog);
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
    if (m_searchIcon) m_searchIcon->setIcon(searchIcon(theme));   // made in setupSearch(), after the first call
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
    if (!maybeSave()) return;
    ui.camera->getScene()->resetCircuit();   // undoable
    m_currentFile.clear();   // Save must not write the empty canvas over the file
    ui.camera->getScene()->markSaved();
    showFileState();
}

void MainWindow::undoOrRedo(bool redo)
{
    // A text field undoes its own text
    QWidget* focus = QApplication::focusWidget();
    if (qobject_cast<QLineEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus) || qobject_cast<QAbstractSpinBox*>(focus)) return;
    CircuitScene* scene = ui.camera->getScene();
    if (scene->isLocked()) {
        statusBar()->showMessage(tr("Pause or reset the simulation to undo or redo."), 4000);
        return;
    }
    if (redo ? !scene->canRedo() : !scene->canUndo()) {
        statusBar()->showMessage(redo ? tr("Nothing to redo.") : tr("Nothing to undo."), 3000);
        return;
    }
    ui.tabWidget->setCurrentWidget(ui.CircuitTab);
    ui.camera->showLevel(0);   // the change is on the circuit itself
    if (redo) scene->redo();
    else scene->undo();
}

bool MainWindow::beginCompile(const QString& message)
{
    if (!maybeSave()) return false;
    // The old circuit stays until the new one compiles
    ui.camera->getScene()->stopSim();

    m_compileError.clear();
    m_compiling = true;
    ui.compileButton->setEnabled(false);
    updateCompileDirStatus();
    statusBar()->showMessage(message);
    return true;
}

void MainWindow::compileHdl()
{
    if (!beginCompile(tr("Compiling..."))) return;
    sendCodeToCompiler(ui.hdlEditor->toPlainText());
}

void MainWindow::compileFinished(bool ok)
{
    m_compiling = false;
    ui.compileButton->setEnabled(true);
    updateCompileDirStatus();

    if (ok) {
        // A new, untitled circuit; it can be compiled again, so it doesn't count as unsaved
        m_currentFile.clear();
        ui.camera->getScene()->clearHistory();
        showFileState();
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

    if (!beginCompile(files.size() == 1 ? tr("Compiling 1 file...") : tr("Compiling %1 files...").arg(files.size()))) return;
    sendFilesToCompiler(files, m_compileIncludeDirs);
}

//===================== About (the ? button) ========================

namespace {

const QString AboutRepository = u"https://github.com/parsaes80/BitSimulator"_s;

// The build copies licenses/ next to the executable, or next to the page in the browser
QString licenseLink(const QString& file, const QString& text)
{
#ifdef Q_OS_WASM
    const QUrl page(QString::fromUtf8(emscripten_run_script_string("document.baseURI")));
    const QString url = page.resolved(QUrl(u"licenses/"_s + file)).toString();
#else
    const QString url = QUrl::fromLocalFile(QDir(QCoreApplication::applicationDirPath()).filePath(u"licenses/"_s + file)).toString();
#endif
    return u"<a href=\"%1\">%2</a>"_s.arg(url, text);
}

QString htmlLink(const QString& url, const QString& text)
{
    return u"<a href=\"%1\">%2</a>"_s.arg(url, text);
}

QString aboutText(ColorScheme scheme)
{
    QString text = u"<p><img src=\":/icons/logo-%1.png\" width=\"300\"></p>"_s.arg(scheme == ColorScheme::Dark ? u"dark"_s : u"light"_s)
                 + MainWindow::tr("<p><b>Version %1</b><br>An interactive digital circuit simulator and Verilog synthesiser.</p>")
                       .arg(QCoreApplication::applicationVersion());

#ifdef Q_OS_WASM
    text += MainWindow::tr("<h3>This is the web demo</h3>"
                           "<p>The full BitSimulator is a desktop app for Linux and Windows: %1. It is faster, "
                           "and it does what a web page can't:</p>"
                           "<ul>"
                           "<li>VHDL, and SystemVerilog packages and imports</li>"
                           "<li>Compiling a whole folder of files (the Compile Dir tab)</li>"
                           "<li>Saving circuits as files anywhere on your computer</li>"
                           "<li>Compiling right away, without first downloading Yosys (about 50 MB)</li>"
                           "<li>Large designs: Yosys and dot run natively, and the simulation runs on its own thread</li>"
                           "</ul>")
                .arg(htmlLink(AboutRepository + u"/releases"_s, MainWindow::tr("download it from GitHub")));
#endif

    text += MainWindow::tr("<p>Copyright &copy; 2025 Parsa Eslami. Free software under the %1. Source code: %2.</p>")
                .arg(licenseLink(u"BitSimulator-MIT.txt"_s, MainWindow::tr("MIT License")), htmlLink(AboutRepository, u"github.com/parsaes80/BitSimulator"_s));

    text += MainWindow::tr("<h3>Third-party software</h3><ul>");
#ifdef Q_OS_WASM
    const QString qtLinking = MainWindow::tr("Linked into the page unmodified; BitSimulator's source code is public, so it can be rebuilt with a modified Qt. "
                                             "Qt's page loader, qtloader.js, is under the %1 only.").arg(licenseLink(u"GPL-3.0.txt"_s, u"GNU GPLv3"_s));
#else
    const QString qtLinking = MainWindow::tr("Dynamically linked and unmodified.");
#endif
    text += MainWindow::tr("<li><b>Qt %1</b>: the user interface. Copyright &copy; The Qt Company Ltd. and other contributors. "
                           "Used under the %2, which builds on the %3. %4 Its source code is at %5. See also About Qt below.</li>")
                .arg(QString::fromLatin1(qVersion()), licenseLink(u"Qt-LGPL3.txt"_s, u"GNU LGPLv3"_s),
                     licenseLink(u"GPL-3.0.txt"_s, u"GNU GPLv3"_s), qtLinking,
                     htmlLink(u"https://download.qt.io/archive/qt/"_s, u"download.qt.io"_s));
#ifdef Q_OS_WASM
    text += MainWindow::tr("<li><b>Yosys</b>: synthesis, compiled to WebAssembly by the %1 project. %2.</li>")
                .arg(htmlLink(u"https://yowasp.org/"_s, u"YoWASP"_s), licenseLink(u"Yosys-ISC.txt"_s, u"ISC License"_s));
#else
    text += MainWindow::tr("<li><b>Yosys</b>: synthesis. %1.</li>").arg(licenseLink(u"Yosys-ISC.txt"_s, u"ISC License"_s));
    text += MainWindow::tr("<li><b>sv2v</b>: SystemVerilog to Verilog. %1.</li>").arg(licenseLink(u"sv2v-BSD-3.txt"_s, u"BSD 3-Clause License"_s));
    text += MainWindow::tr("<li><b>GHDL</b>: VHDL to Verilog. %1; its source code is at %2.</li>")
                .arg(licenseLink(u"GHDL-GPL-2.0-or-later.txt"_s, u"GNU GPL 2.0 or later"_s), htmlLink(u"https://github.com/ghdl/ghdl"_s, u"github.com/ghdl/ghdl"_s));
#endif
    text += MainWindow::tr("<li><b>Graphviz</b>: the layout of compiled circuits (dot), with BitSimulator's speed-up patch, "
                           "which is in the source code. %1; its source code is at %2.</li>")
                .arg(licenseLink(u"Graphviz-EPL-2.0.txt"_s, u"Eclipse Public License 2.0"_s), htmlLink(u"https://graphviz.org/download/source/"_s, u"graphviz.org"_s));
#ifdef Q_OS_WASM
    text += MainWindow::tr("<li><b>Noto Sans</b>: the interface font. %1.</li>").arg(licenseLink(u"NotoSans-OFL-1.1.txt"_s, u"SIL Open Font License 1.1"_s));
#endif
#ifdef Q_OS_WIN
    text += MainWindow::tr("<li><b>Windows runtime libraries</b> next to yosys.exe and ghdl.exe: GNU Readline (%1), GNU termcap (%2), "
                           "the GCC runtime (%1 with the %3) and others.</li>")
                .arg(licenseLink(u"GPL-3.0.txt"_s, u"GNU GPLv3"_s), licenseLink(u"GPL-2.0.txt"_s, u"GNU GPLv2"_s),
                     licenseLink(u"GCC-Runtime-Library-Exception-3.1.txt"_s, u"GCC Runtime Library Exception"_s));
#endif
    text += MainWindow::tr("</ul><p>The %1 lists every part with its version and where its source code is.</p>")
                .arg(htmlLink(AboutRepository + u"#third-party-software"_s, MainWindow::tr("README")));
    return text;
}

}

void MainWindow::showAboutDialog()
{
    auto* dialog = new QDialog(this);
    dialog->setObjectName(u"aboutDialog"_s);
    dialog->setWindowTitle(MainWindow::tr("About BitSimulator"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(560, 600);

    auto* label = new QLabel(aboutText(m_colorScheme));
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setOpenExternalLinks(true);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    label->setContentsMargins(4, 0, 4, 0);

    auto* scroll = new QScrollArea;
    scroll->setWidget(label);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton* aboutQt = buttons->addButton(MainWindow::tr("About Qt"), QDialogButtonBox::HelpRole);
    QObject::connect(aboutQt, &QPushButton::clicked, dialog, [dialog] { QMessageBox::aboutQt(dialog); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);

    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(scroll);
    layout->addWidget(buttons);
    dialog->show();
}
