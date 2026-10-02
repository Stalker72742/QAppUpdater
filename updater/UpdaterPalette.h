#pragma once

#include <QColor>

namespace QAppUpdater {

// Colors of the updater window: dark or light following the system color scheme, with the app's accent color.
struct Palette {
    QColor bg0;            // card background
    QColor bg1;            // window background
    QColor bg2;            // inputs and buttons
    QColor bg3;            // hovered inputs and buttons
    QColor border;
    QColor text;
    QColor textMuted;
    QColor textFaint;      // disabled text
    QColor accent;
    QColor accentContrast; // text drawn on top of `accent`
    QColor danger;
    QColor ok;
};

Palette updaterPalette();

// Sets the QApplication palette, so widgets the stylesheet doesn't cover match too.
void applyApplicationPalette(Palette const& palette);

} // namespace QAppUpdater
