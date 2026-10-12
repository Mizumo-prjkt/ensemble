#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QTextBrowser;
class QPushButton;
class QLabel;

struct HelpTopic {
    QString id;
    QString icon;
    QString title;
    QString summary;
    QString contentHtml;
};

class HelpGuideDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HelpGuideDialog(QWidget *parent = nullptr);
    ~HelpGuideDialog() override = default;

public slots:
    void updateTheme();
    void selectTopic(const QString &topicId);

private slots:
    void onTopicSelectionChanged();
    void onSearchTextChanged(const QString &text);

private:
    void setupUi();
    void populateTopics();
    void renderTopic(const HelpTopic &topic);

    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_topicList = nullptr;
    QTextBrowser *m_contentBrowser = nullptr;
    QLabel *m_headerTitle = nullptr;
    QLabel *m_headerSubTitle = nullptr;
    QPushButton *m_closeBtn = nullptr;

    QList<HelpTopic> m_topics;
};
