#pragma once

#include <QStringList>
#include <QWidget>

class CodeEditor;
class QLabel;
class Ao3Project;

class CssEditorPane : public QWidget
{
    Q_OBJECT

public:
    explicit CssEditorPane(QWidget *parent = nullptr);

    QString css() const;
    QStringList cssClassNames() const { return m_classNames; }
    CodeEditor *editor() const { return m_editor; }
    void bindProject(const Ao3Project *project) { m_project = project; }

public slots:
    void setCss(const QString &css);

signals:
    void cssChanged(const QString &css);
    void cssClassesChanged(const QStringList &classNames);

private:
    void parseCssClasses(const QString &css);

    CodeEditor *m_editor = nullptr;
    QLabel *m_warningLabel = nullptr;
    const Ao3Project *m_project = nullptr;
    bool m_blockChanges = false;
    QStringList m_classNames;
};
