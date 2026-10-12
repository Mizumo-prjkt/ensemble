#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QFrame;

class MainMenuWidget : public QWidget {
  Q_OBJECT

public:
  explicit MainMenuWidget(QWidget *parent = nullptr);

public slots:
  void updateTheme();
  void setProjectState(bool hasActiveProject, const QString &title = QString());

signals:
  void newProjectRequested();
  void openProjectRequested();
  void saveProjectRequested();
  void closeProjectRequested();
  void startWritingRequested();
  void settingsRequested();
  void helpRequested();

private:
  void setupUi();
  QPushButton *createActionButton(const QString &objectName,
                                  const QString &iconAndTitle,
                                  const QString &subtext,
                                  QLabel **outTitleLabel = nullptr,
                                  QLabel **outSubtextLabel = nullptr);

  QVBoxLayout *m_layout = nullptr;
  QPushButton *m_continueBtn = nullptr;
  QLabel *m_continueTitleLabel = nullptr;
  QLabel *m_continueSubLabel = nullptr;
  QPushButton *m_newBtn = nullptr;
  QPushButton *m_openBtn = nullptr;
  QPushButton *m_saveBtn = nullptr;
  QPushButton *m_closeBtn = nullptr;
  QPushButton *m_helpBtn = nullptr;
  QPushButton *m_settingsBtn = nullptr;
  QPushButton *m_themeToggleBtn = nullptr;

  QLabel *m_titleLabel = nullptr;
  QLabel *m_subtitleLabel = nullptr;
  QFrame *m_infoBox = nullptr;
  QLabel *m_motdTitle = nullptr;
  QLabel *m_motdText = nullptr;
  QLabel *m_actionsLabel = nullptr;
  bool m_hasActiveProject = false;
};

using MainMenuDialog = MainMenuWidget;