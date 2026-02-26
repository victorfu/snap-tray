#ifndef SCROLLAUTOMATIONDRIVERFACTORY_H
#define SCROLLAUTOMATIONDRIVERFACTORY_H

class QObject;
class IScrollAutomationDriver;

IScrollAutomationDriver* createPlatformScrollAutomationDriver(QObject *parent = nullptr);
bool isPlatformScrollAutomationAvailable();

#endif // SCROLLAUTOMATIONDRIVERFACTORY_H
