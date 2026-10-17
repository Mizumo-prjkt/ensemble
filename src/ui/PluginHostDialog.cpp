#include "PluginHostDialog.h"
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace EnsemblePlugin {

PluginHostDialog::PluginHostDialog(const QString &publicId,
                                   const QString &winId,
                                   const QString &title,
                                   int width,
                                   int height,
                                   QWidget *parent)
    : QDialog(parent)
    , m_publicId(publicId)
    , m_winId(winId)
{
    setWindowTitle(QStringLiteral("[%1] %2").arg(publicId, title));
    resize(width, height);
    setAttribute(Qt::WA_DeleteOnClose, false);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    m_contentLayout = new QVBoxLayout();
    m_contentLayout->setSpacing(8);
    mainLayout->addLayout(m_contentLayout);

    mainLayout->addStretch();
}

void PluginHostDialog::addLabel(const QString &text) {
    auto *label = new QLabel(text, this);
    label->setWordWrap(true);
    m_contentLayout->addWidget(label);
}

void PluginHostDialog::addText(const QString &content, bool readOnly) {
    auto *edit = new QTextEdit(this);
    edit->setPlainText(content);
    edit->setReadOnly(readOnly);
    edit->setMinimumHeight(80);
    m_contentLayout->addWidget(edit);
}

void PluginHostDialog::addInput(const QString &placeholder, const QString &defaultText) {
    auto *input = new QLineEdit(this);
    input->setPlaceholderText(placeholder);
    input->setText(defaultText);
    m_inputs.append(input);
    m_contentLayout->addWidget(input);
}

void PluginHostDialog::addButton(const QString &label, const QString &actionName) {
    auto *btn = new QPushButton(label, this);
    connect(btn, &QPushButton::clicked, this, [this, actionName]() {
        QVariantMap inputs;
        for (int i = 0; i < m_inputs.size(); ++i) {
            inputs[QStringLiteral("input_%1").arg(i)] = m_inputs.at(i)->text();
            if (i == 0) {
                inputs[QStringLiteral("text")] = m_inputs.at(i)->text();
            }
        }
        emit actionTriggered(m_publicId, m_winId, actionName, inputs);
    });
    m_contentLayout->addWidget(btn);
}

void PluginHostDialog::closeEvent(QCloseEvent *event) {
    emit dialogClosed(m_winId);
    QDialog::closeEvent(event);
}

} // namespace EnsemblePlugin
