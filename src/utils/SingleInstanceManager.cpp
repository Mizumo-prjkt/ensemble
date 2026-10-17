#include "SingleInstanceManager.h"
#include "debug/debug.hpp"
#include "themes/ThemeManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QUrl>
#include <QWidget>
#include <QWindow>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <csignal>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

SingleInstanceManager::SingleInstanceManager(QWidget *targetWindow, QObject *parent)
    : QObject(parent), m_targetWindow(targetWindow) {
}

SingleInstanceManager::~SingleInstanceManager() {
    stopListening();
}

QString SingleInstanceManager::runtimeDir() {
#if defined(Q_OS_UNIX)
    const QByteArray xdg = qgetenv("XDG_RUNTIME_DIR");
    if (!xdg.isEmpty()) {
        const QString dir = QString::fromLocal8Bit(xdg);
        if (QDir(dir).exists()) {
            return dir;
        }
    }
#endif
    return QDir::tempPath();
}

QString SingleInstanceManager::socketName() {
    const QString username = QString::fromLocal8Bit(qgetenv("USER"));
    if (!username.isEmpty()) {
        return QStringLiteral("ensemble_app_socket_") + username;
    }
    const quint32 hash = qHash(QDir::homePath());
    return QStringLiteral("ensemble_app_socket_") + QString::number(hash, 16);
}

QString SingleInstanceManager::socketPath() {
#if defined(Q_OS_UNIX)
    return QDir(runtimeDir()).filePath(socketName());
#else
    return socketName();
#endif
}

QString SingleInstanceManager::pidFilePath() {
    return QDir(runtimeDir()).filePath(socketName() + QStringLiteral(".pid"));
}

QString SingleInstanceManager::currentBootId() {
#if defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/sys/kernel/random/boot_id"));
    if (file.open(QIODevice::ReadOnly)) {
        const QString id = QString::fromUtf8(file.readAll()).trimmed();
        file.close();
        if (!id.isEmpty()) {
            return id;
        }
    }
#elif defined(Q_OS_WIN)
    const ULONGLONG uptimeMs = GetTickCount64();
    const qint64 bootMinute = (QDateTime::currentSecsSinceEpoch() * 1000 - uptimeMs) / 60000;
    return QString::number(bootMinute);
#endif
    return QString();
}

QString SingleInstanceManager::getProcessExecutable(qint64 pid) {
    if (pid <= 0) return QString();
#if defined(Q_OS_LINUX)
    // 1. Read /proc/<pid>/exe symlink
    const QString exeLink = QStringLiteral("/proc/%1/exe").arg(pid);
    const QString target = QFile::symLinkTarget(exeLink);
    if (!target.isEmpty()) {
        return target;
    }
    // 2. Fallback to /proc/<pid>/cmdline
    QFile cmdFile(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (cmdFile.open(QIODevice::ReadOnly)) {
        const QByteArray cmdline = cmdFile.readAll();
        cmdFile.close();
        const int nullIdx = cmdline.indexOf('\0');
        const QByteArray firstArg = (nullIdx >= 0) ? cmdline.left(nullIdx) : cmdline;
        if (!firstArg.isEmpty()) {
            return QString::fromUtf8(firstArg);
        }
    }
    // 3. Fallback to /proc/<pid>/comm
    QFile commFile(QStringLiteral("/proc/%1/comm").arg(pid));
    if (commFile.open(QIODevice::ReadOnly)) {
        const QString comm = QString::fromUtf8(commFile.readAll()).trimmed();
        commFile.close();
        if (!comm.isEmpty()) {
            return comm;
        }
    }
#elif defined(Q_OS_WIN)
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (hProcess != nullptr) {
        wchar_t path[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
            CloseHandle(hProcess);
            return QString::fromWCharArray(path);
        }
        CloseHandle(hProcess);
    }
#endif
    return QString();
}

bool SingleInstanceManager::isEnsembleProcess(qint64 pid) {
    if (pid <= 0) return false;
    if (pid == QCoreApplication::applicationPid()) {
        return true;
    }
    const QString exePath = getProcessExecutable(pid);
    if (exePath.isEmpty()) {
        return false;
    }
    const QString fileName = QFileInfo(exePath).fileName().toLower();
    const QString currentApp = QFileInfo(QCoreApplication::applicationFilePath()).fileName().toLower();
    if (!currentApp.isEmpty() && (fileName == currentApp || fileName.startsWith(currentApp))) {
        return true;
    }
    if (fileName.contains(QStringLiteral("ensemble"))) {
        return true;
    }
    if (fileName.startsWith(QStringLiteral("test_"))) {
        return true;
    }
    return false;
}

bool SingleInstanceManager::isProcessRunning(qint64 pid) {
    if (pid <= 0) return false;
#if defined(Q_OS_UNIX)
    if (kill(static_cast<pid_t>(pid), 0) != 0) {
        return false;
    }
    // Process is active: verify it is an actual Ensemble process
    return isEnsembleProcess(pid);
#elif defined(Q_OS_WIN)
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (hProcess != nullptr) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(hProcess, &exitCode)) {
            CloseHandle(hProcess);
            if (exitCode == STILL_ACTIVE) {
                return isEnsembleProcess(pid);
            }
        } else {
            CloseHandle(hProcess);
        }
    }
    return false;
