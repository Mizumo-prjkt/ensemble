#include "HtmlSourcePane.h"
#include "CodeEditor.h"
#include "EditorMinimap.h"
#include "HtmlSyntaxHighlighter.h"
#include "export/Ao3HtmlSanitizer.h"
#include "export/Ao3TagWhitelist.h"
#include "themes/ThemeManager.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QXmlStreamReader>

HtmlSourcePane::HtmlSourcePane(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // 1. Issue Banner Card (collapsible, counts issues instead of listing them)
  m_issueCard = new QFrame(this);
  m_issueCard->setObjectName(QStringLiteral("htmlIssueCard"));
  auto *cardLayout = new QVBoxLayout(m_issueCard);
  cardLayout->setContentsMargins(10, 8, 10, 8);
  cardLayout->setSpacing(6);

  auto *cardHeader = new QHBoxLayout();
  cardHeader->setContentsMargins(0, 0, 0, 0);
  cardHeader->setSpacing(8);

  m_issueSummaryLabel = new QLabel(m_issueCard);
  m_issueSummaryLabel->setObjectName(QStringLiteral("issueSummaryLabel"));
  m_issueSummaryLabel->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 11px;"));
  cardHeader->addWidget(m_issueSummaryLabel, 1);

  m_toggleDetailsBtn = new QPushButton(QStringLiteral("Show Details ▾"), m_issueCard);
  m_toggleDetailsBtn->setObjectName(QStringLiteral("toggleDetailsBtn"));
  m_toggleDetailsBtn->setCursor(Qt::PointingHandCursor);
  connect(m_toggleDetailsBtn, &QPushButton::clicked, this, &HtmlSourcePane::toggleIssueDetails);
  cardHeader->addWidget(m_toggleDetailsBtn);

  m_openProblemsBtn = new QPushButton(QStringLiteral("🔍 View in Problems Window"), m_issueCard);
  m_openProblemsBtn->setObjectName(QStringLiteral("viewProblemsBtn"));
  m_openProblemsBtn->setCursor(Qt::PointingHandCursor);
  m_openProblemsBtn->setToolTip(QStringLiteral("Open the dedicated Problems Window with story-wide diagnostics"));
  connect(m_openProblemsBtn, &QPushButton::clicked, this, &HtmlSourcePane::openProblemsRequested);
  cardHeader->addWidget(m_openProblemsBtn);

  m_issueDetailsLabel = new QLabel(m_issueCard);
  m_issueDetailsLabel->setObjectName(QStringLiteral("issueDetailsLabel"));
  m_issueDetailsLabel->setWordWrap(true);
  m_issueDetailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_issueDetailsLabel->setVisible(false);
  m_issueDetailsLabel->setStyleSheet(QStringLiteral("font-size: 11px; line-height: 1.4; padding-top: 4px;"));

  cardLayout->addLayout(cardHeader);
  cardLayout->addWidget(m_issueDetailsLabel);
  m_issueCard->hide();

  // 2. Quick Tags Toolbar & Enclosing Tag Breadcrumbs ("say the tags")
  m_tagBarWidget = new QFrame(this);
  m_tagBarWidget->setObjectName(QStringLiteral("htmlTagBar"));
  auto *tagBarLayout = new QHBoxLayout(m_tagBarWidget);
  tagBarLayout->setContentsMargins(8, 4, 8, 4);
  tagBarLayout->setSpacing(5);

  m_tagBarTitleLabel = new QLabel(QStringLiteral("AO3 Tags:"), m_tagBarWidget);
  m_tagBarTitleLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700;"));
  tagBarLayout->addWidget(m_tagBarTitleLabel);

  struct QuickTagDef {
    QString label;
    QString tag;
    QString defClass;
    QString tooltip;
  };
  const QList<QuickTagDef> quickTags = {
    {QStringLiteral("+ span"), QStringLiteral("span"), QStringLiteral("dialogue"), QStringLiteral("Insert inline <span> with Work Skin class (e.g. for character colors / styling)")},
    {QStringLiteral("+ div"), QStringLiteral("div"), QStringLiteral("card"), QStringLiteral("Insert <div> block container for cards, boxes, or custom layouts")},
    {QStringLiteral("+ details"), QStringLiteral("details"), QString(), QStringLiteral("Insert collapsible <details><summary> spoiler or translation note")},
    {QStringLiteral("+ quote"), QStringLiteral("blockquote"), QString(), QStringLiteral("Insert <blockquote> for letters, quotations, or excerpts")},
    {QStringLiteral("+ ruby"), QStringLiteral("ruby"), QString(), QStringLiteral("Insert <ruby><rt> furigana or pronunciation annotation")},
    {QStringLiteral("+ em"), QStringLiteral("em"), QString(), QStringLiteral("Wrap in <em> for emphasis (italics)")},
    {QStringLiteral("+ strong"), QStringLiteral("strong"), QString(), QStringLiteral("Wrap in <strong> for bold text")},
    {QStringLiteral("+ s"), QStringLiteral("s"), QString(), QStringLiteral("Wrap in <s> for strikethrough")},
    {QStringLiteral("+ sub"), QStringLiteral("sub"), QString(), QStringLiteral("Wrap in <sub> for subscript")},
    {QStringLiteral("+ sup"), QStringLiteral("sup"), QString(), QStringLiteral("Wrap in <sup> for superscript (e.g. footnotes)")},
    {QStringLiteral("+ code"), QStringLiteral("code"), QString(), QStringLiteral("Wrap in <code> for monospace text")},
    {QStringLiteral("+ hr"), QStringLiteral("hr"), QString(), QStringLiteral("Insert <hr /> scene break divider")},
    {QStringLiteral("+ a"), QStringLiteral("a"), QString(), QStringLiteral("Insert or wrap in <a href=\"...\"> link")},
  };

  for (const auto &def : quickTags) {
    auto *btn = new QPushButton(def.label, m_tagBarWidget);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(def.tooltip);
    connect(btn, &QPushButton::clicked, this, [this, tag = def.tag, cls = def.defClass]() {
      insertTag(tag, cls);
    });
    tagBarLayout->addWidget(btn);
    m_quickTagButtons.append(btn);
  }

  tagBarLayout->addStretch(1);

  m_tagBreadcrumbLabel = new QLabel(QStringLiteral("🏷️ Tags: (root document)"), m_tagBarWidget);
  m_tagBreadcrumbLabel->setObjectName(QStringLiteral("tagBreadcrumbLabel"));
  m_tagBreadcrumbLabel->setToolTip(QStringLiteral("Active HTML tag nesting at cursor position"));
  tagBarLayout->addWidget(m_tagBreadcrumbLabel);

  // 3. Code Editor & Minimap
  m_editor = new CodeEditor(this);
  m_editor->setSyntaxMode(CodeEditor::SyntaxMode::Html);
  QFont monoFont(QStringLiteral("Monospace"));
  monoFont.setStyleHint(QFont::Monospace);
  monoFont.setPointSize(10);
  m_editor->setFont(monoFont);
  m_editor->setPlaceholderText(QStringLiteral("AO3 HTML source…"));
  m_editor->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_editor, &QWidget::customContextMenuRequested, this, &HtmlSourcePane::showEditorContextMenu);

  new HtmlSyntaxHighlighter(m_editor->document());
  m_editor->installEventFilter(this);
  connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, &HtmlSourcePane::onCursorPositionChanged);

  // Minimap Scroll Preview sidebar
  auto *minimap = new EditorMinimap(this);
  minimap->setTargetEditor(m_editor);

  auto *editorRow = new QHBoxLayout();
  editorRow->setContentsMargins(0, 0, 0, 0);
  editorRow->setSpacing(0);
  editorRow->addWidget(m_editor, 1);
  editorRow->addWidget(minimap);

  // 4. Bottom Action Row
  m_copyAo3Button = new QPushButton(QStringLiteral("📋 Copy for AO3"), this);
  m_copyAo3Button->setObjectName(QStringLiteral("copyAo3Button"));
  m_copyAo3Button->setCursor(Qt::PointingHandCursor);
  m_copyAo3Button->setToolTip(QStringLiteral("Copy clean, sanitized HTML directly to clipboard ready to paste into AO3"));

  m_formatButton = new QPushButton(QStringLiteral("Format HTML"), this);
  m_formatButton->setObjectName(QStringLiteral("formatButton"));
  m_formatButton->setCursor(Qt::PointingHandCursor);
  m_formatButton->setToolTip(QStringLiteral("Format paragraph linebreaks to prevent one-liners"));

  m_applyButton = new QPushButton(QStringLiteral("Apply to Editor"), this);
  m_applyButton->setObjectName(QStringLiteral("applyButton"));
  m_applyButton->setCursor(Qt::PointingHandCursor);

  auto *buttonRow = new QHBoxLayout;
  buttonRow->setContentsMargins(8, 6, 8, 6);
  buttonRow->setSpacing(8);
  buttonRow->addStretch();
  buttonRow->addWidget(m_copyAo3Button);
  buttonRow->addWidget(m_formatButton);
  buttonRow->addWidget(m_applyButton);

  layout->addWidget(m_issueCard);
  layout->addWidget(m_tagBarWidget);
  layout->addLayout(editorRow, 1);
  layout->addLayout(buttonRow);

  // Linter timer (debounced at 400ms)
  m_linterTimer = new QTimer(this);
  m_linterTimer->setSingleShot(true);
  m_linterTimer->setInterval(400);
  connect(m_linterTimer, &QTimer::timeout, this, &HtmlSourcePane::runLinter);

  connect(m_copyAo3Button, &QPushButton::clicked, this, &HtmlSourcePane::copyAo3Html);
  connect(m_formatButton, &QPushButton::clicked, this, &HtmlSourcePane::formatHtml);
  connect(m_applyButton, &QPushButton::clicked, this, &HtmlSourcePane::onApplyClicked);
  connect(m_editor, &QPlainTextEdit::textChanged, this, &HtmlSourcePane::onTextChanged);

  updateTheme();
  connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
          [this](AppTheme) { updateTheme(); });
  connect(&ThemeManager::instance(), &ThemeManager::skinFlavorChanged, this,
          [this](SkinFlavor) { updateTheme(); });
}

