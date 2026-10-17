#pragma once

#include <QDialog>

class QTableWidget;
class QLabel;
class QPushButton;
class QTextEdit;
class QComboBox;

namespace EnsemblePlugin {

class PluginManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit PluginManagerDialog(QWidget *parent = nullptr);
    ~PluginManagerDialog() override = default;

public slots:
    void refreshPluginList();

private slots:
    void onSelectionChanged();
    void onLoadPluginClicked();
    void onOpenPluginsFolderClicked();
    void onReloadClicked();
    void onToggleActiveClicked();
    void onRunActionClicked();
    void onCreateSamplePluginClicked();

private:
    void setupUi();
    void updateDetailsPane(const QString &publicId);

    QTableWidget *m_table = nullptr;
    QLabel *m_idLabel = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_statusBadge = nullptr;
    QLabel *m_pathLabel = nullptr;
    QTextEdit *m_errorBox = nullptr;
    QComboBox *m_actionCombo = nullptr;
    QPushButton *m_runActionBtn = nullptr;
    QPushButton *m_toggleActiveBtn = nullptr;
    QPushButton *m_loadBtn = nullptr;
    QPushButton *m_openFolderBtn = nullptr;
    QPushButton *m_reloadBtn = nullptr;
    QPushButton *m_createSampleBtn = nullptr;
};

} // namespace EnsemblePlugin
