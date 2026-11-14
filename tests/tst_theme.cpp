// Light and dark mode: the style sheet table in BitSimulator.ui, the theme button, and the canvas dots.

#include "TestSupport.h"
#include "Theme.h"
#include <QElapsedTimer>
#include <QStyleHints>

using namespace TestSupport;

namespace {

// The value of "qproperty-<name>: #rrggbb" in a style sheet
QString canvasProperty(const QString& sheet, const QString& name)
{
    const QRegularExpression pattern(QStringLiteral("qproperty-%1:\\s*(#[0-9a-fA-F]{6})").arg(name));
    return pattern.match(sheet).captured(1);
}

QString uiStyleSheet()
{
    QMainWindow window;
    Ui::BitSimulatorClass ui;
    ui.setupUi(&window);
    return window.styleSheet();
}

} // namespace

class TestTheme : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }

    //===================== The table in BitSimulator.ui ========================

    void everyColorInTheUiHasALightModeColor()
    {
        const ThemeSheets sheets(uiStyleSheet());
        QVERIFY2(sheets.unpairedColors().isEmpty(),
                 qPrintable(QStringLiteral("add these to the table at the top of the style sheet: ")
                            + sheets.unpairedColors().join(QStringLiteral(", "))));

        const QString dark = sheets.sheet(ColorScheme::Dark), light = sheets.sheet(ColorScheme::Light);
        QVERIFY(!canvasProperty(dark, QStringLiteral("canvasColor")).isEmpty());
        QVERIFY(canvasProperty(dark, QStringLiteral("canvasColor")) != canvasProperty(light, QStringLiteral("canvasColor")));
        QVERIFY(canvasProperty(dark, QStringLiteral("wireColor")) != canvasProperty(light, QStringLiteral("wireColor")));
    }

    void colorsAreSwappedInOnePass()
    {
        // #111111 is a light color and also a dark one; it must not be swapped twice
        const ThemeSheets sheets(QStringLiteral("/* table\n  #000000  #111111  a\n  #111111  #222222  b\n*/\n"
                                                "A { color: #000000; } B { color: #111111; } C { color: #ABCDEF; }"));
        const QString light = sheets.sheet(ColorScheme::Light);
        QVERIFY(light.contains(QStringLiteral("A { color: #111111; }")));
        QVERIFY(light.contains(QStringLiteral("B { color: #222222; }")));
        QVERIFY(light.contains(QStringLiteral("C { color: #ABCDEF; }")));          // not in the table: unchanged
        QCOMPARE(sheets.unpairedColors(), QStringList{QStringLiteral("#abcdef")});
        QCOMPARE(sheets.color(QStringLiteral("#000000"), ColorScheme::Light), QColor(QStringLiteral("#111111")));
        QCOMPARE(sheets.color(QStringLiteral("#000000"), ColorScheme::Dark), QColor(QStringLiteral("#000000")));
    }

    void onlyTheFirstCommentIsTheTable()
    {
        const ThemeSheets sheets(QStringLiteral("/*\n #000000 #ffffff\n*/\nA { color: #000000; }\n/*\n #000000 #ff0000\n*/"));
        QVERIFY(sheets.sheet(ColorScheme::Light).contains(QStringLiteral("A { color: #ffffff; }")));
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
        const QColor darkCanvas = canvasTheme().background;
        const QColor darkWire = canvasTheme().wire;
        const QString darkSheet = app.window.styleSheet();
        QCOMPARE(button->property("scheme").toString(), QStringLiteral("dark"));
        QVERIFY(button->toolTip().contains(QStringLiteral("light")));

        button->click();
        QCOMPARE(app.window.colorScheme(), ColorScheme::Light);
        QVERIFY(app.window.styleSheet() != darkSheet);
        QVERIFY(canvasTheme().background != darkCanvas);                 // the canvas got its light colors
        QVERIFY(canvasTheme().background.lightness() > 200);
        QVERIFY(canvasTheme().wire != darkWire);
        QCOMPARE(button->property("scheme").toString(), QStringLiteral("light"));
        QVERIFY(button->toolTip().contains(QStringLiteral("dark")));
        QVERIFY(qApp->palette().color(QPalette::Window).lightness() > 200);

        button->click();
        QCOMPARE(app.window.colorScheme(), ColorScheme::Dark);
        QCOMPARE(app.window.styleSheet(), darkSheet);
        QCOMPARE(canvasTheme().background, darkCanvas);
        QCOMPARE(canvasTheme().wire, darkWire);
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
        QCOMPARE(app.start->text(), QStringLiteral("Pause"));
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
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QStringLiteral("took %1 ms").arg(timer.elapsed())));
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
                if (image.pixelColor(x, y) != canvasTheme().background) dotPixels++;
            }
        }
        QVERIFY(dotPixels > 50);                                         // about 100 dots in this area
    }
};

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
