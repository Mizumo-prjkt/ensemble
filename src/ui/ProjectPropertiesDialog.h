#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class Ao3Project;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QTreeWidget;
class QTextEdit;
class QTabWidget;

class ProjectPropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProjectPropertiesDialog(Ao3Project *project, QWidget *parent = nullptr);
    ~ProjectPropertiesDialog() override = default;

private slots:
    void onSaveClicked();
    void onNewCommitClicked();
    void onCommitSelected();
    void onCopyImageEmbedHtml();
    void onOpenImageUrl();
    void onPreviewImageEmbed();

private:
    void setupUi();
    QWidget *createMetadataTab();
    QWidget *createTagsTab();
    QWidget *createInfoTab();
    QWidget *createHistoryTab();
    QWidget *createImageEmbedsTab();
    void refreshHistoryList();
    void refreshImageEmbeds();
    void saveProperties();

    Ao3Project *m_project = nullptr;

    // Metadata controls
    QLineEdit *m_titleEdit = nullptr;
    QLineEdit *m_authorEdit = nullptr;
    QComboBox *m_ratingCombo = nullptr;

    // Tag checkboxes
    QList<QCheckBox *> m_warningBoxes;
    QList<QCheckBox *> m_categoryBoxes;

    // Tag line edits (comma-separated)
    QLineEdit *m_fandomsEdit = nullptr;
    QLineEdit *m_relationshipsEdit = nullptr;
    QLineEdit *m_charactersEdit = nullptr;
    QLineEdit *m_tagsEdit = nullptr;

    // Information labels
    QLabel *m_filePathLabel = nullptr;
    QLabel *m_createdLabel = nullptr;
    QLabel *m_modifiedLabel = nullptr;
    QLabel *m_lastModifiedByLabel = nullptr;
    QLabel *m_statsLabel = nullptr;

    // History controls
    QTreeWidget *m_commitTree = nullptr;
    QTextEdit *m_commitDetailsEdit = nullptr;

    // Image Embed controls
    QTreeWidget *m_imageTree = nullptr;
    QPushButton *m_copyImageBtn = nullptr;
    QPushButton *m_openImageBtn = nullptr;
    QPushButton *m_previewImageBtn = nullptr;
};
