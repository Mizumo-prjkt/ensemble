#include "MainWindow.h"

#include "export/Ao3HtmlSanitizer.h"
#include "export/Ao3TagWhitelist.h"
#include "model/ProjectSerializer.h"
#include <QBuffer>
#include <QElapsedTimer>
#include <QPainter>
#include "ui/AboutDialog.h"
#include "ui/CreditsDialog.h"
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
#include "ui/SettingsDialog.h"
#include "ui/HelpGuideDialog.h"
#include "ui/AgentThoughtsDialog.h"
#include "ui/LoreGraphDialog.h"
#include "ui/ProjectPropertiesDialog.h"
#include "ui/CodeEditor.h"
#include "ui/ProjectLoadErrorDialog.h"
#include "debug/debug.hpp"
#include "themes/ThemeManager.h"
#include "mcp/McpServer.h"
#include "mcp/McpHandler.h"

#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextEdit>
#include <QWindow>
#include <QXmlStreamReader>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QUrl>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QTime>
#include "AppIcon.h"
#include "utils/AppPaths.h"

#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("Ensemble"));
  setWindowIcon(AppIcon::icon());
  resize(1280, 800);

  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MainWindow::onThemeChanged);
  connect(&ThemeManager::instance(), &ThemeManager::skinFlavorChanged, this, [this](SkinFlavor) {
    onThemeChanged(ThemeManager::instance().currentTheme());
  });

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

  m_saveAction = fileMenu->addAction(QStringLiteral("&Save"));
  m_saveAction->setShortcut(QKeySequence::Save);
  connect(m_saveAction, &QAction::triggered, this, &MainWindow::onSaveProject);

  m_saveAsAction = fileMenu->addAction(QStringLiteral("Save &As…"));
  m_saveAsAction->setShortcut(QKeySequence::SaveAs);
  connect(m_saveAsAction, &QAction::triggered, this,
          &MainWindow::onSaveProjectAs);

  m_closeProjectAction = fileMenu->addAction(QStringLiteral("&Close Project"));
  m_closeProjectAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
  connect(m_closeProjectAction, &QAction::triggered, this,
          &MainWindow::onCloseProject);

  fileMenu->addSeparator();
  auto *importAo3Action = fileMenu->addAction(QStringLiteral("Import from &AO3…"));
  importAo3Action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+I")));
  connect(importAo3Action, &QAction::triggered, this, &MainWindow::onImportFromAo3);

  fileMenu->addSeparator();
  m_propertiesAction = fileMenu->addAction(QStringLiteral("P&roperties…"));
  m_propertiesAction->setShortcuts({QKeySequence(Qt::ALT | Qt::Key_Return), QKeySequence(Qt::ALT | Qt::Key_Enter)});
  connect(m_propertiesAction, &QAction::triggered, this, &MainWindow::onEditProperties);

  fileMenu->addSeparator();
  auto *preferencesAction = fileMenu->addAction(QStringLiteral("Preference&s…"));
  preferencesAction->setShortcut(QKeySequence::Preferences);
  connect(preferencesAction, &QAction::triggered, this, &MainWindow::onShowSettings);

  fileMenu->addSeparator();
  auto *mainMenuAction = fileMenu->addAction(QStringLiteral("&Main Menu…"));
  connect(mainMenuAction, &QAction::triggered, this,
          &MainWindow::onShowMainMenu);

  auto *quitAction = fileMenu->addAction(QStringLiteral("&Quit"));
  quitAction->setShortcut(QKeySequence::Quit);
  connect(quitAction, &QAction::triggered, this, &QWidget::close);

  m_exportChapterAction =
      exportMenu->addAction(QStringLiteral("Export Current &Chapter HTML…"));
  connect(m_exportChapterAction, &QAction::triggered, this,
          &MainWindow::onExportChapterHtml);

  m_exportAllAction =
      exportMenu->addAction(QStringLiteral("Export &All Chapters…"));
  connect(m_exportAllAction, &QAction::triggered, this,
          &MainWindow::onExportAllChapters);

  m_copyHtmlAction =
      exportMenu->addAction(QStringLiteral("&Copy HTML to Clipboard"));
  m_copyHtmlAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
  connect(m_copyHtmlAction, &QAction::triggered, this,
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

  // View -> Theme
  viewMenu->addSeparator();
  auto *themeMenu = viewMenu->addMenu(QStringLiteral("&Theme"));
  m_lightThemeAction = themeMenu->addAction(QStringLiteral("☀️ &Light Mode"));
  m_lightThemeAction->setCheckable(true);
  m_lightThemeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+1")));

  m_darkThemeAction = themeMenu->addAction(QStringLiteral("🌙 &Dark Mode"));
  m_darkThemeAction->setCheckable(true);
  m_darkThemeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+2")));

  m_reversiThemeAction = themeMenu->addAction(QStringLiteral("🔴 AO3 &Reversi Mode"));
  m_reversiThemeAction->setCheckable(true);
  m_reversiThemeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+3")));

  m_darkerThemeAction = themeMenu->addAction(QStringLiteral("🖤 &Darker Mode (OLED)"));
  m_darkerThemeAction->setCheckable(true);
  m_darkerThemeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+4")));

  m_themeGroup = new QActionGroup(this);
  m_themeGroup->addAction(m_lightThemeAction);
  m_themeGroup->addAction(m_darkThemeAction);
  m_themeGroup->addAction(m_reversiThemeAction);
  m_themeGroup->addAction(m_darkerThemeAction);

  const AppTheme curTheme = ThemeManager::instance().currentTheme();
  if (curTheme == AppTheme::Dark) {
    m_darkThemeAction->setChecked(true);
  } else if (curTheme == AppTheme::Darker) {
    m_darkerThemeAction->setChecked(true);
  } else if (curTheme == AppTheme::Ao3Reversi) {
    m_reversiThemeAction->setChecked(true);
  } else {
    m_lightThemeAction->setChecked(true);
  }

  connect(m_lightThemeAction, &QAction::triggered, this, []() {
    ThemeManager::instance().setTheme(AppTheme::Light);
  });
  connect(m_darkThemeAction, &QAction::triggered, this, []() {
    ThemeManager::instance().setTheme(AppTheme::Dark);
  });
  connect(m_reversiThemeAction, &QAction::triggered, this, []() {
    ThemeManager::instance().setTheme(AppTheme::Ao3Reversi);
  });
  connect(m_darkerThemeAction, &QAction::triggered, this, []() {
    ThemeManager::instance().setTheme(AppTheme::Darker);
  });

  auto *toggleThemeAction = viewMenu->addAction(QStringLiteral("Toggle &Theme"));
  toggleThemeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+T")));
  connect(toggleThemeAction, &QAction::triggered, this, []() {
    ThemeManager::instance().toggleTheme();
  });

  viewMenu->addSeparator();
  auto *agentThoughtsAction = viewMenu->addAction(QStringLiteral("🧠 AI Agent &Thoughts Monitor…"));
  agentThoughtsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
  connect(agentThoughtsAction, &QAction::triggered, this, &MainWindow::onShowAgentThoughts);

  m_loreGraphAction = viewMenu->addAction(QStringLiteral("🗺️ Lore Graph && Narrative &Map…"));
  m_loreGraphAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
  connect(m_loreGraphAction, &QAction::triggered, this, &MainWindow::onShowLoreGraph);

  viewMenu->addSeparator();
  auto *viewPrefAction = viewMenu->addAction(QStringLiteral("Preference&s…"));
  viewPrefAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
  connect(viewPrefAction, &QAction::triggered, this, &MainWindow::onShowSettings);

  if (EnsembleDebug::isDebugBuild()) {
    viewMenu->addSeparator();
    auto *debugConsoleAction = viewMenu->addAction(QStringLiteral("Debug &Console"));
    debugConsoleAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+D")));
    connect(debugConsoleAction, &QAction::triggered, this, &MainWindow::onShowDebugConsole);
  }

  // Help menu
  auto *helpGuideAction = helpMenu->addAction(QStringLiteral("📖 Help && User Guide…"));
  helpGuideAction->setShortcut(QKeySequence::HelpContents);
  connect(helpGuideAction, &QAction::triggered, this, &MainWindow::onShowHelpGuide);

  helpMenu->addSeparator();
  auto *aboutAction = helpMenu->addAction(QStringLiteral("&About Ensemble"));
  connect(aboutAction, &QAction::triggered, this, &MainWindow::onShowAbout);

  auto *creditsAction = helpMenu->addAction(QStringLiteral("&Open Source Credits…"));
  connect(creditsAction, &QAction::triggered, this, &MainWindow::onShowCredits);

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
  connect(m_mainMenuWidget, &MainMenuWidget::closeProjectRequested, this,
          &MainWindow::onCloseProject);
  connect(m_mainMenuWidget, &MainMenuWidget::startWritingRequested, this,
          &MainWindow::onStartWriting);
  connect(m_mainMenuWidget, &MainMenuWidget::settingsRequested, this,
          &MainWindow::onShowSettings);
  connect(m_mainMenuWidget, &MainMenuWidget::helpRequested, this,
          &MainWindow::onShowHelpGuide);

  m_centralStack->addWidget(m_mainMenuWidget); // Index 0
  m_centralStack->addWidget(central);          // Index 1

  // Start on Welcome View (Index 0)
  m_centralStack->setCurrentIndex(0);
  setCentralWidget(m_centralStack);

  statusBar()->showMessage(QStringLiteral("Ready"));

  // Create dialogs (lazy initialization)
  m_aboutDialog = new AboutDialog(this);
  m_creditsDialog = new CreditsDialog(this);

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

  if (m_editorPane && m_editorPane->editor()) {
    connect(m_editorPane->editor(), &QTextEdit::cursorPositionChanged, this, &MainWindow::updateCursorPosition);
    connect(m_editorPane->editor(), &QTextEdit::selectionChanged, this, &MainWindow::updateStatusBar);
    connect(m_editorPane->editor(), &QTextEdit::textChanged, this, &MainWindow::updateStatusBar);
  }
  if (m_htmlPane && m_htmlPane->editor()) {
    connect(m_htmlPane->editor(), &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::updateCursorPosition);
    connect(m_htmlPane->editor(), &QPlainTextEdit::selectionChanged, this, &MainWindow::updateStatusBar);
    connect(m_htmlPane->editor(), &QPlainTextEdit::textChanged, this, &MainWindow::updateStatusBar);
  }
  if (m_cssPane && m_cssPane->editor())
    connect(m_cssPane->editor(), &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::updateCursorPosition);

  qApp->installEventFilter(this);
  setAcceptDrops(true);

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

  m_hasActiveProject = false;
  m_project.setDirty(false);
  updateMainMenuState();
  updateWindowTitle();
  setupStatusBar();

  // 60-second autosave timer
  m_autosaveTimer = new QTimer(this);
  m_autosaveTimer->setInterval(60000); // 60 seconds
  connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::performAutosave);
  m_autosaveTimer->start();

  // Check for unsaved session crash recovery
  QTimer::singleShot(250, this, &MainWindow::checkForCrashRecovery);

  // Auto-start MCP server if enabled in preferences
  QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
  if (settings.value(QStringLiteral("ai/mcpEnabled"), false).toBool()) {
    const quint16 port = static_cast<quint16>(settings.value(QStringLiteral("ai/mcpPort"), 26303).toUInt());
    startMcpServer(port);
  }
}

