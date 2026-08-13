#include "MainWindow.h"

#include "export/Ao3HtmlSanitizer.h"
#include "model/ProjectSerializer.h"
#include "ui/AboutDialog.h"
#include "ui/Ao3ImportDialog.h"
#include "ui/ChapterSidebar.h"
#include "ui/CssEditorPane.h"
#include "ui/EditorPane.h"
#include "ui/FindReplaceDialog.h"
#include "ui/HtmlSourcePane.h"
#include "ui/MainMenuWindow.h"
#include "ui/PreviewPane.h"
#include "ui/CodeEditor.h"

#include <QStackedWidget>
#include <QTextEdit>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSplitter>
#include "AppIcon.h"

#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("Ensemble"));
  setWindowIcon(AppIcon::icon());
  resize(1280, 800);

  m_htmlSyncTimer = new QTimer(this);
  m_htmlSyncTimer->setSingleShot(true);
  m_htmlSyncTimer->setInterval(300);
  connect(m_htmlSyncTimer, &QTimer::timeout, this,
          &MainWindow::debouncedSyncHtml);

  m_previewTimer = new QTimer(this);
  m_previewTimer->setSingleShot(true);
  m_previewTimer->setInterval(400);
  connect(m_previewTimer, &QTimer::timeout, this,
          &MainWindow::debouncedUpdatePreview);

  // Menu
  auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
  auto *exportMenu = menuBar()->addMenu(QStringLiteral("&Export"));
  auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
  auto *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));

  auto *newAction = fileMenu->addAction(QStringLiteral("&New"));
  newAction->setShortcut(QKeySequence::New);
  connect(newAction, &QAction::triggered, this, &MainWindow::onNewProject);

  auto *openAction = fileMenu->addAction(QStringLiteral("&Open…"));
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, &MainWindow::onOpenProject);

  auto *saveAction = fileMenu->addAction(QStringLiteral("&Save"));
  saveAction->setShortcut(QKeySequence::Save);
  connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveProject);

  auto *saveAsAction = fileMenu->addAction(QStringLiteral("Save &As…"));
  saveAsAction->setShortcut(QKeySequence::SaveAs);
  connect(saveAsAction, &QAction::triggered, this,
          &MainWindow::onSaveProjectAs);

  fileMenu->addSeparator();
  auto *importAo3Action = fileMenu->addAction(QStringLiteral("Import from &AO3…"));
  importAo3Action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+I")));
  connect(importAo3Action, &QAction::triggered, this, &MainWindow::onImportFromAo3);

  fileMenu->addSeparator();
  auto *mainMenuAction = fileMenu->addAction(QStringLiteral("&Main Menu…"));
  connect(mainMenuAction, &QAction::triggered, this,
          &MainWindow::onShowMainMenu);

  auto *quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
  quitAction->setShortcut(QKeySequence::Quit);
  connect(quitAction, &QAction::triggered, this, &QWidget::close);

  auto *exportChapterAction =
      exportMenu->addAction(QStringLiteral("Export Current &Chapter HTML…"));
  connect(exportChapterAction, &QAction::triggered, this,
          &MainWindow::onExportChapterHtml);

  auto *exportAllAction =
      exportMenu->addAction(QStringLiteral("Export &All Chapters…"));
  connect(exportAllAction, &QAction::triggered, this,
          &MainWindow::onExportAllChapters);

  auto *copyHtmlAction =
      exportMenu->addAction(QStringLiteral("&Copy HTML to Clipboard"));
  copyHtmlAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
  connect(copyHtmlAction, &QAction::triggered, this,
          &MainWindow::onCopyHtmlToClipboard);

  // View menu - simplified preview controls
  auto *togglePreviewAction = viewMenu->addAction(QStringLiteral("Show Live &Preview"));
  togglePreviewAction->setCheckable(true);
  togglePreviewAction->setChecked(true);
  togglePreviewAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
  connect(togglePreviewAction, &QAction::toggled, this, &MainWindow::onToggleLivePreview);

  auto *popOutAction = viewMenu->addAction(QStringLiteral("Pop Out Preview &Window"));
  popOutAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
  connect(popOutAction, &QAction::triggered, this, &MainWindow::onPopOutPreview);

  // Help menu
  auto *aboutAction = helpMenu->addAction(QStringLiteral("&About"));
  connect(aboutAction, &QAction::triggered, this, &MainWindow::onShowAbout);

  // Stacked central widget
  m_centralStack = new QStackedWidget(this);

  // Page 1: Editor Workspace
  auto *central = new QWidget(this);
  auto *mainLayout = new QVBoxLayout(central);
  mainLayout->setContentsMargins(4, 4, 4, 4);

  m_chapterSidebar = new ChapterSidebar(central);
  m_chapterSidebar->setMinimumWidth(160);
  m_chapterSidebar->setMaximumWidth(240);

  // Tabbed editor + HTML source pane
  m_editorTabs = new QTabWidget(central);

  m_editorPane = new EditorPane(central);
  m_editorPane->setMinimumWidth(280);
  m_editorTabs->addTab(m_editorPane, QStringLiteral("Rich Text"));

  m_htmlPane = new HtmlSourcePane(central);
  m_htmlPane->setMinimumWidth(280);
  m_editorTabs->addTab(m_htmlPane, QStringLiteral("Raw HTML"));

  m_cssPane = new CssEditorPane(central);
  m_editorTabs->addTab(m_cssPane, QStringLiteral("Work Skin CSS"));

  m_previewPane = new PreviewPane(central);

  m_mainSplitter = new QSplitter(Qt::Horizontal, central);
  m_mainSplitter->addWidget(m_chapterSidebar);
  m_mainSplitter->addWidget(m_editorTabs);
  m_mainSplitter->addWidget(m_previewPane);
  m_mainSplitter->setStretchFactor(0, 0);
  m_mainSplitter->setStretchFactor(1, 2);
  m_mainSplitter->setStretchFactor(2, 1);
  m_mainSplitter->setSizes({200, 580, 500});

  mainLayout->addWidget(m_mainSplitter);

  // Page 0: Welcome View Widget
  m_mainMenuWidget = new MainMenuWidget(this);
  connect(m_mainMenuWidget, &MainMenuWidget::newProjectRequested, this,
          &MainWindow::onNewProject);
  connect(m_mainMenuWidget, &MainMenuWidget::openProjectRequested, this,
          &MainWindow::onOpenProject);
  connect(m_mainMenuWidget, &MainMenuWidget::saveProjectRequested, this,
          &MainWindow::onSaveProject);
  connect(m_mainMenuWidget, &MainMenuWidget::startWritingRequested, this,
          &MainWindow::onStartWriting);

  m_centralStack->addWidget(m_mainMenuWidget); // Index 0
  m_centralStack->addWidget(central);          // Index 1

  // Start on Welcome View (Index 0)
  m_centralStack->setCurrentIndex(0);
  setCentralWidget(m_centralStack);

  statusBar()->showMessage(QStringLiteral("Ready"));

  // Create dialogs (lazy initialization)
  m_aboutDialog = new AboutDialog(this);

  // Connections
  m_chapterSidebar->bindProject(&m_project);
  m_cssPane->bindProject(&m_project);

  connect(m_editorPane, &EditorPane::contentChanged, this,
          &MainWindow::onEditorContentChanged);
  connect(m_editorPane, &EditorPane::htmlExported, this,
          &MainWindow::onEditorHtmlExported);
  connect(m_htmlPane, &HtmlSourcePane::applyRequested, this,
          &MainWindow::onHtmlApplyRequested);
  connect(m_htmlPane, &HtmlSourcePane::editingStarted, this,
          &MainWindow::onHtmlEditingStarted);
  connect(m_htmlPane, &HtmlSourcePane::editingFinished, this,
          &MainWindow::onHtmlEditingFinished);
  connect(m_htmlPane, &HtmlSourcePane::htmlChanged, this,
          &MainWindow::onHtmlChanged);
  connect(m_editorTabs, &QTabWidget::currentChanged, this,
          &MainWindow::onTabChanged);
  connect(m_cssPane, &CssEditorPane::cssChanged, this,
          &MainWindow::onCssChanged);
  connect(m_cssPane, &CssEditorPane::cssClassesChanged, this,
          &MainWindow::onCssClassesChanged);
  connect(m_chapterSidebar, &ChapterSidebar::chapterSelected, this,
          &MainWindow::onChapterSelected);
  connect(m_chapterSidebar, &ChapterSidebar::chaptersReordered, this,
          &MainWindow::onChaptersReordered);

  loadCurrentChapterIntoEditors();
  m_cssPane->setCss(m_project.workSkinCss());

  // Initialize the editor with any existing CSS
  m_editorPane->setCustomCss(m_project.workSkinCss());
  m_editorPane->setAvailableCssClasses(m_cssPane->cssClassNames());

  // Find/Replace shortcuts
  auto *findAction = new QAction(this);
  findAction->setShortcut(QKeySequence::Find); // Ctrl+F
  connect(findAction, &QAction::triggered, this, &MainWindow::onFindRequested);
  addAction(findAction);

  auto *replaceAction = new QAction(this);
  replaceAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
  connect(replaceAction, &QAction::triggered, this,
          &MainWindow::onReplaceRequested);
  addAction(replaceAction);

  updateWindowTitle();
  schedulePreviewUpdate();

  // Open Welcome UI / Main Menu automatically on startup
  QTimer::singleShot(0, this, &MainWindow::onShowMainMenu);
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (maybeSave())
    event->accept();
  else
    event->ignore();
}

