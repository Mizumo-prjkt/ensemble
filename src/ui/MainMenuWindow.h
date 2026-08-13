#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

class MainMenuWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MainMenuWidget(QWidget *parent = nullptr);

signals:
    void newProjectRequested();
    void openProjectRequested();
    void saveProjectRequested();
    void startWritingRequested();

private:
    void setupUi();

    QVBoxLayout *m_layout = nullptr;
    QPushButton *m_newBtn = nullptr;
    QPushButton *m_openBtn = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_startWritingBtn = nullptr;
};

using MainMenuDialog = MainMenuWidget;