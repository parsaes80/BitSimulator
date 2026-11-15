// The small pure functions in Items.h (component and source properties) that the items and the properties panel rely on.

#include <QtTest>
#include <QSet>
#include <bit>
#include "Items.h"

using namespace Qt::StringLiterals;

namespace {
// The values a source outputs at ticks 0 .. count-1
QList<std::uint64_t> sequence(const SourceProps& props, int count)
{
    QList<std::uint64_t> values;
    for (int tick = 0; tick < count; tick++) values.append(sourceValueAt(props, std::uint64_t(tick)));
    return values;
}
}

class TestItemProps : public QObject {
    Q_OBJECT
private slots:
    void bitsNeeded_data()
    {
        QTest::addColumn<QList<std::uint64_t>>("values");
        QTest::addColumn<int>("bits");
        QTest::newRow("empty list") << QList<std::uint64_t>{} << 1;
        QTest::newRow("only zero") << QList<std::uint64_t>{0} << 1;
        QTest::newRow("one") << QList<std::uint64_t>{0, 1} << 1;
        QTest::newRow("two needs 2 bits") << QList<std::uint64_t>{2} << 2;
        QTest::newRow("2,5") << QList<std::uint64_t>{2, 5} << 3;
        QTest::newRow("seven") << QList<std::uint64_t>{7, 1} << 3;
        QTest::newRow("eight") << QList<std::uint64_t>{8} << 4;
        QTest::newRow("255") << QList<std::uint64_t>{255} << 8;
        QTest::newRow("256") << QList<std::uint64_t>{3, 256} << 9;
        QTest::newRow("32 bits") << QList<std::uint64_t>{0xFFFF'FFFFull} << 32;
        QTest::newRow("33 bits") << QList<std::uint64_t>{0x1'0000'0000ull} << 33;
        QTest::newRow("64 bits") << QList<std::uint64_t>{~std::uint64_t{0}} << 64;
    }
    void bitsNeeded()
    {
        QFETCH(QList<std::uint64_t>, values);
        QFETCH(int, bits);
        QCOMPARE(::bitsNeeded(values), bits);
    }

    void muxSelectLines_data()
    {
        QTest::addColumn<int>("inputs");
        QTest::addColumn<int>("lines");
        QTest::newRow("2") << 2 << 1;
        QTest::newRow("3") << 3 << 2;
        QTest::newRow("4") << 4 << 2;
        QTest::newRow("5") << 5 << 3;      // the natural-log version gave 2 for 5, 6 and 7
        QTest::newRow("6") << 6 << 3;
        QTest::newRow("7") << 7 << 3;
        QTest::newRow("8") << 8 << 3;
        QTest::newRow("9") << 9 << 4;
        QTest::newRow("16") << 16 << 4;
        QTest::newRow("below the minimum") << 1 << 1;
    }
    void muxSelectLines()
    {
        QFETCH(int, inputs);
        QFETCH(int, lines);
        QCOMPARE(::muxSelectLines(inputs), lines);
        // Every data input must be addressable
        if (inputs >= 2) QVERIFY((1 << lines) >= inputs);
    }

    void normalizedGate()
    {
        QCOMPARE(normalized(GateProps{GType::NOT, 5}).numInputs, 1);
        QCOMPARE(normalized(GateProps{GType::AND, 1}).numInputs, 2);
        QCOMPARE(normalized(GateProps{GType::AND, 0}).numInputs, 2);
        QCOMPARE(normalized(GateProps{GType::XOR, 6}).numInputs, 6);
        QCOMPARE(normalized(GateProps{GType::NOR, 3}).type, GType::NOR);
    }

    void normalizedMuxAndDisplay()
    {
        QCOMPARE(normalized(MuxProps{MType::MUX, 0}).numInputs, 2);
        QCOMPARE(normalized(MuxProps{MType::MUX, 6}).numInputs, 6);
        QCOMPARE(normalized(DisplayProps{0, -1}), (DisplayProps{1, 0}));
        QCOMPARE(normalized(DisplayProps{4, 3}), (DisplayProps{4, 3}));
    }

    void normalizedSource()
    {
        // A list or a constant always has enough pins for its largest value; asking for more is fine
        QCOMPARE(normalized(SourceProps::list({1, 2}, 1)).bits, 2);
        QCOMPARE(normalized(SourceProps::list({1, 2}, 6)).bits, 6);
        QCOMPARE(normalized(SourceProps::list({300}, 2)).bits, 9);
        SourceProps constant;
        constant.mode = SourceMode::Constant;
        constant.constant = 0xFFFF'FFFF'FFFFull;
        constant.bits = 1;
        QCOMPARE(normalized(constant).bits, 48);
        // Other modes keep the pins they were given: a counter wraps at that width
        QCOMPARE(normalized(SourceProps::counter(5)).bits, 5);

        // An empty list would crash the painter and the simulator, so it becomes a single 0
        QCOMPARE(normalized(SourceProps::list({}, 1)).values, (QList<std::uint64_t>{0}));
        // Limits
        QCOMPARE(normalized(SourceProps::counter(0)).bits, 1);
        QCOMPARE(normalized(SourceProps::counter(200)).bits, SourceLimits::maxBits);
        QCOMPARE(normalized(SourceProps::counter(4, 0)).ticksPerStep, 1);
        QList<std::uint64_t> tooLong(SourceLimits::maxSteps + 5, 1);
        QCOMPARE(normalized(SourceProps::list(tooLong, 1)).values.size(), SourceLimits::maxSteps);

        // Waveforms: one pattern per pin, and no empty pattern
        SourceProps waves = SourceProps::waveform({true});
        waves.bits = 3;
        waves.waves.append(QList<bool>{});
        const SourceProps fixed = normalized(waves);
        QCOMPARE(fixed.waves.size(), 3);
        QCOMPARE(fixed.waves[1], QList<bool>{false});
        QCOMPARE(fixed.waves[2], QList<bool>{false});
    }

    void counterWrapsAtTheBitWidth()
    {
        SourceProps up = SourceProps::counter(8);
        up.start = 254;
        QCOMPARE(sequence(up, 4), (QList<std::uint64_t>{254, 255, 0, 1}));

        SourceProps down = SourceProps::counter(8);
        down.countDown = true;
        down.start = 1;
        QCOMPARE(sequence(down, 4), (QList<std::uint64_t>{1, 0, 255, 254}));

        // Step 3, a new value every 2 ticks
        SourceProps slow = SourceProps::counter(4, 2);
        slow.step = 3;
        QCOMPARE(sequence(slow, 8), (QList<std::uint64_t>{0, 0, 3, 3, 6, 6, 9, 9}));
        QCOMPARE(sourceValueAt(slow, 2 * 6), std::uint64_t(18 % 16));
    }

    void wideCountersCountThroughTheirWholeRange()
    {
        // The HDL compiler's inputs: before, 31-bit and 32-bit inputs got almost no values
        const SourceProps c31 = SourceProps::counter(31, 2);
        QCOMPARE(sourceValueAt(c31, 2 * 0x7FFF'FFFFull), std::uint64_t(0x7FFF'FFFF));
        QCOMPARE(sourceValueAt(c31, 2 * 0x8000'0000ull), std::uint64_t(0));

        const SourceProps c32 = SourceProps::counter(32, 2);
        QCOMPARE(sourceValueAt(c32, 2), std::uint64_t(1));
        QCOMPARE(sourceValueAt(c32, 2 * 0xFFFF'FFFFull), std::uint64_t(0xFFFF'FFFF));
        QCOMPARE(sourceValueAt(c32, 2 * 0x1'0000'0000ull), std::uint64_t(0));

        SourceProps c64 = SourceProps::counter(64);
        c64.start = ~std::uint64_t{0};
        QCOMPARE(sequence(c64, 2), (QList<std::uint64_t>{~std::uint64_t{0}, 0}));
    }

    void grayCodeChangesOnePinPerStep()
    {
        SourceProps gray = SourceProps::counter(3);
        gray.mode = SourceMode::GrayCode;
        QCOMPARE(sequence(gray, 9), (QList<std::uint64_t>{0, 1, 3, 2, 6, 7, 5, 4, 0}));

        SourceProps wide = SourceProps::counter(16);
        wide.mode = SourceMode::GrayCode;
        wide.start = 1000;
        for (std::uint64_t tick = 0; tick < 300; tick++) {
            const std::uint64_t changed = sourceValueAt(wide, tick) ^ sourceValueAt(wide, tick + 1);
            QCOMPARE(std::popcount(changed), 1);
        }
    }

    void walkingOneMovesAcrossThePins()
    {
        SourceProps walk = SourceProps::counter(4);
        walk.mode = SourceMode::WalkingOne;
        QCOMPARE(sequence(walk, 5), (QList<std::uint64_t>{1, 2, 4, 8, 1}));
        walk.countDown = true;
        QCOMPARE(sequence(walk, 5), (QList<std::uint64_t>{8, 4, 2, 1, 8}));
        walk.bits = 64;
        walk.countDown = false;
        QCOMPARE(sourceValueAt(walk, 63), std::uint64_t{1} << 63);
    }

    void randomIsRepeatableAndUsesEveryBit()
    {
        SourceProps random = SourceProps::counter(12);
        random.mode = SourceMode::Random;
        random.seed = 7;
        const QList<std::uint64_t> first = sequence(random, 500);
        QCOMPARE(sequence(random, 500), first);                          // the same on every run
        std::uint64_t seen = 0;
        for (const std::uint64_t value : first) {
            QVERIFY(value < (1u << 12));
            seen |= value;
        }
        QCOMPARE(seen, std::uint64_t(0xFFF));                             // every pin is 1 at some point
        QVERIFY(QSet<std::uint64_t>(first.begin(), first.end()).size() > 400);

        random.seed = 8;
        QVERIFY(sequence(random, 500) != first);                          // another seed, other numbers

        random.bits = 64;
        bool highBit = false;
        for (const std::uint64_t value : sequence(random, 100)) highBit |= (value >> 63) != 0;
        QVERIFY(highBit);
    }

    void listRepeatsAndHoldsEachValue()
    {
        SourceProps list = SourceProps::list({5, 6, 7}, 3);
        list.ticksPerStep = 2;
        QCOMPARE(sequence(list, 8), (QList<std::uint64_t>{5, 5, 6, 6, 7, 7, 5, 5}));
    }

    void eachPinRepeatsItsOwnWaveform()
    {
        SourceProps waves = SourceProps::waveform({false, true});   // pin 0: 0 1 0 1 ...
        waves.bits = 2;
        waves.waves.append(QList<bool>{true, true, false});           // pin 1: 1 1 0 1 1 0 ...
        QCOMPARE(sequence(waves, 6), (QList<std::uint64_t>{2, 3, 0, 3, 2, 1}));
    }

    void withoutRepeatTheLastStepStays()
    {
        // A reset pulse: 1 for two steps, then 0 for good
        SourceProps pulse = SourceProps::waveform({true, true, false});
        pulse.repeat = false;
        QCOMPARE(sequence(pulse, 6), (QList<std::uint64_t>{1, 1, 0, 0, 0, 0}));

        // Each pin stops on its own last step; a list stops on its last value
        SourceProps waves = SourceProps::waveform({false, true});
        waves.bits = 2;
        waves.waves.append(QList<bool>{true, true, false});
        waves.repeat = false;
        QCOMPARE(sequence(waves, 5), (QList<std::uint64_t>{2, 3, 1, 1, 1}));
        SourceProps list = SourceProps::list({4, 7, 9}, 4);
        list.repeat = false;
        list.ticksPerStep = 2;
        QCOMPARE(sequence(list, 8), (QList<std::uint64_t>{4, 4, 7, 7, 9, 9, 9, 9}));   // each value for 2 ticks
        QVERIFY(SourceProps{}.repeat);                                   // repeating is the default
    }

    void constantAlwaysOutputsItsValue()
    {
        SourceProps constant;
        constant.mode = SourceMode::Constant;
        constant.bits = 8;
        constant.constant = 0xA5;
        constant.ticksPerStep = 3;
        QCOMPARE(sequence(constant, 4), (QList<std::uint64_t>{0xA5, 0xA5, 0xA5, 0xA5}));
    }

    void defaults()
    {
        QCOMPARE(GateProps{}, (GateProps{GType::AND, 2}));
        QCOMPARE(RegisterProps{}, (RegisterProps{RType::D, true, true}));
        const SourceProps source;
        QCOMPARE(source.mode, SourceMode::Counter);
        QCOMPARE(normalized(source), source);
    }

    void titles()
    {
        QCOMPARE(itemTitle(GateProps{GType::NAND, 2}), u"NAND gate"_s);
        QCOMPARE(itemTitle(GateProps{GType::NOT, 1}), u"NOT gate"_s);
        QCOMPARE(itemTitle(RegisterProps{RType::D, true, true}), u"D flip-flop"_s);
        QCOMPARE(itemTitle(RegisterProps{RType::JK, false, false}), u"JK latch"_s);
        QCOMPARE(itemTitle(MuxProps{}), u"Multiplexer"_s);
        QCOMPARE(itemTitle(SourceProps::waveform({false})), u"Waveform source"_s);
        QCOMPARE(itemTitle(SourceProps::list({1}, 1)), u"List source"_s);
        QCOMPARE(itemTitle(SourceProps::counter(4)), u"Counter source"_s);
        QCOMPARE(itemTitle(DisplayProps{}), u"Display"_s);
    }

    void kindsMatchTheVariantOrder()
    {
        // The scene's tool table and the panel's pages are indexed by these
        QCOMPARE(itemKind<GateProps>, ItemProps(GateProps{}).index());
        QCOMPARE(itemKind<RegisterProps>, ItemProps(RegisterProps{}).index());
        QCOMPARE(itemKind<MuxProps>, ItemProps(MuxProps{}).index());
        QCOMPARE(itemKind<SourceProps>, ItemProps(SourceProps{}).index());
        QCOMPARE(itemKind<DisplayProps>, ItemProps(DisplayProps{}).index());
        QCOMPARE(itemKind<BlockProps>, ItemProps(BlockProps{}).index());
        QCOMPARE(itemKindCount, std::size_t(6));
    }
};

QTEST_GUILESS_MAIN(TestItemProps)
#include "tst_itemprops.moc"
