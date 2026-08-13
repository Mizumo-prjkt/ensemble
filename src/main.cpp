#include "debug/debug.hpp"
#include "ui/AppIcon.h"
#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[]) {
  EnsembleDebug::initDebugSystem();

  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("Ensemble"));
  app.setOrganizationName(QStringLiteral("Ensemble"));
  app.setApplicationVersion(QStringLiteral("1.1.0"));
  app.setWindowIcon(AppIcon::icon());

  ENSEMBLE_PROFILE_SCOPE("Application Window Initialization");

  MainWindow window;
  window.show();

  return app.exec();
}
