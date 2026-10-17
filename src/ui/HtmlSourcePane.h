#pragma once

#include <QList>
#include <QStringList>
#include <QWidget>

class CodeEditor;
class QFrame;
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

    static QStringList getTagPathAt(const QString &text, int cursorPos);

public slots:
    void setHtml(const QString &html);
    void forceApply();
    void formatHtml();
    void copyAo3Html();
    void insertTag(const QString &tag, const QString &defaultClass = QString(), const QString &defaultInner = QString());
    void updateTheme();
    void updateSettings();

signals:
    void applyRequested(const QString &html);
    void editingStarted();
    void editingFinished();
    void htmlChanged(const QString &html);
    void openProblemsRequested();
    void statusMessageRequested(const QString &message);

private slots:
    void onApplyClicked();
    void onTextChanged();
    void runLinter();
    void toggleIssueDetails();
    void onCursorPositionChanged();
    void showEditorContextMenu(const QPoint &pos);

private:
    bool eventFilter(QObject *obj, QEvent *event) override;
    bool tryExpandAutoblock();
    static QString formatHtmlSource(const QString &rawHtml);

    CodeEditor *m_editor = nullptr;
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_formatButton = nullptr;
    QPushButton *m_copyAo3Button = nullptr;

    // Issue Banner Card (collapsible, counts issues instead of listing them)
    QFrame *m_issueCard = nullptr;
    QLabel *m_issueSummaryLabel = nullptr;
    QPushButton *m_toggleDetailsBtn = nullptr;
    QPushButton *m_openProblemsBtn = nullptr;
    QLabel *m_issueDetailsLabel = nullptr;

    // Quick Tags Toolbar & Tag Breadcrumb ("say the tags")
    QFrame *m_tagBarWidget = nullptr;
    QLabel *m_tagBarTitleLabel = nullptr;
    QLabel *m_tagBreadcrumbLabel = nullptr;
    QList<QPushButton *> m_quickTagButtons;

    QTimer *m_linterTimer = nullptr;
    bool m_blockChanges = false;
    bool m_editing = false;
    bool m_detailsExpanded = false;
};

