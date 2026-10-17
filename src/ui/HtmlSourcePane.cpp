#include "HtmlSourcePane.h"
#include "CodeEditor.h"
#include "EditorMinimap.h"
#include "HtmlSyntaxHighlighter.h"
#include "themes/ThemeManager.h"

#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
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

  // Linter warning label (hidden by default)
  m_linterWarning = new QLabel(this);
  m_linterWarning->setWordWrap(true);
  m_linterWarning->hide();

  m_editor = new CodeEditor(this);
  m_editor->setSyntaxMode(CodeEditor::SyntaxMode::Html);
  QFont monoFont(QStringLiteral("Monospace"));
  monoFont.setStyleHint(QFont::Monospace);
  monoFont.setPointSize(10);
  m_editor->setFont(monoFont);
  m_editor->setPlaceholderText(QStringLiteral("AO3 HTML source…"));

  new HtmlSyntaxHighlighter(m_editor->document());
  m_editor->installEventFilter(this);

  // Minimap Scroll Preview sidebar
  auto *minimap = new EditorMinimap(this);
  minimap->setTargetEditor(m_editor);

  auto *editorRow = new QHBoxLayout();
  editorRow->setContentsMargins(0, 0, 0, 0);
  editorRow->setSpacing(0);
  editorRow->addWidget(m_editor, 1);
  editorRow->addWidget(minimap);

  m_formatButton = new QPushButton(QStringLiteral("Format HTML"), this);
  m_formatButton->setCursor(Qt::PointingHandCursor);
  m_formatButton->setToolTip(QStringLiteral("Format paragraph linebreaks to prevent one-liners"));

  m_applyButton = new QPushButton(QStringLiteral("Apply to Editor"), this);
  m_applyButton->setCursor(Qt::PointingHandCursor);

  auto *buttonRow = new QHBoxLayout;
  buttonRow->setContentsMargins(8, 4, 8, 4);
  buttonRow->setSpacing(8);
  buttonRow->addStretch();
  buttonRow->addWidget(m_formatButton);
  buttonRow->addWidget(m_applyButton);

  layout->addWidget(m_linterWarning);
  layout->addLayout(editorRow, 1);
  layout->addLayout(buttonRow);

  // Linter timer (debounced at 400ms)
  m_linterTimer = new QTimer(this);
  m_linterTimer->setSingleShot(true);
  m_linterTimer->setInterval(400);
  connect(m_linterTimer, &QTimer::timeout, this, &HtmlSourcePane::runLinter);

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

  if (m_formatButton) {
    m_formatButton->setStyleSheet(QStringLiteral(
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
         ThemeManager::instance().surfaceBgColor().name()));
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

  if (m_linterWarning) {
    m_linterWarning->setStyleSheet(
        isDark
            ? QStringLiteral("QLabel { background: #2b2718; color: #ffb86c; "
                             "padding: 6px 10px; border-bottom: 1px solid "
                             "#624c16; font-size: 11px; font-weight: 600; }")
            : QStringLiteral("QLabel { background: #fef3c7; color: #b45309; "
                             "padding: 6px 10px; border-bottom: 1px solid "
                             "#fde68a; font-size: 11px; font-weight: 600; }"));
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
  static const QRegularExpression pBetweenRe(R"(<\/p>\s*<p(\s+[^>]*)?>)", QRegularExpression::CaseInsensitiveOption);
  html.replace(pBetweenRe, QStringLiteral("</p>\n\n<p\\1>"));

  // 2. Insert two newlines between block elements
  static const QRegularExpression divCloseOpenRe(R"(<\/(div|blockquote|h[1-6])>\s*<(div|blockquote|h[1-6]|p)(\s+[^>]*)?>)", QRegularExpression::CaseInsensitiveOption);
  html.replace(divCloseOpenRe, QStringLiteral("</\\1>\n\n<\\2\\3>"));

  static const QRegularExpression pDivRe(R"(<\/p>\s*<(div|blockquote|h[1-6])(\s+[^>]*)?>)", QRegularExpression::CaseInsensitiveOption);
  html.replace(pDivRe, QStringLiteral("</p>\n\n<\\1\\2>"));

  static const QRegularExpression divPRe(R"(<\/(div|blockquote|h[1-6])>\s*<p(\s+[^>]*)?>)", QRegularExpression::CaseInsensitiveOption);
  html.replace(divPRe, QStringLiteral("</\\1>\n\n<p\\2>"));

  // 3. Format lists
  static const QRegularExpression liRe(R"(<\/li>\s*<li(\s+[^>]*)?>)", QRegularExpression::CaseInsensitiveOption);
  html.replace(liRe, QStringLiteral("</li>\n  <li\\1>"));

  static const QRegularExpression ulLiRe(R"(<(ul|ol)(\s+[^>]*)?>\s*<li(\s+[^>]*)?>)", QRegularExpression::CaseInsensitiveOption);
  html.replace(ulLiRe, QStringLiteral("<\\1\\2>\n  <li\\3>"));

  static const QRegularExpression liUlCloseRe(R"(<\/li>\s*<\/(ul|ol)>)", QRegularExpression::CaseInsensitiveOption);
  html.replace(liUlCloseRe, QStringLiteral("</li>\n</\\1>"));

  // 4. Format horizontal rules
  static const QRegularExpression hrRe(R"(\s*<hr\s*\/?>\s*)", QRegularExpression::CaseInsensitiveOption);
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
  m_linterWarning->hide();
  m_editor->clearDiagnosticIssues();
  runLinter();
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
      R"(^([a-zA-Z0-9]+)(?:#([a-zA-Z0-9_-]+))?(?:\.([a-zA-Z0-9_.-]+))?$)");
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

// --- Linter with Diagnostic Highlighting & Hover Tooltips ---

void HtmlSourcePane::runLinter() {
  const QString text = m_editor->toPlainText();
  if (text.trimmed().isEmpty()) {
    m_linterWarning->hide();
    m_editor->clearDiagnosticIssues();
    return;
  }

  QStringList warnings;
  QList<CodeDiagnosticIssue> issues;

  // Check 1: Inline style attributes (not allowed on AO3)
  static const QRegularExpression styleAttrRe(
      R"(\bstyle\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>]+))",
      QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator styleIt = styleAttrRe.globalMatch(text);
  while (styleIt.hasNext()) {
    QRegularExpressionMatch m = styleIt.next();
    CodeDiagnosticIssue issue;
    issue.startPos = m.capturedStart();
    issue.length = m.capturedLength();
    issue.isError = false; // warning
    issue.message = QStringLiteral("Inline style= attributes are disallowed by AO3. Use Work Skin CSS classes instead.");
    issues.append(issue);
  }
  if (!issues.isEmpty()) {
    warnings << QStringLiteral("⚠ Inline style= attributes detected (disallowed on AO3).");
  }

  // Check 2: Disallowed tags (script, style, iframe, form, input, object, embed)
  static const QRegularExpression disallowedTagRe(
      R"(<\/?(script|style|iframe|form|input|object|embed|applet)\b[^>]*>)",
      QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator disIt = disallowedTagRe.globalMatch(text);
  while (disIt.hasNext()) {
    QRegularExpressionMatch m = disIt.next();
    CodeDiagnosticIssue issue;
    issue.startPos = m.capturedStart();
    issue.length = m.capturedLength();
    issue.isError = true;
    issue.message = QStringLiteral("<%1> tags are blocked by AO3 security whitelist.").arg(m.captured(1).toLower());
    issues.append(issue);
    warnings << QStringLiteral("❌ Disallowed tag <%1> detected.").arg(m.captured(1).toLower());
  }

  // Check 2b: <span> tags - even though AO3 accepts <span>, it breaks rich text styling and editing in Ensemble
  static const QRegularExpression spanTagRe(
      R"(<\/?span\b[^>]*>)",
      QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatchIterator spanIt = spanTagRe.globalMatch(text);
  while (spanIt.hasNext()) {
    QRegularExpressionMatch m = spanIt.next();
    CodeDiagnosticIssue issue;
    issue.startPos = m.capturedStart();
    issue.length = m.capturedLength();
    issue.isError = true;
    issue.message = QStringLiteral("<span> tags break styling and rich text editing in Ensemble. Even though AO3 accepts <span>, avoid using it; use semantic tags (e.g. <b>, <em>, <strong>, <i>, <blockquote>, <p>, <div>) instead.");
    issues.append(issue);
    warnings << QStringLiteral("❌ <span> tag detected (breaks styling in Ensemble; avoid using).");
  }

  // Check 3: Mismatched tags using QXmlStreamReader
  const QString wrapped = QStringLiteral("<root>%1</root>").arg(text);
  QXmlStreamReader reader(wrapped);
  while (!reader.atEnd()) {
    reader.readNext();
    if (reader.hasError()) {
      const QString err = reader.errorString();
      const qint64 line = reader.lineNumber() - 1;
      warnings << QStringLiteral("⚠ HTML parse error near line %1: %2").arg(line).arg(err);

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

  if (warnings.isEmpty()) {
    m_linterWarning->hide();
  } else {
    m_linterWarning->setText(warnings.join(QStringLiteral("  |  ")));
    m_linterWarning->show();
  }
}
