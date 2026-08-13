#include "Ao3HtmlImporter.h"

#include "Ao3HtmlSanitizer.h"

#include <QAbstractTextDocumentLayout>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFormat>
#include <QTextFragment>

// Must match the property ID used in EditorPane.cpp and Ao3HtmlExporter.cpp
static constexpr int CssClassProperty = QTextFormat::Property::UserProperty + 1;

namespace {

// Marker prefix for encoding CSS classes into font-family so Qt preserves them
// through setHtml(). We use a distinctive prefix so we can find and remove it.
static const QString classMarkerPrefix = QStringLiteral("ao3class__");

// Pre-process HTML to encode class= attributes as font-family markers.
// Qt's setHtml() will preserve font-family but drops class attributes.
// e.g. <span class="flashback"> becomes
//      <span class="flashback" style="font-family: 'ao3class__flashback';">
QString encodeClassesAsMarkers(const QString &html) {
  // Match elements with a class attribute
  static const QRegularExpression classAttrRe(
      R"re((<[a-zA-Z][\w:-]*\b[^>]*?)\bclass\s*=\s*"([^"]+)"([^>]*>))re",
      QRegularExpression::CaseInsensitiveOption);

  QString result = html;
  // We need to work backwards to preserve offsets
  QList<QRegularExpressionMatch> matches;
  QRegularExpressionMatchIterator it = classAttrRe.globalMatch(result);
  while (it.hasNext())
    matches.prepend(it.next());

  for (const QRegularExpressionMatch &m : matches) {
    QString className = m.captured(2).trimmed();
    if (className.isEmpty())
      continue;

    // Build marker font-family
    const QString markerFont =
        classMarkerPrefix +
        className.replace(QLatin1Char(' '), QLatin1Char('_'));

    // Check if element already has a style attribute
    const QString fullMatch = m.captured(0);
    QString replacement;
    if (fullMatch.contains(QStringLiteral("style="))) {
      // Append to existing style
      replacement = fullMatch;
      static const QRegularExpression styleRe(R"(style\s*=\s*")");
      replacement.replace(
          styleRe,
          QStringLiteral("style=\"font-family: '%1'; ").arg(markerFont));
    } else {
      // Add new style attribute before the closing >
      replacement =
          m.captured(1) + QStringLiteral("class=\"%1\"").arg(className) +
          QStringLiteral(" style=\"font-family: '%1';\"").arg(markerFont) +
          m.captured(3);
    }

    result.replace(m.capturedStart(), m.capturedLength(), replacement);
  }

  return result;
}

QString cleanHtmlEntities(const QString &html) {
  QString cleaned = html;
  cleaned.replace(QStringLiteral("&amp;quot;"), QStringLiteral("\""));
  cleaned.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
  cleaned.replace(QStringLiteral("&amp;apos;"), QStringLiteral("'"));
  cleaned.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
  cleaned.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
  cleaned.replace(QStringLiteral("&amp;lt;"), QStringLiteral("<"));
  cleaned.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
  cleaned.replace(QStringLiteral("&amp;gt;"), QStringLiteral(">"));
  cleaned.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
  return cleaned;
}

} // namespace

void Ao3HtmlImporter::importHtml(QTextEdit *editor, const QString &rawHtml) {
  if (!editor)
    return;

  QString cleanHtml = rawHtml;
  cleanHtml.remove(QRegularExpression(QStringLiteral(R"(<!--[\s\S]*?-->)")));

  // 1. Sanitize the HTML first to ensure valid markup
  const QString sanitized = Ao3HtmlSanitizer::sanitize(cleanHtml);

  // 2. Pre-process HTML to encode class= attributes as font-family markers
  const QString markedHtml = encodeClassesAsMarkers(sanitized);

  editor->blockSignals(true);
  editor->document()->clear();
  editor->document()->setHtml(markedHtml);

  // Post-process: find font-family markers and convert to CssClassProperty
  for (QTextBlock block = editor->document()->begin(); block.isValid();
       block = block.next()) {
    const QTextBlockFormat blockFmt = block.blockFormat();

    // Normalize heading display: Qt maps h tags reasonably via setHtml.
    if (blockFmt.headingLevel() > 0) {
      QTextCharFormat charFmt;
      charFmt.setFontWeight(QFont::Bold);
      QTextCursor cursor(block);
      cursor.select(QTextCursor::BlockUnderCursor);
      cursor.mergeCharFormat(charFmt);
    }
    if (blockFmt.intProperty(QTextFormat::BlockQuoteLevel) > 0) {
      QTextCharFormat charFmt;
      charFmt.setFontItalic(true);
      QTextCursor cursor(block);
      cursor.select(QTextCursor::BlockUnderCursor);
      cursor.mergeCharFormat(charFmt);
    }

    // Scan fragments for class markers in font-family
    for (QTextBlock::iterator fragIt = block.begin(); !fragIt.atEnd();
         ++fragIt) {
      const QTextFragment fragment = fragIt.fragment();
      if (!fragment.isValid())
        continue;

      QTextCharFormat fmt = fragment.charFormat();
      const QStringList families = fmt.fontFamilies().toStringList();
      QString foundClass;

      for (const QString &family : families) {
        if (family.startsWith(classMarkerPrefix)) {
          foundClass = family.mid(classMarkerPrefix.length());
          foundClass.replace(QLatin1Char('_'), QLatin1Char(' '));
          break;
        }
      }

      if (!foundClass.isEmpty()) {
        // Set the CssClassProperty, visual skin highlight, and remove marker
        // font
        QTextCursor cursor(editor->document());
        cursor.setPosition(fragment.position());
        cursor.setPosition(fragment.position() + fragment.length(),
                           QTextCursor::KeepAnchor);

        QTextCharFormat newFmt;
        newFmt.setProperty(CssClassProperty, foundClass);
        newFmt.setBackground(QColor(40, 42, 54));   // #282a36 dark badge bg
        newFmt.setForeground(QColor(80, 250, 123)); // #50fa7b emerald text
        newFmt.setUnderlineStyle(QTextCharFormat::DashUnderline);
        newFmt.setUnderlineColor(QColor(139, 233, 253)); // #8be9fd cyan dash

        // Remove the marker from font families
        QStringList cleanFamilies;
        for (const QString &f : families) {
          if (!f.startsWith(classMarkerPrefix))
            cleanFamilies << f;
        }
        if (!cleanFamilies.isEmpty())
          newFmt.setFontFamilies(cleanFamilies);

        cursor.mergeCharFormat(newFmt);
      }
    }
  }
  editor->blockSignals(false);

  if (editor->document()) {
    editor->document()->markContentsDirty(0,
                                          editor->document()->characterCount());
  }
  if (editor->viewport()) {
    editor->viewport()->update();
  }
}