MainWindow::~MainWindow() {
  if (m_mcpServer) {
    m_mcpServer->disconnect();
    m_mcpServer->stop();
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (maybeSave(QStringLiteral("closing"), false)) {
    if (m_mcpServer) {
      m_mcpServer->stop();
    }
    event->accept();
  } else {
    event->ignore();
  }
}

bool MainWindow::hasUnsavedChanges() const {
  if (!m_hasActiveProject)
    return false;
  if (m_project.isDirty())
    return true;
  if (m_editorPane && m_editorPane->editor() && m_editorPane->editor()->document() &&
      m_editorPane->editor()->document()->isModified())
    return true;
  if (m_htmlPane && m_htmlPane->editor() && m_htmlPane->editor()->document() &&
      m_htmlPane->editor()->document()->isModified())
    return true;
  if (m_cssPane && m_cssPane->editor() && m_cssPane->editor()->document() &&
      m_cssPane->editor()->document()->isModified())
    return true;

  // Untitled project with actual story content
  if (m_project.filePath().isEmpty()) {
    if (m_project.chapters().size() > 1)
      return true;
    if (m_project.chapters().size() == 1) {
      const QString chHtml = m_project.chapters().first().html().trimmed();
      if (!chHtml.isEmpty() && chHtml != QStringLiteral("<p></p>") && chHtml != QStringLiteral("<p><br/></p>"))
        return true;
    }
    QFile defaultCss(QStringLiteral(":/default-work-skin.css"));
    QString defCss;
    if (defaultCss.open(QIODevice::ReadOnly | QIODevice::Text))
      defCss = QString::fromUtf8(defaultCss.readAll());
    if (m_project.workSkinCss() != defCss && !m_project.workSkinCss().trimmed().isEmpty())
      return true;
  }
  return false;
}

bool MainWindow::maybeSave(const QString &actionContext, bool forcePromptIfActive) {
  if (!m_hasActiveProject)
    return true;

  saveCurrentChapterFromEditors();

  const bool hasChanges = hasUnsavedChanges();
  if (!hasChanges && !forcePromptIfActive)
    return true;

  const QString projTitle = m_project.title().isEmpty()
                                ? QStringLiteral("Untitled Project")
                                : m_project.title().toHtmlEscaped();

  QMessageBox msgBox(this);
  msgBox.setWindowTitle(hasChanges ? QStringLiteral("Unsaved Changes")
                                   : QStringLiteral("Save Active Project"));
  msgBox.setIcon(hasChanges ? QMessageBox::Warning : QMessageBox::Question);
  msgBox.setStyleSheet(ThemeManager::instance().getDialogStyleSheet());

  if (hasChanges) {
    msgBox.setText(QStringLiteral("<h3>Save changes before %1?</h3>"
                                  "<p>The project <b>%2</b> has unsaved modifications. If you proceed without saving, your changes will be discarded.</p>")
                       .arg(actionContext, projTitle));
    msgBox.setInformativeText(QStringLiteral("Do you want to save your work now?"));
  } else {
    msgBox.setText(QStringLiteral("<h3>Save project before %1?</h3>"
                                  "<p>An active project (<b>%2</b>) is currently open in this session.</p>")
                       .arg(actionContext, projTitle));
    msgBox.setInformativeText(QStringLiteral("Do you want to save your work before proceeding?"));
  }

  QPushButton *saveBtn = msgBox.addButton(QStringLiteral("Save"), QMessageBox::AcceptRole);
  QPushButton *discardBtn = msgBox.addButton(QStringLiteral("Don't Save"), QMessageBox::DestructiveRole);
  QPushButton *cancelBtn = msgBox.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
  msgBox.setDefaultButton(saveBtn);
  msgBox.exec();

  if (msgBox.clickedButton() == cancelBtn) {
    // User cancelled: abort action, keep window and data alive!
    return false;
  }

  if (msgBox.clickedButton() == discardBtn) {
    // User explicitly chose Don't Save: clean up autosave and allow action
    removeAutosaveFile();
    m_project.setDirty(false);
    if (m_editorPane && m_editorPane->editor() && m_editorPane->editor()->document())
      m_editorPane->editor()->document()->setModified(false);
    if (m_htmlPane && m_htmlPane->editor() && m_htmlPane->editor()->document())
      m_htmlPane->editor()->document()->setModified(false);
    if (m_cssPane && m_cssPane->editor() && m_cssPane->editor()->document())
      m_cssPane->editor()->document()->setModified(false);
    return true;
  }

  // User clicked Save:
  onSaveProject();
  if (hasUnsavedChanges()) {
    // User cancelled the file dialog or saving failed: abort action!
    return false;
  }

  removeAutosaveFile();
  return true;
}

void MainWindow::onNewProject() {
  if (m_hasActiveProject && !maybeSave(QStringLiteral("creating a new project"), true))
    return;

  saveCurrentChapterFromEditors();
  removeAutosaveFile();
  m_project.resetToNew();
  m_hasActiveProject = true;
  m_project.setDirty(false);
  m_chapterSidebar->refresh();
  loadCurrentChapterIntoEditors();
  m_cssPane->setCss(m_project.workSkinCss());
  m_editorPane->setCustomCss(m_project.workSkinCss());
  m_editorPane->setAvailableCssClasses(m_cssPane->cssClassNames());
  m_project.setDirty(false);
  if (m_editorPane && m_editorPane->editor() && m_editorPane->editor()->document())
    m_editorPane->editor()->document()->setModified(false);
  if (m_htmlPane && m_htmlPane->editor() && m_htmlPane->editor()->document())
    m_htmlPane->editor()->document()->setModified(false);
  if (m_cssPane && m_cssPane->editor() && m_cssPane->editor()->document())
    m_cssPane->editor()->document()->setModified(false);
  updateWindowTitle();
  updateMainMenuState();
  schedulePreviewUpdate();
  m_centralStack->setCurrentIndex(1);
}

bool MainWindow::openProjectFile(const QString &path) {
  if (path.isEmpty())
    return false;

  QString cleanPath = path.trimmed();
  if ((cleanPath.startsWith(QLatin1Char('"')) && cleanPath.endsWith(QLatin1Char('"'))) ||
      (cleanPath.startsWith(QLatin1Char('\'')) && cleanPath.endsWith(QLatin1Char('\'')))) {
    cleanPath = cleanPath.mid(1, cleanPath.length() - 2).trimmed();
  }
  if (cleanPath.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
    cleanPath = QUrl(cleanPath).toLocalFile();
  }
  const QFileInfo cleanFi(cleanPath);
  cleanPath = cleanFi.absoluteFilePath();

  if (m_hasActiveProject && !maybeSave(QStringLiteral("opening another project"), true))
    return false;

  QString loadPath = cleanPath;
  bool isRecoveredSession = false;

  // Check if companion .ao3projasve exists
  QString companionAutosave = cleanPath;
  if (companionAutosave.endsWith(QStringLiteral(".ao3proj"), Qt::CaseInsensitive)) {
    companionAutosave.chop(8);
  }
  companionAutosave += QStringLiteral(".ao3projasve");

  if (cleanPath.endsWith(QStringLiteral(".ao3projasve"), Qt::CaseInsensitive)) {
    loadPath = cleanPath;
    isRecoveredSession = true;
  } else if (QFile::exists(companionAutosave)) {
    QFileInfo autoFi(companionAutosave);
    QFileInfo origFi(cleanPath);
    if (autoFi.lastModified() >= origFi.lastModified()) {
      QMessageBox msgBox(this);
      msgBox.setWindowTitle(QStringLiteral("Autosave Recovery Detected"));
      msgBox.setIcon(QMessageBox::Question);
      msgBox.setText(QStringLiteral("<h3>Autosave Recovery File Found</h3>"
                                    "<p>A newer autosaved recovery file (<b>%1</b>) was found from an unsaved session.<br/>"
                                    "<small>Autosaved on: %2</small><br/>"
                                    "<small>Last saved on: %3</small></p>"
                                    "<p>Would you like to recover your unsaved session or open the last explicitly saved file?</p>")
                         .arg(autoFi.fileName(),
                              autoFi.lastModified().toString(Qt::ISODate),
                              origFi.lastModified().toString(Qt::ISODate)));
      QPushButton *recoverBtn = msgBox.addButton(QStringLiteral("Recover Autosave"), QMessageBox::AcceptRole);
      QPushButton *openSavedBtn = msgBox.addButton(QStringLiteral("Open Last Saved"), QMessageBox::RejectRole);
      msgBox.setDefaultButton(recoverBtn);
      msgBox.exec();

      if (msgBox.clickedButton() == recoverBtn) {
        loadPath = companionAutosave;
        isRecoveredSession = true;
      } else {
        QFile::remove(companionAutosave);
      }
    }
  }

  qCInfo(logApp) << "Opening project with 3-stage validation:" << loadPath;
  ProjectLoadReport report = ProjectSerializer::loadWithValidation(loadPath, m_project);

  if (!report.success) {
    qCWarning(logApp) << "Failed to open project file:" << loadPath
                      << "Stage:" << static_cast<int>(report.failedStage)
                      << "Reason:" << report.summaryError;

    // Fail straight to Main Menu with an error and a button showing detailed explanation
    m_hasActiveProject = false;
    updateMainMenuState();
    updateWindowTitle();
    m_centralStack->setCurrentIndex(0);

    ProjectLoadErrorDialog errDialog(report, this);
    errDialog.exec();
    return false;
  }

  // Render project
  m_hasActiveProject = true;
  m_chapterSidebar->refresh();
  loadCurrentChapterIntoEditors();
  m_cssPane->setCss(m_project.workSkinCss());
  m_editorPane->setCustomCss(m_project.workSkinCss());
  m_editorPane->setAvailableCssClasses(m_cssPane->cssClassNames());

  if (isRecoveredSession) {
    if (cleanPath.endsWith(QStringLiteral(".ao3projasve"), Qt::CaseInsensitive)) {
      QString targetProj = cleanPath;
      targetProj.chop(12);
      targetProj += QStringLiteral(".ao3proj");
      m_project.setFilePath(targetProj);
    } else {
      m_project.setFilePath(cleanPath);
    }
    m_project.setDirty(true);
    m_lastAutosavePath = companionAutosave;
    updateWindowTitle();
    updateMainMenuState();
    schedulePreviewUpdate();
    m_centralStack->setCurrentIndex(1); // Workspace
    statusBar()->showMessage(QStringLiteral("Project successfully recovered from autosave (.ao3projasve)"), 6000);
    return true;
  }

  m_project.setDirty(false);
  if (m_editorPane && m_editorPane->editor() && m_editorPane->editor()->document())
    m_editorPane->editor()->document()->setModified(false);
  if (m_htmlPane && m_htmlPane->editor() && m_htmlPane->editor()->document())
    m_htmlPane->editor()->document()->setModified(false);
  if (m_cssPane && m_cssPane->editor() && m_cssPane->editor()->document())
    m_cssPane->editor()->document()->setModified(false);

  updateWindowTitle();
  updateMainMenuState();
  schedulePreviewUpdate();
  m_centralStack->setCurrentIndex(1); // Workspace

  if (report.isLegacy) {
    qCInfo(logApp) << "Legacy project format detected for:" << cleanPath;
    statusBar()->showMessage(QStringLiteral("Opened legacy project in compatibility mode (saving will output as .ao3proj)"), 6000);

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle(QStringLiteral("Legacy Project Format"));
    msgBox.setText(QStringLiteral("<h3>Legacy Project Format Detected</h3>"
                                  "<p>This project is saved in an older legacy format (v1 JSON) and has been opened in <b>legacy compatibility mode</b>.</p>"));
    msgBox.setInformativeText(QStringLiteral(
        "When you save, Ensemble will automatically upgrade and save it as the standard modern <b>.ao3proj</b> archive container format."));
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setStyleSheet(ThemeManager::instance().getDialogStyleSheet());
    msgBox.exec();
  } else {
    statusBar()->showMessage(QStringLiteral("Opened %1").arg(cleanPath), 3000);
  }

  return true;
}

void MainWindow::onOpenProject() {
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("Open Project"), QString(),
      QStringLiteral("AO3 Project (*.ao3proj *.ao3projasve);;Legacy AO3 JSON (*.json);;All Files (*)"));
  if (path.isEmpty())
    return;

  openProjectFile(path);
}

