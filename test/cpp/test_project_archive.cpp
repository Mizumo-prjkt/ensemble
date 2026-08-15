#include "model/Ao3Project.h"
#include "model/ProjectSerializer.h"
#include "model/ZstdArchive.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>
#include <cassert>

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);

  QTemporaryDir tempDir;
  assert(tempDir.isValid());

  const QString projPath =
      tempDir.filePath(QStringLiteral("test_work.ao3proj"));

  // 1. Create a project
  Ao3Project originalProj;
  originalProj.setTitle(QStringLiteral("My Test AO3 Epic"));
  originalProj.setAuthor(QStringLiteral("TestAuthor"));
  originalProj.setWorkSkinCss(
      QStringLiteral(".custom-class { color: #bd93f9; font-weight: bold; }"));
  originalProj.setHistoryLog(QStringLiteral("Initial commit: created story."));

  originalProj.chapters().clear();
  Chapter ch1;
  ch1.setTitle(QStringLiteral("Chapter 1: The Gathering"));
  ch1.setOrder(1);
  ch1.setHtml(QStringLiteral("<p>It was a dark and stormy night...</p>"));
  originalProj.chapters().append(ch1);

  Chapter ch2;
  ch2.setTitle(QStringLiteral("Chapter 2: The Journey"));
  ch2.setOrder(2);
  ch2.setHtml(QStringLiteral("<p>They ventured into the unknown realm.</p>"));
  originalProj.chapters().append(ch2);

  // 2. Save project to .ao3proj archive
  QString saveError;
  bool saveOk = ProjectSerializer::save(projPath, originalProj, &saveError);
  if (!saveOk) {
    qCritical() << "Save failed:" << saveError;
    return 1;
  }
  qDebug() << "Successfully saved project archive to:" << projPath
           << "Size:" << QFile(projPath).size() << "bytes";
  assert(QFile::exists(projPath));
  assert(QFile(projPath).size() > 0);

  // 3. Verify format detection
  assert(ZstdArchive::isZstdArchive(projPath));

  // 4. Load project back from .ao3proj archive
  Ao3Project loadedProj;
  QString loadError;
  bool loadOk = ProjectSerializer::load(projPath, loadedProj, &loadError);
  if (!loadOk) {
    qCritical() << "Load failed:" << loadError;
    return 2;
  }

  // 5. Verify integrity
  assert(loadedProj.title() == originalProj.title());
  assert(loadedProj.author() == originalProj.author());
  assert(loadedProj.workSkinCss() == originalProj.workSkinCss());
  assert(loadedProj.historyLog() == originalProj.historyLog());
  assert(loadedProj.chapters().size() == 2);
  assert(loadedProj.chapters().at(0).title() == ch1.title());
  assert(loadedProj.chapters().at(0).html() == ch1.html());
  assert(loadedProj.chapters().at(1).title() == ch2.title());
  assert(loadedProj.chapters().at(1).html() == ch2.html());

  // 6. Test loading existing user deflate archive if present
  const QString realFilePath = QStringLiteral("./test/units/test1.ao3proj");
  if (QFile::exists(realFilePath)) {
    Ao3Project realProj;
    QString realError;
    bool realOk = ProjectSerializer::load(realFilePath, realProj, &realError);
    if (!realOk) {
      qCritical() << "Failed to load real-world deflate project:" << realError;
      return 3;
    }
    qDebug() << "✓ Real-world Deflate project loaded successfully:"
             << realProj.title() << "Chapters:" << realProj.chapters().size();
  }

  qDebug() << "✓ All project archive serialization & deserialization tests "
              "passed successfully!";
  return 0;
}
