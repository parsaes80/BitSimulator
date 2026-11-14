#include "PropertiesPanel.h"
#include "CircuitCanvas.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <expected>

namespace {

template <typename... Ts>
struct Overloaded : Ts... { using Ts::operator()...; };

//===================== Source value text <-> lists ========================

std::expected<QList<bool>, QString> parseWaveform(const QString& text)
{
    QList<bool> values;
    for (const QChar c : text) {
        if (c == u'0') values.append(false);
        else if (c == u'1') values.append(true);
        else if (c.isSpace() || c == u',' || c == u'-') continue;      // separators are allowed
        else return std::unexpected(PropertiesPanel::tr("A waveform can only contain 0 and 1."));
    }
    if (values.isEmpty()) return std::unexpected(PropertiesPanel::tr("Enter at least one step."));
    if (values.size() > ItemLimits::maxSourceSteps)
        return std::unexpected(PropertiesPanel::tr("A source can have at most %1 steps.").arg(ItemLimits::maxSourceSteps));
    return values;
}

std::expected<QList<int>, QString> parseNumbers(const QString& text)
{
    static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
    const int largest = (1 << ItemLimits::maxSourceBits) - 1;

    QList<int> values;
    for (const QString& part : text.split(separators, Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = part.toInt(&ok);
        if (!ok || value < 0)
            return std::unexpected(PropertiesPanel::tr("\"%1\" is not a whole number of 0 or more.").arg(part));
        if (value > largest)
            return std::unexpected(PropertiesPanel::tr("Values above %1 are not supported.").arg(largest));
        values.append(value);
    }
    if (values.isEmpty()) return std::unexpected(PropertiesPanel::tr("Enter at least one value."));
    if (values.size() > ItemLimits::maxSourceSteps)
        return std::unexpected(PropertiesPanel::tr("A source can have at most %1 steps.").arg(ItemLimits::maxSourceSteps));
    return values;
}

QString formatWaveform(const QList<bool>& values)
{
    QString text;
    text.reserve(values.size());
    for (const bool value : values) text += value ? u'1' : u'0';
    return text;
}

QString formatNumbers(const QList<int>& values)
{
    QStringList parts;
    for (const int value : values) parts << QString::number(value);
    return parts.join(QStringLiteral(", "));
}

//===================== Small widget helpers ========================

QLabel* mutedLabel(const QString& text = {})
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setProperty("muted", true);     // colored by the QLabel[muted="true"] rule in BitSimulator.ui
    return label;
}

QSpinBox* makeSpin(int minimum, int maximum)
{
    auto* spin = new QSpinBox;
    spin->setRange(minimum, maximum);
    spin->setKeyboardTracking(false);     // typing "12" should not apply "1" first
    // Qt ignores a spin box's style sheet padding when it places the text, so indent it here
    if (auto* edit = spin->findChild<QLineEdit*>()) edit->setTextMargins(6, 0, 0, 0);
    return spin;
}

// Shows a value without triggering the spin box's valueChanged signal.
void showValue(QSpinBox* spin, int value)
{
    const QSignalBlocker blocker(spin);
    if (value > spin->maximum()) spin->setMaximum(value);    // e.g. a wide display made by the HDL compiler
    spin->setValue(value);
}

QFormLayout* makeForm(QWidget* page)
{
    auto* form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
}

// A row of joined buttons where exactly one is chosen (like tabs). The group reports clicks via idClicked.
struct Segmented {
    QWidget* widget;
    QButtonGroup* group;
};

Segmented makeSegmented(const QList<QPair<int, QString>>& options)
{
    auto* widget = new QWidget;
    widget->setObjectName(QStringLiteral("segmented"));
    auto* row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    auto* group = new QButtonGroup(widget);
    group->setExclusive(true);
    for (int i = 0; i < options.size(); i++) {
        auto* button = new QToolButton;
        button->setText(options[i].second);
        button->setCheckable(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setProperty("segment", i == 0 ? "first" : (i == options.size() - 1 ? "last" : "middle"));
        group->addButton(button, options[i].first);
        row->addWidget(button);
    }
    return {widget, group};
}

void check(QButtonGroup* group, int id)
{
    if (auto* button = group->button(id)) button->setChecked(true);
}

// A QStackedWidget is normally as tall as its tallest page. Ignoring the size of the hidden
// pages makes it exactly as tall as the page being shown, so what follows sits right below it.
void showPage(QStackedWidget* stack, int index)
{
    for (int i = 0; i < stack->count(); i++) {
        stack->widget(i)->setSizePolicy(QSizePolicy::Preferred, i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    }
    stack->setCurrentIndex(index);
}

} // namespace

//===================== WaveformEditor ========================

namespace {
constexpr int WaveColumns = 8;       // one byte per row
constexpr int WaveCellWidth = 24;
constexpr int WaveRowHeight = 40;
}

WaveformEditor::WaveformEditor(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click a step to flip it"));
}

void WaveformEditor::setValues(const QList<bool>& values)
{
    if (m_values == values) return;
    const int oldRows = rows();
    m_values = values;
    if (rows() != oldRows) updateGeometry();
    update();
}

int WaveformEditor::rows() const
{
    return std::max(1, static_cast<int>((m_values.size() + WaveColumns - 1) / WaveColumns));
}

QSize WaveformEditor::sizeHint() const
{
    return {WaveColumns * WaveCellWidth + 1, rows() * WaveRowHeight};
}

void WaveformEditor::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    if (!isEnabled()) painter.setOpacity(0.45);

    // The same colors as a wire on the canvas: a 1 is drawn like a wire carrying one
    const CanvasTheme& theme = canvasTheme();
    const QColor accent = theme.wireHigh;
    const QColor grid = theme.wireLow;
    QColor fill = accent;
    fill.setAlpha(45);

    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), theme.background);

    for (int i = 0; i < m_values.size(); i++) {
        const int column = i % WaveColumns;
        const int x = column * WaveCellWidth;
        const int y = (i / WaveColumns) * WaveRowHeight;
        const int highY = y + 7, lowY = y + 23;
        const bool value = m_values[i];

        painter.setPen(QPen(grid, 1, Qt::DotLine));
        painter.drawLine(x, y + 2, x, y + WaveRowHeight - 3);

        if (value) painter.fillRect(x + 1, highY, WaveCellWidth - 1, lowY - highY, fill);

        painter.setPen(QPen(accent, 2));
        const int levelY = value ? highY : lowY;
        painter.drawLine(x, levelY, x + WaveCellWidth, levelY);
        if (column > 0 && m_values[i - 1] != value) painter.drawLine(x, highY, x, lowY);   // rising or falling edge

        painter.setPen(value ? theme.componentText : theme.pin);
        painter.drawText(QRect(x, lowY + 2, WaveCellWidth, WaveRowHeight - 25), Qt::AlignCenter, value ? QStringLiteral("1") : QStringLiteral("0"));
    }
}

