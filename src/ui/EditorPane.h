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

    enum class StyleTargetMode {
        Span,   // Inline text highlighter (<span class="...">)
        Div     // Card / block container (<div class="...">)
    };

    QTextEdit *editor() const { return m_editor; }
    QString currentHtml() const;
    StyleTargetMode styleMode() const { return m_styleMode; }
    void setStyleMode(StyleTargetMode mode);
    void refreshClassHighlights();

public slots:
    void setHtml(const QString &html);
    void focusEditor();
    void setCustomCss(const QString &css);
    void setAvailableCssClasses(const QStringList &classes);
    void runSpellAndGrammarChecks();
    void updateTheme();
    void updateSettings();
    void updateToolbarActiveStates();
    void updateToolbarIcons();

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
    void onApplyCssClassWithMode(const QString &className, StyleTargetMode mode);
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
    class QComboBox *m_styleModeSelector = nullptr;
    StyleTargetMode m_styleMode = StyleTargetMode::Span;

    QAction *m_boldAction = nullptr;
    QAction *m_italicAction = nullptr;
    QAction *m_underlineAction = nullptr;
    QAction *m_strikeAction = nullptr;
    QAction *m_h1Action = nullptr;
    QAction *m_h2Action = nullptr;
    QAction *m_h3Action = nullptr;
    QAction *m_paragraphAction = nullptr;
    QAction *m_blockquoteAction = nullptr;
    QAction *m_bulletListAction = nullptr;
    QAction *m_numberedListAction = nullptr;
    QAction *m_linkAction = nullptr;
    QAction *m_imageAction = nullptr;
    QAction *m_hrAction = nullptr;
    QAction *m_codeAction = nullptr;
};
