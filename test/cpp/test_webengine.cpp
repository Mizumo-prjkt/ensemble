#include <QApplication>
#include <QWebEngineView>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QWebEngineView view;
    view.setHtml("<html><body style='background:#1a1a24;color:#fff;'><h1>WebEngine Test</h1></body></html>");
    view.show();
    qDebug() << "WebEngine initialized successfully!";
    return 0;
}