#else
    return false;
#endif
}

bool SingleInstanceManager::terminateProcess(qint64 pid) {
    if (pid <= 0) return false;
    if (!isEnsembleProcess(pid)) {
        qCWarning(logApp) << "Refusing to terminate process" << pid << "because it is not verified as an Ensemble process.";
        return false;
    }
    qCInfo(logApp) << "Attempting to terminate verified Ensemble process ID:" << pid;
#if defined(Q_OS_UNIX)
    const pid_t p = static_cast<pid_t>(pid);
    if (kill(p, SIGTERM) == 0) {
        for (int i = 0; i < 15; ++i) {
            QThread::msleep(100);
            if (kill(p, 0) != 0) {
                qCInfo(logApp) << "Process" << pid << "terminated cleanly via SIGTERM.";
                return true;
            }
        }
    }
    qCWarning(logApp) << "Process" << pid << "did not exit after SIGTERM, sending SIGKILL.";
    kill(p, SIGKILL);
    QThread::msleep(200);
    return kill(p, 0) != 0;
#elif defined(Q_OS_WIN)
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (hProcess != nullptr) {
        BOOL res = TerminateProcess(hProcess, 1);
        CloseHandle(hProcess);
        return res != FALSE;
    }
    return false;
#else
    return false;
#endif
}

