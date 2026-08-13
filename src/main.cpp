#include "ui/MainWindow.h"
#include "ui/AppIcon.h"

#include <QApplication>

int main(int argc, char *argv[])
{

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Ensemble"));
    app.setOrganizationName(QStringLiteral("Ensemble"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));
    app.setWindowIcon(AppIcon::icon());

    MainWindow window;
    window.show();

    return app.exec();
}
