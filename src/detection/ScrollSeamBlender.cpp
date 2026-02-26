#include "detection/ScrollSeamBlender.h"

#include "utils/MatConverter.h"

#include <QtGlobal>

#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>

#include <limits>

namespace {

QImage asRgb32(const QImage &image)
{
    if (image.isNull()) {
        return QImage();
    }
    if (image.format() == QImage::Format_RGB32) {
        return image;
    }
    return image.convertToFormat(QImage::Format_RGB32);
}

cv::Mat qImageToBgr(const QImage &image)
{
    QImage rgb32 = asRgb32(image);
    if (rgb32.isNull()) {
        return cv::Mat();
    }

    cv::Mat bgra = MatConverter::toMatCopy(rgb32);
    if (bgra.empty()) {
        return cv::Mat();
    }

    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

QImage bgrToQImage(const cv::Mat &bgr)
{
    if (bgr.empty()) {
        return QImage();
    }

    cv::Mat bgra;
    cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);
    return MatConverter::toQImage(bgra);
}

QImage featherBlend(const QImage &previous,
                    const QImage &current)
{
    if (previous.isNull() || current.isNull() || previous.size() != current.size()) {
        return QImage();
    }

    const QImage prev = asRgb32(previous);
    const QImage curr = asRgb32(current);
    QImage output(prev.size(), QImage::Format_RGB32);

    const int h = output.height();
    const int w = output.width();

    for (int y = 0; y < h; ++y) {
        const float rowAlpha = (h <= 1)
            ? 1.0f
            : (static_cast<float>(y) / static_cast<float>(h - 1));
        const QRgb *prevLine = reinterpret_cast<const QRgb *>(prev.constScanLine(y));
        const QRgb *currLine = reinterpret_cast<const QRgb *>(curr.constScanLine(y));
        QRgb *outLine = reinterpret_cast<QRgb *>(output.scanLine(y));

        for (int x = 0; x < w; ++x) {
            const float prevWeight = 1.0f - rowAlpha;
            const float currWeight = rowAlpha;
            const int r = qRound(prevWeight * qRed(prevLine[x]) + currWeight * qRed(currLine[x]));
            const int g = qRound(prevWeight * qGreen(prevLine[x]) + currWeight * qGreen(currLine[x]));
            const int b = qRound(prevWeight * qBlue(prevLine[x]) + currWeight * qBlue(currLine[x]));
            outLine[x] = qRgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
        }
    }

    return output;
}

} // namespace

ScrollSeamBlender::ScrollSeamBlender(const ScrollSeamBlendOptions &options)
    : m_options(options)
{
    m_options.seamSearchWindowPx = qBound(16, m_options.seamSearchWindowPx, 320);
    m_options.seamWidth = qBound(8, m_options.seamWidth, 128);
    m_options.featherSigma = qBound(1.0, m_options.featherSigma, 32.0);
}

int ScrollSeamBlender::findMinimumEnergySeamRow(const QImage &previousOverlapBand,
                                                 const QImage &currentOverlapBand) const
{
    if (previousOverlapBand.isNull() || currentOverlapBand.isNull() ||
        previousOverlapBand.size() != currentOverlapBand.size()) {
        return 0;
    }

    cv::Mat prevGray = MatConverter::toGray(previousOverlapBand);
    cv::Mat currGray = MatConverter::toGray(currentOverlapBand);
    if (prevGray.empty() || currGray.empty() || prevGray.size() != currGray.size()) {
        return 0;
    }

    cv::Mat blended;
    cv::addWeighted(prevGray, 0.5, currGray, 0.5, 0.0, blended);

    cv::Mat grad;
    cv::Sobel(blended, grad, CV_32F, 0, 1, 3, 1.0, 0.0, cv::BORDER_REPLICATE);
    cv::Mat absGrad;
    cv::absdiff(grad, cv::Scalar(0.0), absGrad);

    const int rows = absGrad.rows;
    if (rows <= 0) {
        return 0;
    }

    double bestEnergy = std::numeric_limits<double>::max();
    int bestRow = 0;

    const int margin = qMin(2, qMax(0, rows - 1));
    const int centerRow = rows / 2;
    const int maxHalfWindow = qMax(8, rows / 3);
    const int halfWindow = qMin(maxHalfWindow, m_options.seamSearchWindowPx);
    int startRow = qMax(margin, centerRow - halfWindow);
    int endRow = qMin(rows - margin - 1, centerRow + halfWindow);
    if (startRow > endRow) {
        startRow = margin;
        endRow = qMax(startRow, rows - margin - 1);
    }

    for (int y = startRow; y <= endRow; ++y) {
        const float *line = absGrad.ptr<float>(y);
        if (!line) {
            continue;
        }

        double energy = 0.0;
        for (int x = 0; x < absGrad.cols; ++x) {
            energy += static_cast<double>(line[x]);
        }

        if (energy < bestEnergy) {
            bestEnergy = energy;
            bestRow = y;
        }
    }

    return qBound(0, bestRow, rows - 1);
}

ScrollSeamBlendResult ScrollSeamBlender::blendTopBand(const QImage &previousBottomBand,
                                                      const QImage &currentTopBand,
                                                      const cv::Mat &weights) const
{
    Q_UNUSED(weights);

    ScrollSeamBlendResult result;

    if (previousBottomBand.isNull() || currentTopBand.isNull() ||
        previousBottomBand.size() != currentTopBand.size()) {
        result.reason = QStringLiteral("Invalid seam inputs");
        return result;
    }

    QImage feathered = featherBlend(previousBottomBand, currentTopBand);
    if (feathered.isNull()) {
        result.reason = QStringLiteral("Failed to feather seam");
        return result;
    }

    if (!m_options.usePoissonByDefault) {
        result.ok = true;
        result.poissonApplied = false;
        result.blendedTopBand = feathered;
        result.reason = QStringLiteral("Seam blend disabled by default");
        return result;
    }

    const cv::Mat srcBgr = qImageToBgr(previousBottomBand);
    const cv::Mat dstBgr = qImageToBgr(feathered);
    if (srcBgr.empty() || dstBgr.empty() || srcBgr.size() != dstBgr.size()) {
        result.ok = true;
        result.poissonApplied = false;
        result.blendedTopBand = feathered;
        result.reason = QStringLiteral("Poisson fallback to feather");
        return result;
    }

    cv::Mat mask(srcBgr.rows, srcBgr.cols, CV_8UC1, cv::Scalar(255));
    cv::Mat poisson;
    try {
        const cv::Point center(dstBgr.cols / 2, dstBgr.rows / 2);
        cv::seamlessClone(srcBgr, dstBgr, mask, center, poisson, cv::NORMAL_CLONE);
    } catch (const cv::Exception &) {
        result.ok = true;
        result.poissonApplied = false;
        result.blendedTopBand = feathered;
        result.reason = QStringLiteral("Poisson failed; feather fallback");
        return result;
    }

    QImage poissonImage = bgrToQImage(poisson);
    if (poissonImage.isNull()) {
        result.ok = true;
        result.poissonApplied = false;
        result.blendedTopBand = feathered;
        result.reason = QStringLiteral("Poisson conversion failed; feather fallback");
        return result;
    }

    result.ok = true;
    result.poissonApplied = true;
    result.blendedTopBand = poissonImage;
    result.reason = QStringLiteral("Poisson applied");
    return result;
}
