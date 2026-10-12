#pragma once

#include <QDialog>

class QLabel;
class QFrame;
class QPushButton;
class QVBoxLayout;

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

public slots:
    void updateTheme();

private:
    void setupUi();

    QVBoxLayout *m_layout = nullptr;
    QFrame *m_infoCard = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subTitleLabel = nullptr;
    QLabel *m_aboutText = nullptr;
    QFrame *m_divider = nullptr;
    QLabel *m_creditsText = nullptr;
    QLabel *m_licenseText = nullptr;
    QPushButton *m_closeBtn = nullptr;
};