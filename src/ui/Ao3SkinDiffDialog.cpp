#include "Ao3SkinDiffDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

Ao3SkinDiffDialog::Ao3SkinDiffDialog(const QString &currentCss, const QString &importedCss, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Work Skin Conflict — Diff Preview"));
    setFixedSize(700, 480);
    setModal(true);
    setupUi(currentCss, importedCss);
}

void Ao3SkinDiffDialog::setupUi(const QString &currentCss, const QString &importedCss)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    auto *infoLabel = new QLabel(QStringLiteral(
        "<b style=\"color: #bd93f9;\">Work Skin Conflict Detected</b><br/>"
        "The project currently has a Work Skin loaded, and the imported work also contains a Work Skin. "
        "Review the differences below and choose an action:"), this);
    infoLabel->setTextFormat(Qt::RichText);
    mainLayout->addWidget(infoLabel);

    auto *diffLayout = new QHBoxLayout();
    diffLayout->setSpacing(12);

    // Left Panel: Current Skin
    auto *currentBox = new QVBoxLayout();
    currentBox->addWidget(new QLabel(QStringLiteral("<b>Current Project Skin:</b>"), this));
    m_currentEdit = new QTextEdit(this);
    m_currentEdit->setReadOnly(true);
    m_currentEdit->setPlainText(currentCss);
    m_currentEdit->setStyleSheet(QStringLiteral("background-color: #1e1e2e; color: #f8f8f2; font-family: monospace; font-size: 11px;"));
    currentBox->addWidget(m_currentEdit);
    diffLayout->addLayout(currentBox);

    // Right Panel: Imported Skin
    auto *importedBox = new QVBoxLayout();
    importedBox->addWidget(new QLabel(QStringLiteral("<b>Imported Work Skin:</b>"), this));
    m_importedEdit = new QTextEdit(this);
    m_importedEdit->setReadOnly(true);
    m_importedEdit->setPlainText(importedCss);
    m_importedEdit->setStyleSheet(QStringLiteral("background-color: #1e1e2e; color: #50fa7b; font-family: monospace; font-size: 11px;"));
    importedBox->addWidget(m_importedEdit);
    diffLayout->addLayout(importedBox);

    mainLayout->addLayout(diffLayout, 1);

    // Action Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    auto *keepBtn = new QPushButton(QStringLiteral("Keep Current Skin"), this);
    auto *useImportedBtn = new QPushButton(QStringLiteral("Use Imported Skin"), this);
    auto *mergeBtn = new QPushButton(QStringLiteral("Merge (Append Both)"), this);

    keepBtn->setStyleSheet(QStringLiteral("background-color: #6272a4; color: #ffffff; padding: 6px 12px; border-radius: 4px;"));
    useImportedBtn->setStyleSheet(QStringLiteral("background-color: #ff79c6; color: #ffffff; padding: 6px 12px; border-radius: 4px;"));
    mergeBtn->setStyleSheet(QStringLiteral("background-color: #bd93f9; color: #ffffff; padding: 6px 12px; border-radius: 4px;"));

    btnLayout->addStretch();
    btnLayout->addWidget(keepBtn);
    btnLayout->addWidget(useImportedBtn);
    btnLayout->addWidget(mergeBtn);

    mainLayout->addLayout(btnLayout);

    connect(keepBtn, &QPushButton::clicked, this, [this]() {
        m_choice = SkinDiffResult::KeepCurrent;
        accept();
    });

    connect(useImportedBtn, &QPushButton::clicked, this, [this]() {
        m_choice = SkinDiffResult::UseImported;
        accept();
    });

    connect(mergeBtn, &QPushButton::clicked, this, [this]() {
        m_choice = SkinDiffResult::MergeAppend;
        accept();
    });
}
