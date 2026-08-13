#pragma once

#include <QWidget>

class CodeEditor;
class QLabel;
class QPushButton;
class QTimer;

class HtmlSourcePane : public QWidget
{
    Q_OBJECT

public:
    explicit HtmlSourcePane(QWidget *parent = nullptr);

    QString html() const;
    bool isEditing() const { return m_editing; }
    CodeEditor *editor() const { return m_editor; }

public slots:
    void setHtml(const QString &html);
    void forceApply();

signals:
    void applyRequested(const QString &html);
    void editingStarted();
    void editingFinished();
    void htmlChanged(const QString &html);

private slots:
    void onApplyClicked();
    void onTextChanged();
    void runLinter();

private:
    bool eventFilter(QObject *obj, QEvent *event) override;
    bool tryExpandAutoblock();

    CodeEditor *m_editor = nullptr;
    QPushButton *m_applyButton = nullptr;
    QLabel *m_linterWarning = nullptr;
    QTimer *m_linterTimer = nullptr;
    bool m_blockChanges = false;
    bool m_editing = false;
};
