#include "Ao3HtmlExporter.h"

#include "Ao3HtmlSanitizer.h"

#include <QFont>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextFragment>
#include <QTextList>
#include <QTextListFormat>

// Must match the property ID used in EditorPane.cpp
static constexpr int CssClassProperty = QTextFormat::Property::UserProperty + 1;

namespace {

QString escapeHtml(const QString &text)
{
    QString out;
    out.reserve(text.size());
    int i = 0;
    int len = text.size();
    static const QRegularExpression entityRe(R"(^&([a-zA-Z0-9]+|#[0-9]+|#x[a-fA-F0-9]+);)");
    while (i < len) {
        QChar c = text[i];
        if (c == '&') {
            // Check if it's a valid entity like &quot;, &amp;, &lt;, &gt;, or numeric entities
            QRegularExpressionMatch m = entityRe.match(text, i);
            if (m.hasMatch() && m.capturedStart() == i) {
                int matchLen = m.capturedLength();
                out += text.mid(i, matchLen);
                i += matchLen;
                continue;
            } else {
                out += QStringLiteral("&amp;");
            }
        } else if (c == '<') {
            out += QStringLiteral("&lt;");
        } else if (c == '>') {
            out += QStringLiteral("&gt;");
        } else {
            out += c;
        }
        i++;
    }
    return out;
}

struct InlineStyle {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool code = false;
    bool superscript = false;
    bool subscript = false;
    QString link;
    QString cssClass;
};

InlineStyle styleFromFormat(const QTextCharFormat &fmt)
{
    InlineStyle s;
    s.bold = fmt.fontWeight() >= QFont::Bold;
    s.italic = fmt.fontItalic();
    s.underline = fmt.fontUnderline();
    s.strike = fmt.fontStrikeOut();
    s.superscript = fmt.verticalAlignment() == QTextCharFormat::AlignSuperScript;
    s.subscript = fmt.verticalAlignment() == QTextCharFormat::AlignSubScript;
    if (fmt.isAnchor())
        s.link = fmt.anchorHref();
    const QStringList families = fmt.fontFamilies().toStringList();
    if (families.join(QString()).contains(QStringLiteral("monospace"), Qt::CaseInsensitive)
        || fmt.fontFixedPitch())
        s.code = true;

    // Read CSS class from custom property
    const QVariant classProp = fmt.property(CssClassProperty);
    if (classProp.isValid() && !classProp.toString().isEmpty())
        s.cssClass = classProp.toString();

    return s;
}

QString wrapInline(const QString &text, const InlineStyle &style)
{
    QString out = escapeHtml(text);
    if (out.isEmpty())
        return out;

    if (style.code)
        out = QStringLiteral("<code>%1</code>").arg(out);
    if (style.superscript)
        out = QStringLiteral("<sup>%1</sup>").arg(out);
    if (style.subscript)
        out = QStringLiteral("<sub>%1</sub>").arg(out);
    if (style.strike)
        out = QStringLiteral("<s>%1</s>").arg(out);
    if (style.underline)
        out = QStringLiteral("<u>%1</u>").arg(out);
    if (style.italic)
        out = QStringLiteral("<em>%1</em>").arg(out);
    if (style.bold)
        out = QStringLiteral("<strong>%1</strong>").arg(out);
    if (!style.link.isEmpty())
        out = QStringLiteral("<a href=\"%1\">%2</a>").arg(escapeHtml(style.link), out);

    // Wrap in a span with the CSS class if one is set
    if (!style.cssClass.isEmpty())
        out = QStringLiteral("<span class=\"%1\">%2</span>").arg(escapeHtml(style.cssClass), out);

    return out;
}

QString exportBlockInline(const QTextBlock &block)
{
    QString html;
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid())
            continue;
        html += wrapInline(fragment.text(), styleFromFormat(fragment.charFormat()));
    }
    return html;
}

QString headingTag(int level)
{
    level = qBound(1, level, 6);
    return QStringLiteral("h%1").arg(level);
}

QString blockClassAttr(const QTextBlockFormat &fmt)
{
    const QVariant classProp = fmt.property(CssClassProperty);
    if (classProp.isValid() && !classProp.toString().isEmpty())
        return QStringLiteral(" class=\"%1\"").arg(escapeHtml(classProp.toString()));
    return QString();
}

QString exportBlock(const QTextBlock &block, const QTextDocument *document)
{
    const QTextBlockFormat fmt = block.blockFormat();
    const QString classAttr = blockClassAttr(fmt);

    if (block.textList()) {
        // Handled at list level.
        return {};
    }

    if (fmt.hasProperty(QTextFormat::BlockCodeLanguage)
        || block.blockFormat().nonBreakableLines()) {
        const QString text = block.text();
        if (text.isEmpty())
            return QStringLiteral("<pre></pre>");
        return QStringLiteral("<pre>%1</pre>").arg(escapeHtml(text));
    }

    const int headingLevel = fmt.headingLevel();
    if (headingLevel > 0) {
        const QString tag = headingTag(headingLevel);
        const QString inner = exportBlockInline(block);
        if (inner.isEmpty())
            return QStringLiteral("<%1%2></%1>").arg(tag, classAttr);
        return QStringLiteral("<%1%2>%3</%1>").arg(tag, classAttr, inner);
    }

    if (fmt.intProperty(QTextFormat::BlockQuoteLevel) > 0) {
        const QString inner = exportBlockInline(block);
        if (inner.isEmpty())
            return QStringLiteral("<blockquote>\n  <p%1></p>\n</blockquote>").arg(classAttr);
        return QStringLiteral("<blockquote>\n  <p%1>%2</p>\n</blockquote>").arg(classAttr, inner);
    }

    const QString inner = exportBlockInline(block);
    if (inner.isEmpty())
        return QStringLiteral("<p%1></p>").arg(classAttr);
    return QStringLiteral("<p%1>%2</p>").arg(classAttr, inner);
}

