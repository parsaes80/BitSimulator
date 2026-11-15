#include "Theme.h"
#include <QApplication>
#include <QDebug>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyleHints>
#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

//===================== The colors ========================

Theme makeTheme(ColorScheme scheme)
{
    const auto pick = [scheme](QRgb dark, QRgb light) { return QColor::fromRgb(scheme == ColorScheme::Dark ? dark : light); };

    Theme t;
    //                           dark      light
    // Widgets
    t.window        = pick(0x0f1316, 0xe9edf0);
    t.panel         = pick(0x141a1e, 0xf2f5f7);
    t.field         = pick(0x0c1013, 0xffffff);
    t.button        = pick(0x1b2227, 0xffffff);
    t.buttonHover   = pick(0x222b31, 0xeef2f4);
    t.buttonPressed = pick(0x12171b, 0xe2e8eb);
    t.buttonChecked = pick(0x10302d, 0xd2f2ec);
    t.border        = pick(0x263036, 0xd5dce0);
    t.borderHover   = pick(0x33404a, 0xb4bfc5);
    t.text          = pick(0xe4ebee, 0x1a2328);
    t.mutedText     = pick(0x7a8890, 0x5f6e76);
    t.disabledText  = pick(0x4f5b62, 0xa4afb5);
    t.errorText     = pick(0xff9e80, 0xc2410c);
    t.accent        = pick(0x14b8a6, 0x14b8a6);
    t.accentHover   = pick(0x2dd4bf, 0x0d9488);
    t.accentPressed = pick(0x0f9488, 0x0b7f75);
    t.onAccent      = pick(0x04211e, 0x04211e);
    t.pause         = pick(0xe0a526, 0xe0a526);
    t.pauseHover    = pick(0xedb740, 0xedb740);
    t.onPause       = pick(0x1b1405, 0x1b1405);

    // Canvas
    t.canvas        = pick(0x0a0e11, 0xf7f9fa);
    t.dot           = pick(0x1e282e, 0xd3dade);
    t.componentFill = pick(0x141b1f, 0xffffff);
    t.componentTint = 0.5;
    t.componentText = pick(0xc3ced4, 0x3d4b53);
    t.andGate       = pick(0xe8b05a, 0xc4891d);
    t.orGate        = pick(0x5fd3a5, 0x13976d);
    t.xorGate       = pick(0xa78bfa, 0x7c4ddb);
    t.notGate       = pick(0xf27c7c, 0xd9474b);
    t.mux           = pick(0xf59e5b, 0xdd6f16);
    t.reg           = pick(0x7aa2f7, 0x3567d6);
    t.source        = pick(0xef4444, 0xc62828);
    t.display       = pick(0x67e8f9, 0x0e8fae);
    t.block         = pick(0xf0abfc, 0xa21caf);
    t.wire          = pick(0xc3ced4, 0x3d4b53);
    t.wireHigh      = pick(0x2dd4bf, 0x0d9488);
    t.wireLow       = pick(0x3b4a52, 0xc3cdd2);
    t.pin           = pick(0x4a5960, 0xa3b0b6);
    t.pinWired      = pick(0x14b8a6, 0x14b8a6);
    t.selection     = pick(0x2dd4bf, 0x0d9488);
    t.drag          = pick(0x7a8890, 0x5f6e76);
    t.snap          = pick(0xfde68a, 0xd98a06);
    return t;
}

const Theme* current = &themeFor(ColorScheme::Dark);

//===================== The style sheet ========================

// Written once; @name stands for that color of the theme, e.g. @accent for Theme::accent.
// Images are in theme/ (theme.qrc).
constexpr const char* SheetTemplate = R"qss(
QWidget {
    color: @text;
    selection-background-color: @accent;
    selection-color: @onAccent;
}

QMainWindow, QDialog, QMessageBox {
    background-color: @window;
}

QToolTip {
    background-color: @button;
    color: @text;
    border: 1px solid @border;
    border-radius: 4px;
    padding: 4px 6px;
}

QLabel[muted="true"] {
    color: @mutedText;
}

/* ---------- Tabs ---------- */

QTabWidget::pane {
    border: none;
    border-top: 1px solid @border;
    background-color: @window;
}

QTabBar {
    background-color: @window;
}

QTabBar::tab {
    background-color: transparent;
    color: @mutedText;
    padding: 7px 16px;
    border: none;
    border-bottom: 2px solid transparent;
}

QTabBar::tab:hover {
    color: @text;
}

QTabBar::tab:selected {
    color: @text;
    border-bottom: 2px solid @accent;
}

/* ---------- Buttons (the component buttons in the toolbar too) ---------- */

QPushButton {
    background-color: @button;
    color: @text;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 5px 10px;
}

