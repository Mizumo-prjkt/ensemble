#pragma once

#include <QObject>
#include <QTcpServer>

class Ao3AuthServer : public QObject
{
    Q_OBJECT
public:
    explicit Ao3AuthServer(QObject *parent = nullptr);
    ~Ao3AuthServer() override;

    bool start();
    void stop();
    quint16 port() const;

    /// URL the user should open after logging in to send cookies back
    QString callbackHelperUrl() const;

signals:
    void cookieReceived(const QString &sessionCookie, const QString &username);
    void errorOccurred(const QString &message);

private slots:
    void onNewConnection();

private:
    void handleRequest(const QByteArray &requestData, class QTcpSocket *socket);
    void sendResponse(QTcpSocket *socket, int statusCode,
                      const QString &contentType, const QByteArray &body);
    QByteArray buildHelperPage() const;
    QByteArray buildSuccessPage() const;

    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
};
