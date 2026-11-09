#ifndef FREEHANDSMOOTHER_H
#define FREEHANDSMOOTHER_H

#include <QLineF>
#include <QPointF>
#include <QtGlobal>

/**
 * @brief Input conditioning shared by the freehand Pencil and Marker tools.
 *
 * Committed stroke points are EMA-smoothed and spaced apart so the spline does
 * not amplify input quantization into wobble. The resulting lag behind the
 * cursor is hidden by each stroke's live tail, which always ends at the raw
 * cursor position.
 */
class FreehandSmoother {
public:
    void reset(const QPointF& pos)
    {
        m_smoothedPoint = pos;
        m_lastRawPoint = pos;
    }

    QPointF smooth(const QPointF& rawPoint)
    {
        const qreal speed = QLineF(m_lastRawPoint, rawPoint).length();
        m_lastRawPoint = rawPoint;

        // Adaptive smoothing: less smoothing at high speed, more at low speed.
        qreal adaptiveFactor;
        if (speed <= kSpeedThresholdLow) {
            adaptiveFactor = kBaseSmoothing;
        } else if (speed >= kSpeedThresholdHigh) {
            adaptiveFactor = kFastSmoothing;
        } else {
            const qreal t = (speed - kSpeedThresholdLow) / (kSpeedThresholdHigh - kSpeedThresholdLow);
            adaptiveFactor = kBaseSmoothing + t * (kFastSmoothing - kBaseSmoothing);
        }

        m_smoothedPoint = adaptiveFactor * rawPoint + (1.0 - adaptiveFactor) * m_smoothedPoint;
        return m_smoothedPoint;
    }

    // Minimum distance before committing another point. Dense samples turn the
    // +/-0.5 physical-pixel input quantization into visible wobble, so spacing
    // is set in physical pixels and capped in logical pixels so small shapes
    // keep their detail at 100%.
    static qreal minPointDistance(qreal devicePixelRatio)
    {
        const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
        return qMin(kMinPointDistancePhysical / dpr, kMaxMinPointDistanceLogical);
    }

    static bool shouldCommit(const QPointF& lastCommitted, const QPointF& candidate,
                             qreal devicePixelRatio)
    {
        return QLineF(lastCommitted, candidate).length() >= minPointDistance(devicePixelRatio);
    }

    static constexpr qreal kMinPointDistancePhysical = 6.0;
    static constexpr qreal kMaxMinPointDistanceLogical = 4.0;

private:
    // Smoothing factor per event (0 = max smoothing, 1 = raw input).
    static constexpr qreal kBaseSmoothing = 0.3;
    static constexpr qreal kFastSmoothing = 0.85;
    // Speed thresholds in logical pixels per event.
    static constexpr qreal kSpeedThresholdLow = 3.0;
    static constexpr qreal kSpeedThresholdHigh = 15.0;

    QPointF m_smoothedPoint;
    QPointF m_lastRawPoint;
};

#endif // FREEHANDSMOOTHER_H
