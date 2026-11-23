#pragma once

#include <QtWidgets/QMainWindow>
#include <QPointer>
#include <QThread>
#include "ui_BitSimulator.h"
#include "simulator.h"
#include "Theme.h"

class ComponentItem;
class FloatingPanel;
class QCompleter;
class QStandardItemModel;
class PropertiesPanel;
class WaveformModel;
enum class SimState;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    ColorScheme colorScheme() const { return m_colorScheme; }

    //----- Files -----
    bool openCircuit(const QString& path);   // asks about unsaved changes first
    bool saveCircuit(const QString& path);
    const QString& currentFile() const { return m_currentFile; }   // empty: untitled
    const QString& savesDir() const { return m_savesDir; }
    void setSavesDir(const QString& dir);
    void setAskToSave(bool ask) { m_askToSave = ask; }            // off in tests
    // Restyles the whole window
    void setColorScheme(ColorScheme scheme);
protected:
    void closeEvent(QCloseEvent* event) override;
#ifdef Q_OS_WASM
    void changeEvent(QEvent* event) override;   // unsaved changes: the browser asks before leaving the page
#endif
private:
    // Connected explicitly in setup(), no on_<object>_<signal> auto-connection
    void toggleSimulation();                 // Start/Pause
    void showSimState(SimState state);
    void resetSimulation();                  // end the run, clear its waveforms
    void clearCircuit();
    void compileHdl();
    void compileFinished(bool ok);
    void chooseCompileDir();
    void scanCompileDir();                   // the .v / .sv files under the folder in the path field
    void updateCompileDirStatus();           // "3 of 4 files selected"
    void compileDir();
    void updateTimerPeriod(int sliderValue);
    void toggleColorScheme();                // stops following the system setting
    void undoOrRedo(bool redo);              // Ctrl+Z / Ctrl+Y
    void newCircuit();
    void openCircuitDialog();
    bool loadCircuitFile(const QString& path);   // without asking about unsaved changes
#ifdef Q_OS_WASM
    void importCircuit(const QString& fileName, const QByteArray& content);   // a file picked in the browser
#endif
    bool save();                             // false if cancelled or failed
    bool saveAs();                           // asks for a name in the saves folder
    bool maybeSave();                        // false: the user cancelled
    void showFileState();                    // title, Files tab labels
    void refreshSavesList();
    void showSavesMenu(const QPoint& pos);
    void updateSearch(const QString& text);  // lists the matches under the search bar
    void jumpToSearchMatch(int match);

signals:
    void sendTimerPeriod(int milliseconds);
    void sendCodeToCompiler(const QString& code);
    void sendFilesToCompiler(const QStringList& files, const QStringList& includeDirs);
private:
    void setup();
    void setupHdlEditor();
    void setupPropertiesPanel();
    void setupColorScheme();
    void setupWaveforms();
    void setupSearch();
    void setupFiles();
    void updateWaveStatus();                 // also whether there is a run to export
    void exportWaveforms();
    bool beginCompile(const QString& message);   // stops the run, disables both compile buttons; false if cancelled

    Ui::BitSimulatorClass ui;
    PropertiesPanel* m_properties = nullptr;
    FloatingPanel* m_propertiesFrame = nullptr;
    WaveformModel* m_waveModel = nullptr;
    QStandardItemModel* m_searchModel = nullptr;
    QCompleter* m_searchCompleter = nullptr;
    QAction* m_searchIcon = nullptr;         // the magnifier; recolored with the mode
    QList<QPointer<ComponentItem>> m_searchMatches;   // the listed ones, by row
    QString m_currentFile;
    QString m_savesDir;
    bool m_askToSave = true;
    ColorScheme m_colorScheme = ColorScheme::Dark;
    bool m_followSystemScheme = true;        // until the user picks a mode
    QString m_compileError;                  // shown if the compile fails
    bool m_compiling = false;
    QString m_scannedDir;
    QStringList m_compileIncludeDirs;        // for `include
    QThread *simThread, *compilerThread;
    Simulator *simObj;
    HDLCompiler *compiler;

    //----- About -----
    void showAboutDialog();   // the ? button: version, license, source code, every third-party part with its license
};
