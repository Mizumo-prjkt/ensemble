#include "CreditsDialog.h"
#include "AppIcon.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

CreditsDialog::CreditsDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(
      QStringLiteral("Open Source & Third-Party Credits — Ensemble"));
  setWindowIcon(AppIcon::icon());
  setModal(true);
  resize(860, 560);
  setMinimumSize(700, 460);

  setStyleSheet(QStringLiteral(
      "QDialog {"
      "  background-color: #14151a;"
      "  color: #f8f8f2;"
      "  font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;"
      "}"
      "QLineEdit {"
      "  background-color: #21222c;"
      "  color: #f8f8f2;"
      "  border: 1px solid #343746;"
      "  border-radius: 6px;"
      "  padding: 6px 12px;"
      "  font-size: 12px;"
      "}"
      "QLineEdit:focus {"
      "  border-color: #bd93f9;"
      "}"
      "QListWidget {"
      "  background-color: #1a1b23;"
      "  border: 1px solid #282a36;"
      "  border-radius: 8px;"
      "  padding: 4px;"
      "  color: #f8f8f2;"
      "}"
      "QListWidget::item {"
      "  padding: 8px 10px;"
      "  border-radius: 6px;"
      "  margin-bottom: 2px;"
      "}"
      "QListWidget::item:hover {"
      "  background-color: #282a36;"
      "  color: #50fa7b;"
      "}"
      "QListWidget::item:selected {"
      "  background-color: #3b335a;"
      "  color: #bd93f9;"
      "  font-weight: 600;"
      "}"
      "QFrame#detailCard {"
      "  background-color: #1a1b23;"
      "  border: 1px solid #282a36;"
      "  border-radius: 8px;"
      "}"
      "QPlainTextEdit {"
      "  background-color: #14151a;"
      "  color: #d4d4d4;"
      "  border: 1px solid #282a36;"
      "  border-radius: 6px;"
      "  padding: 8px;"
      "  font-family: 'Consolas', 'Monospace', monospace;"
      "  font-size: 11px;"
      "  line-height: 1.4;"
      "}"
      "QPushButton {"
      "  background-color: #282a36;"
      "  color: #f8f8f2;"
      "  border: 1px solid #44475a;"
      "  border-radius: 6px;"
      "  padding: 6px 14px;"
      "  font-size: 12px;"
      "  font-weight: 600;"
      "}"
      "QPushButton:hover {"
      "  background-color: #343746;"
      "  border-color: #6272a4;"
      "  color: #50fa7b;"
      "}"
      "QPushButton#closeBtn {"
      "  background-color: #7f5af0;"
      "  color: #ffffff;"
      "  border: none;"
      "  padding: 7px 20px;"
      "  font-weight: bold;"
      "}"
      "QPushButton#closeBtn:hover {"
      "  background-color: #6c47d9;"
      "}"));

  populateEntries();
  setupUi();

  if (!m_entries.isEmpty()) {
    m_listWidget->setCurrentRow(0);
  }
}