void MainWindow::onCloseProject() {
  if (m_hasActiveProject && !maybeSave(QStringLiteral("closing the project"), true))
    return;

  removeAutosaveFile();
  m_project.resetToNew();
  m_hasActiveProject = false;
  m_project.setDirty(false);
  m_chapterSidebar->refresh();
  loadCurrentChapterIntoEditors();
  m_cssPane->setCss(m_project.workSkinCss());
  m_editorPane->setCustomCss(m_project.workSkinCss());
  m_editorPane->setAvailableCssClasses(m_cssPane->cssClassNames());
  m_project.setDirty(false);
  if (m_editorPane && m_editorPane->editor() && m_editorPane->editor()->document())
    m_editorPane->editor()->document()->setModified(false);
  if (m_htmlPane && m_htmlPane->editor() && m_htmlPane->editor()->document())
    m_htmlPane->editor()->document()->setModified(false);
  if (m_cssPane && m_cssPane->editor() && m_cssPane->editor()->document())
    m_cssPane->editor()->document()->setModified(false);
  updateWindowTitle();
  updateMainMenuState();
  m_centralStack->setCurrentIndex(0);
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

  QString finalPath = path;
  if (!finalPath.endsWith(QStringLiteral(".ao3proj"), Qt::CaseInsensitive)) {
    // If it was legacy .json or anything else, replace or append .ao3proj
    int dotIdx = finalPath.lastIndexOf(QLatin1Char('.'));
    if (dotIdx > 0 && finalPath.mid(dotIdx).contains(QStringLiteral("json"), Qt::CaseInsensitive)) {
      finalPath = finalPath.left(dotIdx) + QStringLiteral(".ao3proj");
    } else {
      finalPath += QStringLiteral(".ao3proj");
    }
  }

  QString error;
  if (!ProjectSerializer::save(finalPath, m_project, &error)) {
    QMessageBox::critical(this, QStringLiteral("Save Failed"), error);
    return false;
  }

  const bool wasLegacy = m_project.isLegacyFormat();
  m_project.setLegacyFormat(false);
  m_project.setFilePath(finalPath);
  m_project.setDirty(false);
  if (m_editorPane && m_editorPane->editor() && m_editorPane->editor()->document())
    m_editorPane->editor()->document()->setModified(false);
  if (m_htmlPane && m_htmlPane->editor() && m_htmlPane->editor()->document())
    m_htmlPane->editor()->document()->setModified(false);
  if (m_cssPane && m_cssPane->editor() && m_cssPane->editor()->document())
    m_cssPane->editor()->document()->setModified(false);

  removeAutosaveFile();
  updateWindowTitle();
  if (wasLegacy) {
    statusBar()->showMessage(QStringLiteral("Project upgraded and saved as .ao3proj: %1").arg(finalPath), 4000);
  } else {
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(finalPath), 3000);
  }
  return true;
}

