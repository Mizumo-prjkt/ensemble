#include "ImageEmbedDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

static QString escapeHtmlAttr(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == '&') {
            out += QStringLiteral("&amp;");
        } else if (c == '<') {
            out += QStringLiteral("&lt;");
        } else if (c == '>') {
            out += QStringLiteral("&gt;");
        } else if (c == '"') {
            out += QStringLiteral("&quot;");
        } else {
            out += c;
        }
    }
    return out;
}

ImageEmbedDialog::ImageEmbedDialog(QWidget *parent)
    : QDialog(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    setWindowTitle(QStringLiteral("Insert Image Embed (AO3 URL Attachment)"));
    setMinimumSize(560, 620);
    resize(640, 680);

    setupUi();

    connect(m_nam, &QNetworkAccessManager::finished, this, &ImageEmbedDialog::onReplyFinished);
}

ImageEmbedDialog::~ImageEmbedDialog()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

void ImageEmbedDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 14, 14, 14);
    mainLayout->setSpacing(12);

    // Tip Banner explaining why AO3 requires image URLs
    auto *banner = new QFrame(this);
    banner->setStyleSheet(QStringLiteral(
        "QFrame {"
        "  background-color: #282a36;"
        "  border: 1px solid #6272a4;"
        "  border-radius: 6px;"
        "  padding: 8px;"
        "}"));
    auto *bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(8, 6, 8, 6);
    auto *bannerText = new QLabel(QStringLiteral(
        "<b>ℹ️ AO3 Image Requirement:</b> Archive of Our Own does not host image files directly. "
        "Images must be hosted online (e.g., Postimages, Imgur, personal host) and embedded via URL. "
        "The generated HTML code is 100% AO3-compliant and ready to paste into the AO3 story box."), banner);
    bannerText->setWordWrap(true);
    bannerText->setStyleSheet(QStringLiteral("color: #8be9fd; font-size: 11px;"));
    bannerLayout->addWidget(bannerText);
    mainLayout->addWidget(banner);

    // Group 1: Source & Accessibility
    auto *sourceGroup = new QGroupBox(QStringLiteral("Image Source & Details"), this);
    auto *sourceForm = new QFormLayout(sourceGroup);
    sourceForm->setLabelAlignment(Qt::AlignRight);
    sourceForm->setSpacing(10);

    // URL row with Load Preview button
    auto *urlRow = new QHBoxLayout();
    m_urlEdit = new QLineEdit(sourceGroup);
    m_urlEdit->setPlaceholderText(QStringLiteral("https://example.com/illustration.png"));
    m_urlEdit->setClearButtonEnabled(true);
    connect(m_urlEdit, &QLineEdit::textChanged, this, &ImageEmbedDialog::onUrlChanged);
    connect(m_urlEdit, &QLineEdit::returnPressed, this, &ImageEmbedDialog::onFetchPreview);

    m_previewBtn = new QPushButton(QStringLiteral("Test / Fetch"), sourceGroup);
    m_previewBtn->setToolTip(QStringLiteral("Download remote image to preview dimensions and appearance"));
    connect(m_previewBtn, &QPushButton::clicked, this, &ImageEmbedDialog::onFetchPreview);

    urlRow->addWidget(m_urlEdit, 1);
    urlRow->addWidget(m_previewBtn);
    sourceForm->addRow(QStringLiteral("<b>Image URL:</b>"), urlRow);

    // Alt text
    m_altEdit = new QLineEdit(sourceGroup);
    m_altEdit->setPlaceholderText(QStringLiteral("Brief description of image for screen readers & AO3 accessibility"));
    connect(m_altEdit, &QLineEdit::textChanged, this, &ImageEmbedDialog::updateAo3HtmlPreview);
    sourceForm->addRow(QStringLiteral("Alt Text:"), m_altEdit);

    // Title text
    m_titleEdit = new QLineEdit(sourceGroup);
    m_titleEdit->setPlaceholderText(QStringLiteral("Optional hover tooltip text"));
    connect(m_titleEdit, &QLineEdit::textChanged, this, &ImageEmbedDialog::updateAo3HtmlPreview);
    sourceForm->addRow(QStringLiteral("Hover Title:"), m_titleEdit);

    mainLayout->addWidget(sourceGroup);

    // Group 2: Display & Formatting
    auto *formatGroup = new QGroupBox(QStringLiteral("Layout & Dimensions"), this);
    auto *formatForm = new QFormLayout(formatGroup);
    formatForm->setLabelAlignment(Qt::AlignRight);
    formatForm->setSpacing(10);

    // Alignment combo
    m_alignCombo = new QComboBox(formatGroup);
    m_alignCombo->addItem(QStringLiteral("Centered (Recommended on AO3)"), QStringLiteral("center"));
    m_alignCombo->addItem(QStringLiteral("Inline (Flowing with text)"), QStringLiteral("inline"));
    m_alignCombo->addItem(QStringLiteral("Left-aligned"), QStringLiteral("left"));
    m_alignCombo->addItem(QStringLiteral("Right-aligned"), QStringLiteral("right"));
    connect(m_alignCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageEmbedDialog::updateAo3HtmlPreview);
    formatForm->addRow(QStringLiteral("Alignment:"), m_alignCombo);

    // Dimensions row
    auto *dimRow = new QHBoxLayout();
    m_widthSpin = new QSpinBox(formatGroup);
    m_widthSpin->setRange(0, 9999);
    m_widthSpin->setSpecialValueText(QStringLiteral("Auto / 100%"));
    m_widthSpin->setSuffix(QStringLiteral(" px"));
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImageEmbedDialog::onWidthChanged);

    m_heightSpin = new QSpinBox(formatGroup);
    m_heightSpin->setRange(0, 9999);
    m_heightSpin->setSpecialValueText(QStringLiteral("Auto"));
    m_heightSpin->setSuffix(QStringLiteral(" px"));
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImageEmbedDialog::onHeightChanged);

    m_keepAspectCheck = new QCheckBox(QStringLiteral("Lock Aspect Ratio"), formatGroup);
    m_keepAspectCheck->setChecked(true);

    dimRow->addWidget(new QLabel(QStringLiteral("Width:"), formatGroup));
    dimRow->addWidget(m_widthSpin);
    dimRow->addSpacing(10);
    dimRow->addWidget(new QLabel(QStringLiteral("Height:"), formatGroup));
    dimRow->addWidget(m_heightSpin);
    dimRow->addSpacing(10);
    dimRow->addWidget(m_keepAspectCheck);
    dimRow->addStretch();
    formatForm->addRow(QStringLiteral("Dimensions:"), dimRow);

    // Wrap in link
    m_wrapLinkCheck = new QCheckBox(QStringLiteral("Make image clickable (links to full-resolution image URL)"), formatGroup);
    m_wrapLinkCheck->setChecked(true);
    connect(m_wrapLinkCheck, &QCheckBox::toggled, this, &ImageEmbedDialog::updateAo3HtmlPreview);
    formatForm->addRow(QStringLiteral("Links:"), m_wrapLinkCheck);

    mainLayout->addWidget(formatGroup);

    // Group 3: Live Preview & Status
    auto *previewGroup = new QGroupBox(QStringLiteral("Live Preview"), this);
    auto *previewLayout = new QVBoxLayout(previewGroup);
    previewLayout->setSpacing(8);

    m_statusLabel = new QLabel(QStringLiteral("Paste or type a valid image URL (http:// or https://) above."), previewGroup);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #6272a4; font-size: 11px;"));
    previewLayout->addWidget(m_statusLabel);

    auto *previewFrame = new QFrame(previewGroup);
    previewFrame->setStyleSheet(QStringLiteral(
        "QFrame {"
        "  background-color: #1e1f29;"
        "  border: 1px dashed #6272a4;"
        "  border-radius: 6px;"
        "  min-height: 120px;"
        "}"));
    auto *frameLayout = new QVBoxLayout(previewFrame);
    frameLayout->setContentsMargins(4, 4, 4, 4);

    m_imagePreviewLabel = new QLabel(QStringLiteral("<i style=\"color:#6272a4;\">No preview loaded yet</i>"), previewFrame);
    m_imagePreviewLabel->setAlignment(Qt::AlignCenter);
    m_imagePreviewLabel->setTextFormat(Qt::RichText);
    frameLayout->addWidget(m_imagePreviewLabel);
    previewLayout->addWidget(previewFrame);

    mainLayout->addWidget(previewGroup);

    // Group 4: AO3 HTML Code Snippet
    auto *htmlGroup = new QGroupBox(QStringLiteral("AO3 Story Box HTML Code"), this);
    auto *htmlLayout = new QVBoxLayout(htmlGroup);
    htmlLayout->setSpacing(6);

    m_htmlSnippetEdit = new QTextEdit(htmlGroup);
    m_htmlSnippetEdit->setReadOnly(true);
    m_htmlSnippetEdit->setMaximumHeight(65);
    m_htmlSnippetEdit->setStyleSheet(QStringLiteral(
        "background-color: #191a21; color: #50fa7b; font-family: monospace; font-size: 11px; border-radius: 4px;"));
    htmlLayout->addWidget(m_htmlSnippetEdit);

    auto *copyRow = new QHBoxLayout();
    m_copyHtmlBtn = new QPushButton(QStringLiteral("📋 Copy AO3 HTML Snippet"), htmlGroup);
    m_copyHtmlBtn->setToolTip(QStringLiteral("Copy this HTML snippet to paste into AO3's web editor"));
    connect(m_copyHtmlBtn, &QPushButton::clicked, this, &ImageEmbedDialog::onCopyAo3Html);
    copyRow->addStretch();
    copyRow->addWidget(m_copyHtmlBtn);
    htmlLayout->addLayout(copyRow);

    mainLayout->addWidget(htmlGroup);

    // Dialog Button Box
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Insert Image"));
    btnBox->button(QDialogButtonBox::Ok)->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: #50fa7b; color: #282a36; font-weight: bold; padding: 6px 14px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #69ff94; }"));
    connect(btnBox, &QDialogButtonBox::accepted, this, &ImageEmbedDialog::onAccept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btnBox);

    updateAo3HtmlPreview();
}