void CreditsDialog::populateEntries() {
  m_entries.clear();

  // 1. Qt Framework
  {
    CreditEntry e;
    e.name = QStringLiteral("Qt Framework (Qt 6)");
    e.version = QStringLiteral("6.8.x");
    e.author = QStringLiteral("The Qt Company & The Qt Project");
    e.licenseType = QStringLiteral("LGPLv3 / Commercial");
    e.websiteUrl = QStringLiteral("https://www.qt.io");
    e.description = QStringLiteral(
        "Cross-platform GUI framework, QtCore runtime, QtWidgets component "
        "ecosystem, QtNetwork access, and signal/slot architecture.");
    e.licenseText = QStringLiteral(
        "GNU LESSER GENERAL PUBLIC LICENSE\n"
        "Version 3, 29 June 2007\n\n"
        "Copyright (C) 2007 Free Software Foundation, Inc. <https://fsf.org/>\n"
        "Everyone is permitted to copy and distribute verbatim copies\n"
        "of this license document, but changing it is not allowed.\n\n"
        "This version of the GNU Lesser General Public License incorporates\n"
        "the terms and conditions of version 3 of the GNU General Public\n"
        "License, supplemented by the additional permissions listed below.\n\n"
        "0. Additional Definitions.\n"
        "As used herein, \"this License\" refers to version 3 of the GNU "
        "Lesser\n"
        "General Public License, and the \"GNU GPL\" refers to version 3 of "
        "the GNU\n"
        "General Public License.\n\n"
        "\"The Library\" refers to a covered work governed by this License, "
        "other\n"
        "than an Application or a Combined Work as defined below...\n");
    m_entries.append(e);
  }

  // 2. Chromium & Qt WebEngine
  {
    CreditEntry e;
    e.name = QStringLiteral("Chromium & Qt WebEngine");
    e.version = QStringLiteral("Chromium 122+ Embedded");
    e.author = QStringLiteral("The Chromium Authors & The Qt Company");
    e.licenseType = QStringLiteral("BSD 3-Clause / LGPLv3");
    e.websiteUrl = QStringLiteral("https://www.chromium.org");
    e.description = QStringLiteral(
        "Chromium-based high-fidelity web engine rendering authentic live AO3 "
        "HTML preview, work skin CSS styles, and export layout rendering.");
    e.licenseText = QStringLiteral(
        "// Copyright 2015 The Chromium Authors\n"
        "//\n"
        "// Redistribution and use in source and binary forms, with or "
        "without\n"
        "// modification, are permitted provided that the following conditions "
        "are\n"
        "// met:\n"
        "//\n"
        "//    * Redistributions of source code must retain the above "
        "copyright\n"
        "// notice, this list of conditions and the following disclaimer.\n"
        "//    * Redistributions in binary form must reproduce the above\n"
        "// copyright notice, this list of conditions and the following "
        "disclaimer\n"
        "// in the documentation and/or other materials provided with the\n"
        "// distribution.\n"
        "//    * Neither the name of Google LLC nor the names of its\n"
        "// contributors may be used to endorse or promote products derived "
        "from\n"
        "// this software without specific prior written permission.\n"
        "//\n"
        "// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND "
        "CONTRIBUTORS\n"
        "// \"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT "
        "NOT\n"
        "// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS "
        "FOR\n"
        "// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE "
        "COPYRIGHT\n"
        "// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, "
        "INCIDENTAL,\n"
        "// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES...\n");
    m_entries.append(e);
  }

  /*
      Miniz won't get credits temporarily until confirmation that source code
     really came from Gemini's trained datasets and was applied. Or it thinks
     it came from miniz or a derivative and just went on to compose a miniz
     clone.

      The Code, from src/utils/miniz.cpp and its header are Autogen by
     Gemini 3.7 Flash and was applied in the project. The License is still
     unknown, but likely MIT. Further investigation is required.

      Features and also runtime behaviors are not yet properly observed as of
     writing.

     So this code block is commented out until everything concludes.

     Mizumo-prjkt: This actually scares me, unlike Claude who tend to suggest
                    cloning the repo and get the source there. Gemini, at least
                    in 3.7 Flash model, just came up with the Compression
     algorithm (???) I guess it actually did its job, but further studies need
     to be checked.

  */
  // 3. miniz
  // {
  //     CreditEntry e;
  //     e.name = QStringLiteral("miniz (Embedded ZIP & Deflate)");
  //     e.version = QStringLiteral("3.0.2");
  //     e.author = QStringLiteral("Rich Geldreich & Tenacious Software LLC");
  //     e.licenseType = QStringLiteral("MIT License / Public Domain");
  //     e.websiteUrl = QStringLiteral("https://github.com/richgel999/miniz");
  //     e.description = QStringLiteral("In-process lossless Deflate data
  //     compression and ZIP archive engine powering .ao3proj project files with
  //     zero external tool dependencies."); e.licenseText = QStringLiteral(
  //         "MIT License\n\n"
  //         "Copyright (c) 2013-2022 Rich Geldreich and Tenacious Software
  //         LLC\n" "Copyright (c) 2022-2024 miniz authors\n\n" "Permission is
  //         hereby granted, free of charge, to any person obtaining a copy\n"
  //         "of this software and associated documentation files (the
  //         \"Software\"), to deal\n" "in the Software without restriction,
  //         including without limitation the rights\n" "to use, copy, modify,
  //         merge, publish, distribute, sublicense, and/or sell\n" "copies of
  //         the Software, and to permit persons to whom the Software is\n"
  //         "furnished to do so, subject to the following conditions:\n\n"
  //         "The above copyright notice and this permission notice shall be
  //         included in all\n" "copies or substantial portions of the
  //         Software.\n\n" "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT
  //         WARRANTY OF ANY KIND, EXPRESS OR\n" "IMPLIED, INCLUDING BUT NOT
  //         LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n" "FITNESS FOR A
  //         PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
  //         "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
  //         OTHER\n" "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
  //         OTHERWISE, ARISING FROM,\n" "OUT OF OR IN CONNECTION WITH THE
  //         SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n" "SOFTWARE.\n"
  //     );
  //     m_entries.append(e);
  // }

  // 4. Zstandard (zstd)
  {
    CreditEntry e;
    e.name = QStringLiteral("Zstandard (zstd)");
    e.version = QStringLiteral("1.5.x Specification");
    e.author = QStringLiteral("Meta Platforms, Inc. / Yann Collet");
    e.licenseType = QStringLiteral("BSD 3-Clause License");
    e.websiteUrl = QStringLiteral("https://facebook.github.io/zstd/");
    e.description = QStringLiteral(
        "Fast real-time compression algorithm specification used for .ao3proj "
        "archive container architecture and legacy format detection.");
    e.licenseText = QStringLiteral(
        "BSD License\n\n"
        "For Zstandard software\n\n"
        "Copyright (c) 2016-present, Meta Platforms, Inc. and affiliates.\n"
        "All rights reserved.\n\n"
        "Redistribution and use in source and binary forms, with or without "
        "modification,\n"
        "are permitted provided that the following conditions are met:\n\n"
        " * Redistributions of source code must retain the above copyright "
        "notice, this\n"
        "   list of conditions and the following disclaimer.\n\n"
        " * Redistributions in binary form must reproduce the above copyright "
        "notice,\n"
        "   this list of conditions and the following disclaimer in the "
        "documentation\n"
        "   and/or other materials provided with the distribution.\n\n"
        " * Neither the name Facebook nor the names of its contributors may be "
        "used to\n"
        "   endorse or promote products derived from this software without "
        "specific\n"
        "   prior written permission.\n\n"
        "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "
        "\"AS IS\" AND\n"
        "ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE "
        "IMPLIED\n"
        "WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE "
        "ARE\n"
        "DISCLAIMED...");
    m_entries.append(e);
  }

  // 5. Archive of Our Own (AO3) / OTW
  {
    CreditEntry e;
    e.name = QStringLiteral("Archive of Our Own (AO3) / OTW");
    e.version = QStringLiteral("OTW Archive Specification");
    e.author = QStringLiteral("Organization for Transformative Works (OTW)");
    e.licenseType = QStringLiteral("GPLv2 / CC-BY-NC");
    e.websiteUrl = QStringLiteral("https://archiveofourown.org");
    e.description =
        QStringLiteral("The open archive project whose HTML sanitization "
                       "rules, allowed tags, and Work Skin styling ecosystem "
                       "form the foundation for Ensemble's design.");
    e.licenseText = QStringLiteral(
        "Archive of Our Own (AO3) is an open-source non-commercial fanfiction "
        "platform\n"
        "created by the Organization for Transformative Works (OTW).\n\n"
        "Ensemble is an independent fanfiction typewriter suite and is not "
        "officially affiliated\n"
        "with or endorsed by the Organization for Transformative Works "
        "(OTW).\n\n"
        "AO3 code is released under the GNU General Public License (GPL) "
        "version 2.\n"
        "Work skin specifications and default stylesheets are property of "
        "their respective\n"
        "authors and the OTW open community.\n");
    m_entries.append(e);
  }

  // 6. Inter Typography
  {
    CreditEntry e;
    e.name = QStringLiteral("Inter Font Family");
    e.version = QStringLiteral("4.0");
    e.author = QStringLiteral("Rasmus Andersson");
    e.licenseType = QStringLiteral("SIL Open Font License 1.1");
    e.websiteUrl = QStringLiteral("https://rsms.me/inter/");
    e.description = QStringLiteral(
        "Modern screen-optimized typography design system used across editor "
        "headers, status indicators, and UI controls.");
    e.licenseText = QStringLiteral(
        "SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007\n\n"
        "Copyright (c) 2016-2024 The Inter Project Authors "
        "(https://github.com/rsms/inter)\n\n"
        "Permission is hereby granted, free of charge, to any person "
        "obtaining\n"
        "a copy of the Font Software, to use, study, copy, merge, embed, "
        "modify,\n"
        "redistribute, and sell modified and unmodified copies of the Font "
        "Software,\n"
        "subject to the following conditions...\n");
    m_entries.append(e);
  }

  // 7. Dracula Theme
  {
    CreditEntry e;
    e.name = QStringLiteral("Dracula Theme Color Palette");
    e.version = QStringLiteral("Theme Spec");
    e.author = QStringLiteral("Zeno Rocha & Dracula Theme Contributors");
    e.licenseType = QStringLiteral("MIT License");
    e.websiteUrl = QStringLiteral("https://draculatheme.com");
    e.description = QStringLiteral(
        "High-contrast dark theme color tokens and syntax highlighting "
        "aesthetics used across code editors and diagnostic panels.");
    e.licenseText = QStringLiteral(
        "MIT License\n\n"
        "Copyright (c) 2013-present Dracula Theme\n\n"
        "Permission is hereby granted, free of charge, to any person obtaining "
        "a copy\n"
        "of this software and associated documentation files (the "
        "\"Software\"), to deal\n"
        "in the Software without restriction, including without limitation the "
        "rights\n"
        "to use, copy, modify, merge, publish, distribute, sublicense, and/or "
        "sell\n"
        "copies of the Software, and to permit persons to whom the Software "
        "is\n"
        "furnished to do so, subject to the following conditions...\n");
    m_entries.append(e);
  }
}

