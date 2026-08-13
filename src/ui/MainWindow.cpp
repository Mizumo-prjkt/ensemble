#include "MainWindow.h"

#include "export/Ao3HtmlSanitizer.h"
#include "model/ProjectSerializer.h"
#include "ui/AboutDialog.h"
#include "ui/Ao3ImportDialog.h"
#include "ui/ChapterSidebar.h"
#include "ui/CssEditorPane.h"
#include "ui/DebugConsoleDialog.h"
#include "ui/EditorPane.h"
#include "ui/FindReplaceDialog.h"
#include "ui/HtmlSourcePane.h"
#include "ui/MainMenuWindow.h"
#include "ui/PreviewPane.h"
#include "ui/ProblemsDialog.h"
#include "ui/CodeEditor.h"
#include "debug/debug.hpp"

#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextEdit>
#include <QXmlStreamReader>

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

  if (EnsembleDebug::isDebugBuild()) {
    viewMenu->addSeparator();
    auto *debugConsoleAction = viewMenu->addAction(QStringLiteral("Debug &Console"));
    debugConsoleAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+D")));
    connect(debugConsoleAction, &QAction::triggered, this, &MainWindow::onShowDebugConsole);
  }

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

  if (m_editorPane && m_editorPane->editor())
    connect(m_editorPane->editor(), &QTextEdit::cursorPositionChanged, this, &MainWindow::updateCursorPosition);
  if (m_htmlPane && m_htmlPane->editor())
    connect(m_htmlPane->editor(), &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::updateCursorPosition);
  if (m_cssPane && m_cssPane->editor())
    connect(m_cssPane->editor(), &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::updateCursorPosition);

  qApp->installEventFilter(this);

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
  setupStatusBar();
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
  updateProblemsDiagnostics();
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

void MainWindow::onShowDebugConsole() {
  if (!m_debugConsoleDialog) {
    m_debugConsoleDialog = new DebugConsoleDialog(this);
  }
  m_debugConsoleDialog->show();
  m_debugConsoleDialog->raise();
  m_debugConsoleDialog->activateWindow();
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

void MainWindow::setupStatusBar() {
  QStatusBar *sb = statusBar();
  sb->setStyleSheet(QStringLiteral(
      "QStatusBar { background-color: #111116; color: #aeb3c6; border-top: 1px solid #282a36; font-size: 11px; padding: 2px 8px; }"
  ));

  m_statusInfoLabel = new QLabel(QStringLiteral("Ready"), this);
  m_statusInfoLabel->setStyleSheet(QStringLiteral("color: #aeb3c6; font-size: 11px;"));
  sb->addWidget(m_statusInfoLabel, 1);

  // Line and Column Indicator (e.g. Ln 692, Col 31)
  m_lineColLabel = new QLabel(QStringLiteral("Ln 1, Col 1"), this);
  m_lineColLabel->setStyleSheet(QStringLiteral("color: #aeb3c6; font-size: 11px; padding: 2px 8px;"));
  sb->addPermanentWidget(m_lineColLabel);

  // Insert/Overwrite Mode Toggle Button (INS / OVR)
  m_insertModeButton = new QPushButton(QStringLiteral("INS"), this);
  m_insertModeButton->setFlat(true);
  m_insertModeButton->setCursor(Qt::PointingHandCursor);
  m_insertModeButton->setStyleSheet(QStringLiteral(
      "QPushButton { color: #aeb3c6; padding: 2px 6px; font-size: 11px; font-weight: bold; border: none; }"
      "QPushButton:hover { color: #ffffff; background-color: #282a36; border-radius: 3px; }"
  ));
  connect(m_insertModeButton, &QPushButton::clicked, this, &MainWindow::toggleInsertMode);
  sb->addPermanentWidget(m_insertModeButton);

  // Caps Lock Warning Indicator (⇪ CAPS LOCK)
  m_capsLockLabel = new QLabel(QStringLiteral("⇪ CAPS LOCK"), this);
  m_capsLockLabel->setStyleSheet(QStringLiteral(
      "color: #ffb86c; background-color: #2b2718; border: 1px solid #624c16; padding: 2px 8px; border-radius: 4px; font-weight: bold; font-size: 11px;"
  ));
  m_capsLockLabel->hide();
  sb->addPermanentWidget(m_capsLockLabel);

  // Problems Badge (⊗ 0 ⚠ N)
  m_problemsBadgeButton = new QPushButton(this);
  m_problemsBadgeButton->setFlat(true);
  m_problemsBadgeButton->setCursor(Qt::PointingHandCursor);
  m_problemsBadgeButton->setStyleSheet(QStringLiteral(
      "QPushButton { color: #50fa7b; background-color: #192b20; border: 1px solid #1c452b; padding: 2px 10px; border-radius: 4px; font-weight: bold; font-size: 11px; }"
      "QPushButton:hover { background-color: #243f2f; }"
  ));
  connect(m_problemsBadgeButton, &QPushButton::clicked, this, &MainWindow::onShowProblemsDialog);
  sb->addPermanentWidget(m_problemsBadgeButton);

  updateProblemsDiagnostics();
  updateCursorPosition();
}

void MainWindow::updateCursorPosition() {
  if (!m_lineColLabel || !m_insertModeButton)
    return;

  QWidget *editor = activeTextEditor();
  int line = 1;
  int col = 1;
  bool isOverwrite = false;

  if (auto *te = qobject_cast<QTextEdit *>(editor)) {
    QTextCursor cursor = te->textCursor();
    line = cursor.blockNumber() + 1;
    col = cursor.positionInBlock() + 1;
    isOverwrite = te->overwriteMode();
  } else if (auto *pte = qobject_cast<QPlainTextEdit *>(editor)) {
    QTextCursor cursor = pte->textCursor();
    line = cursor.blockNumber() + 1;
    col = cursor.positionInBlock() + 1;
    isOverwrite = pte->overwriteMode();
  }

  m_lineColLabel->setText(QStringLiteral("Ln %1, Col %2").arg(line).arg(col));

  if (isOverwrite) {
    m_insertModeButton->setText(QStringLiteral("OVR"));
    m_insertModeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: #ff5555; background-color: #3b1818; border: 1px solid #621616; padding: 2px 6px; border-radius: 3px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #542222; }"
    ));
  } else {
    m_insertModeButton->setText(QStringLiteral("INS"));
    m_insertModeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: #aeb3c6; padding: 2px 6px; font-size: 11px; font-weight: bold; border: none; }"
        "QPushButton:hover { color: #ffffff; background-color: #282a36; border-radius: 3px; }"
    ));
  }
}