void ImageEmbedDialog::setImageUrl(const QString &url)
{
    if (m_urlEdit) {
        m_urlEdit->setText(url);
    }
}

void ImageEmbedDialog::setAltText(const QString &alt)
{
    if (m_altEdit) {
        m_altEdit->setText(alt);
    }
}

void ImageEmbedDialog::setTitleText(const QString &title)
{
    if (m_titleEdit) {
        m_titleEdit->setText(title);
    }
}

void ImageEmbedDialog::setImageWidth(int width)
{
    if (m_widthSpin) {
        m_widthSpin->setValue(width);
    }
}

void ImageEmbedDialog::setImageHeight(int height)
{
    if (m_heightSpin) {
        m_heightSpin->setValue(height);
    }
}

void ImageEmbedDialog::setAlignment(const QString &align)
{
    if (!m_alignCombo) return;
    int idx = m_alignCombo->findData(align);
    if (idx >= 0) {
        m_alignCombo->setCurrentIndex(idx);
    }
}

void ImageEmbedDialog::setWrapInLink(bool wrap)
{
    if (m_wrapLinkCheck) {
        m_wrapLinkCheck->setChecked(wrap);
    }
}

QString ImageEmbedDialog::imageUrl() const
{
    return m_urlEdit ? m_urlEdit->text().trimmed() : QString();
}

