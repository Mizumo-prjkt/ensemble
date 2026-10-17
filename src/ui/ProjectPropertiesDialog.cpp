#include "ProjectPropertiesDialog.h"
#include "ImageEmbedDialog.h"
#include "model/Ao3Project.h"
#include "utils/DiffUtil.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

static const QStringList kStandardRatings = {
    QStringLiteral("Not Rated"),
    QStringLiteral("General Audiences"),
    QStringLiteral("Teen And Up"),
    QStringLiteral("Mature"),
    QStringLiteral("Explicit")
};

static const QStringList kStandardWarnings = {
    QStringLiteral("Choose Not To Use Archive Warnings"),
    QStringLiteral("Graphic Depictions Of Violence"),
    QStringLiteral("Major Character Death"),
    QStringLiteral("No Archive Warnings Apply"),
    QStringLiteral("Rape/Non-Con"),
    QStringLiteral("Underage Sex")
};

static const QStringList kStandardCategories = {
    QStringLiteral("F/F"),
    QStringLiteral("F/M"),
    QStringLiteral("Gen"),
    QStringLiteral("M/M"),
    QStringLiteral("Multi"),
    QStringLiteral("Other")
};

ProjectPropertiesDialog::ProjectPropertiesDialog(Ao3Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
{
    setWindowTitle(QStringLiteral("Project Properties — %1").arg(m_project ? m_project->title() : QStringLiteral("Ensemble")));
    setMinimumSize(640, 520);
    resize(700, 560);

    setupUi();
}

void ProjectPropertiesDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(createMetadataTab(), QStringLiteral("📄 Work Metadata"));
    tabs->addTab(createTagsTab(), QStringLiteral("🏷️ AO3 Tags && Fandoms"));
    tabs->addTab(createImageEmbedsTab(), QStringLiteral("🖼️ Image Embeds"));
    tabs->addTab(createInfoTab(), QStringLiteral("📊 File Details"));
    tabs->addTab(createHistoryTab(), QStringLiteral("📸 Snapshots (history.cmt)"));
    mainLayout->addWidget(tabs);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, &ProjectPropertiesDialog::onSaveClicked);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btnBox);
}

QWidget *ProjectPropertiesDialog::createMetadataTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(10);

    m_titleEdit = new QLineEdit(widget);
    m_titleEdit->setPlaceholderText(QStringLiteral("Work title…"));
    if (m_project) {
        m_titleEdit->setText(m_project->title());
    }
    form->addRow(QStringLiteral("<b>Title:</b>"), m_titleEdit);

    m_authorEdit = new QLineEdit(widget);
    m_authorEdit->setPlaceholderText(QStringLiteral("Author / Pseud name…"));
    if (m_project) {
        m_authorEdit->setText(m_project->author());
    }
    form->addRow(QStringLiteral("<b>Author / Pseud:</b>"), m_authorEdit);

    m_ratingCombo = new QComboBox(widget);
    for (const auto &rating : kStandardRatings) {
        m_ratingCombo->addItem(rating);
    }
    if (m_project) {
        int idx = m_ratingCombo->findText(m_project->rating());
        if (idx >= 0) {
            m_ratingCombo->setCurrentIndex(idx);
        } else {
            m_ratingCombo->setCurrentText(m_project->rating());
        }
    }
    form->addRow(QStringLiteral("<b>Rating:</b>"), m_ratingCombo);

    layout->addLayout(form);

    // Categories group box
    auto *catGroup = new QGroupBox(QStringLiteral("Categories"), widget);
    auto *catLayout = new QGridLayout(catGroup);
    int catRow = 0;
    int catCol = 0;
    for (const auto &cat : kStandardCategories) {
        auto *cb = new QCheckBox(cat, catGroup);
        if (m_project && m_project->categories().contains(cat)) {
            cb->setChecked(true);
        }
        m_categoryBoxes.append(cb);
        catLayout->addWidget(cb, catRow, catCol);
        catCol++;
        if (catCol > 2) {
            catCol = 0;
            catRow++;
        }
    }
    layout->addWidget(catGroup);

    // Warnings group box
    auto *warnGroup = new QGroupBox(QStringLiteral("Archive Warnings"), widget);
    auto *warnLayout = new QVBoxLayout(warnGroup);
    for (const auto &warn : kStandardWarnings) {
        auto *cb = new QCheckBox(warn, warnGroup);
        if (m_project && m_project->archiveWarnings().contains(warn)) {
            cb->setChecked(true);
        }
        m_warningBoxes.append(cb);
        warnLayout->addWidget(cb);
    }
    layout->addWidget(warnGroup);

    layout->addStretch();
    return widget;
}