void HtmlSourcePane::updateTheme() {
  const bool isDark = ThemeManager::instance().isDarkMode();
  if (m_editor) {
    m_editor->setStyleSheet(ThemeManager::instance().getCodeEditorStyleSheet());
  }

  const int rad = ThemeManager::instance().buttonRadius();

  if (m_issueCard) {
    m_issueCard->setStyleSheet(isDark ? QStringLiteral(
        "#htmlIssueCard { background-color: #2b2718; border-bottom: 1px solid #624c16; }"
        "QLabel { color: #ffb86c; }"
        "QPushButton { background: #3d351e; color: #ffb86c; border: 1px solid #624c16; border-radius: 4px; padding: 2px 8px; font-size: 10px; font-weight: 600; }"
        "QPushButton:hover { background: #4d4326; }"
    ) : QStringLiteral(
        "#htmlIssueCard { background-color: #fef3c7; border-bottom: 1px solid #fde68a; }"
        "QLabel { color: #92400e; }"
        "QPushButton { background: #fde68a; color: #92400e; border: 1px solid #fcd34d; border-radius: 4px; padding: 2px 8px; font-size: 10px; font-weight: 600; }"
        "QPushButton:hover { background: #fcd34d; }"
    ));
  }

  if (m_tagBarWidget) {
    m_tagBarWidget->setStyleSheet(QStringLiteral(
        "#htmlTagBar { background-color: %1; border-bottom: 1px solid %2; }"
    ).arg(ThemeManager::instance().cardBgColor().name(),
         ThemeManager::instance().borderColor().name()));
  }

  if (m_tagBarTitleLabel) {
    m_tagBarTitleLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: %1;")
        .arg(ThemeManager::instance().mutedTextColor().name()));
  }

  if (m_tagBreadcrumbLabel) {
    m_tagBreadcrumbLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 600; color: %1; font-family: monospace;")
        .arg(ThemeManager::instance().mutedTextColor().name()));
  }

  const QString tagBtnStyle = QStringLiteral(
      "QPushButton {"
      "  background-color: %1;"
      "  color: %2;"
      "  border: 1px solid %3;"
      "  border-radius: %4px;"
      "  padding: 2px 7px;"
      "  font-size: 11px;"
      "  font-weight: 600;"
      "}"
      "QPushButton:hover {"
      "  background-color: %5;"
      "  color: #ffffff;"
      "}"
  ).arg(
      isDark ? QStringLiteral("#2b303c") : QStringLiteral("#f1f5f9"),
      ThemeManager::instance().primaryTextColor().name(),
      ThemeManager::instance().borderColor().name(),
      QString::number(qMin(rad, 4)),
      ThemeManager::instance().accentColor().name()
  );

  for (auto *btn : m_quickTagButtons) {
    if (btn) btn->setStyleSheet(tagBtnStyle);
  }

  const QString actionBtnStyle = QStringLiteral(
      "QPushButton {"
      "  background-color: %1;"
      "  color: %2;"
      "  border: 1px solid %3;"
      "  border-radius: %4px;"
      "  padding: 6px 12px;"
      "  font-size: 11px;"
      "  font-weight: 600;"
      "}"
      "QPushButton:hover {"
      "  background-color: %5;"
      "}"
  ).arg(ThemeManager::instance().cardBgColor().name(),
       ThemeManager::instance().primaryTextColor().name(),
       ThemeManager::instance().borderColor().name(),
       QString::number(rad),
       ThemeManager::instance().surfaceBgColor().name());

  if (m_copyAo3Button) {
    m_copyAo3Button->setStyleSheet(actionBtnStyle);
  }

  if (m_formatButton) {
    m_formatButton->setStyleSheet(actionBtnStyle);
  }

  if (m_applyButton) {
    const QString accent = ThemeManager::instance().accentColor().name();
    m_applyButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: %1;"
        "  color: #ffffff;"
        "  border: none;"
        "  border-radius: %2px;"
        "  padding: 6px 14px;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "  background-color: %3;"
        "}"
    ).arg(accent, QString::number(rad), ThemeManager::instance().accentHoverColor().name()));
  }
}

