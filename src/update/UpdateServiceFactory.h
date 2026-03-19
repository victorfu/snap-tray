#ifndef SNAPTRAY_UPDATESERVICEFACTORY_H
#define SNAPTRAY_UPDATESERVICEFACTORY_H

#include "update/IUpdateService.h"

#include <memory>

std::unique_ptr<IUpdateService> createPlatformUpdateService(UpdateServiceKind kind,
                                                            InstallSource source);

#endif // SNAPTRAY_UPDATESERVICEFACTORY_H
