#include "Waveform.h"
#include <QActionGroup>
#include <QRegularExpression>
#include <QIODevice>
#include <QDateTime>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <queue>
#include <functional>
#include <limits>
#include "Theme.h"

using namespace Qt::StringLiterals;

//===================== 1. WaveHistory ========================

void WaveHistory::start(u32 runId, const std::vector<u32>& recordedNets)
{
    const size_t maxChanges = m_maxChanges;
    *this = WaveHistory{};
    m_maxChanges = maxChanges;
    m_runId = runId;
    const u32 highest = recordedNets.empty() ? 0 : *std::ranges::max_element(recordedNets);
    m_slot.assign(size_t(highest) + 1, 0);
    m_nets.resize(recordedNets.size());
    for (size_t i = 0; i < recordedNets.size(); i++) m_slot[recordedNets[i]] = u32(i + 1);
}

bool WaveHistory::add(const WaveChanges& changes)
{
    if (changes.runId != m_runId || m_runId == 0) return false;
    for (const WaveChange& change : changes.changes) {
        if (change.net >= m_slot.size() || !m_slot[change.net]) continue;
        m_nets[m_slot[change.net] - 1].ticks.push_back(change.tick);
        m_changeCount++;
    }
    if (changes.hasTicks) {
        m_lastTick = m_hasTicks ? std::max(m_lastTick, changes.lastTick) : changes.lastTick;
        m_hasTicks = true;
    }
    if (m_changeCount > m_maxChanges) dropOldest();
    return true;
}

const WaveHistory::Net* WaveHistory::net(u32 id) const
{
    if (id >= m_slot.size() || !m_slot[id]) return nullptr;
    return &m_nets[m_slot[id] - 1];
}

bool WaveHistory::bit(u32 id, u64 tick) const
{
    if (id == WaveSignal::AlwaysOne) return true;
    const Net* n = net(id);
    if (!n) return false;
    const auto changed = std::ranges::upper_bound(n->ticks, tick) - n->ticks.begin();
    return n->startValue != ((changed & 1) != 0);
}

size_t WaveHistory::changesBetween(u32 id, u64 from, u64 to) const
{
    const Net* n = net(id);
    if (!n || to < from) return 0;
    return size_t(std::ranges::upper_bound(n->ticks, to) - std::ranges::lower_bound(n->ticks, from));
}

std::optional<u64> WaveHistory::nextChange(u32 id, u64 tick) const
{
    const Net* n = net(id);
    if (!n) return std::nullopt;
    const auto it = std::ranges::upper_bound(n->ticks, tick);
    if (it == n->ticks.end()) return std::nullopt;
    return *it;
}

std::span<const u64> WaveHistory::changeTicks(u32 id) const
{
    const Net* n = net(id);
    return n ? std::span<const u64>(n->ticks) : std::span<const u64>();
}

void WaveHistory::dropOldest()
{
    // Drop the oldest quarter; each net keeps the value it had then
    while (m_changeCount > m_maxChanges && m_firstTick < m_lastTick) {
        const u64 cutoff = m_firstTick + std::max<u64>(1, (m_lastTick - m_firstTick) / 4);
        for (Net& n : m_nets) {
            const auto keep = std::ranges::lower_bound(n.ticks, cutoff);
            const auto dropped = keep - n.ticks.begin();
            if (dropped == 0) continue;
            if (dropped & 1) n.startValue = !n.startValue;
            n.ticks.erase(n.ticks.begin(), keep);
            m_changeCount -= size_t(dropped);
        }
        m_firstTick = cutoff;
    }
}

//===================== 2. formatValue ========================

namespace {

// Any width: repeated division by 10 over 32-bit words
QString decimal(const std::vector<bool>& bits)
{
    std::vector<u32> words((bits.size() + 31) / 32, 0);
    for (size_t i = 0; i < bits.size(); i++) {
        if (bits[i]) words[i / 32] |= u32(1) << (i % 32);
    }
    QString digits;
    const auto isZero = [&words] { return std::ranges::all_of(words, [](u32 w) { return w == 0; }); };
    while (!isZero()) {
        u64 remainder = 0;
        for (size_t i = words.size(); i-- > 0;) {
            const u64 current = (remainder << 32) | words[i];
            words[i] = u32(current / 10);
            remainder = current % 10;
        }
        digits.prepend(QChar(u'0' + int(remainder)));
    }
    return digits.isEmpty() ? u"0"_s : digits;
}

}

QString formatValue(const std::vector<bool>& bits, Radix radix)
{
    const size_t width = bits.size();
    if (width == 0) return {};

    switch (radix) {
    case Radix::Binary: {
        QString text;
        text.reserve(qsizetype(width));
        for (size_t i = width; i-- > 0;) text += bits[i] ? u'1' : u'0';
        return text;
    }
    case Radix::Hex: {
        QString text;
        for (size_t digit = (width + 3) / 4; digit-- > 0;) {
            int value = 0;
            for (size_t b = 0; b < 4; b++) {
                const size_t i = digit * 4 + b;
                if (i < width && bits[i]) value |= 1 << b;
            }
            text += QChar(value < 10 ? u'0' + value : u'a' + value - 10);
        }
        return text;
    }
    case Radix::Unsigned:
        return decimal(bits);
    case Radix::Signed: {
        if (!bits.back()) return decimal(bits);
        // Negative: magnitude by two's complement
        std::vector<bool> magnitude(width);
        bool carry = true;
        for (size_t i = 0; i < width; i++) {
            const bool inverted = !bits[i];
            magnitude[i] = inverted != carry;
            carry = inverted && carry;
        }
        return u'-' + decimal(magnitude);
    }
    }
    return {};
}

QString radixName(Radix radix)
{
    switch (radix) {
    case Radix::Hex: return QObject::tr("Hexadecimal");
    case Radix::Unsigned: return QObject::tr("Unsigned decimal");
    case Radix::Signed: return QObject::tr("Signed decimal");
    case Radix::Binary: return QObject::tr("Binary");
    }
    return {};
}

//===================== 3. WaveformModel ========================