void CreditsDialog::setupUi() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(14);

  // Header banner
  auto *headerLayout = new QHBoxLayout();
  auto *headerIcon = new QLabel(this);
  headerIcon->setPixmap(AppIcon::pixmap(40, 40));

  auto *headerTextLayout = new QVBoxLayout();
  headerTextLayout->setSpacing(2);
  auto *title =
      new QLabel(QStringLiteral("Open Source & Third-Party Credits"), this);
  title->setStyleSheet(
      QStringLiteral("font-size: 18px; font-weight: 700; color: #8be9fd;"));
  auto *subTitle =
      new QLabel(QStringLiteral("Ensemble is powered by and built upon these "
                                "excellent open-source projects."),
                 this);
  subTitle->setStyleSheet(QStringLiteral("font-size: 12px; color: #6272a4;"));
  headerTextLayout->addWidget(title);
  headerTextLayout->addWidget(subTitle);

  headerLayout->addWidget(headerIcon);
  headerLayout->addLayout(headerTextLayout, 1);
  mainLayout->addLayout(headerLayout);

  // Search / Filter box
  m_searchEdit = new QLineEdit(this);
  m_searchEdit->setPlaceholderText(
      QStringLiteral("🔍 Filter libraries, authors, or licenses..."));
  m_searchEdit->setClearButtonEnabled(true);
  connect(m_searchEdit, &QLineEdit::textChanged, this,
          &CreditsDialog::onFilterChanged);
  mainLayout->addWidget(m_searchEdit);

  // Splitter: List on Left, Detail on Right
  auto *splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setHandleWidth(8);
  splitter->setStyleSheet(QStringLiteral(
      "QSplitter::handle { background-color: #282a36; border-radius: 4px; }"));

  // Left: List
  m_listWidget = new QListWidget(splitter);
  m_listWidget->setMinimumWidth(240);
  for (const CreditEntry &entry : m_entries) {
    auto *item = new QListWidgetItem(
        QStringLiteral("%1\n%2").arg(entry.name, entry.licenseType),
        m_listWidget);
    item->setData(Qt::UserRole, entry.name);
  }
  connect(m_listWidget, &QListWidget::itemSelectionChanged, this,
          &CreditsDialog::onItemSelectionChanged);

  // Right: Detail Panel
  auto *detailCard = new QFrame(splitter);
  detailCard->setObjectName(QStringLiteral("detailCard"));
  auto *detailLayout = new QVBoxLayout(detailCard);
  detailLayout->setContentsMargins(16, 16, 16, 16);
  detailLayout->setSpacing(10);

  // Top detail header
  auto *metaHeader = new QHBoxLayout();
  auto *metaTextLayout = new QVBoxLayout();
  metaTextLayout->setSpacing(2);

  m_titleLabel = new QLabel(detailCard);
  m_titleLabel->setStyleSheet(
      QStringLiteral("font-size: 16px; font-weight: 700; color: #f8f8f2;"));

  m_authorLabel = new QLabel(detailCard);
  m_authorLabel->setStyleSheet(
      QStringLiteral("font-size: 12px; color: #94a3b8;"));

  metaTextLayout->addWidget(m_titleLabel);
  metaTextLayout->addWidget(m_authorLabel);
  metaHeader->addLayout(metaTextLayout, 1);

  m_licenseBadge = new QLabel(detailCard);
  m_licenseBadge->setStyleSheet(QStringLiteral(
      "background-color: #282a36; color: #50fa7b; border: 1px solid #1c452b; "
      "border-radius: 6px; padding: 4px 10px; font-size: 11px; font-weight: "
      "600;"));
  metaHeader->addWidget(m_licenseBadge, 0, Qt::AlignTop);

  detailLayout->addLayout(metaHeader);

  m_descLabel = new QLabel(detailCard);
  m_descLabel->setStyleSheet(
      QStringLiteral("font-size: 12px; color: #bd93f9; line-height: 1.4;"));
  m_descLabel->setWordWrap(true);
  detailLayout->addWidget(m_descLabel);

  // License text view
  auto *licenseSectionLabel =
      new QLabel(QStringLiteral("LICENSE NOTICE"), detailCard);
  licenseSectionLabel->setStyleSheet(QStringLiteral(
      "font-size: 11px; font-weight: 700; color: #6272a4; margin-top: 4px;"));
  detailLayout->addWidget(licenseSectionLabel);

  m_licenseEdit = new QPlainTextEdit(detailCard);
  m_licenseEdit->setReadOnly(true);
  m_licenseEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  detailLayout->addWidget(m_licenseEdit, 1);

  // Button bar in detail card
  auto *detailButtonRow = new QHBoxLayout();
  m_copyNoticeLabel = new QLabel(detailCard);
  m_copyNoticeLabel->setStyleSheet(
      QStringLiteral("color: #50fa7b; font-size: 11px; font-weight: 600;"));
  m_copyNoticeLabel->hide();

  m_copyLicenseBtn =
      new QPushButton(QStringLiteral("📋 Copy License"), detailCard);
  connect(m_copyLicenseBtn, &QPushButton::clicked, this,
          &CreditsDialog::onCopyLicenseClicked);

  m_visitWebsiteBtn =
      new QPushButton(QStringLiteral("🌐 Visit Website"), detailCard);
  connect(m_visitWebsiteBtn, &QPushButton::clicked, this,
          &CreditsDialog::onVisitWebsiteClicked);

  detailButtonRow->addWidget(m_copyNoticeLabel);
  detailButtonRow->addStretch();
  detailButtonRow->addWidget(m_copyLicenseBtn);
  detailButtonRow->addWidget(m_visitWebsiteBtn);
  detailLayout->addLayout(detailButtonRow);

  splitter->addWidget(m_listWidget);
  splitter->addWidget(detailCard);
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 2);

  mainLayout->addWidget(splitter, 1);

  // Footer actions
  auto *footerRow = new QHBoxLayout();
  auto *footerNote =
      new QLabel(QStringLiteral("All trademarks and registered trademarks are "
                                "the property of their respective owners."),
                 this);
  footerNote->setStyleSheet(QStringLiteral("font-size: 11px; color: #6272a4;"));

  auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
  closeBtn->setObjectName(QStringLiteral("closeBtn"));
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

  footerRow->addWidget(footerNote);
  footerRow->addStretch();
  footerRow->addWidget(closeBtn);
  mainLayout->addLayout(footerRow);
}