QWidget *ProjectPropertiesDialog::createTagsTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(12);

    m_fandomsEdit = new QLineEdit(widget);
    m_fandomsEdit->setPlaceholderText(QStringLiteral("e.g. Hatsune Miku, Vocaloid (comma-separated)"));
    if (m_project) {
        m_fandomsEdit->setText(m_project->fandoms().join(QStringLiteral(", ")));
    }
    form->addRow(QStringLiteral("<b>Fandoms:</b>"), m_fandomsEdit);

    m_relationshipsEdit = new QLineEdit(widget);
    m_relationshipsEdit->setPlaceholderText(QStringLiteral("e.g. Alice/Bob, Charlie & Dave (comma-separated)"));
    if (m_project) {
        m_relationshipsEdit->setText(m_project->relationships().join(QStringLiteral(", ")));
    }
    form->addRow(QStringLiteral("<b>Relationships:</b>"), m_relationshipsEdit);

    m_charactersEdit = new QLineEdit(widget);
    m_charactersEdit->setPlaceholderText(QStringLiteral("e.g. Alice, Bob, Charlie (comma-separated)"));
    if (m_project) {
        m_charactersEdit->setText(m_project->characters().join(QStringLiteral(", ")));
    }
    form->addRow(QStringLiteral("<b>Characters:</b>"), m_charactersEdit);

    m_tagsEdit = new QLineEdit(widget);
    m_tagsEdit->setPlaceholderText(QStringLiteral("e.g. Slow Burn, Fluff, Alternate Universe (comma-separated)"));
    if (m_project) {
        m_tagsEdit->setText(m_project->tags().join(QStringLiteral(", ")));
    }
    form->addRow(QStringLiteral("<b>Additional Tags:</b>"), m_tagsEdit);

    layout->addLayout(form);

    auto *hintLabel = new QLabel(
        QStringLiteral("<small style='color: gray;'>"
                       "Separate multiple tags using commas. Tags will be automatically saved in metadata.xml "
                       "and synchronized with the Lore Graph and story archive.</small>"), widget);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    layout->addStretch();
    return widget;
}

QWidget *ProjectPropertiesDialog::createInfoTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    auto *form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->setSpacing(10);

    const QString pathStr = (m_project && !m_project->filePath().isEmpty())
        ? m_project->filePath()
        : QStringLiteral("(Not saved yet / New Project)");
    m_filePathLabel = new QLabel(pathStr, widget);
    m_filePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("<b>File Path:</b>"), m_filePathLabel);

    const QString createdStr = (m_project && m_project->createdDate().isValid())
        ? m_project->createdDate().toLocalTime().toString(Qt::ISODate)
        : QStringLiteral("N/A");
    m_createdLabel = new QLabel(createdStr, widget);
    form->addRow(QStringLiteral("<b>Created Date:</b>"), m_createdLabel);

    const QString modStr = (m_project && m_project->modifiedDate().isValid())
        ? m_project->modifiedDate().toLocalTime().toString(Qt::ISODate)
        : QStringLiteral("N/A");
    m_modifiedLabel = new QLabel(modStr, widget);
    form->addRow(QStringLiteral("<b>Last Modified:</b>"), m_modifiedLabel);

    const QString byStr = (m_project && !m_project->lastModifiedBy().isEmpty())
        ? m_project->lastModifiedBy()
        : QStringLiteral("user");
    m_lastModifiedByLabel = new QLabel(byStr, widget);
    form->addRow(QStringLiteral("<b>Modified By:</b>"), m_lastModifiedByLabel);

    int totalWords = 0;
    int chapCount = 0;
    if (m_project) {
        chapCount = m_project->chapters().size();
        for (const auto &chap : m_project->chapters()) {
            const QString plain = chap.toPlainText();
            if (!plain.trimmed().isEmpty()) {
                totalWords += plain.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
            }
        }
    }
    m_statsLabel = new QLabel(QStringLiteral("%1 chapters, %2 total words").arg(chapCount).arg(totalWords), widget);
    form->addRow(QStringLiteral("<b>Statistics:</b>"), m_statsLabel);

    layout->addLayout(form);
    layout->addStretch();
    return widget;
}

