#pragma once

#include <QMainWindow>
#include <QList>
#include <QString>
#include "model/Ao3Project.h"
#include "ui/ProblemsDialog.h"
#include "export/Ao3HtmlSanitizer.h"

enum class AppTheme;

class ChapterSidebar;
class EditorPane;
class HtmlSourcePane;
class CssEditorPane;
class PreviewPane;
class QTabWidget;
class QSplitter;
class QStackedWidget;
class QAction;
class QActionGroup;
class QTimer;
class QLabel;
class QPushButton;
class MainMenuWidget;
class AboutDialog;
class CreditsDialog;
class FindReplaceDialog;
class DebugConsoleDialog;
class SettingsDialog;
class HelpGuideDialog;
class AgentThoughtsDialog;
class LoreGraphDialog;
class McpServer;
class McpHandler;

class MainWindow : public QMainWindow
{
    Q_OBJECT
    friend class McpHandler;

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool hasActiveProject() const { return m_hasActiveProject; }
    void raiseToFront();
    bool openProjectFile(const QString &path);

    Ao3Project &project() { return m_project; }
    const Ao3Project &project() const { return m_project; }
    McpServer *mcpServer() const { return m_mcpServer; }
    void startMcpServer(quint16 preferredPort = 26303);
    void stopMcpServer();
    SanitizeResult applyMcpChapterHtml(int chapterIndex, const QString &html);
    void addChapterFromMcp(const QString &title);
    void deleteChapterFromMcp(int chapterIndex);
    void reorderChaptersFromMcp(int from, int to);
    void setWorkSkinCssFromMcp(const QString &css);
    bool saveProjectFromMcp();
    bool loadProjectFromMcp(const QString &path, bool saveCurrent = false, QString *errorMessage = nullptr);
    bool reloadProjectFromDisk();
    QByteArray capturePreviewScreenshot(int chapterIndex);
    void updateProblemsDiagnostics();
    QList<ProblemItem> currentProblems() const { return m_currentProblems; }
    void updateLoreGraphUi();
    LoreGraphDialog *loreGraphDialog() const { return m_loreGraphDialog; }

public slots:
    void updateStatusBar();

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onCloseProject();
    void onStartWriting();
    void onImportFromAo3();
    void onShowMainMenu();
    void onShowHelpGuide();
    void updateMainMenuState();
    void onExportChapterHtml();
    void onExportAllChapters();
    void onCopyHtmlToClipboard();
    void onCheckForUpdates();
    void onShowAbout();
    void onShowCredits();
    void onToggleLivePreview(bool checked);
    void onPopOutPreview();
    void onShowDebugConsole();
    void onShowProblemsDialog();
    void onShowSettings();
    void onShowAgentThoughts();
    void onShowLoreGraph();
    void onOpenChapterManager();
    void onEditProperties();
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
    void updateWindowTitle();
    void schedulePreviewUpdate();
    void onThemeChanged(AppTheme theme);
    void performAutosave();
    void checkForCrashRecovery();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool hasUnsavedChanges() const;
    bool maybeSave(const QString &actionContext = QStringLiteral("closing"), bool forcePromptIfActive = false);
    bool saveProjectToPath(const QString &path);
    void loadCurrentChapterIntoEditors();
    void saveCurrentChapterFromEditors();
    void setupStatusBar();
    QWidget *activeTextEditor() const;
    QString currentAutosaveFilePath() const;
    void removeAutosaveFile();
    void clearAutosaveSettings();

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

    QAction *m_darkThemeAction = nullptr;
    QAction *m_darkerThemeAction = nullptr;
    QAction *m_lightThemeAction = nullptr;
    QAction *m_reversiThemeAction = nullptr;
    QActionGroup *m_themeGroup = nullptr;

    AboutDialog *m_aboutDialog = nullptr;
    CreditsDialog *m_creditsDialog = nullptr;
    FindReplaceDialog *m_findReplaceDialog = nullptr;
    DebugConsoleDialog *m_debugConsoleDialog = nullptr;
    ProblemsDialog *m_problemsDialog = nullptr;
    SettingsDialog *m_settingsDialog = nullptr;
    HelpGuideDialog *m_helpGuideDialog = nullptr;
    AgentThoughtsDialog *m_agentThoughtsDialog = nullptr;
    LoreGraphDialog *m_loreGraphDialog = nullptr;

    QAction *m_closeProjectAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_propertiesAction = nullptr;
    QAction *m_chapterManagerAction = nullptr;
    QAction *m_loreGraphAction = nullptr;
    QAction *m_exportChapterAction = nullptr;
    QAction *m_exportAllAction = nullptr;
    QAction *m_copyHtmlAction = nullptr;

    QPushButton *m_problemsBadgeButton = nullptr;
    QPushButton *m_themeToggleButton = nullptr;
    QLabel *m_wordCountLabel = nullptr;
    QLabel *m_statusInfoLabel = nullptr;
    QLabel *m_lineColLabel = nullptr;
    QPushButton *m_insertModeButton = nullptr;
    QLabel *m_capsLockLabel = nullptr;
    bool m_capsLockOn = false;
    QList<ProblemItem> m_currentProblems;

    McpServer *m_mcpServer = nullptr;
    McpHandler *m_mcpHandler = nullptr;
    QPushButton *m_mcpStatusBadge = nullptr;

    QTimer *m_htmlSyncTimer = nullptr;
    QTimer *m_previewTimer = nullptr;
    QTimer *m_autosaveTimer = nullptr;

    bool m_hasActiveProject = false;
    bool m_syncInProgress = false;
    bool m_htmlSourceEditing = false;
    bool m_previewInPopup = false;
    QString m_pendingHtml;
    QString m_lastAutosavePath;
};
