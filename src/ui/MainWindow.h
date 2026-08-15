#pragma once

#include "model/Ao3Project.h"

#include <QMainWindow>

#include "ui/ProblemsDialog.h"

class FindReplaceDialog;
class AboutDialog;
class CreditsDialog;
class DebugConsoleDialog;
class ProblemsDialog;
class ChapterSidebar;
class CssEditorPane;
class EditorPane;
class HtmlSourcePane;
class MainMenuWidget;
class PreviewPane;
class QTimer;
class QTabWidget;
class QSplitter;
class QStackedWidget;
class QPushButton;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onExportChapterHtml();
    void onExportAllChapters();
    void onCopyHtmlToClipboard();
    void onShowMainMenu();
    void onStartWriting();
    void onShowAbout();
    void onShowCredits();
    void onImportFromAo3();
    void onToggleLivePreview(bool checked);
    void onPopOutPreview();
    void onShowDebugConsole();
    void onShowProblemsDialog();
    void updateProblemsDiagnostics();
    void updateCursorPosition();
    void toggleInsertMode();
    void updateCapsLockState(bool active);
    void onEditorContentChanged();
    void onEditorHtmlExported(const QString &html);
    void onHtmlApplyRequested(const QString &html);
    void onHtmlEditingStarted();
    void onHtmlEditingFinished();
    void onHtmlChanged(const QString &html);
    void onTabChanged(int index);
    void onCssChanged(const QString &css);
    void onCssClassesChanged(const QStringList &classNames);
    void onChapterSelected(int index);
    void onChaptersReordered();
    void onFindRequested();
    void onReplaceRequested();

    void debouncedSyncHtml();
    void debouncedUpdatePreview();
    void updateStatusBar();
    void updateWindowTitle();
    void schedulePreviewUpdate();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool maybeSave();
    bool saveProjectToPath(const QString &path);
    void loadCurrentChapterIntoEditors();
    void saveCurrentChapterFromEditors();
    void setupStatusBar();
    QWidget *activeTextEditor() const;

    Ao3Project m_project;
    QStackedWidget *m_centralStack = nullptr;
    MainMenuWidget *m_mainMenuWidget = nullptr;

    ChapterSidebar *m_chapterSidebar = nullptr;
    EditorPane *m_editorPane = nullptr;
    HtmlSourcePane *m_htmlPane = nullptr;
    CssEditorPane *m_cssPane = nullptr;
    PreviewPane *m_previewPane = nullptr;
    PreviewPane *m_previewPopup = nullptr;
    QTabWidget *m_editorTabs = nullptr;
    QSplitter *m_mainSplitter = nullptr;
    QAction *m_previewToggleAction = nullptr;

    AboutDialog *m_aboutDialog = nullptr;
    CreditsDialog *m_creditsDialog = nullptr;
    FindReplaceDialog *m_findReplaceDialog = nullptr;
    DebugConsoleDialog *m_debugConsoleDialog = nullptr;
    ProblemsDialog *m_problemsDialog = nullptr;

    QPushButton *m_problemsBadgeButton = nullptr;
    QLabel *m_statusInfoLabel = nullptr;
    QLabel *m_lineColLabel = nullptr;
    QPushButton *m_insertModeButton = nullptr;
    QLabel *m_capsLockLabel = nullptr;
    bool m_capsLockOn = false;
    QList<ProblemItem> m_currentProblems;

    QTimer *m_htmlSyncTimer = nullptr;
    QTimer *m_previewTimer = nullptr;

    bool m_syncInProgress = false;
    bool m_htmlSourceEditing = false;
    bool m_previewInPopup = false;
    QString m_pendingHtml;
};
