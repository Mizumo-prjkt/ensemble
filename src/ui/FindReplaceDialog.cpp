#include "FindReplaceDialog.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>

FindReplaceDialog::FindReplaceDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Find and Replace"));
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    resize(380, 150);

    auto *mainLayout = new QVBoxLayout(this);
    auto *gridLayout = new QGridLayout;

    auto *findLabel = new QLabel(QStringLiteral("Find:"), this);
    m_findEdit = new QLineEdit(this);
    gridLayout->addWidget(findLabel, 0, 0);
    gridLayout->addWidget(m_findEdit, 0, 1);

    auto *replaceLabel = new QLabel(QStringLiteral("Replace:"), this);
    m_replaceEdit = new QLineEdit(this);
    gridLayout->addWidget(replaceLabel, 1, 0);
    gridLayout->addWidget(m_replaceEdit, 1, 1);

    m_caseCheck = new QCheckBox(QStringLiteral("Case Sensitive"), this);
    gridLayout->addWidget(m_caseCheck, 2, 1);

    mainLayout->addLayout(gridLayout);

    auto *btnLayout = new QHBoxLayout;
    m_findNextBtn = new QPushButton(QStringLiteral("Find Next"), this);
    m_replaceBtn = new QPushButton(QStringLiteral("Replace"), this);
    m_replaceAllBtn = new QPushButton(QStringLiteral("Replace All"), this);
    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);

    btnLayout->addWidget(m_findNextBtn);
    btnLayout->addWidget(m_replaceBtn);
    btnLayout->addWidget(m_replaceAllBtn);
    btnLayout->addWidget(closeBtn);

    mainLayout->addLayout(btnLayout);

    connect(m_findNextBtn, &QPushButton::clicked, this, &FindReplaceDialog::onFindNext);
    connect(m_replaceBtn, &QPushButton::clicked, this, &FindReplaceDialog::onReplace);
    connect(m_replaceAllBtn, &QPushButton::clicked, this, &FindReplaceDialog::onReplaceAll);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    // Default button
    m_findNextBtn->setDefault(true);
}

void FindReplaceDialog::setTargetEditor(QWidget *editor)
{
    m_targetEditor = editor;
}

void FindReplaceDialog::showFind()
{
    m_replaceEdit->setEnabled(false);
    m_replaceBtn->setEnabled(false);
    m_replaceAllBtn->setEnabled(false);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    show();
    raise();
    activateWindow();
}

void FindReplaceDialog::showReplace()
{
    m_replaceEdit->setEnabled(true);
    m_replaceBtn->setEnabled(true);
    m_replaceAllBtn->setEnabled(true);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    show();
    raise();
    activateWindow();
}

void FindReplaceDialog::onFindNext()
{
    if (!m_targetEditor)
        return;

    const QString searchString = m_findEdit->text();
    if (searchString.isEmpty())
        return;

    QTextDocument *doc = nullptr;
    QTextCursor cursor;
    
    auto *te = qobject_cast<QTextEdit *>(m_targetEditor);
    auto *pe = qobject_cast<QPlainTextEdit *>(m_targetEditor);
    
    if (te) {
        doc = te->document();
        cursor = te->textCursor();
    } else if (pe) {
        doc = pe->document();
        cursor = pe->textCursor();
    }

    if (!doc)
        return;

    QTextDocument::FindFlags flags;
    if (m_caseCheck->isChecked())
        flags |= QTextDocument::FindCaseSensitively;

    // Search from the current cursor position
    QTextCursor found = doc->find(searchString, cursor, flags);

    // If not found, wrap around and search from the beginning of the document
    if (found.isNull()) {
        QTextCursor startCursor(doc);
        found = doc->find(searchString, startCursor, flags);
    }

    if (!found.isNull()) {
        if (te) te->setTextCursor(found);
        else if (pe) pe->setTextCursor(found);
    } else {
        QMessageBox::information(this, QStringLiteral("Find"),
                                 QStringLiteral("Cannot find \"%1\"").arg(searchString));
    }
}

void FindReplaceDialog::onReplace()
{
    if (!m_targetEditor)
        return;

    const QString searchString = m_findEdit->text();
    const QString replaceString = m_replaceEdit->text();
    if (searchString.isEmpty())
        return;

    QTextCursor cursor;
    auto *te = qobject_cast<QTextEdit *>(m_targetEditor);
    auto *pe = qobject_cast<QPlainTextEdit *>(m_targetEditor);
    
    if (te) cursor = te->textCursor();
    else if (pe) cursor = pe->textCursor();

    if (cursor.isNull())
        return;

    // Check if the current selection matches the search term
    bool isMatch = false;
    if (m_caseCheck->isChecked()) {
        isMatch = (cursor.selectedText() == searchString);
    } else {
        isMatch = (cursor.selectedText().compare(searchString, Qt::CaseInsensitive) == 0);
    }

    if (isMatch) {
        cursor.insertText(replaceString);
        if (te) te->setTextCursor(cursor);
        else if (pe) pe->setTextCursor(cursor);
    }

    // Move to the next match
    onFindNext();
}

void FindReplaceDialog::onReplaceAll()
{
    if (!m_targetEditor)
        return;

    const QString searchString = m_findEdit->text();
    const QString replaceString = m_replaceEdit->text();
    if (searchString.isEmpty())
        return;

    QTextDocument *doc = nullptr;
    auto *te = qobject_cast<QTextEdit *>(m_targetEditor);
    auto *pe = qobject_cast<QPlainTextEdit *>(m_targetEditor);
    
    if (te) doc = te->document();
    else if (pe) doc = pe->document();

    if (!doc)
        return;

    QTextDocument::FindFlags flags;
    if (m_caseCheck->isChecked())
        flags |= QTextDocument::FindCaseSensitively;

    int replacementsCount = 0;
    QTextCursor cursor(doc);

    // Lock updates during replace all to speed up and avoid flickering
    cursor.beginEditBlock();

    while (true) {
        QTextCursor found = doc->find(searchString, cursor, flags);
        if (found.isNull())
            break;

        found.insertText(replaceString);
        cursor = found;
        replacementsCount++;
    }

    cursor.endEditBlock();

    QMessageBox::information(this, QStringLiteral("Replace All"),
                             QStringLiteral("Replaced %1 occurrence(s).").arg(replacementsCount));
}