QWidget *ProjectPropertiesDialog::createHistoryTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *topBar = new QHBoxLayout();
    auto *descLabel = new QLabel(QStringLiteral("Snapshot timeline stored in <code>history.cmt</code> and <code>history/</code>:"), widget);
    topBar->addWidget(descLabel);

    topBar->addStretch();

    auto *newCommitBtn = new QPushButton(QStringLiteral("➕ New Snapshot…"), widget);
    connect(newCommitBtn, &QPushButton::clicked, this, &ProjectPropertiesDialog::onNewCommitClicked);
    topBar->addWidget(newCommitBtn);

    layout->addLayout(topBar);

    auto *splitter = new QSplitter(Qt::Vertical, widget);

    m_commitTree = new QTreeWidget(splitter);
    m_commitTree->setHeaderLabels({
        QStringLiteral("Snapshot Hash"),
        QStringLiteral("Author"),
        QStringLiteral("Date"),
        QStringLiteral("Message")
    });
    m_commitTree->setRootIsDecorated(false);
    m_commitTree->setAlternatingRowColors(true);
    m_commitTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_commitTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_commitTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_commitTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    connect(m_commitTree, &QTreeWidget::itemSelectionChanged, this, &ProjectPropertiesDialog::onCommitSelected);
    splitter->addWidget(m_commitTree);

    m_commitDetailsEdit = new QTextEdit(splitter);
    m_commitDetailsEdit->setReadOnly(true);
    m_commitDetailsEdit->setFont(QFont(QStringLiteral("monospace"), 9));
    m_commitDetailsEdit->setPlaceholderText(QStringLiteral("Select a snapshot above to view full details and line-by-line diff…"));
    splitter->addWidget(m_commitDetailsEdit);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 3);

    layout->addWidget(splitter, 1);

    refreshHistoryList();
    return widget;
}

void ProjectPropertiesDialog::refreshHistoryList()
{
    m_commitTree->clear();
    if (!m_project) return;

    const QString log = m_project->historyLog();
    if (log.trimmed().isEmpty()) {
        m_commitDetailsEdit->setText(QStringLiteral("No snapshots recorded yet."));
        return;
    }

    // Split commits by "commit "
    const QStringList blocks = log.split(QStringLiteral("\ncommit "), Qt::SkipEmptyParts);
    for (int i = 0; i < blocks.size(); ++i) {
        QString block = blocks.at(i);
        if (i == 0 && block.startsWith(QStringLiteral("commit "))) {
            block = block.mid(7);
        }

        const QStringList lines = block.split(QLatin1Char('\n'));
        QString hash = lines.value(0).trimmed();
        QString author;
        QString date;
        QString msg;

        for (int j = 1; j < lines.size(); ++j) {
            const QString &line = lines.at(j);
            if (line.startsWith(QStringLiteral("Author: "))) {
                author = line.mid(8).trimmed();
            } else if (line.startsWith(QStringLiteral("Date:   "))) {
                date = line.mid(8).trimmed();
            } else if (line.startsWith(QStringLiteral("    "))) {
                if (!msg.isEmpty()) msg += QLatin1Char(' ');
                msg += line.mid(4).trimmed();
            }
        }

        auto *item = new QTreeWidgetItem(m_commitTree);
        item->setText(0, hash.left(12));
        item->setText(1, author);
        item->setText(2, DiffUtil::formatIsoDateWithLocal(date));
        item->setText(3, msg);
        item->setData(0, Qt::UserRole, block);
        item->setData(0, Qt::UserRole + 1, hash);
        item->setData(0, Qt::UserRole + 2, date);
    }

    if (m_commitTree->topLevelItemCount() > 0) {
        m_commitTree->setCurrentItem(m_commitTree->topLevelItem(0));
    }
}