void HtmlSourcePane::updateSettings() {
  if (m_editor) {
    m_editor->refreshHighlights();
  }
  runLinter();
}

QString HtmlSourcePane::html() const { return m_editor->toPlainText(); }

QString HtmlSourcePane::formatHtmlSource(const QString &rawHtml) {
  if (rawHtml.trimmed().isEmpty()) return rawHtml;

  QString html = rawHtml;
  html.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  html.replace(QStringLiteral("\r"), QStringLiteral("\n"));

  // 1. Insert two newlines between consecutive </p> and <p>
  static const QRegularExpression pBetweenRe(QStringLiteral(R"(<\/p>\s*<p(\s+[^>]*)?>)"), QRegularExpression::CaseInsensitiveOption);
  html.replace(pBetweenRe, QStringLiteral("</p>\n\n<p\\1>"));

  // 2. Insert two newlines between block elements
  static const QRegularExpression divCloseOpenRe(QStringLiteral(R"(<\/(div|blockquote|h[1-6])>\s*<(div|blockquote|h[1-6]|p)(\s+[^>]*)?>)"), QRegularExpression::CaseInsensitiveOption);
  html.replace(divCloseOpenRe, QStringLiteral("</\\1>\n\n<\\2\\3>"));

  static const QRegularExpression pDivRe(QStringLiteral(R"(<\/p>\s*<(div|blockquote|h[1-6])(\s+[^>]*)?>)"), QRegularExpression::CaseInsensitiveOption);
  html.replace(pDivRe, QStringLiteral("</p>\n\n<\\1\\2>"));

  static const QRegularExpression divPRe(QStringLiteral(R"(<\/(div|blockquote|h[1-6])>\s*<p(\s+[^>]*)?>)"), QRegularExpression::CaseInsensitiveOption);
  html.replace(divPRe, QStringLiteral("</\\1>\n\n<p\\2>"));

  // 3. Format lists
  static const QRegularExpression liRe(QStringLiteral(R"(<\/li>\s*<li(\s+[^>]*)?>)"), QRegularExpression::CaseInsensitiveOption);
  html.replace(liRe, QStringLiteral("</li>\n  <li\\1>"));

  static const QRegularExpression ulLiRe(QStringLiteral(R"(<(ul|ol)(\s+[^>]*)?>\s*<li(\s+[^>]*)?>)"), QRegularExpression::CaseInsensitiveOption);
  html.replace(ulLiRe, QStringLiteral("<\\1\\2>\n  <li\\3>"));

  static const QRegularExpression liUlCloseRe(QStringLiteral(R"(<\/li>\s*<\/(ul|ol)>)"), QRegularExpression::CaseInsensitiveOption);
  html.replace(liUlCloseRe, QStringLiteral("</li>\n</\\1>"));

  // 4. Format horizontal rules
  static const QRegularExpression hrRe(QStringLiteral(R"(\s*<hr\s*\/?>\s*)"), QRegularExpression::CaseInsensitiveOption);
  html.replace(hrRe, QStringLiteral("\n\n<hr />\n\n"));

  return html.trimmed();
}

