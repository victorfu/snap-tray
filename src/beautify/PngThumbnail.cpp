#include "PngThumbnail.h"

#include <QColorSpace>
#include <QVector>
#include <QtEndian>
#include <spng.h>

#include <array>
#include <memory>

namespace {

// Every Adam7 pass visits a disjoint set of source pixels. The decoder
// supplies the deinterlaced row number and the packed pixels for that pass.
constexpr std::array<int, 7> kFirstX{0, 4, 0, 2, 0, 1, 0};
constexpr std::array<int, 7> kStepX{8, 8, 4, 4, 2, 2, 1};

struct Bin {
    quint64 r = 0, g = 0, b = 0, a = 0;
    quint32 count = 0;
};

QColorSpace pngColors(spng_ctx* ctx)
{
    spng_iccp icc{};
    if (!spng_get_iccp(ctx, &icc)) {
        const auto space = QColorSpace::fromIccProfile(QByteArray::fromRawData(icc.profile, icc.profile_len));
        if (space.isValid()) return space;
    }
    uint8_t intent;
    if (!spng_get_srgb(ctx, &intent)) return QColorSpace::SRgb;
    double gamma = 0;
    if (spng_get_gama(ctx, &gamma) || gamma <= 0) return QColorSpace::SRgb;
    spng_chrm primaries{};
    if (spng_get_chrm(ctx, &primaries)) {
        return QColorSpace(QColorSpace::Primaries::SRgb, float(1 / gamma));
    }
    return QColorSpace({primaries.white_point_x, primaries.white_point_y},
                       {primaries.red_x, primaries.red_y}, {primaries.green_x, primaries.green_y},
                       {primaries.blue_x, primaries.blue_y}, QColorSpace::TransferFunction::Gamma, float(1 / gamma));
}

} // namespace

QTransform SnapTray::pngOrientation(const QByteArray& encoded)
{
    std::unique_ptr<spng_ctx, decltype(&spng_ctx_free)> ctx(spng_ctx_new(0), spng_ctx_free);
    spng_exif exif{};
    if (!ctx || spng_set_png_buffer(ctx.get(), encoded.constData(), encoded.size())
        || spng_get_exif(ctx.get(), &exif) || exif.length < 8) return {};

    // PNG's eXIf chunk starts directly with the TIFF header. Read only the
    // orientation SHORT in IFD0. Other directories and offsets are irrelevant.
    const auto* data = reinterpret_cast<const uchar*>(exif.data);
    const bool little = data[0] == 'I' && data[1] == 'I';
    if (!little && !(data[0] == 'M' && data[1] == 'M')) return {};
    const auto u16 = [little](const uchar* p) {
        return little ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
    };
    const auto u32 = [little](const uchar* p) {
        return little ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
    };
    if (u16(data + 2) != 42) return {};
    const size_t directory = u32(data + 4);
    if (directory < 8 || directory > exif.length - 2) return {};
    constexpr size_t kEntryBytes = 12;
    const size_t count = u16(data + directory);
    if (count > (exif.length - directory - 2) / kEntryBytes) return {};
    for (size_t i = 0; i < count; ++i) {
        const auto* entry = data + directory + 2 + i * kEntryBytes;
        constexpr quint16 kOrientationTag = 0x0112;
        if (u16(entry) != kOrientationTag || u16(entry + 2) != 3 || u32(entry + 4) != 1) continue;
        const int orientation = u16(entry + 8);
        static const std::array<QTransform, 8> transforms{
            QTransform(), QTransform::fromScale(-1, 1), QTransform().rotate(180),
            QTransform::fromScale(1, -1), QTransform(0, 1, 1, 0, 0, 0),
            QTransform().rotate(90), QTransform(0, -1, -1, 0, 0, 0), QTransform().rotate(270)
        };
        return orientation >= 1 && orientation <= int(transforms.size()) ? transforms[orientation - 1] : QTransform();
    }
    return {};
}

QImage SnapTray::readPngThumbnail(const QByteArray& encoded, QSize target)
{
    std::unique_ptr<spng_ctx, decltype(&spng_ctx_free)> ctx(spng_ctx_new(0), spng_ctx_free);
    if (!ctx || spng_set_png_buffer(ctx.get(), encoded.constData(), encoded.size())) return {};
    spng_ihdr header{};
    if (spng_get_ihdr(ctx.get(), &header) || target.isEmpty()) return {};
    if (spng_decode_image(ctx.get(), nullptr, 0, SPNG_FMT_RGBA8, SPNG_DECODE_PROGRESSIVE | SPNG_DECODE_TRNS)) return {};

    QImage result(target, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull()) return {};
    result.setColorSpace(pngColors(ctx.get()));
    // Ordinary PNGs arrive in order, so keep just one output row of sums.
    // Interlaced images revisit rows and need sums for the thumbnail canvas.
    const bool interlaced = header.interlace_method == SPNG_INTERLACE_ADAM7;
    QVector<Bin> bins(target.width() * (interlaced ? target.height() : 1));
    QByteArray scanline(qsizetype(header.width) * 4, Qt::Uninitialized);
    const auto finishRow = [&](int y, Bin* row) {
        auto* out = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < target.width(); ++x) {
            const Bin& b = row[x];
            if (!b.count) return false;
            out[x] = qRgba(b.r / b.count, b.g / b.count, b.b / b.count, b.a / b.count);
        }
        return true;
    };

    int previousY = 0;
    int status = 0;
    do {
        spng_row_info info{};
        if (spng_get_row_info(ctx.get(), &info)) return {};
        status = spng_decode_scanline(ctx.get(), scanline.data(), scanline.size());
        if (status && status != SPNG_EOI) return {};
        const int y = qint64(info.row_num) * target.height() / header.height;
        if (!interlaced && previousY != y) {
            if (!finishRow(previousY, bins.data())) return {};
            bins.fill({});
        }
        previousY = y;
        Bin* row = bins.data() + (interlaced ? y * target.width() : 0);
        const auto* pixel = reinterpret_cast<const uchar*>(scanline.constData());
        const int step = interlaced ? kStepX[info.pass] : 1;
        for (quint32 x = interlaced ? kFirstX[info.pass] : 0; x < header.width; x += step, pixel += 4) {
            Bin& bin = row[qint64(x) * target.width() / header.width];
            const QRgb value = qPremultiply(qRgba(pixel[0], pixel[1], pixel[2], pixel[3]));
            bin.r += qRed(value);
            bin.g += qGreen(value);
            bin.b += qBlue(value);
            bin.a += qAlpha(value);
            ++bin.count;
        }
    } while (status != SPNG_EOI);
    if (interlaced) {
        for (int y = 0; y < target.height(); ++y) {
            if (!finishRow(y, bins.data() + y * target.width())) return {};
        }
    } else if (!finishRow(previousY, bins.data())) {
        return {};
    }
    return result;
}