QPushButton:hover {
    background-color: @buttonHover;
    border-color: @borderHover;
}

QPushButton:pressed {
    background-color: @buttonPressed;
}

QPushButton:checked {
    background-color: @buttonChecked;
    border: 1px solid @accent;
}

QPushButton:disabled {
    background-color: @panel;
    color: @disabledText;
}

/* Start / Pause: teal while it says Start, amber while it says Pause */
#startButton {
    background-color: @accent;
    border-color: @accent;
    color: @onAccent;
    font-weight: bold;
}

#startButton:hover {
    background-color: @accentHover;
    border-color: @accentHover;
}

#startButton:pressed {
    background-color: @accentPressed;
}

#startButton[running="true"] {
    background-color: @pause;
    border-color: @pause;
    color: @onPause;
}

#startButton[running="true"]:hover {
    background-color: @pauseHover;
    border-color: @pauseHover;
}

/* Light / dark mode: a round icon button showing the mode it switches to (MainWindow sets the icon) */
#themeButton {
    background-color: transparent;
    border: 1px solid transparent;
    border-radius: 18px;
    padding: 0px;
    icon-size: 18px;
}

#themeButton:hover {
    background-color: @buttonHover;
    border-color: @borderHover;
}

#themeButton:pressed {
    background-color: @buttonPressed;
}

/* ---------- Speed slider ---------- */

QSlider::groove:horizontal {
    height: 4px;
    background-color: @border;
    border-radius: 2px;
}

QSlider::sub-page:horizontal {
    background-color: @accent;
    border-radius: 2px;
}

QSlider::handle:horizontal {
    background-color: @text;
    width: 14px;
    margin: -5px 0;
    border-radius: 7px;
}

QSlider::handle:horizontal:hover {
    background-color: @accentHover;
}

/* ---------- Text fields, number boxes, drop-downs ---------- */

QLineEdit, QSpinBox, QComboBox {
    background-color: @field;
    color: @text;
    border: 1px solid @border;
    border-radius: 5px;
    padding: 3px 6px;
    min-height: 18px;
}

QLineEdit:hover, QSpinBox:hover, QComboBox:hover {
    border-color: @borderHover;
}

QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
    border-color: @accent;
}

QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled {
    color: @disabledText;
}

/* The text field inside a number box: the number box itself draws the border */
QSpinBox QLineEdit {
    background-color: transparent;
    border: none;
    padding: 0px;
    min-height: 0px;
}

/* The arrow images are in theme/ (theme.qrc) */
QSpinBox::up-button, QSpinBox::down-button {
    subcontrol-origin: border;
    width: 20px;
    border: none;
    background-color: transparent;
}

QSpinBox::up-button {
    subcontrol-position: top right;
}

QSpinBox::down-button {
    subcontrol-position: bottom right;
}

QSpinBox::up-arrow {
    image: url(:/theme/arrow-up.png);
}

QSpinBox::down-arrow, QComboBox::down-arrow {
    image: url(:/theme/arrow-down.png);
}

QSpinBox::up-arrow:disabled, QSpinBox::up-arrow:off {
    image: url(:/theme/arrow-up-disabled.png);
}

QSpinBox::down-arrow:disabled, QSpinBox::down-arrow:off, QComboBox::down-arrow:disabled {
    image: url(:/theme/arrow-down-disabled.png);
}

QComboBox::drop-down {
    subcontrol-origin: border;
    subcontrol-position: center right;
    width: 22px;
    border: none;
}

QComboBox QAbstractItemView {
    background-color: @panel;
    border: 1px solid @border;
    outline: none;
    padding: 2px;
}

QCheckBox {
    spacing: 8px;
}

QCheckBox::indicator {
    width: 14px;
    height: 14px;
    background-color: @field;
    border: 1px solid @borderHover;
    border-radius: 4px;
}

QCheckBox::indicator:hover {
    border-color: @accent;
}

QCheckBox::indicator:checked {
    background-color: @accent;
    border-color: @accent;
    image: url(:/theme/check.png);
}

QCheckBox::indicator:checked:hover {
    background-color: @accentHover;
    border-color: @accentHover;
}

QCheckBox::indicator:disabled {
    background-color: @panel;
    border-color: @border;
}

QCheckBox:disabled {
    color: @disabledText;
}

/* ---------- HDL editor ---------- */

QPlainTextEdit {
    background-color: @field;
    color: @text;
    border: none;
    padding: 6px 8px;
}

/* ---------- Scroll bars ---------- */

QScrollBar:vertical {
    background-color: @canvas;
    width: 10px;
    margin: 0;
}

QScrollBar:horizontal {
    background-color: @canvas;
    height: 10px;
    margin: 0;
}

QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background-color: @border;
    border-radius: 3px;
    margin: 2px;
}