bool MainWindow::maybeSave() {
  if (!m_project.isDirty())
    return true;

  const auto reply = QMessageBox::question(
      this, QStringLiteral("Unsaved Changes"),
      QStringLiteral("Save changes before closing?"),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);

  if (reply == QMessageBox::Cancel)
    return false;
  if (reply == QMessageBox::Discard)
    return true;
  onSaveProject();
  return !m_project.isDirty();
}

void MainWindow::onNewProject() {
  if (!maybeSave())
    return;

  saveCurrentChapterFromEditors();
  m_project.resetToNew();
  m_chapterSidebar->refresh();
  loadCurrentChapterIntoEditors();
  m_cssPane->setCss(m_project.workSkinCss());
  m_editorPane->setCustomCss(m_project.workSkinCss());
  m_editorPane->setAvailableCssClasses(m_cssPane->cssClassNames());
  updateWindowTitle();
  schedulePreviewUpdate();
  m_centralStack->setCurrentIndex(1);
}

void MainWindow::onOpenProject() {
  if (!maybeSave())
    return;

  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("Open Project"), QString(),
      QStringLiteral("AO3 Project (*.ao3proj);;All Files (*)"));
  if (path.isEmpty())
    return;

  QString error;
  if (!ProjectSerializer::load(path, m_project, &error)) {
    QMessageBox::critical(this, QStringLiteral("Open Failed"), error);
    return;
  }

  m_chapterSidebar->refresh();
  loadCurrentChapterIntoEditors();
  m_cssPane->setCss(m_project.workSkinCss());
  m_editorPane->setCustomCss(m_project.workSkinCss());
  m_editorPane->setAvailableCssClasses(m_cssPane->cssClassNames());
  updateWindowTitle();
  schedulePreviewUpdate();
  statusBar()->showMessage(QStringLiteral("Opened %1").arg(path), 3000);
  m_centralStack->setCurrentIndex(1);
}

