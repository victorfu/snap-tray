#include "update/UpdateServiceFactory.h"

#include <QtGlobal>

std::unique_ptr<IUpdateService> createPlatformUpdateService(UpdateServiceKind kind,
                                                            InstallSource source)
{
    Q_UNUSED(kind);
    Q_UNUSED(source);
    return nullptr;
}