void HtmlSourcePane::setHtml(const QString &html) {
  m_blockChanges = true;
  QSettings settings(QStringLiteral("Ensemble"), QStringLiteral("Ensemble"));
  const bool autoFormat = settings.value(QStringLiteral("editor/autoFormatHtml"), true).toBool();
  const QString processedHtml = autoFormat ? formatHtmlSource(html) : html;
  m_editor->setPlainText(processedHtml);
  m_blockChanges = false;
  m_editing = false;
  if (m_issueCard) m_issueCard->hide();
  m_editor->clearDiagnosticIssues();
  runLinter();
  onCursorPositionChanged();
}

void HtmlSourcePane::formatHtml() {
  const QString current = m_editor->toPlainText();
  const QString formatted = formatHtmlSource(current);
  if (formatted != current) {
    m_blockChanges = true;
    m_editor->setPlainText(formatted);
    m_blockChanges = false;
    m_editing = true;
    emit editingStarted();
    emit htmlChanged(formatted);
    runLinter();
    onCursorPositionChanged();
  }
}

void HtmlSourcePane::forceApply() {
  if (m_editing) {
    onApplyClicked();
  }
}

void HtmlSourcePane::onApplyClicked() {
  m_editing = false;
  emit applyRequested(m_editor->toPlainText());
  emit editingFinished();
}

void HtmlSourcePane::onTextChanged() {
  if (m_blockChanges)
    return;

  if (!m_editing) {
    m_editing = true;
    emit editingStarted();
  }

  emit htmlChanged(m_editor->toPlainText());
  m_linterTimer->start(); // restart debounce
  onCursorPositionChanged();
}

bool HtmlSourcePane::eventFilter(QObject *obj, QEvent *event) {
  if (obj == m_editor && event->type() == QEvent::KeyPress) {
    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->key() == Qt::Key_Tab &&
        keyEvent->modifiers() == Qt::NoModifier) {
      if (tryExpandAutoblock()) {
        return true; // consumed
      }
    }
  }
  return QWidget::eventFilter(obj, event);
}

