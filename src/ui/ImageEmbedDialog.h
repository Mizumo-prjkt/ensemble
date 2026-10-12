#pragma once

#include <QDialog>
#include <QPixmap>
#include <QSize>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QSpinBox;
class QTextEdit;

class ImageEmbedDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ImageEmbedDialog(QWidget *parent = nullptr);
    ~ImageEmbedDialog() override;

    // Prefill data when editing or configuring an embed
    void setImageUrl(const QString &url);
    void setAltText(const QString &alt);
    void setTitleText(const QString &title);
    void setImageWidth(int width);
    void setImageHeight(int height);
    void setAlignment(const QString &align); // "inline", "center", "left", "right"
    void setWrapInLink(bool wrap);

    // Accessors
    QString imageUrl() const;
    QString altText() const;
    QString titleText() const;
    int imageWidth() const;
    int imageHeight() const;
    QString alignment() const; // "inline", "center", "left", "right"
    bool wrapInLink() const;

    // Pure AO3 HTML snippet generator
    QString generateAo3Html() const;

    // Loaded pixmap (if successfully downloaded during preview)
    QPixmap previewPixmap() const { return m_previewPixmap; }

private slots:
    void onUrlChanged();
    void onFetchPreview();
    void onReplyFinished(QNetworkReply *reply);
    void onWidthChanged(int w);
    void onHeightChanged(int h);
    void updateAo3HtmlPreview();
    void onCopyAo3Html();
    void onAccept();

private:
    void setupUi();

    QLineEdit *m_urlEdit = nullptr;
    QPushButton *m_previewBtn = nullptr;
    QLineEdit *m_altEdit = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QSpinBox *m_widthSpin = nullptr;
    QSpinBox *m_heightSpin = nullptr;
    QCheckBox *m_keepAspectCheck = nullptr;
    QComboBox *m_alignCombo = nullptr;
    QCheckBox *m_wrapLinkCheck = nullptr;

    QLabel *m_imagePreviewLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTextEdit *m_htmlSnippetEdit = nullptr;
    QPushButton *m_copyHtmlBtn = nullptr;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    QPixmap m_previewPixmap;
    QSize m_naturalSize;
    bool m_syncingDimensions = false;
};
