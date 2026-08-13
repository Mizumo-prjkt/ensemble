#pragma once

#include "model/Ao3Project.h"

#include <QMainWindow>

class FindReplaceDialog;
class AboutDialog;
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
    void onImportFromAo3();
    void onToggleLivePreview(bool checked);
    void onPopOutPreview();
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

private:
    bool maybeSave();
    bool saveProjectToPath(const QString &path);
    void loadCurrentChapterIntoEditors();
    void saveCurrentChapterFromEditors();
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
    FindReplaceDialog *m_findReplaceDialog = nullptr;

    QTimer *m_htmlSyncTimer = nullptr;
    QTimer *m_previewTimer = nullptr;

    bool m_syncInProgress = false;
    bool m_htmlSourceEditing = false;
    bool m_previewInPopup = false;
    QString m_pendingHtml;
};