void ProjectPropertiesDialog::onCommitSelected()
{
    auto *item = m_commitTree->currentItem();
    if (!item || !m_project) {
        m_commitDetailsEdit->clear();
        return;
    }

    const QString block = item->data(0, Qt::UserRole).toString();
    const QString fullHash = item->data(0, Qt::UserRole + 1).toString();
    const QString shortHash = item->text(0).trimmed();
    const QString rawDate = item->data(0, Qt::UserRole + 2).toString();

    // Look for matching diff in m_snapshotDiffs
    QString diffText = m_project->snapshotDiff(fullHash);
    if (diffText.isEmpty() && !shortHash.isEmpty()) {
        diffText = m_project->snapshotDiff(shortHash);
    }
    if (diffText.isEmpty()) {
        for (auto it = m_project->snapshotDiffs().constBegin(); it != m_project->snapshotDiffs().constEnd(); ++it) {
            if (it.key().startsWith(shortHash) || shortHash.startsWith(it.key())) {
                diffText = it.value();
                break;
            }
        }
    }

    QString html;
    html += QStringLiteral("<div style='font-family: monospace; font-size: 11px; line-height: 1.45;'>");

    // Format commit metadata
    const QStringList lines = block.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        if (l.startsWith(QStringLiteral("Author: "))) {
            html += QStringLiteral("<b>Author:</b> <span style='color: #38bdf8;'>%1</span><br/>").arg(l.mid(8).toHtmlEscaped());
        } else if (l.startsWith(QStringLiteral("Date:   "))) {
            html += QStringLiteral("<b>Date:</b>   <span style='color: #fbbf24;'>%1</span><br/>").arg(DiffUtil::formatIsoDateWithLocal(rawDate.isEmpty() ? l.mid(8).trimmed() : rawDate).toHtmlEscaped());
        } else if (l.startsWith(QStringLiteral("    "))) {
            html += QStringLiteral("<div style='margin: 8px 0; padding: 6px 10px; background: rgba(56,189,248,0.1); border-left: 3px solid #38bdf8; font-weight: bold;'>%1</div>").arg(l.trimmed().toHtmlEscaped());
        } else if (!l.trimmed().isEmpty()) {
            html += QStringLiteral("<b>snapshot <span style='color: #c084fc;'>%1</span></b><br/>").arg(l.toHtmlEscaped());
        }
    }

    html += QStringLiteral("<hr style='border: none; border-top: 1px solid #475569; margin: 10px 0;'/>");

    if (!diffText.isEmpty()) {
        html += QStringLiteral("<b style='color: #94a3b8;'>📄 Line-by-Line Changes (Diff):</b><br/><br/>");
        const QStringList diffLines = diffText.split(QLatin1Char('\n'));
        for (const QString &dl : diffLines) {
            if (dl.startsWith(QStringLiteral("+")) && !dl.startsWith(QStringLiteral("+++"))) {
                html += QStringLiteral("<div style='color: #4ade80; background: rgba(74, 222, 128, 0.12); padding: 1px 4px; white-space: pre-wrap;'>%1</div>").arg(dl.toHtmlEscaped());
            } else if (dl.startsWith(QStringLiteral("-")) && !dl.startsWith(QStringLiteral("---"))) {
                html += QStringLiteral("<div style='color: #f87171; background: rgba(248, 113, 113, 0.12); padding: 1px 4px; white-space: pre-wrap;'>%1</div>").arg(dl.toHtmlEscaped());
            } else if (dl.startsWith(QStringLiteral("@@"))) {
                html += QStringLiteral("<div style='color: #38bdf8; font-weight: bold; margin-top: 6px; padding: 1px 4px; background: rgba(56, 189, 248, 0.08); white-space: pre-wrap;'>%1</div>").arg(dl.toHtmlEscaped());
            } else if (dl.startsWith(QStringLiteral("---")) || dl.startsWith(QStringLiteral("+++"))) {
                html += QStringLiteral("<div style='color: #93c5fd; font-weight: bold; white-space: pre-wrap;'>%1</div>").arg(dl.toHtmlEscaped());
            } else if (dl.startsWith(QStringLiteral("#"))) {
                // Ignore internal header lines in diff view
                continue;
            } else {
                html += QStringLiteral("<div style='color: #94a3b8; padding: 1px 4px; white-space: pre-wrap;'>%1</div>").arg(dl.toHtmlEscaped());
            }
        }
    } else {
        html += QStringLiteral(
            "<div style='margin-top: 8px; padding: 12px; background: rgba(239, 68, 68, 0.08); border: 1px dashed #ef4444; border-radius: 6px; color: #fca5a5;'>"
            "⚠️ <strong>No diff snapshot available for this commit.</strong><br/>"
            "The diff file may have been deleted by an external software, or this file was created before snapshots were introduced to this feature."
            "</div>"
        );
    }

    html += QStringLiteral("</div>");
    m_commitDetailsEdit->setHtml(html);
}