QString WaveformModel::keyOf(const WaveSignal& signal)
{
    return signal.scope + QChar(0x1f) + signal.name;   // a character no name contains
}

void WaveformModel::startRun(u32 runId, const QList<WaveSignal>& runSignals, const std::vector<u32>& recordedNets, const QString& topName)
{
    m_signals = runSignals;
    m_keys.clear();
    for (const WaveSignal& signal : std::as_const(m_signals)) m_keys.append(keyOf(signal));
    m_topName = topName;
    m_history.start(runId, recordedNets);
    m_running = true;

    if (m_reset) {
        // A new circuit: all of its inputs and outputs
        m_shown.clear();
        for (const WaveSignal& signal : std::as_const(m_signals)) {
            if (signal.scope.isEmpty()) m_shown.append({keyOf(signal)});
        }
        m_reset = false;
    } else {
        // The same circuit: what still exists stays, new inputs and outputs are added
        const QSet<QString> keys(m_keys.begin(), m_keys.end());
        m_shown.removeIf([&keys](const Shown& shown) { return !keys.contains(shown.key); });
        for (const WaveSignal& signal : std::as_const(m_signals)) {
            const QString key = keyOf(signal);
            if (signal.scope.isEmpty() && !m_known.contains(key) && !isShown(int(m_keys.indexOf(key)))) m_shown.append({key});
        }
    }
    m_known = QSet<QString>(m_keys.begin(), m_keys.end());
    emit signalsChanged();
    emit historyGrew();
}

void WaveformModel::addChanges(const WaveChanges& changes)
{
    if (m_history.add(changes)) emit historyGrew();
}

void WaveformModel::resetSignals()
{
    m_signals.clear();
    m_keys.clear();
    m_shown.clear();
    m_known.clear();
    m_history = WaveHistory{};
    m_running = false;
    m_reset = true;
    emit signalsChanged();
    emit historyGrew();
}

void WaveformModel::stopRun()
{
    if (!m_running) return;
    m_running = false;
    emit signalsChanged();
}

void WaveformModel::clearHistory()
{
    m_history = WaveHistory{};   // run id 0 takes no reports
    m_running = false;
    emit signalsChanged();
    emit historyGrew();
}

int WaveformModel::shownIndex(int signal) const
{
    if (signal < 0 || signal >= m_keys.size()) return -1;
    const QString& key = m_keys[signal];
    for (qsizetype i = 0; i < m_shown.size(); i++) {
        if (m_shown[i].key == key) return int(i);
    }
    return -1;
}

QList<WaveRow> WaveformModel::rows() const
{
    QList<WaveRow> result;
    for (const Shown& shown : m_shown) {
        const int signal = int(m_keys.indexOf(shown.key));
        if (signal < 0) continue;
        const int width = m_signals[signal].width();
        result.append({signal, -1, width});
        if (shown.expanded && width > 1) {
            // Top part first; words start at multiples of the size from bit 0
            const int size = std::clamp(shown.wordBits, 1, width);
            for (int bit = (width - 1) / size * size; bit >= 0; bit -= size) result.append({signal, bit, std::min(size, width - bit)});
        }
    }
    return result;
}

bool WaveformModel::isShown(int signal) const
{
    return shownIndex(signal) >= 0;
}

void WaveformModel::show(int signal)
{
    if (signal < 0 || signal >= m_keys.size() || isShown(signal)) return;
    m_shown.append({m_keys[signal]});
    emit signalsChanged();
}

void WaveformModel::hide(int signal)
{
    const int index = shownIndex(signal);
    if (index < 0) return;
    m_shown.removeAt(index);
    emit signalsChanged();
}

void WaveformModel::move(int signal, int by)
{
    const int index = shownIndex(signal);
    const int to = index + by;
    if (index < 0 || to < 0 || to >= m_shown.size()) return;
    m_shown.move(index, to);
    emit signalsChanged();
}

Radix WaveformModel::radix(int signal) const
{
    const int index = shownIndex(signal);
    return index < 0 ? Radix::Hex : m_shown[index].radix;
}

void WaveformModel::setRadix(int signal, Radix radix)
{
    const int index = shownIndex(signal);
    if (index < 0 || m_shown[index].radix == radix) return;
    m_shown[index].radix = radix;
    emit signalsChanged();
}

bool WaveformModel::isExpanded(int signal) const
{
    const int index = shownIndex(signal);
    return index >= 0 && m_shown[index].expanded;
}

void WaveformModel::setExpanded(int signal, bool expanded)
{
    const int index = shownIndex(signal);
    if (index < 0 || m_shown[index].expanded == expanded) return;
    m_shown[index].expanded = expanded;
    emit signalsChanged();
}

int WaveformModel::wordBits(int signal) const
{
    const int index = shownIndex(signal);
    return index < 0 ? 1 : m_shown[index].wordBits;
}

void WaveformModel::splitIntoWords(int signal, int bits)
{
    const int index = shownIndex(signal);
    if (index < 0 || bits < 1) return;
    Shown& shown = m_shown[index];
    if (shown.expanded && shown.wordBits == bits) return;
    shown.expanded = true;
    shown.wordBits = bits;
    emit signalsChanged();
}

std::vector<u32> WaveformModel::netsOf(const WaveRow& row) const
{
    if (row.signal < 0 || row.signal >= m_signals.size()) return {};
    const WaveSignal& signal = m_signals[row.signal];
    if (row.bit < 0) return signal.nets;
    if (row.bit >= signal.width()) return {};
    const auto first = signal.nets.begin() + row.bit;
    return {first, first + std::min(std::max(row.width, 1), signal.width() - row.bit)};
}

std::vector<bool> WaveformModel::bitsAt(const WaveRow& row, u64 tick) const
{
    const std::vector<u32> nets = netsOf(row);
    std::vector<bool> bits(nets.size());
    for (size_t i = 0; i < nets.size(); i++) bits[i] = m_history.bit(nets[i], tick);
    return bits;
}

QString WaveformModel::valueText(const WaveRow& row, u64 tick) const
{
    if (m_history.isEmpty()) return {};
    const std::vector<bool> bits = bitsAt(row, tick);
    if (bits.size() == 1) return bits[0] ? u"1"_s : u"0"_s;
    return formatValue(bits, radix(row.signal));
}

