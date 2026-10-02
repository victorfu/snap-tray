#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>

#include "WindowDetector.h"

#include <windows.h>
#include <sddl.h>
#include <cstdio>
#include <cstring>

namespace {
constexpr char kHelperArgument[] = "--metadata-window-helper";
constexpr wchar_t kWindowTitle[] = L"SnapTray metadata test";
constexpr int kProcessTimeoutMs = 5000;

int runWindowHelper()
{
    // Keep the window hidden; GetWindowTextW can read its caption without a UI loop.
    HWND window = CreateWindowExW(0, L"STATIC", kWindowTitle, WS_OVERLAPPED,
                                  0, 0, 100, 100, nullptr, nullptr, nullptr, nullptr);
    if (!window) {
        return 1;
    }
    std::printf("%llu\n", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(window)));
    std::fflush(stdout);
    std::getchar(); // The parent releases the helper through stdin.
    DestroyWindow(window);
    return 0;
}
} // namespace

class tst_WindowDetectorMetadata : public QObject
{
    Q_OBJECT

private slots:
    void testPopulateWindowMetadata_data();
    void testPopulateWindowMetadata();
};

void tst_WindowDetectorMetadata::testPopulateWindowMetadata_data()
{
    QTest::addColumn<bool>("denyMemoryRead");
    QTest::newRow("normal-process") << false;
    QTest::newRow("memory-read-denied") << true;
}

void tst_WindowDetectorMetadata::testPopulateWindowMetadata()
{
    QFETCH(bool, denyMemoryRead);

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto freeDescriptor = qScopeGuard([&]() {
        if (descriptor) LocalFree(descriptor);
    });
    if (denyMemoryRead) {
        // Restrict only this child fixture, without requiring an elevated process.
        QVERIFY(ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(D;;0x0010;;;WD)(A;;GA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr));
    }
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    QProcess child;
    child.setCreateProcessArgumentsModifier([&](QProcess::CreateProcessArguments* arguments) {
        if (denyMemoryRead) arguments->processAttributes = &attributes;
        arguments->flags |= CREATE_NO_WINDOW;
        arguments->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        arguments->startupInfo->wShowWindow = SW_HIDE;
    });
    const auto stopChild = qScopeGuard([&]() {
        if (child.state() != QProcess::NotRunning) {
            child.write("\n");
            if (!child.waitForFinished(kProcessTimeoutMs)) {
                child.kill();
                child.waitForFinished(kProcessTimeoutMs);
            }
        }
    });
    child.start(QCoreApplication::applicationFilePath(), {QString::fromLatin1(kHelperArgument)});
    QVERIFY2(child.waitForStarted(kProcessTimeoutMs), qPrintable(child.errorString()));
    QTRY_VERIFY_WITH_TIMEOUT(child.canReadLine(), kProcessTimeoutMs);
    bool validId = false;
    const auto windowId = child.readLine().trimmed().toULongLong(&validId);
    QVERIFY(validId);

    if (denyMemoryRead) {
        HANDLE memoryHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                          FALSE, static_cast<DWORD>(child.processId()));
        const DWORD error = GetLastError();
        if (memoryHandle) CloseHandle(memoryHandle);
        QVERIFY(memoryHandle == nullptr);
        QCOMPARE(error, DWORD(ERROR_ACCESS_DENIED));
    }

    DetectedElement element{};
    element.windowId = static_cast<uint32_t>(windowId);
    element.ownerPid = child.processId();
    WindowDetector::populateWindowMetadata(element);

    QCOMPARE(element.windowTitle, QString::fromWCharArray(kWindowTitle));
    QCOMPARE(element.ownerApp, QFileInfo(QCoreApplication::applicationFilePath()).baseName());
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], kHelperArgument) == 0) {
        return runWindowHelper();
    }
    QCoreApplication app(argc, argv);
    tst_WindowDetectorMetadata test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_WindowDetectorMetadata_win.moc"
