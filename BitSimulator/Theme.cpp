#include "Theme.h"
#include <QApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyleHints>

namespace {

const QRegularExpression& hexColor()
{
    static const QRegularExpression pattern(QStringLiteral("#[0-9a-fA-F]{6}\\b"));
    return pattern;
}

} // namespace

ThemeSheets::ThemeSheets(const QString& darkSheet)
    : m_dark(darkSheet)
{
    // The table lives in the first comment: lines that start with two colors
    const qsizetype headerEnd = darkSheet.startsWith(QLatin1String("/*")) ? darkSheet.indexOf(QLatin1String("*/")) : -1;
    const QString header = headerEnd < 0 ? QString() : darkSheet.left(headerEnd);
    static const QRegularExpression row(QStringLiteral("^\\s*(#[0-9a-fA-F]{6})\\s+(#[0-9a-fA-F]{6})\\b"),
                                        QRegularExpression::MultilineOption);
    for (auto it = row.globalMatch(header); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        m_toLight.insert(match.captured(1).toLower(), match.captured(2).toLower());
    }

    // Swap every color in one pass, so a light color that is also some dark color is not swapped again
    qsizetype copied = 0;
    for (auto it = hexColor().globalMatch(darkSheet); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString dark = match.captured().toLower();
        m_light += QStringView(darkSheet).mid(copied, match.capturedStart() - copied);
        copied = match.capturedEnd();

        const auto light = m_toLight.constFind(dark);
        if (light != m_toLight.constEnd()) {
            m_light += *light;
            continue;
        }
        m_light += match.captured();
        if (match.capturedStart() > headerEnd && !m_unpaired.contains(dark)) m_unpaired.append(dark);
    }
    m_light += QStringView(darkSheet).mid(copied);
}

QColor ThemeSheets::color(const QString& darkColor, ColorScheme scheme) const
{
    if (scheme == ColorScheme::Light) {
        if (const auto light = m_toLight.constFind(darkColor.toLower()); light != m_toLight.constEnd()) return QColor(*light);
    }
    return QColor(darkColor);
}

void applyBaseStyle(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
}

void applyPalette(QApplication& app, const ThemeSheets& sheets, ColorScheme scheme)
{
    // These are colors of the style sheet in BitSimulator.ui, named by their dark value
    const auto color = [&](const char* dark) { return sheets.color(QLatin1String(dark), scheme); };
    const QColor window = color("#0f1316"), field = color("#0c1013"), raised = color("#1b2227");
    const QColor text = color("#e4ebee"), muted = color("#7a8890"), disabled = color("#4f5b62");
    const QColor border = color("#263036"), accent = color("#14b8a6"), onAccent = color("#04211e");

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, field);
    palette.setColor(QPalette::AlternateBase, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Button, raised);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, accent);
    palette.setColor(QPalette::ToolTipBase, raised);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, onAccent);
    palette.setColor(QPalette::Link, accent);
    palette.setColor(QPalette::Light, raised);
    palette.setColor(QPalette::Midlight, border);
    palette.setColor(QPalette::Mid, border);
    palette.setColor(QPalette::Dark, border.darker(scheme == ColorScheme::Dark ? 150 : 120));
    palette.setColor(QPalette::Shadow, scheme == ColorScheme::Dark ? QColor(Qt::black) : border.darker(160));

    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, disabled);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, border);

    app.setPalette(palette);
}

ColorScheme systemColorScheme()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light ? ColorScheme::Light : ColorScheme::Dark;
}