QString MainWindow::currentAutosaveFilePath() const {
  if (!m_project.filePath().isEmpty()) {
    QString path = m_project.filePath();
    if (path.endsWith(QStringLiteral(".ao3proj"), Qt::CaseInsensitive)) {
      path.chop(8);
    }
    return path + QStringLiteral(".ao3projasve");
  }

  QString safeTitle = m_project.title().trimmed().toLower();
  safeTitle.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]")), QStringLiteral("_"));
  if (safeTitle.isEmpty()) {
    safeTitle = QStringLiteral("untitled_project");
  }
  return QDir(AppPaths::autosaveDir()).filePath(safeTitle + QStringLiteral(".ao3projasve"));
}

void MainWindow::performAutosave() {
  if (!m_hasActiveProject)
    return;

  saveCurrentChapterFromEditors();

  if (!hasUnsavedChanges())
    return;

  const QString autosavePath = currentAutosaveFilePath();
  if (autosavePath.isEmpty())
    return;

  QFileInfo fi(autosavePath);
  QDir().mkpath(fi.absolutePath());

  QString err;
  if (ProjectSerializer::save(autosavePath, m_project, &err)) {
    const_cast<MainWindow *>(this)->m_lastAutosavePath = autosavePath;
    qCInfo(logApp) << "60-second autosave written to:" << autosavePath;

    QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
    settings.setValue(QStringLiteral("autosave/hasActiveRecovery"), true);
    settings.setValue(QStringLiteral("autosave/recoveryFile"), autosavePath);
    settings.setValue(QStringLiteral("autosave/originalFile"), m_project.filePath());
    settings.setValue(QStringLiteral("autosave/projectTitle"), m_project.title());
    settings.setValue(QStringLiteral("autosave/timestamp"), QDateTime::currentDateTime().toString(Qt::ISODate));

    statusBar()->showMessage(QStringLiteral("Autosaved session (%1)").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))), 3000);
  } else {
    qCWarning(logApp) << "Autosave failed:" << err;
  }
}

void MainWindow::removeAutosaveFile() {
  const QString autoPath = currentAutosaveFilePath();
  if (QFile::exists(autoPath)) {
    QFile::remove(autoPath);
  }
  if (!m_lastAutosavePath.isEmpty() && QFile::exists(m_lastAutosavePath)) {
    QFile::remove(m_lastAutosavePath);
    m_lastAutosavePath.clear();
  }
  clearAutosaveSettings();
}

void MainWindow::clearAutosaveSettings() {
  QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
  settings.setValue(QStringLiteral("autosave/hasActiveRecovery"), false);
  settings.remove(QStringLiteral("autosave/recoveryFile"));
  settings.remove(QStringLiteral("autosave/originalFile"));
  settings.remove(QStringLiteral("autosave/projectTitle"));
  settings.remove(QStringLiteral("autosave/timestamp"));
}