QString ImageEmbedDialog::altText() const
{
    return m_altEdit ? m_altEdit->text().trimmed() : QString();
}

QString ImageEmbedDialog::titleText() const
{
    return m_titleEdit ? m_titleEdit->text().trimmed() : QString();
}

int ImageEmbedDialog::imageWidth() const
{
    return m_widthSpin ? m_widthSpin->value() : 0;
}

int ImageEmbedDialog::imageHeight() const
{
    return m_heightSpin ? m_heightSpin->value() : 0;
}

QString ImageEmbedDialog::alignment() const
{
    return m_alignCombo ? m_alignCombo->currentData().toString() : QStringLiteral("center");
}

bool ImageEmbedDialog::wrapInLink() const
{
    return m_wrapLinkCheck && m_wrapLinkCheck->isChecked();
}

void ImageEmbedDialog::onUrlChanged()
{
    updateAo3HtmlPreview();

    // Auto-fetch preview after a small pause if URL looks like a complete web URL
    const QString url = imageUrl();
    if (url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
        url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        if (url.contains(QLatin1Char('.'))) {
            QTimer::singleShot(500, this, [this, url]() {
                if (imageUrl() == url && (!m_currentReply || m_currentReply->isFinished())) {
                    onFetchPreview();
                }
            });
        }
    }
}

