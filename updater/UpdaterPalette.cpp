#include "UpdaterPalette.h"

#include <QApplication>
#include <QPalette>
#include <QStyleHints>

#include "QAppUpdater/AppInfo.h"

namespace QAppUpdater {

Palette updaterPalette()
{
    bool const light = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light;

    Palette p;
    if (light) {
        p.bg0 = QColor(0xFFFFFF);
        p.bg1 = QColor(0xF3F3F5);
        p.bg2 = QColor(0xFFFFFF);
        p.bg3 = QColor(0xE8E8EC);
        p.border = QColor(0xD8D8DE);
        p.text = QColor(0x1A1A1E);
        p.textMuted = QColor(0x66666D);
        p.textFaint = QColor(0xA4A4AA);
        p.danger = QColor(0xD14343);
        p.ok = QColor(0x2E9E5B);
    } else {
        p.bg0 = QColor(0x0E0E10);
        p.bg1 = QColor(0x141417);
        p.bg2 = QColor(0x1C1C20);
        p.bg3 = QColor(0x26262B);
        p.border = QColor(0x2A2A30);
        p.text = QColor(0xECECEC);
        p.textMuted = QColor(0x8A8A90);
        p.textFaint = QColor(0x55555A);
        p.danger = QColor(0xE05555);
        p.ok = QColor(0x3CB371);
    }

    QColor const accent(appInfo().accentColor);
    p.accent = accent.isValid() ? accent : QColor(0x4C8DFF);
    // Dark text on a light accent, light text on a dark one.
    p.accentContrast = p.accent.lightnessF() > 0.6 ? QColor(0x111114) : QColor(0xFFFFFF);
    return p;
}

void applyApplicationPalette(Palette const& p)
{
    QPalette palette;
    palette.setColor(QPalette::Window, p.bg1);
    palette.setColor(QPalette::WindowText, p.text);
    palette.setColor(QPalette::Base, p.bg2);
    palette.setColor(QPalette::AlternateBase, p.bg3);
    palette.setColor(QPalette::Text, p.text);
    palette.setColor(QPalette::Button, p.bg2);
    palette.setColor(QPalette::ButtonText, p.text);
    palette.setColor(QPalette::Highlight, p.accent);
    palette.setColor(QPalette::HighlightedText, p.accentContrast);
    palette.setColor(QPalette::PlaceholderText, p.textFaint);
    palette.setColor(QPalette::ToolTipBase, p.bg2);
    palette.setColor(QPalette::ToolTipText, p.text);
    palette.setColor(QPalette::Link, p.accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, p.textFaint);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, p.textFaint);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, p.textFaint);
    QApplication::setPalette(palette);
}

} // namespace QAppUpdater