void CreditsDialog::onFilterChanged(const QString &text) {
  const QString filter = text.trimmed().toLower();
  for (int i = 0; i < m_listWidget->count(); ++i) {
    QListWidgetItem *item = m_listWidget->item(i);
    const QString name = item->data(Qt::UserRole).toString();

    bool match = false;
    if (filter.isEmpty()) {
      match = true;
    } else {
      for (const CreditEntry &entry : m_entries) {
        if (entry.name == name) {
          if (entry.name.toLower().contains(filter) ||
              entry.author.toLower().contains(filter) ||
              entry.licenseType.toLower().contains(filter) ||
              entry.description.toLower().contains(filter)) {
            match = true;
          }
          break;
        }
      }
    }
    item->setHidden(!match);
  }
}

void CreditsDialog::onItemSelectionChanged() {
  QListWidgetItem *cur = m_listWidget->currentItem();
  if (!cur)
    return;

  const QString name = cur->data(Qt::UserRole).toString();
  for (const CreditEntry &entry : m_entries) {
    if (entry.name == name) {
      updateDetailView(entry);
      break;
    }
  }
}

void CreditsDialog::updateDetailView(const CreditEntry &entry) {
  m_titleLabel->setText(entry.name);
  m_authorLabel->setText(
      QStringLiteral("By %1 • Version %2").arg(entry.author, entry.version));
  m_licenseBadge->setText(entry.licenseType);
  m_descLabel->setText(entry.description);
  m_licenseEdit->setPlainText(entry.licenseText);
  m_copyNoticeLabel->hide();
}

void CreditsDialog::onCopyLicenseClicked() {
  QClipboard *clipboard = QApplication::clipboard();
  if (clipboard && m_licenseEdit) {
    clipboard->setText(m_licenseEdit->toPlainText());
    m_copyNoticeLabel->setText(
        QStringLiteral("✓ License copied to clipboard!"));
    m_copyNoticeLabel->show();
    QTimer::singleShot(2500, m_copyNoticeLabel, &QLabel::hide);
  }
}

void CreditsDialog::onVisitWebsiteClicked() {
  QListWidgetItem *cur = m_listWidget->currentItem();
  if (!cur)
    return;

  const QString name = cur->data(Qt::UserRole).toString();
  for (const CreditEntry &entry : m_entries) {
    if (entry.name == name) {
      if (!entry.websiteUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(entry.websiteUrl));
      }
      break;
    }
  }
}