void ImageEmbedDialog::onFetchPreview()
{
    const QString url = imageUrl();
    if (url.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("<span style=\"color:#ff5555;\">Please enter an image URL first.</span>"));
        return;
    }

    if (!url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
        !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        m_statusLabel->setText(QStringLiteral("<span style=\"color:#ffb86c;\">Warning: AO3 image URLs must start with https:// or http://</span>"));
        return;
    }

    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    m_statusLabel->setText(QStringLiteral("<span style=\"color:#f1fa8c;\">⏳ Downloading preview from host...</span>"));

    const QUrl qurl(url);
    QNetworkRequest request(qurl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("EnsembleAO3Typewriter/1.0 (Image Embed Preview)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_currentReply = m_nam->get(request);
}

void ImageEmbedDialog::onReplyFinished(QNetworkReply *reply)
{
    if (reply != m_currentReply) {
        reply->deleteLater();
        return;
    }
    m_currentReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError) {
            m_statusLabel->setText(QStringLiteral("<span style=\"color:#ff5555;\">⚠️ Failed to load image: %1</span>").arg(reply->errorString()));
            m_imagePreviewLabel->setText(QStringLiteral("<i style=\"color:#ff5555;\">Image could not be downloaded</i>"));
        }
        reply->deleteLater();
        return;
    }

    const QByteArray data = reply->readAll();
    reply->deleteLater();

    QPixmap pixmap;
    if (!pixmap.loadFromData(data)) {
        m_statusLabel->setText(QStringLiteral("<span style=\"color:#ff5555;\">⚠️ Remote URL did not return a valid image format.</span>"));
        m_imagePreviewLabel->setText(QStringLiteral("<i style=\"color:#ff5555;\">Not a valid image</i>"));
        return;
    }

    m_previewPixmap = pixmap;
    m_naturalSize = pixmap.size();

    m_statusLabel->setText(QStringLiteral("<span style=\"color:#50fa7b;\">✓ Image loaded: %1 × %2 px (%3 KB)</span>")
                               .arg(QString::number(m_naturalSize.width()),
                                    QString::number(m_naturalSize.height()),
                                    QString::number(data.size() / 1024)));

    // Scale pixmap for thumbnail display up to 140px height
    const QPixmap scaled = pixmap.scaled(QSize(400, 140), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imagePreviewLabel->setPixmap(scaled);

    // If width and height are currently 0, initialize them with natural dimensions if under 800px, or 800px
    if (m_widthSpin && m_heightSpin && m_widthSpin->value() == 0 && m_heightSpin->value() == 0) {
        m_syncingDimensions = true;
        if (m_naturalSize.width() > 800) {
            m_widthSpin->setValue(800);
            const int scaledH = qRound(800.0 * m_naturalSize.height() / m_naturalSize.width());
            m_heightSpin->setValue(scaledH);
        } else {
            m_widthSpin->setValue(m_naturalSize.width());
            m_heightSpin->setValue(m_naturalSize.height());
        }
        m_syncingDimensions = false;
        updateAo3HtmlPreview();
    }
}

