#ifndef BEAUTIFYSETTINGS_H
#define BEAUTIFYSETTINGS_H

#include <QColor>
#include <QString>
#include <QVector>

enum class BeautifyBackgroundType {
    Solid = 0,
    LinearGradient = 1,
    RadialGradient = 2
};

enum class BeautifyAspectRatio {
    Auto = 0,
    Square_1_1 = 1,
    Wide_16_9 = 2,
    Wide_4_3 = 3,
    Tall_9_16 = 4,
    Twitter_2_1 = 5
};

struct BeautifySettings {
    // Background
    BeautifyBackgroundType backgroundType = BeautifyBackgroundType::LinearGradient;
    QColor backgroundColor{0, 180, 219};
    QColor gradientStartColor{0, 180, 219};
    QColor gradientEndColor{0, 131, 176};
    int gradientAngle = 135;

    // Padding (space around screenshot in result image)
    int padding = 64;

    // Corner radius for the screenshot inset
    int cornerRadius = 12;

    // Shadow for screenshot inset
    bool shadowEnabled = true;
    int shadowBlur = 40;
    int shadowOffsetX = 0;
    int shadowOffsetY = 8;
    QColor shadowColor{0, 0, 0, 80};

    // Aspect ratio of output
    BeautifyAspectRatio aspectRatio = BeautifyAspectRatio::Auto;
};

struct BeautifyPreset {
    QString name;
    QColor gradientStart;
    QColor gradientEnd;
    int gradientAngle;
};

inline QVector<BeautifyPreset> beautifyPresets() {
    return {
        {"Ocean",    QColor(0, 180, 219),   QColor(0, 131, 176),   135},
        {"Sunset",   QColor(255, 154, 0),   QColor(255, 87, 51),   135},
        {"Lavender", QColor(150, 100, 240), QColor(200, 100, 200), 135},
        {"Forest",   QColor(17, 153, 82),   QColor(56, 239, 125),  135},
        {"Midnight", QColor(30, 30, 60),    QColor(60, 30, 90),    135},
        {"Rose",     QColor(255, 110, 127), QColor(191, 63, 127),  135},
        {"Sky",      QColor(135, 206, 235), QColor(176, 224, 230), 135},
        {"Charcoal", QColor(55, 55, 55),    QColor(85, 85, 85),    135},
    };
}

#endif // BEAUTIFYSETTINGS_H