bool HtmlSourcePane::tryExpandAutoblock() {
  QTextCursor cursor = m_editor->textCursor();
  QString line = cursor.block().text();
  int col = cursor.positionInBlock();

  int start = col;
  while (start > 0 && !line[start - 1].isSpace()) {
    --start;
  }
  if (start >= col)
    return false;

  QString abbr = line.mid(start, col - start);

  static const QRegularExpression abbrRe(
      QStringLiteral(R"(^([a-zA-Z0-9]+)(?:#([a-zA-Z0-9_-]+))?(?:\.([a-zA-Z0-9_.-]+))?$)"));
  QRegularExpressionMatch match = abbrRe.match(abbr);
  if (!match.hasMatch())
    return false;

  QString tagName = match.captured(1);
  QString idName = match.captured(2);
  QString rawClasses = match.captured(3);

  QString classAttr;
  if (!rawClasses.isEmpty()) {
    QStringList classList = rawClasses.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (!classList.isEmpty()) {
      classAttr =
          QStringLiteral(" class=\"%1\"").arg(classList.join(QLatin1Char(' ')));
    }
  }

  cursor.setPosition(cursor.block().position() + start);
  cursor.setPosition(cursor.block().position() + col, QTextCursor::KeepAnchor);

  static const QSet<QString> voidTags = {
      QStringLiteral("br"),    QStringLiteral("hr"),  QStringLiteral("img"),
      QStringLiteral("input"), QStringLiteral("col"),
  };

  QString expansion;
  if (voidTags.contains(tagName.toLower())) {
    expansion = QStringLiteral("<%1%2 />").arg(tagName, classAttr);
  } else {
    expansion = QStringLiteral("<%1%2>\n\n</%1>").arg(tagName, classAttr);
  }

  cursor.insertText(expansion);

  if (!voidTags.contains(tagName.toLower())) {
    cursor.movePosition(QTextCursor::Up, QTextCursor::MoveAnchor, 1);
    cursor.movePosition(QTextCursor::EndOfBlock);
  }
  m_editor->setTextCursor(cursor);

  return true;
}

// --- Quick Tags & Breadcrumbs ---

void HtmlSourcePane::insertTag(const QString &tag, const QString &defaultClass, const QString &defaultInner) {
  QTextCursor cursor = m_editor->textCursor();
  QString selected = cursor.selectedText();
  selected.replace(QChar(0x2029), QLatin1Char('\n'));

  QString classAttr;
  if (!defaultClass.isEmpty()) {
    classAttr = QStringLiteral(" class=\"%1\"").arg(defaultClass);
  }

  if (tag == QLatin1String("hr") || tag == QLatin1String("br")) {
    cursor.insertText(QStringLiteral("\n\n<%1 />\n\n").arg(tag));
    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
    return;
  }

  if (tag == QLatin1String("details")) {
    QString summary = QStringLiteral("Spoiler / Author Note");
    QString body = selected.isEmpty() ? (defaultInner.isEmpty() ? QStringLiteral("Hidden content here...") : defaultInner) : selected;
    QString snippet = QStringLiteral("<details%1>\n  <summary>%2</summary>\n  <p>%3</p>\n</details>\n").arg(classAttr, summary, body);
    cursor.insertText(snippet);
    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
    return;
  }

  if (tag == QLatin1String("ruby")) {
    QString base = selected.isEmpty() ? QStringLiteral("Base") : selected;
    QString snippet = QStringLiteral("<ruby>%1<rt>%2</rt></ruby>").arg(base, defaultInner.isEmpty() ? QStringLiteral("annotation") : defaultInner);
    cursor.insertText(snippet);
    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
    return;
  }

  if (tag == QLatin1String("div")) {
    QString body = selected.isEmpty() ? (defaultInner.isEmpty() ? QStringLiteral("Content here...") : defaultInner) : selected;
    QString snippet = QStringLiteral("<div%1>\n  %2\n</div>\n").arg(classAttr, body);
    cursor.insertText(snippet);
    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
    return;
  }

  if (tag == QLatin1String("blockquote")) {
    QString body = selected.isEmpty() ? (defaultInner.isEmpty() ? QStringLiteral("Quoted text...") : defaultInner) : selected;
    QString snippet = QStringLiteral("<blockquote>\n  <p>%1</p>\n</blockquote>\n").arg(body);
    cursor.insertText(snippet);
    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
    return;
  }

  if (tag == QLatin1String("a")) {
    QString body = selected.isEmpty() ? (defaultInner.isEmpty() ? QStringLiteral("link text") : defaultInner) : selected;
    QString snippet = QStringLiteral("<a href=\"#\">%1</a>").arg(body);
    cursor.insertText(snippet);
    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
    return;
  }

  // Inline tags: span, em, strong, s, sub, sup, code, etc.
  if (selected.isEmpty()) {
    int startPos = cursor.position();
    QString openTag = QStringLiteral("<%1%2>").arg(tag, classAttr);
    QString closeTag = QStringLiteral("</%1>").arg(tag);
    cursor.insertText(openTag + closeTag);
    cursor.setPosition(startPos + openTag.length());
    m_editor->setTextCursor(cursor);
  } else {
    QString snippet = QStringLiteral("<%1%2>%3</%1>").arg(tag, classAttr, selected);
    cursor.insertText(snippet);
    m_editor->setTextCursor(cursor);
  }
  m_editor->setFocus();
}

