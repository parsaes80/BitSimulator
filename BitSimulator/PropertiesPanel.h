#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>
#include "ItemProps.h"

class CircuitScene;
enum class SimState;
class ComponentItem;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QStackedWidget;

// A row of steps drawn as a digital waveform; clicking a step flips it between 0 and 1.
class WaveformEditor : public QWidget {
    Q_OBJECT
public:
    explicit WaveformEditor(QWidget* parent = nullptr);

    void setValues(const QList<bool>& values);
    const QList<bool>& values() const { return m_values; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void valuesEdited(const QList<bool>& values);   // only emitted for clicks, not for setValues()

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    int rows() const;

    QList<bool> m_values;
};

// The inspector beside the canvas.
//
// With one component selected it shows and edits that component. With nothing
// selected it shows the defaults of the active tool, i.e. what the next placed
// component will look like. Both cases use the same editors because both are
// described by ItemProps.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(CircuitScene* scene, QWidget* parent = nullptr);

    // Running: the circuit is read-only, so the editors are disabled. Paused: a notice explains what an edit does.
    void showSimState(SimState state);

private:
    void refresh();                         // selection or tool changed: decide what to show
    void load(const ItemProps& props);      // put properties into the editors
    void commit(const ItemProps& props);    // apply an edit to the selected component or the tool defaults
    void showMessage(const QString& text);

    // Takes the properties on screen, lets `change` modify them, and applies the result.
    template <typename Props, typename Change>
    void edit(Change&& change);

    void addPage(std::size_t kind, QWidget* page);
    QWidget* buildGatePage();
    QWidget* buildRegisterPage();
    QWidget* buildMuxPage();
    QWidget* buildSourcePage();
    QWidget* buildDisplayPage();

    void loadGate(const GateProps& props);
    void loadRegister(const RegisterProps& props);
    void loadMux(const MuxProps& props);
    void loadSource(const SourceProps& props);
    void loadDisplay(const DisplayProps& props);

    CircuitScene* m_scene;
    QPointer<ComponentItem> m_item;         // the selected component, or null when editing tool defaults
    ItemProps m_shown;                      // what the editors currently display
    bool m_loading = false;                 // true while load() fills the editors, so they do not commit
    bool m_locked = false;

    // Remembered so that switching a source between waveform and values keeps what was typed
    QList<bool> m_lastWaveform{false, true};
    QList<int> m_lastNumbers{0, 1, 2};

    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QWidget* m_nextSection = nullptr;       // heading shown above the editors when nothing is selected
    QLabel* m_nextTitle = nullptr;
    QLabel* m_placeHint = nullptr;
    QStackedWidget* m_pages = nullptr;
    int m_infoPage = -1;
    QLabel* m_info = nullptr;
    QLabel* m_message = nullptr;
    QLabel* m_lockNotice = nullptr;

    QComboBox* m_gateType = nullptr;
    QLabel* m_gateTypeLabel = nullptr;
    QSpinBox* m_gateInputs = nullptr;

    QButtonGroup* m_regType = nullptr;
    QButtonGroup* m_regTrigger = nullptr;
    QCheckBox* m_regEnable = nullptr;
    QLabel* m_regPins = nullptr;

    QSpinBox* m_muxInputs = nullptr;
    QLabel* m_muxInfo = nullptr;

    QButtonGroup* m_sourceKind = nullptr;
    QStackedWidget* m_sourcePages = nullptr;
    QLineEdit* m_wavePattern = nullptr;
    WaveformEditor* m_waveEditor = nullptr;
    QLineEdit* m_numbers = nullptr;
    QSpinBox* m_sourceBits = nullptr;

    QSpinBox* m_displayInputs = nullptr;
    QSpinBox* m_displayOutputs = nullptr;
};