void MainWindow::toggleInsertMode() {
  QWidget *editor = activeTextEditor();
  if (auto *te = qobject_cast<QTextEdit *>(editor)) {
    te->setOverwriteMode(!te->overwriteMode());
  } else if (auto *pte = qobject_cast<QPlainTextEdit *>(editor)) {
    pte->setOverwriteMode(!pte->overwriteMode());
  }
  updateCursorPosition();
}

void MainWindow::updateCapsLockState(bool active) {
  m_capsLockOn = active;
  if (m_capsLockLabel) {
    if (m_capsLockOn)
      m_capsLockLabel->show();
    else
      m_capsLockLabel->hide();
  }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->key() == Qt::Key_CapsLock) {
      if (event->type() == QEvent::KeyPress) {
        updateCapsLockState(!m_capsLockOn);
      }
    } else if (event->type() == QEvent::KeyPress) {
      if (keyEvent->key() == Qt::Key_Insert) {
        toggleInsertMode();
      }
      const QString text = keyEvent->text();
      if (text.length() == 1) {
        const QChar c = text.at(0);
        if (c.isLetter()) {
          const bool shiftPressed = keyEvent->modifiers().testFlag(Qt::ShiftModifier);
          if (c.isUpper() && !shiftPressed) {
            updateCapsLockState(true);
          } else if (c.isLower() && !shiftPressed) {
            updateCapsLockState(false);
          }
        }
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateProblemsDiagnostics() {
  m_currentProblems.clear();
  int errorCount = 0;
  int warningCount = 0;

  // 1. Check Work Skin CSS issues
  const QStringList cssClasses = m_cssPane ? m_cssPane->cssClassNames() : QStringList();
  const QSet<QString> cssClassSet(cssClasses.begin(), cssClasses.end());

  // Check duplicate CSS classes
  QMap<QString, int> classCounts;
  for (const QString &cls : cssClasses) {
    classCounts[cls]++;
  }

  for (auto iter = classCounts.constBegin(); iter != classCounts.constEnd(); ++iter) {
    if (iter.value() > 1) {
      ProblemItem item;
      item.severity = ProblemItem::Warning;
      item.category = QStringLiteral("Work Skin CSS");
      item.description = QStringLiteral("Duplicate CSS class '.%1' defined %2 times in Work Skin.").arg(iter.key()).arg(iter.value());
      item.chapterIndex = -1;
      m_currentProblems.append(item);
      warningCount++;
    }
  }

  // 2. Check chapter structural issues & missing CSS classes
  static const QRegularExpression classAttrRe(R"re(class\s*=\s*"([^"]+)")re", QRegularExpression::CaseInsensitiveOption);
  QMap<QString, QList<int>> missingClassChapters;

  int chIdx = 0;
  for (const Chapter &ch : m_project.chapters()) {
    const QString html = ch.html();
    if (ch.title().trimmed().isEmpty()) {
      ProblemItem item;
      item.severity = ProblemItem::Warning;
      item.category = QStringLiteral("Chapter Structure");
      item.description = QStringLiteral("Chapter %1 has an empty title.").arg(chIdx + 1);
      item.chapterIndex = chIdx;
      m_currentProblems.append(item);
      warningCount++;
    }
    if (html.trimmed().isEmpty()) {
      ProblemItem item;
      item.severity = ProblemItem::Warning;
      item.category = QStringLiteral("Chapter Content");
      item.description = QStringLiteral("Chapter %1 (\"%2\") has no body content.").arg(chIdx + 1).arg(ch.title());
      item.chapterIndex = chIdx;
      m_currentProblems.append(item);
      warningCount++;
    }

    // Check HTML parse errors via QXmlStreamReader
    if (!html.trimmed().isEmpty()) {
      const QString wrapped = QStringLiteral("<root>%1</root>").arg(html);
      QXmlStreamReader reader(wrapped);
      while (!reader.atEnd()) {
        reader.readNext();
        if (reader.hasError()) {
          const QString err = reader.errorString();
          const qint64 line = qMax<qint64>(1, reader.lineNumber() - 1);

          ProblemItem item;
          item.severity = ProblemItem::Error; // HTML tag mismatch is an Error
          item.category = QStringLiteral("HTML Syntax");
          item.description = QStringLiteral("HTML parse error in Chapter %1 (\"%2\") near line %3: %4")
                                 .arg(chIdx + 1)
                                 .arg(ch.title())
                                 .arg(line)
                                 .arg(err);
          item.chapterIndex = chIdx;
          m_currentProblems.append(item);
          errorCount++;
          break;
        }
      }
    }

    // Check for inline style= attributes
    static const QRegularExpression styleAttrRe(R"(\bstyle\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>]+))", QRegularExpression::CaseInsensitiveOption);
    if (html.contains(styleAttrRe)) {
      ProblemItem item;
      item.severity = ProblemItem::Warning;
      item.category = QStringLiteral("AO3 Compliance");
      item.description = QStringLiteral("Inline style= attributes detected in Chapter %1 (\"%2\"). AO3 strips inline styles upon posting.").arg(chIdx + 1).arg(ch.title());
      item.chapterIndex = chIdx;
      m_currentProblems.append(item);
      warningCount++;
    }

    // Scan chapter HTML for used CSS classes missing from Work Skin CSS
    QRegularExpressionMatchIterator it = classAttrRe.globalMatch(html);
    while (it.hasNext()) {
      QRegularExpressionMatch m = it.next();
      const QStringList classes = m.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
      for (const QString &cls : classes) {
        if (!cssClassSet.contains(cls)) {
          if (!missingClassChapters[cls].contains(chIdx)) {
            missingClassChapters[cls].append(chIdx);
          }
        }
      }
    }

    chIdx++;
  }

  // Add missing/removed CSS class warnings
  for (auto iter = missingClassChapters.constBegin(); iter != missingClassChapters.constEnd(); ++iter) {
    const QString &cls = iter.key();
    const QList<int> &chList = iter.value();

    ProblemItem item;
    item.severity = ProblemItem::Warning;
    item.category = QStringLiteral("Work Skin CSS");
    if (chList.size() <= 2) {
      QStringList chNames;
      for (int idx : chList) {
        chNames << QStringLiteral("Chapter %1").arg(idx + 1);
      }
      item.description = QStringLiteral("CSS class '.%1' is used in %2, but is missing from Work Skin.").arg(cls, chNames.join(QStringLiteral(", ")));
    } else {
      item.description = QStringLiteral("CSS class '.%1' is used across %2 chapters, but is missing from Work Skin.").arg(cls).arg(chList.size());
    }
    item.chapterIndex = chList.first();
    m_currentProblems.append(item);
    warningCount++;
  }

  if (!m_problemsBadgeButton)
    return;

  if (errorCount > 0) {
    m_problemsBadgeButton->setText(QStringLiteral("⊗ %1  ⚠ %2").arg(errorCount).arg(warningCount));
    m_problemsBadgeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: #ff5555; background-color: #3b1818; border: 1px solid #621616; padding: 2px 10px; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #542222; }"
    ));
  } else if (warningCount > 0) {
    m_problemsBadgeButton->setText(QStringLiteral("⊗ 0  ⚠ %1").arg(warningCount));
    m_problemsBadgeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: #ffb86c; background-color: #2b2718; border: 1px solid #624c16; padding: 2px 10px; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #3f3823; }"
    ));
  } else {
    m_problemsBadgeButton->setText(QStringLiteral("✔ 0 Problems"));
    m_problemsBadgeButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: #50fa7b; background-color: #192b20; border: 1px solid #1c452b; padding: 2px 10px; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #243f2f; }"
    ));
  }
}

void MainWindow::onShowProblemsDialog() {
  updateProblemsDiagnostics();
  if (!m_problemsDialog) {
    m_problemsDialog = new ProblemsDialog(this);
    connect(m_problemsDialog, &ProblemsDialog::chapterSelected, this, &MainWindow::onChapterSelected);
    connect(m_problemsDialog, &ProblemsDialog::cssTabRequested, this, [this]() {
      if (m_editorTabs) m_editorTabs->setCurrentIndex(2);
    });
  }
  m_problemsDialog->setProblems(m_currentProblems);
  m_problemsDialog->show();
  m_problemsDialog->raise();
  m_problemsDialog->activateWindow();
}