QStringList HtmlSourcePane::getTagPathAt(const QString &text, int cursorPos) {
  if (text.isEmpty() || cursorPos <= 0) {
    return {};
  }

  const int checkPos = qMin(cursorPos, static_cast<int>(text.length()));
  static const QRegularExpression tokenRe(QStringLiteral(R"(<!--[\s\S]*?-->|<(\/)?([a-zA-Z0-9:-]+)([^>]*)>)"));
  static const QRegularExpression classRe(QStringLiteral(R"re(\bclass\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))re"), QRegularExpression::CaseInsensitiveOption);
  static const QSet<QString> voidTags = {
    QStringLiteral("br"), QStringLiteral("hr"), QStringLiteral("img"),
    QStringLiteral("col"), QStringLiteral("input"), QStringLiteral("wbr")
  };

  QList<QString> tagStack;
  QRegularExpressionMatchIterator it = tokenRe.globalMatch(text);

  while (it.hasNext()) {
    QRegularExpressionMatch m = it.next();
    int tokenStart = m.capturedStart();
    if (tokenStart >= checkPos) {
      break;
    }

    QString fullMatch = m.captured(0);
    if (fullMatch.startsWith(QStringLiteral("<!--"))) {
      continue; // Skip comments
    }

    bool isClosing = !m.captured(1).isEmpty();
    QString tagName = m.captured(2).toLower();
    QString attrs = m.captured(3);

    bool isSelfClosing = attrs.trimmed().endsWith(QLatin1Char('/')) || voidTags.contains(tagName);

    if (isClosing) {
      for (int i = tagStack.size() - 1; i >= 0; --i) {
        QString existing = tagStack[i];
        QString existingTag = existing.section(QLatin1Char('.'), 0, 0);
        if (existingTag == tagName) {
          while (tagStack.size() > i) {
            tagStack.removeLast();
          }
          break;
        }
      }
    } else if (!isSelfClosing) {
      QString displayTag = tagName;
      QRegularExpressionMatch classMatch = classRe.match(attrs);
      if (classMatch.hasMatch()) {
        QString cls = classMatch.captured(1);
        if (cls.isEmpty()) cls = classMatch.captured(2);
        if (cls.isEmpty()) cls = classMatch.captured(3);
        QString firstClass = cls.split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0);
        if (!firstClass.isEmpty()) {
          displayTag += QStringLiteral(".%1").arg(firstClass);
        }
      }
      tagStack.append(displayTag);
    }
  }

  return tagStack;
}

void HtmlSourcePane::onCursorPositionChanged() {
  if (!m_editor || !m_tagBreadcrumbLabel) return;
  int pos = m_editor->textCursor().position();
  QStringList path = getTagPathAt(m_editor->toPlainText(), pos);
  if (path.isEmpty()) {
    m_tagBreadcrumbLabel->setText(QStringLiteral("🏷️ Tags: (root document)"));
    m_tagBreadcrumbLabel->setToolTip(QStringLiteral("Cursor is at root document level"));
  } else {
    QString breadcrumbs;
    for (int i = 0; i < path.size(); ++i) {
      if (i > 0) breadcrumbs += QStringLiteral(" › ");
      breadcrumbs += QStringLiteral("<%1>").arg(path[i]);
    }
    m_tagBreadcrumbLabel->setText(QStringLiteral("🏷️ Tags: %1").arg(breadcrumbs));
    m_tagBreadcrumbLabel->setToolTip(QStringLiteral("Active HTML tag nesting at cursor:\n%1").arg(breadcrumbs));
  }
}

// --- Copy for AO3 ---

void HtmlSourcePane::copyAo3Html() {
  const QString raw = m_editor->toPlainText();
  if (raw.trimmed().isEmpty()) {
    emit statusMessageRequested(QStringLiteral("HTML editor is empty. Nothing to copy."));
    return;
  }
  const QString clean = Ao3HtmlSanitizer::sanitize(raw);
  QApplication::clipboard()->setText(clean);
  if (m_copyAo3Button) {
    m_copyAo3Button->setText(QStringLiteral("✓ Copied for AO3!"));
    QTimer::singleShot(2500, this, [this]() {
      if (m_copyAo3Button) {
        m_copyAo3Button->setText(QStringLiteral("📋 Copy for AO3"));
      }
    });
  }
  emit statusMessageRequested(QStringLiteral("Clean AO3 HTML copied to clipboard."));
}