void MainWindow::onSaveProject() {
  if (m_project.filePath().isEmpty()) {
    onSaveProjectAs();
    return;
  }
  saveProjectToPath(m_project.filePath());
}

void MainWindow::onSaveProjectAs() {
  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("Save Project As"), QString(),
      QStringLiteral("AO3 Project (*.ao3proj);;All Files (*)"));
  if (path.isEmpty())
    return;

  QString finalPath = path;
  if (!finalPath.endsWith(QStringLiteral(".ao3proj"), Qt::CaseInsensitive))
    finalPath += QStringLiteral(".ao3proj");

  saveProjectToPath(finalPath);
}

bool MainWindow::saveProjectToPath(const QString &path) {
  saveCurrentChapterFromEditors();

  QString error;
  if (!ProjectSerializer::save(path, m_project, &error)) {
    QMessageBox::critical(this, QStringLiteral("Save Failed"), error);
    return false;
  }

  m_project.setFilePath(path);
  m_project.setDirty(false);
  updateWindowTitle();
  statusBar()->showMessage(QStringLiteral("Saved %1").arg(path), 3000);
  return true;
}

void MainWindow::onExportChapterHtml() {
  saveCurrentChapterFromEditors();
  const Chapter *chapter = m_project.activeChapter();
  if (!chapter)
    return;

  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("Export Chapter HTML"), QString(),
      QStringLiteral("HTML (*.html);;All Files (*)"));
  if (path.isEmpty())
    return;

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text |
                 QIODevice::Truncate)) {
    QMessageBox::critical(this, QStringLiteral("Export Failed"),
                          QStringLiteral("Could not write file."));
    return;
  }

  file.write(chapter->html().toUtf8());
  statusBar()->showMessage(QStringLiteral("Exported chapter HTML"), 3000);
}

