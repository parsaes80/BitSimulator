#pragma once

#include <QFrame>
#include <QList>
#include <QPoint>
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
class QToolButton;

// Click a step to flip it; past MaxShownSteps the pattern is edited as text
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
    void valuesEdited(const QList<bool>& values);   // clicks only, not setValues()

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    int rows() const;

    QList<bool> m_values;
};

// Edits the selected component, or with nothing selected the active tool's defaults
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(CircuitScene* scene, QWidget* parent = nullptr);

    // Running: editors disabled; Paused: a notice says an edit ends the run
    void showSimState(SimState state);

private:
    void refresh();
    void load(const ItemProps& props);
    void commit(const ItemProps& props);    // to the selected component or the tool defaults
    void showMessage(const QString& text);

    // Applies `change` to the properties on screen
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
    void setWavePinCount(int count);
    void selectWavePin(int pin);            // the pin whose pattern the editor shows
    void loadDisplay(const DisplayProps& props);
    void loadBlock(const BlockProps& props);

    CircuitScene* m_scene;
    QPointer<ComponentItem> m_item;         // null: editing tool defaults
    ItemProps m_shown;
    bool m_loading = false;                 // so filling the editors doesn't commit
    bool m_locked = false;

    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QWidget* m_nextSection = nullptr;       // shown when nothing is selected
    QLabel* m_nextTitle = nullptr;
    QLabel* m_placeHint = nullptr;
    QStackedWidget* m_pages = nullptr;
    std::array<QWidget*, std::variant_size_v<ItemProps>> m_kindPages{};   // by itemKind
    QLabel* m_info = nullptr;
    QLabel* m_message = nullptr;
    QLabel* m_lockNotice = nullptr;
    QWidget* m_nameRow = nullptr;            // sources and displays only
    QLineEdit* m_name = nullptr;
    void renameSelected();

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

    QComboBox* m_sourceMode = nullptr;
    QFormLayout* m_sourceForm = nullptr;
    QSpinBox* m_sourceBits = nullptr;
    QSpinBox* m_sourceEvery = nullptr;      // N ticks per value
    QCheckBox* m_sourceRepeat = nullptr;    // List, Waveforms: repeat or hold the last value
    QList<QWidget*> m_sourcePages;          // only the current one is shown, so the panel fits it
    QFormLayout* m_sequenceForm = nullptr;  // counter, Gray code, walking one
    QButtonGroup* m_sourceDirection = nullptr;
    QLineEdit* m_sourceStart = nullptr;
    QLineEdit* m_sourceStep = nullptr;
    QSpinBox* m_sourceSeed = nullptr;
    QPlainTextEdit* m_sourceList = nullptr;
    QLabel* m_sourceListCount = nullptr;
    QWidget* m_wavePins = nullptr;          // per pin: button + pattern
    QScrollArea* m_wavePinsScroll = nullptr;
    QList<QLineEdit*> m_wavePatterns;
    QButtonGroup* m_wavePinGroup = nullptr;
    int m_wavePin = 0;
    QLabel* m_waveEditorLabel = nullptr;
    WaveformEditor* m_waveEditor = nullptr;
    QLineEdit* m_sourceConstant = nullptr;
    QLabel* m_sourcePreview = nullptr;      // the first few values
    QLabel* m_sourceInfo = nullptr;

    QSpinBox* m_displayInputs = nullptr;
    QSpinBox* m_displayOutputs = nullptr;
};

// A box over the canvas: drag its title to move it, ▴/▾ or double-click minimizes; stays inside
class FloatingPanel : public QFrame {
    Q_OBJECT
public:
    // `area` (the canvas viewport) must be a child of `parent`
    FloatingPanel(const QString& title, QWidget* content, QWidget* area, QWidget* parent);

    bool isMinimized() const { return m_minimized; }
    void setMinimized(bool minimized);

    QWidget* titleBar() const { return m_titleBar; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;   // title drags, area resizes

private:
    void place();                                     // from the stored position and the area's size
    void moveTo(QPoint topLeft);                      // within the area
    int minimizedHeight() const;
    int openMinimumHeight() const;

    QWidget* m_area;
    QWidget* m_titleBar;
    QToolButton* m_minimizeButton;
    QWidget* m_body;                                  // hidden when minimized
    bool m_minimized = false;

    // Distance from the area's top and from its nearer side
    bool m_fromRight = true;
    QPoint m_offset{12, 12};

    bool m_dragging = false;
    QPointF m_dragStartGlobal;
    QPoint m_dragStartPos;                            // in the area
};
