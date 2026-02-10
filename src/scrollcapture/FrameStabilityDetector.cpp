#include "scrollcapture/FrameStabilityDetector.h"

#include "capture/ICaptureEngine.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>

namespace {
QRect logicalToImageRect(const QRect& logicalRect,
                         const QRect& windowRect,
                         const QImage& image)
{
    if (image.isNull() || !windowRect.isValid() || !logicalRect.isValid()) {
        return {};
    }

    const double scaleX = static_cast<double>(image.width()) / static_cast<double>(windowRect.width());
    const double scaleY = static_cast<double>(image.height()) / static_cast<double>(windowRect.height());

    QRect translated = logicalRect.translated(-windowRect.topLeft());
    QRect physical(
        static_cast<int>(std::round(translated.x() * scaleX)),
        static_cast<int>(std::round(translated.y() * scaleY)),
        static_cast<int>(std::round(translated.width() * scaleX)),
        static_cast<int>(std::round(translated.height() * scaleY))
    );
    return physical.intersected(QRect(0, 0, image.width(), image.height()));
}

cv::Mat toGrayDownscaled(const QImage& image)
{
    if (image.isNull()) {
        return {};
    }

    QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat mat(gray.height(), gray.width(), CV_8UC1,
                const_cast<uchar*>(gray.constBits()), gray.bytesPerLine());

    constexpr int kTargetWidth = 520;
    if (mat.cols <= kTargetWidth) {
        return mat.clone();
    }

    const double scale = static_cast<double>(kTargetWidth) / static_cast<double>(mat.cols);
    cv::Mat resized;
    cv::resize(mat, resized, cv::Size(), scale, scale, cv::INTER_AREA);
    return resized;
}
} // namespace

FrameStabilityDetector::Result FrameStabilityDetector::waitUntilStable(ICaptureEngine* engine,
                                                                       const QRect& contentRect,
                                                                       const QRect& windowRect,
                                                                       const Params& params,
                                                                       std::atomic_bool* cancelFlag,
                                                                       std::atomic_bool* stopFlag) const
{
    Result result;
    if (!engine) {
        return result;
    }

    QElapsedTimer timer;
    timer.start();

    cv::Mat previousMat;
    int stableCount = 0;

    while (timer.elapsed() <= params.maxSettleMs) {
        if (cancelFlag && cancelFlag->load()) {
            return result;
        }
        if (stopFlag && stopFlag->load()) {
            return result;
        }

        QImage frame = engine->captureFrame();
        if (frame.isNull()) {
            QThread::msleep(params.intervalMs);
            continue;
        }

        result.frame = frame;
        const QRect cropRect = logicalToImageRect(contentRect, windowRect, frame);
        if (!cropRect.isValid()) {
            break;
        }

        cv::Mat currentMat = toGrayDownscaled(frame.copy(cropRect));
        if (currentMat.empty()) {
            break;
        }

        if (!previousMat.empty() && previousMat.size() == currentMat.size()) {
            cv::Mat diff;
            cv::absdiff(currentMat, previousMat, diff);
            result.lastDiff = cv::mean(diff)[0];

            if (result.lastDiff < params.stableThreshold) {
                ++stableCount;
                if (stableCount >= params.requiredStableCount) {
                    result.stable = true;
                    result.weak = false;
                    return result;
                }
            } else {
                stableCount = 0;
            }
        }

        previousMat = currentMat;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(params.intervalMs);

        if (cancelFlag && cancelFlag->load()) {
            return result;
        }
        if (stopFlag && stopFlag->load()) {
            return result;
        }
    }

    result.weak = true;
    return result;
}