void HtmlSourcePane::showEditorContextMenu(const QPoint &pos) {
  QMenu *menu = m_editor->createStandardContextMenu();
  menu->addSeparator();

  auto *copyAo3Action = menu->addAction(QStringLiteral("📋 Copy Clean HTML for AO3"));
  copyAo3Action->setToolTip(QStringLiteral("Sanitizes and copies full HTML to clipboard ready for AO3"));
  connect(copyAo3Action, &QAction::triggered, this, &HtmlSourcePane::copyAo3Html);

  if (m_editor->textCursor().hasSelection()) {
    auto *copySelAction = menu->addAction(QStringLiteral("📋 Copy Selected HTML for AO3"));
    connect(copySelAction, &QAction::triggered, this, [this]() {
      QString sel = m_editor->textCursor().selectedText();
      sel.replace(QChar(0x2029), QLatin1Char('\n'));
      QString clean = Ao3HtmlSanitizer::sanitize(sel);
      QApplication::clipboard()->setText(clean);
      emit statusMessageRequested(QStringLiteral("Selected HTML copied for AO3."));
    });
  }

  auto *tagMenu = menu->addMenu(QStringLiteral("🏷️ Insert AO3 Tag"));
  tagMenu->addAction(QStringLiteral("<span> (Work Skin class / dialogue)"), this, [this]() { insertTag(QStringLiteral("span"), QStringLiteral("dialogue")); });
  tagMenu->addAction(QStringLiteral("<div> (Card / box container)"), this, [this]() { insertTag(QStringLiteral("div"), QStringLiteral("card")); });
  tagMenu->addAction(QStringLiteral("<details> (Collapsible spoiler / note)"), this, [this]() { insertTag(QStringLiteral("details")); });
  tagMenu->addAction(QStringLiteral("<blockquote> (Block quote / letter)"), this, [this]() { insertTag(QStringLiteral("blockquote")); });
  tagMenu->addAction(QStringLiteral("<ruby> (Furigana / pronunciation)"), this, [this]() { insertTag(QStringLiteral("ruby")); });
  tagMenu->addAction(QStringLiteral("<em> (Emphasis / italic)"), this, [this]() { insertTag(QStringLiteral("em")); });
  tagMenu->addAction(QStringLiteral("<strong> (Strong / bold)"), this, [this]() { insertTag(QStringLiteral("strong")); });
  tagMenu->addAction(QStringLiteral("<s> (Strikethrough)"), this, [this]() { insertTag(QStringLiteral("s")); });
  tagMenu->addAction(QStringLiteral("<sub> (Subscript)"), this, [this]() { insertTag(QStringLiteral("sub")); });
  tagMenu->addAction(QStringLiteral("<sup> (Superscript)"), this, [this]() { insertTag(QStringLiteral("sup")); });
  tagMenu->addAction(QStringLiteral("<code> (Monospace code)"), this, [this]() { insertTag(QStringLiteral("code")); });
  tagMenu->addAction(QStringLiteral("<hr /> (Scene break divider)"), this, [this]() { insertTag(QStringLiteral("hr")); });
  tagMenu->addAction(QStringLiteral("<a> (Hyperlink)"), this, [this]() { insertTag(QStringLiteral("a")); });

  menu->exec(m_editor->mapToGlobal(pos));
  delete menu;
}

void HtmlSourcePane::toggleIssueDetails() {
  if (!m_issueDetailsLabel) return;
  m_detailsExpanded = !m_detailsExpanded;
  m_issueDetailsLabel->setVisible(m_detailsExpanded);
  m_toggleDetailsBtn->setText(m_detailsExpanded ? QStringLiteral("Hide Details ▴") : QStringLiteral("Show Details ▾"));
}

// --- Linter with Diagnostic Highlighting & Collapsible Count Summary ---

