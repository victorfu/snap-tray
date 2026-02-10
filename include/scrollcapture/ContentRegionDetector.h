#ifndef CONTENTREGIONDETECTOR_H
#define CONTENTREGIONDETECTOR_H

#include <QImage>
#include <QRect>
#include <QString>

class ContentRegionDetector
{
public:
    struct DetectionResult {
        bool success = false;
        bool fallbackUsed = false;
        QRect contentRect;
        int headerIgnore = 0;
        int footerIgnore = 0;
        double confidence = 0.0;
        QString warning;
    };

    DetectionResult detect(const QImage& before,
                           const QImage& after,
                           const QRect& windowRect) const;
};

#endif // CONTENTREGIONDETECTOR_H
