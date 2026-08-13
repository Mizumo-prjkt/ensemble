#pragma once

#include <QDialog>

class QLabel;
class QVBoxLayout;

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

private:
    void setupUi();

    QVBoxLayout *m_layout = nullptr;
};