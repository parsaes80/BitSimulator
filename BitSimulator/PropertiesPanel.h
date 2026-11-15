#pragma once

#include <QList>
#include <QPointer>
#include <QWidget>
#include <array>
#include <variant>
#include "Items.h"

class CircuitScene;
enum class SimState;
class ComponentItem;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QFormLayout;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class QSpinBox;
class QStackedWidget;

// A row of steps drawn as a digital waveform; clicking a step flips it between 0 and 1.
// Long patterns show their first MaxShownSteps steps; the rest are edited as text.
class WaveformEditor : public QWidget {
    Q_OBJECT
public:
    explicit WaveformEditor(QWidget* parent = nullptr);

    void setValues(const QList<bool>& values);
    const QList<bool>& values() const { return m_values; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

    static constexpr int MaxShownSteps = 64;
    int shownSteps() const;

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
    QWidget* buildBlockPage();

    void loadGate(const GateProps& props);
    void loadRegister(const RegisterProps& props);
    void loadMux(const MuxProps& props);
    void loadSource(const SourceProps& props);
    void setWavePinCount(int count);        // one pattern row per source pin
    void selectWavePin(int pin);            // the pin whose pattern the click editor shows
    void loadDisplay(const DisplayProps& props);
    void loadBlock(const BlockProps& props);

    CircuitScene* m_scene;
    QPointer<ComponentItem> m_item;         // the selected component, or null when editing tool defaults
    ItemProps m_shown;                      // what the editors currently display
    bool m_loading = false;                 // true while load() fills the editors, so they do not commit
    bool m_locked = false;

    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QWidget* m_nextSection = nullptr;       // heading shown above the editors when nothing is selected
    QLabel* m_nextTitle = nullptr;
    QLabel* m_placeHint = nullptr;
    QStackedWidget* m_pages = nullptr;
    std::array<QWidget*, std::variant_size_v<ItemProps>> m_kindPages{};   // the editor page of each kind, by itemKind
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

    QLabel* m_blockModule = nullptr;
    QLabel* m_blockInputs = nullptr;
    QLabel* m_blockOutputs = nullptr;

    QSpinBox* m_muxInputs = nullptr;
    QLabel* m_muxInfo = nullptr;

    // Source: the settings every mode has, then one page of settings per kind of mode
    QComboBox* m_sourceMode = nullptr;
    QFormLayout* m_sourceForm = nullptr;
    QSpinBox* m_sourceBits = nullptr;
    QSpinBox* m_sourceEvery = nullptr;      // a new value every N ticks
    QCheckBox* m_sourceRepeat = nullptr;    // List and Waveforms: start again, or hold the last value
    QList<QWidget*> m_sourcePages;          // in SourcePage order; only the current one is shown, so
                                            // the panel is exactly as tall as the settings in view
    QFormLayout* m_sequenceForm = nullptr;  // counter, Gray code and walking one share a page
    QButtonGroup* m_sourceDirection = nullptr;
    QLineEdit* m_sourceStart = nullptr;
    QLineEdit* m_sourceStep = nullptr;
    QSpinBox* m_sourceSeed = nullptr;
    QPlainTextEdit* m_sourceList = nullptr;
    QLabel* m_sourceListCount = nullptr;
    QWidget* m_wavePins = nullptr;          // a row per pin: pin button + pattern
    QScrollArea* m_wavePinsScroll = nullptr;
    QList<QLineEdit*> m_wavePatterns;
    QButtonGroup* m_wavePinGroup = nullptr;
    int m_wavePin = 0;
    QLabel* m_waveEditorLabel = nullptr;
    WaveformEditor* m_waveEditor = nullptr;
    QLineEdit* m_sourceConstant = nullptr;
    QLabel* m_sourcePreview = nullptr;      // the first few values, so the effect of a setting is visible
    QLabel* m_sourceInfo = nullptr;

    QSpinBox* m_displayInputs = nullptr;
    QSpinBox* m_displayOutputs = nullptr;
};
