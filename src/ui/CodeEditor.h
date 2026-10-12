#pragma once

#include <QList>
#include <QPlainTextEdit>
#include <QString>

struct CodeDiagnosticIssue {
    int startPos = -1;
    int length = 0;
    int lineNumber = 0;
    QString message;
    bool isError = false; // false = warning (amber), true = error (red)
};

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    enum class SyntaxMode {
        None,
        Html,
        Css
    };

    explicit CodeEditor(QWidget *parent = nullptr);

    void setSyntaxMode(SyntaxMode mode) { m_syntaxMode = mode; }
    SyntaxMode syntaxMode() const { return m_syntaxMode; }

    void setDiagnosticIssues(const QList<CodeDiagnosticIssue> &issues);
    QList<CodeDiagnosticIssue> diagnosticIssues() const { return m_diagnosticIssues; }
    void clearDiagnosticIssues();
    void refreshHighlights();

    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth();

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect &rect, int dy);

private:
    void updateExtraSelections();

    QWidget *lineNumberArea = nullptr;
    SyntaxMode m_syntaxMode = SyntaxMode::None;
    QList<CodeDiagnosticIssue> m_diagnosticIssues;
};
