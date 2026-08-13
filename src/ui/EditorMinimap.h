#pragma once

#include <QWidget>

class QTextEdit;
class QPlainTextEdit;
class QScrollBar;
class QTextDocument;

class EditorMinimap : public QWidget
{
    Q_OBJECT

public:
    explicit EditorMinimap(QWidget *parent = nullptr);

    void setTargetEditor(QTextEdit *editor);
    void setTargetEditor(QPlainTextEdit *editor);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void handleMouseScroll(int y);
    QScrollBar* activeScrollBar() const;
    QTextDocument* activeDocument() const;

    QTextEdit *m_textEdit = nullptr;
    QPlainTextEdit *m_plainEdit = nullptr;
    bool m_dragging = false;
};
