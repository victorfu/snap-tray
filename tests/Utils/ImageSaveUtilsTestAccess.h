#pragma once
#include "utils/ImageSaveUtils.h"

struct ImageSaveUtilsTestAccess {
    using Publisher = std::function<SnapTray::FilePublishResult(const QString&, const QString&)>;
    static ImageSaveUtils::UniqueSaveResult save(const QImage& image,
                                                const ImageSaveUtils::UniqueSaveSpec& spec,
                                                Publisher publisher,
                                                const QString& uuid = {})
    {
        ImageSaveUtils::UniqueSaveHooks hooks;
        hooks.publish = std::move(publisher);
        hooks.uuidSuffix = uuid;
        return ImageSaveUtils::saveImageUniqueWithHooks(image, spec, "PNG", hooks);
    }
};