void MainWindow::checkForCrashRecovery() {
  if (m_hasActiveProject)
    return;

  QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
  QString recoveryFile;
  QString originalFile;
  QString projectTitle;
  QString timestampStr;

  if (settings.value(QStringLiteral("autosave/hasActiveRecovery"), false).toBool()) {
    recoveryFile = settings.value(QStringLiteral("autosave/recoveryFile")).toString();
    originalFile = settings.value(QStringLiteral("autosave/originalFile")).toString();
    projectTitle = settings.value(QStringLiteral("autosave/projectTitle")).toString();
    timestampStr = settings.value(QStringLiteral("autosave/timestamp")).toString();
  }

  if (recoveryFile.isEmpty() || !QFile::exists(recoveryFile)) {
    QDir autoDir(AppPaths::autosaveDir());
    const QStringList files = autoDir.entryList({QStringLiteral("*.ao3projasve")}, QDir::Files, QDir::Time);
    if (!files.isEmpty()) {
      recoveryFile = autoDir.filePath(files.first());
      projectTitle = QFileInfo(recoveryFile).baseName();
      timestampStr = QFileInfo(recoveryFile).lastModified().toString(Qt::ISODate);
    }
  }

  if (recoveryFile.isEmpty() || !QFile::exists(recoveryFile))
    return;

  QMessageBox msgBox(this);
  msgBox.setWindowTitle(QStringLiteral("Session Crash Recovery"));
  msgBox.setIcon(QMessageBox::Information);
  msgBox.setText(QStringLiteral("<h3>Recover Unsaved Project?</h3>"
                                "<p>Ensemble detected an autosaved recovery file from a previous session that did not close cleanly:</p>"
                                "<p><b>%1</b><br/>"
                                "<small>Autosaved on: %2</small></p>"
                                "<p>Would you like to recover your unsaved work?</p>")
                     .arg(projectTitle.isEmpty() ? QFileInfo(recoveryFile).fileName() : projectTitle.toHtmlEscaped(),
                          timestampStr.isEmpty() ? QFileInfo(recoveryFile).lastModified().toString(Qt::ISODate) : timestampStr));
  QPushButton *recoverBtn = msgBox.addButton(QStringLiteral("Recover Project"), QMessageBox::AcceptRole);
  QPushButton *discardBtn = msgBox.addButton(QStringLiteral("Discard"), QMessageBox::DestructiveRole);
  msgBox.setDefaultButton(recoverBtn);
  msgBox.exec();

  if (msgBox.clickedButton() == recoverBtn) {
    qCInfo(logApp) << "Recovering project from autosave:" << recoveryFile;
    ProjectLoadReport report = ProjectSerializer::loadWithValidation(recoveryFile, m_project);
    if (report.success) {
      m_hasActiveProject = true;
      m_project.setDirty(true);
      if (!originalFile.isEmpty()) {
        m_project.setFilePath(originalFile);
      }
      m_lastAutosavePath = recoveryFile;
      m_chapterSidebar->refresh();
      loadCurrentChapterIntoEditors();
      m_cssPane->setCss(m_project.workSkinCss());
      m_editorPane->setCustomCss(m_project.workSkinCss());
      m_editorPane->setAvailableCssClasses(m_cssPane->cssClassNames());
      m_project.setDirty(true);
      updateWindowTitle();
      updateMainMenuState();
      schedulePreviewUpdate();
      m_centralStack->setCurrentIndex(1); // Workspace
      statusBar()->showMessage(QStringLiteral("Project successfully recovered from autosave (.ao3projasve)"), 6000);
    } else {
      qCWarning(logApp) << "Failed to recover from autosave:" << report.summaryError;
      QMessageBox::warning(this, QStringLiteral("Recovery Failed"),
                           QStringLiteral("Could not load autosaved file: %1").arg(report.summaryError));
    }
  } else {
    qCInfo(logApp) << "Discarding autosave file:" << recoveryFile;
    QFile::remove(recoveryFile);
    clearAutosaveSettings();
  }
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
  if (!m_hasActiveProject)
    return;
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
    if (m_hasActiveProject) {
      m_project.setDirty(true);
    }
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
  if (!m_hasActiveProject)
    return;
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
  if (!m_hasActiveProject) {
    m_editorPane->setCustomCss(css);
    return;
  }
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
  if (!m_hasActiveProject)
    return;
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

  if (m_editorPane && m_editorPane->editor() && m_editorPane->editor()->document())
    m_editorPane->editor()->document()->setModified(false);
  if (m_htmlPane && m_htmlPane->editor() && m_htmlPane->editor()->document())
    m_htmlPane->editor()->document()->setModified(false);
  if (m_cssPane && m_cssPane->editor() && m_cssPane->editor()->document())
    m_cssPane->editor()->document()->setModified(false);

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

static int countWordsInPlainText(const QString &text) {
  if (text.trimmed().isEmpty())
    return 0;
  return text.split(QRegularExpression(QStringLiteral("[\\s\\x{2029}]+")), Qt::SkipEmptyParts).size();
}

void MainWindow::updateStatusBar() {
  if (!m_wordCountLabel) return;

  if (!m_hasActiveProject || m_project.chapters().isEmpty()) {
    m_wordCountLabel->setText(QStringLiteral("Words: 0 (Total: 0)"));
    return;
  }

  // 1. Calculate active chapter words and current selection words
  int chapterWords = 0;
  int selWords = 0;

  if (m_editorTabs && m_editorTabs->currentIndex() == 1 && m_htmlPane && m_htmlPane->editor()) {
    // Raw HTML editing tab
    QTextDocument doc;
    doc.setHtml(m_htmlPane->editor()->toPlainText());
    chapterWords = countWordsInPlainText(doc.toPlainText());

    QTextCursor cur = m_htmlPane->editor()->textCursor();
    if (cur.hasSelection()) {
      QTextDocument selDoc;
      selDoc.setHtml(cur.selectedText());
      selWords = countWordsInPlainText(selDoc.toPlainText());
    }
  } else if (m_editorPane && m_editorPane->editor()) {
    // Rich text editor tab
    const QString plainText = m_editorPane->editor()->toPlainText();
    chapterWords = countWordsInPlainText(plainText);

    QTextCursor cur = m_editorPane->editor()->textCursor();
    if (cur.hasSelection()) {
      selWords = countWordsInPlainText(cur.selectedText());
    }
  } else {
    const Chapter *ch = m_project.activeChapter();
    if (ch) {
      QTextDocument doc;
      doc.setHtml(ch->html());
      chapterWords = countWordsInPlainText(doc.toPlainText());
    }
  }

  // 2. Calculate total work words across all chapters
  int totalWords = 0;
  const int activeIdx = m_project.activeChapterIndex();
  const auto &chapters = m_project.chapters();

  for (int i = 0; i < chapters.size(); ++i) {
    if (i == activeIdx) {
      totalWords += chapterWords;
    } else {
      QTextDocument doc;
      doc.setHtml(chapters.at(i).html());
      totalWords += countWordsInPlainText(doc.toPlainText());
    }
  }

  // 3. Format exact word count with locale thousands separator (no tilde)
  const QLocale locale;
  QString text;
  if (selWords > 0) {
    text = QStringLiteral("Words: %1 selected of %2 (Total: %3)")
        .arg(locale.toString(selWords),
             locale.toString(chapterWords),
             locale.toString(totalWords));
  } else {
    text = QStringLiteral("Words: %1 (Total: %2)")
        .arg(locale.toString(chapterWords),
             locale.toString(totalWords));
  }

  m_wordCountLabel->setText(text);

  // Clear obsolete temporary Words: message from status bar if present
  if (statusBar()->currentMessage().startsWith(QStringLiteral("Words:"))) {
    statusBar()->clearMessage();
  }
}

void MainWindow::updateWindowTitle() {
  if (!m_hasActiveProject) {
    setWindowTitle(QStringLiteral("Ensemble"));
    return;
  }

  QString title = m_project.title().trimmed();
  if (title.isEmpty()) {
    title = QStringLiteral("Untitled Work");
  }

  if (m_project.isDirty())
    title += QStringLiteral(" *");

  if (!m_project.filePath().isEmpty())
    title += QStringLiteral(" — ") + m_project.filePath();

  title += QStringLiteral(" — Ensemble");
  setWindowTitle(title);
}

void MainWindow::onShowMainMenu() {
  updateMainMenuState();
  m_centralStack->setCurrentIndex(0);
}

void MainWindow::onStartWriting() {
  if (!m_hasActiveProject) {
    m_hasActiveProject = true;
    m_project.resetToNew();
    m_project.setDirty(false);
    updateMainMenuState();
  }
  updateWindowTitle();
  schedulePreviewUpdate();
  m_centralStack->setCurrentIndex(1);
}

void MainWindow::onShowHelpGuide() {
  if (!m_helpGuideDialog) {
    m_helpGuideDialog = new HelpGuideDialog(this);
  }
  m_helpGuideDialog->updateTheme();
  m_helpGuideDialog->show();
  m_helpGuideDialog->raise();
  m_helpGuideDialog->activateWindow();
}

void MainWindow::updateMainMenuState() {
  if (m_mainMenuWidget) {
    m_mainMenuWidget->setProjectState(m_hasActiveProject, m_project.title());
  }
  if (m_closeProjectAction) {
    m_closeProjectAction->setEnabled(m_hasActiveProject);
  }
  if (m_saveAction) {
    m_saveAction->setEnabled(m_hasActiveProject);
  }
  if (m_saveAsAction) {
    m_saveAsAction->setEnabled(m_hasActiveProject);
  }
  if (m_exportChapterAction) {
    m_exportChapterAction->setEnabled(m_hasActiveProject);
  }
  if (m_exportAllAction) {
    m_exportAllAction->setEnabled(m_hasActiveProject);
  }
  if (m_copyHtmlAction) {
    m_copyHtmlAction->setEnabled(m_hasActiveProject);
  }
  if (m_propertiesAction) {
    m_propertiesAction->setEnabled(m_hasActiveProject);
  }
  if (m_loreGraphAction) {
    m_loreGraphAction->setEnabled(m_hasActiveProject);
  }
}

void MainWindow::onShowAbout() { m_aboutDialog->exec(); }

void MainWindow::onShowCredits() {
  if (!m_creditsDialog) {
    m_creditsDialog = new CreditsDialog(this);
  }
  m_creditsDialog->exec();
}

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
  sb->setStyleSheet(ThemeManager::instance().getStatusBarStyleSheet());

  m_wordCountLabel = new QLabel(QStringLiteral("Words: 0 (Total: 0)"), this);
  m_wordCountLabel->setToolTip(QStringLiteral("Exact Chapter Words, Total Work Words, and Selection Count"));
  sb->addWidget(m_wordCountLabel);

  m_statusInfoLabel = new QLabel(QStringLiteral("Ready"), this);
  sb->addWidget(m_statusInfoLabel, 1);

  // Line and Column Indicator (e.g. Ln 692, Col 31)
  m_lineColLabel = new QLabel(QStringLiteral("Ln 1, Col 1"), this);
  sb->addPermanentWidget(m_lineColLabel);

  // Insert/Overwrite Mode Toggle Button (INS / OVR)
  m_insertModeButton = new QPushButton(QStringLiteral("INS"), this);
  m_insertModeButton->setFlat(true);
  m_insertModeButton->setCursor(Qt::PointingHandCursor);
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
  connect(m_problemsBadgeButton, &QPushButton::clicked, this, &MainWindow::onShowProblemsDialog);
  sb->addPermanentWidget(m_problemsBadgeButton);

  // MCP Status Badge (🤖 MCP)
  m_mcpStatusBadge = new QPushButton(this);
  m_mcpStatusBadge->setFlat(true);
  m_mcpStatusBadge->setCursor(Qt::PointingHandCursor);
  m_mcpStatusBadge->setToolTip(QStringLiteral("Model Context Protocol (MCP) Server Status. Click to view AI Agent Thoughts & Monitor."));
  m_mcpStatusBadge->setText(QStringLiteral("🤖 MCP: Off"));
  m_mcpStatusBadge->setStyleSheet(QStringLiteral("color: #6272a4; font-size: 11px; padding: 2px 6px;"));
  connect(m_mcpStatusBadge, &QPushButton::clicked, this, &MainWindow::onShowAgentThoughts);
  sb->addPermanentWidget(m_mcpStatusBadge);

  // Theme Quick Toggle Button (🌙 / ☀️)
  m_themeToggleButton = new QPushButton(this);
  m_themeToggleButton->setFlat(true);
  m_themeToggleButton->setCursor(Qt::PointingHandCursor);
  m_themeToggleButton->setToolTip(QStringLiteral("Toggle Dark / Light Theme"));
  connect(m_themeToggleButton, &QPushButton::clicked, this, []() {
      ThemeManager::instance().toggleTheme();
  });
  sb->addPermanentWidget(m_themeToggleButton);

  onThemeChanged(ThemeManager::instance().currentTheme());
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
  if (event->type() == QEvent::FileOpen) {
    auto *fileOpenEvent = static_cast<QFileOpenEvent *>(event);
    raiseToFront();
    openProjectFile(fileOpenEvent->file());
    return true;
  }
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

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData() && event->mimeData()->hasUrls()) {
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
      if (url.isLocalFile()) {
        const QString localFile = url.toLocalFile();
        if (localFile.endsWith(QStringLiteral(".ao3proj"), Qt::CaseInsensitive) ||
            localFile.endsWith(QStringLiteral(".ao3projasve"), Qt::CaseInsensitive) ||
            localFile.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
          event->acceptProposedAction();
          return;
        }
      }
    }
  }
  QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event) {
  if (event->mimeData() && event->mimeData()->hasUrls()) {
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
      if (url.isLocalFile()) {
        const QString localFile = url.toLocalFile();
        if (localFile.endsWith(QStringLiteral(".ao3proj"), Qt::CaseInsensitive) ||
            localFile.endsWith(QStringLiteral(".ao3projasve"), Qt::CaseInsensitive) ||
            localFile.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
          event->acceptProposedAction();
          raiseToFront();
          openProjectFile(localFile);
          return;
        }
      }
    }
  }
  QMainWindow::dropEvent(event);
}

