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
    void updateTheme();
    void updateSettings();

signals:
    void contentChanged();
    void htmlExported(const QString &html);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

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
    void onInsertImageEmbed();
    void onInsertHorizontalRule();
    void onCodeBlock();
    void onApplyCssClass(const QString &className);
    void onDocumentChanged();
    void showEditorContextMenu(const QPoint &pos);

private:
    void setupToolbar();
    void mergeCharFormat(const QTextCharFormat &format);
    void mergeBlockFormat(const QTextBlockFormat &format);
    void handleEditorTooltip(class QHelpEvent *helpEvent);
    void fetchRemoteImagesInDocument();

    QToolBar *m_toolbar = nullptr;
    QTextEdit *m_editor = nullptr;
    EditorMinimap *m_minimap = nullptr;
    SpellChecker *m_spellChecker = nullptr;
    QTimer *m_spellTimer = nullptr;
    class QNetworkAccessManager *m_imageNam = nullptr;
    QString m_customCss;
    QStringList m_availableCssClasses;
    bool m_blockChanges = false;
};
