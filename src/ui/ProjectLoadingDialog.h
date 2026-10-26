#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QProgressBar;
class QFrame;

class ProjectLoadingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProjectLoadingDialog(const QString &filePath, QWidget *parent = nullptr);
    ~ProjectLoadingDialog() override = default;

    void setProgress(int percent, const QString &statusMessage);
    void logDiagnostic(const QString &message, bool isError = false);
    void finish();

    void updateTheme();

    int progressValue() const;
    QString currentStatus() const;

    void reject() override;

private:
    QLabel *m_titleLabel = nullptr;
    QLabel *m_fileNameLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_statusLabel = nullptr;
    class QTextEdit *m_detailsLog = nullptr;
    QFrame *m_cardFrame = nullptr;
};
