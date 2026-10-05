#include "update/UpdateServiceFactory.h"
#include "update/AppImageUpdateService.h"

std::unique_ptr<IUpdateService> createPlatformUpdateService(UpdateServiceKind kind, InstallSource source)
{
    if (kind == UpdateServiceKind::AppImageUpdate && source == InstallSource::AppImage)
        return std::make_unique<AppImageUpdateService>();
    return nullptr;
}
