// Runs outside the GUI process. No operation overwrites the running AppImage.
#include <vector>
#include <appimage/update.h>
#include "signing/signaturevalidator.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>
#include <QThread>
#include <QUuid>
#include <QVersionNumber>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

using appimage::update::Updater;
using appimage::update::UpdatableAppImage;
using appimage::update::signing::SignatureValidator;
using appimage::update::signing::SignatureValidationResult;
namespace {
constexpr int kExitWaitMs = 60000;
constexpr int kStartupWaitMs = 30000;
volatile sig_atomic_t cancelled = 0;
void interrupt(int) { cancelled = 1; }
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void replaceFile(const QString& source, const QString& destination)
{
    require(::rename(QFile::encodeName(source).constData(), QFile::encodeName(destination).constData()) == 0,
            "Cannot atomically replace the AppImage. The previous version is retained.");
}
void event(const QJsonObject& value) { std::cout << QJsonDocument(value).toJson(QJsonDocument::Compact).constData() << std::endl; }
void save(const QString& path, const QJsonObject& value)
{
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly), "Cannot write update transaction.");
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    require(file.write(bytes) == bytes.size() && file.commit(), "Cannot save update transaction.");
}
QJsonObject read(const QString& path)
{
    QFile file(path); require(file.open(QIODevice::ReadOnly), "No downloaded update is available.");
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    require(!object.isEmpty(), "Invalid update transaction."); return object;
}
QString hash(const QString& path)
{
    QFile file(path); require(file.open(QIODevice::ReadOnly), "Cannot read AppImage.");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot hash AppImage."); return QString::fromLatin1(hash.result().toHex());
}
void validate(const QString& oldFile, const QString& newFile)
{
    SignatureValidator validator;
    const auto oldResult = validator.validate(UpdatableAppImage(oldFile.toStdString()));
    const auto newResult = validator.validate(UpdatableAppImage(newFile.toStdString()));
    require(oldResult.type() == SignatureValidationResult::ResultType::SUCCESS &&
            newResult.type() == SignatureValidationResult::ResultType::SUCCESS,
            "AppImage signature validation failed. Download an official signed release manually.");
    const auto oldKeys = oldResult.keyFingerprints();
    const auto newKeys = newResult.keyFingerprints();
    bool same = false;
    for (const auto& key : oldKeys) for (const auto& candidate : newKeys) if (key == candidate) same = true;
    require(same, "The update uses a different signing key.");
    require(UpdatableAppImage(newFile.toStdString()).appImageType() == 2, "Invalid AppImage format.");
    QFile file(newFile); require(file.open(QIODevice::ReadOnly), "Cannot read candidate.");
    const auto header = file.read(20);
    require(header.size() == 20 && header[4] == 2 && header[5] == 1 &&
            static_cast<unsigned char>(header[18]) == 62 && header[19] == 0, "Wrong AppImage architecture.");
}
QProcessEnvironment cleanEnvironment()
{
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const char* name : {"APPIMAGE", "APPDIR", "ARGV0", "LD_LIBRARY_PATH", "LD_PRELOAD", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "SNAPTRAY_UPDATE_ACK", "SNAPTRAY_UPDATE_TOKEN"}) environment.remove(name);
    // A staged version probe/restart must not offer desktop integration for the temporary file.
    environment.insert("APPIMAGELAUNCHER_DISABLE", "1");
    return environment;
}
void launch(const QString& image, const QString& server = {}, const QString& token = {}, qint64* pid = nullptr)
{
    QProcess child; child.setProgram(image); child.setWorkingDirectory(QFileInfo(image).absolutePath());
#ifdef SNAPTRAY_UPDATER_TEST_RUNNER
    // Only compiled into the integration-test executable, never the shipped helper.
    const auto runner = qEnvironmentVariable("SNAPTRAY_TEST_RUNNER");
    require(!runner.isEmpty(), "Missing integration-test runner.");
    child.setProgram(runner);
    child.setArguments({image});
#endif
    auto environment = cleanEnvironment();
    if (!server.isEmpty()) { environment.insert("SNAPTRAY_UPDATE_ACK", server); environment.insert("SNAPTRAY_UPDATE_TOKEN", token); }
    child.setProcessEnvironment(environment);
    child.setStandardOutputFile(QProcess::nullDevice()); child.setStandardErrorFile(QProcess::nullDevice());
    require(child.startDetached(pid), "Could not launch SnapTray.");
}
void verifyPending(const QString& image, const QString& stage, const QJsonObject& pending)
{
    require(hash(image) == pending.value("oldHash").toString(), "The installed AppImage changed. Remove the pending update and check again.");
    require(hash(stage + "/candidate.AppImage") == pending.value("newHash").toString(), "The downloaded update changed.");
    validate(image, stage + "/candidate.AppImage");
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    signal(SIGTERM, interrupt); signal(SIGINT, interrupt);
    try {
        const auto args = app.arguments();
        require(args.size() >= 3, "Usage: updater download|pending|apply|validate APPIMAGE [arguments]");
        const auto command = args[1];
        const auto image = QFileInfo(args[2]).absoluteFilePath();
        if (command == "validate") { validate(image, image); event({{"event", "valid"}}); return 0; }
        const auto stage = image + ".snaptray-update";
        struct stat status{};
        if (::lstat(QFile::encodeName(stage).constData(), &status) != 0)
            require(::mkdir(QFile::encodeName(stage).constData(), 0700) == 0, "The AppImage directory is not writable.");
        require(::lstat(QFile::encodeName(stage).constData(), &status) == 0 && S_ISDIR(status.st_mode) &&
                status.st_uid == getuid() && (status.st_mode & 0077) == 0, "Unsafe update staging directory.");
        QLockFile lock(stage + "/lock"); lock.setStaleLockTime(0);
        require(lock.tryLock(), "Another update is already in progress.");
        const auto backup = image + ".snaptray-backup";
        const auto transaction = stage + "/transaction.json";
        // Recover an interrupted apply before accepting a new operation.
        if (QFileInfo::exists(transaction)) {
            const auto journal = read(transaction);
            if (QFileInfo::exists(backup) && hash(backup) == journal.value("oldHash").toString()) {
                require(!QFileInfo::exists(image) || hash(image) == journal.value("newHash").toString() || hash(image) == journal.value("oldHash").toString(), "Installed file changed during interrupted update.");
                replaceFile(backup, image);
            }
            require(QFile::remove(transaction), "Cannot clear interrupted transaction.");
            QFile::remove(stage + "/pending.json");
            throw std::runtime_error("An interrupted update was rolled back. Check for updates again.");
        }
        if (command == "download") {
            require(args.size() == 5, "Invalid download arguments.");
            const auto version = args[4];
            require(QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+$").match(version).hasMatch(), "Invalid version.");
            const auto expected = "https://github.com/victorfu/snap-tray/releases/download/v" + version + "/SnapTray-" + version + "-x86_64.AppImage.zsync";
            require(args[3] == expected, "Invalid update source.");
            validate(image, image); // Never bootstrap trust from an unsigned source.
            const auto oldHash = hash(image);
            QStorageInfo storage(QFileInfo(image).absolutePath());
            require(storage.isValid() && storage.bytesAvailable() > QFileInfo(image).size() * 3, "Not enough free space for a safe update.");
            const auto seed = stage + "/seed.AppImage";
            QFile::remove(stage + "/pending.json"); QFile::remove(seed);
            require(QFile::copy(image, seed), "Cannot stage the current AppImage.");
            Updater updater(seed.toStdString(), false);
            updater.setUpdateInformation(("zsync|" + expected).toStdString());
            require(updater.start(), "Could not start the download.");
            while (!updater.isDone()) {
                if (cancelled) { updater.stop(); break; }
                double progress = 0; updater.progress(progress);
                event({{"event", "progress"}, {"progress", progress}});
                QThread::msleep(100);
            }
            while (!updater.isDone()) QThread::msleep(50);
            require(!cancelled && !updater.hasError(), "Download cancelled or failed.");
            event({{"event", "verifying"}});
            std::string downloaded; require(updater.pathToNewFile(downloaded), "Missing downloaded file.");
            const auto output = QFileInfo(QString::fromStdString(downloaded)).canonicalFilePath();
            require(QFileInfo(output).absolutePath() == stage && output != seed, "Unsafe update output path.");
            validate(seed, output);
            const auto candidate = stage + "/candidate.AppImage";
            QFile::remove(candidate); require(QFile::rename(output, candidate), "Cannot stage the downloaded update.");
            require(QFile::setPermissions(candidate, QFile::permissions(image)), "Cannot preserve executable permissions.");
            // A signed executable may now be queried; never execute an unverified download.
            QProcess probe; probe.setProcessEnvironment(cleanEnvironment()); probe.setProgram(candidate); probe.setArguments({"--version"}); probe.start();
            require(probe.waitForFinished(15000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0 &&
                    QString::fromUtf8(probe.readAllStandardOutput()).trimmed() == "SnapTray version " + version, "Downloaded AppImage version does not match the release.");
            save(stage + "/pending.json", {{"version", version}, {"oldHash", oldHash}, {"newHash", hash(candidate)}});
            QFile::remove(seed);
            event({{"event", "ready"}, {"version", version}}); return 0;
        }
        if (command == "discard") {
            for (const auto& name : {"pending.json", "candidate.AppImage", "seed.AppImage"}) {
                const auto path = stage + "/" + name;
                require(!QFileInfo::exists(path) || QFile::remove(path), "Cannot discard the downloaded update.");
            }
            event({{"event", "discarded"}}); return 0;
        }
        const auto pending = read(stage + "/pending.json");
        verifyPending(image, stage, pending);
        if (command == "pending") { event({{"event", "ready"}, {"version", pending.value("version")}}); return 0; }
        require(command == "apply" && args.size() == 5, "Invalid apply arguments.");
        bool pidOk = false; const auto parentPid = args[3].toLongLong(&pidOk);
        require(pidOk && parentPid > 1, "Invalid parent process.");
        const auto token = args[4]; require(QRegularExpression("^[a-f0-9-]{36}$").match(token).hasMatch(), "Invalid restart token.");
        const auto armed = stage + "/armed-" + token;
        save(armed, {{"ready", true}});
        int elapsed = 0;
        while (::kill(static_cast<pid_t>(parentPid), 0) == 0 && elapsed < kExitWaitMs && !cancelled) { QThread::msleep(100); elapsed += 100; }
        QFile::remove(armed);
        require(elapsed < kExitWaitMs && !cancelled, "SnapTray did not exit; update was not installed.");
        verifyPending(image, stage, pending);
        QLocalServer server; server.setSocketOptions(QLocalServer::UserAccessOption);
        const auto serverName = "snaptray-update-" + token;
        require(server.listen(serverName), "Cannot create restart confirmation channel.");
        const auto stagedBackup = stage + "/backup.AppImage";
        QFile::remove(stagedBackup);
        require(QFile::copy(image, stagedBackup), "Cannot back up the installed AppImage.");
        require(hash(stagedBackup) == pending.value("oldHash").toString(), "Installed AppImage changed during backup.");
        save(transaction, pending);
        replaceFile(stagedBackup, backup);
        qint64 childPid = 0;
        bool started = false;
        try {
            replaceFile(stage + "/candidate.AppImage", image);
            launch(image, serverName, token, &childPid);
            for (int waited = 0; waited < kStartupWaitMs && !cancelled; waited += 250) {
                if (server.waitForNewConnection(250)) {
                    auto* socket = server.nextPendingConnection();
                    if (socket->bytesAvailable() || socket->waitForReadyRead(3000)) started = socket->readAll() == token.toUtf8();
                    delete socket;
                    if (started) break;
                }
                if (::kill(static_cast<pid_t>(childPid), 0) != 0) break;
            }
        } catch (...) { started = false; }
        if (!started) {
            if (childPid > 1) {
                ::kill(-static_cast<pid_t>(childPid), SIGTERM);
                for (int i = 0; i < 30 && ::kill(static_cast<pid_t>(childPid), 0) == 0; ++i) QThread::msleep(100);
                if (::kill(static_cast<pid_t>(childPid), 0) == 0) ::kill(-static_cast<pid_t>(childPid), SIGKILL);
            }
            replaceFile(backup, image);
        }
        require(QFile::remove(transaction), "Cannot finish update transaction.");
        QFile::remove(stage + "/pending.json");
        if (!started) { launch(image); throw std::runtime_error("The new version did not start; the previous version was restored."); }
        event({{"event", "installed"}}); return 0;
    } catch (const std::exception& error) {
        event({{"event", "error"}, {"message", QString::fromUtf8(error.what())}}); return 1;
    }
}
