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

QString filterAttributes(const QString &tag, const QString &attrBlob, QSet<QString> *strippedAttrs = nullptr)
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
        if (!allowed.contains(name)) {
            if (strippedAttrs) {
                strippedAttrs->insert(name);
            }
            continue;
        }
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

SanitizeResult Ao3HtmlSanitizer::sanitizeDetailed(const QString &html)
{
    SanitizeResult result;
    QSet<QString> strippedTagsSet;
    QSet<QString> strippedAttrsSet;

    QString cleaned = unescapeQuoteEntities(html);
    if (cleaned.contains(QStringLiteral("<!--"))) {
        result.wasModified = true;
        cleaned.remove(QRegularExpression(QStringLiteral(R"(<!--[\s\S]*?-->)")));
    }
    if (cleaned.trimmed().isEmpty()) {
        result.cleanHtml = QStringLiteral("<p></p>");
        result.wasModified = (html != result.cleanHtml);
        return result;
    }

    const QSet<QString> allowed = Ao3TagWhitelist::allowedTags();

    static const QRegularExpression tagRe(
        QStringLiteral(R"(<\/?([a-zA-Z][\w:-]*)([^>]*)\/?>)"),
        QRegularExpression::CaseInsensitiveOption);

    QString out;
    out.reserve(cleaned.size());

    QStack<QString> openTags;
    int lastPos = 0;

    QRegularExpressionMatchIterator it = tagRe.globalMatch(cleaned);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += escapeHtml(cleaned.mid(lastPos, m.capturedStart() - lastPos));

        const QString rawTag = m.captured(1);
        const QString tag = normalizeTagName(rawTag);
        const bool closing = m.captured(0).startsWith(QStringLiteral("</"));
        const bool selfClosing = m.captured(0).endsWith(QStringLiteral("/>")) || isVoidTag(tag);
        const QString attrs = filterAttributes(tag, m.captured(2).trimmed(), &strippedAttrsSet);

        if (!allowed.contains(tag)) {
            strippedTagsSet.insert(rawTag.toLower());
            lastPos = m.capturedEnd();
            continue;
        }

        if (closing) {
            while (!openTags.isEmpty() && openTags.top() != tag)
                out += QStringLiteral("</%1>").arg(openTags.pop());
            if (!openTags.isEmpty() && openTags.top() == tag) {
                openTags.pop();
                out += QStringLiteral("</%1>").arg(tag);
            }
        } else if (selfClosing) {
            if (attrs.isEmpty())
                out += QStringLiteral("<%1 />").arg(tag);
            else
                out += QStringLiteral("<%1 %2 />").arg(tag, attrs);
        } else {
            if (attrs.isEmpty())
                out += QStringLiteral("<%1>").arg(tag);
            else
                out += QStringLiteral("<%1 %2>").arg(tag, attrs);
            openTags.push(tag);
        }

        lastPos = m.capturedEnd();
    }

    out += escapeHtml(cleaned.mid(lastPos));
    out += repairStack(openTags);

    // Strip script/style remnants and inline style attributes defensively.
    static const QRegularExpression scriptStyleRe(QStringLiteral(R"(<\/?(?:script|style)[^>]*>)"),
                                                  QRegularExpression::CaseInsensitiveOption);
    if (out.contains(scriptStyleRe)) {
        strippedTagsSet.insert(QStringLiteral("script"));
        out.remove(scriptStyleRe);
    }

    static const QRegularExpression styleAttrRe(QStringLiteral(R"(\sstyle\s*=\s*("[^"]*"|'[^']*'|[^\s>]+))"),
                                                QRegularExpression::CaseInsensitiveOption);
    if (out.contains(styleAttrRe)) {
        strippedAttrsSet.insert(QStringLiteral("style"));
        out.remove(styleAttrRe);
    }

    if (out.trimmed().isEmpty())
        out = QStringLiteral("<p></p>");

    result.cleanHtml = out;
    result.strippedTags = QStringList(strippedTagsSet.begin(), strippedTagsSet.end());
    result.strippedTags.sort();
    result.strippedAttributes = QStringList(strippedAttrsSet.begin(), strippedAttrsSet.end());
    result.strippedAttributes.sort();
    result.wasModified = (out != html) || !result.strippedTags.isEmpty() || !result.strippedAttributes.isEmpty();

    return result;
}

QString Ao3HtmlSanitizer::sanitize(const QString &html)
{
    return sanitizeDetailed(html).cleanHtml;
}