QString WaveformModel::label(const WaveRow& row) const
{
    if (row.signal < 0 || row.signal >= m_signals.size()) return {};
    const WaveSignal& signal = m_signals[row.signal];
    const QString name = signal.scope.isEmpty() ? signal.name : signal.scope + u'/' + signal.name;
    if (row.bit >= 0) return u"%1[%2]"_s.arg(name).arg(row.bit / std::max(1, wordBits(row.signal)));   // a bit, or a word's index
    if (signal.width() > 1) return u"%1[%2:0]"_s.arg(name).arg(signal.width() - 1);
    return name;
}

//===================== 4. WaveformView ========================

namespace {
constexpr int ScopeSpacing = 6;          // between columns
constexpr int MinSegmentPx = 2;          // narrower ones become a dense band
constexpr int ArrowWidth = 14;           // a bus's expand arrow
constexpr int WaveMargin = 5;            // row edge to high or low level
constexpr int DragThreshold = 4;         // before a press becomes a pan

// 1, 2 or 5 times a power of ten, at least `minimum`
u64 niceStep(double minimum)
{
    u64 power = 1;
    while (true) {
        for (const u64 factor : {1, 2, 5}) {
            if (double(power * factor) >= minimum) return power * factor;
        }
        if (power > std::numeric_limits<u64>::max() / 100) return power;
        power *= 10;
    }
}
}

WaveformView::WaveformView(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(false);
    m_hbar = new QScrollBar(Qt::Horizontal, this);
    m_vbar = new QScrollBar(Qt::Vertical, this);
    connect(m_hbar, &QScrollBar::valueChanged, this, [this](int value) {
        if (m_updatingBars || !m_model) return;
        setFollow(false);
        m_leftTick = double(m_model->history().firstTick()) + value * m_hbarScale * m_ticksPerPixel;
        clampView();
        update();
    });
    connect(m_vbar, &QScrollBar::valueChanged, this, [this](int value) {
        if (m_updatingBars) return;
        m_scrollY = value;
        update();
    });
    layoutParts();
}

void WaveformView::setModel(WaveformModel* model)
{
    m_model = model;
    connect(model, &WaveformModel::signalsChanged, this, &WaveformView::signalsChanged);
    connect(model, &WaveformModel::historyGrew, this, &WaveformView::historyGrew);
    signalsChanged();
}

void WaveformView::signalsChanged()
{
    m_rows = m_model ? m_model->rows() : QList<WaveRow>{};
    if (m_selected >= m_rows.size()) m_selected = -1;
    if (!m_model || m_model->history().isEmpty()) {
        // No ticks (new run, reset, new circuit): no cursors, follow at the starting zoom
        m_cursor.reset();
        m_cursor2.reset();
        m_leftTick = 0;
        m_ticksPerPixel = 1.0 / 20;
        setFollow(true);
    }
    layoutParts();
    clampView();
    update();
}

void WaveformView::historyGrew()
{
    clampView();
    updateScrollBars();
    update();
}

QRect WaveformView::waveArea() const
{
    const int left = m_nameWidth + m_valueWidth + 2 * ScopeSpacing;
    const int right = width() - m_vbar->sizeHint().width();
    const int bottom = height() - m_hbar->sizeHint().height();
    return QRect(QPoint(left, RulerHeight), QPoint(std::max(left, right - 1), std::max(RulerHeight, bottom - 1)));
}

double WaveformView::tickAt(double x) const
{
    return m_leftTick + (x - waveArea().left()) * m_ticksPerPixel;
}

double WaveformView::xOf(double tick) const
{
    return waveArea().left() + (tick - m_leftTick) / m_ticksPerPixel;
}

void WaveformView::layoutParts()
{
    // Fits the longest label, within limits
    const QFontMetrics metrics(font());
    int longest = metrics.horizontalAdvance(tr("Signal"));
    if (m_model) {
        for (const WaveRow& row : std::as_const(m_rows)) longest = std::max(longest, metrics.horizontalAdvance(m_model->label(row)));
    }
    m_nameWidth = std::clamp(longest + ArrowWidth + 16, 110, 280);

    const int barH = m_hbar->sizeHint().height();
    const int barW = m_vbar->sizeHint().width();
    const QRect waves = waveArea();
    m_hbar->setGeometry(waves.left(), height() - barH, std::max(0, width() - waves.left() - barW), barH);
    m_vbar->setGeometry(width() - barW, RulerHeight, barW, std::max(0, height() - RulerHeight - barH));
    updateScrollBars();
}

void WaveformView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutParts();
    clampView();
}

void WaveformView::clampView()
{
    const int visibleRows = std::max(0, waveArea().height() - 1);
    m_scrollY = std::clamp(m_scrollY, 0, std::max(0, rowsHeight() - visibleRows));
    if (!m_model || m_model->history().isEmpty()) return;

    const WaveHistory& history = m_model->history();
    const double first = double(history.firstTick());
    const double end = double(history.lastTick()) + 1;       // the end of the newest tick
    const double width = std::max(1, waveArea().width());
    const double maxTicksPerPixel = std::max(MinTicksPerPixel, (end - first) / width * 1.05);
    m_ticksPerPixel = std::clamp(m_ticksPerPixel, MinTicksPerPixel, std::max(maxTicksPerPixel, 1.0 / 20));
    const double visible = width * m_ticksPerPixel;
    if (m_follow) m_leftTick = end - visible;
    m_leftTick = std::clamp(m_leftTick, first, std::max(first, end - visible));
    updateScrollBars();
}