void ProjectPropertiesDialog::onNewCommitClicked()
{
    if (!m_project) return;

    bool ok = false;
    const QString msg = QInputDialog::getText(
        this,
        QStringLiteral("Create Project Snapshot"),
        QStringLiteral("Snapshot message / description:"),
        QLineEdit::Normal,
        QString(),
        &ok);

    if (ok && !msg.trimmed().isEmpty()) {
        const QString authorTag = DiffUtil::defaultAuthorTag(m_authorEdit ? m_authorEdit->text().trimmed() : QString());
        m_project->appendHistoryCommit(authorTag, msg.trimmed());
        m_project->markDirtyBy(authorTag);
        refreshHistoryList();
    }
}

void ProjectPropertiesDialog::saveProperties()
{
    if (!m_project) return;

    bool changed = false;

    // Title
    const QString newTitle = m_titleEdit ? m_titleEdit->text().trimmed() : QString();
    if (!newTitle.isEmpty() && newTitle != m_project->title()) {
        m_project->setTitle(newTitle);
        changed = true;
    }

    // Author
    const QString newAuthor = m_authorEdit ? m_authorEdit->text().trimmed() : QString();
    if (!newAuthor.isEmpty() && newAuthor != m_project->author()) {
        m_project->setAuthor(newAuthor);
        changed = true;
    }

    // Rating
    if (m_ratingCombo) {
        const QString newRating = m_ratingCombo->currentText();
        if (newRating != m_project->rating()) {
            m_project->setRating(newRating);
            changed = true;
        }
    }

    // Categories
    QStringList newCats;
    for (auto *cb : m_categoryBoxes) {
        if (cb && cb->isChecked()) {
            newCats.append(cb->text());
        }
    }
    if (newCats != m_project->categories()) {
        m_project->setCategories(newCats);
        changed = true;
    }

    // Warnings
    QStringList newWarns;
    for (auto *cb : m_warningBoxes) {
        if (cb && cb->isChecked()) {
            newWarns.append(cb->text());
        }
    }
    if (newWarns != m_project->archiveWarnings()) {
        m_project->setArchiveWarnings(newWarns);
        changed = true;
    }

    // Helper for comma-separated tags
    auto parseCommaTags = [](const QString &text) {
        QStringList list;
        for (const QString &item : text.split(QLatin1Char(','))) {
            const QString trimmed = item.trimmed();
            if (!trimmed.isEmpty() && !list.contains(trimmed)) {
                list.append(trimmed);
            }
        }
        return list;
    };

    if (m_fandomsEdit) {
        const QStringList newFandoms = parseCommaTags(m_fandomsEdit->text());
        if (newFandoms != m_project->fandoms()) {
            m_project->setFandoms(newFandoms);
            changed = true;
        }
    }

    if (m_relationshipsEdit) {
        const QStringList newRels = parseCommaTags(m_relationshipsEdit->text());
        if (newRels != m_project->relationships()) {
            m_project->setRelationships(newRels);
            changed = true;
        }
    }

    if (m_charactersEdit) {
        const QStringList newChars = parseCommaTags(m_charactersEdit->text());
        if (newChars != m_project->characters()) {
            m_project->setCharacters(newChars);
            changed = true;
        }
    }

    if (m_tagsEdit) {
        const QStringList newTags = parseCommaTags(m_tagsEdit->text());
        if (newTags != m_project->tags()) {
            m_project->setTags(newTags);
            changed = true;
        }
    }

    if (changed) {
        m_project->markDirtyBy(QStringLiteral("user"));
        m_project->appendHistoryCommit(m_project->author(), QStringLiteral("Updated project properties and metadata"));
    }
}

void ProjectPropertiesDialog::onSaveClicked()
{
    saveProperties();
    accept();
}

