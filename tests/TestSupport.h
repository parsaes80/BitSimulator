#pragma once


#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QDialog>
#include <functional>
#include <QTimer>
#include <QMessageBox>
#include <QTextCursor>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>

#include "CircuitCanvas.h"
#include "hdlcompiler.h"
#include "MainWindow.h"
#include "PropertiesPanel.h"

namespace TestSupport {

// Aborts with a clear message if it is missing
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

// A properties panel segment button, by its text
inline QToolButton* segment(QWidget* root, const QString& text)
{
    for (QToolButton* button : root->findChildren<QToolButton*>()) {
        if (button->text() == text) return button;
    }
    qFatal("Test setup: no segment button '%s'", qPrintable(text));
    return nullptr;
}

// Like the mouse: press on one pin, move, release on the other
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

// Like a user: replaces the text
inline void typeInto(QLineEdit* edit, const QString& text)
{
    edit->setFocus();
    edit->selectAll();
    QTest::keyClicks(edit, text);
}

// Like a user: setCurrentIndex alone doesn't emit activated()
inline void choose(QComboBox* combo, const QString& text)
{
    const int index = combo->findText(text);
    if (index < 0) qFatal("Test setup: combo box has no entry '%s'", qPrintable(text));
    combo->setCurrentIndex(index);
    combo->activated(index);
}

// QVERIFY2's message for a std::expected: QVERIFY2 evaluates it even on success, where error() is undefined (MSVC crashes)
template <typename T>
inline QByteArray errorText(const std::expected<T, QString>& result)
{
    return result ? QByteArray() : result.error().toLocal8Bit();
}

// The tools the Compile button needs
inline bool hdlToolsAvailable()
{
    using namespace Qt::StringLiterals;
    const ToolPaths& tools = toolPaths();
    return QFile::exists(tools.yosys) && QFile::exists(tools.dot) && QFile::exists(tools.sv2v);
}
// Answers the next modal dialog once it opens, as the user would
inline void whenDialogOpens(std::function<void(QWidget*)> answer)
{
    auto* poll = new QTimer;
    QObject::connect(poll, &QTimer::timeout, poll, [poll, answer] {
        QWidget* dialog = QApplication::activeModalWidget();
        if (!dialog) return;
        poll->deleteLater();
        poll->stop();
        answer(dialog);
    });
    poll->start(10);
}

inline void typeName(QWidget* dialog, const QString& name)
{
    auto* input = dialog->findChild<QLineEdit*>();
    QVERIFY(input);
    input->setText(name);
    static_cast<QDialog*>(dialog)->accept();
}

inline void pressButton(QWidget* dialog, QMessageBox::StandardButton button)
{
    auto* box = qobject_cast<QMessageBox*>(dialog);
    QVERIFY(box && box->button(button));
    box->button(button)->click();
}

// if (!hdlToolsAvailable()) QSKIP(MissingHdlTools);
inline constexpr const char* MissingHdlTools = "yosys, Graphviz dot or sv2v was not found in the working directory";

// The application window, with shortcuts to what the tests use
struct App {
    QTemporaryDir saves;      // each window gets its own saves folder
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
        window.setSavesDir(saves.path());
        window.setAskToSave(false);   // a modal box would stall the tests; the file tests turn it back on
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

    QString title() const { return child<QLabel>(panel, "propertiesTitle")->text(); }
    // Empty while something is selected
    QString nextComponent() const
    {
        const QLabel* label = child<QLabel>(panel, "propertiesNextTitle");
        return label->isVisible() ? label->text() : QString();
    }
    // Empty if none
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
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);   // items go with deleteLater()
        QCoreApplication::processEvents();
    }
};

}
