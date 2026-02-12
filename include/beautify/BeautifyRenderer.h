#ifndef BEAUTIFYRENDERER_H
#define BEAUTIFYRENDERER_H

#include "beautify/BeautifySettings.h"
#include <QPixmap>
#include <QPainter>
#include <QSize>

class BeautifyRenderer {
public:
    static QPixmap applyToPixmap(const QPixmap& source, const BeautifySettings& settings);
    static QSize calculateOutputSize(const QSize& sourceSize, const BeautifySettings& settings);
    static void render(QPainter& painter, const QRect& targetRect,
                       const QPixmap& source, const BeautifySettings& settings);

private:
    static void drawBackground(QPainter& painter, const QRect& rect,
                               const BeautifySettings& settings);
    static void drawShadow(QPainter& painter, const QRect& insetRect,
                           const BeautifySettings& settings);
    static void drawScreenshot(QPainter& painter, const QRect& insetRect,
                               const QPixmap& source, const BeautifySettings& settings);
    static QRect calculateInsetRect(const QSize& outputSize, const QSize& sourceSize,
                                    const BeautifySettings& settings);
    static QSize applyAspectRatio(const QSize& baseSize, BeautifyAspectRatio ratio);
};

#endif // BEAUTIFYRENDERER_H