void ImageEmbedDialog::onWidthChanged(int w)
{
    if (m_syncingDimensions) return;
    if (m_keepAspectCheck && m_keepAspectCheck->isChecked() && m_naturalSize.isValid() && m_naturalSize.width() > 0) {
        m_syncingDimensions = true;
        if (w > 0) {
            const int newH = qRound(static_cast<double>(w) * m_naturalSize.height() / m_naturalSize.width());
            m_heightSpin->setValue(newH);
        } else {
            m_heightSpin->setValue(0);
        }
        m_syncingDimensions = false;
    }
    updateAo3HtmlPreview();
}

void ImageEmbedDialog::onHeightChanged(int h)
{
    if (m_syncingDimensions) return;
    if (m_keepAspectCheck && m_keepAspectCheck->isChecked() && m_naturalSize.isValid() && m_naturalSize.height() > 0) {
        m_syncingDimensions = true;
        if (h > 0) {
            const int newW = qRound(static_cast<double>(h) * m_naturalSize.width() / m_naturalSize.height());
            m_widthSpin->setValue(newW);
        } else {
            m_widthSpin->setValue(0);
        }
        m_syncingDimensions = false;
    }
    updateAo3HtmlPreview();
}

QString ImageEmbedDialog::generateAo3Html() const
{
    const QString url = imageUrl();
    if (url.isEmpty()) {
        return QString();
    }

    QString img = QStringLiteral("<img src=\"%1\"").arg(escapeHtmlAttr(url));
    const QString alt = altText();
    if (!alt.isEmpty()) {
        img += QStringLiteral(" alt=\"%1\"").arg(escapeHtmlAttr(alt));
    }
    const QString title = titleText();
    if (!title.isEmpty()) {
        img += QStringLiteral(" title=\"%1\"").arg(escapeHtmlAttr(title));
    }
    const int w = imageWidth();
    if (w > 0) {
        img += QStringLiteral(" width=\"%1\"").arg(w);
    }
    const int h = imageHeight();
    if (h > 0) {
        img += QStringLiteral(" height=\"%1\"").arg(h);
    }
    img += QStringLiteral(" />");

    if (wrapInLink()) {
        img = QStringLiteral("<a href=\"%1\">%2</a>").arg(escapeHtmlAttr(url), img);
    }

    const QString align = alignment();
    if (align == QStringLiteral("center")) {
        return QStringLiteral("<p align=\"center\">%1</p>").arg(img);
    } else if (align == QStringLiteral("left")) {
        return QStringLiteral("<p align=\"left\">%1</p>").arg(img);
    } else if (align == QStringLiteral("right")) {
        return QStringLiteral("<p align=\"right\">%1</p>").arg(img);
    }

    return img;
}

void ImageEmbedDialog::updateAo3HtmlPreview()
{
    if (m_htmlSnippetEdit) {
        const QString snippet = generateAo3Html();
        m_htmlSnippetEdit->setPlainText(snippet.isEmpty() ? QStringLiteral("<img src=\"...\" alt=\"...\" />") : snippet);
    }
}

void ImageEmbedDialog::onCopyAo3Html()
{
    const QString snippet = generateAo3Html();
    if (snippet.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No Image URL"),
                                 QStringLiteral("Please enter an image URL to generate and copy the AO3 HTML snippet."));
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(snippet);

    m_copyHtmlBtn->setText(QStringLiteral("✓ Copied to Clipboard!"));
    QTimer::singleShot(2000, this, [this]() {
        if (m_copyHtmlBtn) {
            m_copyHtmlBtn->setText(QStringLiteral("📋 Copy AO3 HTML Snippet"));
        }
    });
}

void ImageEmbedDialog::onAccept()
{
    const QString url = imageUrl();
    if (url.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Missing Image URL"),
                             QStringLiteral("Please enter an image URL before inserting."));
        if (m_urlEdit) {
            m_urlEdit->setFocus();
        }
        return;
    }

    if (!url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
        !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        auto ret = QMessageBox::question(
            this, QStringLiteral("Non-Standard Image URL"),
            QStringLiteral("The image URL '%1' does not begin with https:// or http://.\n\n"
                           "AO3 requires public HTTP/HTTPS URLs to display images properly.\n"
                           "Do you want to insert it anyway?").arg(url),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            return;
        }
    }

    accept();
}
