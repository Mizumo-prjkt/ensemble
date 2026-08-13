#include "SpellChecker.h"

#include <QCoreApplication>
#include <QDebug>
#include <QProcessEnvironment>
#include <QRegularExpressionMatchIterator>
#include <QTextStream>

SpellChecker::SpellChecker(QObject *parent)
    : QObject(parent)
{
    initAspell();
}

SpellChecker::~SpellChecker()
{
    if (m_aspellProcess) {
        if (m_aspellProcess->state() == QProcess::Running) {
            m_aspellProcess->terminate();
            m_aspellProcess->waitForFinished(500);
        }
        delete m_aspellProcess;
        m_aspellProcess = nullptr;
    }
}

void SpellChecker::initAspell()
{
    // Try aspell CLI
    QProcess check;
    check.start(QStringLiteral("aspell"), {QStringLiteral("version")});
    if (check.waitForFinished(1000) && check.exitCode() == 0) {
        m_aspellAvailable = true;
    } else {
        // Fallback check hunspell
        check.start(QStringLiteral("hunspell"), {QStringLiteral("-v")});
        if (check.waitForFinished(1000) && check.exitCode() == 0) {
            m_aspellAvailable = true;
        }
    }
}

QSet<QString> SpellChecker::checkSpellingWithAspell(const QStringList &words)
{
    QSet<QString> misspelled;
    if (words.isEmpty())
        return misspelled;

    QProcess process;
    const QString cmd = m_aspellAvailable ? QStringLiteral("aspell") : QStringLiteral("hunspell");
    process.start(cmd, {QStringLiteral("-a"), QStringLiteral("--lang=en_US")});

    if (!process.waitForStarted(1000))
        return misspelled;

    QByteArray inputData;
    for (const QString &word : words) {
        inputData.append("^");
        inputData.append(word.toUtf8());
        inputData.append("\n");
    }

    process.write(inputData);
    process.closeWriteChannel();

    if (!process.waitForFinished(2000)) {
        process.kill();
        return misspelled;
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QStringList lines = output.split(QLatin1Char('\n'));

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('&')) || trimmed.startsWith(QLatin1Char('#'))) {
            // & word count offset: suggestions
            // # word offset
            const QStringList parts = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                misspelled.insert(parts[1]);
            }
        }
    }

    return misspelled;
}

QList<CheckResult> SpellChecker::checkText(const QString &text)
{
    QList<CheckResult> results;
    if (text.isEmpty())
        return results;

    // 1. Run Grammar & Punctuation Rules
    results.append(checkGrammar(text));

    // 2. Extract words for spell checking
    static const QRegularExpression wordRe(QStringLiteral(R"(\b[a-zA-Z]{2,}\b)"));
    QMap<QString, QList<int>> wordPositions;
    QStringList uniqueWords;

    QRegularExpressionMatchIterator it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString word = match.captured(0);
        const int pos = match.capturedStart();

        // Skip capitalized words in sentence middle (likely proper names like Kamisato, Aether, Ayaka)
        if (word.at(0).isUpper() && pos > 0) {
            const QChar prevChar = text.at(pos - 1);
            if (prevChar != QLatin1Char('.') && prevChar != QLatin1Char('!') &&
                prevChar != QLatin1Char('?') && prevChar != QLatin1Char('"') &&
                prevChar != QLatin1Char('\n')) {
                continue;
            }
        }

        if (!wordPositions.contains(word))
            uniqueWords.append(word);
        wordPositions[word].append(pos);
    }

    // Run Aspell / Hunspell check
    if (m_aspellAvailable && !uniqueWords.isEmpty()) {
        const QSet<QString> misspelledWords = checkSpellingWithAspell(uniqueWords);
        for (const QString &badWord : misspelledWords) {
            const QList<int> positions = wordPositions.value(badWord);
            for (int pos : positions) {
                CheckResult r;
                r.startPos = pos;
                r.length = badWord.length();
                r.type = CheckResult::SpellingError;
                r.word = badWord;
                r.message = QStringLiteral("Possible spelling mistake: '%1'").arg(badWord);
                results.append(r);
            }
        }
    }

    return results;
}

QList<CheckResult> SpellChecker::checkGrammar(const QString &text)
{
    QList<CheckResult> results;

    // A. Repeated Words (e.g. "the the", "and and", "is is", "in in", "of of")
    static const QRegularExpression repeatRe(
        QStringLiteral(R"(\b(the|and|is|in|of|to|that|it|for|on|with|as|at|by|from)\s+\1\b)"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator rIt = repeatRe.globalMatch(text);
    while (rIt.hasNext()) {
        const QRegularExpressionMatch m = rIt.next();
        CheckResult r;
        r.startPos = m.capturedStart();
        r.length = m.capturedLength();
        r.type = CheckResult::GrammarWarning;
        r.message = QStringLiteral("Repeated word: '%1'").arg(m.captured(0));
        results.append(r);
    }

    // B. Punctuation Spacing Errors (e.g. "word ,word" or "word.word" missing space after period)
    static const QRegularExpression spaceBeforePunctRe(QStringLiteral(R"(\s+[,.!?;:])"));
    QRegularExpressionMatchIterator pIt = spaceBeforePunctRe.globalMatch(text);
    while (pIt.hasNext()) {
        const QRegularExpressionMatch m = pIt.next();
        CheckResult r;
        r.startPos = m.capturedStart();
        r.length = m.capturedLength();
        r.type = CheckResult::GrammarWarning;
        r.message = QStringLiteral("Unexpected space before punctuation: '%1'").arg(m.captured(0));
        results.append(r);
    }

    // C. Missing space after comma/period before a word (e.g. "hello,world")
    static const QRegularExpression missingSpaceAfterRe(QStringLiteral(R"(\b[a-zA-Z]+[,;][a-zA-Z]+\b)"));
    QRegularExpressionMatchIterator mIt = missingSpaceAfterRe.globalMatch(text);
    while (mIt.hasNext()) {
        const QRegularExpressionMatch m = mIt.next();
        CheckResult r;
        r.startPos = m.capturedStart();
        r.length = m.capturedLength();
        r.type = CheckResult::GrammarWarning;
        r.message = QStringLiteral("Missing space after punctuation: '%1'").arg(m.captured(0));
        results.append(r);
    }

    return results;
}