SingleInstanceAction SingleInstanceManager::checkAndHandle(bool disableGuiPrompt, const QString &filePathToOpen) {
    const QString name = socketName();
    const QString sockPath = socketPath();
    const QString pidPath = pidFilePath();
    const QString currentBoot = currentBootId();

    qint64 savedPid = 0;
    QString savedBootId;
    if (QFile::exists(pidPath)) {
        QFile file(pidPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray raw = file.readAll().trimmed();
            file.close();
            QJsonParseError parseErr;
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseErr);
            if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject obj = doc.object();
                savedPid = obj.value(QStringLiteral("pid")).toVariant().toLongLong();
                savedBootId = obj.value(QStringLiteral("bootId")).toString();
            } else {
                savedPid = raw.toLongLong();
            }
        }
    }

    // Check if the PID file was created during a prior boot session (machine restart)
    if (!savedBootId.isEmpty() && !currentBoot.isEmpty() && savedBootId != currentBoot) {
        qCInfo(logApp) << "Stale PID file detected from prior boot session (saved boot ID:"
                       << savedBootId << ", current boot ID:" << currentBoot << "). Cleaning up stale socket and PID file.";
        QLocalServer::removeServer(sockPath);
        QLocalServer::removeServer(name);
        QFile::remove(pidPath);
        return SingleInstanceAction::ProceedAsPrimary;
    }

    // Try connecting to existing local socket
    QLocalSocket socket;
    socket.connectToServer(sockPath);
    bool connected = socket.waitForConnected(600);
    if (!connected && sockPath != name) {
        // Fallback check on old /tmp socket name if runtime dir differs
        socket.connectToServer(name);
        connected = socket.waitForConnected(400);
    }

    if (connected) {
        qCInfo(logApp) << "Connected to active Ensemble instance on local socket:" << sockPath;

        // Command existing instance to raise its window to front or open file
        if (!filePathToOpen.isEmpty()) {
            QString clean = filePathToOpen.trimmed();
            if ((clean.startsWith(QLatin1Char('"')) && clean.endsWith(QLatin1Char('"'))) ||
                (clean.startsWith(QLatin1Char('\'')) && clean.endsWith(QLatin1Char('\'')))) {
                clean = clean.mid(1, clean.length() - 2).trimmed();
            }
            if (clean.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
                clean = QUrl(clean).toLocalFile();
            }
            const QString absPath = QFileInfo(clean).absoluteFilePath();
            socket.write(QStringLiteral("OPEN:%1\n").arg(absPath).toUtf8());
        } else {
            socket.write("RAISE\n");
        }
        socket.flush();
        socket.waitForBytesWritten(500);

        qint64 remotePid = savedPid;
        bool receivedAck = false;
        if (socket.waitForReadyRead(1500)) {
            const QByteArray response = socket.readAll();
            const QString respStr = QString::fromUtf8(response);
            const QStringList lines = respStr.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                if (line.startsWith(QLatin1String("PID:"))) {
                    bool ok = false;
                    qint64 p = line.mid(4).trimmed().toLongLong(&ok);
                    if (ok && p > 0) remotePid = p;
                }
                if (line.contains(QLatin1String("STATUS:OK"))) {
                    receivedAck = true;
                }
            }
        }
        socket.disconnectFromServer();

        // If the running instance acknowledged the command, it is alive and healthy!
        // We brought it to front and can exit cleanly without showing any modal popup.
        if (receivedAck || disableGuiPrompt || !filePathToOpen.isEmpty()) {
            qCInfo(logApp) << "Window raise request acknowledged by existing Ensemble instance (PID:" << remotePid << "). Exiting helper process.";
            return SingleInstanceAction::ExitApplication;
        }

        // Socket connected but did not reply with STATUS:OK within timeout -> frozen/hung instance
        qCWarning(logApp) << "Existing Ensemble instance connected but failed to acknowledge command within timeout.";
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowTitle(QStringLiteral("Ensemble Instance Not Responding"));
        msgBox.setText(QStringLiteral(
            "<h3>An instance of Ensemble is already running (PID: %1)</h3>"
            "<p>The application connected to the existing instance, but the instance did not respond to window commands.</p>")
            .arg(remotePid > 0 ? QString::number(remotePid) : QStringLiteral("Active")));
        msgBox.setInformativeText(QStringLiteral(
            "Would you like to terminate the unresponsive process and start a fresh instance?"));

        QPushButton *killBtn = msgBox.addButton(QStringLiteral("Terminate Process && Start Fresh"), QMessageBox::DestructiveRole);
        QPushButton *cancelBtn = msgBox.addButton(QMessageBox::Cancel);
        msgBox.setDefaultButton(killBtn);
        msgBox.setStyleSheet(ThemeManager::instance().getDialogStyleSheet());

        msgBox.exec();

        if (msgBox.clickedButton() == killBtn) {
            qCWarning(logApp) << "User requested terminating unresponsive Ensemble instance (PID:" << remotePid << ")";
            if (remotePid > 0) {
                terminateProcess(remotePid);
            }
            QLocalServer::removeServer(sockPath);
            QLocalServer::removeServer(name);
            QFile::remove(pidPath);
            QThread::msleep(300);
            return SingleInstanceAction::ProceedAsPrimary;
        }

        return SingleInstanceAction::ExitApplication;
    }

    // Socket failed to connect: check if a verified Ensemble process is actually running
    if (savedPid > 0 && isProcessRunning(savedPid)) {
        qCWarning(logApp) << "Found verified running Ensemble PID" << savedPid << "but socket refused connection.";
        if (!disableGuiPrompt) {
            QMessageBox msgBox;
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setWindowTitle(QStringLiteral("Stuck Ensemble Process Detected"));
            msgBox.setText(QStringLiteral(
                "<h3>An existing Ensemble process was found (PID: %1)</h3>"
                "<p>The process is running in the background but is not responding or accepting window commands.</p>")
                .arg(savedPid));
            msgBox.setInformativeText(QStringLiteral(
                "Would you like to terminate the stuck process in the process manager and start a fresh instance?"));

            QPushButton *killBtn = msgBox.addButton(QStringLiteral("Terminate Stuck Process && Launch"), QMessageBox::DestructiveRole);
            QPushButton *cancelBtn = msgBox.addButton(QMessageBox::Cancel);
            msgBox.setDefaultButton(killBtn);
            msgBox.setStyleSheet(ThemeManager::instance().getDialogStyleSheet());

            msgBox.exec();

            if (msgBox.clickedButton() == killBtn) {
                terminateProcess(savedPid);
                QLocalServer::removeServer(sockPath);
                QLocalServer::removeServer(name);
                QFile::remove(pidPath);
                QThread::msleep(300);
                return SingleInstanceAction::ProceedAsPrimary;
            } else {
                return SingleInstanceAction::ExitApplication;
            }
        } else {
            terminateProcess(savedPid);
        }
    }

    // Clean up stale socket/pid files from prior crashes
    QLocalServer::removeServer(sockPath);
    QLocalServer::removeServer(name);
    QFile::remove(pidPath);
    return SingleInstanceAction::ProceedAsPrimary;
}