void WaveformView::updateScrollBars()
{
    const QScopedValueRollback<bool> updating(m_updatingBars, true);
    const int visibleRows = std::max(0, waveArea().height());
    m_vbar->setRange(0, std::max(0, rowsHeight() - visibleRows));
    m_vbar->setPageStep(visibleRows);
    m_vbar->setSingleStep(RowHeight);
    m_vbar->setValue(m_scrollY);

    if (!m_model || m_model->history().isEmpty()) {
        m_hbar->setRange(0, 0);
        return;
    }
    const WaveHistory& history = m_model->history();
    const double first = double(history.firstTick());
    const double totalPx = (double(history.lastTick()) + 1 - first) / m_ticksPerPixel;
    const double width = std::max(1, waveArea().width());
    m_hbarScale = std::max(1.0, totalPx / 1e9);              // keeps the range within an int
    m_hbar->setRange(0, int(std::max(0.0, (totalPx - width) / m_hbarScale)));
    m_hbar->setPageStep(int(width / m_hbarScale));
    m_hbar->setSingleStep(std::max(1, int(40 / m_hbarScale)));
    m_hbar->setValue(int((m_leftTick - first) / m_ticksPerPixel / m_hbarScale));
}

void WaveformView::setCursorTick(std::optional<u64> tick)
{
    m_cursor = tick;
    emit cursorsChanged();
    update();
}

void WaveformView::setSecondCursorTick(std::optional<u64> tick)
{
    m_cursor2 = tick;
    emit cursorsChanged();
    update();
}

void WaveformView::setFollow(bool follow)
{
    if (m_follow == follow) return;
    m_follow = follow;
    if (follow) clampView();
    emit followChanged(follow);
    update();
}

void WaveformView::zoomIn()
{
    zoomAround(m_follow ? waveArea().right() : waveArea().center().x(), 0.5);
}

void WaveformView::zoomOut()
{
    zoomAround(m_follow ? waveArea().right() : waveArea().center().x(), 2.0);
}

void WaveformView::zoomToFit()
{
    if (!m_model || m_model->history().isEmpty()) return;
    const WaveHistory& history = m_model->history();
    m_ticksPerPixel = (double(history.lastTick()) + 1 - double(history.firstTick())) / std::max(1, waveArea().width());
    m_leftTick = double(history.firstTick());
    clampView();
    update();
}

void WaveformView::zoomAround(double x, double factor)
{
    const double tick = tickAt(x);
    m_ticksPerPixel *= factor;
    m_ticksPerPixel = std::max(m_ticksPerPixel, MinTicksPerPixel);
    m_leftTick = tick - (x - waveArea().left()) * m_ticksPerPixel;
    clampView();
    update();
}

int WaveformView::rowAt(int y) const
{
    if (y < RulerHeight) return -1;
    const int row = (y - RulerHeight + m_scrollY) / RowHeight;
    return row < m_rows.size() ? row : -1;
}

void WaveformView::scrollTime(double pixels)
{
    if (pixels == 0) return;
    setFollow(false);   // like a drag
    m_leftTick += pixels * m_ticksPerPixel;
    clampView();
    update();
}

void WaveformView::scrollRows(int pixels)
{
    m_scrollY += pixels;
    clampView();
    updateScrollBars();
    update();
}

std::optional<u64> WaveformView::nextChange(const std::vector<u32>& nets, u64 tick) const
{
    std::optional<u64> next;
    for (const u32 net : nets) {
        const std::optional<u64> change = m_model->history().nextChange(net, tick);
        if (change && (!next || *change < *next)) next = change;
    }
    return next;
}

//----- Painting -----

QString WaveformView::rulerText(u64 tick) const
{
    return QString::number(tick);
}

void WaveformView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const Theme& theme = currentTheme();
    const QRect waves = waveArea();
    painter.fillRect(rect(), theme.panel);
    painter.fillRect(QRect(waves.left(), 0, width() - waves.left(), height()), theme.canvas);

    if (!m_model || !m_model->hasRun()) {
        painter.setPen(theme.mutedText);
        painter.drawText(rect(), Qt::AlignCenter, tr("Start the simulation to see the waveforms."));
        return;
    }

    paintRuler(painter);

    const WaveHistory& history = m_model->history();
    const std::optional<u64> at = m_cursor ? m_cursor : (history.isEmpty() ? std::nullopt : std::optional<u64>(history.lastTick()));
    QFont valueFont = font();
    valueFont.setStyleHint(QFont::Monospace);
    valueFont.setFamilies({u"JetBrains Mono"_s, u"Cascadia Mono"_s, u"Consolas"_s, u"DejaVu Sans Mono"_s, u"monospace"_s});
    const QFontMetrics nameMetrics(font());

    painter.save();
    painter.setClipRect(QRect(0, RulerHeight, width(), waves.height() + 1));
    const int first = m_scrollY / RowHeight;
    for (int i = first; i < m_rows.size(); i++) {
        const int top = RulerHeight + i * RowHeight - m_scrollY;
        if (top > waves.bottom()) break;
        const WaveRow& row = m_rows[i];

        if (i == m_selected) painter.fillRect(QRect(0, top, waves.left(), RowHeight), theme.buttonChecked);

        // A bus gets an arrow; parts of an expanded bus are indented
        const WaveSignal& signal = m_model->signalList()[row.signal];
        int textLeft = 6 + ArrowWidth;
        if (row.bit < 0 && signal.width() > 1) {
            painter.setPen(theme.mutedText);
            painter.drawText(QRect(4, top, ArrowWidth, RowHeight), Qt::AlignCenter,
                             m_model->isExpanded(row.signal) ? u"▾"_s : u"▸"_s);
        }
        if (row.bit >= 0) textLeft += 10;
        painter.setPen(row.bit >= 0 ? theme.mutedText : theme.text);
        painter.drawText(QRect(textLeft, top, m_nameWidth - textLeft - 4, RowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                         nameMetrics.elidedText(m_model->label(row), Qt::ElideLeft, m_nameWidth - textLeft - 4));

        // At the cursor, or the newest tick
        if (at) {
            painter.setFont(valueFont);
            painter.setPen(theme.text);
            const QRect valueRect(m_nameWidth + ScopeSpacing, top, m_valueWidth, RowHeight);
            painter.drawText(valueRect, Qt::AlignVCenter | Qt::AlignRight,
                             QFontMetrics(valueFont).elidedText(m_model->valueText(row, *at), Qt::ElideLeft, m_valueWidth));
            painter.setFont(font());
        }

        painter.setPen(theme.waveGrid);
        painter.drawLine(0, top + RowHeight - 1, width(), top + RowHeight - 1);
        paintWaves(painter, row, top);
    }
    painter.restore();

    if (m_cursor2) paintCursor(painter, *m_cursor2, theme.waveCursor2);
    if (m_cursor) paintCursor(painter, *m_cursor, theme.waveCursor);
    if (const std::optional<QRect> pill = measureLabel()) {
        const u64 ticks = std::max(*m_cursor, *m_cursor2) - std::min(*m_cursor, *m_cursor2);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme.button);
        painter.drawRoundedRect(*pill, 9, 9);
        painter.setPen(theme.text);
        painter.drawText(*pill, Qt::AlignCenter, tr("Δ %1 ticks").arg(ticks));
    }
}

