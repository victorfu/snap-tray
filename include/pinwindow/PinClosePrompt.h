#pragma once

#include <QEventLoop>

class PinClosePrompt : public QEventLoop
{
    Q_OBJECT
public:
    enum Choice { Cancel, Save, Discard };

    Q_INVOKABLE void cancel() { exit(Cancel); }
    Q_INVOKABLE void save() { exit(Save); }
    Q_INVOKABLE void discard() { exit(Discard); }
};