void MainWindow::onExportAllChapters() {
  saveCurrentChapterFromEditors();

  const QString dirPath = QFileDialog::getExistingDirectory(
      this, QStringLiteral("Export All Chapters"));
  if (dirPath.isEmpty())
    return;

  int index = 1;
  for (const Chapter &chapter : m_project.chapters()) {
    const QString filename =
        QStringLiteral("chapter-%1.html").arg(index, 2, 10, QLatin1Char('0'));
    QFile file(QDir(dirPath).filePath(filename));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text |
                   QIODevice::Truncate)) {
      QMessageBox::critical(this, QStringLiteral("Export Failed"),
                            QStringLiteral("Could not write %1").arg(filename));
      return;
    }
    file.write(chapter.html().toUtf8());
    ++index;
  }

  statusBar()->showMessage(
      QStringLiteral("Exported %1 chapters").arg(m_project.chapters().size()),
      3000);
}

void MainWindow::onCopyHtmlToClipboard() {
  saveCurrentChapterFromEditors();
  const Chapter *chapter = m_project.activeChapter();
  if (!chapter)
    return;

  QApplication::clipboard()->setText(chapter->html());
  statusBar()->showMessage(QStringLiteral("HTML copied to clipboard"), 2000);
}

void MainWindow::onImportFromAo3() {
  Ao3ImportDialog dialog(this, this);
  dialog.exec();
}

void MainWindow::onEditorContentChanged() {
  m_project.setDirty(true);
  updateWindowTitle();
  updateStatusBar();
  m_htmlSyncTimer->start();
  schedulePreviewUpdate();
}

void MainWindow::onEditorHtmlExported(const QString &html) {
  m_pendingHtml = html;
  m_htmlSyncTimer->start();
}

void MainWindow::debouncedSyncHtml() {
  if (m_htmlSourceEditing || m_syncInProgress)
    return;

  saveCurrentChapterFromEditors();

  m_syncInProgress = true;
  m_htmlPane->setHtml(m_pendingHtml.isEmpty() ? m_editorPane->currentHtml()
                                              : m_pendingHtml);
  m_syncInProgress = false;
}

void MainWindow::onHtmlApplyRequested(const QString &html) {
  const QString sanitized = Ao3HtmlSanitizer::sanitize(html);

  m_syncInProgress = true;
  m_editorPane->setHtml(sanitized);
  m_htmlPane->setHtml(sanitized);

  if (Chapter *chapter = m_project.activeChapter()) {
    chapter->setHtml(sanitized);
    m_project.setDirty(true);
  }

  m_syncInProgress = false;
  m_htmlSourceEditing = false;
  updateWindowTitle();
  updateStatusBar();
  schedulePreviewUpdate();
}

void MainWindow::onHtmlEditingStarted() { m_htmlSourceEditing = true; }

void MainWindow::onHtmlEditingFinished() { m_htmlSourceEditing = false; }

void MainWindow::onHtmlChanged(const QString &html) {
  // Live preview update from HTML source editing
  m_pendingHtml = html;
  m_project.setDirty(true);
  updateWindowTitle();
  schedulePreviewUpdate();
}

void MainWindow::onTabChanged(int index) {
  // If we switch away from the Raw HTML tab, commit the edited HTML to the Rich
  // Text editor. This resets the HTML editing state so future edits in Rich
  // Text sync back correctly.
  if (index != 1 && m_htmlSourceEditing) {
    m_htmlPane->forceApply();
  }
}

void MainWindow::onCssChanged(const QString &css) {
  m_project.setWorkSkinCss(css);
  m_project.setDirty(true);
  updateWindowTitle();

  // Push CSS to the rich text editor so it can render styles
  m_editorPane->setCustomCss(css);

  schedulePreviewUpdate();
}

void MainWindow::onCssClassesChanged(const QStringList &classNames) {
  // Update the rich text editor's available classes for the context menu
  m_editorPane->setAvailableCssClasses(classNames);
}

void MainWindow::onChapterSelected(int index) {
  if (index == m_project.activeChapterIndex())
    return;

  saveCurrentChapterFromEditors();
  m_project.setActiveChapterIndex(index);
  loadCurrentChapterIntoEditors();
  schedulePreviewUpdate();
}

void MainWindow::onChaptersReordered() {
  m_project.setDirty(true);
  updateWindowTitle();
}

