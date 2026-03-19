#ifndef AUTOLAUNCHMANAGER_H
#define AUTOLAUNCHMANAGER_H

class AutoLaunchManager
{
public:
    static bool isEnabled();
    static bool setEnabled(bool enabled);
    static bool syncWithPreference();
};

#endif // AUTOLAUNCHMANAGER_H