std::optional<QRect> WaveformView::measureLabel() const
{
    if (!m_cursor || !m_cursor2) return std::nullopt;
    const QRect waves = waveArea();
    const u64 a = std::min(*m_cursor, *m_cursor2), b = std::max(*m_cursor, *m_cursor2);
    const double x0 = std::max<double>(xOf(double(a)), waves.left()), x1 = std::min<double>(xOf(double(b)), waves.right());
    const int textWidth = QFontMetrics(font()).horizontalAdvance(tr("Δ %1 ticks").arg(b - a)) + 14;
    return QRect(int((x0 + x1) / 2 - textWidth / 2.0), 3, textWidth, RulerHeight - 6);
}

void WaveformView::paintRuler(QPainter& painter)
{
    const Theme& theme = currentTheme();
    const QRect waves = waveArea();
    painter.fillRect(QRect(0, 0, width(), RulerHeight), theme.panel);
    painter.setPen(theme.mutedText);
    painter.drawText(QRect(6, 0, m_nameWidth - 6, RulerHeight), Qt::AlignVCenter | Qt::AlignLeft, tr("Signal"));
    const WaveHistory& history = m_model->history();
    const QString valueTitle = m_cursor ? tr("At %1").arg(*m_cursor) : history.isEmpty() ? tr("Value") : tr("At %1").arg(history.lastTick());
    painter.drawText(QRect(m_nameWidth + ScopeSpacing, 0, m_valueWidth, RulerHeight), Qt::AlignVCenter | Qt::AlignRight, valueTitle);
    painter.setPen(theme.border);
    painter.drawLine(0, RulerHeight - 1, width(), RulerHeight - 1);
    if (history.isEmpty()) return;

    // Labels at round ticks, at least 70 px apart, none under the measuring label
    const std::optional<QRect> measure = measureLabel();
    const QFontMetrics metrics(font());
    const u64 step = niceStep(70 * m_ticksPerPixel);
    const double firstVisible = std::max(0.0, tickAt(waves.left()));
    u64 tick = u64(firstVisible / double(step)) * step;
    painter.save();
    painter.setClipRect(QRect(waves.left(), 0, waves.width(), height()));
    for (; double(tick) <= tickAt(waves.right() + 1); tick += step) {
        const int x = int(std::round(xOf(double(tick))));
        painter.setPen(theme.waveGrid);
        painter.drawLine(x, RulerHeight, x, waves.bottom());
        painter.setPen(theme.border);
        painter.drawLine(x, RulerHeight - 6, x, RulerHeight - 1);
        const QRect label(x + 3, 0, metrics.horizontalAdvance(rulerText(tick)) + 2, RulerHeight - 3);
        if (measure && label.adjusted(-4, 0, 4, 0).intersects(*measure)) continue;
        painter.setPen(theme.mutedText);
        painter.drawText(label, Qt::AlignVCenter | Qt::AlignLeft, rulerText(tick));
    }
    painter.restore();
}

void WaveformView::paintCursor(QPainter& painter, u64 tick, const QColor& color)
{
    const QRect waves = waveArea();
    const double x = xOf(double(tick));
    if (x < waves.left() || x > waves.right()) return;
    painter.setPen(QPen(color, 1.5));
    painter.drawLine(QPointF(x, 0), QPointF(x, waves.bottom()));
}

void WaveformView::paintWaves(QPainter& painter, const WaveRow& row, int top)
{
    const WaveHistory& history = m_model->history();
    if (history.isEmpty()) return;
    const Theme& theme = currentTheme();
    const QRect waves = waveArea();
    const std::vector<u32> nets = m_model->netsOf(row);
    const bool bus = nets.size() > 1;
    const double high = top + WaveMargin, low = top + RowHeight - 1 - WaveMargin, middle = (high + low) / 2;

    const double startTick = std::max(double(history.firstTick()), std::floor(tickAt(waves.left())));
    const u64 visStart = u64(std::max(0.0, startTick));
    const u64 visEnd = std::min(history.lastTick(), u64(std::max(0.0, std::floor(tickAt(waves.right() + 1)))));
    if (visStart > visEnd) return;

    painter.save();
    painter.setClipRect(QRect(waves.left(), top, waves.width(), RowHeight - 1));
    painter.setRenderHint(QPainter::Antialiasing);
    QColor fill = theme.wireHigh;
    fill.setAlpha(bus ? 30 : 40);
    QColor band = theme.wireHigh;
    band.setAlpha(110);
    const QPen line(theme.wireHigh, 1.5);
    const QFontMetrics metrics(font());

    std::optional<bool> previous;           // a 1-bit row's value at the last segment's end
    u64 tick = visStart;
    while (tick <= visEnd) {
        const std::optional<u64> change = nextChange(nets, tick);
        const u64 end = change ? std::min(*change, history.lastTick() + 1) : history.lastTick() + 1;
        const double x0 = xOf(double(tick)), x1 = xOf(double(end));

        if (x1 - x0 < MinSegmentPx && end <= visEnd) {
            // Too many changes for the pixels: one band, then skip ahead
            const u64 skipTo = std::max(end, u64(std::max(0.0, std::floor(tickAt(x0 + MinSegmentPx)))));
            painter.fillRect(QRectF(x0, high, xOf(double(skipTo)) - x0, low - high), band);
            previous.reset();
            tick = skipTo;
            continue;
        }

        if (bus) {
            const double slant = std::min(3.0, (x1 - x0) / 2);
            const QPolygonF box{{x0, middle}, {x0 + slant, high}, {x1 - slant, high}, {x1, middle}, {x1 - slant, low}, {x0 + slant, low}};
            painter.setPen(line);
            painter.setBrush(fill);
            painter.drawPolygon(box);
            const QString text = m_model->valueText(row, tick);
            const double left = std::max(x0, double(waves.left())) + slant + 3;
            const double right = std::min(x1, double(waves.right())) - slant - 3;
            if (right - left > metrics.horizontalAdvance(text)) {
                painter.setPen(theme.componentText);
                painter.drawText(QRectF(left, high, right - left, low - high), Qt::AlignCenter, text);
            }
        } else {
            const bool value = history.bit(nets.empty() ? 0 : nets[0], tick);
            if (value) painter.fillRect(QRectF(x0, high, x1 - x0, low - high), fill);
            painter.setPen(line);
            const double y = value ? high : low;
            painter.drawLine(QPointF(x0, y), QPointF(x1, y));
            if (previous && *previous != value) painter.drawLine(QPointF(x0, high), QPointF(x0, low));
            previous = value;
        }
        tick = end;
    }
    painter.restore();
}

