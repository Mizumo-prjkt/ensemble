#pragma once

#include <QDialog>
#include <QTextEdit>

enum class SkinDiffResult
{
    KeepCurrent,
    UseImported,
    MergeAppend
};

class Ao3SkinDiffDialog : public QDialog
{
    Q_OBJECT
public:
    explicit Ao3SkinDiffDialog(const QString &currentCss, const QString &importedCss, QWidget *parent = nullptr);
    ~Ao3SkinDiffDialog() override = default;

    SkinDiffResult userChoice() const { return m_choice; }

private:
    void setupUi(const QString &currentCss, const QString &importedCss);

    QTextEdit *m_currentEdit = nullptr;
    QTextEdit *m_importedEdit = nullptr;
    SkinDiffResult m_choice = SkinDiffResult::KeepCurrent;
};
