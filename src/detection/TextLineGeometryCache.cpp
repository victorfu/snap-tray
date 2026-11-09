#include "detection/TextLineGeometryCache.h"
#include <cmath>

void TextLineGeometryCache::clear()
{
    ++m_generation;
    m_key.reset();
    m_source = {};
    m_snapshot.reset();
    m_attempted = false;
}
void TextLineGeometryCache::request(const Key& key, const QPixmap& source)
{
    if (source.isNull() || !std::isfinite(key.dpr) || key.dpr <= 0 ||
        key.crop.isEmpty() || !source.rect().contains(key.crop)) { clear(); return; }
    if (!m_key || !(*m_key == key)) {
        clear();
        m_key = key;
        m_source = source;
    }
    start();
}
void TextLineGeometryCache::start()
{
    if (!m_key || m_inFlight || m_attempted) return;
    const auto key = *m_key;
    const auto generation = m_generation;
    m_inFlight = true;
    m_attempted = true;
    QPixmap crop = m_source.copy(key.crop);
    crop.setDevicePixelRatio(1);
    QPointer<TextLineGeometryCache> self(this);
    m_runner(crop, key.languages, [self, key, generation](const OCRResult& result) {
        if (!self) return;
        self->m_inFlight = false;
        if (self->m_generation == generation && self->m_key && *self->m_key == key && result.success) {
            const QRectF logicalCrop(key.crop.x()/key.dpr, key.crop.y()/key.dpr,
                                     key.crop.width()/key.dpr, key.crop.height()/key.dpr);
            self->m_snapshot = TextLineSnapshot::fromOCR(result.blocks, logicalCrop);
        }
        self->start(); // at most one queued current source; failures do not retry
    });
}