//----- Mouse and keys -----

void WaveformView::wheelEvent(QWheelEvent* event)
{
    // Some platforms send Shift+wheel as a sideways turn
    const QPoint delta = event->angleDelta();
    const double steps = (delta.y() != 0 ? delta.y() : delta.x()) / 120.0;
    if (steps == 0) return;
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (modifiers & Qt::ControlModifier) {
        // Around the pointer; around the right edge when following or over the names
        const QRect waves = waveArea();
        const double x = m_follow || event->position().x() < waves.left() ? waves.right() : event->position().x();
        zoomAround(x, std::pow(1.25, -steps));
    } else if (modifiers & Qt::ShiftModifier) {
        scrollRows(int(-steps * RowHeight * 2));
    } else {
        // An eighth of the view per notch
        scrollTime(-steps * waveArea().width() / 8.0);
    }
    event->accept();
}

void WaveformView::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    const QPoint pos = event->position().toPoint();
    if (event->button() != Qt::LeftButton) return;

    if (pos.x() < waveArea().left()) {
        // Selects a row; a bus's arrow expands or collapses it
        m_selected = rowAt(pos.y());
        if (m_selected >= 0 && pos.x() < 4 + ArrowWidth) {
            const WaveRow row = m_rows[m_selected];
            if (row.bit < 0 && m_model->signalList()[row.signal].width() > 1) m_model->setExpanded(row.signal, !m_model->isExpanded(row.signal));
        }
        update();
        return;
    }
    m_pressed = true;
    m_panning = false;
    m_pressPos = pos;
    m_pressLeftTick = m_leftTick;
    m_pressScrollY = m_scrollY;
}

void WaveformView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_pressed) return;
    const QPoint delta = event->position().toPoint() - m_pressPos;
    if (!m_panning && delta.manhattanLength() < DragThreshold) return;
    if (!m_panning) {
        m_panning = true;
        setCursor(Qt::ClosedHandCursor);
    }
    if (delta.x() != 0) setFollow(false);
    m_leftTick = m_pressLeftTick - delta.x() * m_ticksPerPixel;
    m_scrollY = m_pressScrollY - delta.y();
    clampView();
    update();
}

void WaveformView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_pressed) return;
    m_pressed = false;
    if (m_panning) {
        m_panning = false;
        unsetCursor();
        return;
    }
    if (!m_model || m_model->history().isEmpty()) return;
    const WaveHistory& history = m_model->history();
    const double at = std::floor(tickAt(event->position().x()));
    const u64 tick = u64(std::clamp(at, double(history.firstTick()), double(history.lastTick())));
    if (event->modifiers() & Qt::ShiftModifier) setSecondCursorTick(tick);
    else setCursorTick(tick);
}

void WaveformView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    const int index = rowAt(pos.y());
    if (pos.x() >= waveArea().left() || index < 0) return;
    const WaveRow row = m_rows[index];
    if (row.bit < 0 && m_model->signalList()[row.signal].width() > 1) m_model->setExpanded(row.signal, !m_model->isExpanded(row.signal));
}

void WaveformView::contextMenuEvent(QContextMenuEvent* event)
{
    const int index = rowAt(event->pos().y());
    if (index < 0) return;
    m_selected = index;
    update();
    showRowMenu(index, event->globalPos());
}