void WaveformEditor::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;

    const QPoint pos = event->position().toPoint();
    const int column = pos.x() / WaveCellWidth;
    const int index = (pos.y() / WaveRowHeight) * WaveColumns + column;
    if (column >= WaveColumns || index < 0 || index >= m_values.size()) return;

    m_values[index] = !m_values[index];
    update();
    emit valuesEdited(m_values);
}

//===================== PropertiesPanel ========================

PropertiesPanel::PropertiesPanel(CircuitScene* scene, QWidget* parent)
    : QWidget(parent), m_scene(scene)
{
    setObjectName(QStringLiteral("propertiesPanel"));
    setMinimumWidth(160);
    setAttribute(Qt::WA_StyledBackground);   // so the style sheet in BitSimulator.ui can paint the background

    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("propertiesTitle"));
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
    m_title->setFont(titleFont);
    m_subtitle = mutedLabel();

    // With nothing selected the editors below describe the next component to place, under their own heading
    m_nextSection = new QWidget;
    auto* nextLayout = new QVBoxLayout(m_nextSection);
    nextLayout->setContentsMargins(0, 0, 0, 2);
    nextLayout->setSpacing(2);
    auto* nextCaption = mutedLabel(tr("NEXT COMPONENT"));
    QFont captionFont = nextCaption->font();
    captionFont.setPointSizeF(captionFont.pointSizeF() * 0.85);
    captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    nextCaption->setFont(captionFont);
    m_nextTitle = new QLabel;
    m_nextTitle->setObjectName(QStringLiteral("propertiesNextTitle"));
    QFont nextFont = m_nextTitle->font();
    nextFont.setBold(true);
    m_nextTitle->setFont(nextFont);
    nextLayout->addWidget(nextCaption);
    nextLayout->addWidget(m_nextTitle);
    m_placeHint = mutedLabel(tr("Middle-click an empty spot on the canvas to place it."));

    auto* separator = new QFrame;
    separator->setObjectName(QStringLiteral("propertiesSeparator"));
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);

    // One page per alternative of ItemProps, added in the same order, so a page index equals props.index()
    m_pages = new QStackedWidget;
    m_pages->setObjectName(QStringLiteral("propertiesPages"));
    addPage(itemKind<GateProps>, buildGatePage());
    addPage(itemKind<RegisterProps>, buildRegisterPage());
    addPage(itemKind<MuxProps>, buildMuxPage());
    addPage(itemKind<SourceProps>, buildSourcePage());
    addPage(itemKind<DisplayProps>, buildDisplayPage());
    m_info = mutedLabel();
    m_infoPage = m_pages->addWidget(m_info);
    m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_message = new QLabel;
    m_message->setObjectName(QStringLiteral("propertiesMessage"));
    m_message->setWordWrap(true);
    m_message->hide();

    m_lockNotice = new QLabel;
    m_lockNotice->setObjectName(QStringLiteral("propertiesLock"));
    m_lockNotice->setWordWrap(true);
    m_lockNotice->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    layout->addWidget(m_title);
    layout->addWidget(m_subtitle);
    layout->addWidget(separator);
    layout->addSpacing(2);
    layout->addWidget(m_lockNotice);
    layout->addWidget(m_nextSection);
    layout->addWidget(m_pages);
    layout->addWidget(m_placeHint);
    layout->addWidget(m_message);
    layout->addStretch(1);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &PropertiesPanel::refresh);
    connect(m_scene, &CircuitScene::toolChanged, this, &PropertiesPanel::refresh);
    connect(m_scene, &CircuitScene::simStateChanged, this, &PropertiesPanel::showSimState);
    refresh();
}

