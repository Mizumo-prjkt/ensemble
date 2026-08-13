#include "Ao3HtmlSanitizer.h"

#include "Ao3TagWhitelist.h"

#include <QRegularExpression>
#include <QStack>
#include <QStringList>

namespace {

QString escapeHtml(const QString &text)
{
    QString out;
    out.reserve(text.size());
    int i = 0;
    int len = text.size();
    while (i < len) {
        QChar c = text[i];
        if (c == '&') {
            // Check if it's a valid entity like &quot;, &amp;, &lt;, &gt;, or numeric entities
            static const QRegularExpression entityRe(R"(^&([a-zA-Z0-9]+|#[0-9]+|#x[a-fA-F0-9]+);)");
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

QString unescapeQuoteEntities(const QString &text)
{
    QString out = text;
    while (out.contains(QStringLiteral("&quot;"))) {
        out.replace(QStringLiteral("&amp;quot;"), QStringLiteral("&quot;"));
        out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    }
    while (out.contains(QStringLiteral("&apos;")) || out.contains(QStringLiteral("&#39;"))) {
        out.replace(QStringLiteral("&amp;apos;"), QStringLiteral("&apos;"));
        out.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
        out.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    }
    return out;
}

QString normalizeTagName(const QString &name)
{
    const QString lower = name.toLower();
    const auto normalizations = Ao3TagWhitelist::tagNormalizations();
    return normalizations.value(lower, lower);
}

bool isVoidTag(const QString &tag)
{
    static const QSet<QString> voidTags = {
        QStringLiteral("br"), QStringLiteral("hr"), QStringLiteral("img"),
        QStringLiteral("col"),
    };
    return voidTags.contains(tag);
}

QString filterAttributes(const QString &tag, const QString &attrBlob)
{
    const QSet<QString> allowed = Ao3TagWhitelist::allowedAttributes();
    QStringList kept;

    static const QRegularExpression attrRe(
        QStringLiteral(R"re((\w+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>/]+)))re"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = attrRe.globalMatch(attrBlob);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString name = m.captured(1).toLower();
        if (!allowed.contains(name))
            continue;
        QString value = m.captured(2);
        if (value.isNull())
            value = m.captured(3);
        if (value.isNull())
            value = m.captured(4);
        kept << QStringLiteral("%1=\"%2\"").arg(name, escapeHtml(value));
    }

    return kept.join(QStringLiteral(" "));
}

QString repairStack(QStack<QString> stack)
{
    QString closing;
    while (!stack.isEmpty())
        closing += QStringLiteral("</%1>").arg(stack.pop());
    return closing;
}

} // namespace

QString Ao3HtmlSanitizer::sanitize(const QString &html)
{
    QString cleaned = unescapeQuoteEntities(html);
    cleaned.remove(QRegularExpression(QStringLiteral(R"(<!--[\s\S]*?-->)")));
    if (cleaned.trimmed().isEmpty())
        return QStringLiteral("<p></p>");

    const QSet<QString> allowed = Ao3TagWhitelist::allowedTags();

    static const QRegularExpression tagRe(
        QStringLiteral(R"(<\/?([a-zA-Z][\w:-]*)([^>]*)\/?>)"),
        QRegularExpression::CaseInsensitiveOption);

    QString result;
    result.reserve(cleaned.size());

    QStack<QString> openTags;
    int lastPos = 0;

    QRegularExpressionMatchIterator it = tagRe.globalMatch(cleaned);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        result += escapeHtml(cleaned.mid(lastPos, m.capturedStart() - lastPos));

        const QString rawTag = m.captured(1);
        const QString tag = normalizeTagName(rawTag);
        const bool closing = m.captured(0).startsWith(QStringLiteral("</"));
        const bool selfClosing = m.captured(0).endsWith(QStringLiteral("/>")) || isVoidTag(tag);
        const QString attrs = filterAttributes(tag, m.captured(2).trimmed());

        if (!allowed.contains(tag)) {
            lastPos = m.capturedEnd();
            continue;
        }

        if (closing) {
            while (!openTags.isEmpty() && openTags.top() != tag)
                result += QStringLiteral("</%1>").arg(openTags.pop());
            if (!openTags.isEmpty() && openTags.top() == tag) {
                openTags.pop();
                result += QStringLiteral("</%1>").arg(tag);
            }
        } else if (selfClosing) {
            if (attrs.isEmpty())
                result += QStringLiteral("<%1 />").arg(tag);
            else
                result += QStringLiteral("<%1 %2 />").arg(tag, attrs);
        } else {
            if (attrs.isEmpty())
                result += QStringLiteral("<%1>").arg(tag);
            else
                result += QStringLiteral("<%1 %2>").arg(tag, attrs);
            openTags.push(tag);
        }

        lastPos = m.capturedEnd();
    }

    result += escapeHtml(cleaned.mid(lastPos));
    result += repairStack(openTags);

    // Strip script/style remnants and inline style attributes defensively.
    result.remove(QRegularExpression(QStringLiteral(R"(<\/?(?:script|style)[^>]*>)"),
                                     QRegularExpression::CaseInsensitiveOption));
    result.remove(QRegularExpression(QStringLiteral(R"(\sstyle\s*=\s*("[^"]*"|'[^']*'|[^\s>]+))"),
                                     QRegularExpression::CaseInsensitiveOption));

    if (result.trimmed().isEmpty())
        return QStringLiteral("<p></p>");

    return result;
}
