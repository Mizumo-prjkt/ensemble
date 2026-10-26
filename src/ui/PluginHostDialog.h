#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QVariantMap>

class QVBoxLayout;
class QLineEdit;

namespace EnsemblePlugin {

class PluginHostDialog : public QDialog {
    Q_OBJECT

public:
    explicit PluginHostDialog(const QString &publicId,
                              const QString &winId,
                              const QString &title,
                              int width = 450,
                              int height = 320,
                              QWidget *parent = nullptr);
    ~PluginHostDialog() override = default;

    const QString &publicId() const noexcept { return m_publicId; }
    const QString &winId() const noexcept { return m_winId; }

    void addLabel(const QString &text);
    void addText(const QString &content, bool readOnly = true);
    void addInput(const QString &placeholder, const QString &defaultText = QString());
    void addButton(const QString &label, const QString &actionName);
    void clearWidgets();

signals:
    void actionTriggered(const QString &publicId,
                         const QString &winId,
                         const QString &actionName,
                         const QVariantMap &inputs);
    void dialogClosed(const QString &winId);

public slots:
    void accept() override;
    void reject() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QString m_publicId;
    QString m_winId;
    QVBoxLayout *m_contentLayout = nullptr;
    QList<QLineEdit *> m_inputs;
    bool m_closing = false;
};

} // namespace EnsemblePlugin