void PropertiesPanel::addPage(std::size_t kind, QWidget* page)
{
    const int index = m_pages->addWidget(page);
    Q_ASSERT(index == static_cast<int>(kind));
    Q_UNUSED(index);
    Q_UNUSED(kind);
}

void PropertiesPanel::showSimState(SimState state)
{
    m_locked = (state == SimState::Running);
    m_pages->setEnabled(!m_locked);

    m_lockNotice->setVisible(state != SimState::Stopped);
    m_lockNotice->setText(state == SimState::Running
        ? tr("The simulation is running.\nPress Pause to edit the circuit.")
        : tr("The simulation is paused.\nEditing the circuit ends this run; the next Start begins again from the first tick."));
    if (m_locked) showMessage({});
}

void PropertiesPanel::showMessage(const QString& text)
{
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
}

void PropertiesPanel::refresh()
{
    showMessage({});

    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    QList<ComponentItem*> components;
    for (QGraphicsItem* item : selected) {
        if (auto* component = dynamic_cast<ComponentItem*>(item)) components.append(component);
    }

    // The "next component" heading and placing hint only make sense while nothing is selected
    m_nextSection->setVisible(selected.isEmpty());
    m_placeHint->setVisible(selected.isEmpty());

    if (selected.isEmpty()) {
        // Nothing selected: say so, and offer the settings of what the active tool will place next
        m_item = nullptr;
        m_title->setText(tr("Nothing selected"));
        m_subtitle->setText(tr("Click a component on the canvas to edit it."));
        load(m_scene->toolProps());
    } else if (selected.size() == 1 && components.size() == 1) {
        m_item = components.first();
        m_subtitle->setText(tr("Selected component"));
        load(m_item->props());
    } else {
        m_item = nullptr;
        const bool singleWire = selected.size() == 1;
        m_title->setText(singleWire ? tr("Wire") : tr("%1 items selected").arg(selected.size()));
        m_subtitle->setText(singleWire ? tr("Selected wire") : tr("Selection"));
        m_info->setText(singleWire ? tr("A wire has no properties. Press Delete to remove it.")
                                   : tr("Select a single component to edit it."));
        showPage(m_pages, m_infoPage);
    }
}