void WaveformView::showRowMenu(int index, const QPoint& globalPos)
{
    const WaveRow row = m_rows[index];
    const int signal = row.signal;
    const bool bus = m_model->signalList()[signal].width() > 1;

    QMenu menu(this);
    if (bus) {
        QMenu* radixMenu = menu.addMenu(tr("Radix"));
        auto* group = new QActionGroup(radixMenu);
        for (const Radix radix : {Radix::Hex, Radix::Unsigned, Radix::Signed, Radix::Binary}) {
            QAction* action = radixMenu->addAction(radixName(radix));
            action->setCheckable(true);
            action->setChecked(m_model->radix(signal) == radix);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [this, signal, radix] { m_model->setRadix(signal, radix); });
        }
        const bool expanded = m_model->isExpanded(signal);
        connect(menu.addAction(expanded ? tr("Collapse") : tr("Expand")), &QAction::triggered, this,
                [this, signal, expanded] { m_model->setExpanded(signal, !expanded); });

        // Bits, the usual word sizes below the width, or any size
        const int width = m_model->signalList()[signal].width();
        const int current = expanded ? m_model->wordBits(signal) : 0;
        QMenu* splitMenu = menu.addMenu(tr("Split into"));
        auto* sizes = new QActionGroup(splitMenu);
        bool listed = false;
        auto addSize = [&](int bits, const QString& text) {
            QAction* action = splitMenu->addAction(text);
            action->setCheckable(true);
            action->setChecked(bits == current);
            listed |= bits == current;
            sizes->addAction(action);
            connect(action, &QAction::triggered, this, [this, signal, bits] { m_model->splitIntoWords(signal, bits); });
        };
        addSize(1, tr("Bits"));
        for (const int bits : {8, 16, 32, 64}) {
            if (bits < width) addSize(bits, tr("%1-bit words").arg(bits));
        }
        if (width > 2) {
            splitMenu->addSeparator();
            const QString other = current > 1 && !listed ? tr("%1-bit words (other size…)").arg(current) : tr("Other size…");
            QAction* action = splitMenu->addAction(other);
            action->setCheckable(true);
            action->setChecked(current > 1 && !listed);
            sizes->addAction(action);
            connect(action, &QAction::triggered, this, [this, signal, width, current] {
                bool ok = false;
                const int bits = QInputDialog::getInt(this, tr("Split into words"), tr("Bits per word:"),
                                                      current > 1 ? current : std::min(8, width - 1), 2, width - 1, 1, &ok);
                if (ok) m_model->splitIntoWords(signal, bits);
            });
        }
        menu.addSeparator();
    }
    connect(menu.addAction(tr("Move up")), &QAction::triggered, this, [this, signal] { m_model->move(signal, -1); });
    connect(menu.addAction(tr("Move down")), &QAction::triggered, this, [this, signal] { m_model->move(signal, +1); });
    menu.addSeparator();
    connect(menu.addAction(tr("Remove from waveforms")), &QAction::triggered, this, [this, signal] { m_model->hide(signal); });
    menu.exec(globalPos);
}

void WaveformView::keyPressEvent(QKeyEvent* event)
{
    if (!m_model) return;
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_selected >= 0) {
        m_model->hide(m_rows[m_selected].signal);
    } else if (event->key() == Qt::Key_Up && m_selected > 0) {
        m_selected--;
        update();
    } else if (event->key() == Qt::Key_Down && m_selected + 1 < m_rows.size()) {
        m_selected++;
        update();
    } else {
        QWidget::keyPressEvent(event);
    }
}

//===================== 5. WaveSignalTree ========================

WaveSignalTree::WaveSignalTree(QTreeWidget* tree, WaveformModel* model, QObject* parent)
    : QObject(parent), m_tree(tree), m_model(model)
{
    if (m_tree->columnCount() < 2) m_tree->setColumnCount(2);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(model, &WaveformModel::signalsChanged, this, &WaveSignalTree::refresh);
    connect(m_tree, &QTreeWidget::itemChanged, this, &WaveSignalTree::itemChanged);
    refresh();
}

void WaveSignalTree::refresh()
{
    QStringList keys;
    for (const WaveSignal& signal : m_model->signalList()) keys.append(WaveformModel::keyOf(signal));
    if (keys != m_builtFor || m_tree->topLevelItemCount() == 0) {
        m_builtFor = keys;
        rebuild();
        return;
    }
    // Same signals: only update the checks
    const QScopedValueRollback<bool> filling(m_filling, true);
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        const QVariant signal = (*it)->data(0, Qt::UserRole);
        if (signal.isValid()) (*it)->setCheckState(0, m_model->isShown(signal.toInt()) ? Qt::Checked : Qt::Unchecked);
    }
}

void WaveSignalTree::rebuild()
{
    const QScopedValueRollback<bool> filling(m_filling, true);

    QSet<QString> open;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if ((*it)->isExpanded()) open.insert((*it)->data(0, Qt::UserRole + 1).toString());
    }
    const bool first = m_tree->topLevelItemCount() == 0 || open.isEmpty();
    m_tree->clear();

    const QList<WaveSignal>& signalList = m_model->signalList();
    if (signalList.isEmpty()) {
        auto* hint = new QTreeWidgetItem(m_tree, {tr("Start the simulation to list the signals.")});
        hint->setFlags(Qt::NoItemFlags);
        return;
    }

    const auto scopeFlags = Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate;
    auto* top = new QTreeWidgetItem(m_tree, {m_model->topName().isEmpty() ? tr("Circuit") : m_model->topName()});
    top->setFlags(scopeFlags);
    top->setData(0, Qt::UserRole + 1, QString());
    QHash<QString, QTreeWidgetItem*> scopes{{QString(), top}};

    // Made with its parents on first use: "u/h0" under "u"
    std::function<QTreeWidgetItem*(const QString&, const QString&)> scopeItem = [&](const QString& path, const QString& module) {
        if (QTreeWidgetItem* item = scopes.value(path)) return item;
        const qsizetype slash = path.lastIndexOf(u'/');
        QTreeWidgetItem* parent = slash < 0 ? top : scopeItem(path.left(slash), QString());
        const QString instance = slash < 0 ? path : path.mid(slash + 1);
        auto* item = new QTreeWidgetItem(parent, {module.isEmpty() ? instance : u"%1 (%2)"_s.arg(instance, module)});
        item->setFlags(scopeFlags);
        item->setData(0, Qt::UserRole + 1, path);
        scopes.insert(path, item);
        return item;
    };

    for (qsizetype i = 0; i < signalList.size(); i++) {
        const WaveSignal& signal = signalList[i];
        QTreeWidgetItem* parent = scopeItem(signal.scope, signal.module);
        if (!signal.module.isEmpty() && parent->text(0) == signal.scope.mid(signal.scope.lastIndexOf(u'/') + 1)) {
            parent->setText(0, u"%1 (%2)"_s.arg(parent->text(0), signal.module));   // made before its module was known
        }
        const QString name = signal.width() > 1 ? u"%1[%2:0]"_s.arg(signal.name).arg(signal.width() - 1) : signal.name;
        auto* item = new QTreeWidgetItem(parent, {name, signal.output ? tr("out") : tr("in")});
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        item->setData(0, Qt::UserRole, int(i));
        item->setCheckState(0, m_model->isShown(int(i)) ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(0, signal.scope.isEmpty() ? name : signal.scope + u'/' + name);
    }

    for (auto it = scopes.cbegin(); it != scopes.cend(); ++it) {
        it.value()->setExpanded(first ? it.key().isEmpty() : open.contains(it.key()));
    }
}

