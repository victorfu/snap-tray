// Scroll area detection and scroll injection for platforms without an
// implementation yet (Linux: AT-SPI is out of scope for #265).
#include "scrollcapture/ScrollAreaLocator.h"
#include "scrollcapture/ScrollInjector.h"

namespace snaptray::scrollcapture {

namespace {

const QString kUnsupportedDetail = QStringLiteral("scroll area detection is not implemented on this platform");

class StubScrollAreaLocator final : public ScrollAreaLocator
{
public:
    bool isAvailable() const override { return false; }

    LocateResult locateAt(const QPoint& /*globalPos*/, const LocateOptions& /*options*/) override
    {
        LocateResult result;
        result.status = LocateStatus::Unsupported;
        result.detail = kUnsupportedDetail;
        return result;
    }

    QJsonObject environment() const override { return {}; }
};

class StubScrollInjector final : public ScrollInjector
{
public:
    QList<ScrollMethod> supportedMethods() const override { return {}; }

    InjectResult scroll(const ScrollArea& /*area*/, const ScrollRequest& /*request*/) override
    {
        return InjectResult::failed(kUnsupportedDetail);
    }
};

} // namespace

std::unique_ptr<ScrollAreaLocator> ScrollAreaLocator::create()
{
    return std::make_unique<StubScrollAreaLocator>();
}

std::unique_ptr<ScrollInjector> ScrollInjector::create()
{
    return std::make_unique<StubScrollInjector>();
}

} // namespace snaptray::scrollcapture