void PropertiesPanel::load(const ItemProps& props)
{
    const QScopedValueRollback<bool> loading(m_loading, true);

    m_shown = props;
    if (m_item) m_title->setText(itemTitle(props));     // a selected component is named in the header
    else m_nextTitle->setText(itemTitle(props));        // otherwise the header says "Nothing selected"
    showPage(m_pages, static_cast<int>(props.index()));

    std::visit(Overloaded{
        [this](const GateProps& p)     { loadGate(p); },
        [this](const RegisterProps& p) { loadRegister(p); },
        [this](const MuxProps& p)      { loadMux(p); },
        [this](const SourceProps& p)   { loadSource(p); },
        [this](const DisplayProps& p)  { loadDisplay(p); },
    }, props);
}

void PropertiesPanel::commit(const ItemProps& props)
{
    if (m_loading || m_locked) return;

    if (m_item) {
        const auto result = m_scene->editComponent(m_item, props);
        showMessage(result ? QString() : result.error());
        load(m_item->props());              // shows the applied (or, if refused, the unchanged) state
    } else {
        showMessage({});
        m_scene->setToolProps(props);       // emits toolChanged, which reloads the editors
    }
}

template <typename Props, typename Change>
void PropertiesPanel::edit(Change&& change)
{
    if (m_loading) return;
    const auto* current = std::get_if<Props>(&m_shown);
    if (!current) return;

    Props next = *current;
    change(next);
    commit(next);
}

//===================== Gate ========================

QWidget* PropertiesPanel::buildGatePage()
{
    auto* page = new QWidget;
    auto* form = makeForm(page);

    m_gateType = new QComboBox;
    for (const GType type : {GType::AND, GType::NAND, GType::OR, GType::NOR, GType::XOR, GType::XNOR, GType::NOT}) {
        m_gateType->addItem(gateTypeName(type), static_cast<int>(type));
    }
    m_gateTypeLabel = new QLabel(tr("Type"));
    m_gateType->setObjectName(QStringLiteral("gateType"));
    m_gateInputs = makeSpin(ItemLimits::minGateInputs, ItemLimits::maxGateInputs);
    m_gateInputs->setObjectName(QStringLiteral("gateInputs"));

    form->addRow(m_gateTypeLabel, m_gateType);
    form->addRow(tr("Inputs"), m_gateInputs);

    connect(m_gateType, &QComboBox::activated, this, [this](int index) {
        const auto type = static_cast<GType>(m_gateType->itemData(index).toInt());
        edit<GateProps>([type](GateProps& p) { p.type = type; });
    });
    connect(m_gateInputs, &QSpinBox::valueChanged, this, [this](int value) {
        edit<GateProps>([value](GateProps& p) { p.numInputs = value; });
    });
    return page;
}

void PropertiesPanel::loadGate(const GateProps& props)
{
    // For a new gate the type is picked with the toolbar buttons, so the choice is only shown for placed gates
    const bool placed = !m_item.isNull();
    m_gateTypeLabel->setVisible(placed);
    m_gateType->setVisible(placed);
    m_gateType->setCurrentIndex(m_gateType->findData(static_cast<int>(props.type)));

    const bool isNot = props.type == GType::NOT;
    {
        const QSignalBlocker blocker(m_gateInputs);
        if (isNot) m_gateInputs->setRange(1, 1);
        else m_gateInputs->setRange(ItemLimits::minGateInputs, std::max(ItemLimits::maxGateInputs, props.numInputs));
    }
    showValue(m_gateInputs, props.numInputs);
    m_gateInputs->setEnabled(!isNot);
}

//===================== Register ========================

QWidget* PropertiesPanel::buildRegisterPage()
{
    auto* page = new QWidget;
    auto* form = makeForm(page);

    const auto type = makeSegmented({{static_cast<int>(RType::SR), registerTypeName(RType::SR)},
                                     {static_cast<int>(RType::JK), registerTypeName(RType::JK)},
                                     {static_cast<int>(RType::D), registerTypeName(RType::D)},
                                     {static_cast<int>(RType::T), registerTypeName(RType::T)}});
    const auto trigger = makeSegmented({{1, tr("Flip-flop")}, {0, tr("Latch")}});
    m_regType = type.group;
    m_regTrigger = trigger.group;
    m_regEnable = new QCheckBox(tr("Enable input"));
    m_regEnable->setObjectName(QStringLiteral("registerEnable"));
    m_regPins = mutedLabel();

    trigger.widget->setToolTip(tr("A flip-flop changes on the rising clock edge. A latch has no clock pin and follows its inputs."));

    form->addRow(tr("Type"), type.widget);
    form->addRow(tr("Kind"), trigger.widget);
    form->addRow(QString(), m_regEnable);
    form->addRow(m_regPins);

    connect(m_regType, &QButtonGroup::idClicked, this, [this](int id) {
        edit<RegisterProps>([id](RegisterProps& p) { p.type = static_cast<RType>(id); });
    });
    connect(m_regTrigger, &QButtonGroup::idClicked, this, [this](int id) {
        edit<RegisterProps>([id](RegisterProps& p) { p.edgeTriggered = (id == 1); });
    });
    connect(m_regEnable, &QCheckBox::clicked, this, [this](bool checked) {
        edit<RegisterProps>([checked](RegisterProps& p) { p.hasEnable = checked; });
    });
    return page;
}

