#pragma once

#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

struct CheckResult {
    enum Type {
        SpellingError,
        GrammarWarning
    };

    int startPos = 0;
    int length = 0;
    Type type = SpellingError;
    QString message;
    QString word;
};

class SpellChecker : public QObject
{
    Q_OBJECT

public:
    explicit SpellChecker(QObject *parent = nullptr);
    ~SpellChecker() override;

    bool isAspellAvailable() const { return m_aspellAvailable; }

    QList<CheckResult> checkText(const QString &text);

private:
    void initAspell();
    QSet<QString> checkSpellingWithAspell(const QStringList &words);
    QList<CheckResult> checkGrammar(const QString &text);

    bool m_aspellAvailable = false;
    QProcess *m_aspellProcess = nullptr;
    QSet<QString> m_customDictionary;
};
