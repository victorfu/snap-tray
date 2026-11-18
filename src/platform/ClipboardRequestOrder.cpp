#include "PlatformFeatures.h"

#include <QClipboard>
#include <QGuiApplication>

// The order of clipboard requests (#353), shared by every platform.

quint64 PlatformFeatures::reserveClipboardRequest()
{
    trackClipboardChanges();
    return ++s_lastClipboardRequest;
}

bool PlatformFeatures::isLatestClipboardRequest(quint64 request)
{
    return request == s_lastClipboardRequest.load();
}

void PlatformFeatures::trackClipboardChanges()
{
    static bool tracking = false;
    if (tracking || !qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        return;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return;
    }
    tracking = true;
    // Text copies (OCR results, pin info) and other apps write the clipboard
    // without asking here; any such change supersedes a reserved copy. Qt
    // reports another app's change on macOS once SnapTray is activated.
    QObject::connect(clipboard, &QClipboard::dataChanged, clipboard, [] {
        if (!s_writingImageCopy) {
            ++s_lastClipboardRequest;
        }
    });
}
