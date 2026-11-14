// The small pure functions in ItemProps.h that the items and the properties panel rely on.

#include <QtTest>
#include "ItemProps.h"

class TestItemProps : public QObject {
    Q_OBJECT
private slots:
    void bitsNeeded_data()
    {
        QTest::addColumn<QList<int>>("values");
        QTest::addColumn<int>("bits");
        QTest::newRow("empty list") << QList<int>{} << 1;
        QTest::newRow("only zero") << QList<int>{0} << 1;
        QTest::newRow("one") << QList<int>{0, 1} << 1;
        QTest::newRow("two needs 2 bits") << QList<int>{2} << 2;
        QTest::newRow("default source 2,5") << QList<int>{2, 5} << 3;
        QTest::newRow("seven") << QList<int>{7, 1} << 3;
        QTest::newRow("eight") << QList<int>{8} << 4;
        QTest::newRow("255") << QList<int>{255} << 8;
        QTest::newRow("256") << QList<int>{3, 256} << 9;
        QTest::newRow("negative is treated as 0") << QList<int>{-4} << 1;
    }
    void bitsNeeded()
    {
        QFETCH(QList<int>, values);
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
        // A waveform always drives exactly one pin
        QCOMPARE(normalized(SourceProps{QList<bool>{true, false}, 7}).bits, 1);
        // An empty list would crash the painter and the simulator, so it becomes a single 0
        QCOMPARE(std::get<QList<bool>>(normalized(SourceProps{QList<bool>{}, 1}).values), QList<bool>{false});
        QCOMPARE(std::get<QList<int>>(normalized(SourceProps{QList<int>{}, 1}).values), QList<int>{0});
        // Values can ask for more pins than they need, but never fewer
        QCOMPARE(normalized(SourceProps{QList<int>{1, 2}, 1}).bits, 2);
        QCOMPARE(normalized(SourceProps{QList<int>{1, 2}, 6}).bits, 6);
        QCOMPARE(normalized(SourceProps{QList<int>{300}, 2}).bits, 9);
    }

    void defaults()
    {
        QCOMPARE(GateProps{}, (GateProps{GType::AND, 2}));
        QCOMPARE(RegisterProps{}, (RegisterProps{RType::D, true, true}));
        const SourceProps source;
        QCOMPARE(source.bits, ::bitsNeeded(std::get<QList<int>>(source.values)));
        QCOMPARE(normalized(source), source);
    }

    void titles()
    {
        QCOMPARE(itemTitle(GateProps{GType::NAND, 2}), QStringLiteral("NAND gate"));
        QCOMPARE(itemTitle(GateProps{GType::NOT, 1}), QStringLiteral("NOT gate"));
        QCOMPARE(itemTitle(RegisterProps{RType::D, true, true}), QStringLiteral("D flip-flop"));
        QCOMPARE(itemTitle(RegisterProps{RType::JK, false, false}), QStringLiteral("JK latch"));
        QCOMPARE(itemTitle(MuxProps{}), QStringLiteral("Multiplexer"));
        QCOMPARE(itemTitle(SourceProps{QList<bool>{false}, 1}), QStringLiteral("Waveform source"));
        QCOMPARE(itemTitle(SourceProps{QList<int>{1}, 1}), QStringLiteral("Value source"));
        QCOMPARE(itemTitle(DisplayProps{}), QStringLiteral("Display"));
    }

    void kindsMatchTheVariantOrder()
    {
        // The scene's tool table and the panel's pages are indexed by these
        QCOMPARE(itemKind<GateProps>, ItemProps(GateProps{}).index());
        QCOMPARE(itemKind<RegisterProps>, ItemProps(RegisterProps{}).index());
        QCOMPARE(itemKind<MuxProps>, ItemProps(MuxProps{}).index());
        QCOMPARE(itemKind<SourceProps>, ItemProps(SourceProps{}).index());
        QCOMPARE(itemKind<DisplayProps>, ItemProps(DisplayProps{}).index());
        QCOMPARE(itemKindCount, std::size_t(5));
    }
};

QTEST_GUILESS_MAIN(TestItemProps)
#include "tst_itemprops.moc"
