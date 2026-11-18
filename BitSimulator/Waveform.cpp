#include "Waveform.h"
#include <algorithm>

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

void WaveHistory::dropOldest()
{
    // Each round drops the oldest quarter of the kept ticks; a net remembers the value it had then
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

// A non-negative number of any width as decimal digits: repeated division by 10 of 32-bit words
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

} // namespace

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
        // Negative: the magnitude is the two's complement (invert, add 1)
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
    return signal.scope + QChar(0x1f) + signal.name;   // a character no name or path contains
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
        // A new circuit: all of its inputs and outputs, in the order the canvas lists them
        m_shown.clear();
        for (const WaveSignal& signal : std::as_const(m_signals)) {
            if (signal.scope.isEmpty()) m_shown.append({keyOf(signal)});
        }
        m_reset = false;
    } else {
        // The same circuit, maybe edited: what is still there stays, and new inputs and outputs are added
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
        result.append({signal, -1});
        const int width = m_signals[signal].width();
        if (shown.expanded && width > 1) {
            for (int bit = width - 1; bit >= 0; bit--) result.append({signal, bit});   // the top bit first
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

std::vector<u32> WaveformModel::netsOf(const WaveRow& row) const
{
    if (row.signal < 0 || row.signal >= m_signals.size()) return {};
    const WaveSignal& signal = m_signals[row.signal];
    if (row.bit < 0) return signal.nets;
    if (row.bit >= signal.width()) return {};
    return {signal.nets[size_t(row.bit)]};
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
    if (row.bit >= 0) return u"%1[%2]"_s.arg(name).arg(row.bit);
    if (signal.width() > 1) return u"%1[%2:0]"_s.arg(name).arg(signal.width() - 1);
    return name;
}