void MainWindow::loadCurrentChapterIntoEditors() {
  const Chapter *chapter = m_project.activeChapter();
  const QString html = chapter ? chapter->html() : QStringLiteral("<p></p>");

  m_syncInProgress = true;
  m_editorPane->setHtml(html);
  m_htmlPane->setHtml(html);
  m_syncInProgress = false;

  updateStatusBar();
  m_editorPane->focusEditor();
}

void MainWindow::saveCurrentChapterFromEditors() {
  if (m_syncInProgress)
    return;

  Chapter *chapter = m_project.activeChapter();
  if (!chapter)
    return;

  const QString html =
      m_htmlSourceEditing && !m_htmlPane->html().trimmed().isEmpty()
          ? Ao3HtmlSanitizer::sanitize(m_htmlPane->html())
          : m_editorPane->currentHtml();

  chapter->setHtml(html);
}

void MainWindow::schedulePreviewUpdate() { m_previewTimer->start(); }

void MainWindow::debouncedUpdatePreview() {
  const Chapter *chapter = m_project.activeChapter();
  const QString title = chapter ? chapter->title() : QString();

  // If we're editing in the HTML pane, use the live HTML text directly
  // for the preview instead of the saved chapter data.
  QString html;
  if (m_htmlSourceEditing && !m_pendingHtml.isEmpty()) {
    html = m_pendingHtml;
  } else {
    html = chapter ? chapter->html() : QString();
  }

  m_previewPane->updatePreview(title, html, m_project.workSkinCss());
  if (m_previewPopup) {
    m_previewPopup->updatePreview(title, html, m_project.workSkinCss());
  }
}

void MainWindow::updateStatusBar() {
  const Chapter *chapter = m_project.activeChapter();
  const QString html = chapter ? chapter->html() : QString();
  const int words =
      html.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)
          .size();
  statusBar()->showMessage(QStringLiteral("Words: ~%1").arg(words));
}

void MainWindow::updateWindowTitle() {
  QString title = m_project.title();
  if (m_project.isDirty())
    title += QStringLiteral(" *");

  if (!m_project.filePath().isEmpty())
    title += QStringLiteral(" — ") + m_project.filePath();

  title += QStringLiteral(" — Ensemble");
  setWindowTitle(title);
}

void MainWindow::onShowMainMenu() { m_centralStack->setCurrentIndex(0); }

void MainWindow::onStartWriting() { m_centralStack->setCurrentIndex(1); }

void MainWindow::onShowAbout() { m_aboutDialog->exec(); }

void MainWindow::onToggleLivePreview(bool checked) {
  if (checked) {
    m_previewPane->show();
    m_mainSplitter->setSizes({200, 580, 500});
    schedulePreviewUpdate();
  } else {
    m_previewPane->hide();
  }
}

void MainWindow::onPopOutPreview() {
  if (!m_previewPopup) {
    m_previewPopup = new PreviewPane(nullptr);
    m_previewPopup->setAttribute(Qt::WA_DeleteOnClose, false);
    m_previewPopup->setWindowFlags(Qt::Window);
    m_previewPopup->resize(700, 850);
    m_previewPopup->setWindowTitle(QStringLiteral("WebEngine Preview — Ensemble"));
  }

  const Chapter *chapter = m_project.activeChapter();
  const QString title = chapter ? chapter->title() : QString();
  const QString html = chapter ? chapter->html() : QString();
  m_previewPopup->updatePreview(title, html, m_project.workSkinCss());

  m_previewPopup->show();
  m_previewPopup->raise();
  m_previewPopup->activateWindow();
}

void MainWindow::onFindRequested() {
  QWidget *editor = activeTextEditor();
  if (!editor)
    return;

  if (!m_findReplaceDialog) {
    m_findReplaceDialog = new FindReplaceDialog(this);
  }
  m_findReplaceDialog->setTargetEditor(editor);
  m_findReplaceDialog->showFind();
}

void MainWindow::onReplaceRequested() {
  QWidget *editor = activeTextEditor();
  if (!editor)
    return;

  if (!m_findReplaceDialog) {
    m_findReplaceDialog = new FindReplaceDialog(this);
  }
  m_findReplaceDialog->setTargetEditor(editor);
  m_findReplaceDialog->showReplace();
}



QWidget *MainWindow::activeTextEditor() const {
  QWidget *current = m_editorTabs->currentWidget();
  if (current == m_editorPane)
    return m_editorPane->editor();
  if (current == m_htmlPane)
    return m_htmlPane->editor();
  if (current == m_cssPane)
    return m_cssPane->editor();
  return nullptr;
}