void WaveSignalTree::itemChanged(QTreeWidgetItem* item, int column)
{
    if (m_filling || column != 0) return;
    const QVariant signal = item->data(0, Qt::UserRole);
    if (!signal.isValid()) return;   // an instance: Qt checks its ports one by one
    if (item->checkState(0) == Qt::Checked) m_model->show(signal.toInt());
    else m_model->hide(signal.toInt());
}

//===================== 6. VCD export ========================

namespace {

// Short identifiers from the printable ASCII range VCD allows: "!", "\"", ..., "!!", ...
QByteArray vcdId(qsizetype index)
{
    QByteArray id;
    do {
        id += char(33 + index % 94);
        index /= 94;
    } while (index > 0);
    return id;
}

QByteArray vcdName(const QString& name)   // VCD names can't contain whitespace
{
    static const QRegularExpression space(u"\\s+"_s);
    const QString clean = name.trimmed().replace(space, u"_"_s);
    return clean.isEmpty() ? QByteArray("unnamed") : clean.toUtf8();
}

} // namespace

std::expected<void, QString> writeVcd(QIODevice& out, const WaveformModel& model)
{
    const WaveHistory& history = model.history();
    const QList<WaveSignal>& signalList = model.signalList();
    if (history.isEmpty() || signalList.isEmpty()) return std::unexpected(QObject::tr("There is no run to export."));

    QByteArray text;
    const auto flush = [&out, &text] {
        const bool ok = out.write(text) == text.size();
        text.clear();
        return ok;
    };
    text += "$date " + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8() + " $end\n";
    text += "$version BitSimulator $end\n";
    text += "$comment One simulation tick is 1 ns $end\n";
    text += "$timescale 1ns $end\n";

    // The circuit's scope holds a scope per block instance, nested by path ("core/alu" inside "core")
    struct Scope {
        QByteArray name;
        QList<int> children;
        QList<qsizetype> signalIndices;
    };
    QList<Scope> scopes{{vcdName(model.topName().isEmpty() ? u"circuit"_s : model.topName()), {}, {}}};
    for (qsizetype i = 0; i < signalList.size(); i++) {
        if (signalList[i].nets.empty()) continue;
        int scope = 0;
        if (!signalList[i].scope.isEmpty()) {
            for (const QString& part : signalList[i].scope.split(u'/')) {
                const QByteArray name = vcdName(part);
                const auto child = std::ranges::find_if(scopes[scope].children, [&](int c) { return scopes[c].name == name; });
                if (child != scopes[scope].children.end()) {
                    scope = *child;
                } else {
                    scopes.append({name, {}, {}});
                    scopes[scope].children.append(int(scopes.size() - 1));
                    scope = int(scopes.size() - 1);
                }
            }
        }
        scopes[scope].signalIndices.append(i);
    }
    const std::function<void(int)> declare = [&](int scope) {
        text += "$scope module " + scopes[scope].name + " $end\n";
        for (const qsizetype i : std::as_const(scopes[scope].signalIndices)) {
            const qsizetype width = qsizetype(signalList[i].nets.size());
            text += "$var wire " + QByteArray::number(width) + ' ' + vcdId(i) + ' ' + vcdName(signalList[i].name);
            if (width > 1) text += " [" + QByteArray::number(width - 1) + ":0]";
            text += " $end\n";
        }
        for (const int child : std::as_const(scopes[scope].children)) declare(child);
        text += "$upscope $end\n";
    };
    declare(0);
    text += "$enddefinitions $end\n";

    // A value is the signal's value after that tick, as the waveforms show it
    const auto value = [&](qsizetype i, u64 tick) {
        const std::vector<u32>& nets = signalList[i].nets;
        if (nets.size() == 1) return QByteArray(1, history.bit(nets[0], tick) ? '1' : '0') + vcdId(i);
        QByteArray bits("b");
        for (size_t b = nets.size(); b-- > 0;) bits += history.bit(nets[b], tick) ? '1' : '0';   // top bit first
        return bits + ' ' + vcdId(i);
    };

    // Every signal's change ticks, then all of them merged in tick order
    const u64 first = history.firstTick();
    std::vector<std::vector<u64>> changes(size_t(signalList.size()));
    for (qsizetype i = 0; i < signalList.size(); i++) {
        std::vector<u64>& ticks = changes[size_t(i)];
        for (const u32 net : signalList[i].nets) std::ranges::copy(history.changeTicks(net), std::back_inserter(ticks));
        std::ranges::sort(ticks);
        ticks.erase(std::ranges::unique(ticks).begin(), ticks.end());
    }
    text += "#" + QByteArray::number(first) + "\n$dumpvars\n";
    using Next = std::pair<u64, qsizetype>;   // tick, signal
    std::priority_queue<Next, std::vector<Next>, std::greater<>> queue;
    std::vector<size_t> position(changes.size());
    for (qsizetype i = 0; i < signalList.size(); i++) {
        if (signalList[i].nets.empty()) continue;
        text += value(i, first) + '\n';
        const std::vector<u64>& ticks = changes[size_t(i)];
        position[size_t(i)] = size_t(std::ranges::upper_bound(ticks, first) - ticks.begin());
        if (position[size_t(i)] < ticks.size()) queue.push({ticks[position[size_t(i)]], i});
    }
    text += "$end\n";
    while (!queue.empty()) {
        const u64 tick = queue.top().first;
        text += "#" + QByteArray::number(tick) + '\n';
        while (!queue.empty() && queue.top().first == tick) {
            const qsizetype i = queue.top().second;
            queue.pop();
            text += value(i, tick) + '\n';
            const std::vector<u64>& ticks = changes[size_t(i)];
            if (++position[size_t(i)] < ticks.size()) queue.push({ticks[position[size_t(i)]], i});
        }
        if (text.size() > (1 << 20) && !flush()) return std::unexpected(out.errorString());
    }
    text += "#" + QByteArray::number(history.lastTick() + 1) + '\n';   // the last tick lasts until here
    if (!flush()) return std::unexpected(out.errorString());
    return {};
}