QWidget *ProjectPropertiesDialog::createImageEmbedsTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *descLabel = new QLabel(QStringLiteral(
        "<b>External Image Attachments & Embeds:</b><br>"
        "Lists all external images embedded across chapters. AO3 requires images to be hosted on external URLs. "
        "Select an image below to copy its AO3 HTML snippet or open it in your browser."), widget);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QStringLiteral("color: #8be9fd; font-size: 11px;"));
    layout->addWidget(descLabel);

    m_imageTree = new QTreeWidget(widget);
    m_imageTree->setHeaderLabels({
        QStringLiteral("Chapter"),
        QStringLiteral("Image URL"),
        QStringLiteral("Alt Text"),
        QStringLiteral("Title"),
        QStringLiteral("Dimensions")
    });
    m_imageTree->setRootIsDecorated(false);
    m_imageTree->setAlternatingRowColors(true);
    m_imageTree->setSortingEnabled(true);
    m_imageTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_imageTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_imageTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_imageTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_imageTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    layout->addWidget(m_imageTree, 1);

    auto *btnRow = new QHBoxLayout();
    m_copyImageBtn = new QPushButton(QStringLiteral("📋 Copy AO3 HTML"), widget);
    m_copyImageBtn->setEnabled(false);
    connect(m_copyImageBtn, &QPushButton::clicked, this, &ProjectPropertiesDialog::onCopyImageEmbedHtml);

    m_openImageBtn = new QPushButton(QStringLiteral("🌐 Open in Browser"), widget);
    m_openImageBtn->setEnabled(false);
    connect(m_openImageBtn, &QPushButton::clicked, this, &ProjectPropertiesDialog::onOpenImageUrl);

    m_previewImageBtn = new QPushButton(QStringLiteral("🖼️ View / Edit Details…"), widget);
    m_previewImageBtn->setEnabled(false);
    connect(m_previewImageBtn, &QPushButton::clicked, this, &ProjectPropertiesDialog::onPreviewImageEmbed);

    auto *refreshBtn = new QPushButton(QStringLiteral("🔄 Refresh List"), widget);
    connect(refreshBtn, &QPushButton::clicked, this, &ProjectPropertiesDialog::refreshImageEmbeds);

    btnRow->addWidget(m_copyImageBtn);
    btnRow->addWidget(m_openImageBtn);
    btnRow->addWidget(m_previewImageBtn);
    btnRow->addStretch();
    btnRow->addWidget(refreshBtn);
    layout->addLayout(btnRow);

    connect(m_imageTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        const bool hasSel = !m_imageTree->selectedItems().isEmpty() &&
                            (m_imageTree->selectedItems().first()->flags() & Qt::ItemIsEnabled);
        m_copyImageBtn->setEnabled(hasSel);
        m_openImageBtn->setEnabled(hasSel);
        m_previewImageBtn->setEnabled(hasSel);
    });

    connect(m_imageTree, &QTreeWidget::itemDoubleClicked, this, &ProjectPropertiesDialog::onPreviewImageEmbed);

    refreshImageEmbeds();

    return widget;
}