bool SingleInstanceManager::startListening() {
    const QString sockPath = socketPath();
    const QString name = socketName();
    const QString pidPath = pidFilePath();

    QLocalServer::removeServer(sockPath);
    if (sockPath != name) {
        QLocalServer::removeServer(name);
    }

    if (!m_server.listen(sockPath)) {
        qCWarning(logApp) << "Failed to start single instance local server:" << m_server.errorString();
        return false;
    }

    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstanceManager::onNewConnection);

    QJsonObject meta;
    meta[QStringLiteral("pid")] = QCoreApplication::applicationPid();
    meta[QStringLiteral("bootId")] = currentBootId();
    meta[QStringLiteral("appPath")] = QCoreApplication::applicationFilePath();
    meta[QStringLiteral("startedAt")] = QDateTime::currentSecsSinceEpoch();

    QFile pidFile(pidPath);
    if (pidFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        pidFile.write(QJsonDocument(meta).toJson(QJsonDocument::Compact));
        pidFile.close();
    }

    m_isListening = true;
    qCInfo(logApp) << "Single instance server listening on:" << sockPath << "(PID:" << QCoreApplication::applicationPid() << ")";
    return true;
}

void SingleInstanceManager::stopListening() {
    if (m_isListening) {
        m_server.close();
        QLocalServer::removeServer(socketPath());
        QLocalServer::removeServer(socketName());
        QFile::remove(pidFilePath());
        m_isListening = false;
    }
}

void SingleInstanceManager::onNewConnection() {
    while (m_server.hasPendingConnections()) {
        QLocalSocket *client = m_server.nextPendingConnection();
        if (!client) continue;

        connect(client, &QLocalSocket::readyRead, this, [this, client]() {
            const QByteArray cmd = client->readAll().trimmed();
            qCInfo(logApp) << "Received command from secondary instance:" << cmd;
            if (cmd.startsWith("OPEN:")) {
                QString openPath = QString::fromUtf8(cmd.mid(5)).trimmed();
                if ((openPath.startsWith(QLatin1Char('"')) && openPath.endsWith(QLatin1Char('"'))) ||
                    (openPath.startsWith(QLatin1Char('\'')) && openPath.endsWith(QLatin1Char('\'')))) {
                    openPath = openPath.mid(1, openPath.length() - 2).trimmed();
                }
                if (openPath.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
                    openPath = QUrl(openPath).toLocalFile();
                }
                openPath = QFileInfo(openPath).absoluteFilePath();
                bringWindowToFront();
                emit openFileRequested(openPath);
            } else if (cmd.startsWith("RAISE")) {
                bringWindowToFront();
            }

            const QString reply = QStringLiteral("PID:%1\nSTATUS:OK\n").arg(QCoreApplication::applicationPid());
            client->write(reply.toUtf8());
            client->flush();
        });

        connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
    }
}

void SingleInstanceManager::bringWindowToFront() {
    if (m_targetWindow) {
        qCInfo(logApp) << "Moving already active Ensemble window to the front of desktop...";
        if (m_targetWindow->isMinimized()) {
            m_targetWindow->showNormal();
        } else {
            m_targetWindow->show();
        }
        m_targetWindow->setWindowState((m_targetWindow->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        m_targetWindow->raise();
        m_targetWindow->activateWindow();
        if (auto *wh = m_targetWindow->windowHandle()) {
            wh->requestActivate();
        }
        QApplication::alert(m_targetWindow, 1500);
    }
}
