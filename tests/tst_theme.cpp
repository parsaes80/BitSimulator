// Light and dark mode: the colors and style sheet in Theme.cpp, the theme button, and the canvas dots.

#include "TestSupport.h"
#include "Theme.h"
#include <QElapsedTimer>
#include <QStyleHints>

using namespace Qt::StringLiterals;

using namespace TestSupport;

namespace {

QImage iconImage(const QIcon& icon)
{
    return icon.pixmap(QSize(18, 18), 1.0).toImage();
}

} // namespace

class TestTheme : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }

    //===================== The colors and the style sheet in Theme.cpp ========================

    void theStyleSheetNamesOnlyThemeColors_data()
    {
        QTest::addColumn<ColorScheme>("scheme");
        QTest::newRow("dark") << ColorScheme::Dark;
        QTest::newRow("light") << ColorScheme::Light;
    }

    void theStyleSheetNamesOnlyThemeColors()
    {
        QFETCH(ColorScheme, scheme);
        QTest::failOnWarning(QRegularExpression(u"style ?sheet|Theme\\.cpp"_s, QRegularExpression::CaseInsensitiveOption));
        const QString sheet = themeStyleSheet(themeFor(scheme));
        QVERIFY(!sheet.isEmpty());
        QVERIFY2(!sheet.contains(u'@'), qPrintable(sheet.mid(sheet.indexOf(u'@'), 40)));   // every @name was a Theme color
        QVERIFY(sheet.contains(u"CircuitCanvas"_s));

        // Qt parses it without complaint
        QWidget widget;
        widget.setStyleSheet(sheet);
        auto* button = new QPushButton(u"x"_s, &widget);
        widget.ensurePolished();
        button->ensurePolished();
    }

    void theSchemesHaveTheirOwnColors()
    {
        const Theme& dark = themeFor(ColorScheme::Dark);
        const Theme& light = themeFor(ColorScheme::Light);
        QVERIFY(dark.canvas.lightness() < 40);
        QVERIFY(light.canvas.lightness() > 200);
        QVERIFY(dark.text.lightness() > light.text.lightness());
        QVERIFY(dark.wire != light.wire);
        QVERIFY(dark.andGate != light.andGate);
        QCOMPARE(dark.accent, light.accent);                             // teal in both
        QVERIFY(themeStyleSheet(dark) != themeStyleSheet(light));
        QVERIFY(themeStyleSheet(dark).contains(dark.window.name()));
        QVERIFY(themeStyleSheet(light).contains(light.window.name()));
    }

    void aSourceMovesFromGrayToTealWithItsOnes_data()
    {
        QTest::addColumn<ColorScheme>("scheme");
        QTest::newRow("dark") << ColorScheme::Dark;
        QTest::newRow("light") << ColorScheme::Light;
    }

    void aSourceMovesFromGrayToTealWithItsOnes()
    {
        QFETCH(ColorScheme, scheme);
        const Theme& theme = themeFor(scheme);
        QCOMPARE(theme.sourceOutline(0), theme.source);                     // all pins 0: gray
        QCOMPARE(theme.sourceOutline(1), theme.sourceLit);                  // all 1: teal, like a wire carrying 1
        QCOMPARE(theme.sourceFill(0), theme.fillFor(theme.source));
        QCOMPARE(theme.sourceFill(1), theme.sourceLit);
        QVERIFY(theme.source.saturation() < 40);                            // gray, not a color

        // In a straight line: each pin at 1 moves it the same step closer to teal
        const QColor lit = theme.sourceLit;
        const auto distance = [&](const QColor& c) {
            return std::abs(c.red() - lit.red()) + std::abs(c.green() - lit.green()) + std::abs(c.blue() - lit.blue());
        };
        const QColor half = theme.sourceOutline(0.5);
        QVERIFY(std::abs(half.red() - (theme.source.red() + lit.red()) / 2) <= 1);
        QVERIFY(std::abs(half.green() - (theme.source.green() + lit.green()) / 2) <= 1);
        int previous = distance(theme.sourceFill(0));
        for (const double ones : {0.25, 0.5, 0.75, 1.0}) {
            const int now = distance(theme.sourceFill(ones));
            QVERIFY(now < previous);
            previous = now;
        }

        // The number stays readable: light text on the gray body, the other text color on solid teal
        QCOMPARE(theme.textOn(theme.sourceFill(0)), theme.componentText);
        QCOMPARE(theme.textOn(theme.sourceFill(1)), theme.canvas);
    }

    void aSourceIsPaintedByHowManyPinsAreOne()
    {
        // A 4-pin source holding 3: two of four pins are 1, so its body is halfway to teal
        CircuitScene scene;
        auto* source = dynamic_cast<SourceItem*>(scene.addComponent(SourceProps::constantOf(3, 4), QPointF(80000, 50000)));
        QVERIFY(source);
        const QRectF body = source->sceneBoundingRect();
        const auto bodyColor = [&] {
            QImage image(int(body.width()), int(body.height()), QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            scene.render(&painter, QRectF(image.rect()), body);
            painter.end();
            // Inside the body (its shape less the outline), near its bottom-left corner: away from the
            // number and the pins
            const QRectF inside = source->shape().boundingRect().adjusted(2, 2, -2, -2);
            const QPointF at = source->mapToScene(inside.bottomLeft() + QPointF(6, -6)) - body.topLeft();
            return image.pixelColor(at.toPoint());
        };
        const auto near = [](const QColor& a, const QColor& b) {
            return std::abs(a.red() - b.red()) <= 3 && std::abs(a.green() - b.green()) <= 3 && std::abs(a.blue() - b.blue()) <= 3;
        };

        sim_values_visible = false;
        QVERIFY2(near(bodyColor(), currentTheme().sourceFill(0)), qPrintable(bodyColor().name()));   // not running: gray
        sim_values_visible = true;
        QVERIFY2(near(bodyColor(), currentTheme().sourceFill(0.5)), qPrintable(bodyColor().name()));
        sim_values_visible = false;
    }

    void theInvertingBubbleIsRed()
    {
        // A NAND gate: the gate is yellow, its bubble red
        CircuitScene scene;
        ComponentItem* nand = scene.addComponent(GateProps{GType::NAND, 2}, QPointF(80000, 50000));
        const QRectF area = nand->sceneBoundingRect();
        QImage image(area.size().toSize(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        scene.render(&painter, QRectF(image.rect()), area);
        painter.end();

        const double width = componentSize(nand->props()).width();
        const double bubble = std::min(10.0, width * 0.2);
        const QPointF centre = nand->mapToScene(QPointF(width / 2 + bubble / 2, 0)) - area.topLeft();
        const QColor inside = image.pixelColor(centre.toPoint());
        const QColor expected = currentTheme().fillFor(currentTheme().inversion);
        QVERIFY2(std::abs(inside.red() - expected.red()) <= 3 && std::abs(inside.green() - expected.green()) <= 3
                     && std::abs(inside.blue() - expected.blue()) <= 3,
                 qPrintable(inside.name() + u" instead of "_s + expected.name()));
    }

    void aComponentBodyIsTintedWithItsOutline()
    {
        Theme theme = themeFor(ColorScheme::Dark);
        theme.componentTint = 0;
        QCOMPARE(theme.fillFor(theme.andGate), theme.componentFill);
        theme.componentTint = 1;
        QCOMPARE(theme.fillFor(theme.andGate), theme.andGate);
        QCOMPARE(theme.gateColor(GType::NAND), theme.andGate);
        QCOMPARE(theme.gateColor(GType::XNOR), theme.xorGate);
    }

    //===================== The theme button ========================

    void startsInTheSystemSetting()
    {
        App app;
        QCOMPARE(app.window.colorScheme(), systemColorScheme());
    }

    void followsTheSystemUntilTheButtonIsUsed()
    {
        QStyleHints* hints = QGuiApplication::styleHints();
        hints->setColorScheme(Qt::ColorScheme::Light);                   // stands in for the user changing the system setting
        if (!QTest::qWaitFor([hints] { return hints->colorScheme() == Qt::ColorScheme::Light; }, 1000)) {
            hints->unsetColorScheme();
            QSKIP("This platform has no light/dark setting (e.g. offscreen). Run with BITSIM_SHOW_GUI=1 to include this test.");
        }

        {
            App app;
            QCOMPARE(app.window.colorScheme(), ColorScheme::Light);         // starts in the system's mode
            hints->setColorScheme(Qt::ColorScheme::Dark);
            QTRY_COMPARE(app.window.colorScheme(), ColorScheme::Dark);      // and follows it

            child<QPushButton>(&app.window, "themeButton")->click();       // the user picks light ...
            QCOMPARE(app.window.colorScheme(), ColorScheme::Light);
            hints->setColorScheme(Qt::ColorScheme::Light);
            hints->setColorScheme(Qt::ColorScheme::Dark);
            QTest::qWait(50);
            QCOMPARE(app.window.colorScheme(), ColorScheme::Light);         // ... and the system no longer overrides it
            app.window.setColorScheme(ColorScheme::Dark);
        }
        hints->unsetColorScheme();
    }

    void theButtonSwitchesTheWholeWindow()
    {
        App app;
        auto* button = child<QPushButton>(&app.window, "themeButton");
        app.window.setColorScheme(ColorScheme::Dark);
        const QColor darkCanvas = currentTheme().canvas;
        const QColor darkWire = currentTheme().wire;
        const QString darkSheet = app.window.styleSheet();
        QCOMPARE(iconImage(button->icon()), iconImage(colorSchemeIcon(ColorScheme::Dark)));   // the sun
        QVERIFY(button->toolTip().contains(u"light"_s));

        button->click();
        QCOMPARE(app.window.colorScheme(), ColorScheme::Light);
        QVERIFY(app.window.styleSheet() != darkSheet);
        QVERIFY(currentTheme().canvas != darkCanvas);                 // the canvas got its light colors
        QVERIFY(currentTheme().canvas.lightness() > 200);
        QVERIFY(currentTheme().wire != darkWire);
        QCOMPARE(iconImage(button->icon()), iconImage(colorSchemeIcon(ColorScheme::Light)));  // the moon
        QVERIFY(iconImage(colorSchemeIcon(ColorScheme::Light)) != iconImage(colorSchemeIcon(ColorScheme::Dark)));
        QVERIFY(button->toolTip().contains(u"dark"_s));
        QVERIFY(qApp->palette().color(QPalette::Window).lightness() > 200);

        button->click();
        QCOMPARE(app.window.colorScheme(), ColorScheme::Dark);
        QCOMPARE(app.window.styleSheet(), darkSheet);
        QCOMPARE(currentTheme().canvas, darkCanvas);
        QCOMPARE(currentTheme().wire, darkWire);
    }

    void switchingKeepsTheCircuitAndTheSimulation()
    {
        App app;
        auto* button = child<QPushButton>(&app.window, "themeButton");
        ComponentItem* gate = app.scene->addComponent(GateProps{}, app.centre);
        app.start->click();
        QCOMPARE(app.scene->simState(), SimState::Running);

        button->click();
        button->click();
        QCOMPARE(app.scene->simState(), SimState::Running);
        QVERIFY(app.scene->items().contains(gate));
        QCOMPARE(app.start->text(), u"Pause"_s);
        app.clear->click();
    }

    //===================== The dot grid ========================

    void theDotGridStaysCheapWhenZoomedOut()
    {
        // The whole 160000 x 100000 canvas in a small picture: without thinning out the dots this is billions of them
        CircuitScene scene;
        QImage image(400, 250, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        QElapsedTimer timer;
        timer.start();
        scene.render(&painter, QRectF(image.rect()), scene.sceneRect());
        QVERIFY2(timer.elapsed() < 2000, qPrintable(u"took %1 ms"_s.arg(timer.elapsed())));
    }

    void dotsAreDrawnWhenZoomedIn()
    {
        CircuitScene scene;
        QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        scene.render(&painter, QRectF(image.rect()), QRectF(1000, 1000, 200, 200));
        painter.end();

        int dotPixels = 0;
        for (int y = 0; y < image.height(); y++) {
            for (int x = 0; x < image.width(); x++) {
                if (image.pixelColor(x, y) != currentTheme().canvas) dotPixels++;
            }
        }
        QVERIFY(dotPixels > 50);                                         // about 100 dots in this area
    }
};

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
