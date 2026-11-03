#include "platform/MacDockPresence.h"

#include <QtTest>

#include <optional>

#import <AppKit/AppKit.h>

namespace {

int activationPolicy()
{
    return static_cast<int>(NSApp.activationPolicy);
}

constexpr int kRegular = static_cast<int>(NSApplicationActivationPolicyRegular);
constexpr int kAccessory = static_cast<int>(NSApplicationActivationPolicyAccessory);

} // namespace

class tst_MacDockPresence : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void claimKeepsAppInDockWhileAlive();
    void overlappingClaimsKeepAppInDock();
};

void tst_MacDockPresence::init()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

void tst_MacDockPresence::claimKeepsAppInDockWhileAlive()
{
    {
        SnapTray::DockPresenceClaim claim;
        QCOMPARE(activationPolicy(), kRegular);
    }
    QCOMPARE(activationPolicy(), kAccessory);
}

void tst_MacDockPresence::overlappingClaimsKeepAppInDock()
{
    std::optional<SnapTray::DockPresenceClaim> settings;
    settings.emplace();
    QCOMPARE(activationPolicy(), kRegular);

    {
        SnapTray::DockPresenceClaim updateSession;
        settings.reset(); // Settings closes first
        QCOMPARE(activationPolicy(), kRegular);
    }
    QCOMPARE(activationPolicy(), kAccessory);
}

QTEST_MAIN(tst_MacDockPresence)
#include "tst_MacDockPresence.moc"
