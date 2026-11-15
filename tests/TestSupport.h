#pragma once

// Shared helpers for the BitSimulator tests.

#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>

#include "CircuitCanvas.h"
#include "MainWindow.h"
#include "PropertiesPanel.h"

namespace TestSupport {

// Finds a child widget by object name; aborts the test program with a clear message if it is missing.
template <typename T>
T* child(QObject* root, const char* name)
{
    T* found = root->findChild<T*>(QLatin1String(name));
    if (!found) qFatal("Test setup: no %s named '%s'", T::staticMetaObject.className(), name);
    return found;
}

// A visible label whose text contains `part`, or null.
inline QLabel* labelWith(QWidget* root, const QString& part)
{
    for (QLabel* label : root->findChildren<QLabel*>()) {
        if (label->isVisible() && label->text().contains(part)) return label;
    }
    return nullptr;
}

// One of the joined "segment" buttons in the properties panel, found by its text.
inline QToolButton* segment(QWidget* root, const QString& text)
{
    for (QToolButton* button : root->findChildren<QToolButton*>()) {
        if (button->text() == text) return button;
    }
    qFatal("Test setup: no segment button '%s'", qPrintable(text));
    return nullptr;
}

// Draws a wire the way the mouse does: press on one pin, move, release on the other.
inline WireItem* connectPorts(CircuitScene* scene, PortItem* from, PortItem* to)
{
    scene->startWireConnection(from->mapToScene(QPointF(0, 0)));
    scene->updateWireConnection(to->mapToScene(QPointF(0, 0)));
    scene->finishWireConnection(to->mapToScene(QPointF(0, 0)));

    PortItem* input = (to->getPortType() == PortType::IN) ? to : from;
    for (WireItem* wire : input->getConnections()) {
        if ((wire->getStartPort() == from && wire->getEndPort() == to) ||
            (wire->getStartPort() == to && wire->getEndPort() == from)) return wire;
    }
    return nullptr;
}

template <typename T>
QList<T*> itemsOfType(const QGraphicsScene* scene)
{
    QList<T*> found;
    for (QGraphicsItem* item : scene->items()) {
        if (auto* typed = dynamic_cast<T*>(item)) found.append(typed);
    }
    return found;
}

// Types into a line edit the way a user does (replacing what is there).
inline void typeInto(QLineEdit* edit, const QString& text)
{
    edit->setFocus();
    edit->selectAll();
    QTest::keyClicks(edit, text);
}

// Picks an entry of a combo box the way a user does (setCurrentIndex alone does not emit activated()).
inline void choose(QComboBox* combo, const QString& text)
{
    const int index = combo->findText(text);
    if (index < 0) qFatal("Test setup: combo box has no entry '%s'", qPrintable(text));
    combo->setCurrentIndex(index);
    combo->activated(index);
}

// The HDL tests need the same external tools as the app's Compile button.
inline bool hdlToolsAvailable()
{
    using namespace Qt::StringLiterals;
#ifdef Q_OS_WIN
    return QFile::exists(u"script.bat"_s) && QFile::exists(u"dot.exe"_s) && QFile::exists(u"sv2v.exe"_s);
#else
    return QFile::exists(u"yosys_bundle/bin/yosys"_s) && QFile::exists(u"dot"_s) && QFile::exists(u"sv2v"_s);
#endif
}
// Skip message for a test that needs them: if (!hdlToolsAvailable()) QSKIP(MissingHdlTools);
inline constexpr const char* MissingHdlTools = "yosys, Graphviz dot or sv2v was not found in the working directory";

// The whole application window with shortcuts to the parts the tests poke at.
struct App {
    MainWindow window;
    CircuitCanvas* canvas = nullptr;
    CircuitScene* scene = nullptr;
    PropertiesPanel* panel = nullptr;
    QPushButton* start = nullptr;
    QPushButton* clear = nullptr;
    QPushButton* compile = nullptr;
    QPlainTextEdit* editor = nullptr;
    QStackedWidget* pages = nullptr;
    QPointF centre;

    App()
    {
        window.resize(1280, 760);
        window.show();
        if (!QTest::qWaitForWindowExposed(&window)) qFatal("Test setup: the main window never appeared");

        canvas = child<CircuitCanvas>(&window, "camera");
        scene = canvas->getScene();
        panel = child<PropertiesPanel>(&window, "propertiesPanel");
        start = child<QPushButton>(&window, "startButton");
        clear = child<QPushButton>(&window, "clearButton");
        compile = child<QPushButton>(&window, "compileButton");
        editor = child<QPlainTextEdit>(&window, "hdlEditor");
        pages = child<QStackedWidget>(panel, "propertiesPages");
        centre = canvas->mapToScene(canvas->viewport()->rect().center());
    }

    QPushButton* tool(const char* name) { return child<QPushButton>(&window, name); }

    // Header of the properties panel
    QString title() const { return child<QLabel>(panel, "propertiesTitle")->text(); }
    // Name of the component the panel offers to place next; empty while something is selected
    QString nextComponent() const
    {
        const QLabel* label = child<QLabel>(panel, "propertiesNextTitle");
        return label->isVisible() ? label->text() : QString();
    }
    // Error shown under the editors; empty if none
    QString message() const
    {
        const QLabel* label = child<QLabel>(panel, "propertiesMessage");
        return label->isVisible() ? label->text() : QString();
    }

    template <typename T> T* editor_(const char* name) const { return child<T>(panel, name); }

    QPoint viewPos(const QPointF& scenePos) const { return canvas->mapFromScene(scenePos); }
    QPoint viewPos(const QGraphicsItem* item) const { return canvas->mapFromScene(item->scenePos()); }

    void select(QGraphicsItem* item)
    {
        scene->clearSelection();
        item->setSelected(true);
        QCoreApplication::processEvents();
    }

    void pressDelete()
    {
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Delete);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);   // items are removed with deleteLater()
        QCoreApplication::processEvents();
    }
};

} // namespace TestSupport
