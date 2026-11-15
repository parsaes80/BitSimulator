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
#include <QPlainTextEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <expected>
#include <limits>

namespace {

template <typename... Ts>
struct Overloaded : Ts... { using Ts::operator()...; };

//===================== Source value text <-> numbers ========================

// One number: decimal, 0x hex or 0b binary; "_" may group digits (0b1010_0101). Up to 64 bits.
std::expected<std::uint64_t, QString> parseNumber(const QString& input)
{
    QString text = input.trimmed().toLower();
    text.remove(u'_');
    int base = 10;
    if (text.startsWith(QStringLiteral("0x"))) { base = 16; text = text.mid(2); }
    else if (text.startsWith(QStringLiteral("0b"))) { base = 2; text = text.mid(2); }

    bool ok = false;
    const std::uint64_t value = text.toULongLong(&ok, base);
    if (!ok || text.isEmpty() || text.startsWith(u'+') || text.startsWith(u'-'))
        return std::unexpected(PropertiesPanel::tr("\"%1\" is not a whole number of 0 or more (decimal, 0x hex or 0b binary, up to 64 bits).").arg(input.trimmed()));
    return value;
}

// A list of numbers separated by commas, semicolons, spaces or new lines.
std::expected<QList<std::uint64_t>, QString> parseNumbers(const QString& text)
{
    static const QRegularExpression separators(QStringLiteral("[,;\\s]+"));
    QList<std::uint64_t> values;
    for (const QString& part : text.split(separators, Qt::SkipEmptyParts)) {
        const auto value = parseNumber(part);
        if (!value) return std::unexpected(value.error());
        values.append(*value);
        if (values.size() > SourceLimits::maxSteps)
            return std::unexpected(PropertiesPanel::tr("A list can have at most %1 values.").arg(SourceLimits::maxSteps));
    }
    if (values.isEmpty()) return std::unexpected(PropertiesPanel::tr("Enter at least one value."));
    return values;
}

std::expected<QList<bool>, QString> parseWaveform(const QString& text)
{
    QList<bool> values;
    for (const QChar c : text) {
        if (c == u'0') values.append(false);
        else if (c == u'1') values.append(true);
        else if (c.isSpace() || c == u',' || c == u'-' || c == u'_') continue;      // separators are allowed
        else return std::unexpected(PropertiesPanel::tr("A waveform can only contain 0 and 1."));
    }
    if (values.isEmpty()) return std::unexpected(PropertiesPanel::tr("Enter at least one step."));
    if (values.size() > SourceLimits::maxSteps)
        return std::unexpected(PropertiesPanel::tr("A waveform can have at most %1 steps.").arg(SourceLimits::maxSteps));
    return values;
}

QString formatWaveform(const QList<bool>& values)
{
    QString text;
    text.reserve(values.size());
    for (const bool value : values) text += value ? u'1' : u'0';
    return text;
}

QString formatNumbers(const QList<std::uint64_t>& values)
{
    QStringList parts;
    parts.reserve(values.size());
    for (const std::uint64_t value : values) parts << QString::number(value);
    return parts.join(QStringLiteral(", "));
}

// Shows a number in a line edit unless the text there already means it (so typing "0x1F" is not rewritten)
void showNumber(QLineEdit* edit, std::uint64_t value)
{
    const auto shown = parseNumber(edit->text());
    if (!shown || *shown != value) edit->setText(QString::number(value));
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

int WaveformEditor::shownSteps() const
{
    return std::min(static_cast<int>(m_values.size()), MaxShownSteps);
}

int WaveformEditor::rows() const
{
    return std::max(1, (shownSteps() + WaveColumns - 1) / WaveColumns);
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

    for (int i = 0; i < shownSteps(); i++) {
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
    if (column >= WaveColumns || index < 0 || index >= shownSteps()) return;

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
    addPage(itemKind<BlockProps>, buildBlockPage());
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
        // A source or display of compiled code stands for one of the design's ports: name it
        const auto* source = dynamic_cast<SourceItem*>(m_item.data());
        const auto* display = dynamic_cast<DisplayItem*>(m_item.data());
        if (source && !source->name().isEmpty()) m_subtitle->setText(tr("Input “%1” of the design").arg(source->name()));
        else if (display && !display->name().isEmpty()) m_subtitle->setText(tr("Output “%1” of the design").arg(display->name()));
        else m_subtitle->setText(tr("Selected component"));
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
        [this](const BlockProps& p)    { loadBlock(p); },
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
    case RType::D:  pins << QStringLiteral("D"); break;
    case RType::T:  pins << QStringLiteral("T"); break;
    }
    if (props.type == RType::D || props.type == RType::T) {
        // Compiled Verilog can reset asynchronously, and to 1
        QString reset = props.asyncReset ? tr("asynchronous reset") : tr("reset");
        if (props.resetValue) reset += tr(" to 1");
        pins << reset;
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

namespace {

// Which page of settings a source mode uses (counter, Gray code and walking one share one)
enum SourcePage { SequencePage, RandomPage, ListPage, WaveformsPage, ConstantPage };

SourcePage sourcePage(SourceMode mode)
{
    switch (mode) {
    case SourceMode::Counter:
    case SourceMode::GrayCode:
    case SourceMode::WalkingOne: return SequencePage;
    case SourceMode::Random:     return RandomPage;
    case SourceMode::List:       return ListPage;
    case SourceMode::Waveforms:  return WaveformsPage;
    case SourceMode::Constant:   return ConstantPage;
    }
    return SequencePage;
}

QString sourceModeInfo(SourceMode mode)
{
    switch (mode) {
    case SourceMode::Counter:
        return PropertiesPanel::tr("Counts from Start by Step, up or down. It wraps around at the number of bits: with 8 bits, 255 is followed by 0.");
    case SourceMode::GrayCode:
        return PropertiesPanel::tr("Counts from Start in Gray code: exactly one pin changes per step. It wraps around at the number of bits.");
    case SourceMode::WalkingOne:
        return PropertiesPanel::tr("A single 1 moves one pin per step. Up: 1, 2, 4, 8, ... (bit 0, the top pin, first). Down: the other way.");
    case SourceMode::Random:
        return PropertiesPanel::tr("A random number over all bits at every step. The same seed gives the same numbers on every run.");
    case SourceMode::List:
        return PropertiesPanel::tr("One value per step, then the list repeats (without Repeat: stays on the last value). Separate values with commas, spaces or new lines; write 0x... for hex and 0b... for binary.");
    case SourceMode::Waveforms:
        return PropertiesPanel::tr("Each pin plays its own pattern of 0s and 1s, one step at a time, and starts it again (without Repeat: stays on its last step). The patterns can have different lengths.");
    case SourceMode::Constant:
        return PropertiesPanel::tr("Always outputs this value. Write 0x... for hex and 0b... for binary.");
    }
    return {};
}

// The first values the source outputs, so the effect of each setting can be seen right away
QString sourcePreview(const SourceProps& props)
{
    if (props.mode == SourceMode::Constant)
        return PropertiesPanel::tr("Outputs %1").arg(sourceValueAt(props, 0));

    constexpr int shownSteps = 8;
    QStringList values;
    for (int step = 0; step < shownSteps; step++) {
        values << QString::number(sourceValueAt(props, std::uint64_t(step) * std::uint64_t(props.ticksPerStep)));
    }
    return PropertiesPanel::tr("Steps: %1, ...").arg(values.join(QStringLiteral(", ")));
}

} // namespace

QWidget* PropertiesPanel::buildSourcePage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // What every mode has: the mode itself, the number of output pins, and how long each value lasts
    auto* common = new QWidget;
    m_sourceForm = makeForm(common);
    m_sourceMode = new QComboBox;
    m_sourceMode->setObjectName(QStringLiteral("sourceMode"));
    for (const SourceMode mode : allSourceModes) m_sourceMode->addItem(sourceModeName(mode), static_cast<int>(mode));
    m_sourceBits = makeSpin(1, SourceLimits::maxBits);
    m_sourceBits->setObjectName(QStringLiteral("sourceBits"));
    m_sourceBits->setToolTip(tr("Output pins; each pin is one bit, lowest bit on top"));
    m_sourceEvery = makeSpin(1, SourceLimits::maxTicksPerStep);
    m_sourceEvery->setObjectName(QStringLiteral("sourceEvery"));
    m_sourceEvery->setToolTip(tr("A new value every this many simulation ticks"));
    m_sourceForm->addRow(tr("Mode"), m_sourceMode);
    m_sourceForm->addRow(tr("Bits"), m_sourceBits);
    m_sourceRepeat = new QCheckBox(tr("Repeat"));
    m_sourceRepeat->setObjectName(QStringLiteral("sourceRepeat"));
    m_sourceRepeat->setToolTip(tr("Start again after the last step. Off: stay on the last value, e.g. a reset pulse 1, 1, 0."));
    m_sourceForm->addRow(tr("Every"), m_sourceEvery);
    m_sourceForm->addRow(QString(), m_sourceRepeat);
    layout->addWidget(common);

    // Counter, Gray code and walking one
    auto* sequencePage = new QWidget;
    m_sequenceForm = makeForm(sequencePage);
    const auto direction = makeSegmented({{0, tr("Up")}, {1, tr("Down")}});
    m_sourceDirection = direction.group;
    m_sourceStart = new QLineEdit;
    m_sourceStart->setObjectName(QStringLiteral("sourceStart"));
    m_sourceStep = new QLineEdit;
    m_sourceStep->setObjectName(QStringLiteral("sourceStep"));
    m_sequenceForm->addRow(tr("Direction"), direction.widget);
    m_sequenceForm->addRow(tr("Start"), m_sourceStart);
    m_sequenceForm->addRow(tr("Step"), m_sourceStep);

    // Random
    auto* randomPage = new QWidget;
    auto* randomForm = makeForm(randomPage);
    m_sourceSeed = makeSpin(0, std::numeric_limits<int>::max());
    m_sourceSeed->setObjectName(QStringLiteral("sourceSeed"));
    randomForm->addRow(tr("Seed"), m_sourceSeed);

    // List
    auto* listPage = new QWidget;
    auto* listLayout = new QVBoxLayout(listPage);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(4);
    m_sourceList = new QPlainTextEdit;
    m_sourceList->setObjectName(QStringLiteral("sourceValues"));
    m_sourceList->setPlaceholderText(QStringLiteral("2, 5"));
    m_sourceList->setTabChangesFocus(true);
    m_sourceList->setFixedHeight(96);
    m_sourceListCount = mutedLabel();
    listLayout->addWidget(m_sourceList);
    listLayout->addWidget(m_sourceListCount);

    // Waveforms: one pattern row per pin, and a click editor for the chosen pin
    auto* wavePage = new QWidget;
    auto* waveLayout = new QVBoxLayout(wavePage);
    waveLayout->setContentsMargins(0, 0, 0, 0);
    waveLayout->setSpacing(6);
    m_wavePins = new QWidget;
    auto* pinsLayout = new QVBoxLayout(m_wavePins);
    pinsLayout->setContentsMargins(0, 0, 0, 0);
    pinsLayout->setSpacing(4);
    m_wavePinsScroll = new QScrollArea;
    m_wavePinsScroll->setWidget(m_wavePins);
    m_wavePinsScroll->setWidgetResizable(true);
    m_wavePinsScroll->setFrameShape(QFrame::NoFrame);
    m_wavePinsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_wavePinGroup = new QButtonGroup(this);
    m_wavePinGroup->setExclusive(true);
    m_waveEditorLabel = mutedLabel();
    m_waveEditor = new WaveformEditor;
    m_waveEditor->setObjectName(QStringLiteral("sourceWaveform"));
    waveLayout->addWidget(m_wavePinsScroll);
    waveLayout->addWidget(m_waveEditorLabel);
    waveLayout->addWidget(m_waveEditor);

    // Constant
    auto* constantPage = new QWidget;
    auto* constantForm = makeForm(constantPage);
    m_sourceConstant = new QLineEdit;
    m_sourceConstant->setObjectName(QStringLiteral("sourceConstant"));
    constantForm->addRow(tr("Value"), m_sourceConstant);

    m_sourcePages = {sequencePage, randomPage, listPage, wavePage, constantPage};   // in SourcePage order
    for (QWidget* settings : std::as_const(m_sourcePages)) layout->addWidget(settings);

    m_sourcePreview = mutedLabel();
    m_sourcePreview->setObjectName(QStringLiteral("sourcePreview"));
    m_sourceInfo = mutedLabel();
    layout->addWidget(m_sourcePreview);
    layout->addWidget(m_sourceInfo);

    // A number field: apply the number when it parses, otherwise explain and keep the last valid one
    const auto connectNumber = [this](QLineEdit* field, std::uint64_t SourceProps::* member) {
        connect(field, &QLineEdit::textEdited, this, [this, member](const QString& text) {
            if (const auto value = parseNumber(text)) {
                edit<SourceProps>([&](SourceProps& p) { p.*member = *value; });
            } else {
                showMessage(value.error());
            }
        });
    };
    connectNumber(m_sourceStart, &SourceProps::start);
    connectNumber(m_sourceStep, &SourceProps::step);
    connectNumber(m_sourceConstant, &SourceProps::constant);

    connect(m_sourceMode, &QComboBox::activated, this, [this](int index) {
        const auto mode = static_cast<SourceMode>(m_sourceMode->itemData(index).toInt());
        edit<SourceProps>([mode](SourceProps& p) { p.mode = mode; });
    });
    connect(m_sourceBits, &QSpinBox::valueChanged, this, [this](int value) {
        edit<SourceProps>([value](SourceProps& p) { p.bits = value; });
    });
    connect(m_sourceEvery, &QSpinBox::valueChanged, this, [this](int value) {
        m_sourceEvery->setSuffix(value == 1 ? tr(" tick") : tr(" ticks"));
        edit<SourceProps>([value](SourceProps& p) { p.ticksPerStep = value; });
    });
    connect(m_sourceRepeat, &QCheckBox::clicked, this, [this](bool checked) {
        edit<SourceProps>([checked](SourceProps& p) { p.repeat = checked; });
    });
    connect(m_sourceDirection, &QButtonGroup::idClicked, this, [this](int id) {
        edit<SourceProps>([id](SourceProps& p) { p.countDown = (id == 1); });
    });
    connect(m_sourceSeed, &QSpinBox::valueChanged, this, [this](int value) {
        edit<SourceProps>([value](SourceProps& p) { p.seed = static_cast<std::uint32_t>(value); });
    });
    connect(m_sourceList, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loading) return;
        if (const auto values = parseNumbers(m_sourceList->toPlainText())) {
            edit<SourceProps>([&values](SourceProps& p) { p.values = *values; });
        } else {
            showMessage(values.error());
        }
    });
    connect(m_wavePinGroup, &QButtonGroup::idClicked, this, &PropertiesPanel::selectWavePin);
    connect(m_waveEditor, &WaveformEditor::valuesEdited, this, [this](const QList<bool>& values) {
        edit<SourceProps>([this, &values](SourceProps& p) {
            if (m_wavePin < p.waves.size()) p.waves[m_wavePin] = values;
        });
    });
    return page;
}

void PropertiesPanel::setWavePinCount(int count)
{
    auto* pinsLayout = static_cast<QVBoxLayout*>(m_wavePins->layout());
    while (m_wavePatterns.size() < count) {
        const int pin = static_cast<int>(m_wavePatterns.size());
        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        auto* pinButton = new QToolButton;
        pinButton->setText(QString::number(pin));
        pinButton->setCheckable(true);
        pinButton->setFixedWidth(34);
        pinButton->setObjectName(QStringLiteral("sourcePin%1").arg(pin));
        pinButton->setProperty("wavePin", true);          // styled by the QToolButton[wavePin="true"] rule in BitSimulator.ui
        pinButton->setToolTip(tr("Pin %1 (bit %1): edit its pattern with the steps below").arg(pin));
        m_wavePinGroup->addButton(pinButton, pin);

        auto* pattern = new QLineEdit;
        pattern->setObjectName(QStringLiteral("sourcePattern%1").arg(pin));
        pattern->setPlaceholderText(QStringLiteral("0101"));
        connect(pattern, &QLineEdit::textEdited, this, [this, pin](const QString& text) {
            selectWavePin(pin);
            if (const auto wave = parseWaveform(text)) {
                edit<SourceProps>([pin, &wave](SourceProps& p) {
                    if (pin < p.waves.size()) p.waves[pin] = *wave;
                });
            } else {
                showMessage(wave.error());       // keep the last valid pattern until the text is fixed
            }
        });

        rowLayout->addWidget(pinButton);
        rowLayout->addWidget(pattern, 1);
        pinsLayout->addWidget(row);
        m_wavePatterns.append(pattern);
    }
    while (m_wavePatterns.size() > count) {
        delete m_wavePatterns.takeLast()->parentWidget();   // the row, with its pin button
    }

    // As tall as the rows, up to about six of them; more rows scroll
    m_wavePins->adjustSize();
    m_wavePinsScroll->setFixedHeight(std::min(m_wavePins->sizeHint().height(), 180));
}

void PropertiesPanel::selectWavePin(int pin)
{
    const auto* props = std::get_if<SourceProps>(&m_shown);
    if (!props) return;
    m_wavePin = std::clamp(pin, 0, std::max(props->bits - 1, 0));
    if (QAbstractButton* button = m_wavePinGroup->button(m_wavePin)) button->setChecked(true);

    const QList<bool> wave = props->waves.value(m_wavePin);
    m_waveEditor->setValues(wave);
    m_waveEditorLabel->setText(wave.size() > WaveformEditor::MaxShownSteps
        ? tr("Pin %1: click a step to flip it. The first %2 of %3 steps are shown; type the rest in its pattern.")
              .arg(m_wavePin).arg(WaveformEditor::MaxShownSteps).arg(wave.size())
        : tr("Pin %1: click a step to flip it.").arg(m_wavePin));
}

void PropertiesPanel::loadSource(const SourceProps& props)
{
    {
        const QSignalBlocker blocker(m_sourceMode);
        m_sourceMode->setCurrentIndex(m_sourceMode->findData(static_cast<int>(props.mode)));
    }

    // A list or constant needs enough pins for its largest value
    int minBits = 1;
    if (props.mode == SourceMode::List) minBits = bitsNeeded(props.values);
    if (props.mode == SourceMode::Constant) minBits = bitsNeeded(props.constant);
    {
        const QSignalBlocker blocker(m_sourceBits);
        m_sourceBits->setRange(minBits, SourceLimits::maxBits);
    }
    showValue(m_sourceBits, props.bits);
    showValue(m_sourceEvery, props.ticksPerStep);
    m_sourceEvery->setSuffix(props.ticksPerStep == 1 ? tr(" tick") : tr(" ticks"));
    m_sourceForm->setRowVisible(m_sourceEvery, props.mode != SourceMode::Constant);
    m_sourceRepeat->setChecked(props.repeat);
    m_sourceForm->setRowVisible(m_sourceRepeat, props.mode == SourceMode::List || props.mode == SourceMode::Waveforms);

    for (qsizetype page = 0; page < m_sourcePages.size(); page++) m_sourcePages[page]->setVisible(page == sourcePage(props.mode));

    // Counter, Gray code, walking one
    check(m_sourceDirection, props.countDown ? 1 : 0);
    showNumber(m_sourceStart, props.start);
    showNumber(m_sourceStep, props.step);
    m_sequenceForm->setRowVisible(m_sourceStart, props.mode != SourceMode::WalkingOne);
    m_sequenceForm->setRowVisible(m_sourceStep, props.mode == SourceMode::Counter);

    showValue(m_sourceSeed, static_cast<int>(std::min<std::uint32_t>(props.seed, std::numeric_limits<int>::max())));

    // List: leave the text alone while it already means the same list, so typing is not rewritten
    if (parseNumbers(m_sourceList->toPlainText()) != props.values) {
        const QSignalBlocker blocker(m_sourceList);
        m_sourceList->setPlainText(formatNumbers(props.values));
    }
    m_sourceListCount->setText(props.values.size() == 1 ? tr("1 value") : tr("%1 values").arg(props.values.size()));

    // Waveforms: a pattern row per pin
    setWavePinCount(props.bits);
    for (int pin = 0; pin < props.bits; pin++) {
        const QList<bool> wave = props.waves.value(pin);
        if (parseWaveform(m_wavePatterns[pin]->text()) != wave) m_wavePatterns[pin]->setText(formatWaveform(wave));
    }
    selectWavePin(m_wavePin);

    showNumber(m_sourceConstant, props.constant);

    m_sourcePreview->setText(sourcePreview(props));
    m_sourceInfo->setText(sourceModeInfo(props.mode));
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

//===================== Block ========================

QWidget* PropertiesPanel::buildBlockPage()
{
    auto* page = new QWidget;
    auto* form = makeForm(page);

    const auto wrapped = [] {
        auto* label = new QLabel;
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };
    m_blockModule = wrapped();
    m_blockModule->setObjectName(QStringLiteral("blockModule"));
    m_blockInputs = wrapped();
    m_blockInputs->setObjectName(QStringLiteral("blockInputs"));
    m_blockOutputs = wrapped();
    m_blockOutputs->setObjectName(QStringLiteral("blockOutputs"));

    form->addRow(tr("Module"), m_blockModule);
    form->addRow(tr("Inputs"), m_blockInputs);
    form->addRow(tr("Outputs"), m_blockOutputs);
    form->addRow(mutedLabel(tr("A submodule of the compiled design. Double-click it to see inside; "
                               "Esc or the path at the top left goes back. Its inside can be viewed "
                               "and simulated, not edited.")));
    return page;
}

void PropertiesPanel::loadBlock(const BlockProps& props)
{
    QStringList inputs, outputs;
    for (const BlockPort& port : props.ports) {
        const QString name = port.width > 1 ? QStringLiteral("%1[%2]").arg(port.name).arg(port.width) : port.name;
        (port.output ? outputs : inputs).append(name);
    }
    m_blockModule->setText(props.module);
    m_blockInputs->setText(inputs.isEmpty() ? tr("none") : inputs.join(QStringLiteral(", ")));
    m_blockOutputs->setText(outputs.isEmpty() ? tr("none") : outputs.join(QStringLiteral(", ")));
}