void ProjectPropertiesDialog::refreshImageEmbeds()
{
    if (!m_imageTree) return;
    m_imageTree->clear();

    if (!m_project) return;

    static const QRegularExpression reImg(
        QStringLiteral(R"(<img\b([^>]*?)\/?>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSrc(
        QStringLiteral(R"(src\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reAlt(
        QStringLiteral(R"(alt\s*=\s*["']([^"']*)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reTitle(
        QStringLiteral(R"(title\s*=\s*["']([^"']*)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reWidth(
        QStringLiteral(R"(width\s*=\s*["']?(\d+)["']?)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reHeight(
        QStringLiteral(R"(height\s*=\s*["']?(\d+)["']?)"),
        QRegularExpression::CaseInsensitiveOption);

    int totalImages = 0;
    const auto &chapters = m_project->chapters();
    for (int i = 0; i < chapters.size(); ++i) {
        const auto &chap = chapters.at(i);
        const QString chapName = chap.title().isEmpty()
            ? QStringLiteral("Chapter %1").arg(i + 1)
            : QStringLiteral("%1. %2").arg(i + 1).arg(chap.title());

        auto it = reImg.globalMatch(chap.html());
        while (it.hasNext()) {
            const QString tagAttrs = it.next().captured(1);
            const auto mSrc = reSrc.match(tagAttrs);
            if (!mSrc.hasMatch()) continue;

            const QString url = mSrc.captured(1).trimmed();
            QString alt;
            const auto mAlt = reAlt.match(tagAttrs);
            if (mAlt.hasMatch()) alt = mAlt.captured(1);

            QString title;
            const auto mTitle = reTitle.match(tagAttrs);
            if (mTitle.hasMatch()) title = mTitle.captured(1);

            QString dims = QStringLiteral("Auto");
            int wVal = 0;
            int hVal = 0;
            const auto mW = reWidth.match(tagAttrs);
            const auto mH = reHeight.match(tagAttrs);
            if (mW.hasMatch()) wVal = mW.captured(1).toInt();
            if (mH.hasMatch()) hVal = mH.captured(1).toInt();
            if (wVal > 0 && hVal > 0) {
                dims = QStringLiteral("%1 × %2 px").arg(wVal).arg(hVal);
            } else if (wVal > 0) {
                dims = QStringLiteral("%1 px width").arg(wVal);
            }

            auto *item = new QTreeWidgetItem(m_imageTree);
            item->setText(0, chapName);
            item->setText(1, url);
            item->setText(2, alt);
            item->setText(3, title);
            item->setText(4, dims);

            item->setData(0, Qt::UserRole, i); // chapter index
            item->setData(1, Qt::UserRole, url);
            item->setData(2, Qt::UserRole, alt);
            item->setData(3, Qt::UserRole, title);
            item->setData(4, Qt::UserRole, wVal);
            item->setData(4, Qt::UserRole + 1, hVal);

            totalImages++;
        }
    }

    if (totalImages == 0) {
        auto *emptyItem = new QTreeWidgetItem(m_imageTree);
        emptyItem->setText(0, QStringLiteral("(No images)"));
        emptyItem->setText(1, QStringLiteral("No external image embeds found in any chapter."));
        emptyItem->setFlags(Qt::NoItemFlags);
    }
}

void ProjectPropertiesDialog::onCopyImageEmbedHtml()
{
    if (!m_imageTree || m_imageTree->selectedItems().isEmpty()) return;
    auto *item = m_imageTree->selectedItems().first();
    const QString url = item->data(1, Qt::UserRole).toString();
    if (url.isEmpty()) return;

    const QString alt = item->data(2, Qt::UserRole).toString();
    const QString title = item->data(3, Qt::UserRole).toString();
    const int w = item->data(4, Qt::UserRole).toInt();
    const int h = item->data(4, Qt::UserRole + 1).toInt();

    QString snippet = QStringLiteral("<img src=\"%1\"").arg(url);
    if (!alt.isEmpty()) snippet += QStringLiteral(" alt=\"%1\"").arg(alt);
    if (!title.isEmpty()) snippet += QStringLiteral(" title=\"%1\"").arg(title);
    if (w > 0) snippet += QStringLiteral(" width=\"%1\"").arg(w);
    if (h > 0) snippet += QStringLiteral(" height=\"%1\"").arg(h);
    snippet += QStringLiteral(" />");

    snippet = QStringLiteral("<p align=\"center\"><a href=\"%1\">%2</a></p>").arg(url, snippet);

    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(snippet);

    m_copyImageBtn->setText(QStringLiteral("✓ Copied AO3 HTML!"));
    QTimer::singleShot(2000, this, [this]() {
        if (m_copyImageBtn) {
            m_copyImageBtn->setText(QStringLiteral("📋 Copy AO3 HTML"));
        }
    });
}

void ProjectPropertiesDialog::onOpenImageUrl()
{
    if (!m_imageTree || m_imageTree->selectedItems().isEmpty()) return;
    auto *item = m_imageTree->selectedItems().first();
    const QString url = item->data(1, Qt::UserRole).toString();
    if (!url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(url));
    }
}

void ProjectPropertiesDialog::onPreviewImageEmbed()
{
    if (!m_imageTree || m_imageTree->selectedItems().isEmpty()) return;
    auto *item = m_imageTree->selectedItems().first();
    const QString url = item->data(1, Qt::UserRole).toString();
    if (url.isEmpty()) return;

    ImageEmbedDialog dlg(this);
    dlg.setImageUrl(url);
    dlg.setAltText(item->data(2, Qt::UserRole).toString());
    dlg.setTitleText(item->data(3, Qt::UserRole).toString());
    dlg.setImageWidth(item->data(4, Qt::UserRole).toInt());
    dlg.setImageHeight(item->data(4, Qt::UserRole + 1).toInt());
    dlg.exec();
}