void MainWindow::updateProblemsDiagnostics() {
  m_currentProblems.clear();
  int errorCount = 0;
  int warningCount = 0;

  // 1. Check Work Skin CSS syntax & structure
  const QString cssText = m_project.workSkinCss();
  QStringList cssErrors;
  CssEditorPane::validateCss(cssText, &cssErrors);
  for (const QString &err : cssErrors) {
    ProblemItem item;
    item.severity = ProblemItem::Error;
    item.category = QStringLiteral("Work Skin CSS");
    item.description = err;
    item.chapterIndex = -1;
    item.affectedChapters.clear();
    m_currentProblems.append(item);
    errorCount++;
  }

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
      item.affectedChapters.clear();
      m_currentProblems.append(item);
      warningCount++;
    }
  }

  // 2. Check chapter structural issues & missing CSS classes
  static const QRegularExpression classAttrRe(R"re(class\s*=\s*"([^"]+)")re", QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression tagExtractRe(R"(<\/?([a-zA-Z][\w:-]*))");
  const QSet<QString> allowedTags = Ao3TagWhitelist::allowedTags();
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
      item.affectedChapters = { chIdx };
      m_currentProblems.append(item);
      warningCount++;
    }
    if (html.trimmed().isEmpty()) {
      ProblemItem item;
      item.severity = ProblemItem::Warning;
      item.category = QStringLiteral("Chapter Content");
      item.description = QStringLiteral("Chapter %1 (\"%2\") has no body content.").arg(chIdx + 1).arg(ch.title());
      item.chapterIndex = chIdx;
      item.affectedChapters = { chIdx };
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
          item.affectedChapters = { chIdx };
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
      item.affectedChapters = { chIdx };
      m_currentProblems.append(item);
      warningCount++;
    }

    // Scan for disallowed HTML tags according to AO3 whitelist
    QSet<QString> disallowedInChapter;
    QRegularExpressionMatchIterator tagIt = tagExtractRe.globalMatch(html);
    while (tagIt.hasNext()) {
      const QString tagName = tagIt.next().captured(1).toLower();
      if (!allowedTags.contains(tagName) && !disallowedInChapter.contains(tagName)) {
        disallowedInChapter.insert(tagName);
        ProblemItem item;
        item.severity = ProblemItem::Warning;
        item.category = QStringLiteral("AO3 Compliance");
        item.description = QStringLiteral("Disallowed HTML tag <%1> detected in Chapter %2 (\"%3\"). AO3 will strip this tag upon posting.")
                               .arg(tagName)
                               .arg(chIdx + 1)
                               .arg(ch.title());
        item.chapterIndex = chIdx;
        item.affectedChapters = { chIdx };
        m_currentProblems.append(item);
        warningCount++;
      }
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
    item.affectedChapters = chList;
    m_currentProblems.append(item);
    warningCount++;
  }

  if (!m_problemsBadgeButton)
    return;

  const bool isDark = ThemeManager::instance().isDarkMode();
  if (errorCount > 0) {
    m_problemsBadgeButton->setText(QStringLiteral("⊗ %1  ⚠ %2").arg(errorCount).arg(warningCount));
    m_problemsBadgeButton->setStyleSheet(isDark ? QStringLiteral(
        "QPushButton { color: #ff5555; background-color: #3b1818; border: 1px solid #621616; padding: 2px 10px; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #542222; }"
    ) : QStringLiteral(
        "QPushButton { color: #dc2626; background-color: #fee2e2; border: 1px solid #fca5a5; padding: 2px 10px; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #fecaca; }"
    ));
  } else if (warningCount > 0) {
    m_problemsBadgeButton->setText(QStringLiteral("⊗ 0  ⚠ %1").arg(warningCount));
    m_problemsBadgeButton->setStyleSheet(isDark ? QStringLiteral(
        "QPushButton { color: #ffb86c; background-color: #2b2718; border: 1px solid #624c16; padding: 2px 10px; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #3f3823; }"
    ) : QStringLiteral(
        "QPushButton { color: #d97706; background-color: #fef3c7; border: 1px solid #fde68a; padding: 2px 10px; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #fde68a; }"
    ));
  } else {
    m_problemsBadgeButton->setText(QStringLiteral("✔ 0 Problems"));
    m_problemsBadgeButton->setStyleSheet(isDark ? QStringLiteral(
        "QPushButton { color: #50fa7b; background-color: #192b20; border: 1px solid #1c452b; padding: 2px 10px; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #243f2f; }"
    ) : QStringLiteral(
        "QPushButton { color: #15803d; background-color: #dcfce7; border: 1px solid #bbf7d0; padding: 2px 10px; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #bbf7d0; }"
    ));
  }
}

