#include "Ao3AuthServer.h"

#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

Ao3AuthServer::Ao3AuthServer(QObject *parent) : QObject(parent)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &Ao3AuthServer::onNewConnection);
}

Ao3AuthServer::~Ao3AuthServer()
{
    stop();
}

bool Ao3AuthServer::start()
{
    // Bind to loopback on a random available port
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        emit errorOccurred(QStringLiteral("Failed to start auth callback server: ") + m_server->errorString());
        return false;
    }
    m_port = m_server->serverPort();
    return true;
}

void Ao3AuthServer::stop()
{
    if (m_server->isListening()) {
        m_server->close();
    }
}

quint16 Ao3AuthServer::port() const
{
    return m_port;
}

QString Ao3AuthServer::callbackHelperUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1/").arg(m_port);
}

void Ao3AuthServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            const QByteArray data = socket->readAll();
            handleRequest(data, socket);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void Ao3AuthServer::handleRequest(const QByteArray &requestData, QTcpSocket *socket)
{
    // Parse the HTTP request line: "GET /path?query HTTP/1.1"
    const int lineEnd = requestData.indexOf("\r\n");
    if (lineEnd < 0) {
        sendResponse(socket, 400, QStringLiteral("text/plain"), "Bad Request");
        return;
    }

    const QByteArray requestLine = requestData.left(lineEnd);
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        sendResponse(socket, 400, QStringLiteral("text/plain"), "Bad Request");
        return;
    }

    const QByteArray method = parts.at(0);
    const QUrl requestUrl = QUrl(QString::fromUtf8(parts.at(1)));
    const QString path = requestUrl.path();

    if (method == "GET" && path == QStringLiteral("/")) {
        // Serve the helper page with cookie extraction JavaScript
        sendResponse(socket, 200, QStringLiteral("text/html; charset=utf-8"), buildHelperPage());
    } else if (method == "GET" && path == QStringLiteral("/callback")) {
        // Receive session cookie via query parameters
        const QUrlQuery query(requestUrl.query());
        const QString sessionCookie = query.queryItemValue(QStringLiteral("cookie"), QUrl::FullyDecoded);
        const QString username = query.queryItemValue(QStringLiteral("user"), QUrl::FullyDecoded);

        if (sessionCookie.isEmpty()) {
            sendResponse(socket, 400, QStringLiteral("text/plain"),
                         "Missing 'cookie' parameter. Please try again.");
            return;
        }

        sendResponse(socket, 200, QStringLiteral("text/html; charset=utf-8"), buildSuccessPage());
        emit cookieReceived(sessionCookie, username);
    } else if (method == "OPTIONS") {
        // Handle CORS preflight
        QByteArray response;
        response += "HTTP/1.1 204 No Content\r\n";
        response += "Access-Control-Allow-Origin: https://archiveofourown.org\r\n";
        response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        response += "Access-Control-Allow-Headers: Content-Type\r\n";
        response += "Connection: close\r\n\r\n";
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    } else {
        sendResponse(socket, 404, QStringLiteral("text/plain"), "Not Found");
    }
}