QScrollBar::handle:vertical {
    min-height: 30px;
}

QScrollBar::handle:horizontal {
    min-width: 30px;
}

QScrollBar::handle:hover {
    background-color: @borderHover;
}

QScrollBar::add-line, QScrollBar::sub-line {
    width: 0;
    height: 0;
    border: none;
}

QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
}

QAbstractScrollArea::corner {
    background-color: @canvas;
}

/* ---------- Properties panel ---------- */

/* The properties panel floats over the top-right of the canvas in this rounded box */
#propertiesFrame {
    background-color: @panel;
    border: 1px solid @border;
    border-radius: 10px;
}

#propertiesScroll, #propertiesScroll QWidget#qt_scrollarea_viewport, #propertiesPanel {
    background-color: transparent;
}

#propertiesSeparator {
    color: @border;
}

#propertiesMessage {
    color: @errorText;
}

#propertiesLock {
    background-color: @field;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 8px;
}

/* The joined buttons, e.g. MUX | DEMUX */
#segmented QToolButton {
    padding: 5px 6px;
    background-color: @button;
    color: @text;
    border: 1px solid @border;
    border-left-width: 0px;
}

#segmented QToolButton:hover {
    background-color: @buttonHover;
}

#segmented QToolButton[segment="first"] {
    border-left-width: 1px;
    border-top-left-radius: 5px;
    border-bottom-left-radius: 5px;
}

#segmented QToolButton[segment="last"] {
    border-top-right-radius: 5px;
    border-bottom-right-radius: 5px;
}

#segmented QToolButton:checked {
    background-color: @accent;
    border-color: @accent;
    color: @onAccent;
    font-weight: bold;
}

#segmented QToolButton:disabled {
    color: @disabledText;
}

/* Over the canvas inside a block: the blocks opened, e.g. top > core (RISCV) > alu (ALU) */
#blockBreadcrumb {
    background-color: @panel;
    border: 1px solid @border;
    border-radius: 8px;
}

#blockBreadcrumb QToolButton {
    padding: 3px 6px;
    background-color: transparent;
    color: @text;
    border: none;
    border-radius: 4px;
}

#blockBreadcrumb QToolButton:hover {
    background-color: @buttonHover;
}

#blockBreadcrumb QToolButton:disabled {
    color: @block;
    font-weight: bold;
}

#blockBreadcrumb QLabel {
    color: @disabledText;
}

/* Lists, such as the files on the Compile Dir tab; the checkboxes look like the other checkboxes */
QListWidget {
    background-color: @field;
    color: @text;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 4px;
    outline: none;
}

QListWidget::item {
    padding: 4px 6px;
    border-radius: 4px;
}

QListWidget::item:hover {
    background-color: @button;
}

QListWidget::item:selected {
    background-color: @buttonChecked;
    color: @text;
}

QListWidget::indicator {
    width: 14px;
    height: 14px;
    background-color: @field;
    border: 1px solid @borderHover;
    border-radius: 4px;
}

QListWidget::indicator:checked {
    background-color: @accent;
    border-color: @accent;
    image: url(:/theme/check.png);
}

/* The pin buttons of a waveform source: the checked one is the pin the step editor shows */
QToolButton[wavePin="true"] {
    padding: 3px 0px;
    background-color: @button;
    color: @text;
    border: 1px solid @border;
    border-radius: 5px;
}

QToolButton[wavePin="true"]:hover {
    background-color: @buttonHover;
}

QToolButton[wavePin="true"]:checked {
    background-color: @accent;
    border-color: @accent;
    color: @onAccent;
    font-weight: bold;
}

/* The numbers of a list source */
#sourceValues {
    background-color: @field;
    border: 1px solid @border;
    border-radius: 5px;
    padding: 2px 4px;
}

#sourceValues:focus {
    border-color: @accent;
}

/* ---------- Canvas: everything on it is painted in code, with the canvas colors of Theme ---------- */

CircuitCanvas {
    border: none;
    background-color: @canvas;
}
)qss";

// The colors the style sheet can name
const std::pair<QLatin1StringView, QColor Theme::*> SheetColors[] = {
    {"window"_L1, &Theme::window},
    {"panel"_L1, &Theme::panel},
    {"field"_L1, &Theme::field},
    {"button"_L1, &Theme::button},
    {"buttonHover"_L1, &Theme::buttonHover},
    {"buttonPressed"_L1, &Theme::buttonPressed},
    {"buttonChecked"_L1, &Theme::buttonChecked},
    {"border"_L1, &Theme::border},
    {"borderHover"_L1, &Theme::borderHover},
    {"text"_L1, &Theme::text},
    {"mutedText"_L1, &Theme::mutedText},
    {"disabledText"_L1, &Theme::disabledText},
    {"errorText"_L1, &Theme::errorText},
    {"accent"_L1, &Theme::accent},
    {"accentHover"_L1, &Theme::accentHover},
    {"accentPressed"_L1, &Theme::accentPressed},
    {"onAccent"_L1, &Theme::onAccent},
    {"pause"_L1, &Theme::pause},
    {"pauseHover"_L1, &Theme::pauseHover},
    {"onPause"_L1, &Theme::onPause},
    {"canvas"_L1, &Theme::canvas},
    {"block"_L1, &Theme::block},
};

} // namespace