void PropertiesPanel::loadRegister(const RegisterProps& props)
{
    check(m_regType, static_cast<int>(props.type));
    check(m_regTrigger, props.edgeTriggered ? 1 : 0);
    m_regEnable->setChecked(props.hasEnable);

    // Name the pins on the left edge in the order they are drawn
    QStringList pins;
    switch (props.type) {
    case RType::SR: pins << QStringLiteral("S") << QStringLiteral("R"); break;
    case RType::JK: pins << QStringLiteral("J") << QStringLiteral("K"); break;
    case RType::D:  pins << QStringLiteral("D") << tr("reset"); break;
    case RType::T:  pins << QStringLiteral("T") << tr("reset"); break;
    }
    if (props.edgeTriggered) pins << tr("clock");
    if (props.hasEnable) pins << tr("enable");
    m_regPins->setText(tr("Left pins, top to bottom: %1").arg(pins.join(QStringLiteral(", "))));
}

//===================== Multiplexer ========================

QWidget* PropertiesPanel::buildMuxPage()
{
    auto* page = new QWidget;
    auto* form = makeForm(page);

    m_muxInputs = makeSpin(ItemLimits::minMuxInputs, ItemLimits::maxMuxInputs);
    m_muxInputs->setObjectName(QStringLiteral("muxInputs"));
    m_muxInfo = mutedLabel();

    form->addRow(tr("Inputs"), m_muxInputs);
    form->addRow(m_muxInfo);

    connect(m_muxInputs, &QSpinBox::valueChanged, this, [this](int value) {
        edit<MuxProps>([value](MuxProps& p) { p.numInputs = value; });
    });
    return page;
}

void PropertiesPanel::loadMux(const MuxProps& props)
{
    showValue(m_muxInputs, props.numInputs);
    const int selectLines = muxSelectLines(props.numInputs);
    m_muxInfo->setText(selectLines == 1 ? tr("1 select line on the bottom edge picks the input.")
                                        : tr("%1 select lines on the bottom edge pick the input.").arg(selectLines));
}

//===================== Source ========================