void MainWindow::onThemeChanged(AppTheme theme) {
  if (m_darkThemeAction) m_darkThemeAction->setChecked(theme == AppTheme::Dark);
  if (m_darkerThemeAction) m_darkerThemeAction->setChecked(theme == AppTheme::Darker);
  if (m_lightThemeAction) m_lightThemeAction->setChecked(theme == AppTheme::Light);
  if (m_reversiThemeAction) m_reversiThemeAction->setChecked(theme == AppTheme::Ao3Reversi);

  const bool isDark = ThemeManager::instance().isDarkMode();

  if (m_wordCountLabel) {
    m_wordCountLabel->setStyleSheet(isDark ? QStringLiteral("color: #f1f5f9; font-size: 11px; font-weight: 600; padding: 2px 6px;")
                                           : QStringLiteral("color: #0f172a; font-size: 11px; font-weight: 600; padding: 2px 6px;"));
  }
  if (m_statusInfoLabel) {
    m_statusInfoLabel->setStyleSheet(isDark ? QStringLiteral("color: #aeb3c6; font-size: 11px;")
                                           : QStringLiteral("color: #475569; font-size: 11px;"));
  }
  if (m_lineColLabel) {
    m_lineColLabel->setStyleSheet(isDark ? QStringLiteral("color: #aeb3c6; font-size: 11px; padding: 2px 8px;")
                                         : QStringLiteral("color: #475569; font-size: 11px; padding: 2px 8px;"));
  }
  if (m_insertModeButton) {
    m_insertModeButton->setStyleSheet(isDark ? QStringLiteral(
        "QPushButton { color: #aeb3c6; padding: 2px 6px; font-size: 11px; font-weight: bold; border: none; }"
        "QPushButton:hover { color: #ffffff; background-color: #282a36; border-radius: 3px; }"
    ) : QStringLiteral(
        "QPushButton { color: #475569; padding: 2px 6px; font-size: 11px; font-weight: bold; border: none; }"
        "QPushButton:hover { color: #0f172a; background-color: #e2e8f0; border-radius: 3px; }"
    ));
  }
  if (m_themeToggleButton) {
    if (theme == AppTheme::Light) {
      m_themeToggleButton->setText(QStringLiteral("☀️ Light"));
      m_themeToggleButton->setToolTip(QStringLiteral("Current Theme: Light. Click to toggle (Light -> Dark -> AO3 Reversi)"));
    } else if (theme == AppTheme::Dark) {
      m_themeToggleButton->setText(QStringLiteral("🌙 Dark"));
      m_themeToggleButton->setToolTip(QStringLiteral("Current Theme: Dark. Click to toggle (Light -> Dark -> AO3 Reversi)"));
    } else if (theme == AppTheme::Ao3Reversi) {
      m_themeToggleButton->setText(QStringLiteral("🔴 Reversi"));
      m_themeToggleButton->setToolTip(QStringLiteral("Current Theme: AO3 Reversi. Click to toggle (Light -> Dark -> AO3 Reversi)"));
    } else {
      m_themeToggleButton->setText(QStringLiteral("🖤 OLED"));
      m_themeToggleButton->setToolTip(QStringLiteral("Current Theme: Darker (OLED). Click to toggle."));
    }

    m_themeToggleButton->setStyleSheet(isDark ? QStringLiteral(
        "QPushButton { color: #f8f8f2; padding: 2px 8px; font-size: 11px; font-weight: 600; border: none; }"
        "QPushButton:hover { background-color: #282a36; border-radius: 3px; }"
    ) : QStringLiteral(
        "QPushButton { color: #0f172a; padding: 2px 8px; font-size: 11px; font-weight: 600; border: none; }"
        "QPushButton:hover { background-color: #e2e8f0; border-radius: 3px; }"
    ));
  }

  statusBar()->setStyleSheet(ThemeManager::instance().getStatusBarStyleSheet());

  if (m_chapterSidebar) {
    m_chapterSidebar->setStyleSheet(ThemeManager::instance().getSidebarStyleSheet());
  }
  if (m_mainMenuWidget) {
    m_mainMenuWidget->updateTheme();
  }
  if (m_editorPane) {
    m_editorPane->updateTheme();
  }
  if (m_htmlPane) {
    m_htmlPane->updateTheme();
  }
  if (m_cssPane) {
    m_cssPane->updateTheme();
  }
  if (m_previewPane) {
    m_previewPane->updateTheme();
  }
  if (m_previewPopup) {
    m_previewPopup->updateTheme();
  }
  if (m_settingsDialog) {
    m_settingsDialog->updateTheme();
  }
  if (m_aboutDialog) {
    m_aboutDialog->updateTheme();
  }
  if (m_creditsDialog) {
    m_creditsDialog->updateTheme();
  }
  if (m_problemsDialog) {
    m_problemsDialog->updateTheme();
  }
  if (m_agentThoughtsDialog) {
    m_agentThoughtsDialog->updateTheme();
  }

  updateProblemsDiagnostics();
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

void MainWindow::onShowSettings() {
  if (!m_settingsDialog) {
    m_settingsDialog = new SettingsDialog(this);
    connect(m_settingsDialog, &SettingsDialog::editorSettingsChanged, this, [this]() {
      if (m_editorPane)
        m_editorPane->updateSettings();
      if (m_htmlPane)
        m_htmlPane->updateSettings();
      if (m_cssPane)
        m_cssPane->updateSettings();
    });
    connect(m_settingsDialog, &SettingsDialog::mcpServerToggled, this, [this](bool enabled) {
      if (enabled) {
        startMcpServer();
      } else {
        stopMcpServer();
      }
    });
  }
  m_settingsDialog->setMcpServer(m_mcpServer);
  m_settingsDialog->show();
  m_settingsDialog->raise();
  m_settingsDialog->activateWindow();
}

void MainWindow::onShowAgentThoughts() {
  if (!m_agentThoughtsDialog) {
    m_agentThoughtsDialog = new AgentThoughtsDialog(m_mcpServer, this);
  } else if (m_mcpServer) {
    m_agentThoughtsDialog->setMcpServer(m_mcpServer);
  }
  m_agentThoughtsDialog->setProject(&m_project);
  m_agentThoughtsDialog->show();
  m_agentThoughtsDialog->raise();
  m_agentThoughtsDialog->activateWindow();
}

void MainWindow::onShowLoreGraph() {
  if (!m_hasActiveProject) {
    return;
  }
  if (!m_loreGraphDialog) {
    m_loreGraphDialog = new LoreGraphDialog(&m_project, this);
  } else {
    m_loreGraphDialog->setProject(&m_project);
  }
  m_loreGraphDialog->show();
  m_loreGraphDialog->raise();
  m_loreGraphDialog->activateWindow();
}

void MainWindow::onEditProperties() {
  if (!m_hasActiveProject) {
    return;
  }
  ProjectPropertiesDialog dialog(&m_project, this);
  if (dialog.exec() == QDialog::Accepted) {
    updateWindowTitle();
    updateStatusBar();
    updateLoreGraphUi();
  }
}

void MainWindow::updateLoreGraphUi() {
  if (m_loreGraphDialog && m_loreGraphDialog->isVisible()) {
    m_loreGraphDialog->refreshFromProject();
  }
}

void MainWindow::raiseToFront() {
  qCInfo(logApp) << "Bringing existing MainWindow to the front of the desktop...";
  if (isMinimized()) {
    showNormal();
  } else {
    show();
  }
  setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
  raise();
  activateWindow();
  if (auto *wh = windowHandle()) {
    wh->requestActivate();
  }
  QApplication::alert(this, 1500);
}

void MainWindow::startMcpServer(quint16 preferredPort) {
  if (!m_mcpServer) {
    m_mcpServer = new McpServer(this);
    m_mcpHandler = new McpHandler(m_mcpServer, this, this);

    auto updateBadge = [this]() {
      if (!m_mcpStatusBadge) return;
      if (m_mcpServer && m_mcpServer->isRunning()) {
        const int count = m_mcpServer->clientCount();
        if (count > 0) {
          m_mcpStatusBadge->setText(QStringLiteral("🤖 MCP:%1 (%2)").arg(m_mcpServer->port()).arg(count));
          m_mcpStatusBadge->setToolTip(QStringLiteral("MCP Server listening on ws://127.0.0.1:%1 (%2 AI agent(s) connected). Click to view Agent Thoughts.")
                                           .arg(m_mcpServer->port()).arg(count));
        } else {
          m_mcpStatusBadge->setText(QStringLiteral("🤖 MCP:%1").arg(m_mcpServer->port()));
          m_mcpStatusBadge->setToolTip(QStringLiteral("MCP Server listening on ws://127.0.0.1:%1 (0 agents connected). Click to view Agent Thoughts.")
                                           .arg(m_mcpServer->port()));
        }
        m_mcpStatusBadge->setStyleSheet(QStringLiteral(
            "color: #50fa7b; font-weight: bold; font-size: 11px; padding: 2px 6px;"
        ));
      } else {
        m_mcpStatusBadge->setText(QStringLiteral("🤖 MCP: Off"));
        m_mcpStatusBadge->setToolTip(QStringLiteral("Model Context Protocol (MCP) Server is offline. Click to view Agent Thoughts or configure."));
        m_mcpStatusBadge->setStyleSheet(QStringLiteral(
            "color: #6272a4; font-size: 11px; padding: 2px 6px;"
        ));
      }
    };

    connect(m_mcpServer, &McpServer::statusChanged, this, updateBadge);
    connect(m_mcpServer, &McpServer::agentPresenceChanged, this,
            [this, updateBadge](int count, const QStringList &names) {
              Q_UNUSED(count);
              Q_UNUSED(names);
              updateBadge();
            });

    if (m_agentThoughtsDialog) {
      m_agentThoughtsDialog->setMcpServer(m_mcpServer);
    }
  }

  if (!m_mcpServer->isRunning()) {
    m_mcpServer->start(preferredPort);
  }
}

void MainWindow::stopMcpServer() {
  if (m_mcpServer && m_mcpServer->isRunning()) {
    m_mcpServer->stop();
  }
}

SanitizeResult MainWindow::applyMcpChapterHtml(int chapterIndex, const QString &html) {
  if (!m_hasActiveProject || chapterIndex < 0 || chapterIndex >= m_project.chapters().size()) {
    return {};
  }

  const SanitizeResult result = Ao3HtmlSanitizer::sanitizeDetailed(html);
  m_project.chapters()[chapterIndex].setHtml(result.cleanHtml);
  m_project.setDirty(true);

  if (chapterIndex == m_project.activeChapterIndex()) {
    m_syncInProgress = true;
    m_editorPane->setHtml(result.cleanHtml);
    m_htmlPane->setHtml(result.cleanHtml);
    m_syncInProgress = false;
    m_htmlSourceEditing = false;
  }

  updateWindowTitle();
  updateStatusBar();
  schedulePreviewUpdate();
  updateProblemsDiagnostics();
  return result;
}

void MainWindow::addChapterFromMcp(const QString &title) {
  if (!m_hasActiveProject) return;
  m_project.addChapter(title.isEmpty() ? QStringLiteral("New Chapter") : title);
  m_project.setDirty(true);
  m_chapterSidebar->refresh();
  updateWindowTitle();
  updateStatusBar();
}

void MainWindow::deleteChapterFromMcp(int chapterIndex) {
  if (!m_hasActiveProject) return;
  if (chapterIndex < 0 || chapterIndex >= m_project.chapters().size()) return;

  m_project.removeChapter(chapterIndex);
  m_project.setDirty(true);
  m_chapterSidebar->refresh();
  loadCurrentChapterIntoEditors();
  updateWindowTitle();
  updateStatusBar();
  schedulePreviewUpdate();
}

void MainWindow::reorderChaptersFromMcp(int from, int to) {
  if (!m_hasActiveProject) return;
  m_project.moveChapter(from, to);
  m_project.setDirty(true);
  m_chapterSidebar->refresh();
  updateWindowTitle();
}

void MainWindow::setWorkSkinCssFromMcp(const QString &css) {
  if (!m_hasActiveProject) {
    m_editorPane->setCustomCss(css);
    return;
  }
  m_project.setWorkSkinCss(css);
  m_project.setDirty(true);
  m_cssPane->setCss(css);
  m_editorPane->setCustomCss(css);
  updateWindowTitle();
  schedulePreviewUpdate();
}

bool MainWindow::saveProjectFromMcp() {
  if (!m_hasActiveProject) return false;
  if (m_project.filePath().isEmpty()) return false;
  return saveProjectToPath(m_project.filePath());
}

bool MainWindow::loadProjectFromMcp(const QString &path, bool saveCurrent, QString *errorMessage) {
  if (path.isEmpty() || !QFile::exists(path)) {
    if (errorMessage) *errorMessage = QStringLiteral("File not found: %1").arg(path);
    return false;
  }

  if (m_hasActiveProject && m_project.isDirty() && saveCurrent) {
    if (!m_project.filePath().isEmpty()) {
      saveProjectToPath(m_project.filePath());
    }
  }

  Ao3Project loadedProj;
  ProjectLoadReport report = ProjectSerializer::loadWithValidation(path, loadedProj);
  if (!report.success) {
    if (errorMessage) *errorMessage = report.summaryError;
    return false;
  }

  m_project = loadedProj;
  m_project.setFilePath(path);
  m_hasActiveProject = true;
  m_chapterSidebar->refresh();
  loadCurrentChapterIntoEditors();
  m_cssPane->setCss(m_project.workSkinCss());
  m_editorPane->setCustomCss(m_project.workSkinCss());
  m_editorPane->setAvailableCssClasses(m_cssPane->cssClassNames());

  m_project.setDirty(false);
  if (m_editorPane && m_editorPane->editor() && m_editorPane->editor()->document())
    m_editorPane->editor()->document()->setModified(false);
  if (m_htmlPane && m_htmlPane->editor() && m_htmlPane->editor()->document())
    m_htmlPane->editor()->document()->setModified(false);
  if (m_cssPane && m_cssPane->editor() && m_cssPane->editor()->document())
    m_cssPane->editor()->document()->setModified(false);

  removeAutosaveFile();
  clearAutosaveSettings();

  m_centralStack->setCurrentIndex(1);
  updateMainMenuState();
  updateWindowTitle();
  updateStatusBar();
  schedulePreviewUpdate();
  updateProblemsDiagnostics();
  updateLoreGraphUi();
  return true;
}

bool MainWindow::reloadProjectFromDisk() {
  if (!m_hasActiveProject) return false;
  const QString path = m_project.filePath();
  if (path.isEmpty() || !QFile::exists(path)) return false;

  qCInfo(logApp) << "Reloading project from disk (discarding unsaved changes):" << path;
  Ao3Project reloaded;
  ProjectLoadReport report = ProjectSerializer::loadWithValidation(path, reloaded);
  if (!report.success) {
    qCWarning(logApp) << "Failed to reload project from disk:" << report.summaryError;
    return false;
  }

  m_project = reloaded;
  m_hasActiveProject = true;
  m_chapterSidebar->refresh();
  loadCurrentChapterIntoEditors();
  m_cssPane->setCss(m_project.workSkinCss());
  m_editorPane->setCustomCss(m_project.workSkinCss());
  m_editorPane->setAvailableCssClasses(m_cssPane->cssClassNames());

  m_project.setDirty(false);
  if (m_editorPane && m_editorPane->editor() && m_editorPane->editor()->document())
    m_editorPane->editor()->document()->setModified(false);
  if (m_htmlPane && m_htmlPane->editor() && m_htmlPane->editor()->document())
    m_htmlPane->editor()->document()->setModified(false);
  if (m_cssPane && m_cssPane->editor() && m_cssPane->editor()->document())
    m_cssPane->editor()->document()->setModified(false);

  removeAutosaveFile();
  clearAutosaveSettings();

  updateWindowTitle();
  updateStatusBar();
  schedulePreviewUpdate();
  updateProblemsDiagnostics();
  return true;
}

QByteArray MainWindow::capturePreviewScreenshot(int chapterIndex) {
  if (!m_hasActiveProject || chapterIndex < 0 || chapterIndex >= m_project.chapters().size()) {
    return QByteArray();
  }

  const Chapter &ch = m_project.chapters().at(chapterIndex);
  if (m_previewPane) {
    m_previewPane->updatePreview(ch.title(), ch.html(), m_project.workSkinCss());
  }

  // Let Chromium engine parse, layout, and render
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 300) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }

  QPixmap pixmap;
  if (m_previewPane) {
    pixmap = m_previewPane->captureScreenshot();
  }

  // If pixmap is null or zero-sized (e.g. headless offscreen mode without active GL surface),
  // fallback to high-fidelity QPainter / QTextDocument rendering so MCP client always receives a valid PNG
  if (pixmap.isNull() || pixmap.width() <= 0 || pixmap.height() <= 0) {
    QImage fallbackImg(1024, 768, QImage::Format_ARGB32_Premultiplied);
    fallbackImg.fill(QColor(QStringLiteral("#121318")));
    QPainter painter(&fallbackImg);
    painter.setRenderHint(QPainter::Antialiasing);

    // Header banner
    painter.fillRect(QRect(0, 0, 1024, 34), QColor(QStringLiteral("#1a1c23")));
    painter.setPen(QColor(QStringLiteral("#aeb3c6")));
    painter.setFont(QFont(QStringLiteral("sans-serif"), 10));
    painter.drawText(QRect(16, 8, 992, 20), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("Preview: %1 (Rendered with active AO3 Work Skin)").arg(ch.title()));

    // Chapter content with workskin CSS
    QTextDocument doc;
    doc.setDefaultStyleSheet(m_project.workSkinCss());
    doc.setHtml(QStringLiteral("<h2 style='color:#33ccba; font-family:sans-serif;'>%1</h2><div style='color:#e2e2e5; font-family:serif; font-size:15px; line-height:1.6;'>%2</div>")
                    .arg(ch.title().toHtmlEscaped(), ch.html()));
    doc.setTextWidth(984);
    painter.translate(20, 50);
    doc.drawContents(&painter);
    pixmap = QPixmap::fromImage(fallbackImg);
  }

  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  pixmap.save(&buffer, "PNG");
  return bytes;
}