QString exportList(QTextList *list)
{
    const bool ordered = list->format().style() == QTextListFormat::ListDecimal
        || list->format().style() == QTextListFormat::ListLowerAlpha
        || list->format().style() == QTextListFormat::ListUpperAlpha
        || list->format().style() == QTextListFormat::ListUpperRoman
        || list->format().style() == QTextListFormat::ListLowerRoman;

    const QString tag = ordered ? QStringLiteral("ol") : QStringLiteral("ul");
    QString html = QStringLiteral("<%1>\n").arg(tag);

    for (int i = 0; i < list->count(); ++i) {
        const QTextBlock block = list->item(i);
        const QString inner = exportBlockInline(block);
        html += QStringLiteral("  <li>%1</li>\n").arg(inner.isEmpty() ? QString() : inner);
    }

    html += QStringLiteral("</%1>").arg(tag);
    return html;
}

QString exportBlockContentWithoutClass(const QTextBlock &block, const QString &containerClass)
{
    const QTextBlockFormat fmt = block.blockFormat();

    QString innerHtml;
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid())
            continue;

        InlineStyle s = styleFromFormat(fragment.charFormat());
        if (s.cssClass == containerClass) {
            s.cssClass.clear(); // Suppress inner span wrapping since outer div handles it
        }
        innerHtml += wrapInline(fragment.text(), s);
    }

    const int headingLevel = fmt.headingLevel();
    if (headingLevel > 0) {
        const QString tag = headingTag(headingLevel);
        return QStringLiteral("<%1>%2</%1>").arg(tag, innerHtml);
    }

    if (fmt.intProperty(QTextFormat::BlockQuoteLevel) > 0) {
        return QStringLiteral("<blockquote>\n  <p>%1</p>\n</blockquote>").arg(innerHtml);
    }

    return QStringLiteral("<p>%1</p>").arg(innerHtml);
}

} // namespace

QString Ao3HtmlExporter::exportDocument(const QTextDocument *document)
{
    if (!document)
        return QStringLiteral("<p></p>");

    QStringList parts;
    QSet<QTextList *> exportedLists;

    QTextBlock block = document->begin();
    while (block.isValid()) {
        if (block.textList()) {
            QTextList *list = block.textList();
            if (!exportedLists.contains(list)) {
                exportedLists.insert(list);
                parts << exportList(list);
            }
            block = block.next();
            continue;
        }

        if (block.text().isEmpty() && block.begin() == block.end()) {
            parts << QStringLiteral("<p></p>");
            block = block.next();
            continue;
        }

        // Determine if this block has a container class
        QString blockClass;
        const QVariant bClassProp = block.blockFormat().property(CssClassProperty);
        if (bClassProp.isValid() && !bClassProp.toString().isEmpty()) {
            blockClass = bClassProp.toString();
        } else if (block.begin() != block.end()) {
            QString fragClass;
            bool uniform = true;
            for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment frag = it.fragment();
                if (frag.isValid() && !frag.text().isEmpty()) {
                    const QString fc = frag.charFormat().property(CssClassProperty).toString();
                    if (fc.isEmpty()) {
                        uniform = false;
                        break;
                    }
                    if (fragClass.isEmpty()) {
                        fragClass = fc;
                    } else if (fragClass != fc) {
                        uniform = false;
                        break;
                    }
                }
            }
            if (uniform && !fragClass.isEmpty()) {
                blockClass = fragClass;
            }
        }

        // If block has a container class, group consecutive blocks sharing this class into <div class="...">
        if (!blockClass.isEmpty()) {
            QStringList divGroup;
            QTextBlock curr = block;
            while (curr.isValid() && !curr.textList()) {
                QString currClass;
                const QVariant cProp = curr.blockFormat().property(CssClassProperty);
                if (cProp.isValid() && !cProp.toString().isEmpty()) {
                    currClass = cProp.toString();
                } else if (curr.begin() != curr.end()) {
                    QString fClass;
                    bool u = true;
                    for (QTextBlock::iterator it = curr.begin(); !it.atEnd(); ++it) {
                        const QTextFragment frag = it.fragment();
                        if (frag.isValid() && !frag.text().isEmpty()) {
                            const QString fc = frag.charFormat().property(CssClassProperty).toString();
                            if (fc.isEmpty() || (!fClass.isEmpty() && fc != fClass)) {
                                u = false;
                                break;
                            }
                            if (fClass.isEmpty()) fClass = fc;
                        }
                    }
                    if (u) currClass = fClass;
                }

                if (currClass != blockClass)
                    break;

                divGroup << exportBlockContentWithoutClass(curr, blockClass);
                curr = curr.next();
            }

            if (!divGroup.isEmpty()) {
                parts << QStringLiteral("<div class=\"%1\">\n  %2\n</div>")
                             .arg(escapeHtml(blockClass), divGroup.join(QStringLiteral("\n  ")));
                block = curr;
                continue;
            }
        }

        parts << exportBlock(block, document);
        block = block.next();
    }

    if (parts.isEmpty())
        return QStringLiteral("<p></p>");

    return Ao3HtmlSanitizer::sanitize(parts.join(QStringLiteral("\n")));
}
