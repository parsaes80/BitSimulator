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
