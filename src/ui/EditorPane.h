#pragma once

#include <QStringList>
#include <QWidget>

class QMenu;
class QToolBar;
class QTextEdit;
class QTextBlockFormat;
class QTextCharFormat;
class QTimer;
class EditorMinimap;
class SpellChecker;

class EditorPane : public QWidget
{
    Q_OBJECT

public:
    explicit EditorPane(QWidget *parent = nullptr);

    QTextEdit *editor() const { return m_editor; }
    QString currentHtml() const;

public slots:
    void setHtml(const QString &html);
    void focusEditor();
    void setCustomCss(const QString &css);
    void setAvailableCssClasses(const QStringList &classes);
    void runSpellAndGrammarChecks();

signals:
    void contentChanged();
    void htmlExported(const QString &html);

private slots:
    void onBold();
    void onItalic();
    void onUnderline();
    void onStrikethrough();
    void onHeading(int level);
    void onParagraph();
    void onBlockquote();
    void onBulletList();
    void onNumberedList();
    void onInsertLink();
    void onInsertHorizontalRule();
    void onCodeBlock();
    void onDocumentChanged();
    void onApplyCssClass(const QString &className);

private:
    void setupToolbar();
    void mergeCharFormat(const QTextCharFormat &format);
    void mergeBlockFormat(const QTextBlockFormat &format);
    void showEditorContextMenu(const QPoint &pos);

    QTextEdit *m_editor = nullptr;
    EditorMinimap *m_minimap = nullptr;
    QToolBar *m_toolbar = nullptr;
    SpellChecker *m_spellChecker = nullptr;
    QTimer *m_spellTimer = nullptr;
    bool m_blockChanges = false;
    QStringList m_availableCssClasses;
    QString m_customCss;
};