void HtmlSourcePane::runLinter() {
  const QString text = m_editor->toPlainText();
  if (text.trimmed().isEmpty()) {
    m_issueCard->hide();
    m_issueDetailsLabel->hide();
    m_editor->clearDiagnosticIssues();
    return;
  }

  QList<CodeDiagnosticIssue> issues;
  int errorCount = 0;
  int warningCount = 0;
  QStringList detailLines;

  // 1. Inline style attributes
  static const QRegularExpression styleAttrRe(
      QStringLiteral(R"(\bstyle\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>]+))"),
      QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator styleIt = styleAttrRe.globalMatch(text);
  int styleCount = 0;
  while (styleIt.hasNext()) {
    QRegularExpressionMatch m = styleIt.next();
    CodeDiagnosticIssue issue;
    issue.startPos = m.capturedStart();
    issue.length = m.capturedLength();
    issue.isError = false; // warning
    issue.message = QStringLiteral("Inline style= attributes are disallowed by AO3. Use Work Skin CSS classes instead.");
    issues.append(issue);
    styleCount++;
  }
  if (styleCount > 0) {
    warningCount += styleCount;
    detailLines.append(QStringLiteral("• <b>%1 inline style= attribute%2:</b> Disallowed by AO3; use Work Skin CSS classes instead.")
                           .arg(styleCount)
                           .arg(styleCount == 1 ? QString() : QStringLiteral("s")));
  }

  // 2. Disallowed tags
  static const QRegularExpression tagRe(QStringLiteral(R"(<\/?([a-zA-Z][\w:-]*))"));
  const QSet<QString> allowedTags = Ao3TagWhitelist::allowedTags();
  QMap<QString, int> disallowedTagCounts;

  QRegularExpressionMatchIterator tagIt = tagRe.globalMatch(text);
  while (tagIt.hasNext()) {
    QRegularExpressionMatch m = tagIt.next();
    QString tagName = m.captured(1).toLower();
    if (!allowedTags.contains(tagName)) {
      disallowedTagCounts[tagName]++;
      CodeDiagnosticIssue issue;
      issue.startPos = m.capturedStart();
      issue.length = m.capturedLength();
      issue.isError = true;
      issue.message = QStringLiteral("<%1> tags are blocked by AO3 security whitelist.").arg(tagName);
      issues.append(issue);
      errorCount++;
    }
  }
  for (auto it = disallowedTagCounts.constBegin(); it != disallowedTagCounts.constEnd(); ++it) {
    detailLines.append(QStringLiteral("• <b>Disallowed tag &lt;%1&gt; (%2x):</b> Blocked by AO3 security whitelist.")
                           .arg(it.key())
                           .arg(it.value()));
  }

  // 2b: <span> tags tip - valid as inline text highlighter, tip diagnostic for hover
  static const QRegularExpression spanTagRe(
      QStringLiteral(R"(<\/?span\b[^>]*>)"),
      QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator spanIt = spanTagRe.globalMatch(text);
  while (spanIt.hasNext()) {
    QRegularExpressionMatch m = spanIt.next();
    CodeDiagnosticIssue issue;
    issue.startPos = m.capturedStart();
    issue.length = m.capturedLength();
    issue.isError = false; // Informational tip/warning
    issue.message = QStringLiteral("Tip: <span> tags should ONLY be used as inline text highlighters. Using <span> like a card layout or container will break the layout; use <div> instead.");
    issues.append(issue);
  }

  // 3. Mismatched tags using QXmlStreamReader
  const QString wrapped = QStringLiteral("<root>%1</root>").arg(text);
  QXmlStreamReader reader(wrapped);
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.hasError()) {
      const QString err = reader.errorString();
      const qint64 line = qMax<qint64>(1, reader.lineNumber() - 1);
      errorCount++;
      detailLines.append(QStringLiteral("• <b>HTML Syntax Error near line %1:</b> %2").arg(line).arg(err.toHtmlEscaped()));

      // Attempt to highlight the line
      QTextBlock block = m_editor->document()->findBlockByLineNumber(qMax(0, static_cast<int>(line) - 1));
      if (block.isValid()) {
        CodeDiagnosticIssue issue;
        issue.startPos = block.position();
        issue.length = qMax(1, block.length() - 1);
        issue.isError = true;
        issue.message = QStringLiteral("HTML Syntax Error: %1").arg(err);
        issues.append(issue);
      }
      break;
    }
  }

  // Apply highlights to editor
  m_editor->setDiagnosticIssues(issues);

  const int totalIssues = errorCount + warningCount;
  if (totalIssues == 0) {
    m_issueCard->hide();
    m_issueDetailsLabel->hide();
    m_detailsExpanded = false;
    m_toggleDetailsBtn->setText(QStringLiteral("Show Details ▾"));
  } else {
    QString summaryText = QStringLiteral("⚠️ %1 HTML issue%2 detected")
                              .arg(totalIssues)
                              .arg(totalIssues == 1 ? QString() : QStringLiteral("s"));
    if (errorCount > 0 && warningCount > 0) {
      summaryText += QStringLiteral(" (%1 error%2, %3 warning%4)")
                         .arg(errorCount)
                         .arg(errorCount == 1 ? QString() : QStringLiteral("s"))
                         .arg(warningCount)
                         .arg(warningCount == 1 ? QString() : QStringLiteral("s"));
    } else if (errorCount > 0) {
      summaryText = QStringLiteral("❌ %1 HTML error%2 detected")
                        .arg(errorCount)
                        .arg(errorCount == 1 ? QString() : QStringLiteral("s"));
    } else {
      summaryText = QStringLiteral("⚠️ %1 HTML warning%2 detected")
                        .arg(warningCount)
                        .arg(warningCount == 1 ? QString() : QStringLiteral("s"));
    }
    m_issueSummaryLabel->setText(summaryText);
    m_issueDetailsLabel->setText(detailLines.join(QStringLiteral("<br>")));
    m_issueCard->show();
  }
}
