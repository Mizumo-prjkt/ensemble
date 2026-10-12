#include "SingleInstanceManager.h"
#include "debug/debug.hpp"
#include "themes/ThemeManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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

QString SingleInstanceManager::socketName() {
    const QString username = QString::fromLocal8Bit(qgetenv("USER"));
    if (!username.isEmpty()) {
        return QStringLiteral("ensemble_app_socket_") + username;
    }
    const quint32 hash = qHash(QDir::homePath());
    return QStringLiteral("ensemble_app_socket_") + QString::number(hash, 16);
}

QString SingleInstanceManager::pidFilePath() {
    return QDir::tempPath() + QDir::separator() + socketName() + QStringLiteral(".pid");
}

bool SingleInstanceManager::isProcessRunning(qint64 pid) {
    if (pid <= 0) return false;
#if defined(Q_OS_UNIX)
    if (kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
#elif defined(Q_OS_WIN)
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (hProcess != nullptr) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(hProcess, &exitCode)) {
            CloseHandle(hProcess);
            return exitCode == STILL_ACTIVE;
        }
        CloseHandle(hProcess);
    }
    return false;
#else
    return false;
#endif
}

bool SingleInstanceManager::terminateProcess(qint64 pid) {
    if (pid <= 0) return false;
    qCInfo(logApp) << "Attempting to terminate process ID:" << pid;
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
    const QString pidPath = pidFilePath();

    qint64 savedPid = 0;
    if (QFile::exists(pidPath)) {
        QFile file(pidPath);
        if (file.open(QIODevice::ReadOnly)) {
            savedPid = file.readAll().trimmed().toLongLong();
            file.close();
        }
    }

    QLocalSocket socket;
    socket.connectToServer(name);
    const bool connected = socket.waitForConnected(600);

    if (connected) {
        qCInfo(logApp) << "Detected another running Ensemble instance on local socket:" << name;

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
            }
        }
        socket.disconnectFromServer();

        if (disableGuiPrompt || !filePathToOpen.isEmpty()) {
            qCInfo(logApp) << "Request successfully forwarded to running instance (PID" << remotePid << "). Exiting helper process.";
            return SingleInstanceAction::ExitApplication;
        }

        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowTitle(QStringLiteral("Ensemble Already Running"));
        msgBox.setText(QStringLiteral(
            "<h3>An instance of Ensemble is already running</h3>"
            "<p>The existing window (Process ID: <b>%1</b>) has been brought to the front of your desktop.</p>")
            .arg(remotePid > 0 ? QString::number(remotePid) : QStringLiteral("Active")));
        msgBox.setInformativeText(QStringLiteral(
            "If the window did not appear or is frozen/unresponsive, you can terminate it from here and start a fresh instance."));

        QPushButton *bringToFrontBtn = msgBox.addButton(QStringLiteral("Switch to Existing Window"), QMessageBox::AcceptRole);
        QPushButton *killBtn = msgBox.addButton(QStringLiteral("Terminate Process && Start Fresh"), QMessageBox::DestructiveRole);
        QPushButton *cancelBtn = msgBox.addButton(QMessageBox::Cancel);

        msgBox.setDefaultButton(bringToFrontBtn);
        msgBox.setStyleSheet(ThemeManager::instance().getDialogStyleSheet());

        msgBox.exec();

        if (msgBox.clickedButton() == killBtn) {
            qCWarning(logApp) << "User requested terminating existing Ensemble instance (PID:" << remotePid << ")";
            if (remotePid > 0) {
                terminateProcess(remotePid);
            }
            QLocalServer::removeServer(name);
            QFile::remove(pidPath);
            QThread::msleep(300);
            return SingleInstanceAction::ProceedAsPrimary;
        }

        return SingleInstanceAction::ExitApplication;
    }

    // If socket failed to connect, check if a stuck process PID is still running
    if (savedPid > 0 && isProcessRunning(savedPid)) {
        qCWarning(logApp) << "Found running Ensemble PID" << savedPid << "but socket refused connection.";
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

    // Clean up stale socket/pid file from prior crashes
    QLocalServer::removeServer(name);
    QFile::remove(pidPath);
    return SingleInstanceAction::ProceedAsPrimary;
}

bool SingleInstanceManager::startListening() {
    const QString name = socketName();
    const QString pidPath = pidFilePath();

    QLocalServer::removeServer(name);

    if (!m_server.listen(name)) {
        qCWarning(logApp) << "Failed to start single instance local server:" << m_server.errorString();
        return false;
    }

    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstanceManager::onNewConnection);

    QFile pidFile(pidPath);
    if (pidFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        pidFile.write(QByteArray::number(QCoreApplication::applicationPid()));
        pidFile.close();
    }

    m_isListening = true;
    qCInfo(logApp) << "Single instance server listening on:" << name << "(PID:" << QCoreApplication::applicationPid() << ")";
    return true;
}

void SingleInstanceManager::stopListening() {
    if (m_isListening) {
        m_server.close();
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