//===================== Theme ========================

QColor Theme::fillFor(const QColor& outline) const
{
    const double t = std::clamp(componentTint, 0.0, 1.0);
    const auto mix = [t](int from, int to) { return static_cast<int>(std::lround(from + (to - from) * t)); };
    return QColor(mix(componentFill.red(), outline.red()), mix(componentFill.green(), outline.green()),
                  mix(componentFill.blue(), outline.blue()));
}

QColor Theme::gateColor(GType type) const
{
    switch (type) {
    case GType::AND: case GType::NAND: return andGate;
    case GType::OR:  case GType::NOR:  return orGate;
    case GType::XOR: case GType::XNOR: return xorGate;
    case GType::NOT: return notGate;
    }
    return componentText;
}

const Theme& themeFor(ColorScheme scheme)
{
    static const Theme dark = makeTheme(ColorScheme::Dark);
    static const Theme light = makeTheme(ColorScheme::Light);
    return scheme == ColorScheme::Dark ? dark : light;
}

const Theme& currentTheme()
{
    return *current;
}

void setCurrentTheme(ColorScheme scheme)
{
    current = &themeFor(scheme);
}

//===================== Style sheet, palette, icons ========================

QString themeStyleSheet(const Theme& theme)
{
    static const QRegularExpression colorName(u"@([A-Za-z]+)"_s);
    const QString sheet = QString::fromLatin1(SheetTemplate).trimmed();

    QString result;
    result.reserve(sheet.size());
    qsizetype copied = 0;
    for (auto it = colorName.globalMatch(sheet); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QStringView name = match.capturedView(1);
        const auto entry = std::ranges::find_if(SheetColors, [name](const auto& color) { return color.first == name; });
        if (entry == std::end(SheetColors)) {
            qWarning() << "Theme.cpp: the style sheet names a color Theme does not have:" << match.captured();
            continue;   // left as it is: Qt ignores the rule
        }
        result += QStringView(sheet).mid(copied, match.capturedStart() - copied);
        result += (theme.*entry->second).name();
        copied = match.capturedEnd();
    }
    result += QStringView(sheet).mid(copied);
    return result;
}

void applyBaseStyle(QApplication& app)
{
    app.setStyle(QStyleFactory::create(u"Fusion"_s));
}

void applyPalette(QApplication& app, const Theme& theme)
{
    const bool dark = (theme.canvas.lightness() < 128);

    QPalette palette;
    palette.setColor(QPalette::Window, theme.window);
    palette.setColor(QPalette::WindowText, theme.text);
    palette.setColor(QPalette::Base, theme.field);
    palette.setColor(QPalette::AlternateBase, theme.window);
    palette.setColor(QPalette::Text, theme.text);
    palette.setColor(QPalette::PlaceholderText, theme.mutedText);
    palette.setColor(QPalette::Button, theme.button);
    palette.setColor(QPalette::ButtonText, theme.text);
    palette.setColor(QPalette::BrightText, theme.accent);
    palette.setColor(QPalette::ToolTipBase, theme.button);
    palette.setColor(QPalette::ToolTipText, theme.text);
    palette.setColor(QPalette::Highlight, theme.accent);
    palette.setColor(QPalette::HighlightedText, theme.onAccent);
    palette.setColor(QPalette::Link, theme.accent);
    palette.setColor(QPalette::Light, theme.button);
    palette.setColor(QPalette::Midlight, theme.border);
    palette.setColor(QPalette::Mid, theme.border);
    palette.setColor(QPalette::Dark, theme.border.darker(dark ? 150 : 120));
    palette.setColor(QPalette::Shadow, dark ? QColor(Qt::black) : theme.border.darker(160));

    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, theme.disabledText);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, theme.border);

    app.setPalette(palette);
}

QIcon colorSchemeIcon(ColorScheme scheme)
{
    // QIcon also loads the @2x image next to it, for high-DPI screens
    return QIcon(scheme == ColorScheme::Dark ? u":/theme/sun.png"_s : u":/theme/moon.png"_s);
}

ColorScheme systemColorScheme()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light ? ColorScheme::Light : ColorScheme::Dark;
}