QWidget* PropertiesPanel::buildSourcePage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    const auto kind = makeSegmented({{0, tr("Waveform")}, {1, tr("Values")}});
    m_sourceKind = kind.group;
    kind.widget->setToolTip(tr("A waveform drives one wire with 0s and 1s. Values drive several wires with whole numbers."));
    layout->addWidget(kind.widget);

    // Waveform: one output pin stepping through a pattern of bits
    auto* wavePage = new QWidget;
    auto* waveForm = makeForm(wavePage);
    m_wavePattern = new QLineEdit;
    m_wavePattern->setPlaceholderText(QStringLiteral("0101"));
    m_wavePattern->setObjectName(QStringLiteral("sourcePattern"));
    m_waveEditor = new WaveformEditor;
    m_waveEditor->setObjectName(QStringLiteral("sourceWaveform"));
    waveForm->addRow(tr("Pattern"), m_wavePattern);
    waveForm->addRow(m_waveEditor);
    waveForm->addRow(mutedLabel(tr("One step per simulation tick, then it repeats. Click a step to flip it.")));

    // Values: several output pins stepping through numbers
    auto* numbersPage = new QWidget;
    auto* numbersForm = makeForm(numbersPage);
    m_numbers = new QLineEdit;
    m_numbers->setPlaceholderText(QStringLiteral("2, 5"));
    m_numbers->setObjectName(QStringLiteral("sourceValues"));
    m_sourceBits = makeSpin(1, ItemLimits::maxSourceBits);
    m_sourceBits->setObjectName(QStringLiteral("sourceBits"));
    numbersForm->addRow(tr("Values"), m_numbers);
    numbersForm->addRow(tr("Bits"), m_sourceBits);
    numbersForm->addRow(mutedLabel(tr("One value per simulation tick, then it repeats. Each bit is one output pin, lowest bit on top.")));

    m_sourcePages = new QStackedWidget;
    m_sourcePages->addWidget(wavePage);
    m_sourcePages->addWidget(numbersPage);
    layout->addWidget(m_sourcePages);

    connect(m_sourceKind, &QButtonGroup::idClicked, this, [this](int id) {
        edit<SourceProps>([this, id](SourceProps& p) {
            if (id == 0) p.values = m_lastWaveform;
            else p.values = m_lastNumbers;
            p.bits = 1;                       // normalized() widens it to fit the values
        });
    });
    connect(m_wavePattern, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (const auto values = parseWaveform(text)) {
            edit<SourceProps>([&values](SourceProps& p) { p.values = *values; });
        } else {
            showMessage(values.error());      // keep the last valid pattern until the text is fixed
        }
    });
    connect(m_waveEditor, &WaveformEditor::valuesEdited, this, [this](const QList<bool>& values) {
        edit<SourceProps>([&values](SourceProps& p) { p.values = values; });
    });
    connect(m_numbers, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (const auto values = parseNumbers(text)) {
            edit<SourceProps>([&values](SourceProps& p) { p.values = *values; });
        } else {
            showMessage(values.error());
        }
    });
    connect(m_sourceBits, &QSpinBox::valueChanged, this, [this](int value) {
        edit<SourceProps>([value](SourceProps& p) { p.bits = value; });
    });
    return page;
}

void PropertiesPanel::loadSource(const SourceProps& props)
{
    if (const auto* wave = std::get_if<QList<bool>>(&props.values)) {
        m_lastWaveform = *wave;
        check(m_sourceKind, 0);
        showPage(m_sourcePages, 0);
        m_waveEditor->setValues(*wave);
        // Leave the text alone while it already means the same thing, so typing "01 01" is not rewritten
        if (parseWaveform(m_wavePattern->text()) != *wave) m_wavePattern->setText(formatWaveform(*wave));
    } else {
        const auto& numbers = std::get<QList<int>>(props.values);
        m_lastNumbers = numbers;
        check(m_sourceKind, 1);
        showPage(m_sourcePages, 1);
        if (parseNumbers(m_numbers->text()) != numbers) m_numbers->setText(formatNumbers(numbers));
        {
            // Fewer pins than the largest value needs is not possible
            const QSignalBlocker blocker(m_sourceBits);
            m_sourceBits->setRange(bitsNeeded(numbers), std::max(ItemLimits::maxSourceBits, props.bits));
        }
        showValue(m_sourceBits, props.bits);
    }
}

//===================== Display ========================

QWidget* PropertiesPanel::buildDisplayPage()
{
    auto* page = new QWidget;
    auto* form = makeForm(page);

    m_displayInputs = makeSpin(ItemLimits::minDisplayInputs, ItemLimits::maxDisplayInputs);
    m_displayInputs->setObjectName(QStringLiteral("displayInputs"));
    m_displayOutputs = makeSpin(ItemLimits::minDisplayOutputs, ItemLimits::maxDisplayOutputs);
    m_displayOutputs->setObjectName(QStringLiteral("displayOutputs"));

    form->addRow(tr("Inputs"), m_displayInputs);
    form->addRow(tr("Outputs"), m_displayOutputs);
    form->addRow(mutedLabel(tr("Shows its inputs as a number, lowest bit on top. Each output passes on the input with the same position.")));

    connect(m_displayInputs, &QSpinBox::valueChanged, this, [this](int value) {
        edit<DisplayProps>([value](DisplayProps& p) { p.numInputs = value; });
    });
    connect(m_displayOutputs, &QSpinBox::valueChanged, this, [this](int value) {
        edit<DisplayProps>([value](DisplayProps& p) { p.numOutputs = value; });
    });
    return page;
}

void PropertiesPanel::loadDisplay(const DisplayProps& props)
{
    showValue(m_displayInputs, props.numInputs);
    showValue(m_displayOutputs, props.numOutputs);
}