void Ao3AuthServer::sendResponse(QTcpSocket *socket, int statusCode,
                                  const QString &contentType, const QByteArray &body)
{
    const char *statusText = "OK";
    if (statusCode == 400) statusText = "Bad Request";
    else if (statusCode == 404) statusText = "Not Found";

    QByteArray response;
    response += QStringLiteral("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(QString::fromLatin1(statusText)).toUtf8();
    response += QStringLiteral("Content-Type: %1\r\n").arg(contentType).toUtf8();
    response += QStringLiteral("Content-Length: %1\r\n").arg(body.size()).toUtf8();
    response += "Access-Control-Allow-Origin: https://archiveofourown.org\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

QByteArray Ao3AuthServer::buildHelperPage() const
{
    const QString html = QStringLiteral(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Ensemble — AO3 Session Transfer</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: 'Segoe UI', -apple-system, sans-serif;
    background: #1e1f29;
    color: #f8f8f2;
    display: flex;
    justify-content: center;
    align-items: center;
    min-height: 100vh;
    padding: 20px;
  }
  .card {
    background: #282a36;
    border-radius: 16px;
    padding: 40px;
    max-width: 640px;
    width: 100%;
    box-shadow: 0 8px 32px rgba(0,0,0,0.5);
  }
  h1 { color: #bd93f9; font-size: 24px; margin-bottom: 8px; }
  .subtitle { color: #6272a4; font-size: 14px; margin-bottom: 28px; }
  .step {
    background: #21222c;
    border-radius: 10px;
    padding: 20px;
    margin-bottom: 16px;
    border-left: 4px solid #bd93f9;
  }
  .step-num {
    display: inline-block;
    background: #bd93f9;
    color: #282a36;
    font-weight: 800;
    width: 28px; height: 28px;
    border-radius: 50%;
    text-align: center;
    line-height: 28px;
    font-size: 14px;
    margin-right: 10px;
  }
  .step-title { font-weight: 700; font-size: 15px; display: inline; }
  .step-body { margin-top: 10px; color: #ccc; font-size: 13px; line-height: 1.6; }
  .btn {
    display: inline-block;
    background: #50fa7b;
    color: #282a36;
    font-weight: 800;
    font-size: 14px;
    border: none;
    border-radius: 8px;
    padding: 12px 24px;
    cursor: pointer;
    text-decoration: none;
    margin-top: 10px;
    transition: transform 0.1s, box-shadow 0.1s;
  }
  .btn-bookmarklet {
    background: #ff79c6;
    color: #fff;
    font-size: 14px;
    padding: 10px 20px;
    cursor: grab;
  }
  .code-box {
    background: #191a21;
    border: 1px solid #44475a;
    border-radius: 6px;
    padding: 12px;
    font-family: monospace;
    font-size: 12px;
    color: #f1fa8c;
    margin-top: 8px;
    word-break: break-all;
  }
  .privacy-note {
    margin-top: 20px;
    padding: 14px;
    background: #21222c;
    border-radius: 8px;
    border: 1px solid #44475a;
    font-size: 11px;
    color: #8be9fd;
    line-height: 1.5;
  }
</style>
</head>
<body>
<div class="card">
  <h1>🎵 Ensemble — Session Transfer Helper</h1>
  <p class="subtitle">Transfer your AO3 session to Ensemble in 1 click</p>

  <div class="step">
    <span class="step-num">1</span>
    <p class="step-title">Option A: Bookmarklet (Easiest)</p>
    <div class="step-body">
      <p style="margin-bottom:8px;">Drag this button to your browser's <b>Bookmarks Bar</b>:</p>
      <a class="btn btn-bookmarklet" id="bookmarklet" href="javascript:void(0);">
        📎 Send Session to Ensemble
      </a>
      <p style="margin-top:10px; color:#aaa; font-size:12px;">Then navigate to <a href="https://archiveofourown.org" target="_blank" style="color:#8be9fd">archiveofourown.org</a> while logged in and click the bookmarklet!</p>
    </div>
  </div>

  <div class="step">
    <span class="step-num">2</span>
    <p class="step-title">Option B: F12 Console Command</p>
    <div class="step-body">
      <p>While on <a href="https://archiveofourown.org" target="_blank" style="color:#8be9fd">AO3</a>, press <code>F12</code> → <b>Console</b> tab, paste this command, and press Enter:</p>
      <div class="code-box" id="consoleCode"></div>
    </div>
  </div>

  <div class="privacy-note">
    🔒 <b>Privacy:</b> Session data is sent directly to <code>127.0.0.1:%1</code> (this computer only). Nothing leaves your machine.
  </div>
</div>

<script>
const CALLBACK_PORT = %1;
const CALLBACK_URL = 'http://127.0.0.1:' + CALLBACK_PORT + '/callback';

const snippet = "fetch('" + CALLBACK_URL + "?cookie=' + encodeURIComponent((document.cookie.match(/_otwarchive_session=([^;]+)/)||[])[1]||'')).then(()=>alert('✅ Session sent to Ensemble!'));";

document.getElementById('consoleCode').textContent = snippet;

const bookmarkletCode = "javascript:void((function(){var c=(document.cookie.match(/_otwarchive_session=([^;]+)/)||[])[1];if(!c){alert('No AO3 session found on this page. Make sure you are logged in to AO3!');return;}var img=new Image();img.src='" + CALLBACK_URL + "?cookie='+encodeURIComponent(c);alert('✅ Session sent to Ensemble!');})())";

document.getElementById('bookmarklet').href = bookmarkletCode;
</script>
</body>
</html>
)HTML").arg(m_port);

    return html.toUtf8();
}

QByteArray Ao3AuthServer::buildSuccessPage() const
{
    const QString html = QStringLiteral(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Ensemble — Session Received!</title>
<style>
  body {
    font-family: 'Segoe UI', -apple-system, sans-serif;
    background: #1e1f29;
    color: #f8f8f2;
    display: flex;
    justify-content: center;
    align-items: center;
    min-height: 100vh;
  }
  .card {
    background: #282a36;
    border-radius: 16px;
    padding: 40px;
    text-align: center;
    box-shadow: 0 8px 32px rgba(0,0,0,0.5);
  }
  h1 { color: #50fa7b; font-size: 28px; }
  p { color: #ccc; margin-top: 12px; }
</style>
</head>
<body>
<div class="card">
  <h1>✅ Session Received!</h1>
  <p>Your AO3 session has been securely transferred to Ensemble.</p>
  <p style="color:#6272a4; font-size:13px; margin-top:16px;">You can close this browser tab now and return to Ensemble.</p>
</div>
</body>
</html>
)HTML");

    return html.toUtf8();
}
