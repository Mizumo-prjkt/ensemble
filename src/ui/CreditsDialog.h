#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QFrame;

struct CreditEntry {
    QString name;
    QString version;
    QString author;
    QString licenseType;
    QString websiteUrl;
    QString description;
    QString licenseText;
};

class CreditsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CreditsDialog(QWidget *parent = nullptr);

public slots:
    void updateTheme();

private slots:
    void onFilterChanged(const QString &text);
    void onItemSelectionChanged();
    void onCopyLicenseClicked();
    void onVisitWebsiteClicked();

private:
    void setupUi();
    void populateEntries();
    void updateDetailView(const CreditEntry &entry);

    QLabel *m_headerTitle = nullptr;
    QLabel *m_headerSubTitle = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_listWidget = nullptr;
    QFrame *m_detailCard = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_authorLabel = nullptr;
    QLabel *m_licenseBadge = nullptr;
    QLabel *m_descLabel = nullptr;
    QLabel *m_licenseSectionLabel = nullptr;
    QPlainTextEdit *m_licenseEdit = nullptr;
    QPushButton *m_copyLicenseBtn = nullptr;
    QPushButton *m_visitWebsiteBtn = nullptr;
    QLabel *m_copyNoticeLabel = nullptr;
    QLabel *m_footerNote = nullptr;
    QPushButton *m_closeBtn = nullptr;

    QList<CreditEntry> m_entries;
};
